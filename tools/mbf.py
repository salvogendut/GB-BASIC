#!/usr/bin/env python3
"""mbf.py - Microsoft Binary Format (single, 32-bit) reference for GB-BASIC.

The BASRUN float engine (apps/basrun/fac.s) stores numbers in MBF single:
  byte0 = mantissa low, byte1 = mantissa mid,
  byte2 = sign(bit7) | mantissa high 7 bits (leading 1 implicit),
  byte3 = exponent (0 = the value 0; bias 128: value = 0.1mmm... * 2^(exp-128))

Used to generate packed constants for C/asm sources and golden values for the
engine test app. `python3 tools/mbf.py 3.14159` prints the encoding;
`--c NAME V` prints a C initializer; `--tbl` dumps the constants fac.s needs.
"""
import math, struct, sys


def encode(v: float):
    if v == 0.0:
        return bytes(4)
    sign = 0x80 if v < 0 else 0
    m = abs(v)
    e = math.frexp(m)[1]          # m = f * 2^e, f in [0.5,1)
    f = math.frexp(m)[0]
    exp = e + 128
    mant = round(f * (1 << 24))   # 24-bit mantissa incl. leading 1
    if mant >= (1 << 24):         # rounding overflowed
        mant >>= 1
        exp += 1
    if exp <= 0:
        return bytes(4)           # underflow -> 0
    if exp > 255:
        raise OverflowError(v)
    return bytes((mant & 0xFF, (mant >> 8) & 0xFF,
                  ((mant >> 16) & 0x7F) | sign, exp))


def decode(b) -> float:
    if b[3] == 0:
        return 0.0
    mant = b[0] | (b[1] << 8) | ((b[2] | 0x80) << 16)
    v = mant / (1 << 24) * 2.0 ** (b[3] - 128)
    return -v if (b[2] & 0x80) else v


def cinit(name, v):
    b = encode(v)
    return "static const num_t %s = {{0x%02X,0x%02X,0x%02X,0x%02X}}; /* %r */" % (
        name, b[0], b[1], b[2], b[3], v)


def asmdb(name, v):
    b = encode(v)
    return "%s: .db 0x%02X,0x%02X,0x%02X,0x%02X   ; %r" % (name, b[0], b[1], b[2], b[3], v)


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--tbl":
        for n, v in [("CTEN", 10.0), ("CHALF", 0.5), ("C1E6", 1e6), ("C1E7", 1e7)]:
            print(asmdb(n, v))
        for n, v in [("C_PIH", math.pi / 2), ("C_2OPI", 2 / math.pi),
                     ("C_S3", -1.0 / 6), ("C_S5", 1.0 / 120), ("C_S7", -1.0 / 5040),
                     ("C_C2", -0.5), ("C_C4", 1.0 / 24), ("C_C6", -1.0 / 720),
                     ("C_ONE", 1.0)]:
            print(cinit(n, v))
    elif a and a[0] == "--c":
        print(cinit(a[1], float(a[2])))
    else:
        for s in a:
            b = encode(float(s))
            print(s, "->", b.hex(), "->", decode(b))
