/* basrun.h - GB-BASIC runtime: shared types, config knobs, cross-module decls.
 *
 * BASRUN.APP runs a GW-BASIC-flavored program in a 40x20 character console
 * window. The interpreter executes as a resumable state machine driven from
 * GB_MSG_FRAME (the kernel WM owns the master loop). Every buffer below is a
 * static pool - no malloc - and every size is a knob for the fit-check ladder.
 */
#ifndef BASRUN_H
#define BASRUN_H

#include "gb.h"

/* ---- console geometry ------------------------------------------------------
 * 40 cols x 6px = 240px = 60 byte cols; 20 rows x 8px = 160 lines.
 * Window: 1 byte col border + 2 inset each side; title bar 14px + 2px gap. */
#define CON_COLS  40
#define CON_ROWS  20
#define WIN_X     8
#define WIN_Y     14
#define WIN_W     64
#define WIN_H     178
#define CX        ((unsigned char)(gb_wm_x() + 2))   /* content origin, live (drag) */
#define CY        ((unsigned char)(gb_wm_y() + 16))

/* ---- capacity knobs (fit-check ladder adjusts these) ---------------------- */
#define PROG_MAX  4096            /* program text cap */
#define PROG_SLK  512             /* gb_fs_load copies whole 512B sectors */
#define NVARS     48              /* numeric scalars */
#define SVARS     16              /* string variables */
#define SSTR_CAP  40              /* max string length */
#define NARRS     8               /* DIM'd arrays */
#define APOOL     160             /* floats in the array pool */
#define FORS      8               /* FOR nesting */
#define GOSUBS    12              /* GOSUB nesting */
#define STRTMP    256             /* per-statement string scratch arena */
#define INBUF     64              /* INPUT line buffer */

/* ---- big buffers live in kernel low RAM, not the 16K bank -------------------
 * 0x2200..0x3DFF is the module bulk-transfer buffer (gb_copybuf): idle except
 * during File Manager copies / saves / card-module streaming. BASRUN parks the
 * program text, console grid and string scratch there - gb_fs_load straight
 * into 0x2200 is the File Manager's own blessed pattern on every backend.
 * Caveat (documented in the README): file transfers performed by OTHER windows
 * while a program is running share this RAM and can disturb the program. */
#define prog     ((char *)0x2200)                    /* PROG_MAX + PROG_SLK */
#define grid     ((char *)(0x2200 + PROG_MAX + PROG_SLK))          /* 800 B */
#define strtmp   ((char *)(0x2200 + PROG_MAX + PROG_SLK + 800))    /* STRTMP */
#define inbuf    ((char *)(0x2200 + PROG_MAX + PROG_SLK + 800 + STRTMP))
#define rowbuf   ((char *)(0x2200 + PROG_MAX + PROG_SLK + 800 + STRTMP + INBUF + 4))
/* rowbuf CON_COLS+1; total ends < 0x3E00 (clipboard) - static_assert by eye:
 * 0x2200 + 4608 + 800 + 256 + 68 + 45 = 0x38D5 */

/* ---- interpreter states ---------------------------------------------------- */
#define ST_IDLE   0               /* nothing loaded */
#define ST_RUN    1               /* executing statements */
#define ST_INPUT  2               /* INPUT line editor active */
#define ST_END    3               /* finished ("Ok"/"Break"/error) - key closes */

/* ---- value ----------------------------------------------------------------- */
/* Numbers are 4-byte packed MBF singles (GW-BASIC SNG format); C never does
 * arithmetic on them directly - everything goes through the fac.s engine. */
typedef struct { unsigned char b[4]; } num_t;

#define VT_NUM 0
#define VT_STR 1
typedef struct {
    unsigned char t;              /* VT_NUM / VT_STR */
    num_t n;
    const char *s;                /* NOT NUL-terminated (may point into prog) */
    unsigned char sl;             /* string length */
} val_t;

/* ---- the MBF float engine (fac.s) ------------------------------------------- */
void f_ld(const void *m);         /* FAC <- packed */
void f_arg(const void *m);        /* ARG <- packed */
void f_st(void *m);               /* packed <- FAC */
void f_fac2arg(void);             /* ARG <- FAC */
void f_add(void);                 /* FAC = FAC + ARG */
void f_sub(void);                 /* FAC = FAC - ARG */
void f_mul(void);
void f_div(void);
signed char f_cmp(void);          /* sign of FAC - ARG: 1 / 0 / -1 */
signed char f_sgn(void);          /* sign of FAC */
void f_neg(void);
signed char f_exp(void);          /* FAC's binary exponent (0 for 0) */
void f_scale(signed char k);      /* FAC *= 2^k */
void f_floor(void);
int  f_toi(void);                 /* FAC -> int16, round-to-nearest (destroys FAC) */
void f_fromi(int v);
unsigned char f_in(const char **pp);   /* parse decimal at *pp -> FAC; 1 = digits */
void f_out(char *dst);            /* GW-style format of FAC (destroys FAC/ARG) */
extern volatile unsigned char fac_err; /* E_OVF / E_DIV0 latched by the engine */

#define FCHK() do { if (fac_err) { err(fac_err); fac_err = 0; } } while (0)

/* ---- error codes ------------------------------------------------------------ */
#define E_NONE   0
#define E_SYNTAX 1
#define E_ULINE  2                /* Undefined line number */
#define E_TYPE   3                /* Type mismatch */
#define E_DIV0   4
#define E_OVF    5                /* Overflow */
#define E_SUBSC  6                /* Subscript out of range */
#define E_RETWG  7                /* RETURN without GOSUB */
#define E_NEXTWF 8                /* NEXT without FOR */
#define E_ODATA  9                /* Out of DATA */
#define E_MEM    10               /* Out of memory */
#define E_STRSP  11               /* Out of string space */
#define E_IFC    12               /* Illegal function call */

/* ---- console (main.c) ------------------------------------------------------- */
void con_clear(void);
void con_putc(char c);
void con_puts(const char *s);           /* NUL-terminated */
void con_putsn(const char *s, unsigned char n);
void con_nl(void);
void con_tab_zone(void);                /* PRINT ',' -> next 14-col zone */
void con_tab_to(unsigned char col);     /* TAB(n): space-fill to column n (0-based) */
void con_locate(unsigned char row, unsigned char col);
void con_flush(void);                   /* repaint dirty rows (once per frame) */
extern unsigned char con_row, con_col;
extern unsigned char scroll_count;      /* scrolls this frame (frame budget cap) */

/* ---- shared interpreter state ----------------------------------------------- */
extern unsigned int prog_len;
extern const char *ip;                  /* execution position in prog */
extern unsigned int cur_line;           /* current line number (errors, GOTO cache) */
extern unsigned char g_err;             /* pending error code (0 = none) */
extern unsigned char g_state;           /* ST_* */
extern unsigned char pending_key;       /* 1-char pushback (Ctrl-C probe vs INKEY$) */
extern unsigned int frame_ctr;          /* frames since start (RANDOMIZE seed) */

void err(unsigned char code);           /* raise (first error wins) */

/* interp.c */
void run_reset(void);                   /* clear vars/stacks, ip = prog start */
void exec_stmt(void);                   /* execute one statement at ip */
void report_error(void);                /* print "<msg> in <line>", -> ST_END */
const char *find_line(unsigned int no); /* line seek (0 = not found) */
num_t *var_slot(char n0, char n1);      /* numeric scalar, created on demand */
unsigned char input_store(void);        /* parse inbuf -> INPUT vars; 1 = ok */

/* expr.c */
void eval(val_t *v);                    /* full expression at ip */
void sk(void);                          /* skip spaces */
unsigned char kw(const char *k);        /* case-insensitive keyword match + skip */
unsigned char at_end(void);             /* ip at ':' / newline / NUL */
unsigned char get_ident(char *n2, unsigned char *is_str);
void strtmp_reset(void);                /* reset the per-statement string arena */
char *strtmp_alloc(unsigned char n);
extern unsigned char in_len;            /* INPUT editor fill (inbuf is low RAM) */

/* interp.c storage (also used by expr.c) */
num_t *arr_slot(char n0, char n1, int idx);      /* array element (auto-DIM 10) */
void svar_get(const char *n2, val_t *v);         /* string variable read */
void svar_set(const char *n2, const val_t *v);   /* string variable write */
unsigned char str_func(val_t *v);                /* string functions; 1 = consumed */

/* val.c - number helpers over the engine */
void fmt_num(const num_t *n, char *dst);   /* GW-style formatting (dst >= 16) */
void rnd_fac(signed char mode);            /* RND -> FAC (mode <0 seed, 0 repeat, >0 next) */
void rnd_seed(unsigned int s);
int  num_toi(const num_t *n);              /* rounded int16 (raises E_OVF) */
void num_fromi(num_t *n, int v);

/* fmath.c - SQR/SIN/COS/TAN over the engine (result in FAC) */
void fn_sqr(void);                         /* FAC = sqrt(FAC) */
void fn_sin(void);
void fn_cos(void);
void fn_tan(void);

/* gfx.c */
void gfx_pset(int x, int y, unsigned char pen);
void gfx_line(int x0, int y0, int x1, int y1, unsigned char pen);
void gfx_box(int x0, int y0, int x1, int y1, unsigned char pen, unsigned char fill);
void gfx_circle(int cx, int cy, int r, unsigned char pen);
extern unsigned char gfx_pen;           /* COLOR - current drawing pen (1-3) */

#endif
