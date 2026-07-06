/* val.c - number <-> text conversion (GW-BASIC style) and RND, for BASRUN.
 *
 * fmt_num follows GW-BASIC single-precision PRINT rules:
 *   - leading ' ' for non-negatives, '-' for negatives (PRINT and STR$ alike)
 *   - up to 7 significant digits, trailing zeros stripped
 *   - fixed notation while the exponent fits (.001 .. 9999999), no leading 0
 *     before the point (".5"), integers come out bare ("42")
 *   - otherwise d.ddddddE+xx
 */
#include "basrun.h"

void fmt_num(float f, char *dst)
{
    unsigned char i = 0, j, ndig;
    unsigned char dig[7];
    signed char e10 = 0;
    unsigned long d;

    if (f < 0.0f) { dst[i++] = '-'; f = -f; }
    else dst[i++] = ' ';
    if (f == 0.0f) { dst[i++] = '0'; dst[i] = 0; return; }
    while (f >= 1e7f) { f = f / 10.0f; e10++; }     /* mantissa -> [1e6, 1e7) */
    while (f < 1e6f)  { f = f * 10.0f; e10--; }
    d = (unsigned long)(f + 0.5f);
    if (d >= 10000000UL) { d /= 10; e10++; }
    for (j = 7; j; j--) { dig[j - 1] = (unsigned char)(d % 10); d /= 10; }
    ndig = 7;
    while (ndig > 1 && dig[ndig - 1] == 0) ndig--;
    e10 = (signed char)(e10 + 6);                   /* value = dig[0].dig[1..] * 10^e10 */
    if (e10 >= -3 && e10 <= 6) {                    /* fixed */
        signed char pt = (signed char)(e10 + 1);    /* digits before the point */
        if (pt <= 0) {
            dst[i++] = '.';
            while (pt < 0) { dst[i++] = '0'; pt++; }
            for (j = 0; j < ndig; j++) dst[i++] = (char)('0' + dig[j]);
        } else {
            for (j = 0; j < ndig || j < (unsigned char)pt; j++) {
                if (j == (unsigned char)pt) dst[i++] = '.';
                dst[i++] = (char)(j < ndig ? '0' + dig[j] : '0');
            }
        }
    } else {                                        /* d.ddddddE+xx */
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

/* parse_num: read a number at *p (digits [.digits] [E[+/-]nn]); advances *p.
 * Returns 1 if a number was read. Shared by literals, VAL, INPUT and READ. */
unsigned char parse_num(const char **p, float *out)
{
    const char *s = *p;
    float v = 0.0f, div = 1.0f;
    unsigned char any = 0, neg = 0;
    signed char e = 0;

    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s - '0'); s++; any = 1; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            div = div * 10.0f;
            v = v + (float)(*s - '0') / div;
            s++; any = 1;
        }
    }
    if (!any) return 0;
    if ((*s == 'E' || *s == 'e') &&
        (s[1] == '+' || s[1] == '-' || (s[1] >= '0' && s[1] <= '9'))) {
        unsigned char eneg = 0;
        s++;
        if (*s == '-') { eneg = 1; s++; }
        else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') { e = (signed char)(e * 10 + (*s - '0')); s++; }
        if (eneg) e = (signed char)-e;
        while (e > 0) { v = v * 10.0f; e--; }
        while (e < 0) { v = v / 10.0f; e++; }
    }
    *out = neg ? -v : v;
    *p = s;
    return 1;
}

/* ---- RND: the GW-BASIC-ish LCG --------------------------------------------- */
static unsigned long rnd_state = 327680UL;
static float rnd_last;

void rnd_seed(unsigned int s)
{
    rnd_state = ((unsigned long)s << 8) | 0x51UL;
    rnd_last = 0.0f;
}

float rnd_next(void)
{
    rnd_state = rnd_state * 214013UL + 2531011UL;
    rnd_last = (float)((rnd_state >> 16) & 0x7FFFUL) / 32768.0f;
    return rnd_last;
}

float rnd_repeat(void) { return rnd_last; }        /* RND(0) */

/* gb_floor: GW INT() - largest integer <= f. */
float gb_floor(float f)
{
    long l = (long)f;
    if (f < 0.0f && (float)l != f) l--;
    return (float)l;
}
