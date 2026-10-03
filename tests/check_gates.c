#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Exercise the real solver implementation while suppressing its CLI main(). */
#define SOLVER_NO_MAIN
#include "../solver.c"

#define STATES ((uint32_t)PERMUTATIONS * (uint32_t)ORIENTATIONS)
#define PDB_STATES ((uint32_t)ORIENTATIONS * (uint32_t)PARTIAL_PERMS)
#define UNSEEN UINT8_C(0xff)
#define REPORT_EVERY UINT32_C(250000)

/*
 * Independent host oracle.
 *
 * These are the three quarter-turn generators of the fixed-corner 2x2x2
 * model.  The verifier rebuilds all coordinate transitions from them instead
 * of trusting the generated tables linked into solver.c.
 */
static const uint8_t ref_source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};

static const uint8_t ref_twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

typedef struct {
    uint16_t perm_q[3][PERMUTATIONS];
    uint16_t ori_q[3][ORIENTATIONS];
    uint8_t pp_q[3][PARTIAL_PERMS];
    uint8_t perm_dist[PERMUTATIONS];
    uint8_t pdb_dist[PDB_STATES];
    uint8_t pp_for_perm[PERMUTATIONS];
    uint8_t perm_max;
    uint8_t pdb_max;
} oracle_t;

static oracle_t oracle;

static state_t ref_quarter_turn(state_t s, uint8_t face)
{
    state_t r;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        const uint8_t from = ref_source[face][i];
        r.p[i] = s.p[from];
        uint8_t x = (uint8_t)(s.o[from] + ref_twist[face][i]);
        if (x >= 3)
            x = (uint8_t)(x - 3);
        r.o[i] = x;
    }
    return r;
}

static uint16_t ref_rank_perm(const uint8_t p[CUBIES])
{
    uint32_t rank = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        for (uint8_t j = (uint8_t)(i + 1); j < CUBIES; ++j)
            smaller = (uint8_t)(smaller + (p[j] < p[i]));
        rank = rank * (uint32_t)(CUBIES - i) + smaller;
    }
    return (uint16_t)rank;
}

static void ref_unrank_perm(uint16_t rank, uint8_t p[CUBIES])
{
    static const uint16_t fact[CUBIES] = {720, 120, 24, 6, 2, 1, 1};
    uint8_t avail[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint8_t n = CUBIES;

    for (uint8_t pos = 0; pos < CUBIES; ++pos) {
        const uint16_t f = fact[pos];
        const uint8_t digit = (uint8_t)(rank / f);
        rank = (uint16_t)(rank % f);
        if (digit >= n) {
            fputs("internal error: bad permutation unrank\n", stderr);
            exit(2);
        }
        p[pos] = avail[digit];
        for (uint8_t i = digit; (uint8_t)(i + 1) < n; ++i)
            avail[i] = avail[i + 1];
        --n;
    }
}

static uint16_t ref_rank_ori(const uint8_t o[CUBIES])
{
    uint16_t rank = 0;
    for (uint8_t i = 0; i < 6; ++i)
        rank = (uint16_t)(rank * 3u + o[i]);
    return rank;
}

static void ref_unrank_ori(uint16_t rank, uint8_t o[CUBIES])
{
    uint8_t sum = 0;
    for (uint8_t i = 6; i-- > 0;) {
        o[i] = (uint8_t)(rank % 3u);
        rank = (uint16_t)(rank / 3u);
        sum = (uint8_t)(sum + o[i]);
    }
    o[6] = (uint8_t)((3u - (sum % 3u)) % 3u);
}

static uint8_t ref_rank_partial_positions(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t bi = (uint8_t)(b - (b > a));
    const uint8_t ci = (uint8_t)(c - (c > a) - (c > b));
    return (uint8_t)(a * 30u + bi * 5u + ci);
}

static uint8_t ref_rank_partial_from_perm(const uint8_t p[CUBIES])
{
    uint8_t a = 0, b = 0, c = 0;
    for (uint8_t pos = 0; pos < CUBIES; ++pos) {
        if (p[pos] == 0)
            a = pos;
        else if (p[pos] == 1)
            b = pos;
        else if (p[pos] == 2)
            c = pos;
    }
    return ref_rank_partial_positions(a, b, c);
}

static void ref_unrank_partial(uint8_t rank, uint8_t *a, uint8_t *b,
                               uint8_t *c)
{
    uint8_t avail[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    *a = (uint8_t)(rank / 30u);
    uint8_t r = (uint8_t)(rank % 30u);
    const uint8_t bi = (uint8_t)(r / 5u);
    const uint8_t ci = (uint8_t)(r % 5u);

    for (uint8_t j = *a; (uint8_t)(j + 1) < CUBIES; ++j)
        avail[j] = avail[j + 1];
    *b = avail[bi];
    for (uint8_t j = bi; (uint8_t)(j + 1) < CUBIES - 1; ++j)
        avail[j] = avail[j + 1];
    *c = avail[ci];
}

static void make_partial_representative(uint8_t pp, uint8_t p[CUBIES])
{
    uint8_t a, b, c;
    ref_unrank_partial(pp, &a, &b, &c);
    for (uint8_t i = 0; i < CUBIES; ++i)
        p[i] = UINT8_C(0xff);
    p[a] = 0;
    p[b] = 1;
    p[c] = 2;

    uint8_t cubie = 3;
    for (uint8_t pos = 0; pos < CUBIES; ++pos)
        if (p[pos] == UINT8_C(0xff))
            p[pos] = cubie++;
}

static void build_ref_transitions(void)
{
    state_t s;

    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        ref_unrank_perm(p, s.p);
        memset(s.o, 0, sizeof s.o);
        for (uint8_t f = 0; f < 3; ++f) {
            const state_t n = ref_quarter_turn(s, f);
            oracle.perm_q[f][p] = ref_rank_perm(n.p);
        }

        const uint8_t ref_pp = ref_rank_partial_from_perm(s.p);
        const uint8_t target_pp = rank_partial_from_perm(s.p);
        if (target_pp != ref_pp) {
            fprintf(stderr,
                    "H2 FAIL: root partial rank mismatch at perm=%u: target=%u ref=%u\n",
                    (unsigned)p, (unsigned)target_pp, (unsigned)ref_pp);
            exit(1);
        }
        oracle.pp_for_perm[p] = ref_pp;

        if (rank_perm(s.p) != p) {
            fprintf(stderr,
                    "H2 FAIL: optimized permutation rank mismatch at perm=%u\n",
                    (unsigned)p);
            exit(1);
        }
    }

    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        for (uint8_t i = 0; i < CUBIES; ++i)
            s.p[i] = i;
        ref_unrank_ori(o, s.o);
        if (rank_ori(s.o) != o) {
            fprintf(stderr,
                    "H2 FAIL: optimized orientation rank mismatch at ori=%u\n",
                    (unsigned)o);
            exit(1);
        }
        for (uint8_t f = 0; f < 3; ++f) {
            const state_t n = ref_quarter_turn(s, f);
            oracle.ori_q[f][o] = ref_rank_ori(n.o);
        }
    }

    for (uint16_t pp = 0; pp < PARTIAL_PERMS; ++pp) {
        make_partial_representative((uint8_t)pp, s.p);
        memset(s.o, 0, sizeof s.o);
        if (ref_rank_partial_from_perm(s.p) != pp) {
            fprintf(stderr,
                    "internal error: partial representative mismatch at %u\n",
                    (unsigned)pp);
            exit(2);
        }
        for (uint8_t f = 0; f < 3; ++f) {
            const state_t n = ref_quarter_turn(s, f);
            oracle.pp_q[f][pp] = ref_rank_partial_from_perm(n.p);
        }
    }
}

static uint8_t build_ref_perm_dist(void)
{
    uint16_t queue[PERMUTATIONS];
    uint16_t head = 0, tail = 1;
    uint8_t maxd = 0;

    memset(oracle.perm_dist, UNSEEN, sizeof oracle.perm_dist);
    oracle.perm_dist[0] = 0;
    queue[0] = 0;

    while (head < tail) {
        const uint16_t p = queue[head++];
        const uint8_t nd = (uint8_t)(oracle.perm_dist[p] + 1);
        for (uint8_t f = 0; f < 3; ++f) {
            uint16_t np = p;
            for (uint8_t t = 0; t < 3; ++t) {
                np = oracle.perm_q[f][np];
                if (oracle.perm_dist[np] == UNSEEN) {
                    oracle.perm_dist[np] = nd;
                    queue[tail++] = np;
                    if (nd > maxd)
                        maxd = nd;
                }
            }
        }
    }

    if (tail != PERMUTATIONS) {
        fprintf(stderr, "oracle permutation BFS incomplete: %u/%u\n",
                (unsigned)tail, (unsigned)PERMUTATIONS);
        exit(2);
    }
    return maxd;
}

static uint8_t build_ref_pdb(void)
{
    uint32_t *queue = (uint32_t *)malloc((size_t)PDB_STATES * sizeof(*queue));
    if (!queue) {
        fputs("error: unable to allocate PDB oracle queue\n", stderr);
        exit(2);
    }

    memset(oracle.pdb_dist, UNSEEN, sizeof oracle.pdb_dist);
    uint32_t head = 0, tail = 1;
    uint8_t maxd = 0;
    oracle.pdb_dist[0] = 0;
    queue[0] = 0;

    while (head < tail) {
        const uint32_t idx = queue[head++];
        const uint16_t o = (uint16_t)(idx / PARTIAL_PERMS);
        const uint8_t pp = (uint8_t)(idx % PARTIAL_PERMS);
        const uint8_t nd = (uint8_t)(oracle.pdb_dist[idx] + 1);

        for (uint8_t f = 0; f < 3; ++f) {
            uint16_t no = o;
            uint8_t npp = pp;
            for (uint8_t t = 0; t < 3; ++t) {
                no = oracle.ori_q[f][no];
                npp = oracle.pp_q[f][npp];
                const uint32_t next =
                    (uint32_t)no * (uint32_t)PARTIAL_PERMS + npp;
                if (oracle.pdb_dist[next] == UNSEEN) {
                    oracle.pdb_dist[next] = nd;
                    queue[tail++] = next;
                    if (nd > maxd)
                        maxd = nd;
                }
            }
        }
    }

    free(queue);
    if (tail != PDB_STATES) {
        fprintf(stderr, "oracle PDB BFS incomplete: %u/%u\n",
                (unsigned)tail, (unsigned)PDB_STATES);
        exit(2);
    }
    return maxd;
}

static void build_oracle(void)
{
    build_ref_transitions();
    oracle.perm_max = build_ref_perm_dist();
    oracle.pdb_max = build_ref_pdb();
}

static uint32_t state_index(uint16_t p, uint16_t o)
{
    return (uint32_t)p * (uint32_t)ORIENTATIONS + (uint32_t)o;
}

static uint8_t *build_exact_distances(uint8_t *diameter,
                                      uint32_t depth_count[MAX_DEPTH + 1])
{
    uint8_t *dist = (uint8_t *)malloc((size_t)STATES);
    uint32_t *queue = (uint32_t *)malloc((size_t)STATES * sizeof(*queue));
    if (!dist || !queue) {
        fputs("error: unable to allocate exact BFS table/queue\n", stderr);
        free(queue);
        free(dist);
        return NULL;
    }

    memset(dist, UNSEEN, (size_t)STATES);
    memset(depth_count, 0, sizeof(uint32_t) * (MAX_DEPTH + 1));
    uint32_t head = 0, tail = 1;
    dist[0] = 0;
    queue[0] = 0;
    depth_count[0] = 1;
    *diameter = 0;

    while (head < tail) {
        const uint32_t idx = queue[head++];
        const uint16_t p = (uint16_t)(idx / ORIENTATIONS);
        const uint16_t o = (uint16_t)(idx % ORIENTATIONS);
        const uint8_t d = dist[idx];

        for (uint8_t f = 0; f < 3; ++f) {
            uint16_t np = p;
            uint16_t no = o;
            for (uint8_t t = 0; t < 3; ++t) {
                np = oracle.perm_q[f][np];
                no = oracle.ori_q[f][no];
                const uint32_t next = state_index(np, no);
                if (dist[next] != UNSEEN)
                    continue;

                const uint8_t nd = (uint8_t)(d + 1);
                if (nd > MAX_DEPTH) {
                    fprintf(stderr,
                            "exact BFS found distance %u greater than MAX_DEPTH=%u\n",
                            (unsigned)nd, (unsigned)MAX_DEPTH);
                    free(queue);
                    free(dist);
                    return NULL;
                }
                dist[next] = nd;
                queue[tail++] = next;
                ++depth_count[nd];
                if (nd > *diameter)
                    *diameter = nd;
            }
        }
    }

    free(queue);
    if (tail != STATES) {
        fprintf(stderr, "exact BFS incomplete: %u/%u states\n",
                (unsigned)tail, (unsigned)STATES);
        free(dist);
        return NULL;
    }
    return dist;
}

static int check_transition_bijection_u16(const uint16_t *table,
                                           uint16_t n, const char *name,
                                           uint8_t face)
{
    uint8_t *seen = (uint8_t *)calloc(n, 1);
    if (!seen) {
        fputs("error: allocation failed in H2\n", stderr);
        return 0;
    }
    for (uint16_t i = 0; i < n; ++i) {
        const uint16_t v = table[i];
        if (v >= n || seen[v]) {
            fprintf(stderr,
                    "H2 FAIL: %s face=%u is not a fully populated bijection at index=%u value=%u\n",
                    name, (unsigned)face, (unsigned)i, (unsigned)v);
            free(seen);
            return 0;
        }
        seen[v] = 1;
    }
    free(seen);
    return 1;
}

static int check_transition_bijection_u8(const uint8_t *table, uint16_t n,
                                          const char *name, uint8_t face)
{
    uint8_t *seen = (uint8_t *)calloc(n, 1);
    if (!seen) {
        fputs("error: allocation failed in H2\n", stderr);
        return 0;
    }
    for (uint16_t i = 0; i < n; ++i) {
        const uint8_t v = table[i];
        if (v >= n || seen[v]) {
            fprintf(stderr,
                    "H2 FAIL: %s face=%u is not a fully populated bijection at index=%u value=%u\n",
                    name, (unsigned)face, (unsigned)i, (unsigned)v);
            free(seen);
            return 0;
        }
        seen[v] = 1;
    }
    free(seen);
    return 1;
}

static int run_h2(void)
{
    uint16_t perm_q_max = 0;
    uint16_t ori_q_max = 0;
    uint8_t pp_q_max = 0;

    for (uint8_t f = 0; f < 3; ++f) {
        if (!check_transition_bijection_u16(perm_q[f], PERMUTATIONS,
                                            "perm_q", f) ||
            !check_transition_bijection_u16(ori_q[f], ORIENTATIONS,
                                            "ori_q", f) ||
            !check_transition_bijection_u8(pp_q[f], PARTIAL_PERMS,
                                           "pp_q", f))
            return 1;

        for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
            if (perm_q[f][p] != oracle.perm_q[f][p]) {
                fprintf(stderr,
                        "H2 FAIL: perm_q[%u][%u]=%u, reference=%u\n",
                        (unsigned)f, (unsigned)p, (unsigned)perm_q[f][p],
                        (unsigned)oracle.perm_q[f][p]);
                return 1;
            }
            if (perm_q[f][p] > perm_q_max)
                perm_q_max = perm_q[f][p];
        }
        for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
            if (ori_q[f][o] != oracle.ori_q[f][o]) {
                fprintf(stderr,
                        "H2 FAIL: ori_q[%u][%u]=%u, reference=%u\n",
                        (unsigned)f, (unsigned)o, (unsigned)ori_q[f][o],
                        (unsigned)oracle.ori_q[f][o]);
                return 1;
            }
            if (ori_q[f][o] > ori_q_max)
                ori_q_max = ori_q[f][o];
        }
        for (uint16_t pp = 0; pp < PARTIAL_PERMS; ++pp) {
            if (pp_q[f][pp] != oracle.pp_q[f][pp]) {
                fprintf(stderr,
                        "H2 FAIL: pp_q[%u][%u]=%u, reference=%u\n",
                        (unsigned)f, (unsigned)pp, (unsigned)pp_q[f][pp],
                        (unsigned)oracle.pp_q[f][pp]);
                return 1;
            }
            if (pp_q[f][pp] > pp_q_max)
                pp_q_max = pp_q[f][pp];
        }
    }

    if (perm_q_max != PERMUTATIONS - 1 || ori_q_max != ORIENTATIONS - 1 ||
        pp_q_max != PARTIAL_PERMS - 1) {
        fprintf(stderr,
                "H2 FAIL: transition maxima perm=%u ori=%u pp=%u\n",
                (unsigned)perm_q_max, (unsigned)ori_q_max,
                (unsigned)pp_q_max);
        return 1;
    }

    uint8_t actual_perm_max = 0;
    uint32_t perm_zero_count = 0;
    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        if (perm_dist[p] != oracle.perm_dist[p]) {
            fprintf(stderr,
                    "H2 FAIL: perm_dist[%u]=%u, reference=%u\n",
                    (unsigned)p, (unsigned)perm_dist[p],
                    (unsigned)oracle.perm_dist[p]);
            return 1;
        }
        if (perm_dist[p] > actual_perm_max)
            actual_perm_max = perm_dist[p];
        if (perm_dist[p] == 0)
            ++perm_zero_count;
    }

    if (perm_dist[0] != 0 || perm_zero_count != 1 ||
        actual_perm_max != oracle.perm_max ||
        actual_perm_max != TABLE_PERM_MAX_DISTANCE) {
        fprintf(stderr,
                "H2 FAIL: perm_dist solved=%u zeros=%u max=%u ref_max=%u declared_max=%u\n",
                (unsigned)perm_dist[0], (unsigned)perm_zero_count,
                (unsigned)actual_perm_max, (unsigned)oracle.perm_max,
                (unsigned)TABLE_PERM_MAX_DISTANCE);
        return 1;
    }

    uint8_t actual_pdb_max = 0;
    uint32_t pdb_zero_count = 0;
    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        for (uint16_t pp = 0; pp < PARTIAL_PERMS; ++pp) {
            const uint8_t v = pdb_get(o, (uint8_t)pp);
            if (v > TABLE_PDB_MAX_DISTANCE) {
                fprintf(stderr,
                        "H2 FAIL: PDB entry o=%u pp=%u has out-of-range value %u\n",
                        (unsigned)o, (unsigned)pp, (unsigned)v);
                return 1;
            }
            if (v > actual_pdb_max)
                actual_pdb_max = v;
            if (v == 0)
                ++pdb_zero_count;
        }
    }

    if (pdb_get(0, 0) != 0 || pdb_zero_count != 1 ||
        actual_pdb_max != oracle.pdb_max ||
        actual_pdb_max != TABLE_PDB_MAX_DISTANCE) {
        fprintf(stderr,
                "H2 FAIL: PDB solved=%u zeros=%u max=%u ref_max=%u declared_max=%u\n",
                (unsigned)pdb_get(0, 0), (unsigned)pdb_zero_count,
                (unsigned)actual_pdb_max, (unsigned)oracle.pdb_max,
                (unsigned)TABLE_PDB_MAX_DISTANCE);
        return 1;
    }

    const size_t static_table_bytes = sizeof perm_q + sizeof ori_q +
                                      sizeof pp_q + sizeof perm_dist +
                                      sizeof pdb_table;
    printf("H2 PASS: all solver tables are populated and verified\n");
    printf("transition maxima: perm_q=%u ori_q=%u pp_q=%u\n",
           (unsigned)perm_q_max, (unsigned)ori_q_max, (unsigned)pp_q_max);
    printf("distance maxima: perm_dist=%u PDB=%u; solved entries are 0\n",
           (unsigned)actual_perm_max, (unsigned)actual_pdb_max);
    printf("solver table bytes: %zu\n", static_table_bytes);
    return 0;
}

static int run_h4(void)
{
    uint32_t even_checked = 0;
    uint32_t odd_checked = 0;

    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        for (uint16_t pp = 0; pp < PARTIAL_PERMS; ++pp) {
            const uint32_t idx =
                (uint32_t)o * (uint32_t)PARTIAL_PERMS + pp;
            const uint8_t expected = oracle.pdb_dist[idx];
            const uint8_t got = pdb_get(o, (uint8_t)pp);
            if (got != expected) {
                fprintf(stderr,
                        "H4 FAIL: logical index=%u (o=%u pp=%u) accessor=%u unpacked=%u (%s nibble)\n",
                        (unsigned)idx, (unsigned)o, (unsigned)pp,
                        (unsigned)got, (unsigned)expected,
                        (idx & 1u) ? "odd/high" : "even/low");
                return 1;
            }
            if (idx & 1u)
                ++odd_checked;
            else
                ++even_checked;
        }
    }

    printf("H4 PASS: packed PDB accessor matches unpacked reference\n");
    printf("checked %u even/low and %u odd/high logical indices\n",
           (unsigned)even_checked, (unsigned)odd_checked);
    return 0;
}

static int run_h1(const uint8_t *exact, uint8_t diameter,
                  const uint32_t depth_count[MAX_DEPTH + 1])
{
    uint32_t checked = 0;
    uint32_t exact_hits = 0;
    uint8_t max_h = 0;

    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        const uint8_t pp = oracle.pp_for_perm[p];
        for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
            const uint32_t idx = state_index(p, o);
            const uint8_t d = exact[idx];
            const uint8_t h = heuristic(p, o, pp);
            if (h > d) {
                fprintf(stderr,
                        "H1 FAIL: p=%u o=%u pp=%u h=%u exact=%u\n",
                        (unsigned)p, (unsigned)o, (unsigned)pp,
                        (unsigned)h, (unsigned)d);
                return 1;
            }
            if (h > max_h)
                max_h = h;
            if (h == d)
                ++exact_hits;
            ++checked;
        }
    }

    printf("H1 PASS: %u states checked, h(s) <= d(s) everywhere\n",
           (unsigned)checked);
    printf("exact HTM diameter: %u; maximum heuristic: %u\n",
           (unsigned)diameter, (unsigned)max_h);
    printf("heuristic equals exact distance on %u states\n",
           (unsigned)exact_hits);
    printf("distance histogram:");
    for (uint8_t d = 0; d <= diameter; ++d)
        printf(" %u:%u", (unsigned)d, (unsigned)depth_count[d]);
    putchar('\n');
    return 0;
}

static void format_state(uint16_t p_rank, uint16_t o_rank, char out[15])
{
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
    ref_unrank_perm(p_rank, p);
    ref_unrank_ori(o_rank, o);
    for (uint8_t i = 0; i < CUBIES; ++i)
        out[i] = (char)('1' + p[i]);
    for (uint8_t i = 0; i < CUBIES; ++i)
        out[7 + i] = (char)('1' + o[i]);
    out[14] = '\0';
}

static int apply_solution_reference(uint16_t p, uint16_t o,
                                    const uint8_t solution[MAX_DEPTH], int len)
{
    for (int i = 0; i < len; ++i) {
        const uint8_t move = solution[i];
        if (move >= MOVES)
            return 0;
        const uint8_t face = (uint8_t)(move / 3u);
        const uint8_t turns = (uint8_t)(move % 3u + 1u);
        for (uint8_t t = 0; t < turns; ++t) {
            p = oracle.perm_q[face][p];
            o = oracle.ori_q[face][o];
        }
    }
    return p == 0 && o == 0;
}

static int run_h3(const uint8_t *exact)
{
    uint32_t checked = 0;
    uint32_t next_report = REPORT_EVERY;
    unsigned long long total_nodes = 0;
    uint32_t max_nodes = 0;
    uint16_t max_nodes_p = 0;
    uint16_t max_nodes_o = 0;
    const time_t start = time(NULL);

    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        const uint8_t pp = oracle.pp_for_perm[p];
        for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
            uint8_t solution[MAX_DEPTH];
            uint32_t nodes = 0;
            const int len = ida_solve_indices(p, o, pp, solution, &nodes);
            const uint8_t d = exact[state_index(p, o)];

            if (len < 0 || len != d) {
                char state[15];
                format_state(p, o, state);
                fprintf(stderr,
                        "H3 FAIL: state=%s p=%u o=%u solver_len=%d exact=%u nodes=%u\n",
                        state, (unsigned)p, (unsigned)o, len,
                        (unsigned)d, (unsigned)nodes);
                return 1;
            }
            if (!apply_solution_reference(p, o, solution, len)) {
                char state[15];
                format_state(p, o, state);
                fprintf(stderr,
                        "H3 FAIL: returned path does not solve state=%s p=%u o=%u\n",
                        state, (unsigned)p, (unsigned)o);
                return 1;
            }

            total_nodes += nodes;
            if (nodes > max_nodes) {
                max_nodes = nodes;
                max_nodes_p = p;
                max_nodes_o = o;
            }
            ++checked;

            if (checked >= next_report && checked < STATES) {
                const time_t now = time(NULL);
                fprintf(stderr,
                        "H3 progress: %u/%u states (%.1f%%), wall=%ld s\n",
                        (unsigned)checked, (unsigned)STATES,
                        100.0 * (double)checked / (double)STATES,
                        (long)difftime(now, start));
                next_report += REPORT_EVERY;
            }
        }
    }

    const time_t finish = time(NULL);
    char worst_state[15];
    format_state(max_nodes_p, max_nodes_o, worst_state);
    printf("H3 PASS: %u states returned exact optimal length\n",
           (unsigned)checked);
    printf("every returned path also reached the solved state in the reference model\n");
    printf("H3 wall-clock time: %.0f s\n", difftime(finish, start));
    printf("search nodes: total=%llu max_per_state=%u at %s\n",
           total_nodes, (unsigned)max_nodes, worst_state);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s h1|h2|h3|h4|fast|all\n"
            "  fast: H2 + H4 + H1\n"
            "  all : H2 + H4 + H1 + H3\n",
            prog);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage(argc > 0 && argv[0] ? argv[0] : "check_gates");
        return 2;
    }

    const int want_h1 = strcmp(argv[1], "h1") == 0;
    const int want_h2 = strcmp(argv[1], "h2") == 0;
    const int want_h3 = strcmp(argv[1], "h3") == 0;
    const int want_h4 = strcmp(argv[1], "h4") == 0;
    const int want_fast = strcmp(argv[1], "fast") == 0;
    const int want_all = strcmp(argv[1], "all") == 0;

    if (!want_h1 && !want_h2 && !want_h3 && !want_h4 && !want_fast &&
        !want_all) {
        usage(argv[0]);
        return 2;
    }

    build_oracle();

    if (want_h2 || want_fast || want_all)
        if (run_h2() != 0)
            return 1;

    if (want_h4 || want_fast || want_all)
        if (run_h4() != 0)
            return 1;

    uint8_t *exact = NULL;
    uint8_t diameter = 0;
    uint32_t depth_count[MAX_DEPTH + 1];
    if (want_h1 || want_h3 || want_fast || want_all) {
        exact = build_exact_distances(&diameter, depth_count);
        if (!exact)
            return 1;
    }

    if (want_h1 || want_fast || want_all)
        if (run_h1(exact, diameter, depth_count) != 0) {
            free(exact);
            return 1;
        }

    if (want_h3 || want_all)
        if (run_h3(exact) != 0) {
            free(exact);
            return 1;
        }

    free(exact);
    return 0;
}
