;; gfx.s - BASRUN graphics core (overlay part 2, linked after fac.s into
;; BASRUN2.BIN). PSET / LINE (with fast horizontal spans) / BOX / CIRCLE over
;; the console content area, coordinates 0..239 x 0..159.
;;
;; ABI: C writes the 16-bit signed parameter cells GX0/GY0/GX1/GY1 and GPEN
;; (fixed addresses right after fac_err - see basrun.h), then CALLs the entry
;; vector. Every pixel is a 1-byte read-modify-write through the kernel
;; GB_SAVERECT/GB_RESTORERECT (B=x C=y D=wbytes E=h HL=buf); horizontal spans
;; use GB_FILL for the whole-byte middle. The window origin is read live from
;; MW_RECT (0x1448/0x1449) so drawing follows a dragged window.
;;
;; Platform pixel packing (plat.inc sets MSX2/PCW = 0/1):
;;   CPC Mode 1: pixel i has bit0 at 7-i, bit1 at 3-i.
;;   MSX Screen 6: linear 2-bit fields, pixel i at bits (6-2i).
;;   PCW CGA2: linear fields too, but save/restore bytes are hardware-space:
;;     GB pens 0..3 map to fields 01, 11, 00, 10.
        .module gfx
        .include "plat.inc"

        .globl  _g_pset
        .globl  _g_line
        .globl  _g_box
        .globl  _g_boxf
        .globl  _g_circle
        .globl  GX0
        .globl  GY0
        .globl  GX1
        .globl  GY1
        .globl  GPEN

GB_SAVERECT     =       0x8036
GB_RESTORERECT  =       0x8039
GB_FILL         =       0x8033
MW_X            =       0x1448
MW_Y            =       0x1449

;; GX0/GY0/GX1/GY1/GPEN live in fac.s (fixed addresses for the C side)
        .area   _DATA
GBYTE:  .ds     1               ; RMW byte
GIDX:   .ds     1               ; pixel index 0..3
GBX:    .ds     1               ; screen byte col of the RMW byte
GBY:    .ds     1               ; screen line
DXW:    .ds     2               ; Bresenham dx
DYW:    .ds     2               ; Bresenham -|dy|
SXB:    .ds     1               ; step signs
SYB:    .ds     1
ERRW:   .ds     2               ; error term
CXW:    .ds     2               ; circle: centre / working
CYW:    .ds     2
CRX:    .ds     2               ; circle x
CRY:    .ds     2               ; circle y
CDW:    .ds     2               ; circle d

        .area   _CODE

;; ---- pset ---------------------------------------------------------------------

;; pset_i: plot (DE = x, HL = y) in pen GPEN. Clips. Clobbers everything.
pset_i:
        bit     7, d
        ret     nz              ; x < 0
        ld      a, d
        or      a
        ret     nz              ; x > 255
        ld      a, e
        cp      #240
        ret     nc              ; x > 239
        bit     7, h
        ret     nz              ; y < 0
        ld      a, h
        or      a
        ret     nz
        ld      a, l
        cp      #160
        ret     nc              ; y > 159
        ld      a, e            ; GIDX = x & 3
        and     #3
        ld      (GIDX), a
        ld      a, e            ; byte col = MW_X + 2 + x/4
        rrca
        rrca
        and     #0x3F
        ld      c, a
        ld      a, (MW_X)
        add     a, #2
        add     a, c
        ld      (GBX), a
        ld      a, (MW_Y)       ; line = MW_Y + 16 + y
        add     a, #16
        add     a, l
        ld      (GBY), a
        ;; read the byte
        ld      a, (GBX)
        ld      b, a
        ld      a, (GBY)
        ld      c, a
        ld      de, #0x0101     ; D = wbytes 1, E = h 1
        ld      hl, #GBYTE
        call    GB_SAVERECT
        ;; modify
        ld      a, (GIDX)
        ld      e, a
        ld      d, #0
        ld      hl, #MASKC
        add     hl, de
        ld      a, (hl)         ; both-bits mask for pixel i
        cpl
        ld      hl, #GBYTE
        and     (hl)
        ld      (hl), a
        ;; set bits from the per-pen tables (platform-specific tables below)
.if PCW
        ld      a, (GPEN)
        and     #3
        add     a, a
        add     a, a            ; pen * 4
        ld      c, a
        ld      a, (GIDX)
        add     a, c
        ld      e, a
        ld      d, #0
        ld      hl, #PBYTE
        add     hl, de
        ld      a, (hl)
        ld      hl, #GBYTE
        or      (hl)
        ld      (hl), a
.else
        ld      a, (GPEN)
        and     #1
        jr      z, ps_b1
        ld      a, (GIDX)
        ld      e, a
        ld      d, #0
        ld      hl, #PB0
        add     hl, de
        ld      a, (hl)
        ld      hl, #GBYTE
        or      (hl)
        ld      (hl), a
ps_b1:  ld      a, (GPEN)
        and     #2
        jr      z, ps_wr
        ld      a, (GIDX)
        ld      e, a
        ld      d, #0
        ld      hl, #PB1
        add     hl, de
        ld      a, (hl)
        ld      hl, #GBYTE
        or      (hl)
        ld      (hl), a
.endif
ps_wr:  ;; write the byte back
        ld      a, (GBX)
        ld      b, a
        ld      a, (GBY)
        ld      c, a
        ld      de, #0x0101
        ld      hl, #GBYTE
        jp      GB_RESTORERECT

.if PCW
MASKC:  .db     0xC0, 0x30, 0x0C, 0x03      ; 2-bit hardware field of pixel i
PBYTE:  .db     0x40, 0x10, 0x04, 0x01      ; GB pen 0 -> hw field 01
        .db     0xC0, 0x30, 0x0C, 0x03      ; GB pen 1 -> hw field 11
        .db     0x00, 0x00, 0x00, 0x00      ; GB pen 2 -> hw field 00
        .db     0x80, 0x20, 0x08, 0x02      ; GB pen 3 -> hw field 10
.else
.if MSX2
MASKC:  .db     0xC0, 0x30, 0x0C, 0x03      ; 2-bit field of pixel i
PB0:    .db     0x40, 0x10, 0x04, 0x01      ; pen bit0 at field position
PB1:    .db     0x80, 0x20, 0x08, 0x02      ; pen bit1
.else
MASKC:  .db     0x88, 0x44, 0x22, 0x11      ; bit0(7-i) | bit1(3-i)
PB0:    .db     0x80, 0x40, 0x20, 0x10      ; pen bit0 at 7-i
PB1:    .db     0x08, 0x04, 0x02, 0x01      ; pen bit1 at 3-i
.endif
.endif

;; void g_pset(void)  - plot (GX0, GY0) in GPEN
_g_pset:
        ld      de, (GX0)
        ld      hl, (GY0)
        jp      pset_i

;; ---- horizontal span -------------------------------------------------------------

;; hline_i: y = HL, x from (GX0) to (GX1) (any order), pen GPEN.
;; Edge pixels via pset_i, whole-byte middle via one GB_FILL.
hline_i:
        push    hl
        ld      hl, (GX0)       ; order x0 <= x1
        ld      de, (GX1)
        or      a
        sbc     hl, de
        jr      c, hl_ord
        jr      z, hl_ord
        ld      hl, (GX0)       ; swap
        ld      de, (GX1)
        ld      (GX0), de
        ld      (GX1), hl
hl_ord: pop     hl
        ;; y bounds
        bit     7, h
        ret     nz
        ld      a, h
        or      a
        ret     nz
        ld      a, l
        cp      #160
        ret     nc
        ld      (CYW), hl       ; keep y
        ;; clip x0 to 0, x1 to 239; reject empty
        ld      hl, (GX0)
        bit     7, h
        jr      z, hl_x0ok
        ld      hl, #0
        ld      (GX0), hl
hl_x0ok:
        ld      hl, (GX1)
        bit     7, h
        ret     nz              ; whole span left of screen
        ld      de, #239
        or      a
        sbc     hl, de
        jr      c, hl_x1ok
        ld      hl, #239
        ld      (GX1), hl
hl_x1ok:
        ld      hl, (GX1)
        ld      de, (GX0)
        or      a
        sbc     hl, de
        ret     c               ; x0 > x1 after clip (off right edge)
        ;; leading edge: pset until x0 aligned or past x1
hl_lead:
        ld      a, (GX0)
        and     #3
        jr      z, hl_trail
        call    hl_cmp01        ; CF if GX1 < GX0
        ret     c
        ld      de, (GX0)
        ld      hl, (CYW)
        push    de
        call    pset_i
        pop     de
        inc     de
        ld      (GX0), de
        jr      hl_lead
hl_trail:
        ld      a, (GX1)
        and     #3
        cp      #3
        jr      z, hl_mid
        call    hl_cmp01
        ret     c
        ld      de, (GX1)
        ld      hl, (CYW)
        push    de
        call    pset_i
        pop     de
        dec     de
        ld      (GX1), de
        jr      hl_trail
hl_mid: call    hl_cmp01
        ret     c
        ;; fill bytes GX0/4 .. GX1/4
        ld      a, (GX0)
        rrca
        rrca
        and     #0x3F
        ld      c, a            ; fb
        ld      a, (GX1)
        rrca
        rrca
        and     #0x3F
        sub     c               ; lb - fb
        inc     a
        ld      d, a            ; D = width in bytes
        ld      a, (MW_X)
        add     a, #2
        add     a, c
        ld      b, a            ; B = x byte col
        ld      a, (CYW)
        ld      e, a
        ld      a, (MW_Y)
        add     a, #16
        add     a, e
        ld      c, a            ; C = line
        ld      e, #1           ; E = h 1
        ld      a, (GPEN)
        jp      GB_FILL

hl_cmp01:                       ; CF set if GX1 < GX0
        ld      hl, (GX1)
        ld      de, (GX0)
        or      a
        sbc     hl, de
        ret

;; ---- line --------------------------------------------------------------------------

;; void g_line(void)  - (GX0,GY0)-(GX1,GY1) in GPEN
_g_line:
        ld      hl, (GY0)       ; horizontal? use the fast span
        ld      de, (GY1)
        or      a
        sbc     hl, de
        jr      nz, ln_bres
        ld      hl, (GY0)
        jp      hline_i
ln_bres:
        ;; dx = |x1-x0|, sx = sign
        ld      hl, (GX1)
        ld      de, (GX0)
        or      a
        sbc     hl, de
        ld      a, #1
        jr      nc, ln_dxok
        ld      a, l            ; negate HL
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
        ld      a, #0xFF        ; sx = -1
ln_dxok:
        ld      (SXB), a
        ld      (DXW), hl
        ;; dy = -|y1-y0|, sy = sign
        ld      hl, (GY1)
        ld      de, (GY0)
        or      a
        sbc     hl, de
        ld      a, #1
        jr      nc, ln_dyneg    ; positive: negate to get -|dy|
        ld      a, #0xFF
        jr      ln_dyok         ; already negative
ln_dyneg:
        push    af
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
        pop     af
ln_dyok:
        ld      (SYB), a
        ld      (DYW), hl
        ;; err = dx + dy
        ld      de, (DXW)
        add     hl, de
        ld      (ERRW), hl
ln_lp:  ld      de, (GX0)       ; plot current
        ld      hl, (GY0)
        call    pset_i
        ld      hl, (GX0)       ; done?
        ld      de, (GX1)
        or      a
        sbc     hl, de
        jr      nz, ln_step
        ld      hl, (GY0)
        ld      de, (GY1)
        or      a
        sbc     hl, de
        ret     z
ln_step:
        ld      hl, (ERRW)      ; e2 = 2*err
        add     hl, hl
        push    hl              ; e2
        ld      de, (DYW)       ; e2 >= dy ? (signed)
        call    cmp_sge
        jr      c, ln_sy        ; e2 < dy
        ld      hl, (ERRW)      ; err += dy; x += sx
        ld      de, (DYW)
        add     hl, de
        ld      (ERRW), hl
        ld      a, (SXB)
        call    sext_de
        ld      hl, (GX0)
        add     hl, de
        ld      (GX0), hl
ln_sy:  pop     hl              ; e2
        ld      de, (DXW)       ; e2 <= dx ? (signed)  <=>  !(e2 > dx)
        call    cmp_sle
        jr      c, ln_lp        ; e2 > dx: skip
        ld      hl, (ERRW)      ; err += dx; y += sy
        ld      de, (DXW)
        add     hl, de
        ld      (ERRW), hl
        ld      a, (SYB)
        call    sext_de
        ld      hl, (GY0)
        add     hl, de
        ld      (GY0), hl
        jr      ln_lp

;; sext_de: A (0x01/0xFF) -> DE = +1/-1
sext_de:
        ld      e, a
        rlca
        sbc     a, a
        ld      d, a
        ret

;; cmp_sge: CF set if HL < DE (signed) - i.e. NC means HL >= DE
cmp_sge:
        ld      a, h
        xor     d
        jp      m, cs_diff
        or      a
        sbc     hl, de
        ret                     ; CF = HL < DE
cs_diff:
        bit     7, h            ; signs differ: HL negative -> HL < DE
        jr      nz, cs_lt
        or      a               ; HL positive -> HL >= DE
        ret
cs_lt:  scf
        ret

;; cmp_sle: CF set if HL > DE (signed) - i.e. NC means HL <= DE
cmp_sle:
        ex      de, hl
        call    cmp_sge         ; CF = DE < HL  <=>  HL > DE
        ret

;; ---- box ------------------------------------------------------------------------

;; order GY0 <= GY1
box_ordy:
        ld      hl, (GY1)
        ld      de, (GY0)
        call    cmp_sge
        ret     nc
        ld      hl, (GY0)
        ld      de, (GY1)
        ld      (GY0), de
        ld      (GY1), hl
        ret

;; void g_boxf(void)  - filled box (GX0,GY0)-(GX1,GY1)
_g_boxf:
        call    box_ordy
bf_lp:  ld      hl, (GX0)       ; hline consumes GX0/GX1: preserve
        push    hl
        ld      hl, (GX1)
        push    hl
        ld      hl, (GY0)
        call    hline_i
        pop     hl
        ld      (GX1), hl
        pop     hl
        ld      (GX0), hl
        ld      hl, (GY0)
        inc     hl
        ld      (GY0), hl
        ld      de, (GY1)
        call    cmp_sle         ; GY0 <= GY1 ?
        jr      nc, bf_lp
        ret

;; void g_box(void)  - outline box
_g_box:
        call    box_ordy
        ld      hl, (GX0)       ; top edge
        push    hl
        ld      hl, (GX1)
        push    hl
        ld      hl, (GY0)
        call    hline_i
        pop     hl
        ld      (GX1), hl
        pop     hl
        ld      (GX0), hl
        ld      hl, (GX0)       ; bottom edge
        push    hl
        ld      hl, (GX1)
        push    hl
        ld      hl, (GY1)
        call    hline_i
        pop     hl
        ld      (GX1), hl
        pop     hl
        ld      (GX0), hl
        ;; vertical edges: y from GY0+1 to GY1-1
        ld      hl, (GY0)
        ld      (CXW), hl       ; CXW = walking y
bx_vl:  ld      hl, (CXW)
        ld      de, (GY1)
        call    cmp_sge         ; y >= GY1? then done (edges drawn)
        jr      nc, bx_done
        ld      de, (GX0)
        ld      hl, (CXW)
        call    pset_i
        ld      de, (GX1)
        ld      hl, (CXW)
        call    pset_i
        ld      hl, (CXW)
        inc     hl
        ld      (CXW), hl
        jr      bx_vl
bx_done:
        ret

;; ---- circle ---------------------------------------------------------------------

;; plot the 8 symmetric points: centre (GX0,GY0), offsets CRX/CRY
circ8:
        ld      de, (CRX)
        ld      hl, (CRY)
        call    circ4           ; (+x,+y) (-x,+y) (+x,-y) (-x,-y)
        ld      de, (CRY)       ; swapped
        ld      hl, (CRX)
circ4:
        ld      (CXW), de       ; CXW = ox, CYW = oy
        ld      (CYW), hl
        ;; (+ox, +oy)
        ld      hl, (GX0)
        ld      de, (CXW)
        add     hl, de
        ex      de, hl
        ld      hl, (GY0)
        push    de
        ld      de, (CYW)
        add     hl, de
        pop     de
        call    pset_i
        ;; (-ox, +oy)
        ld      hl, (GX0)
        ld      de, (CXW)
        or      a
        sbc     hl, de
        ex      de, hl
        ld      hl, (GY0)
        push    de
        ld      de, (CYW)
        add     hl, de
        pop     de
        call    pset_i
        ;; (+ox, -oy)
        ld      hl, (GX0)
        ld      de, (CXW)
        add     hl, de
        ex      de, hl
        ld      hl, (GY0)
        push    de
        ld      de, (CYW)
        or      a
        sbc     hl, de
        pop     de
        call    pset_i
        ;; (-ox, -oy)
        ld      hl, (GX0)
        ld      de, (CXW)
        or      a
        sbc     hl, de
        ex      de, hl
        ld      hl, (GY0)
        push    de
        ld      de, (CYW)
        or      a
        sbc     hl, de
        pop     de
        jp      pset_i

;; void g_circle(void)  - centre (GX0,GY0), radius GX1, pen GPEN
_g_circle:
        ld      hl, (GX1)       ; r < 0 -> nothing
        bit     7, h
        ret     nz
        ld      (CRX), hl       ; x = r
        ld      hl, #0
        ld      (CRY), hl       ; y = 0
        ld      hl, (GX1)       ; d = 1 - r
        ex      de, hl
        ld      hl, #1
        or      a
        sbc     hl, de
        ld      (CDW), hl
ci_lp:  ld      hl, (CRX)       ; while x >= y
        ld      de, (CRY)
        call    cmp_sge
        ret     c
        call    circ8
        ld      hl, (CRY)       ; y++
        inc     hl
        ld      (CRY), hl
        ld      hl, (CDW)       ; d < 0 ?
        bit     7, h
        jr      z, ci_dx
        ld      hl, (CRY)       ; d += 2*y + 1
        add     hl, hl
        inc     hl
        ld      de, (CDW)
        add     hl, de
        ld      (CDW), hl
        jr      ci_lp
ci_dx:  ld      hl, (CRX)       ; x--
        dec     hl
        ld      (CRX), hl
        ld      hl, (CRY)       ; d += 2*(y - x) + 1
        ld      de, (CRX)
        or      a
        sbc     hl, de
        add     hl, hl
        inc     hl
        ld      de, (CDW)
        add     hl, de
        ld      (CDW), hl
        jr      ci_lp
