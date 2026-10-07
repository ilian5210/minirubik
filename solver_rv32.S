#ifndef STATE
#define STATE "21345671111111"
#endif
#ifndef EARLY_PERM
#define EARLY_PERM 1
#endif
#ifndef RENDER
#define RENDER 0
#endif
#ifndef RENDER_DELAY
#define RENDER_DELAY 40000
#endif
#ifndef BRANCHLESS_TWIST
#define BRANCHLESS_TWIST 0
#endif
#if RENDER
#ifndef LED_MATRIX_0_BASE
#error Create LED Matrix 0 in Ripes and include its ripes_system.h
#endif
#if LED_MATRIX_0_WIDTH < 35 || LED_MATRIX_0_HEIGHT < 25
#error The cube net requires a LED matrix at least 35 wide by 25 tall
#endif
#ifdef C_REFERENCE
#error The C reference is a renderer-free measurement build
#endif
#endif

.text
.globl _start
_start:
    la sp, stack_end
#ifdef C_REFERENCE
    call c_main
    j exit_status
#else
    la a0, input_state
    la a1, cube
    call parse_state
    bnez a0, invalid_input
#if RENDER
    /* Replay validation mutates cube; retain the input for the live display. */
    la t0, cube
    la t1, render_cube
    li t2, 14
render_save_input:
    lbu t3, 0(t0)
    sb t3, 0(t1)
    addi t0, t0, 1
    addi t1, t1, 1
    addi t2, t2, -1
    bnez t2, render_save_input
    call render_frame
    call render_pause
#endif
    la a0, cube
    call rank_perm
    mv s0, a0
    la a0, cube
    addi a0, a0, 7
    call rank_ori
    mv s1, a0
    la a0, cube
    call rank_partial
    mv a2, a0
    mv a0, s0
    mv a1, s1
    la a3, solution
    call ida_solve
    bltz a0, search_failed
    mv s0, a0
    la t0, solution_length
    sw a0, 0(t0)
    la t0, search_nodes
    sw a1, 0(t0)
    la t0, expected_length
    lw t0, 0(t0)
    bltz t0, expected_length_ok
    bne s0, t0, verify_failed
expected_length_ok:
    /* T5: replay the actual path in the independent cubie model. */
    la a0, cube
    la a1, solution
    mv a2, s0
    call verify_solution
    bnez a0, verify_failed
    li s1, 0
    la s2, solution
print_loop:
    beq s1, s0, print_done
    beqz s1, print_move
    li a0, 32
    li a7, 11
    ecall
print_move:
    add t0, s2, s1
    lbu t0, 0(t0)
    slli t0, t0, 2
    la t1, move_names
    add t0, t0, t1
    lbu a0, 0(t0)
    li a7, 11
    ecall
    lbu a0, 1(t0)
    beqz a0, print_next
    ecall
print_next:
#if RENDER
    add t0, s2, s1
    lbu a0, 0(t0)
    call render_move
    call render_frame
    call render_pause
#endif
    addi s1, s1, 1
    j print_loop
print_done:
    li a0, 10
    li a7, 11
    ecall
    li a0, 0
    j exit_status
invalid_input:
    la a0, invalid_message
    li a7, 4
    ecall
    li a0, 2
    j exit_status
search_failed:
    la a0, search_message
    j failure_message
verify_failed:
    la a0, verify_message
failure_message:
    li a7, 4
    ecall
    li a0, 1
#endif
exit_status:
    la t0, test_status
    sw a0, 0(t0)
    li a7, 93
    ecall

#ifndef C_REFERENCE
/* a0=string, a1=14-byte state; a0=0 valid, -1 invalid.
 * Each character is range-checked before reading the next, including NUL.
 * The second half maintains the orientation sum reduced modulo three.
 */
parse_state:
    li t0, 0
    li t1, 0
    li t2, 7
parse_perm_loop:
    lbu t3, 0(a0)
    addi t3, t3, -49
    bgeu t3, t2, parse_bad
    li t4, 1
    sll t4, t4, t3
    and t5, t1, t4
    bnez t5, parse_bad
    or t1, t1, t4
    sb t3, 0(a1)
    addi a0, a0, 1
    addi a1, a1, 1
    addi t0, t0, 1
    blt t0, t2, parse_perm_loop
    li t0, 0
    li t1, 0
    li t6, 3
parse_ori_loop:
    lbu t3, 0(a0)
    addi t3, t3, -49
    bgeu t3, t6, parse_bad
    sb t3, 0(a1)
    add t1, t1, t3
    blt t1, t6, parse_ori_reduced
    addi t1, t1, -3
parse_ori_reduced:
    addi a0, a0, 1
    addi a1, a1, 1
    addi t0, t0, 1
    blt t0, t2, parse_ori_loop
    lbu t3, 0(a0)
    or t3, t3, t1
    bnez t3, parse_bad
    li a0, 0
    ret
parse_bad:
    li a0, -1
    ret

/* Lehmer rank: load the seven cubies once, then 21 unsigned comparisons.
 * Horner multipliers 6,5,4,3,2 use shifts/adds, with no helper calls.
 */
rank_perm:
    lbu a2, 0(a0)
    lbu a3, 1(a0)
    lbu a4, 2(a0)
    lbu a5, 3(a0)
    lbu a6, 4(a0)
    lbu a7, 5(a0)
    lbu t3, 6(a0)
    sltu t0, a3, a2
    sltu t1, a4, a2
    add t0, t0, t1
    sltu t1, a5, a2
    add t0, t0, t1
    sltu t1, a6, a2
    add t0, t0, t1
    sltu t1, a7, a2
    add t0, t0, t1
    sltu t1, t3, a2
    add t0, t0, t1
    slli t1, t0, 2
    slli t0, t0, 1
    add t0, t0, t1
    sltu t1, a4, a3
    add t0, t0, t1
    sltu t1, a5, a3
    add t0, t0, t1
    sltu t1, a6, a3
    add t0, t0, t1
    sltu t1, a7, a3
    add t0, t0, t1
    sltu t1, t3, a3
    add t0, t0, t1
    slli t1, t0, 2
    add t0, t0, t1
    sltu t1, a5, a4
    add t0, t0, t1
    sltu t1, a6, a4
    add t0, t0, t1
    sltu t1, a7, a4
    add t0, t0, t1
    sltu t1, t3, a4
    add t0, t0, t1
    slli t0, t0, 2
    sltu t1, a6, a5
    add t0, t0, t1
    sltu t1, a7, a5
    add t0, t0, t1
    sltu t1, t3, a5
    add t0, t0, t1
    slli t1, t0, 1
    add t0, t0, t1
    sltu t1, a7, a6
    add t0, t0, t1
    sltu t1, t3, a6
    add t0, t0, t1
    slli t0, t0, 1
    sltu t1, t3, a7
    add a0, t0, t1
    ret

rank_ori:
    lbu t0, 0(a0)
    addi t1, a0, 1
    addi t2, a0, 6
rank_ori_loop:
    slli t3, t0, 1
    add t0, t0, t3
    lbu t3, 0(t1)
    add t0, t0, t3
    addi t1, t1, 1
    bne t1, t2, rank_ori_loop
    mv a0, t0
    ret

rank_partial:
    li t0, 0
    li t1, 0
    li t2, 0
    li t3, 0
    li a2, 1
    li a3, 2
    li a4, 7
rank_partial_loop:
    lbu t4, 0(a0)
    beqz t4, rank_partial_a
    beq t4, a2, rank_partial_b
    bne t4, a3, rank_partial_next
    mv t2, t3
    j rank_partial_next
rank_partial_a:
    mv t0, t3
    j rank_partial_next
rank_partial_b:
    mv t1, t3
rank_partial_next:
    addi a0, a0, 1
    addi t3, t3, 1
    bne t3, a4, rank_partial_loop
    sltu t3, t0, t2
    sltu t4, t1, t2
    sub t2, t2, t3
    sub t2, t2, t4
    sltu t3, t0, t1
    sub t1, t1, t3
    slli t3, t0, 5
    slli t0, t0, 1
    sub t0, t3, t0
    slli t3, t1, 2
    add t1, t1, t3
    add t0, t0, t1
    add a0, t0, t2
    ret

/* a0=p, a1=o, a2=pp, a3=11-byte output. Returns a0=length/-1,
 * a1=visited nodes. Valid ranks are a precondition, as in the C core.
 * Not reentrant: the explicit DFS frames have static storage.
 *
 * s0 = frame, s1 = remaining depth, s2 = bound, s3 = face descriptors,
 * s4 = perm_dist, s5 = packed PDB, s6 = path cursor, s7 = nodes,
 * s9/s10/s11 = current p/o/pp. a5 carries the incoming face.
 *
 * A 64-byte frame stores parent p/o/pp at 0/4/8, face/turn at 12/16,
 * candidates at 20/24/28, incoming face at 32, table rows at 36/40/44.
 * The cursor replaces C's repeated array indexing and entered[] flags.
 */
.globl ida_solve
ida_solve:
    addi sp, sp, -80
    sw s0, 0(sp)
    sw s1, 4(sp)
    sw s2, 8(sp)
    sw s3, 12(sp)
    sw s4, 16(sp)
    sw s5, 20(sp)
    sw s6, 24(sp)
    sw s7, 28(sp)
    sw s8, 32(sp)
    sw s9, 36(sp)
    sw s10, 40(sp)
    sw s11, 44(sp)
    sw a0, 48(sp)
    sw a1, 52(sp)
    sw a2, 56(sp)
    sw a3, 60(sp)
    la s3, face_tables
    la t0, heuristic_tables
    lw s4, 0(t0)
    lw s5, 4(t0)
    li s7, 0
    /* Initial bound = max(perm_dist[p], PDB[o,pp]). */
    add t0, s4, a0
    lbu s2, 0(t0)
    slli t0, a1, 6
    slli t1, a1, 5
    add t0, t0, t1
    slli t1, a1, 3
    add t0, t0, t1
    add t0, t0, a1
    srli t1, a2, 1
    add t0, t0, t1
    add t0, s5, t0
    lbu t0, 0(t0)
    andi t1, a2, 1
    slli t1, t1, 2
    srl t0, t0, t1
    andi t0, t0, 15
    bgeu s2, t0, search_iteration
    mv s2, t0
search_iteration:
    la s0, frames
    mv s1, s2
    lw s6, 60(sp)
    lw s9, 48(sp)
    lw s10, 52(sp)
    lw s11, 56(sp)
    li a5, 3
search_node:
    addi s7, s7, 1
    add t0, s4, s9
    lbu t2, 0(t0)
#if EARLY_PERM
    /* max(a,b)>remaining iff either component exceeds remaining.
     * Short circuit before the more expensive packed PDB lookup.
     */
    bltu s1, t2, search_backtrack
#endif
    slli t0, s10, 6
    slli t1, s10, 5
    add t0, t0, t1
    slli t1, s10, 3
    add t0, t0, t1
    add t0, t0, s10
    srli t1, s11, 1
    add t0, t0, t1
    add t0, s5, t0
    lbu t0, 0(t0)
    andi t1, s11, 1
    slli t1, t1, 2
    srl t0, t0, t1
    andi t0, t0, 15
#if !EARLY_PERM
    bgeu t0, t2, search_max_ready
    mv t0, t2
search_max_ready:
#endif
    bltu s1, t0, search_backtrack
    or t0, s9, s10
    beqz t0, search_found
    beqz s1, search_backtrack
    sw s9, 0(s0)
    sw s10, 4(s0)
    sw s11, 8(s0)
    sw a5, 32(s0)
    sw zero, 12(s0)
search_face:
    lw t0, 12(s0)
    li t1, 3
    beq t0, t1, search_backtrack
    lw t1, 32(s0)
    beq t0, t1, search_skip_face
    sw zero, 16(s0)
    sw s9, 20(s0)
    sw s10, 24(s0)
    sw s11, 28(s0)
    slli t0, t0, 4
    add t0, s3, t0
    lw t1, 0(t0)
    sw t1, 36(s0)
    lw t1, 4(t0)
    sw t1, 40(s0)
    lw t1, 8(t0)
    sw t1, 44(s0)
search_turn:
    lw t0, 20(s0)
    lw t1, 36(s0)
    slli t0, t0, 1
    add t0, t0, t1
    lhu s9, 0(t0)
    sw s9, 20(s0)
    lw t0, 24(s0)
    lw t1, 40(s0)
    slli t0, t0, 1
    add t0, t0, t1
    lhu s10, 0(t0)
    sw s10, 24(s0)
    lw t0, 28(s0)
    lw t1, 44(s0)
    add t0, t0, t1
    lbu s11, 0(t0)
    sw s11, 28(s0)
    lw a5, 12(s0)
    lw t0, 16(s0)
    slli t1, a5, 1
    add t1, t1, a5
    add t1, t1, t0
    sb t1, 0(s6)
    addi t0, t0, 1
    sw t0, 16(s0)
    addi s0, s0, 64
    addi s6, s6, 1
    addi s1, s1, -1
    j search_node
search_skip_face:
    addi t0, t0, 1
    sw t0, 12(s0)
    j search_face
search_backtrack:
    beq s1, s2, search_next_bound
    addi s0, s0, -64
    addi s6, s6, -1
    addi s1, s1, 1
    lw s9, 0(s0)
    lw s10, 4(s0)
    lw s11, 8(s0)
    lw t0, 16(s0)
    li t1, 3
    bne t0, t1, search_turn
    lw t0, 12(s0)
    j search_skip_face
search_next_bound:
    addi s2, s2, 1
    li t0, 11
    bge t0, s2, search_iteration
    li a0, -1
    j search_return
search_found:
    sub a0, s2, s1
search_return:
    mv a1, s7
    lw s0, 0(sp)
    lw s1, 4(sp)
    lw s2, 8(sp)
    lw s3, 12(sp)
    lw s4, 16(sp)
    lw s5, 20(sp)
    lw s6, 24(sp)
    lw s7, 28(sp)
    lw s8, 32(sp)
    lw s9, 36(sp)
    lw s10, 40(sp)
    lw s11, 44(sp)
    addi sp, sp, 80
    ret
#endif

/* Independent replay without coordinate transition tables. Mutates cube.
 * a0=cube, a1=path, a2=length; returns 0 only for a solved result.
 */
.globl verify_solution
verify_solution:
    li t0, 11
    bltu t0, a2, verify_bad
    add a2, a1, a2
    la a6, replay_tmp
verify_move:
    beq a1, a2, verify_final
    lbu t0, 0(a1)
    addi a1, a1, 1
    li t1, 9
    bgeu t0, t1, verify_bad
    la a3, move_source
    la a4, move_twist
    li t1, 3
verify_decode:
    bltu t0, t1, verify_decoded
    addi t0, t0, -3
    addi a3, a3, 7
    addi a4, a4, 7
    j verify_decode
verify_decoded:
    addi a5, t0, 1
verify_quarter:
    li t0, 0
verify_cubie:
    add t1, a3, t0
    lbu t1, 0(t1)
    add t1, a0, t1
    lbu t2, 0(t1)
    add t3, a6, t0
    sb t2, 0(t3)
    lbu t2, 7(t1)
    add t1, a4, t0
    lbu t1, 0(t1)
    add t2, t2, t1
    addi t2, t2, -3
#if BRANCHLESS_TWIST
    srai t4, t2, 31
    andi t4, t4, 3
    add t2, t2, t4
#else
    bgez t2, verify_reduced
    addi t2, t2, 3
verify_reduced:
#endif
    sb t2, 7(t3)
    addi t0, t0, 1
    li t1, 7
    bne t0, t1, verify_cubie
    li t0, 0
verify_copy:
    add t1, a6, t0
    lbu t2, 0(t1)
    add t1, a0, t0
    sb t2, 0(t1)
    addi t0, t0, 1
    li t1, 14
    bne t0, t1, verify_copy
    addi a5, a5, -1
    bnez a5, verify_quarter
    j verify_move
verify_final:
    li t0, 0
verify_final_loop:
    add t1, a0, t0
    lbu t2, 0(t1)
    bne t2, t0, verify_bad
    lbu t2, 7(t1)
    bnez t2, verify_bad
    addi t0, t0, 1
    li t1, 7
    bne t0, t1, verify_final_loop
    li a0, 0
    ret
verify_bad:
    li a0, -1
    ret

#if RENDER
/* a0=actual solution move byte (3*face+turn). Leaf; preserves s0..s11.
 * The display has its own cubie state, never a prerecorded sequence.
 */
render_move:
    la a3, move_source
    la a4, move_twist
    li t1, 3
render_decode:
    bltu a0, t1, render_decoded
    addi a0, a0, -3
    addi a3, a3, 7
    addi a4, a4, 7
    j render_decode
render_decoded:
    addi a5, a0, 1
    la a0, render_cube
    la a6, render_tmp
render_quarter:
    li t0, 0
render_cubie:
    add t1, a3, t0
    lbu t1, 0(t1)
    add t1, a0, t1
    lbu t2, 0(t1)
    add t3, a6, t0
    sb t2, 0(t3)
    lbu t2, 7(t1)
    add t1, a4, t0
    lbu t1, 0(t1)
    add t2, t2, t1
    addi t2, t2, -3
    bgez t2, render_reduced
    addi t2, t2, 3
render_reduced:
    sb t2, 7(t3)
    addi t0, t0, 1
    li t1, 7
    bne t0, t1, render_cubie
    li t0, 0
render_copy:
    add t1, a6, t0
    lbu t2, 0(t1)
    add t1, a0, t0
    sb t2, 0(t1)
    addi t0, t0, 1
    li t1, 14
    bne t0, t1, render_copy
    addi a5, a5, -1
    bnez a5, render_quarter
    ret

/* 24 facelets, each 4x3 LEDs. Clear the entire peripheral to black first.
 * Descriptors contain (pixel x, pixel y, physical corner, face slot).
 * RGB24 words are stored at BASE + 4*(y*WIDTH+x), with no multiplication.
 */
.globl render_frame, render_pause
render_frame:
    li t0, LED_MATRIX_0_BASE
    li t1, LED_MATRIX_0_HEIGHT
render_clear_row:
    li t2, LED_MATRIX_0_WIDTH
render_clear_pixel:
    sw zero, 0(t0)
    addi t0, t0, 4
    addi t2, t2, -1
    bnez t2, render_clear_pixel
    addi t1, t1, -1
    bnez t1, render_clear_row
    la a0, led_facelets
    addi a1, a0, 96
    la a2, render_cube
    la a3, led_cubie_faces
    la a4, led_palette
    li a5, LED_MATRIX_0_WIDTH
    li a6, LED_MATRIX_0_BASE
render_facelet:
    lbu t0, 2(a0)
    lbu t1, 3(a0)
    beqz t0, render_fixed_corner
    addi t0, t0, -1
    add t0, a2, t0
    lbu t2, 7(t0)
    add t1, t1, t2
    li t2, 3
    blt t1, t2, render_slot_ready
    addi t1, t1, -3
render_slot_ready:
    lbu t0, 0(t0)
    addi t0, t0, 1
render_fixed_corner:
    slli t2, t0, 1
    add t0, t0, t2
    add t0, t0, t1
    add t0, a3, t0
    lbu t0, 0(t0)
    slli t0, t0, 2
    add t0, a4, t0
    lw a7, 0(t0)
    lbu t0, 0(a0)
    lbu t1, 1(a0)
    beqz t1, render_address_ready
render_row_offset:
    add t0, t0, a5
    addi t1, t1, -1
    bnez t1, render_row_offset
render_address_ready:
    slli t0, t0, 2
    add t0, t0, a6
    slli t3, a5, 2
    li t1, 3
render_tile_row:
    sw a7, 0(t0)
    sw a7, 4(t0)
    sw a7, 8(t0)
    sw a7, 12(t0)
    add t0, t0, t3
    addi t1, t1, -1
    bnez t1, render_tile_row
    addi a0, a0, 4
    bne a0, a1, render_facelet
    ret

/* Visual pacing only; excluded entirely from the CLI measurement build. */
render_pause:
    li t0, RENDER_DELAY
    beqz t0, render_pause_done
render_pause_loop:
    addi t0, t0, -1
    bnez t0, render_pause_loop
render_pause_done:
    ret
#endif

.section .rodata
.globl input_state, expected_length
input_state:
    .asciz STATE
    .zero 32 - (. - input_state)
.balign 4
expected_length:
    .word -1 /* Patched to the BFS distance by the host test driver. */
#ifndef C_REFERENCE
move_names:
    .byte 82,0,0,0, 82,50,0,0, 82,39,0,0
    .byte 66,0,0,0, 66,50,0,0, 66,39,0,0
    .byte 68,0,0,0, 68,50,0,0, 68,39,0,0
invalid_message:
    .asciz "invalid state: expected PPPPPPPOOOOOOO\n"
search_message:
    .asciz "search failed\n"
verify_message:
    .asciz "solution verification failed\n"
#endif
move_source:
    .byte 1,4,2,0,3,5,6, 0,1,2,4,5,6,3, 0,2,5,3,1,4,6
move_twist:
    .byte 1,2,0,2,1,0,0, 0,0,0,1,2,1,2, 0,0,0,0,0,0,0
#if RENDER
.globl led_cubie_faces, led_facelets, led_palette
/* Face IDs: U,L,F,R,B,D. Physical cubies include the fixed corner 0. */
led_cubie_faces:
    .byte 0,2,1, 0,3,2, 5,2,3, 5,1,2
    .byte 0,4,3, 5,3,4, 5,4,1, 0,1,4
led_facelets:
    .byte 9,0,7,0, 13,0,4,0, 9,3,0,0, 13,3,1,0 /* U */
    .byte 0,7,7,1, 4,7,0,2, 0,10,6,2, 4,10,3,1 /* L */
    .byte 9,7,0,1, 13,7,1,2, 9,10,3,2, 13,10,2,1 /* F */
    .byte 18,7,1,1, 22,7,4,2, 18,10,2,2, 22,10,5,1 /* R */
    .byte 27,7,4,1, 31,7,7,2, 27,10,5,2, 31,10,6,1 /* B */
    .byte 9,14,3,0, 13,14,2,0, 9,17,6,0, 13,17,5,0 /* D */
.balign 4
led_palette:
    .word 0xffffff, 0xff8000, 0x00ff00, 0xff0000, 0x0000ff, 0xffff00
#endif

.bss
.balign 16
#ifndef C_REFERENCE
frames:
    .zero 768
#endif
.globl cube, solution, solution_length, search_nodes
cube:
    .zero 14
replay_tmp:
    .zero 14
solution:
    .zero 11
.balign 4
solution_length:
    .zero 4
search_nodes:
    .zero 4
test_status:
    .zero 4
#if RENDER
.globl render_cube
render_cube:
    .zero 14
render_tmp:
    .zero 14
#endif
.balign 16
stack:
    .zero 512 /* Includes headroom for the GCC reference's call frames. */
stack_end:
