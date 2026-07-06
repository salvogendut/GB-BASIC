;; facvec.s - in-bank thunks to the BASRUN2.BIN float-engine overlay.
;;
;; The engine (fac.s) lives in low RAM at LR_ENGINE (0x2200) behind a fixed
;; jump-vector table; these 3-byte thunks keep every C call site a direct
;; CALL. fac_err lives at LR_ENGDATA (0x2C60) - see basrun.h.
        .module facvec
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
        .globl  _f_exp
        .globl  _f_scale
        .globl  _f_floor
        .globl  _f_toi
        .globl  _f_fromi
        .globl  _f_in
        .globl  _f_out
        .globl  _g_pset
        .globl  _g_line
        .globl  _g_box
        .globl  _g_boxf
        .globl  _g_circle

ENG     =       0x2200

        .area   _CODE
_f_ld:          jp      ENG+0
_f_arg:         jp      ENG+3
_f_st:          jp      ENG+6
_f_fac2arg:     jp      ENG+9
_f_add:         jp      ENG+12
_f_sub:         jp      ENG+15
_f_mul:         jp      ENG+18
_f_div:         jp      ENG+21
_f_cmp:         jp      ENG+24
_f_sgn:         jp      ENG+27
_f_neg:         jp      ENG+30
_f_exp:         jp      ENG+33
_f_scale:       jp      ENG+36
_f_floor:       jp      ENG+39
_f_toi:         jp      ENG+42
_f_fromi:       jp      ENG+45
_f_in:          jp      ENG+48
_f_out:         jp      ENG+51
_g_pset:        jp      ENG+54
_g_line:        jp      ENG+57
_g_box:         jp      ENG+60
_g_boxf:        jp      ENG+63
_g_circle:      jp      ENG+66
