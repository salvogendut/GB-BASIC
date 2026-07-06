/* BASRUN - the GB-BASIC runtime: a 40x20 character console window that runs a
 * GW-BASIC-flavored program (see docs/LANGUAGE.md in the repo root).
 *
 * The kernel WM owns the master loop; the interpreter runs as a resumable
 * state machine inside GB_MSG_FRAME - a budget of statements per frame so the
 * desktop stays live. Console output goes to a 40x20 char grid (telnet-style:
 * per-row dirty flags, one opaque gb_text per row, memmove scroll), flushed
 * once per frame. Direct-draw graphics (PSET/LINE/CIRCLE) paint the same
 * content area and are NOT repainted after drag/overlap (accepted trade-off).
 *
 * Launched from the File Manager / editor with a .BAS file argument the
 * program is loaded via gb_fs_load; launched file-less (e.g. as a saver in
 * the test harness) it falls back to the BUILTIN test program. */
#include "basrun.h"

/* ---- console (grid/rowbuf live in low RAM - see basrun.h) ------------------- */
static unsigned char dirty[CON_ROWS];
unsigned char con_row, con_col;
static char *con_ptr;                   /* cached &grid[row*COLS+col] */
unsigned char scroll_count;

static void con_seek(void)              /* recompute con_ptr (rare ops only) */
{
    con_ptr = grid + (unsigned int)con_row * CON_COLS + con_col;
}

/* ---- shared interpreter state ---------------------------------------------- */
unsigned int prog_len;
const char *ip;
unsigned int cur_line;
unsigned char g_err;
unsigned char g_state = ST_IDLE;
unsigned char pending_key;
unsigned char in_len;
unsigned int frame_ctr;

static unsigned char blink_ctr, blink_on;   /* input/end cursor blink */

void err(unsigned char code)
{
    if (!g_err) g_err = code;
}

void con_clear(void)
{
    char *p = grid;
    unsigned int i;
    for (i = 0; i < CON_ROWS * CON_COLS; i++) *p++ = ' ';
    for (i = 0; i < CON_ROWS; i++) dirty[i] = 1;
    con_row = 0; con_col = 0;
    con_ptr = grid;
}

static void scroll_up(void)
{
    char *d = grid;
    const char *s = grid + CON_COLS;
    unsigned int i;
    for (i = 0; i < (CON_ROWS - 1) * CON_COLS; i++) *d++ = *s++;
    for (i = 0; i < CON_COLS; i++) *d++ = ' ';
    for (i = 0; i < CON_ROWS; i++) dirty[i] = 1;
    scroll_count++;
}

void con_nl(void)
{
    con_col = 0;
    if (con_row < CON_ROWS - 1) con_row++;
    else scroll_up();
    con_seek();
}

void con_putc(char c)
{
    *con_ptr++ = c;
    dirty[con_row] = 1;
    if (++con_col >= CON_COLS) con_nl();
}

void con_puts(const char *s)
{
    while (*s) con_putc(*s++);
}

void con_putsn(const char *s, unsigned char n)
{
    while (n--) con_putc(*s++);
}

void con_tab_zone(void)                 /* PRINT ',' -> next 14-column zone */
{
    unsigned char next = (unsigned char)(((con_col / 14) + 1) * 14);
    if (next >= CON_COLS) { con_nl(); return; }
    while (con_col < next) con_putc(' ');
}

void con_tab_to(unsigned char col)      /* TAB(n) (0-based target column) */
{
    if (col >= CON_COLS) col = CON_COLS - 1;
    if (con_col > col) con_nl();
    while (con_col < col) con_putc(' ');
}

void con_locate(unsigned char row, unsigned char col)
{
    if (row >= CON_ROWS) row = CON_ROWS - 1;
    if (col >= CON_COLS) col = CON_COLS - 1;
    con_row = row; con_col = col;
    con_seek();
}

/* cursor cell rect (the underscore sits on the glyph's bottom row) */
static void cursor_draw(unsigned char pen)
{
    gb_fill((unsigned char)(CX + (con_col * 3) / 2),
            (unsigned char)(CY + con_row * 8 + 7), 2, 1, pen);
}

void con_flush(void)
{
    unsigned char r, c, any = 0;
    const char *g;
    for (r = 0; r < CON_ROWS; r++) if (dirty[r]) { any = 1; break; }
    if (!any) return;
    gb_curhide();
    g = grid;
    for (r = 0; r < CON_ROWS; r++, g += CON_COLS) {
        char *d;
        const char *s;
        if (!dirty[r]) continue;
        dirty[r] = 0;
        d = rowbuf; s = g;
        for (c = 0; c < CON_COLS; c++) *d++ = *s++;
        rowbuf[CON_COLS] = 0;
        gb_text(CX, (unsigned char)(CY + r * 8), rowbuf);
    }
    gb_curshow();
}

/* ---- built-in test program (used when launched with no file argument) ------- */
#ifndef BUILTIN_OFF
static const char builtin[] =
    "10 REM M5 GFX\n"
    "20 CLS\n"
    "30 FOR I=0 TO 230 STEP 23\n"
    "40 LINE (0,159)-(I,10),1\n"
    "50 NEXT\n"
    "60 CIRCLE (60,120),30,3\n"
    "70 CIRCLE (60,120),18,2\n"
    "80 LINE (150,60)-(220,120),3,B\n"
    "90 LINE (160,70)-(210,110),2,BF\n"
    "100 COLOR 2\n"
    "110 PSET (238,12):PSET (236,14)\n"
    "120 END\n";
#endif

/* ---- state machine ---------------------------------------------------------- */
static void frame(void)
{
    unsigned char budget;

    frame_ctr++;
    pending_key = gb_getkey();
    if (pending_key == 0x03 && g_state == ST_RUN) {     /* Ctrl-C */
        con_nl(); con_puts("Break"); con_nl();
        g_state = ST_END;
        pending_key = 0;
    }

    switch (g_state) {
    case ST_RUN:
        scroll_count = 0;
        budget = 24;                      /* statements per frame (desktop stays live) */
        while (budget-- && g_state == ST_RUN && scroll_count < 4) {
            strtmp_reset();
            exec_stmt();
            if (g_err) { report_error(); break; }
        }
        break;
    case ST_INPUT: {
        unsigned char k = pending_key, n = 8;
        pending_key = 0;
        while (n--) {
            if (!k) k = gb_getkey();
            if (!k) break;
            if (k == 0x03) {                      /* Ctrl-C */
                con_nl(); con_puts("Break"); con_nl();
                g_state = ST_END;
                break;
            }
            if (k == 0x0D) {                      /* Enter */
                cursor_draw(0);
                con_nl();
                if (input_store()) g_state = ST_RUN;
                else {
                    con_puts("?Redo from start");
                    con_nl();
                    con_puts("? ");
                    in_len = 0;
                }
                break;
            }
            if (k == 0x08 || k == 0x7F) {         /* Backspace */
                if (in_len && con_col) {
                    cursor_draw(0);
                    in_len--;
                    con_col--;
                    con_putc(' ');
                    con_col--;
                }
            } else if (k >= 32 && k < 127 && in_len < INBUF &&
                       con_col < CON_COLS - 1) {
                inbuf[in_len++] = (char)k;
                con_putc((char)k);
            }
            k = 0;
        }
        if (g_state == ST_INPUT && ++blink_ctr >= 16) {
            blink_ctr = 0;
            blink_on ^= 1;
            gb_curhide();
            cursor_draw((unsigned char)(blink_on ? 3 : 0));
            gb_curshow();
        }
        break; }
    case ST_END:
        if (pending_key) { gb_wm_close(); return; }
        if (++blink_ctr >= 16) {          /* idle cursor blink */
            blink_ctr = 0;
            blink_on ^= 1;
            gb_curhide();
            cursor_draw((unsigned char)(blink_on ? 3 : 0));
            gb_curshow();
        }
        break;
    default:
        break;
    }
    con_flush();
}

/* ---- window proc ------------------------------------------------------------- */
static void draw(void)                    /* GB_MSG_DRAW: WM drew the chrome */
{
    unsigned char r;
    for (r = 0; r < CON_ROWS; r++) dirty[r] = 1;
    con_flush();
}

/* drag_window: copied from geobench lib/gb/gbwin.c gb_drag_window (BSD) so
 * BASRUN can link without the rest of gbwin (grip/resize - we are fixed-size). */
static unsigned char drag_window(unsigned char *x, unsigned char *y,
                                 unsigned char w, unsigned char h)
{
    unsigned char ox = *x, oy = *y;
    unsigned char gdx = (unsigned char)(gb_mx() - ox);
    unsigned char gdy = (unsigned char)(gb_my() - oy);
    unsigned char xmax = (unsigned char)(GB_COLS - w);
    unsigned char ymax = (unsigned char)(GB_LINES - h);
    unsigned char nx, ny, mx, my, f, lifted = 0;

    for (;;) {
        f = gb_poll();
        if (!(f & GB_FIRE)) break;
        mx = gb_mx();
        my = gb_my();
        nx = (mx >= gdx) ? (unsigned char)(mx - gdx) : 0;
        ny = (my >= gdy) ? (unsigned char)(my - gdy) : 0;
        if (nx > xmax) nx = xmax;
        if (ny < 8)    ny = 8;
        if (ny > ymax) ny = ymax;
        if (nx == ox && ny == oy) continue;
        gb_curhide();
        if (!lifted) { gb_fill(ox, oy, w, h, 0); lifted = 1; }
        else gb_frame(ox, oy, w, h, 0);
        ox = nx;
        oy = ny;
        gb_frame(ox, oy, w, h, 3);
        gb_curshow();
    }
    if (!lifted) return 0;
    gb_curhide();
    gb_frame(ox, oy, w, h, 0);
    gb_curshow();
    *x = ox;
    *y = oy;
    return 1;
}

static void drag(void)
{
    unsigned char x = gb_wm_x(), y = gb_wm_y();
    if (drag_window(&x, &y, WIN_W, WIN_H)) {
        gb_wm_setpos(x, y);
        gb_restore_parent();
    }
}

static void proc(void)
{
    switch (gb_msg.type) {
        case GB_MSG_DRAW:  draw();  break;
        case GB_MSG_FRAME: frame(); break;
        case GB_MSG_CLOSE: gb_wm_close(); break;
        case GB_MSG_DRAG:  drag();  break;
    }
}

static const gb_mwin_t mw = { WIN_X, WIN_Y, WIN_W, WIN_H, 0, 0, proc, "GB-BASIC" };

/* strip_cr: drop every \r in prog[0..n), NUL-terminate; returns new length. */
static unsigned int strip_cr(unsigned int n)
{
    unsigned int i, j = 0;
    for (i = 0; i < n; i++) if (prog[i] != '\r') prog[j++] = prog[i];
    prog[j] = 0;
    return j;
}

void main(void)
{
    unsigned char n;
    static char orig11[11];
    gb_wm_managed(&mw);                             /* register FIRST: captures the file arg */
    /* two-stage load: pull the float-engine overlay (BASRUN2.BIN) into low RAM
       at LR_ENGINE by temporarily repointing our window's file argument */
    gb_get_name(orig11);
    gb_set_name("BASRUN2 BIN");
    /* the floppy loader's size gate is on-disk 128-byte records INCLUDING the
       AMSDOS header, and it writes whole 512B sectors - allow 0x0AC0 (sectors
       spill into the not-yet-loaded program area, which is harmless) */
    n = (unsigned char)(gb_fs_load((char *)LR_ENGINE, 0x0E00) != 0);
    gb_set_name(orig11);
    if (!n || *(volatile unsigned char *)LR_ENGINE != 0xC3) {   /* jp = engine present */
        con_puts("BASRUN2.BIN missing.");
        con_nl();
        g_state = ST_END;
        gb_restore_parent();
        return;
    }
    fac_err = 0;
    if (HANDOFF_MAGIC[0] == 'G' && HANDOFF_MAGIC[1] == 'B' &&
        HANDOFF_MAGIC[2] == 'R' && HANDOFF_MAGIC[3] == 'N') {
        /* Run from the editor: the program is already in RAM (plain \n). Copy it
           down into prog[] (the engine load above may have touched the low end)
           and consume the magic so a later file-launch won't reuse it. */
        unsigned int i, hn = HANDOFF_LEN;
        if (hn > PROG_MAX) hn = PROG_MAX;
        for (i = 0; i < hn; i++) prog[i] = HANDOFF_PROG[i];
        prog[hn] = 0;
        prog_len = hn;
        HANDOFF_MAGIC[0] = 0;
    } else {
        prog_len = gb_fs_load(prog, PROG_MAX);      /* launch .BAS file, 0 if none */
        prog_len = strip_cr(prog_len);
#ifndef BUILTIN_OFF
        if (prog_len == 0) {                        /* file-less (saver/test) -> builtin */
            unsigned int i;
            for (i = 0; builtin[i]; i++) prog[i] = builtin[i];
            prog[i] = 0;
            prog_len = i;
        }
#endif
    }
    con_clear();                                    /* blank console AFTER the loads
                                                       (a file load can spill sectors into
                                                       the grid area) so a program without
                                                       CLS starts on a clean screen */
    if (prog_len) {
        run_reset();
        g_state = ST_RUN;
    } else {
        con_puts("No program.");
        con_nl();
        g_state = ST_END;
    }
    for (n = 64; n; n--) if (!gb_getkey()) break;   /* drop buffered keys */
    gb_restore_parent();                            /* first paint */
}
