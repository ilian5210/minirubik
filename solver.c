#include <stdint.h>
#ifndef SOLVER_FREESTANDING
#include <stdio.h>
#include <string.h>
#endif

#define CUBIES 7
#define PERMUTATIONS 5040
#define ORIENTATIONS 729
#define PARTIAL_PERMS 210
#define MOVES 9
#define MAX_DEPTH 11
#define NO_FACE 3

typedef struct {
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
} state_t;

#include "pdb_table.h"
#if !defined(SOLVER_NO_MAIN) || defined(SOLVER_FREESTANDING)
static const char move_names[MOVES][3] = {
    "R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"
};
static int valid(const state_t *s)
{
    uint8_t mask = 0;
    uint8_t osum = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (s->p[i] >= CUBIES || s->o[i] >= 3)
            return 0;
        const uint8_t bit = (uint8_t)(1u << s->p[i]);
        if (mask & bit)
            return 0;
        mask = (uint8_t)(mask | bit);
        osum = (uint8_t)(osum + s->o[i]);
        if (osum >= 3)
            osum = (uint8_t)(osum - 3);
    }
    return mask == 0x7f && osum == 0;
}
static int parse_state(const char *input, state_t *s)
{
    /*
     * Do not read past the first NUL byte.  The old version indexed all 14
     * characters unconditionally, so a short argument caused an out-of-bounds
     * read before it was rejected.
     */
    for (int i = 0; i < 14; ++i) {
        const unsigned char ch = (unsigned char)input[i];
        if (ch == '\0')
            return 0;

        const int limit = i < 7 ? 7 : 3;
        if (ch < '1' || ch > (unsigned char)('0' + limit))
            return 0;
        if (i < 7)
            s->p[i] = (uint8_t)(ch - '1');
        else
            s->o[i - 7] = (uint8_t)(ch - '1');
    }

    /* If the first 14 bytes were present, byte 14 is safe to inspect. */
    return input[14] == '\0' && valid(s);
}
#endif
static uint8_t smaller_after(const uint8_t p[CUBIES], uint8_t i)
{
    uint8_t n = 0;
    for (uint8_t j = (uint8_t)(i + 1); j < CUBIES; ++j)
        n = (uint8_t)(n + (p[j] < p[i]));
    return n;
}
/* Same Lehmer rank as upstream solver.c, but no variable multiply. */
static uint16_t rank_perm(const uint8_t p[CUBIES])
{
    uint32_t r = smaller_after(p, 0);
    r = (r << 2) + (r << 1) + smaller_after(p, 1); /* *6 */
    r = (r << 2) + r + smaller_after(p, 2);        /* *5 */
    r = (r << 2) + smaller_after(p, 3);            /* *4 */
    r = (r << 1) + r + smaller_after(p, 4);        /* *3 */
    r = (r << 1) + smaller_after(p, 5);            /* *2 */
    return (uint16_t)r;
}
static uint16_t rank_ori(const uint8_t o[CUBIES])
{
    uint16_t r = o[0];
    r = (uint16_t)((r << 1) + r + o[1]);
    r = (uint16_t)((r << 1) + r + o[2]);
    r = (uint16_t)((r << 1) + r + o[3]);
    r = (uint16_t)((r << 1) + r + o[4]);
    r = (uint16_t)((r << 1) + r + o[5]);
    return r;
}
static uint8_t rank_partial_from_perm(const uint8_t p[CUBIES])
{
    uint8_t a = 0, b = 0, c = 0;
    for (uint8_t pos = 0; pos < CUBIES; ++pos) {
        if (p[pos] == 0) a = pos;
        else if (p[pos] == 1) b = pos;
        else if (p[pos] == 2) c = pos;
    }
    const uint8_t bi = (uint8_t)(b - (b > a));
    const uint8_t ci = (uint8_t)(c - (c > a) - (c > b));
    /* a*30 + bi*5 + ci, using shifts/adds only. */
    return (uint8_t)(((a << 5) - (a << 1)) + (bi << 2) + bi + ci);
}
static uint8_t pdb_get(uint16_t o, uint8_t pp)
{
    /* 105 = 64 + 32 + 8 + 1. Rows are even-sized before packing. */
    const uint32_t row = ((uint32_t)o << 6) + ((uint32_t)o << 5) +
                         ((uint32_t)o << 3) + o;
    const uint8_t x = pdb_table[row + (pp >> 1)];
    return (pp & 1u) ? (uint8_t)(x >> 4) : (uint8_t)(x & 0x0f);
}
static uint8_t heuristic(uint16_t p, uint16_t o, uint8_t pp)
{
    const uint8_t a = perm_dist[p];
    const uint8_t b = pdb_get(o, pp);
    return a > b ? a : b;
}
typedef struct {
    uint16_t p[MAX_DEPTH + 1];
    uint16_t o[MAX_DEPTH + 1];
    uint8_t pp[MAX_DEPTH + 1];
    uint16_t cand_p[MAX_DEPTH + 1];
    uint16_t cand_o[MAX_DEPTH + 1];
    uint8_t cand_pp[MAX_DEPTH + 1];
    uint8_t last_face[MAX_DEPTH + 1];
    uint8_t face[MAX_DEPTH + 1];
    uint8_t turn[MAX_DEPTH + 1];
    uint8_t entered[MAX_DEPTH + 1];
    uint8_t path[MAX_DEPTH];
} search_t;
static int bounded_search(search_t *s, uint8_t bound, uint32_t *nodes)
{
    uint8_t depth = 0;
    s->last_face[0] = NO_FACE;
    s->entered[0] = 0;
    for (;;) {
        if (!s->entered[depth]) {
            ++*nodes;
            const uint8_t h = heuristic(s->p[depth], s->o[depth], s->pp[depth]);
            if ((uint8_t)(depth + h) > bound)
                goto backtrack;
            if (s->p[depth] == 0 && s->o[depth] == 0)
                return depth;
            if (depth == bound)
                goto backtrack;
            s->face[depth] = 0;
            s->turn[depth] = 0;
            s->entered[depth] = 1;
        }
        for (;;) {
            uint8_t f = s->face[depth];
            if (f >= 3)
                goto backtrack;
            if (f == s->last_face[depth]) {
                s->face[depth] = (uint8_t)(f + 1);
                s->turn[depth] = 0;
                continue;
            }
            if (s->turn[depth] == 0) {
                s->cand_p[depth] = s->p[depth];
                s->cand_o[depth] = s->o[depth];
                s->cand_pp[depth] = s->pp[depth];
            }
            s->cand_p[depth] = perm_q[f][s->cand_p[depth]];
            s->cand_o[depth] = ori_q[f][s->cand_o[depth]];
            s->cand_pp[depth] = pp_q[f][s->cand_pp[depth]];

            const uint8_t t = s->turn[depth];
            s->path[depth] = (uint8_t)(f + f + f + t);
            s->turn[depth] = (uint8_t)(t + 1);
            if (s->turn[depth] == 3) {
                s->face[depth] = (uint8_t)(f + 1);
                s->turn[depth] = 0;
            }
            const uint8_t child = (uint8_t)(depth + 1);
            s->p[child] = s->cand_p[depth];
            s->o[child] = s->cand_o[depth];
            s->pp[child] = s->cand_pp[depth];
            s->last_face[child] = f;
            s->entered[child] = 0;
            depth = child;
            break;
        }
        continue;

backtrack:
        s->entered[depth] = 0;
        if (depth == 0)
            return -1;
        --depth;
    }
}
static int ida_solve_indices(uint16_t p, uint16_t o, uint8_t pp,
                             uint8_t solution[MAX_DEPTH], uint32_t *nodes)
{
    search_t s;
    s.p[0] = p;
    s.o[0] = o;
    s.pp[0] = pp;
    *nodes = 0;
    uint8_t bound = heuristic(p, o, pp);
    for (; bound <= MAX_DEPTH; ++bound) {
        const int len = bounded_search(&s, bound, nodes);
        if (len >= 0) {
            for (int i = 0; i < len; ++i)
                solution[i] = s.path[i];
            return len;
        }
    }
    return -1;
}
#if !defined(SOLVER_NO_MAIN) || defined(SOLVER_FREESTANDING)
static int ida_solve(const state_t *input, uint8_t solution[MAX_DEPTH],
                     uint32_t *nodes)
{
    return ida_solve_indices(rank_perm(input->p), rank_ori(input->o),
                             rank_partial_from_perm(input->p),
                             solution, nodes);
}
#endif

#ifndef SOLVER_NO_MAIN
static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}
int main(int argc, char **argv)
{
    state_t state;
    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver");
        return 2;
    }

    uint8_t solution[MAX_DEPTH];
    uint32_t nodes;
    const int len = ida_solve(&state, solution, &nodes);
    if (len < 0) {
        fputs("search failed\n", stderr);
        return 1;
    }
    for (int i = 0; i < len; ++i) {
        if (i) putchar(' ');
        fputs(move_names[solution[i]], stdout);
    }
    putchar('\n');
#ifdef SHOW_STATS
    fprintf(stderr, "length=%d nodes=%u root_h=%u\n", len, nodes,
            heuristic(rank_perm(state.p), rank_ori(state.o),
                      rank_partial_from_perm(state.p)));
#else
    (void)nodes;
#endif
    return output_failed();
}
#endif
