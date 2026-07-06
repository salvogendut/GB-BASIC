/* val.c - number helpers over the MBF engine (fac.s), plus RND.
 *
 * All real float work happens in fac.s; this file only wraps common
 * sequences so call sites stay small.
 */
#include "basrun.h"

void fmt_num(const num_t *n, char *dst)
{
    f_ld(n);
    f_out(dst);
}

int num_toi(const num_t *n)
{
    f_ld(n);
    return f_toi();
}

void num_fromi(num_t *n, int v)
{
    f_fromi(v);
    f_st(n);
}

/* ---- RND: 16-bit LCG, result scaled to [0,1) by a power of two ------------- */
static unsigned int rnd_state = 0x2A17;
static num_t rnd_last;

void rnd_seed(unsigned int s)
{
    rnd_state = s ^ 0x5A5A;
}

/* rnd_fac: leave the RND result in FAC. mode > 0 = next, 0 = repeat last,
 * < 0 = the caller reseeded already (treat as next). */
void rnd_fac(signed char mode)
{
    if (mode == 0) { f_ld(&rnd_last); return; }
    rnd_state = rnd_state * 25173u + 13849u;
    f_fromi((int)(rnd_state & 0x7FFF));
    f_scale(-15);                       /* exact /32768 */
    f_st(&rnd_last);
}
