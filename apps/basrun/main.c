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

/* ---- console --------------------------------------------------------------- */
static char grid[CON_ROWS * CON_COLS];
static unsigned char dirty[CON_ROWS];
unsigned char con_row, con_col;
unsigned char scroll_count;
static char rowbuf[CON_COLS + 1];

/* ---- shared interpreter state ---------------------------------------------- */
char prog[PROG_MAX + PROG_SLK];
unsigned int prog_len;
const char *ip;
unsigned int cur_line;
unsigned char g_err;
unsigned char g_state = ST_IDLE;
unsigned char pending_key;
unsigned int frame_ctr;

static unsigned char blink_ctr, blink_on;   /* input/end cursor blink */

void err(unsigned char code)
{
    if (!g_err) g_err = code;
}

void con_clear(void)
{
    unsigned int i;
    for (i = 0; i < CON_ROWS * CON_COLS; i++) grid[i] = ' ';
    for (i = 0; i < CON_ROWS; i++) dirty[i] = 1;
    con_row = 0; con_col = 0;
}

static void scroll_up(void)
{
    unsigned int i;
    for (i = 0; i < (CON_ROWS - 1) * CON_COLS; i++) grid[i] = grid[i + CON_COLS];
    for (i = (CON_ROWS - 1) * CON_COLS; i < CON_ROWS * CON_COLS; i++) grid[i] = ' ';
    for (i = 0; i < CON_ROWS; i++) dirty[i] = 1;
    scroll_count++;
}

void con_nl(void)
{
    con_col = 0;
    if (con_row < CON_ROWS - 1) con_row++;
    else scroll_up();
}

void con_putc(char c)
{
    grid[(unsigned int)con_row * CON_COLS + con_col] = c;
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
    for (r = 0; r < CON_ROWS; r++) if (dirty[r]) { any = 1; break; }
    if (!any) return;
    gb_curhide();
    for (r = 0; r < CON_ROWS; r++) {
        if (!dirty[r]) continue;
        dirty[r] = 0;
        for (c = 0; c < CON_COLS; c++) rowbuf[c] = grid[(unsigned int)r * CON_COLS + c];
        rowbuf[CON_COLS] = 0;
        gb_text(CX, (unsigned char)(CY + r * 8), rowbuf);
    }
    gb_curshow();
}

/* ---- M1 built-in demo (replaced by the interpreter from M2 on) -------------- */
static unsigned char demo_i;

static void demo_step(void)
{
    if (demo_i < 30) {
        char msg[24];
        unsigned char n = (unsigned char)(demo_i + 1), j = 0;
        const char *t = "CONSOLE TEST LINE ";
        while (*t) msg[j++] = *t++;
        if (n >= 10) msg[j++] = (char)('0' + n / 10);
        msg[j++] = (char)('0' + n % 10);
        msg[j] = 0;
        con_puts(msg);
        con_nl();
        demo_i++;
        return;
    }
    con_puts("Ok");
    con_nl();
    g_state = ST_END;
}

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
        budget = 4;                       /* M1: a few demo lines per frame */
        while (budget-- && g_state == ST_RUN && scroll_count < 4)
            demo_step();
        break;
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

static void drag(void)
{
    unsigned char x = gb_wm_x(), y = gb_wm_y();
    if (gb_drag_window(&x, &y, WIN_W, WIN_H)) {
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

void main(void)
{
    unsigned char n;
    con_clear();
    g_state = ST_RUN;
    demo_i = 0;
    gb_wm_managed(&mw);
    for (n = 64; n; n--) if (!gb_getkey()) break;   /* drop buffered keys */
    gb_restore_parent();                            /* first paint */
}
