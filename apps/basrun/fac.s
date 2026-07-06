;; fac.s - GB-BASIC floating point engine (Microsoft Binary Format single).
;;
;; Why asm: a C float interpreter doesn't fit the 16K app bank - SDCC's float
;; call sequences plus z80.lib pulled ~11K more than the budget allows. This
;; engine keeps the two operands in fixed RAM (FAC and ARG, exactly like
;; Microsoft's own BASICs) so a C call site is a plain 3-byte CALL, and the
;; whole z80.lib float library drops out of the link.
;;
;; Number format (packed, 4 bytes - MBF single, what GW-BASIC calls SNG):
;;   byte0 mantissa low, byte1 mantissa mid,
;;   byte2 sign(bit7) | mantissa high 7 bits (the leading 1 is implicit),
;;   byte3 exponent, bias 128 (0 means the value 0; mantissa in [0.5,1)).
;;
;; Unpacked FAC/ARG: exponent byte, sign byte (0x00/0xFF), 3 mantissa bytes
;; MSB-first with the leading 1 EXPLICIT (bit7 of the high byte set), plus a
;; shared GUARD byte for rounding. All routines preserve IY (the C frame);
;; IX/BC/DE/HL/A are scratch, per __sdcccall(1).
;;
;; C API (see basrun.h): f_ld/f_arg/f_st move packed <-> FAC/ARG; f_add/f_sub/
;; f_mul/f_div leave FAC = FAC op ARG; f_cmp/f_sgn report; f_neg/f_scale/
;; f_floor adjust; f_toi/f_fromi convert 16-bit ints; f_in parses text via a
;; char** cursor; f_out formats GW-style. Errors (overflow, /0) latch into
;; fac_err (E_* codes from basrun.h) - C checks and clears it.
        .module fac

        .globl  _f_ld
        .globl  _f_arg
        .globl  _f_st
        .globl  _f_fac2arg
        .globl  _f_add
        .globl  _f_sub
        .globl  _f_mul
        .globl  _f_div
        .globl  _f_cmp
        .globl  _f_sgn
        .globl  _f_neg
        .globl  _f_scale
        .globl  _f_floor
        .globl  _f_toi
        .globl  _f_fromi
        .globl  _f_in
        .globl  _f_out
        .globl  _fac_err

E_DIV0  =       4               ; keep in sync with basrun.h
E_OVF   =       5

        .area   _DATA
_fac_err::
        .ds     1
FEXP:   .ds     1
FSGN:   .ds     1               ; 0x00 = +, 0xFF = -
FM:     .ds     3               ; FM+0 = MSB (bit7 = explicit leading 1)
AEXP:   .ds     1
ASGN:   .ds     1
AM:     .ds     3
GUARD:  .ds     1
EXP16:  .ds     2               ; working exponent, 16-bit signed
ACCH:   .ds     3               ; multiply accumulator (high 24)
REMX:   .ds     1               ; divide remainder extension
QREG:   .ds     4               ; divide quotient
ACC3:   .ds     3               ; parse/format integer accumulator (MSB first)
SDIG:   .ds     1               ; parse: significant digits taken
DEXP:   .ds     1               ; parse: decimal exponent (signed byte)
FRAC:   .ds     1               ; parse: in-fraction flag
NEGF:   .ds     1               ; parse: mantissa sign flag
ANYF:   .ds     1               ; parse: saw a digit
FOUTE:  .ds     1               ; format: decimal exponent e10 (signed byte)
DIGS:   .ds     7               ; format: decimal digits
TMPB:   .ds     1

        .area   _CODE

;; ---- load / store -----------------------------------------------------------

;; void f_ld(const void *m)  - FAC <- packed MBF at HL
_f_ld:
        ld      a, (hl)
        ld      (FM+2), a
        inc     hl
        ld      a, (hl)
        ld      (FM+1), a
        inc     hl
        ld      a, (hl)
        ld      c, a            ; sign | mant hi
        or      #0x80           ; explicit leading 1
        ld      (FM+0), a
        inc     hl
        ld      a, (hl)
        ld      (FEXP), a
        or      a
        jr      nz, ld_sgn
        xor     a               ; the value 0: clean state
        ld      (FM+0), a
        ld      (FM+1), a
        ld      (FM+2), a
        ld      (FSGN), a
        ret
ld_sgn: ld      a, c
        and     #0x80
        jr      z, ld_pos
        ld      a, #0xFF
ld_pos: ld      (FSGN), a
        ret

;; void f_arg(const void *m)  - ARG <- packed MBF at HL
_f_arg:
        ld      a, (hl)
        ld      (AM+2), a
        inc     hl
        ld      a, (hl)
        ld      (AM+1), a
        inc     hl
        ld      a, (hl)
        ld      c, a
        or      #0x80
        ld      (AM+0), a
        inc     hl
        ld      a, (hl)
        ld      (AEXP), a
        or      a
        jr      nz, la_sgn
        xor     a
        ld      (AM+0), a
        ld      (AM+1), a
        ld      (AM+2), a
        ld      (ASGN), a
        ret
la_sgn: ld      a, c
        and     #0x80
        jr      z, la_pos
        ld      a, #0xFF
la_pos: ld      (ASGN), a
        ret

;; void f_st(void *m)  - packed MBF at HL <- FAC
_f_st:
        ld      a, (FEXP)
        or      a
        jr      nz, st_nz
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        inc     hl
        ld      (hl), a
        ret
st_nz:  ld      a, (FM+2)
        ld      (hl), a
        inc     hl
        ld      a, (FM+1)
        ld      (hl), a
        inc     hl
        ld      a, (FM+0)
        and     #0x7F
        ld      b, a
        ld      a, (FSGN)
        and     #0x80
        or      b
        ld      (hl), a
        inc     hl
        ld      a, (FEXP)
        ld      (hl), a
        ret

;; void f_fac2arg(void)  - ARG <- FAC (5-byte copy)
_f_fac2arg:
        ld      hl, #FEXP
        ld      de, #AEXP
        ld      bc, #5
        ldir
        ret

;; ARG -> FAC
arg2fac:
        ld      hl, #AEXP
        ld      de, #FEXP
        ld      bc, #5
        ldir
        ret

;; swap FAC <-> ARG (5 bytes)
fac_swap:
        ld      hl, #FEXP
        ld      de, #AEXP
        ld      b, #5
sw_l:   ld      c, (hl)
        ld      a, (de)
        ld      (hl), a
        ld      a, c
        ld      (de), a
        inc     hl
        inc     de
        djnz    sw_l
        ret

;; FAC <- 0
set_zero:
        xor     a
        ld      (FEXP), a
        ld      (FSGN), a
        ld      (FM+0), a
        ld      (FM+1), a
        ld      (FM+2), a
        ld      (GUARD), a
        ret

;; ---- normalize + round tail --------------------------------------------------
;; In: FM(3) + GUARD hold the mantissa, EXP16 the working exponent.
;; Normalizes left, rounds on GUARD bit7, clamps EXP16 into 1..255
;; (<=0 -> the value 0; >255 -> fac_err = E_OVF, saturate).
norm_round:
        ld      a, (FM+0)
        ld      b, a
        ld      a, (FM+1)
        or      b
        ld      b, a
        ld      a, (FM+2)
        or      b
        ld      b, a
        ld      a, (GUARD)
        or      b
        jr      z, set_zero
nr_lp:  ld      a, (FM+0)
        bit     7, a
        jr      nz, nr_rnd
        ld      hl, #GUARD
        sla     (hl)
        ld      hl, #FM+2
        rl      (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        ld      hl, (EXP16)
        dec     hl
        ld      (EXP16), hl
        jr      nr_lp
nr_rnd: ld      a, (GUARD)
        bit     7, a
        jr      z, nr_clamp
        ld      hl, #FM+2
        inc     (hl)
        jr      nz, nr_clamp
        dec     hl
        inc     (hl)
        jr      nz, nr_clamp
        dec     hl
        inc     (hl)
        jr      nz, nr_clamp
        ld      a, #0x80        ; carry out of bit 23: 1.000... -> 0.100.. exp+1
        ld      (FM+0), a
        ld      hl, (EXP16)
        inc     hl
        ld      (EXP16), hl
nr_clamp:
        xor     a
        ld      (GUARD), a
        ld      hl, (EXP16)
        ld      a, h
        or      a
        jr      z, nr_lo
        bit     7, h
        jr      nz, set_zero    ; negative exponent: underflow -> 0
        jr      nr_ovf
nr_lo:  ld      a, l
        or      a
        jr      z, set_zero     ; exponent 0: underflow -> 0
        ld      (FEXP), a
        ret
nr_ovf: ld      a, #E_OVF
        ld      (_fac_err), a
        ld      a, #0xFF        ; saturate to max magnitude
        ld      (FEXP), a
        ld      (FM+0), a
        ld      (FM+1), a
        ld      (FM+2), a
        ret

;; ---- add / sub ----------------------------------------------------------------

;; void f_sub(void)  - FAC = FAC - ARG
_f_sub:
        ld      a, (AEXP)
        or      a
        ret     z
        ld      a, (ASGN)
        cpl
        ld      (ASGN), a
        ;; fall through into f_add

;; void f_add(void)  - FAC = FAC + ARG
_f_add:
        ld      a, (AEXP)
        or      a
        ret     z               ; + 0
        ld      a, (FEXP)
        or      a
        jr      nz, ad_go
        jp      arg2fac         ; 0 + ARG
ad_go:  ld      b, a            ; make FEXP the larger exponent
        ld      a, (AEXP)
        cp      b
        jr      c, ad_ok
        jr      z, ad_ok
        call    fac_swap
ad_ok:  ld      a, (AEXP)
        ld      b, a
        ld      a, (FEXP)
        ld      l, a            ; EXP16 = FEXP
        ld      h, #0
        ld      (EXP16), hl
        sub     b               ; A = exponent gap (>= 0)
        cp      #25
        ret     nc              ; ARG too small to matter
        ld      c, a            ; C = gap
        xor     a
        ld      (GUARD), a
        ld      a, c
        or      a
        jr      z, ad_sgn
ad_sh:  ld      hl, #AM         ; shift ARG right into GUARD, gap times
        srl     (hl)
        inc     hl
        rr      (hl)
        inc     hl
        rr      (hl)
        ld      hl, #GUARD
        rr      (hl)
        dec     c
        jr      nz, ad_sh
ad_sgn: ld      a, (ASGN)
        ld      hl, #FSGN
        xor     (hl)
        jr      nz, ad_neg
        ;; same sign: FM += AM
        ld      a, (FM+2)
        ld      hl, #AM+2
        add     a, (hl)
        ld      (FM+2), a
        ld      a, (FM+1)
        dec     hl
        adc     a, (hl)
        ld      (FM+1), a
        ld      a, (FM+0)
        dec     hl
        adc     a, (hl)
        ld      (FM+0), a
        jp      nc, norm_round
        ld      hl, #FM         ; carry: shift the 25-bit sum right one
        rr      (hl)
        inc     hl
        rr      (hl)
        inc     hl
        rr      (hl)
        ld      hl, #GUARD
        rr      (hl)
        ld      hl, (EXP16)
        inc     hl
        ld      (EXP16), hl
        jp      norm_round
ad_neg: ;; opposite signs: FM.0 - AM.GUARD (32-bit), sign fixup on borrow
        xor     a
        ld      hl, #GUARD
        sub     (hl)
        ld      (hl), a
        ld      a, (FM+2)
        ld      hl, #AM+2
        sbc     a, (hl)
        ld      (FM+2), a
        ld      a, (FM+1)
        dec     hl
        sbc     a, (hl)
        ld      (FM+1), a
        ld      a, (FM+0)
        dec     hl
        sbc     a, (hl)
        ld      (FM+0), a
        jp      nc, norm_round  ; FAC magnitude won: sign already right
        ld      a, (ASGN)       ; ARG magnitude won: negate + take ARG's sign
        ld      (FSGN), a
        xor     a
        ld      hl, #GUARD
        sub     (hl)
        ld      (hl), a
        ld      a, #0
        ld      hl, #FM+2
        sbc     a, (hl)
        ld      (hl), a
        ld      a, #0
        dec     hl
        sbc     a, (hl)
        ld      (hl), a
        ld      a, #0
        dec     hl
        sbc     a, (hl)
        ld      (hl), a
        jp      norm_round

;; ---- multiply -------------------------------------------------------------------

;; void f_mul(void)  - FAC = FAC * ARG
_f_mul:
        ld      a, (FEXP)
        or      a
        ret     z               ; 0 * x
        ld      a, (AEXP)
        or      a
        jp      z, set_zero     ; x * 0
        ld      l, a            ; EXP16 = FEXP + AEXP - 128
        ld      h, #0
        ld      a, (FEXP)
        ld      e, a
        ld      d, #0
        add     hl, de
        ld      de, #-128
        add     hl, de
        ld      (EXP16), hl
        ld      a, (ASGN)       ; sign = FSGN xor ASGN
        ld      hl, #FSGN
        xor     (hl)
        ld      (hl), a
        xor     a               ; ACCH = 0
        ld      (ACCH+0), a
        ld      (ACCH+1), a
        ld      (ACCH+2), a
        ld      b, #24
mu_lp:  ld      a, (FM+2)       ; multiplier LSB set?
        and     #1
        jr      z, mu_sh0
        ld      a, (ACCH+2)     ; ACCH += AM (24-bit, keep the carry)
        ld      hl, #AM+2
        add     a, (hl)
        ld      (ACCH+2), a
        ld      a, (ACCH+1)
        dec     hl
        adc     a, (hl)
        ld      (ACCH+1), a
        ld      a, (ACCH+0)
        dec     hl
        adc     a, (hl)
        ld      (ACCH+0), a
        jr      mu_sh           ; CF = 25th bit
mu_sh0: or      a               ; clear CF
mu_sh:  ld      hl, #ACCH       ; 49-bit shift right: CF -> ACCH -> FM
        rr      (hl)
        inc     hl
        rr      (hl)
        inc     hl
        rr      (hl)
        ld      hl, #FM
        rr      (hl)
        inc     hl
        rr      (hl)
        inc     hl
        rr      (hl)
        djnz    mu_lp
        ld      a, (FM+0)       ; product: high 24 in ACCH, next byte = guard
        ld      (GUARD), a
        ld      a, (ACCH+0)
        ld      (FM+0), a
        ld      a, (ACCH+1)
        ld      (FM+1), a
        ld      a, (ACCH+2)
        ld      (FM+2), a
        jp      norm_round

;; ---- divide ----------------------------------------------------------------------

;; void f_div(void)  - FAC = FAC / ARG
_f_div:
        ld      a, (AEXP)
        or      a
        jr      nz, dv_go
        ld      a, #E_DIV0      ; x / 0
        ld      (_fac_err), a
        ld      a, #0xFF        ; saturate
        ld      (FEXP), a
        ld      (FM+0), a
        ld      (FM+1), a
        ld      (FM+2), a
        ret
dv_go:  ld      a, (FEXP)
        or      a
        ret     z               ; 0 / x
        ld      l, a            ; EXP16 = FEXP - AEXP + 129
        ld      h, #0
        ld      a, (AEXP)
        ld      e, a
        ld      d, #0
        or      a
        sbc     hl, de
        ld      de, #129
        add     hl, de
        ld      (EXP16), hl
        ld      a, (ASGN)
        ld      hl, #FSGN
        xor     (hl)
        ld      (hl), a
        xor     a
        ld      (REMX), a
        ld      (QREG+0), a
        ld      (QREG+1), a
        ld      (QREG+2), a
        ld      (QREG+3), a
        ld      b, #26          ; 26 quotient bits (24 + 2 for round)
dv_lp:  push    bc
        ld      a, (REMX)       ; REMX:FM >= AM ?
        or      a
        jr      nz, dv_one
        ld      a, (FM+0)
        ld      hl, #AM+0
        cp      (hl)
        jr      c, dv_zero
        jr      nz, dv_one
        ld      a, (FM+1)
        inc     hl
        cp      (hl)
        jr      c, dv_zero
        jr      nz, dv_one
        ld      a, (FM+2)
        inc     hl
        cp      (hl)
        jr      c, dv_zero
dv_one: ld      a, (FM+2)       ; REM -= AM
        ld      hl, #AM+2
        sub     (hl)
        ld      (FM+2), a
        ld      a, (FM+1)
        dec     hl
        sbc     a, (hl)
        ld      (FM+1), a
        ld      a, (FM+0)
        dec     hl
        sbc     a, (hl)
        ld      (FM+0), a
        ld      a, (REMX)
        sbc     a, #0
        ld      (REMX), a
        scf
        jr      dv_q
dv_zero:
        or      a
dv_q:   ld      hl, #QREG+3     ; QREG = QREG<<1 | CF
        rl      (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        ld      hl, #FM+2       ; REMX:REM <<= 1
        sla     (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        ld      hl, #REMX
        rl      (hl)
        pop     bc
        djnz    dv_lp
        ld      b, #6           ; left-align the 26 bits to 32
dv_al:  ld      hl, #QREG+3
        sla     (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        djnz    dv_al
        ld      a, (QREG+0)
        ld      (FM+0), a
        ld      a, (QREG+1)
        ld      (FM+1), a
        ld      a, (QREG+2)
        ld      (FM+2), a
        ld      a, (QREG+3)
        ld      (GUARD), a
        jp      norm_round

;; ---- compare / sign ------------------------------------------------------------------

;; signed char f_cmp(void)  - A = 1 if FAC > ARG, 0 if equal, 0xFF if FAC < ARG.
;; Reads only; both operands preserved.
_f_cmp:
        ld      a, (FEXP)
        or      a
        jr      nz, cp_f
        ld      a, (AEXP)       ; FAC == 0
        or      a
        jr      z, cp_eq
        ld      a, (ASGN)
        or      a
        jr      nz, cp_gt       ; ARG < 0 -> FAC bigger
        jr      cp_lt
cp_f:   ld      a, (AEXP)
        or      a
        jr      nz, cp_fa
        ld      a, (FSGN)       ; ARG == 0
        or      a
        jr      nz, cp_lt
        jr      cp_gt
cp_fa:  ld      a, (FSGN)
        ld      hl, #ASGN
        xor     (hl)
        jr      z, cp_mag
        ld      a, (FSGN)       ; signs differ
        or      a
        jr      nz, cp_lt       ; FAC negative
        jr      cp_gt
cp_mag: ld      a, (FEXP)       ; same sign: |FAC| vs |ARG|
        ld      hl, #AEXP
        cp      (hl)
        jr      c, cp_small
        jr      nz, cp_big
        ld      a, (FM+0)
        ld      hl, #AM+0
        cp      (hl)
        jr      c, cp_small
        jr      nz, cp_big
        ld      a, (FM+1)
        inc     hl
        cp      (hl)
        jr      c, cp_small
        jr      nz, cp_big
        ld      a, (FM+2)
        inc     hl
        cp      (hl)
        jr      c, cp_small
        jr      nz, cp_big
cp_eq:  xor     a
        ret
cp_big: ld      a, (FSGN)       ; |FAC| > |ARG|
        or      a
        jr      nz, cp_lt       ; both negative -> FAC smaller
cp_gt:  ld      a, #1
        ret
cp_small:
        ld      a, (FSGN)       ; |FAC| < |ARG|
        or      a
        jr      nz, cp_gt       ; both negative -> FAC bigger
cp_lt:  ld      a, #0xFF
        ret

;; signed char f_sgn(void)  - A = 1 / 0 / 0xFF for FAC.
_f_sgn:
        ld      a, (FEXP)
        or      a
        ret     z
        ld      a, (FSGN)
        or      a
        jr      nz, cp_lt
        jr      cp_gt

;; void f_neg(void)
_f_neg:
        ld      a, (FEXP)
        or      a
        ret     z
        ld      a, (FSGN)
        cpl
        ld      (FSGN), a
        ret

;; void f_scale(signed char k)  - FAC *= 2^k  (A = k)
_f_scale:
        ld      c, a
        ld      a, (FEXP)
        or      a
        ret     z
        ld      l, a
        ld      h, #0
        ld      a, c            ; sign-extend k
        ld      e, a
        rlca
        sbc     a, a
        ld      d, a
        add     hl, de
        ld      (EXP16), hl
        xor     a
        ld      (GUARD), a
        jp      norm_round

;; ---- floor -------------------------------------------------------------------------

;; void f_floor(void)  - FAC = largest integer <= FAC (GW INT)
_f_floor:
        ld      a, (FEXP)
        or      a
        ret     z
        cp      #152            ; 24 integer bits: already integral
        ret     nc
        cp      #129
        jr      nc, fl_mask
        ;; |FAC| < 1
        ld      a, (FSGN)
        or      a
        jp      z, set_zero     ; floor of small positive = 0
        ld      a, #129         ; floor of small negative = -1
        ld      (FEXP), a
        ld      a, #0x80
        ld      (FM+0), a
        xor     a
        ld      (FM+1), a
        ld      (FM+2), a
        ret
fl_mask:
        sub     #128            ; A = integer bits 1..23
        ld      b, a
        ld      a, #24
        sub     b               ; A = fraction bits 1..23
        ld      b, a            ; B = fraction bit count
        ld      e, #0           ; E = "dropped anything" flag
        ld      hl, #FM+2
fl_bytes:
        ld      a, b
        cp      #8
        jr      c, fl_part
        ld      a, (hl)         ; clear a whole byte
        or      e
        ld      e, a
        ld      (hl), #0
        dec     hl
        ld      a, b
        sub     #8
        ld      b, a
        jr      nz, fl_bytes
        jr      fl_neg
fl_part:
        or      a
        jr      z, fl_neg
        ld      c, #0xFF        ; mask = (1<<B) - 1
        ld      a, #8
        sub     b
        ld      b, a
fl_msk: srl     c
        djnz    fl_msk
        ld      a, (hl)
        and     c
        or      e
        ld      e, a
        ld      a, c
        cpl
        and     (hl)
        ld      (hl), a
fl_neg: ld      a, (FSGN)
        or      a
        ret     z
        ld      a, e
        or      a
        ret     z               ; negative but exact: done
        ;; negative with a dropped fraction: magnitude += 1 integer ulp.
        ;; The ulp is 1 << F where F = 24 - (FEXP-128) fraction bits (1..23):
        ;; build it in ACC3 by doubling, then add to the mantissa.
        ld      a, (FEXP)
        sub     #128
        ld      b, a
        ld      a, #24
        sub     b
        ld      b, a            ; B = F: ulp = 1 << F, so double F times
        ld      a, #1
        ld      (ACC3+2), a
        xor     a
        ld      (ACC3+1), a
        ld      (ACC3+0), a
fl_dbl: ld      hl, #ACC3+2
        sla     (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        djnz    fl_dbl
fl_addm:
        ld      a, (FM+2)
        ld      hl, #ACC3+2
        add     a, (hl)
        ld      (FM+2), a
        ld      a, (FM+1)
        dec     hl
        adc     a, (hl)
        ld      (FM+1), a
        ld      a, (FM+0)
        dec     hl
        adc     a, (hl)
        ld      (FM+0), a
        ret     nc
        ld      hl, #FM         ; carried out: renormalize right
        rr      (hl)
        inc     hl
        rr      (hl)
        inc     hl
        rr      (hl)
        ld      hl, #FEXP
        inc     (hl)
        ret

;; ---- int conversions -----------------------------------------------------------------

;; int f_toi(void)  - DE = round-to-nearest 16-bit int of FAC (destroys FAC).
;; Out of range -> fac_err = E_OVF, DE = 0x7FFF/0x8000.
_f_toi:
        ld      de, #0
        ld      a, (FEXP)
        or      a
        ret     z
        cp      #128
        jr      nc, ti_ge
        ret                     ; |x| < 0.5 -> 0
ti_ge:  cp      #145
        jr      c, ti_ok
ti_ovf: ld      a, #E_OVF
        ld      (_fac_err), a
        ld      a, (FSGN)
        or      a
        ld      de, #0x7FFF
        ret     z
        ld      de, #0x8000
        ret
ti_ok:  sub     #128
        ld      b, a            ; B = integer bits 0..16
        ld      de, #0
        or      a
        jr      z, ti_rnd
ti_sh:  ld      hl, #FM+2       ; FM <<= 1, top bit -> DE
        sla     (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        rl      e
        rl      d
        djnz    ti_sh
ti_rnd: ld      a, (FM+0)       ; next mantissa bit = round bit
        bit     7, a
        jr      z, ti_sgnf
        inc     de
        ld      a, d
        or      e
        jr      z, ti_ovf       ; wrapped past 0xFFFF
ti_sgnf:
        ld      a, (FSGN)
        or      a
        jr      nz, ti_neg
        bit     7, d            ; positive: must fit 15 bits
        jr      nz, ti_ovf
        ret
ti_neg: ld      a, d            ; negative: magnitude <= 0x8000
        cp      #0x80
        jr      c, ti_negok
        jr      nz, ti_ovf
        ld      a, e
        or      a
        jr      nz, ti_ovf
ti_negok:
        xor     a               ; DE = -DE
        sub     e
        ld      e, a
        ld      a, #0
        sbc     a, d
        ld      d, a
        ret

;; void f_fromi(int v)  - FAC <- HL (signed 16-bit)
_f_fromi:
        xor     a
        ld      (FSGN), a
        ld      a, h
        or      l
        jp      z, set_zero
        bit     7, h
        jr      z, fi_pos
        ld      a, #0xFF
        ld      (FSGN), a
        xor     a               ; HL = -HL
        sub     l
        ld      l, a
        ld      a, #0
        sbc     a, h
        ld      h, a
fi_pos: ld      a, h
        ld      (FM+0), a
        ld      a, l
        ld      (FM+1), a
        xor     a
        ld      (FM+2), a
        ld      (GUARD), a
        ld      hl, #144        ; 16 integer bits
        ld      (EXP16), hl
        jp      norm_round

;; ---- decimal parse ---------------------------------------------------------------------

;; ACC3 (24-bit int, MSB first) = ACC3 * 10 + C   (C = digit 0..9).
;; Preserves DE (f_in keeps its text cursor there).
acc_mul10add:
        push    de
        call    acc_m10a
        pop     de
        ret
acc_m10a:
        ld      a, (ACC3+2)     ; save x
        ld      e, a
        ld      a, (ACC3+1)
        ld      d, a
        ld      a, (ACC3+0)
        ld      b, a            ; B:DE = x
        ld      hl, #ACC3+2     ; x <<= 2
        sla     (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        ld      hl, #ACC3+2
        sla     (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        ld      a, (ACC3+2)     ; x = x*4 + x  (= 5x)
        add     a, e
        ld      (ACC3+2), a
        ld      a, (ACC3+1)
        adc     a, d
        ld      (ACC3+1), a
        ld      a, (ACC3+0)
        adc     a, b
        ld      (ACC3+0), a
        ld      hl, #ACC3+2     ; x <<= 1 (= 10x)
        sla     (hl)
        dec     hl
        rl      (hl)
        dec     hl
        rl      (hl)
        ld      a, (ACC3+2)     ; x += digit
        add     a, c
        ld      (ACC3+2), a
        ret     nc
        ld      hl, #ACC3+1
        inc     (hl)
        ret     nz
        dec     hl
        inc     (hl)
        ret

;; unsigned char f_in(const char **pp)  - parse a decimal number at *pp into
;; FAC, advancing *pp. A = 1 if digits were consumed, 0 otherwise.
_f_in:
        push    hl              ; pp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)         ; DE = p
        xor     a
        ld      (ACC3+0), a
        ld      (ACC3+1), a
        ld      (ACC3+2), a
        ld      (SDIG), a
        ld      (DEXP), a
        ld      (FRAC), a
        ld      (NEGF), a
        ld      (ANYF), a
        ld      a, (de)         ; optional sign
        cp      #0x2D
        jr      nz, in_ps
        ld      a, #0xFF
        ld      (NEGF), a
        inc     de
        jr      in_lp
in_ps:  cp      #0x2B
        jr      nz, in_lp
        inc     de
in_lp:  ld      a, (de)
        cp      #0x2E
        jr      nz, in_dig
        ld      a, (FRAC)
        or      a
        jp      nz, in_done     ; second '.' ends the number
        ld      a, #1
        ld      (FRAC), a
        inc     de
        jr      in_lp
in_dig: cp      #0x30
        jr      c, in_e
        cp      #0x3A
        jr      nc, in_e
        inc     de
        sub     #0x30
        ld      c, a            ; C = digit
        ld      a, #1
        ld      (ANYF), a
        ld      a, (SDIG)
        cp      #7
        jr      c, in_take
        ld      a, (FRAC)       ; accumulator full: integer digits scale up,
        or      a               ; fraction digits are just dropped
        jr      nz, in_lp
        ld      hl, #DEXP
        inc     (hl)
        jr      in_lp
in_take:
        ld      a, c            ; leading zeros: no significance
        or      a
        jr      nz, in_sig
        ld      a, (SDIG)
        or      a
        jr      nz, in_sig
        ld      a, (FRAC)       ; "0.0007": each leading fraction zero /10
        or      a
        jr      z, in_lp
        ld      hl, #DEXP
        dec     (hl)
        jr      in_lp
in_sig: call    acc_mul10add
        ld      hl, #SDIG
        inc     (hl)
        ld      a, (FRAC)
        or      a
        jr      z, in_lp
        ld      hl, #DEXP
        dec     (hl)
        jr      in_lp
in_e:   ld      a, (ANYF)       ; E notation only after digits
        or      a
        jr      z, in_done
        ld      a, (de)
        cp      #0x45
        jr      z, in_e2
        cp      #0x65
        jr      nz, in_done
in_e2:  push    de              ; peek past E: sign/digit required
        inc     de
        ld      a, (de)
        cp      #0x2B
        jr      z, in_e3
        cp      #0x2D
        jr      z, in_e3
        cp      #0x30
        jr      c, in_ebad
        cp      #0x3A
        jr      c, in_e3
in_ebad:
        pop     de              ; not an exponent (e.g. variable E follows)
        jr      in_done
in_e3:  pop     bc              ; discard the saved pointer (DE already past 'E')
        ld      b, #0           ; B = exp sign flag
        ld      c, #0           ; C = exp value
        ld      a, (de)
        cp      #0x2D
        jr      nz, in_e4
        ld      b, #0xFF
        inc     de
        jr      in_e5
in_e4:  cp      #0x2B
        jr      nz, in_e5
        inc     de
in_e5:  ld      a, (de)
        cp      #0x30
        jr      c, in_eap
        cp      #0x3A
        jr      nc, in_eap
        inc     de
        sub     #0x30
        push    af
        ld      a, c            ; C = C*10 + d (clamp at 99)
        cp      #10
        jr      nc, in_ec
        add     a, a
        ld      l, a
        add     a, a
        add     a, a
        add     a, l            ; A = C*10
        ld      c, a
in_ec:  pop     af
        add     a, c
        ld      c, a
        jr      in_e5
in_eap: ld      a, b
        or      a
        ld      a, c
        jr      z, in_epos
        neg
in_epos:
        ld      hl, #DEXP
        add     a, (hl)
        ld      (hl), a
in_done:
        pop     hl              ; pp: store the advanced pointer back
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ;; FAC = ACC3 as a 24-bit integer
        xor     a
        ld      (GUARD), a
        ld      a, (ACC3+0)
        ld      (FM+0), a
        ld      a, (ACC3+1)
        ld      (FM+1), a
        ld      a, (ACC3+2)
        ld      (FM+2), a
        ld      a, (NEGF)
        ld      (FSGN), a
        ld      hl, #152        ; 24 integer bits
        ld      (EXP16), hl
        call    norm_round
        ;; scale by 10^DEXP - in 10^6 chunks first (fewer roundings), then 10s
        ld      a, (DEXP)
        or      a
        jr      z, in_ret
        bit     7, a
        jr      nz, in_div
in_mc:  cp      #6              ; DEXP >= 6: one mul by 1e6
        jr      c, in_ms
        push    af
        ld      hl, #C1E6
        call    _f_arg
        call    _f_mul
        pop     af
        sub     #6
        jr      nz, in_mc
        jr      in_ret
in_ms:  ld      b, a
        ld      hl, #CTEN
        call    _f_arg
in_ml:  push    bc
        call    _f_mul
        pop     bc
        djnz    in_ml
        jr      in_ret
in_div: neg
in_dc:  cp      #6              ; DEXP <= -6: one div by 1e6
        jr      c, in_ds
        push    af
        ld      hl, #C1E6
        call    _f_arg
        call    _f_div
        pop     af
        sub     #6
        jr      nz, in_dc
        jr      in_ret
in_ds:  or      a
        jr      z, in_ret
        ld      b, a
        ld      hl, #CTEN
        call    _f_arg
in_dl:  push    bc
        call    _f_div
        pop     bc
        djnz    in_dl
in_ret: ld      a, (ANYF)
        ret

;; ---- decimal format -------------------------------------------------------------------

CTEN:   .db     0x00, 0x00, 0x20, 0x84          ; 10.0
CHALF:  .db     0x00, 0x00, 0x00, 0x80          ; 0.5
C1E6:   .db     0x00, 0x24, 0x74, 0x94          ; 1e6
C1E7:   .db     0x80, 0x96, 0x18, 0x98          ; 1e7
C1E13:  .db     0xE7, 0x84, 0x11, 0xAC          ; 1e13 (format chunk threshold)
C0P1:   .db     0xCD, 0xCC, 0x4C, 0x7D          ; 0.1

;; 24-bit powers of ten, MSB first (1e6 .. 1)
PW10:   .db     0x0F, 0x42, 0x40                ; 1000000
        .db     0x01, 0x86, 0xA0                ; 100000
        .db     0x00, 0x27, 0x10                ; 10000
        .db     0x00, 0x03, 0xE8                ; 1000
        .db     0x00, 0x00, 0x64                ; 100
        .db     0x00, 0x00, 0x0A                ; 10
        .db     0x00, 0x00, 0x01                ; 1

;; void f_out(char *dst)  - format FAC (GW PRINT rules) into dst, NUL-terminated.
;; Destroys FAC and ARG.
_f_out:
        push    hl              ; dst
        ld      a, (FSGN)
        or      a
        ld      a, #0x20        ; ' '
        jr      z, fo_sg
        ld      a, #0x2D
fo_sg:  pop     hl
        ld      (hl), a
        inc     hl
        push    hl
        ld      a, (FEXP)
        or      a
        jr      nz, fo_nz
        pop     hl
        ld      (hl), #0x30
        inc     hl
        ld      (hl), #0
        ret
fo_nz:  xor     a
        ld      (FSGN), a       ; work on the magnitude
        ld      (FOUTE), a
fo_upc: ld      hl, #C1E13      ; chunk: while FAC >= 1e13: /1e7, e10 += 7
        call    _f_arg
        call    _f_cmp
        cp      #0xFF
        jr      z, fo_up
        ld      hl, #C1E7
        call    _f_arg
        call    _f_div
        ld      a, (FOUTE)
        add     a, #7
        ld      (FOUTE), a
        jr      fo_upc
fo_up:  ld      hl, #C1E7       ; while FAC >= 1e7: /10, e10++
        call    _f_arg
        call    _f_cmp
        cp      #0xFF
        jr      z, fo_dnc
        ld      hl, #CTEN
        call    _f_arg
        call    _f_div
        ld      hl, #FOUTE
        inc     (hl)
        jr      fo_up
fo_dnc: ld      hl, #C0P1       ; chunk: while FAC < 0.1: *1e7, e10 -= 7
        call    _f_arg
        call    _f_cmp
        cp      #0xFF
        jr      nz, fo_dn
        ld      hl, #C1E7
        call    _f_arg
        call    _f_mul
        ld      a, (FOUTE)
        sub     #7
        ld      (FOUTE), a
        jr      fo_dnc
fo_dn:  ld      hl, #C1E6       ; while FAC < 1e6: *10, e10--
        call    _f_arg
        call    _f_cmp
        cp      #0xFF
        jr      nz, fo_rnd
        ld      hl, #CTEN
        call    _f_arg
        call    _f_mul
        ld      hl, #FOUTE
        dec     (hl)
        jr      fo_dn
fo_rnd: ld      a, (FEXP)       ; FEXP 152 = an exact 24-bit integer: adding .5
        cp      #152            ; would round the mantissa up (9999999 -> 1E+07)
        jr      z, fo_int
        ld      hl, #CHALF      ; D = int(FAC + 0.5), 7 digits
        call    _f_arg
        call    _f_add
fo_int:
        ;; integer part: FEXP in 148..152 -> shift right (152-FEXP)
        ld      a, (FM+0)
        ld      (ACC3+0), a
        ld      a, (FM+1)
        ld      (ACC3+1), a
        ld      a, (FM+2)
        ld      (ACC3+2), a
        ld      a, #152
        ld      hl, #FEXP
        sub     (hl)
        jr      z, fo_dig
        ld      b, a
fo_shr: ld      hl, #ACC3
        srl     (hl)
        inc     hl
        rr      (hl)
        inc     hl
        rr      (hl)
        djnz    fo_shr
fo_dig: ;; rounding may have pushed to exactly 1e7
        ld      a, (ACC3+0)
        cp      #0x98            ; 1e7 = 0x989680
        jr      c, fo_d7
        jr      nz, fo_dig7chk
        ld      a, (ACC3+1)
        cp      #0x96
        jr      c, fo_d7
        jr      nz, fo_dig7chk
        ld      a, (ACC3+2)
        cp      #0x80
        jr      c, fo_d7
fo_dig7chk:
        ld      a, #0x0F         ; -> 1000000, e10++
        ld      (ACC3+0), a
        ld      a, #0x42
        ld      (ACC3+1), a
        ld      a, #0x40
        ld      (ACC3+2), a
        ld      hl, #FOUTE
        inc     (hl)
fo_d7:  ;; extract 7 decimal digits by power-of-ten subtraction
        push    ix              ; IX is callee-saved under __sdcccall(1)
        ld      ix, #PW10
        ld      de, #DIGS
        ld      b, #7
fo_dl:  push    bc
        ld      c, #0
fo_sub: ld      a, (ACC3+2)     ; ACC3 - PW10[i] ?
        sub     a, 2 (ix)
        ld      l, a
        ld      a, (ACC3+1)
        sbc     a, 1 (ix)
        ld      h, a
        ld      a, (ACC3+0)
        sbc     a, 0 (ix)
        jr      c, fo_dnx       ; went negative: digit done
        ld      (ACC3+0), a
        ld      a, h
        ld      (ACC3+1), a
        ld      a, l
        ld      (ACC3+2), a
        inc     c
        jr      fo_sub
fo_dnx: ld      a, c
        add     a, #0x30
        ld      (de), a
        inc     de
        inc     ix
        inc     ix
        inc     ix
        pop     bc
        djnz    fo_dl
        pop     ix
        ;; significant digit count (strip trailing zeros, keep >= 1)
        ld      b, #7
        ld      hl, #DIGS+6
fo_st:  ld      a, (hl)
        cp      #0x30
        jr      nz, fo_lay
        dec     hl
        dec     b
        ld      a, b
        cp      #1
        jr      nz, fo_st
fo_lay: ;; B = ndig; true decimal exponent e10 = FOUTE + 6 (D is d0.dddddd*1e6)
        pop     hl              ; dst (past the sign char)
        ld      a, (FOUTE)
        add     a, #6
        ld      c, a            ; C = e10
        cp      #7
        jp      p, fo_e         ; e10 > 6 -> E notation (signed compare: 7..127)
        cp      #-3
        jp      m, fo_e         ; e10 < -3 -> E notation
        ;; fixed: pt = e10 + 1 digits before the point
        ld      a, c
        inc     a
        ld      c, a            ; C = pt (-2 .. 7)
        or      a
        jr      z, fo_fr
        jp      m, fo_fr
        ;; pt > 0: emit digits, point at pt, pad zeros to pt
        ld      de, #DIGS
fo_ip:  ld      a, b            ; digits remaining
        or      a
        jr      z, fo_ip0
        ld      a, (de)
        inc     de
        ld      (hl), a
        inc     hl
        dec     b
        dec     c
        jr      nz, fo_ip
        ;; point (only if digits remain)
        ld      a, b
        or      a
        jr      z, fo_fin
        ld      (hl), #0x2E
        inc     hl
fo_fp:  ld      a, (de)
        inc     de
        ld      (hl), a
        inc     hl
        djnz    fo_fp
        jr      fo_fin
fo_ip0: ld      (hl), #0x30      ; ran out of digits before the point: pad
        inc     hl
        dec     c
        jr      nz, fo_ip0
        jr      fo_fin
fo_fr:  ;; pt <= 0: ".000ddd"
        ld      (hl), #0x2E
        inc     hl
        ld      a, c
        or      a
        jr      z, fo_fd
fo_fz:  ld      (hl), #0x30
        inc     hl
        inc     a
        jr      nz, fo_fz
fo_fd:  ld      de, #DIGS
fo_fdl: ld      a, (de)
        inc     de
        ld      (hl), a
        inc     hl
        djnz    fo_fdl
        jr      fo_fin
fo_e:   ;; E notation: d[.ddd]E±xx
        ld      de, #DIGS
        ld      a, (de)
        inc     de
        ld      (hl), a
        inc     hl
        dec     b
        jr      z, fo_ee
        ld      (hl), #0x2E
        inc     hl
fo_el:  ld      a, (de)
        inc     de
        ld      (hl), a
        inc     hl
        djnz    fo_el
fo_ee:  ld      (hl), #0x45
        inc     hl
        ld      a, c
        bit     7, a
        jr      z, fo_ep
        ld      (hl), #0x2D
        neg
        jr      fo_ex
fo_ep:  ld      (hl), #0x2B
fo_ex:  inc     hl
        ld      b, #0
fo_et:  cp      #10
        jr      c, fo_eu
        sub     #10
        inc     b
        jr      fo_et
fo_eu:  ld      c, a
        ld      a, b
        add     a, #0x30
        ld      (hl), a
        inc     hl
        ld      a, c
        add     a, #0x30
        ld      (hl), a
        inc     hl
fo_fin: ld      (hl), #0
        ret
