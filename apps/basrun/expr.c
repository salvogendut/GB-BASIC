/* expr.c - scanner + expression evaluator for BASRUN (precedence climbing).
 *
 * One recursive eval_bin keeps code size and Z80 stack depth down (we run on
 * the kernel's resident stack). Values are passed by pointer (val_t) - SDCC
 * struct-by-value is costly. String literals point INTO the program text
 * (zero copy). GW semantics: relationals yield -1/0; AND/OR/NOT/MOD work on
 * rounded 16-bit ints; ^ is right-associative with integer exponents only
 * (documented deviation: no fractional powers - keeps powf/expf/logf out).
 */
#include "basrun.h"
#include <math.h>

void sk(void)
{
    while (*ip == ' ' || *ip == '\t') ip++;
}

unsigned char at_end(void)
{
    sk();
    return (unsigned char)(*ip == 0 || *ip == ':' || *ip == '\n' || *ip == '\r');
}

static char up(char c)
{
    return (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
}

/* kw: case-insensitive keyword match at ip (skips leading spaces); requires a
 * word boundary after an alphabetic tail so "TOTAL" does not match "TO".
 * Advances ip past the keyword on a hit. */
unsigned char kw(const char *k)
{
    const char *p;
    char last = 0;
    sk();
    p = ip;
    while (*k) {
        if (up(*p) != *k) return 0;
        last = *k;
        p++; k++;
    }
    if (last >= 'A' && last <= 'Z') {
        char c = up(*p);
        if ((c >= 'A' && c <= 'Z') || (*p >= '0' && *p <= '9') || *p == '$' || *p == '.')
            return 0;
    }
    ip = p;
    return 1;
}

/* get_ident: read a variable name [A-Z][A-Z0-9]*[$]; the first two chars
 * (uppercased, second 0 if absent) are significant. 1 = an identifier read. */
unsigned char get_ident(char *n2, unsigned char *is_str)
{
    char c = up(*(sk(), ip));
    unsigned char pos = 0;
    if (c < 'A' || c > 'Z') return 0;
    n2[0] = c; n2[1] = 0; *is_str = 0;
    ip++;
    for (;;) {
        c = *ip;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            if (pos == 0) { n2[1] = c; pos = 1; }
            ip++;
        } else break;
    }
    if (*ip == '$') { *is_str = 1; ip++; }
    return 1;
}

/* ---- operators --------------------------------------------------------------- */
#define OP_OR  1
#define OP_AND 2
#define OP_EQ  3
#define OP_NE  4
#define OP_LT  5
#define OP_GT  6
#define OP_LE  7
#define OP_GE  8
#define OP_ADD 9
#define OP_SUB 10
#define OP_MUL 11
#define OP_DIV 12
#define OP_MOD 13
#define OP_POW 14

static const unsigned char PREC[15] = {
    0, 1, 2, 3, 3, 3, 3, 3, 3, 4, 4, 5, 5, 5, 6
};

static unsigned char get_op(void)
{
    char c;
    sk();
    c = *ip;
    if (c == '<') {
        ip++;
        if (*ip == '=') { ip++; return OP_LE; }
        if (*ip == '>') { ip++; return OP_NE; }
        return OP_LT;
    }
    if (c == '>') {
        ip++;
        if (*ip == '=') { ip++; return OP_GE; }
        return OP_GT;
    }
    if (c == '=') { ip++; return OP_EQ; }
    if (c == '+') { ip++; return OP_ADD; }
    if (c == '-') { ip++; return OP_SUB; }
    if (c == '*') { ip++; return OP_MUL; }
    if (c == '/') { ip++; return OP_DIV; }
    if (c == '^') { ip++; return OP_POW; }
    if (kw("OR"))  return OP_OR;
    if (kw("AND")) return OP_AND;
    if (kw("MOD")) return OP_MOD;
    return 0;
}

/* round a float to a 16-bit int (GW logical/MOD operand conversion) */
static int to_i16(float f)
{
    long l = (long)(f + (f >= 0.0f ? 0.5f : -0.5f));
    if (l < -32768L || l > 32767L) { err(E_OVF); return 0; }
    return (int)l;
}

static void ovf_check(float f)
{
    if (f > 3.402e38f || f < -3.402e38f) err(E_OVF);
}

static void num2(const val_t *a, const val_t *b)   /* both must be numeric */
{
    if (a->t != VT_NUM || b->t != VT_NUM) err(E_TYPE);
}

/* strcmp on (ptr,len) pairs -> <0 / 0 / >0 */
static signed char scmp(const val_t *a, const val_t *b)
{
    unsigned char i, n = (a->sl < b->sl) ? a->sl : b->sl;
    for (i = 0; i < n; i++) {
        if (a->s[i] != b->s[i])
            return (signed char)((unsigned char)a->s[i] < (unsigned char)b->s[i] ? -1 : 1);
    }
    if (a->sl == b->sl) return 0;
    return (signed char)(a->sl < b->sl ? -1 : 1);
}

/* string temp arena (concat / CHR$ / STR$ results live one statement) */
static char strtmp[STRTMP];
static unsigned char strtmp_used;

void strtmp_reset(void)
{
    strtmp_used = 0;
}

char *strtmp_alloc(unsigned char n)
{
    if ((unsigned int)strtmp_used + n > STRTMP) { err(E_STRSP); return strtmp; }
    { char *p = strtmp + strtmp_used; strtmp_used = (unsigned char)(strtmp_used + n); return p; }
}

static void apply(unsigned char op, val_t *a, const val_t *b)
{
    if (a->t == VT_STR || b->t == VT_STR) {
        signed char r;
        if (a->t != b->t) { err(E_TYPE); return; }
        if (op == OP_ADD) {                         /* string concatenation */
            unsigned int n = (unsigned int)a->sl + b->sl;
            char *d;
            if (n > 255) { err(E_STRSP); return; }
            d = strtmp_alloc((unsigned char)n);
            if (g_err) return;
            { unsigned char i;
              for (i = 0; i < a->sl; i++) d[i] = a->s[i];
              for (i = 0; i < b->sl; i++) d[a->sl + i] = b->s[i]; }
            a->s = d; a->sl = (unsigned char)n;
            return;
        }
        if (op < OP_EQ || op > OP_GE) { err(E_TYPE); return; }
        r = scmp(a, b);
        a->t = VT_NUM;
        switch (op) {
            case OP_EQ: a->n = (r == 0) ? -1.0f : 0.0f; break;
            case OP_NE: a->n = (r != 0) ? -1.0f : 0.0f; break;
            case OP_LT: a->n = (r <  0) ? -1.0f : 0.0f; break;
            case OP_GT: a->n = (r >  0) ? -1.0f : 0.0f; break;
            case OP_LE: a->n = (r <= 0) ? -1.0f : 0.0f; break;
            default:    a->n = (r >= 0) ? -1.0f : 0.0f; break;
        }
        return;
    }
    switch (op) {
    case OP_ADD: a->n = a->n + b->n; ovf_check(a->n); break;
    case OP_SUB: a->n = a->n - b->n; ovf_check(a->n); break;
    case OP_MUL: a->n = a->n * b->n; ovf_check(a->n); break;
    case OP_DIV:
        if (b->n == 0.0f) { err(E_DIV0); return; }
        a->n = a->n / b->n; ovf_check(a->n);
        break;
    case OP_MOD: {
        int x = to_i16(a->n), y = to_i16(b->n);
        if (g_err) return;
        if (y == 0) { err(E_DIV0); return; }
        a->n = (float)(x % y);
        break; }
    case OP_AND: case OP_OR: {
        int x = to_i16(a->n), y = to_i16(b->n);
        if (g_err) return;
        a->n = (float)(op == OP_AND ? (x & y) : (x | y));
        break; }
    case OP_POW: {
        float e = gb_floor(b->n), r = 1.0f, base = a->n;
        int ei;
        unsigned char neg = 0;
        if (e != b->n) { err(E_IFC); return; }      /* integer exponents only */
        if (e < -64.0f || e > 64.0f) { err(E_OVF); return; }
        ei = (int)e;
        if (ei < 0) { neg = 1; ei = -ei; }
        while (ei--) { r = r * base; ovf_check(r); if (g_err) return; }
        if (neg) {
            if (r == 0.0f) { err(E_DIV0); return; }
            r = 1.0f / r;
        }
        a->n = r;
        break; }
    default:                                        /* relationals */
        a->n = 0.0f - (float)(
            (op == OP_EQ) ? (a->n == b->n) :
            (op == OP_NE) ? (a->n != b->n) :
            (op == OP_LT) ? (a->n <  b->n) :
            (op == OP_GT) ? (a->n >  b->n) :
            (op == OP_LE) ? (a->n <= b->n) : (a->n >= b->n));
        break;
    }
    (void)num2;
}

/* ---- functions ----------------------------------------------------------------- */
static void expect(char c)
{
    sk();
    if (*ip == c) ip++;
    else err(E_SYNTAX);
}

static void eval_bin(val_t *lhs, unsigned char minprec);

static void arg_num(val_t *v)                       /* '(' numeric-expr ')' */
{
    expect('(');
    if (g_err) return;
    eval_bin(v, 1);
    if (g_err) return;
    if (v->t != VT_NUM) { err(E_TYPE); return; }
    expect(')');
}

static void primary(val_t *v)
{
    char c;
    v->t = VT_NUM; v->n = 0.0f;
    sk();
    c = *ip;
    if (c == '"') {                                  /* string literal (in prog text) */
        unsigned char n = 0;
        ip++;
        v->t = VT_STR; v->s = ip;
        while (*ip && *ip != '"' && *ip != '\n') { ip++; n++; }
        v->sl = n;
        if (*ip == '"') ip++;
        return;
    }
    if ((c >= '0' && c <= '9') || c == '.') {
        if (!parse_num(&ip, &v->n)) err(E_SYNTAX);
        return;
    }
    if (c == '(') {
        ip++;
        eval_bin(v, 1);
        if (g_err) return;
        expect(')');
        return;
    }
    if (c == '-') { ip++; eval_bin(v, PREC[OP_POW]); if (v->t != VT_NUM) { err(E_TYPE); return; } v->n = -v->n; return; }
    if (c == '+') { ip++; eval_bin(v, PREC[OP_POW]); if (v->t != VT_NUM) err(E_TYPE); return; }
    if (kw("NOT")) {
        int x;
        eval_bin(v, 3);
        if (g_err) return;
        if (v->t != VT_NUM) { err(E_TYPE); return; }
        x = to_i16(v->n);
        v->n = (float)(~x);
        return;
    }
    /* functions */
    if (kw("ABS")) { arg_num(v); if (v->n < 0.0f) v->n = -v->n; return; }
    if (kw("SGN")) { arg_num(v); v->n = (v->n > 0.0f) ? 1.0f : (v->n < 0.0f) ? -1.0f : 0.0f; return; }
    if (kw("INT")) { arg_num(v); v->n = gb_floor(v->n); return; }
    if (kw("SQR")) {
        arg_num(v);
        if (g_err) return;
        if (v->n < 0.0f) { err(E_IFC); return; }
        v->n = sqrtf(v->n);
        return;
    }
    if (kw("SIN")) { arg_num(v); v->n = sinf(v->n); return; }
    if (kw("COS")) { arg_num(v); v->n = cosf(v->n); return; }
    if (kw("TAN")) {
        float cs;
        arg_num(v);
        if (g_err) return;
        cs = cosf(v->n);
        if (cs == 0.0f) { err(E_OVF); return; }
        v->n = sinf(v->n) / cs;
        return;
    }
    if (kw("RND")) {
        sk();
        if (*ip == '(') {
            val_t a;
            arg_num(&a);
            if (g_err) return;
            if (a.n < 0.0f) { rnd_seed((unsigned int)(-a.n)); v->n = rnd_next(); }
            else if (a.n == 0.0f) v->n = rnd_repeat();
            else v->n = rnd_next();
        } else v->n = rnd_next();
        return;
    }
    if (str_func(v)) return;                        /* M4: string funcs (interp.c owns svars) */
    /* variable */
    {
        char n2[2];
        unsigned char is_str;
        if (!get_ident(n2, &is_str)) { err(E_SYNTAX); return; }
        sk();
        if (!is_str && *ip == '(') {                 /* array element */
            val_t idx;
            float *slot;
            ip++;
            eval_bin(&idx, 1);
            if (g_err) return;
            if (idx.t != VT_NUM) { err(E_TYPE); return; }
            expect(')');
            if (g_err) return;
            slot = arr_slot(n2[0], n2[1], (int)gb_floor(idx.n + 0.5f));
            if (g_err) return;
            v->n = *slot;
            return;
        }
        if (is_str) {
            svar_get(n2, v);                         /* string variable (M4) */
            return;
        }
        v->n = *var_slot(n2[0], n2[1]);
    }
}

static void eval_bin(val_t *lhs, unsigned char minprec)
{
    primary(lhs);
    if (g_err) return;
    for (;;) {
        const char *save = ip;
        unsigned char op = get_op();
        val_t rhs;
        if (!op || PREC[op] < minprec) { ip = save; return; }
        eval_bin(&rhs, (unsigned char)(op == OP_POW ? PREC[op] : PREC[op] + 1));
        if (g_err) return;
        apply(op, lhs, &rhs);
        if (g_err) return;
    }
}

void eval(val_t *v)
{
    eval_bin(v, 1);
}
