/* Full C algorithm: parse, validate, rank, search, and format the answer.
 * Only the hosted argv/stdio interface is adapted to Ripes. Both versions
 * share the independent assembly replay check and startup/exit convention.
 */
#define SOLVER_NO_MAIN
#define SOLVER_FREESTANDING
#include "../solver.c"

extern const char input_state[32];
extern const int32_t expected_length;
extern state_t cube;
extern uint8_t solution[MAX_DEPTH];
extern uint32_t solution_length, search_nodes;
extern int verify_solution(state_t *, const uint8_t *, unsigned);

static void print_char(unsigned ch)
{
    register unsigned a0 __asm__("a0") = ch;
    register unsigned a7 __asm__("a7") = 11;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static void print_string(const char *s)
{
    while (*s) print_char((unsigned char)*s++);
}

int c_main(void)
{
    if (!parse_state(input_state, &cube)) {
        print_string("invalid state: expected PPPPPPPOOOOOOO\n");
        return 2;
    }
    const int len = ida_solve(&cube, solution, &search_nodes);
    if (len < 0) {
        print_string("search failed\n");
        return 1;
    }
    solution_length = (uint32_t)len;
    if ((expected_length >= 0 && len != expected_length) ||
        verify_solution(&cube, solution, (unsigned)len)) {
        print_string("solution verification failed\n");
        return 1;
    }
    for (int i = 0; i < len; ++i) {
        if (i) print_char(' ');
        print_string(move_names[solution[i]]);
    }
    print_char('\n');
    return 0;
}
