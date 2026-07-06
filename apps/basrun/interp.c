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
typedef struct { char n0, n1; float v; } nvar_t;
static nvar_t nvar[NVARS];
static unsigned char n_nvars;

float *var_slot(char n0, char n1)
{
    unsigned char i;
    for (i = 0; i < n_nvars; i++)
        if (nvar[i].n0 == n0 && nvar[i].n1 == n1) return &nvar[i].v;
    if (n_nvars >= NVARS) { err(E_MEM); return &nvar[0].v; }
    nvar[n_nvars].n0 = n0; nvar[n_nvars].n1 = n1; nvar[n_nvars].v = 0.0f;
    return &nvar[n_nvars++].v;
}

/* ---- numeric arrays (1-D, bump-allocated float pool) --------------------------- */
typedef struct { char n0, n1; unsigned int base, nelem; } arr_t;
static arr_t arr[NARRS];
static unsigned char n_arrs;
static float apool[APOOL];
static unsigned int apool_used;

static arr_t *arr_create(char n0, char n1, unsigned int nelem)
{
    unsigned int i;
    if (n_arrs >= NARRS || apool_used + nelem > APOOL) { err(E_MEM); return 0; }
    arr[n_arrs].n0 = n0; arr[n_arrs].n1 = n1;
    arr[n_arrs].base = apool_used; arr[n_arrs].nelem = nelem;
    for (i = 0; i < nelem; i++) apool[apool_used + i] = 0.0f;
    apool_used += nelem;
    return &arr[n_arrs++];
}

/* arr_slot: A(idx) - auto-DIM A(10) on first use, GW-style */
float *arr_slot(char n0, char n1, int idx)
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
            eval(&v);
            if (g_err) return;
            if (v.t != VT_NUM) { err(E_TYPE); return; }
            sk();
            if (*ip != ')') { err(E_SYNTAX); return; }
            ip++;
            if (v.n >= 1.0f) con_tab_to((unsigned char)(v.n - 1.0f));
        } else {
            val_t v;
            eval(&v);
            if (g_err) return;
            if (v.t == VT_STR) con_putsn(v.s, v.sl);
            else {
                fmt_num(v.n, b);
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
    float *slot;

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
        slot = arr_slot(n2[0], n2[1], (int)gb_floor(idx.n + 0.5f));
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

    if (kw("PRINT")) { st_print(); return; }
    sk();
    if (*ip == '?') { ip++; st_print(); return; }
    if (*ip == '\'') { skip_to_eol(); return; }
    if (kw("REM")) { skip_to_eol(); return; }
    if (kw("LET")) { st_let(); return; }
    if (kw("END")) { finish_ok(); return; }
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
