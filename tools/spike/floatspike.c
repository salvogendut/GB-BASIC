/* floatspike - M0 size/behavior spike for GB-BASIC (NOT shipped).
 *
 * Proves, inside a real GEOBENCH managed window in the #4000 bank under
 * --fomit-frame-pointer on the kernel's resident stack:
 *   - SDCC software float: + - * / compare, int<->float conversions
 *   - sinf/cosf (the sincosf pull), sqrtf
 *   - a first cut of the GW-style number formatter (becomes val.c)
 *   - setjmp/longjmp from z80.lib (the documented error-handling fallback)
 * The build's fit-check prints the _CODE cost of all of the above — the M0 gate.
 */
#include "gb.h"
#include <math.h>
#include <setjmp.h>

#define WX 6
#define WY 20
#define WW 68
#define WH 150

static jmp_buf jb;
static char out[16];

/* --- first-cut GW-style float formatter (draft of val.c fmt_num) ---------- */
static const float p10[] = { 1e6f, 1e5f, 1e4f, 1e3f, 1e2f, 1e1f, 1e0f };

static void fmt_num(float f, char *dst)
{
    unsigned char i = 0, j, ndig, dig[7];
    signed char e10 = 0;
    unsigned long d;

    if (f < 0.0f) { dst[i++] = '-'; f = -f; }
    else dst[i++] = ' ';
    if (f == 0.0f) { dst[i++] = '0'; dst[i] = 0; return; }
    /* scale mantissa into [1e6, 1e7) => 7 significant digits */
    while (f >= 1e7f)  { f = f / 10.0f; e10++; }
    while (f < 1e6f)   { f = f * 10.0f; e10--; }
    d = (unsigned long)(f + 0.5f);
    if (d >= 10000000UL) { d /= 10; e10++; }
    for (j = 7; j; j--) { dig[j - 1] = (unsigned char)(d % 10); d /= 10; }
    ndig = 7;
    while (ndig > 1 && dig[ndig - 1] == 0) ndig--;
    e10 = (signed char)(e10 + 6);                 /* now: value = d[0].d[1..] * 10^e10 */
    if (e10 >= -2 && e10 <= 6) {                  /* fixed notation */
        signed char pt = (signed char)(e10 + 1);  /* digits before the point */
        if (pt <= 0) {                            /* .0*ddd */
            dst[i++] = '.';
            while (pt < 0) { dst[i++] = '0'; pt++; }
            for (j = 0; j < ndig; j++) dst[i++] = (char)('0' + dig[j]);
        } else {
            for (j = 0; j < ndig || j < (unsigned char)pt; j++) {
                if (j == (unsigned char)pt) dst[i++] = '.';
                dst[i++] = (char)(j < ndig ? '0' + dig[j] : '0');
            }
        }
    } else {                                      /* d.ddddddE+xx */
        dst[i++] = (char)('0' + dig[0]);
        if (ndig > 1) {
            dst[i++] = '.';
            for (j = 1; j < ndig; j++) dst[i++] = (char)('0' + dig[j]);
        }
        dst[i++] = 'E';
        if (e10 < 0) { dst[i++] = '-'; e10 = (signed char)-e10; }
        else dst[i++] = '+';
        dst[i++] = (char)('0' + e10 / 10);
        dst[i++] = (char)('0' + e10 % 10);
    }
    dst[i] = 0;
}

static unsigned char row;
static void show(const char *label, float f)
{
    fmt_num(f, out);
    gb_text(WX + 2, (unsigned char)(WY + 16 + row * 10), label);
    gb_text(WX + 26, (unsigned char)(WY + 16 + row * 10), out);
    row++;
}

static void draw(void)
{
    volatile float a = 1.0f, b = 3.0f;   /* volatile: force runtime float ops */
    volatile int n = 7;
    row = 0;
    show("1/3", a / b);
    show("355/113", 355.0f / 113.0f);
    show("SQR 2", sqrtf(2.0f));
    show("SIN .5", sinf(0.5f));
    show("COS .5", cosf(0.5f));
    show("7*1.5", (float)n * 1.5f);
    show("INT -2.5", (float)(int)(-2.5f));
    show("1E9", 1e9f);
    show("CMP", (a / b > 0.333f && a / b < 0.334f) ? 1.0f : -999.0f);
    if (setjmp(jb) == 0) {
        longjmp(jb, 7);
        show("JMP", -111.0f);            /* must NOT appear */
    } else {
        show("JMP OK", 42.0f);           /* must appear as 42 */
    }
}

static void proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  draw(); break;
        case GB_MSG_CLOSE: gb_wm_close(); break;
    }
}

static const gb_mwin_t mw = { WX, WY, WW, WH, 0, 0, proc, "FLOATSPIKE" };

void main(void)
{
    gb_wm_managed(&mw);
    gb_restore_parent();
}
