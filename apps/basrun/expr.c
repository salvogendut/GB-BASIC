/* expr.c - scanner + expression evaluator for BASRUN (precedence climbing).
 *
 * One recursive eval_bin keeps code size and Z80 stack depth down (we run on
 * the kernel's resident stack). Values are passed by pointer (val_t); numbers
 * are packed MBF handled entirely by the fac.s engine, so a numeric operation
 * here is f_ld/f_arg + one engine call + f_st. String literals point INTO the
 * program text (zero copy). GW semantics: relationals yield -1/0; AND/OR/NOT/
 * MOD work on rounded 16-bit ints; ^ is right-associative with integer
 * exponents only (documented deviation - keeps exp/log out of the bank).
 */
#include "basrun.h"

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

/* string temp arena (in low RAM - see basrun.h) */
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

/* rel_op: store the relational result (-1/0) for comparator outcome r */
static void rel_op(val_t *a, unsigned char op, signed char r)
{
    unsigned char t;
    switch (op) {
        case OP_EQ: t = (unsigned char)(r == 0); break;
        case OP_NE: t = (unsigned char)(r != 0); break;
        case OP_LT: t = (unsigned char)(r <  0); break;
        case OP_GT: t = (unsigned char)(r >  0); break;
        case OP_LE: t = (unsigned char)(r <= 0); break;
        default:    t = (unsigned char)(r >= 0); break;
    }
    a->t = VT_NUM;
    num_fromi(&a->n, t ? -1 : 0);
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
        rel_op(a, op, scmp(a, b));
        return;
    }
    switch (op) {
    case OP_ADD: f_ld(&a->n); f_arg(&b->n); f_add(); f_st(&a->n); break;
    case OP_SUB: f_ld(&a->n); f_arg(&b->n); f_sub(); f_st(&a->n); break;
    case OP_MUL: f_ld(&a->n); f_arg(&b->n); f_mul(); f_st(&a->n); break;
    case OP_DIV: f_ld(&a->n); f_arg(&b->n); f_div(); f_st(&a->n); break;
    case OP_MOD: {
        int x = num_toi(&a->n), y = num_toi(&b->n);
        if (fac_err) break;
        if (y == 0) { err(E_DIV0); return; }
        num_fromi(&a->n, x % y);
        break; }
    case OP_AND: case OP_OR: {
        int x = num_toi(&a->n), y = num_toi(&b->n);
        if (fac_err) break;
        num_fromi(&a->n, (int)(op == OP_AND ? (x & y) : (x | y)));
        break; }
    case OP_POW: {
        int e;
        unsigned char neg = 0;
        static const num_t one = {{0x00, 0x00, 0x00, 0x81}};
        f_ld(&b->n);
        f_floor();
        f_arg(&b->n);
        if (f_cmp() != 0) { err(E_IFC); return; }   /* fractional exponent */
        e = num_toi(&b->n);
        if (fac_err) break;
        if (e < 0) { neg = 1; e = -e; }
        f_ld(&one);                                 /* running product in FAC */
        f_arg(&a->n);                               /* base in ARG (f_mul preserves it) */
        while (e--) {
            f_mul();
            if (fac_err) break;
        }
        if (neg) {                                  /* 1/r */
            f_st(&a->n);
            f_ld(&one);
            f_arg(&a->n);
            f_div();
        }
        f_st(&a->n);
        break; }
    default:                                        /* numeric relationals */
        f_ld(&a->n);
        f_arg(&b->n);
        rel_op(a, op, f_cmp());
        break;
    }
    FCHK();
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

static void fn_abs(void) { if (f_sgn() < 0) f_neg(); }
static void fn_sgnv(void) { f_fromi(f_sgn()); }

/* fn1: parse '(x)' and leave x in FAC. 1 = ok. */
static unsigned char fn1(void)
{
    val_t v;
    arg_num(&v);
    if (g_err) return 0;
    f_ld(&v.n);
    return 1;
}

static void primary(val_t *v)
{
    char c;
    v->t = VT_NUM;
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
        if (!f_in(&ip)) err(E_SYNTAX);
        f_st(&v->n);
        FCHK();
        return;
    }
    if (c == '(') {
        ip++;
        eval_bin(v, 1);
        if (g_err) return;
        expect(')');
        return;
    }
    if (c == '-') {
        ip++;
        eval_bin(v, PREC[OP_POW]);
        if (g_err) return;
        if (v->t != VT_NUM) { err(E_TYPE); return; }
        f_ld(&v->n); f_neg(); f_st(&v->n);
        return;
    }
    if (c == '+') { ip++; eval_bin(v, PREC[OP_POW]); if (v->t != VT_NUM) err(E_TYPE); return; }
    if (kw("NOT")) {
        int x;
        eval_bin(v, 3);
        if (g_err) return;
        if (v->t != VT_NUM) { err(E_TYPE); return; }
        x = num_toi(&v->n);
        FCHK();
        num_fromi(&v->n, ~x);
        return;
    }
    /* numeric functions (FAC -> FAC), table-dispatched */
    {
        static const struct { const char *n; void (*f)(void); } NFN[] = {
            { "ABS", fn_abs }, { "SGN", fn_sgnv }, { "INT", f_floor },
            { "SQR", fn_sqr }, { "SIN", fn_sin }, { "COS", fn_cos },
            { "TAN", fn_tan },
        };
        unsigned char i;
        for (i = 0; i < 7; i++) {
            if (kw(NFN[i].n)) {
                if (fn1()) { NFN[i].f(); f_st(&v->n); FCHK(); }
                return;
            }
        }
    }
    if (kw("RND")) {
        sk();
        if (*ip == '(') {
            signed char s;
            if (!fn1()) return;
            s = f_sgn();
            if (s < 0) { f_neg(); rnd_seed((unsigned int)f_toi()); }
            rnd_fac(s);
        } else rnd_fac(1);
        f_st(&v->n);
        return;
    }
    if (str_func(v)) return;                        /* string functions (interp.c) */
    /* variable */
    {
        char n2[2];
        unsigned char is_str;
        if (!get_ident(n2, &is_str)) { err(E_SYNTAX); return; }
        sk();
        if (!is_str && *ip == '(') {                 /* array element */
            val_t idx;
            num_t *slot;
            ip++;
            eval_bin(&idx, 1);
            if (g_err) return;
            if (idx.t != VT_NUM) { err(E_TYPE); return; }
            expect(')');
            if (g_err) return;
            slot = arr_slot(n2[0], n2[1], num_toi(&idx.n));
            if (g_err) return;
            v->n = *slot;
            return;
        }
        if (is_str) {
            svar_get(n2, v);                         /* string variable */
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

/* eval_num: evaluate a numeric expression; 0 on error/type mismatch */
unsigned char eval_num(val_t *v)
{
    eval_bin(v, 1);
    if (g_err) return 0;
    if (v->t != VT_NUM) { err(E_TYPE); return 0; }
    return 1;
}
