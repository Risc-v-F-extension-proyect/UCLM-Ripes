# RV64+D: 29 casos para 12 instrucciones D y 2 conversiones.
# gp identifica el primero que falla.

.data
.align 3
dresult: .zero 8
fresult: .zero 4
da1: .dword 0x3ff8000000000000
de1: .dword 0x3ff8000000000000
da2: .dword 0xc004000000000000
de2: .dword 0xc004000000000000
da3: .dword 0x7ff0000000000000
de3: .dword 0x7ff0000000000000
da4: .dword 0x3ff8000000000000
db4: .dword 0x4002000000000000
de4: .dword 0x400e000000000000
da5: .dword 0xc010000000000000
db5: .dword 0x3ff8000000000000
de5: .dword 0xc004000000000000
da6: .dword 0x4016000000000000
db6: .dword 0x4000000000000000
de6: .dword 0x400c000000000000
da7: .dword 0xc008000000000000
db7: .dword 0x4000000000000000
de7: .dword 0xc014000000000000
da8: .dword 0x3ff8000000000000
db8: .dword 0x4010000000000000
de8: .dword 0x4018000000000000
da9: .dword 0xc004000000000000
db9: .dword 0x4008000000000000
de9: .dword 0xc01e000000000000
da10: .dword 0x401e000000000000
db10: .dword 0x4004000000000000
de10: .dword 0x4008000000000000
da11: .dword 0xc022000000000000
db11: .dword 0x4010000000000000
de11: .dword 0xc002000000000000
da12: .dword 0x4010000000000000
de12: .dword 0x4000000000000000
da13: .dword 0x4000000000000000
de13: .dword 0x3ff6a09e667f3bcd
da14: .dword 0x4008000000000000
db14: .dword 0xc000000000000000
de14: .dword 0xc000000000000000
da15: .dword 0x0000000000000000
db15: .dword 0x8000000000000000
de15: .dword 0x8000000000000000
da16: .dword 0x4008000000000000
db16: .dword 0xc000000000000000
de16: .dword 0x4008000000000000
da17: .dword 0x0000000000000000
db17: .dword 0x8000000000000000
de17: .dword 0x0000000000000000
da18: .dword 0x400c000000000000
db18: .dword 0xbff0000000000000
de18: .dword 0xc00c000000000000
da19: .dword 0xc00c000000000000
db19: .dword 0x3ff0000000000000
de19: .dword 0x400c000000000000
da20: .dword 0x400c000000000000
db20: .dword 0x3ff0000000000000
de20: .dword 0xc00c000000000000
da21: .dword 0xc00c000000000000
db21: .dword 0xbff0000000000000
de21: .dword 0x400c000000000000
da22: .dword 0x400c000000000000
db22: .dword 0xbff0000000000000
de22: .dword 0xc00c000000000000
da23: .dword 0xc00c000000000000
db23: .dword 0xbff0000000000000
de23: .dword 0x400c000000000000
fa24: .word 0x3fc00000
dce24: .dword 0x3ff8000000000000
fa25: .word 0xc0200000
dce25: .dword 0xc004000000000000
fa26: .word 0x40490fdb
dce26: .dword 0x400921fb60000000
ca27: .dword 0x3ff8000000000000
fce27: .word 0x3fc00000
ca28: .dword 0xc004000000000000
fce28: .word 0xc0200000
ca29: .dword 0x400921fb54442d18
fce29: .word 0x40490fdb

.text
.globl main
main:

# 1: fld/fsd; 1.5
  li gp, 1
  la t0, da1
  fld f0, 0(t0)
  la t0, dresult
  fsd f0, 0(t0)
  ld t1, 0(t0)
  la t2, de1
  ld t3, 0(t2)
  bne t1, t3, fail

# 2: fld/fsd; -2.5
  li gp, 2
  la t0, da2
  fld f0, 0(t0)
  la t0, dresult
  fsd f0, 0(t0)
  ld t1, 0(t0)
  la t2, de2
  ld t3, 0(t2)
  bne t1, t3, fail

# 3: fld/fsd; +Inf
  li gp, 3
  la t0, da3
  fld f0, 0(t0)
  la t0, dresult
  fsd f0, 0(t0)
  ld t1, 0(t0)
  la t2, de3
  ld t3, 0(t2)
  bne t1, t3, fail

# 4: fadd.d; 1.5+2.25
  li gp, 4
  la t0, da4
  fld f0, 0(t0)
  la t0, db4
  fld f1, 0(t0)
  fadd.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de4
  ld t3, 0(t2)
  bne t1, t3, fail

# 5: fadd.d; -4+1.5
  li gp, 5
  la t0, da5
  fld f0, 0(t0)
  la t0, db5
  fld f1, 0(t0)
  fadd.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de5
  ld t3, 0(t2)
  bne t1, t3, fail

# 6: fsub.d; 5.5-2
  li gp, 6
  la t0, da6
  fld f0, 0(t0)
  la t0, db6
  fld f1, 0(t0)
  fsub.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de6
  ld t3, 0(t2)
  bne t1, t3, fail

# 7: fsub.d; -3-2
  li gp, 7
  la t0, da7
  fld f0, 0(t0)
  la t0, db7
  fld f1, 0(t0)
  fsub.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de7
  ld t3, 0(t2)
  bne t1, t3, fail

# 8: fmul.d; 1.5*4
  li gp, 8
  la t0, da8
  fld f0, 0(t0)
  la t0, db8
  fld f1, 0(t0)
  fmul.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de8
  ld t3, 0(t2)
  bne t1, t3, fail

# 9: fmul.d; -2.5*3
  li gp, 9
  la t0, da9
  fld f0, 0(t0)
  la t0, db9
  fld f1, 0(t0)
  fmul.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de9
  ld t3, 0(t2)
  bne t1, t3, fail

# 10: fdiv.d; 7.5/2.5
  li gp, 10
  la t0, da10
  fld f0, 0(t0)
  la t0, db10
  fld f1, 0(t0)
  fdiv.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de10
  ld t3, 0(t2)
  bne t1, t3, fail

# 11: fdiv.d; -9/4
  li gp, 11
  la t0, da11
  fld f0, 0(t0)
  la t0, db11
  fld f1, 0(t0)
  fdiv.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de11
  ld t3, 0(t2)
  bne t1, t3, fail

# 12: fsqrt.d; sqrt(4)
  li gp, 12
  la t0, da12
  fld f0, 0(t0)
  fsqrt.d f2, f0
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de12
  ld t3, 0(t2)
  bne t1, t3, fail

# 13: fsqrt.d; sqrt(2) redondeado
  li gp, 13
  la t0, da13
  fld f0, 0(t0)
  fsqrt.d f2, f0
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de13
  ld t3, 0(t2)
  bne t1, t3, fail

# 14: fmin.d; min(3,-2)
  li gp, 14
  la t0, da14
  fld f0, 0(t0)
  la t0, db14
  fld f1, 0(t0)
  fmin.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de14
  ld t3, 0(t2)
  bne t1, t3, fail

# 15: fmin.d; min(+0,-0)
  li gp, 15
  la t0, da15
  fld f0, 0(t0)
  la t0, db15
  fld f1, 0(t0)
  fmin.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de15
  ld t3, 0(t2)
  bne t1, t3, fail

# 16: fmax.d; max(3,-2)
  li gp, 16
  la t0, da16
  fld f0, 0(t0)
  la t0, db16
  fld f1, 0(t0)
  fmax.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de16
  ld t3, 0(t2)
  bne t1, t3, fail

# 17: fmax.d; max(+0,-0)
  li gp, 17
  la t0, da17
  fld f0, 0(t0)
  la t0, db17
  fld f1, 0(t0)
  fmax.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de17
  ld t3, 0(t2)
  bne t1, t3, fail

# 18: fsgnj.d; copia signo negativo
  li gp, 18
  la t0, da18
  fld f0, 0(t0)
  la t0, db18
  fld f1, 0(t0)
  fsgnj.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de18
  ld t3, 0(t2)
  bne t1, t3, fail

# 19: fsgnj.d; copia signo positivo
  li gp, 19
  la t0, da19
  fld f0, 0(t0)
  la t0, db19
  fld f1, 0(t0)
  fsgnj.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de19
  ld t3, 0(t2)
  bne t1, t3, fail

# 20: fsgnjn.d; niega signo positivo
  li gp, 20
  la t0, da20
  fld f0, 0(t0)
  la t0, db20
  fld f1, 0(t0)
  fsgnjn.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de20
  ld t3, 0(t2)
  bne t1, t3, fail

# 21: fsgnjn.d; niega signo negativo
  li gp, 21
  la t0, da21
  fld f0, 0(t0)
  la t0, db21
  fld f1, 0(t0)
  fsgnjn.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de21
  ld t3, 0(t2)
  bne t1, t3, fail

# 22: fsgnjx.d; XOR signos +,-
  li gp, 22
  la t0, da22
  fld f0, 0(t0)
  la t0, db22
  fld f1, 0(t0)
  fsgnjx.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de22
  ld t3, 0(t2)
  bne t1, t3, fail

# 23: fsgnjx.d; XOR signos -,-
  li gp, 23
  la t0, da23
  fld f0, 0(t0)
  la t0, db23
  fld f1, 0(t0)
  fsgnjx.d f2, f0, f1
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, de23
  ld t3, 0(t2)
  bne t1, t3, fail

# 24: fcvt.d.s; 1.5f
  li gp, 24
  la t0, fa24
  flw f0, 0(t0)
  fcvt.d.s f2, f0
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, dce24
  ld t3, 0(t2)
  bne t1, t3, fail

# 25: fcvt.d.s; -2.5f
  li gp, 25
  la t0, fa25
  flw f0, 0(t0)
  fcvt.d.s f2, f0
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, dce25
  ld t3, 0(t2)
  bne t1, t3, fail

# 26: fcvt.d.s; pi float
  li gp, 26
  la t0, fa26
  flw f0, 0(t0)
  fcvt.d.s f2, f0
  la t0, dresult
  fsd f2, 0(t0)
  ld t1, 0(t0)
  la t2, dce26
  ld t3, 0(t2)
  bne t1, t3, fail

# 27: fcvt.s.d; 1.5
  li gp, 27
  la t0, ca27
  fld f0, 0(t0)
  fcvt.s.d f2, f0
  la t0, fresult
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, fce27
  lw t3, 0(t2)
  bne t1, t3, fail

# 28: fcvt.s.d; -2.5
  li gp, 28
  la t0, ca28
  fld f0, 0(t0)
  fcvt.s.d f2, f0
  la t0, fresult
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, fce28
  lw t3, 0(t2)
  bne t1, t3, fail

# 29: fcvt.s.d; pi double
  li gp, 29
  la t0, ca29
  fld f0, 0(t0)
  fcvt.s.d f2, f0
  la t0, fresult
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, fce29
  lw t3, 0(t2)
  bne t1, t3, fail

pass:
  li a0, 42
  li a7, 93
  ecall
fail:
  li a0, 0
  li a7, 93
  ecall

