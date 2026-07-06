/* interp.c - BASRUN statement dispatch, variable storage, error reporting.
 *
 * Direct interpretation from the program text (no tokenizer): exec_stmt reads
 * a keyword at ip against the statement table and runs its handler; a bare
 * identifier is an implied LET. Handlers leave ip at the statement separator
 * (':' / newline / NUL); exec_stmt steps over it on the next call.
 *
 * Errors: err(code) raises (first wins, poisoned values flow through the
 * evaluator); the frame loop calls report_error which prints GW-style
 * "<message> in <line>" and stops the program.
 */
#include "basrun.h"

/* ---- numeric scalars ---------------------------------------------------------- */
typedef struct { char n0, n1; num_t v; } nvar_t;
#define nvar ((nvar_t *)LR_NVAR)
static unsigned char n_nvars;

num_t *var_slot(char n0, char n1)
{
    unsigned char i;
    for (i = 0; i < n_nvars; i++)
        if (nvar[i].n0 == n0 && nvar[i].n1 == n1) return &nvar[i].v;
    if (n_nvars >= NVARS) { err(E_MEM); return &nvar[0].v; }
    nvar[n_nvars].n0 = n0; nvar[n_nvars].n1 = n1;
    nvar[n_nvars].v.b[0] = 0; nvar[n_nvars].v.b[1] = 0;
    nvar[n_nvars].v.b[2] = 0; nvar[n_nvars].v.b[3] = 0;
    return &nvar[n_nvars++].v;
}

/* ---- numeric arrays (1-D, bump-allocated float pool) --------------------------- */
typedef struct { char n0, n1; unsigned int base, nelem; } arr_t;
static arr_t arr[NARRS];
static unsigned char n_arrs;
#define apool ((num_t *)LR_APOOL)
static unsigned int apool_used;

static arr_t *arr_create(char n0, char n1, unsigned int nelem)
{
    unsigned int i;
    if (n_arrs >= NARRS || apool_used + nelem > APOOL) { err(E_MEM); return 0; }
    arr[n_arrs].n0 = n0; arr[n_arrs].n1 = n1;
    arr[n_arrs].base = apool_used; arr[n_arrs].nelem = nelem;
    for (i = 0; i < nelem; i++) {
        num_t *z = &apool[apool_used + i];
        z->b[0] = 0; z->b[1] = 0; z->b[2] = 0; z->b[3] = 0;
    }
    apool_used += nelem;
    return &arr[n_arrs++];
}

/* arr_dim: DIM A(n) - explicit creation; a second DIM is a Duplicate Definition */
void arr_dim(char n0, char n1, unsigned int nelem)
{
    unsigned char i;
    for (i = 0; i < n_arrs; i++)
        if (arr[i].n0 == n0 && arr[i].n1 == n1) { err(E_DUPDEF); return; }
    arr_create(n0, n1, nelem);
}

/* arr_slot: A(idx) - auto-DIM A(10) on first use, GW-style */
num_t *arr_slot(char n0, char n1, int idx)
{
    unsigned char i;
    arr_t *a = 0;
    for (i = 0; i < n_arrs; i++)
        if (arr[i].n0 == n0 && arr[i].n1 == n1) { a = &arr[i]; break; }
    if (!a) a = arr_create(n0, n1, 11);
    if (!a) return &apool[0];
    if (idx < 0 || (unsigned int)idx >= a->nelem) { err(E_SUBSC); return &apool[0]; }
    return &apool[a->base + idx];
}

/* ---- string variables (M4 fills these in) --------------------------------------- */
void svar_get(const char *n2, val_t *v)
{
    (void)n2;
    v->t = VT_STR; v->s = prog; v->sl = 0;          /* undefined -> "" */
}

unsigned char str_func(val_t *v)                     /* string functions arrive in M4 */
{
    (void)v;
    return 0;
}

/* ---- control-flow state ------------------------------------------------------------ */
typedef struct {
    char n0, n1;                 /* loop variable */
    num_t limit, step;
    const char *body;            /* resume point just after the FOR statement */
    unsigned int line;
} for_t;
#define fors ((for_t *)LR_FORS)
static unsigned char for_sp;

typedef struct { const char *rip; unsigned int rline; } gosub_t;
#define gosubs ((gosub_t *)LR_GOSUB)
static unsigned char gosub_sp;

static const char *data_ip;      /* next DATA item (0 = scan from data_scan_from) */
static const char *data_scan_from;

/* ---- run control ------------------------------------------------------------------ */
static void read_line_no(void)
{
    sk();
    if (*ip >= '0' && *ip <= '9') {
        unsigned int n = 0;
        while (*ip >= '0' && *ip <= '9') { n = n * 10 + (unsigned char)(*ip - '0'); ip++; }
        cur_line = n;
    }
}

void run_reset(void)
{
    n_nvars = 0; n_arrs = 0; apool_used = 0;
    for_sp = 0; gosub_sp = 0;
    data_ip = 0; data_scan_from = prog;
    g_err = 0; cur_line = 0;
    ip = prog;
    read_line_no();
}

/* find_line: seek a line number (forward from the current line when the target
 * is ahead - the GW "current line cache"; else from the top). 0 = not found. */
const char *find_line(unsigned int no)
{
    const char *p = prog;
    unsigned int n;
    if (no > cur_line && cur_line) {                 /* ahead: scan from here */
        p = ip;
        while (*p && *p != '\n') p++;                /* to the end of this line */
        if (*p) p++;
    }
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0) {
            if (p != prog && no <= cur_line) { p = prog; continue; }  /* wrap once */
            return 0;
        }
        n = 0;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (unsigned char)(*p - '0'); p++; }
        if (n == no) return p;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
}

static void finish_ok(void)
{
    if (con_col) con_nl();
    con_puts("Ok");
    con_nl();
    g_state = ST_END;
}

static void puts_u16(unsigned int n)
{
    char b[5];
    unsigned char i = 0;
    if (!n) { con_putc('0'); return; }
    while (n) { b[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) con_putc(b[--i]);
}

static const char *const err_msg[] = {
    0,
    "Syntax error",
    "Undefined line number",
    "Type mismatch",
    "Division by zero",
    "Overflow",
    "Subscript out of range",
    "RETURN without GOSUB",
    "NEXT without FOR",
    "Out of DATA",
    "Out of memory",
    "Out of string space",
    "Illegal function call",
    "Duplicate Definition",
};

void report_error(void)
{
    if (con_col) con_nl();
    con_puts(err_msg[g_err]);
    if (cur_line) { con_puts(" in "); puts_u16(cur_line); }
    con_nl();
    g_err = 0;
    g_state = ST_END;
}

/* ---- statements ----------------------------------------------------------------- */
static void skip_to_eol(void)
{
    while (*ip && *ip != '\n') ip++;
}

/* parse_u16: read a plain line number at ip. 1 = got one. */
static unsigned char parse_u16(unsigned int *out)
{
    unsigned int n = 0;
    unsigned char any = 0;
    sk();
    while (*ip >= '0' && *ip <= '9') {
        n = n * 10 + (unsigned char)(*ip - '0');
        ip++; any = 1;
    }
    *out = n;
    return any;
}

/* jump to a line number (GOTO/GOSUB/THEN n) */
static void goto_line(unsigned int n)
{
    const char *p = find_line(n);
    if (!p) { err(E_ULINE); return; }
    ip = p;
    cur_line = n;
}

/* truth of a numeric expression at ip */
static unsigned char eval_truth(void)
{
    val_t v;
    if (!eval_num(&v)) return 0;
    f_ld(&v.n);
    return (unsigned char)(f_sgn() != 0);
}

static void st_goto(void)
{
    unsigned int n;
    if (!parse_u16(&n)) { err(E_SYNTAX); return; }
    goto_line(n);
}

static void st_gosub(void)
{
    unsigned int n;
    if (!parse_u16(&n)) { err(E_SYNTAX); return; }
    if (gosub_sp >= GOSUBS) { err(E_MEM); return; }
    gosubs[gosub_sp].rip = ip;             /* at the separator after the number */
    gosubs[gosub_sp].rline = cur_line;
    gosub_sp++;
    goto_line(n);
}

static void st_return(void)
{
    if (!gosub_sp) { err(E_RETWG); return; }
    gosub_sp--;
    ip = gosubs[gosub_sp].rip;
    cur_line = gosubs[gosub_sp].rline;
}

/* skip one statement (to ':' / EOL), respecting string literals */
static void skip_stmt(void)
{
    for (;;) {
        char c = *ip;
        if (c == 0 || c == '\n' || c == ':') return;
        ip++;
        if (c == '"') {
            while (*ip && *ip != '"' && *ip != '\n') ip++;
            if (*ip == '"') ip++;
        }
    }
}

/* IF e THEN {line|stmts} [ELSE {line|stmts}]  |  IF e GOTO line [ELSE ...] */
static void st_if(void)
{
    unsigned char t = eval_truth();
    unsigned int n;
    if (g_err) return;
    if (kw("GOTO")) {
        if (t) { st_goto(); return; }
    } else if (kw("THEN")) {
        if (t) {
            if (parse_u16(&n)) { goto_line(n); return; }
            return;                        /* execute the inline statements */
        }
    } else { err(E_SYNTAX); return; }
    /* condition false: scan this line for a top-level ELSE (it needs no ':'
     * before it - IF X THEN PRINT "A" ELSE PRINT "B"), else skip to EOL */
    {
        unsigned char bound = 1;           /* at a token boundary? */
        for (;;) {
            char c = *ip;
            if (c == 0 || c == '\n') return;
            if (c == ' ' || c == '\t' || c == ':') { ip++; bound = 1; continue; }
            if (c == '"') {                /* string literal: skip whole */
                ip++;
                while (*ip && *ip != '"' && *ip != '\n') ip++;
                if (*ip == '"') ip++;
                bound = 1;
                continue;
            }
            if (bound && kw("ELSE")) {
                if (parse_u16(&n)) { goto_line(n); return; }
                return;                    /* execute the ELSE statements */
            }
            bound = (unsigned char)!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                     (c >= '0' && c <= '9') || c == '$' || c == '.');
            ip++;
        }
    }
}

static void st_for(void)
{
    char n2[2];
    unsigned char is_str, i;
    num_t *slot;
    val_t v;
    for_t *f;

    if (!get_ident(n2, &is_str) || is_str) { err(E_SYNTAX); return; }
    slot = var_slot(n2[0], n2[1]);
    sk();
    if (*ip != '=') { err(E_SYNTAX); return; }
    ip++;
    if (!eval_num(&v)) return;
    *slot = v.n;
    if (!kw("TO")) { err(E_SYNTAX); return; }
    /* GW: re-using a loop variable discards the old frame (and any inner ones) */
    for (i = 0; i < for_sp; i++)
        if (fors[i].n0 == n2[0] && fors[i].n1 == n2[1]) { for_sp = i; break; }
    if (for_sp >= FORS) { err(E_MEM); return; }
    f = &fors[for_sp];
    if (!eval_num(&v)) return;
    f->limit = v.n;
    if (kw("STEP")) {
        if (!eval_num(&v)) return;
        f->step = v.n;
    } else {
        num_fromi(&f->step, 1);
    }
    f->n0 = n2[0]; f->n1 = n2[1];
    f->body = ip;                          /* at the separator after FOR */
    f->line = cur_line;
    for_sp++;
}

static void st_next(void)
{
    char n2[2];
    unsigned char is_str;
    const char *save = ip;
    for_t *f;
    num_t *slot;
    signed char ssgn, c;

    if (!get_ident(n2, &is_str)) { ip = save; n2[0] = 0; }  /* bare NEXT */
    for (;;) {
        if (!for_sp) { err(E_NEXTWF); return; }
        f = &fors[for_sp - 1];
        if (n2[0] == 0 || (f->n0 == n2[0] && f->n1 == n2[1])) break;
        for_sp--;                          /* GW: NEXT J pops unmatched inner loops */
    }
    slot = var_slot(f->n0, f->n1);
    f_ld(&f->step);
    ssgn = f_sgn();
    f_ld(slot);
    f_arg(&f->step);
    f_add();
    f_st(slot);
    FCHK();
    if (g_err) return;
    f_arg(&f->limit);
    c = f_cmp();
    if (ssgn >= 0 ? (c <= 0) : (c >= 0)) {
        ip = f->body;                      /* loop again */
        cur_line = f->line;
    } else {
        for_sp--;                          /* done */
    }
}

static void st_dim(void)
{
    char n2[2];
    unsigned char is_str;
    val_t v;
    for (;;) {
        if (!get_ident(n2, &is_str) || is_str) { err(E_SYNTAX); return; }
        sk();
        if (*ip != '(') { err(E_SYNTAX); return; }
        ip++;
        if (!eval_num(&v)) return;
        sk();
        if (*ip != ')') { err(E_SYNTAX); return; }
        ip++;
        {
            int n = num_toi(&v.n);
            FCHK();
            if (g_err) return;
            if (n < 0) { err(E_SUBSC); return; }
            arr_dim(n2[0], n2[1], (unsigned int)(n + 1));
            if (g_err) return;
        }
        sk();
        if (*ip != ',') return;
        ip++;
    }
}

/* ---- DATA / READ / RESTORE ---------------------------------------------------------- */

/* data_scan: find the first item of the next DATA statement at/after p (0 = none) */
static const char *data_scan(const char *p)
{
    const char *q = p;
    unsigned char linestart = 1;
    for (;;) {
        if (linestart) {                    /* skip spaces + the line number */
            while (*q == ' ' || *q == '\t') q++;
            while (*q >= '0' && *q <= '9') q++;
            linestart = 0;
        }
        while (*q == ' ' || *q == '\t') q++;
        if (*q == 0) return 0;
        if (*q == '\n') { q++; linestart = 1; continue; }
        if (*q == ':') { q++; continue; }
        /* statement start: DATA? */
        if ((q[0] == 'D' || q[0] == 'd') && (q[1] == 'A' || q[1] == 'a') &&
            (q[2] == 'T' || q[2] == 't') && (q[3] == 'A' || q[3] == 'a')) {
            q += 4;
            while (*q == ' ' || *q == '\t') q++;
            return q;
        }
        /* not DATA: skip this statement (respect quotes) */
        for (;;) {
            char c = *q;
            if (c == 0 || c == '\n' || c == ':') break;
            q++;
            if (c == '"') {
                while (*q && *q != '"' && *q != '\n') q++;
                if (*q == '"') q++;
            }
        }
    }
}

/* read one DATA item into v (typed by is_str); advances the DATA pointer */
static void data_item(val_t *v, unsigned char is_str)
{
    const char *p = data_ip;
    if (!p) {
        p = data_scan(data_scan_from);
        if (!p) { err(E_ODATA); return; }
    }
    while (*p == ' ' || *p == '\t') p++;
    if (is_str) {
        v->t = VT_STR;
        if (*p == '"') {
            p++;
            v->s = p;
            v->sl = 0;
            while (*p && *p != '"' && *p != '\n') { p++; v->sl++; }
            if (*p == '"') p++;
        } else {
            const char *e;
            v->s = p;
            while (*p && *p != ',' && *p != ':' && *p != '\n') p++;
            e = p;
            while (e > v->s && (e[-1] == ' ' || e[-1] == '\t')) e--;
            v->sl = (unsigned char)(e - v->s);
        }
    } else {
        v->t = VT_NUM;
        if (!f_in(&p)) { err(E_SYNTAX); return; }
        f_st(&v->n);
        FCHK();
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p == ',') {
        data_ip = p + 1;                   /* next item in this statement */
    } else {
        data_ip = 0;                       /* scan for the next DATA from here */
        data_scan_from = p;
    }
}

static void st_read(void)
{
    char n2[2];
    unsigned char is_str;
    val_t v;
    for (;;) {
        if (!get_ident(n2, &is_str)) { err(E_SYNTAX); return; }
        if (is_str) {
            data_item(&v, 1);
            if (g_err) return;
            svar_set(n2, &v);
        } else {
            num_t *slot;
            sk();
            if (*ip == '(') {
                val_t idx;
                ip++;
                eval(&idx);
                if (g_err) return;
                sk();
                if (*ip != ')') { err(E_SYNTAX); return; }
                ip++;
                slot = arr_slot(n2[0], n2[1], num_toi(&idx.n));
            } else slot = var_slot(n2[0], n2[1]);
            if (g_err) return;
            data_item(&v, 0);
            if (g_err) return;
            *slot = v.n;
        }
        sk();
        if (*ip != ',') return;
        ip++;
    }
}

static void st_restore(void)
{
    unsigned int n;
    data_ip = 0;
    if (parse_u16(&n)) {
        const char *p = find_line(n);
        if (!p) { err(E_ULINE); return; }
        data_scan_from = p;
    } else {
        data_scan_from = prog;
    }
}

static void st_randomize(void)
{
    val_t v;
    if (at_end()) { rnd_seed(frame_ctr); return; }
    if (!eval_num(&v)) return;
    rnd_seed((unsigned int)num_toi(&v.n));
    FCHK();
}

static void st_stop(void)
{
    if (con_col) con_nl();
    con_puts("Break in ");
    puts_u16(cur_line);
    con_nl();
    g_state = ST_END;
}

static void st_print(void)
{
    unsigned char nl = 1;
    char b[16];
    for (;;) {
        sk();
        if (at_end()) break;
        nl = 1;
        if (kw("TAB")) {
            val_t v;
            sk();
            if (*ip != '(') { err(E_SYNTAX); return; }
            ip++;
            if (!eval_num(&v)) return;
            sk();
            if (*ip != ')') { err(E_SYNTAX); return; }
            ip++;
            { int t = num_toi(&v.n);
              FCHK();
              if (t >= 1) con_tab_to((unsigned char)(t - 1)); }
        } else {
            val_t v;
            eval(&v);
            if (g_err) return;
            if (v.t == VT_STR) con_putsn(v.s, v.sl);
            else {
                fmt_num(&v.n, b);
                con_puts(b);
                con_putc(' ');                       /* GW: numbers get a trailing space */
            }
        }
        sk();
        if (*ip == ';') { ip++; nl = 0; continue; }
        if (*ip == ',') { ip++; con_tab_zone(); nl = 0; continue; }
        break;
    }
    if (nl) con_nl();
}

static void st_let(void)
{
    char n2[2];
    unsigned char is_str;
    num_t *slot;

    if (!get_ident(n2, &is_str)) { err(E_SYNTAX); return; }
    sk();
    if (is_str) {
        val_t v;
        if (*ip != '=') { err(E_SYNTAX); return; }
        ip++;
        eval(&v);
        if (g_err) return;
        if (v.t != VT_STR) { err(E_TYPE); return; }
        svar_set(n2, &v);
        return;
    }
    if (*ip == '(') {                                /* array element target */
        val_t idx;
        ip++;
        eval(&idx);
        if (g_err) return;
        if (idx.t != VT_NUM) { err(E_TYPE); return; }
        sk();
        if (*ip != ')') { err(E_SYNTAX); return; }
        ip++;
        slot = arr_slot(n2[0], n2[1], num_toi(&idx.n));
    } else {
        slot = var_slot(n2[0], n2[1]);
    }
    if (g_err) return;
    sk();
    if (*ip != '=') { err(E_SYNTAX); return; }
    ip++;
    {
        val_t v;
        eval(&v);
        if (g_err) return;
        if (v.t != VT_NUM) { err(E_TYPE); return; }
        *slot = v.n;
    }
}

void svar_set(const char *n2, const val_t *v)        /* M4 fills this in */
{
    (void)n2; (void)v;
    err(E_TYPE);
}

/* ---- exec_stmt: one statement at ip ----------------------------------------------- */
void exec_stmt(void)
{
    sk();
    while (*ip == ':') { ip++; sk(); }
    if (*ip == '\r') { ip++; return; }
    if (*ip == '\n') { ip++; read_line_no(); return; }
    if (*ip == 0) { finish_ok(); return; }

    sk();
    if (*ip == '?') { ip++; st_print(); return; }
    if (*ip == '\'') { skip_to_eol(); return; }
    {
        static const struct { const char *n; void (*f)(void); } STMT[] = {
            { "PRINT", st_print }, { "REM", skip_to_eol }, { "IF", st_if },
            { "FOR", st_for }, { "NEXT", st_next }, { "GOTO", st_goto },
            { "GOSUB", st_gosub }, { "RETURN", st_return }, { "LET", st_let },
            { "DIM", st_dim }, { "DATA", skip_stmt }, { "READ", st_read },
            { "RESTORE", st_restore }, { "RANDOMIZE", st_randomize },
            { "ELSE", skip_to_eol },   /* taken-THEN ran into the ELSE tail */
            { "STOP", st_stop }, { "END", finish_ok },
        };
        unsigned char i;
        for (i = 0; i < 17; i++)
            if (kw(STMT[i].n)) { STMT[i].f(); return; }
    }
    /* implied LET (bare identifier) */
    {
        const char *save = ip;
        char n2[2];
        unsigned char is_str;
        if (get_ident(n2, &is_str)) {
            ip = save;
            st_let();
            return;
        }
    }
    err(E_SYNTAX);
}
