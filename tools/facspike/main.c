/* facspike - test app for the MBF float engine (fac.s), NOT shipped.
 *
 * Exercises parse/format/add/sub/mul/div/cmp/floor/toi/fromi/scale in a
 * GEOBENCH window; every line shows a label and the engine's f_out rendering.
 * Verified by screenshot against host-side expected values (tools/mbf.py).
 */
#include "gb.h"

typedef struct { unsigned char b[4]; } num_t;
extern void f_ld(const void *m);
extern void f_arg(const void *m);
extern void f_st(void *m);
extern void f_fac2arg(void);
extern void f_add(void);
extern void f_sub(void);
extern void f_mul(void);
extern void f_div(void);
extern signed char f_cmp(void);
extern signed char f_sgn(void);
extern void f_neg(void);
extern void f_scale(signed char k);
extern void f_floor(void);
extern int f_toi(void);
extern void f_fromi(int v);
extern unsigned char f_in(const char **pp);
extern void f_out(char *dst);
extern volatile unsigned char fac_err;

#define WX 4
#define WY 12
#define WW 72
#define WH 184

static char out[20];
static unsigned char row;
static num_t ta, tb;

static void show(const char *label)
{
    gb_text((unsigned char)(WX + 2), (unsigned char)(WY + 16 + row * 8), label);
    gb_text((unsigned char)(WX + 30), (unsigned char)(WY + 16 + row * 8), out);
    row++;
}

static void parse_to(num_t *dst, const char *s)
{
    const char *p = s;
    f_in(&p);
    f_st(dst);
}

static void bin_show(const char *label, const char *a, const char *b, unsigned char op)
{
    parse_to(&ta, a);
    parse_to(&tb, b);
    f_ld(&ta);
    f_arg(&tb);
    switch (op) {
        case 0: f_add(); break;
        case 1: f_sub(); break;
        case 2: f_mul(); break;
        case 3: f_div(); break;
    }
    f_out(out);
    show(label);
}

static void fmt_show(const char *label, const char *s)
{
    parse_to(&ta, s);
    f_ld(&ta);
    f_out(out);
    show(label);
}

static void hex_show(const char *label, const char *s)   /* packed FAC bytes */
{
    static const char hx[] = "0123456789ABCDEF";
    unsigned char i;
    parse_to(&ta, s);
    for (i = 0; i < 4; i++) {
        out[i * 2]     = hx[ta.b[3 - i] >> 4];
        out[i * 2 + 1] = hx[ta.b[3 - i] & 15];
    }
    out[8] = 0;
    show(label);
}

static void draw(void)
{
    row = 0;
    hex_show("X 9", "9");            /* expect 84 10 00 00 (exp mantHS mid lo) */
    hex_show("X 99", "99");          /* expect 87 46 00 00 */
    hex_show("X 9999999", "9999999");/* expect 98 18 96 7F */
    hex_show("X 25", "25");          /* expect 85 48 00 00 */
    hex_show("X 2.5", "2.5");        /* expect 82 20 00 00 */
    hex_show("X 1E9", "1E9");        /* expect 9E 6E 6B 28 */
    fmt_show("P 1", "1");
    fmt_show("P 3.141593", "3.141593");
    fmt_show("P .5", ".5");
    fmt_show("P -2.5", "-2.5");
    fmt_show("P 1E9", "1E9");
    fmt_show("P .0001", "0.0001");
    fmt_show("P 9999999", "9999999");
    bin_show("1/3", "1", "3", 3);
    bin_show("355/113", "355", "113", 3);
    bin_show(".1+.2", ".1", ".2", 0);
    bin_show("1-3", "1", "3", 1);
    bin_show("1.5*-4", "1.5", "-4", 2);
    bin_show("2E4*3E4", "20000", "30000", 2);
    /* floor(-2.5) */
    parse_to(&ta, "-2.5");
    f_ld(&ta);
    f_floor();
    f_out(out);
    show("FLR -2.5");
    /* toi(-2.5) then back */
    parse_to(&ta, "-2.5");
    f_ld(&ta);
    f_fromi(f_toi());
    f_out(out);
    show("TOI -2.5");
    /* scale: 1 * 2^-3 */
    parse_to(&ta, "1");
    f_ld(&ta);
    f_scale(-3);
    f_out(out);
    show("1>>3");
    /* cmp 2 vs 3 / 3 vs 3 / -1 vs -2 */
    parse_to(&ta, "2"); parse_to(&tb, "3");
    f_ld(&ta); f_arg(&tb);
    out[0] = (char)('0' + 1 + f_cmp()); out[1] = 0;   /* expect '0' (lt) */
    parse_to(&ta, "3");
    f_ld(&ta);
    out[1] = (char)('0' + 1 + f_cmp()); out[2] = 0;   /* expect '1' (eq) */
    parse_to(&ta, "-1"); parse_to(&tb, "-2");
    f_ld(&ta); f_arg(&tb);
    out[2] = (char)('0' + 1 + f_cmp()); out[3] = 0;   /* expect '2' (gt) */
    show("CMP LEG");
    /* fromi(-12345) */
    f_fromi(-12345);
    f_out(out);
    show("FI -12345");
    /* div by zero flag */
    parse_to(&ta, "5"); parse_to(&tb, "0");
    f_ld(&ta); f_arg(&tb);
    f_div();
    out[0] = (char)('0' + fac_err); out[1] = 0;
    fac_err = 0;
    show("DIV0 ERR");
}

static void proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  draw(); break;
        case GB_MSG_CLOSE: gb_wm_close(); break;
    }
}

static const gb_mwin_t mw = { WX, WY, WW, WH, 0, 0, proc, "FACSPIKE" };

void main(void)
{
    gb_wm_managed(&mw);
    gb_restore_parent();
}
