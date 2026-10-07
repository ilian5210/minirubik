/* Same number of stores for every memory case. Only the address span changes.
 * The two words are patched before loading, just like the solver input.
 * Deliberately outside the solver data budget: this characterizes Ripes.
 */
.text
.globl _start
_start:
    la t0, probe_words
    lw t1, 0(t0)
    slli t1, t1, 2
    li t2, 0x01000000
    add t3, t2, t1
    la t0, probe_stores
    lw t4, 0(t0)
    mv t0, t2
probe_loop:
    sw t4, 0(t0)
    addi t0, t0, 4
    bltu t0, t3, probe_no_wrap
    mv t0, t2
probe_no_wrap:
    addi t4, t4, -1
    bnez t4, probe_loop
    li a0, 0
    li a7, 93
    ecall
.section .rodata
.globl probe_words, probe_stores
probe_words: .word 1024
probe_stores: .word 1048576
