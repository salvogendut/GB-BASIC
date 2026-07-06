# GB-BASIC language reference

GB-BASIC aims to *feel* like GW-BASIC for line-numbered programs. It is not a
full GW-BASIC — it targets a single 16 KB GEOBENCH app bank plus a small
low-RAM overlay. This page lists what's there and how it differs.

## Program model

- Programs are **line-numbered**; every statement line starts with a number.
- Statements are separated by `:` within a line.
- The runtime executes from the top; there is no immediate ("Ok") command mode
  — BASRUN runs a file and, when it ends, waits for a key and closes.
- Errors print GW-style — `Syntax error in 130` — and stop the program.

## Numbers

Values are **single-precision floating point** in Microsoft Binary Format (what
GW-BASIC calls `SNG`): ~7 significant digits, range ≈ 1e−38 … 1e38. `PRINT`
formats them the GW way — a leading space for non-negatives, `.5` (no leading
zero), integers bare (`42`, not `42.0`), and `E` notation outside the fixed
range.

There is no separate integer type and no `%`/`!`/`#` type-suffix declaration;
everything numeric is single. Variable names are significant to **2 characters**
(so `COUNT` and `CO` are the same variable); a trailing `$` marks a string.

> Avoid variable names that *start* with a keyword — the scanner matches
> keywords first, so `FORCE` reads as `FOR CE`. (This mirrors real Locomotive
> BASIC behaviour.)

## Statements

| Statement | Notes |
|-----------|-------|
| `PRINT` (`?`) | `;` no gap, `,` 14-column zones, `TAB(n)` moves to column *n*. Numbers print with a trailing space, GW-style. |
| `INPUT ["prompt";] var[,var…]` | `;` after the prompt adds `? `. Multiple variables need comma-separated input; bad input reprints `?Redo from start`. |
| `LET v = e` / `v = e` | `LET` optional. Targets: scalars, string vars, `A(i)` array elements. |
| `IF e THEN … [ELSE …]` | After `THEN`: a line number = `GOTO`, or inline statements. `IF e GOTO n` is accepted. **Single-line only** — no nested `IF` within the same line. |
| `FOR v = a TO b [STEP s]` / `NEXT [v]` | GW semantics: the body always runs once; `NEXT J` closes any unmatched inner loops; re-using a loop variable discards the old frame. |
| `GOTO n`, `GOSUB n`, `RETURN` | 8-deep `GOSUB` stack. |
| `DIM A(n)` | 1-D **numeric** arrays, indices `0..n`. Re-`DIM` is a Duplicate Definition error. Undeclared `A(i)` auto-dimensions to 10. |
| `DATA` / `READ` / `RESTORE [n]` | `READ` into numeric or string targets; `DATA` items may be quoted or bare; `RESTORE n` resets to a line. |
| `REM` (`'`) | comment to end of line. |
| `END`, `STOP` | `END` → `Ok`; `STOP` → `Break in NNN`. |
| `RANDOMIZE [n]` | seed `RND`; bare `RANDOMIZE` seeds from the frame counter. |
| `CLS` | clear the console (also clears graphics). |
| `LOCATE row, col` | 1-based; move the text cursor. |
| `COLOR n` | set the drawing pen (0–3) for later graphics. |
| `PSET (x,y)[,c]` | plot a pixel. |
| `LINE (x1,y1)-(x2,y2)[,c[,B\|BF]]` | line, box outline (`B`), or filled box (`BF`). |
| `CIRCLE (x,y),r[,c]` | circle (no aspect/arc parameters). |

Interrupt a running program with **Ctrl-C** (`Break`).

## Functions

- Numeric: `ABS(x) SGN(x) INT(x) SQR(x) SIN(x) COS(x) TAN(x) RND[(x)]`.
  `INT` is a true floor (rounds toward −∞). `RND` returns `[0,1)`; `RND(0)`
  repeats the last value, `RND(-n)` reseeds.
- String: `LEN(s$) ASC(s$) CHR$(n) LEFT$(s$,n) RIGHT$(s$,n) MID$(s$,i[,n])
  INKEY$`. `INKEY$` returns "" or the next key without waiting.

## Operators

`+ - * /`, `^` (see deviation), `MOD`, unary `-`. Relationals `= <> < > <= >=`
yield `-1` (true) / `0` (false). `AND OR NOT` operate on values rounded to
16-bit integers (GW-style bitwise logic). String `+` concatenates; string
relationals compare lexicographically.

## Screen and graphics

The console is **40 columns × 20 rows**. Graphics share the same window with a
coordinate space of **0..239 × 0..159** and **four pens** (0 = background,
1–3 = the desktop palette). Drawing goes straight to the window.

## Deviations from GW-BASIC (by design, to fit the bank)

- **`^` takes integer exponents only** (`2^10` yes, `2^0.5` → *Illegal function
  call*). This keeps `EXP`/`LOG`/`POW` out of the build. Use `SQR` for roots.
- **No `STR$` / `VAL`** in this build (dropped for space; may return later).
- **`COLOR` is a single global pen**, not separate fore/back/border — there is
  no per-character text colour.
- **Single-line `IF`** only; no nested `IF … THEN IF …` on one line, and no
  multi-line `IF`/`ENDIF`.
- **Strings**: up to 8 string variables, each up to ~25 characters; no string
  arrays; assignments longer than the cap are silently truncated.
- **Graphics are not repainted** after the window is dragged or covered — they
  are drawn once. Text is repainted; graphics are transient. (`SCREEN`,
  `DRAW`, `PAINT`, `GET`/`PUT` are not implemented.)
- **Numeric-only 1-D arrays**; no `A(i,j)` and no string arrays.
- No `DEF FN`, `WHILE/WEND`, `ON … GOTO`, `PEEK/POKE`, file I/O, `PLAY`/`SOUND`.

## Capacities (this build)

| Limit | Value |
|-------|-------|
| Program text | 1728 bytes |
| Numeric variables | 36 |
| String variables | 8 (≤ ~25 chars each) |
| Numeric array elements | 40 (across all arrays) |
| `FOR` nesting | 8 |
| `GOSUB` nesting | 8 |
| Console | 40 × 20 |
| Graphics | 240 × 160, pens 0–3 |

These are compile-time knobs (`apps/basrun/basrun.h`); they trade against each
other and against code size within the bank.
