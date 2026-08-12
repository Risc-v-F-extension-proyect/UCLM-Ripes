# Suite FP enfocada

Compara bit a bit resultados IEEE 754 del subconjunto implementado.

- `rv32_f/rv32_f_all.s`: RV32 + F, 23 casos para 12 instrucciones.
- `rv64_f/rv64_f_all.s`: RV64 + F, 23 casos para 12 instrucciones.
- `rv64_d/rv64_d_all.s`: RV64 + D, 29 casos para 12 instrucciones D y 2 conversiones.

Éxito: ecall 93 con `a0=42`. Fallo: `a0=0` y `gp` indica el primer caso incorrecto.

Configuración: selecciona RV32/F, RV64/F o RV64/D según la carpeta. D habilita F automáticamente.

