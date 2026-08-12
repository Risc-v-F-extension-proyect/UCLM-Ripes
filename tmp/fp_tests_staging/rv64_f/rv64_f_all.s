# RV64 + F: 12 instrucciones implementadas
# 23 casos. gp identifica el primero que falla.

.data
.align 3
result: .zero 8
a1: .word 0x3fc00000
e1: .word 0x3fc00000
a2: .word 0xc0200000
e2: .word 0xc0200000
a3: .word 0x7f800000
e3: .word 0x7f800000
a4: .word 0x3fc00000
b4: .word 0x40100000
e4: .word 0x40700000
a5: .word 0xc0800000
b5: .word 0x3fc00000
e5: .word 0xc0200000
a6: .word 0x40b00000
b6: .word 0x40000000
e6: .word 0x40600000
a7: .word 0xc0400000
b7: .word 0x40000000
e7: .word 0xc0a00000
a8: .word 0x3fc00000
b8: .word 0x40800000
e8: .word 0x40c00000
a9: .word 0xc0200000
b9: .word 0x40400000
e9: .word 0xc0f00000
a10: .word 0x40f00000
b10: .word 0x40200000
e10: .word 0x40400000
a11: .word 0xc1100000
b11: .word 0x40800000
e11: .word 0xc0100000
a12: .word 0x40800000
e12: .word 0x40000000
a13: .word 0x40000000
e13: .word 0x3fb504f3
a14: .word 0x40400000
b14: .word 0xc0000000
e14: .word 0xc0000000
a15: .word 0x00000000
b15: .word 0x80000000
e15: .word 0x80000000
a16: .word 0x40400000
b16: .word 0xc0000000
e16: .word 0x40400000
a17: .word 0x00000000
b17: .word 0x80000000
e17: .word 0x00000000
a18: .word 0x40600000
b18: .word 0xbf800000
e18: .word 0xc0600000
a19: .word 0xc0600000
b19: .word 0x3f800000
e19: .word 0x40600000
a20: .word 0x40600000
b20: .word 0x3f800000
e20: .word 0xc0600000
a21: .word 0xc0600000
b21: .word 0xbf800000
e21: .word 0x40600000
a22: .word 0x40600000
b22: .word 0xbf800000
e22: .word 0xc0600000
a23: .word 0xc0600000
b23: .word 0xbf800000
e23: .word 0x40600000

.text
.globl main
main:

# 1: flw/fsw; 1.5
  li gp, 1
  la t0, a1
  flw f0, 0(t0)
  la t0, result
  fsw f0, 0(t0)
  lw t1, 0(t0)
  la t2, e1
  lw t3, 0(t2)
  bne t1, t3, fail

# 2: flw/fsw; -2.5
  li gp, 2
  la t0, a2
  flw f0, 0(t0)
  la t0, result
  fsw f0, 0(t0)
  lw t1, 0(t0)
  la t2, e2
  lw t3, 0(t2)
  bne t1, t3, fail

# 3: flw/fsw; +Inf
  li gp, 3
  la t0, a3
  flw f0, 0(t0)
  la t0, result
  fsw f0, 0(t0)
  lw t1, 0(t0)
  la t2, e3
  lw t3, 0(t2)
  bne t1, t3, fail

# 4: fadd.s; 1.5+2.25
  li gp, 4
  la t0, a4
  flw f0, 0(t0)
  la t0, b4
  flw f1, 0(t0)
  fadd.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e4
  lw t3, 0(t2)
  bne t1, t3, fail

# 5: fadd.s; -4+1.5
  li gp, 5
  la t0, a5
  flw f0, 0(t0)
  la t0, b5
  flw f1, 0(t0)
  fadd.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e5
  lw t3, 0(t2)
  bne t1, t3, fail

# 6: fsub.s; 5.5-2
  li gp, 6
  la t0, a6
  flw f0, 0(t0)
  la t0, b6
  flw f1, 0(t0)
  fsub.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e6
  lw t3, 0(t2)
  bne t1, t3, fail

# 7: fsub.s; -3-2
  li gp, 7
  la t0, a7
  flw f0, 0(t0)
  la t0, b7
  flw f1, 0(t0)
  fsub.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e7
  lw t3, 0(t2)
  bne t1, t3, fail

# 8: fmul.s; 1.5*4
  li gp, 8
  la t0, a8
  flw f0, 0(t0)
  la t0, b8
  flw f1, 0(t0)
  fmul.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e8
  lw t3, 0(t2)
  bne t1, t3, fail

# 9: fmul.s; -2.5*3
  li gp, 9
  la t0, a9
  flw f0, 0(t0)
  la t0, b9
  flw f1, 0(t0)
  fmul.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e9
  lw t3, 0(t2)
  bne t1, t3, fail

# 10: fdiv.s; 7.5/2.5
  li gp, 10
  la t0, a10
  flw f0, 0(t0)
  la t0, b10
  flw f1, 0(t0)
  fdiv.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e10
  lw t3, 0(t2)
  bne t1, t3, fail

# 11: fdiv.s; -9/4
  li gp, 11
  la t0, a11
  flw f0, 0(t0)
  la t0, b11
  flw f1, 0(t0)
  fdiv.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e11
  lw t3, 0(t2)
  bne t1, t3, fail

# 12: fsqrt.s; sqrt(4)
  li gp, 12
  la t0, a12
  flw f0, 0(t0)
  fsqrt.s f2, f0
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e12
  lw t3, 0(t2)
  bne t1, t3, fail

# 13: fsqrt.s; sqrt(2) redondeado
  li gp, 13
  la t0, a13
  flw f0, 0(t0)
  fsqrt.s f2, f0
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e13
  lw t3, 0(t2)
  bne t1, t3, fail

# 14: fmin.s; min(3,-2)
  li gp, 14
  la t0, a14
  flw f0, 0(t0)
  la t0, b14
  flw f1, 0(t0)
  fmin.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e14
  lw t3, 0(t2)
  bne t1, t3, fail

# 15: fmin.s; min(+0,-0)
  li gp, 15
  la t0, a15
  flw f0, 0(t0)
  la t0, b15
  flw f1, 0(t0)
  fmin.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e15
  lw t3, 0(t2)
  bne t1, t3, fail

# 16: fmax.s; max(3,-2)
  li gp, 16
  la t0, a16
  flw f0, 0(t0)
  la t0, b16
  flw f1, 0(t0)
  fmax.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e16
  lw t3, 0(t2)
  bne t1, t3, fail

# 17: fmax.s; max(+0,-0)
  li gp, 17
  la t0, a17
  flw f0, 0(t0)
  la t0, b17
  flw f1, 0(t0)
  fmax.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e17
  lw t3, 0(t2)
  bne t1, t3, fail

# 18: fsgnj.s; copia signo negativo
  li gp, 18
  la t0, a18
  flw f0, 0(t0)
  la t0, b18
  flw f1, 0(t0)
  fsgnj.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e18
  lw t3, 0(t2)
  bne t1, t3, fail

# 19: fsgnj.s; copia signo positivo
  li gp, 19
  la t0, a19
  flw f0, 0(t0)
  la t0, b19
  flw f1, 0(t0)
  fsgnj.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e19
  lw t3, 0(t2)
  bne t1, t3, fail

# 20: fsgnjn.s; niega signo positivo
  li gp, 20
  la t0, a20
  flw f0, 0(t0)
  la t0, b20
  flw f1, 0(t0)
  fsgnjn.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e20
  lw t3, 0(t2)
  bne t1, t3, fail

# 21: fsgnjn.s; niega signo negativo
  li gp, 21
  la t0, a21
  flw f0, 0(t0)
  la t0, b21
  flw f1, 0(t0)
  fsgnjn.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e21
  lw t3, 0(t2)
  bne t1, t3, fail

# 22: fsgnjx.s; XOR signos +,-
  li gp, 22
  la t0, a22
  flw f0, 0(t0)
  la t0, b22
  flw f1, 0(t0)
  fsgnjx.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e22
  lw t3, 0(t2)
  bne t1, t3, fail

# 23: fsgnjx.s; XOR signos -,-
  li gp, 23
  la t0, a23
  flw f0, 0(t0)
  la t0, b23
  flw f1, 0(t0)
  fsgnjx.s f2, f0, f1
  la t0, result
  fsw f2, 0(t0)
  lw t1, 0(t0)
  la t2, e23
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

