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
    nvar_t *p = nvar;
    unsigned char i;
    for (i = 0; i < n_nvars; i++, p++)
        if (p->n0 == n0 && p->n1 == n1) return &p->v;
    if (n_nvars >= NVARS) { err(E_MEM); return &nvar->v; }
    p->n0 = n0; p->n1 = n1;
    p->v.b[0] = 0; p->v.b[1] = 0; p->v.b[2] = 0; p->v.b[3] = 0;
    n_nvars++;
    return &p->v;
}

/* ---- numeric arrays (1-D, bump-allocated float pool) --------------------------- */
typedef struct { char n0, n1; unsigned int base, nelem; } arr_t;
static arr_t arr[NARRS];
static unsigned char n_arrs;
#define apool ((num_t *)LR_APOOL)
static unsigned int apool_used;

static arr_t *arr_create(char n0, char n1, unsigned int nelem)
{
    arr_t *a;
    num_t *z;
    unsigned int i;
    if (n_arrs >= NARRS || apool_used + nelem > APOOL) { err(E_MEM); return 0; }
    a = &arr[n_arrs];
    a->n0 = n0; a->n1 = n1;
    a->base = apool_used; a->nelem = nelem;
    z = apool + apool_used;
    for (i = 0; i < nelem; i++, z++) {
        z->b[0] = 0; z->b[1] = 0; z->b[2] = 0; z->b[3] = 0;
    }
    apool_used += nelem;
    n_arrs++;
    return a;
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
    arr_t *a = arr;
    for (i = 0; ; i++, a++) {
        if (i >= n_arrs) { a = arr_create(n0, n1, 11); break; }
        if (a->n0 == n0 && a->n1 == n1) break;
    }
    if (!a) return apool;
    if (idx < 0 || (unsigned int)idx >= a->nelem) { err(E_SUBSC); return apool; }
    return apool + a->base + idx;
}

/* ---- string variables (fixed low-RAM slots: n0,n1,len + SSTR_CAP bytes) ---------- */
typedef struct { char n0, n1; unsigned char len; char d[SSTR_CAP]; } svar_t;
#define svar ((svar_t *)LR_SVAR)
static unsigned char n_svars;

static svar_t *svar_slot(const char *n2)
{
    svar_t *p = svar;
    unsigned char i;
    for (i = 0; i < n_svars; i++, p++)
        if (p->n0 == n2[0] && p->n1 == n2[1]) return p;
    if (n_svars >= SVARS) { err(E_MEM); return svar; }
    p->n0 = n2[0]; p->n1 = n2[1]; p->len = 0;
    n_svars++;
    return p;
}

void svar_get(const char *n2, val_t *v)
{
    svar_t *p = svar;
    unsigned char i;
    v->t = VT_STR; v->s = prog; v->sl = 0;          /* undefined -> "" */
    for (i = 0; i < n_svars; i++, p++)
        if (p->n0 == n2[0] && p->n1 == n2[1]) {
            v->s = p->d;
            v->sl = p->len;
            return;
        }
}

void svar_set(const char *n2, const val_t *v)
{
    svar_t *s = svar_slot(n2);
    unsigned char i, n = v->sl;
    if (g_err) return;
    if (n > SSTR_CAP) n = SSTR_CAP;                 /* silently clamp (documented) */
    for (i = 0; i < n; i++) s->d[i] = v->s[i];
    s->len = n;
}

/* ---- string / string-arg functions (called from primary; 1 = consumed) ----------- */
static unsigned char arg_open(void)
{
    sk();
    if (*ip != '(') { err(E_SYNTAX); return 0; }
    ip++;
    return 1;
}

static unsigned char arg_more(void)                  /* ',' between args */
{
    sk();
    if (*ip != ',') { err(E_SYNTAX); return 0; }
    ip++;
    return 1;
}

static unsigned char arg_close(void)
{
    sk();
    if (*ip != ')') { err(E_SYNTAX); return 0; }
    ip++;
    return 1;
}

static unsigned char eval_str(val_t *v)
{
    eval(v);
    if (g_err) return 0;
    if (v->t != VT_STR) { err(E_TYPE); return 0; }
    return 1;
}

unsigned char str_func(val_t *v)
{
    if (kw("CHR$")) {
        val_t a;
        char *d;
        if (!arg_open()) return 1;
        if (!eval_num(&a)) return 1;
        if (!arg_close()) return 1;
        d = strtmp_alloc(1);
        if (g_err) return 1;
        d[0] = (char)num_toi(&a.n);
        FCHK();
        v->t = VT_STR; v->s = d; v->sl = 1;
        return 1;
    }
    if (kw("STR$")) {
        val_t a;
        char b[16], *d;
        unsigned char n = 0;
        if (!arg_open()) return 1;
        if (!eval_num(&a)) return 1;
        if (!arg_close()) return 1;
        fmt_num(&a.n, b);
        while (b[n]) n++;
        d = strtmp_alloc(n);
        if (g_err) return 1;
        { unsigned char i; for (i = 0; i < n; i++) d[i] = b[i]; }
        v->t = VT_STR; v->s = d; v->sl = n;
        return 1;
    }
    if (kw("INKEY$")) {
        v->t = VT_STR; v->s = strtmp; v->sl = 0;
        if (pending_key) {
            char *d = strtmp_alloc(1);
            if (g_err) return 1;
            d[0] = (char)pending_key;
            pending_key = 0;
            v->s = d; v->sl = 1;
        }
        return 1;
    }
    if (kw("LEFT$") || kw("RIGHT$") || kw("MID$")) {
        /* the keyword is consumed; identify it by the letter 3 back from ip
           (past the '$'): LEFT$ -> 'F', RIGHT$ -> 'H', MID$ -> 'I' */
        char sel = ip[-3];
        val_t s, a;
        int i0, nn;
        if (sel >= 'a') sel = (char)(sel - 32);
        if (!arg_open()) return 1;
        if (!eval_str(&s)) return 1;
        if (!arg_more()) return 1;
        if (!eval_num(&a)) return 1;
        i0 = num_toi(&a.n);
        FCHK();
        v->t = VT_STR;
        if (sel == 'I') {                            /* MID$(s$, i [, n]) */
            nn = 255;
            sk();
            if (*ip == ',') {
                ip++;
                if (!eval_num(&a)) return 1;
                nn = num_toi(&a.n);
                FCHK();
            }
            if (!arg_close()) return 1;
            if (i0 < 1 || nn < 0) { err(E_IFC); return 1; }
            i0--;
            if (i0 >= s.sl) { v->s = strtmp; v->sl = 0; return 1; }
            if (nn > s.sl - i0) nn = s.sl - i0;
            v->s = s.s + i0;
            v->sl = (unsigned char)nn;
            return 1;
        }
        if (!arg_close()) return 1;                  /* LEFT$/RIGHT$(s$, n) */
        if (i0 < 0) { err(E_IFC); return 1; }
        if (i0 > s.sl) i0 = s.sl;
        v->s = (sel == 'H') ? s.s + (s.sl - i0) : s.s;
        v->sl = (unsigned char)i0;
        return 1;
    }
    if (kw("LEN")) {
        val_t s;
        if (!arg_open()) return 1;
        if (!eval_str(&s)) return 1;
        if (!arg_close()) return 1;
        v->t = VT_NUM;
        num_fromi(&v->n, (int)s.sl);
        return 1;
    }
    if (kw("ASC")) {
        val_t s;
        if (!arg_open()) return 1;
        if (!eval_str(&s)) return 1;
        if (!arg_close()) return 1;
        if (!s.sl) { err(E_IFC); return 1; }
        v->t = VT_NUM;
        num_fromi(&v->n, (unsigned char)s.s[0]);
        return 1;
    }
    if (kw("VAL")) {
        val_t s;
        char b[20];
        const char *p = b;
        unsigned char n;
        if (!arg_open()) return 1;
        if (!eval_str(&s)) return 1;
        if (!arg_close()) return 1;
        n = (s.sl < 19) ? s.sl : 19;
        { unsigned char i; for (i = 0; i < n; i++) b[i] = s.s[i]; }
        b[n] = 0;
        v->t = VT_NUM;
        if (f_in(&p)) { f_st(&v->n); FCHK(); }
        else num_fromi(&v->n, 0);
        return 1;
    }
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
    n_nvars = 0; n_arrs = 0; apool_used = 0; n_svars = 0;
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
    f = fors;
    for (i = 0; i < for_sp; i++, f++)
        if (f->n0 == n2[0] && f->n1 == n2[1]) { for_sp = i; break; }
    if (for_sp >= FORS) { err(E_MEM); return; }
    f = fors + for_sp;
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

/* ---- INPUT ------------------------------------------------------------------------ */
static const char *input_vars;         /* varlist position for input_store */

/* INPUT ["prompt" ;|,] var[,var...] - prints the prompt, remembers the varlist,
 * parks the interpreter in ST_INPUT; main.c's line editor calls input_store. */
static void st_input(void)
{
    sk();
    if (*ip == '"') {                   /* prompt literal */
        ip++;
        while (*ip && *ip != '"' && *ip != '\n') con_putc(*ip++);
        if (*ip == '"') ip++;
        sk();
        if (*ip == ';') { ip++; con_puts("? "); }
        else if (*ip == ',') ip++;
        else { err(E_SYNTAX); return; }
    } else {
        con_puts("? ");
    }
    sk();
    input_vars = ip;
    skip_stmt();                        /* ip -> statement end; editor resumes there */
    in_len = 0;
    g_state = ST_INPUT;
}

/* input_store: parse inbuf against the remembered varlist. 1 = ok, 0 = redo. */
unsigned char input_store(void)
{
    const char *save = ip;
    const char *bp = inbuf;
    unsigned char ok = 0;
    inbuf[in_len] = 0;
    ip = input_vars;
    for (;;) {
        char n2[2];
        unsigned char is_str;
        if (!get_ident(n2, &is_str)) goto out;
        while (*bp == ' ') bp++;
        if (is_str) {
            val_t v;
            const char *e;
            v.t = VT_STR;
            v.s = bp;
            while (*bp && *bp != ',') bp++;
            e = bp;
            while (e > v.s && e[-1] == ' ') e--;
            v.sl = (unsigned char)(e - v.s);
            svar_set(n2, &v);
        } else {
            num_t *slot = var_slot(n2[0], n2[1]);   /* plain vars only (no A(i)) */
            if (g_err) goto out;
            if (!f_in(&bp)) goto out;
            f_st(slot);
            FCHK();
            if (g_err) goto out;
        }
        sk();
        if (*ip == ',') {               /* more variables: need a comma in the input */
            ip++;
            while (*bp == ' ') bp++;
            if (*bp != ',') goto out;
            bp++;
            continue;
        }
        ok = 1;                         /* varlist done */
        break;
    }
out:
    g_err = 0;                          /* a redo, not a program error */
    fac_err = 0;
    ip = save;
    return ok;
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
            { "PRINT", st_print }, { "INPUT", st_input }, { "REM", skip_to_eol }, { "IF", st_if },
            { "FOR", st_for }, { "NEXT", st_next }, { "GOTO", st_goto },
            { "GOSUB", st_gosub }, { "RETURN", st_return }, { "LET", st_let },
            { "DIM", st_dim }, { "DATA", skip_stmt }, { "READ", st_read },
            { "RESTORE", st_restore }, { "RANDOMIZE", st_randomize },
            { "ELSE", skip_to_eol },   /* taken-THEN ran into the ELSE tail */
            { "STOP", st_stop }, { "END", finish_ok },
        };
        unsigned char i;
        for (i = 0; i < 18; i++)
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
