#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#define CUBIES 7
#define PERMUTATIONS 5040
#define ORIENTATIONS 729
#define PARTIAL_PERMS 210
#define PDB_STATES (ORIENTATIONS * PARTIAL_PERMS)
#define PDB_PACKED_BYTES ((PDB_STATES + 1) / 2)


static int ascii_equal_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/*
 * Keep the generated header out of a conventional build directory named
 * "output".  If the program is launched from project/output, the default
 * destination becomes ../pdb_table.h.  Otherwise it is pdb_table.h in the
 * current directory.  An explicit command-line path still overrides this.
 */
static const char *default_output_path(char path[32])
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) {
        strcpy(path, "pdb_table.h");
        return path;
    }

    size_t n = strlen(cwd);
    while (n > 0 && (cwd[n - 1] == '/' || cwd[n - 1] == '\\'))
        cwd[--n] = '\0';

    const char *base = cwd;
    for (const char *q = cwd; *q; ++q) {
        if (*q == '/' || *q == '\\')
            base = q + 1;
    }

    strcpy(path, ascii_equal_nocase(base, "output")
                     ? "../pdb_table.h"
                     : "pdb_table.h");
    return path;
}

static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};

static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

typedef struct {
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
} state_t;

static state_t quarter_turn(state_t s, uint8_t face)
{
    state_t r;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        const uint8_t from = source[face][i];
        r.p[i] = s.p[from];
        uint8_t x = (uint8_t)(s.o[from] + twist[face][i]);
        if (x >= 3)
            x = (uint8_t)(x - 3);
        r.o[i] = x;
    }
    return r;
}

static uint16_t rank_perm(const uint8_t p[CUBIES])
{
    uint32_t rank = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        for (uint8_t j = (uint8_t)(i + 1); j < CUBIES; ++j)
            if (p[j] < p[i])
                ++smaller;
        rank = rank * (CUBIES - i) + smaller;
    }
    return (uint16_t)rank;
}

static uint16_t rank_ori(const uint8_t o[CUBIES])
{
    uint16_t rank = 0;
    for (uint8_t i = 0; i < 6; ++i)
        rank = (uint16_t)(rank * 3u + o[i]);
    return rank;
}

static void unrank_perm(uint16_t rank, uint8_t p[CUBIES])
{
    static const uint16_t fact[7] = {720, 120, 24, 6, 2, 1, 1};
    uint8_t avail[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint8_t n = CUBIES;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        const uint16_t f = fact[i];
        const uint8_t q = (uint8_t)(rank / f);
        rank = (uint16_t)(rank % f);
        p[i] = avail[q];
        for (uint8_t j = q; j + 1 < n; ++j)
            avail[j] = avail[j + 1];
        --n;
    }
}

static void unrank_ori(uint16_t rank, uint8_t o[CUBIES])
{
    uint8_t sum = 0;
    for (uint8_t i = 6; i-- > 0;) {
        o[i] = (uint8_t)(rank % 3u);
        rank = (uint16_t)(rank / 3u);
        sum = (uint8_t)(sum + o[i]);
    }
    o[6] = (uint8_t)((3u - (sum % 3u)) % 3u);
}

/* Ordered positions of cubies 0,1,2: 7P3 = 210 states. */
static uint8_t rank_partial(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t bi = (uint8_t)(b - (b > a));
    const uint8_t ci = (uint8_t)(c - (c > a) - (c > b));
    return (uint8_t)(a * 30u + bi * 5u + ci);
}

static void unrank_partial(uint8_t rank, uint8_t *a, uint8_t *b, uint8_t *c)
{
    uint8_t avail[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    *a = (uint8_t)(rank / 30u);
    uint8_t r = (uint8_t)(rank % 30u);
    uint8_t bi = (uint8_t)(r / 5u);
    uint8_t ci = (uint8_t)(r % 5u);

    for (uint8_t j = *a; j + 1 < 7; ++j)
        avail[j] = avail[j + 1];
    *b = avail[bi];
    for (uint8_t j = bi; j + 1 < 6; ++j)
        avail[j] = avail[j + 1];
    *c = avail[ci];
}

static void validate_partial_ranking(void)
{
    uint8_t seen[PARTIAL_PERMS] = {0};

    for (uint8_t a = 0; a < CUBIES; ++a) {
        for (uint8_t b = 0; b < CUBIES; ++b) {
            if (b == a)
                continue;
            for (uint8_t c = 0; c < CUBIES; ++c) {
                if (c == a || c == b)
                    continue;
                const uint8_t r = rank_partial(a, b, c);
                if (r >= PARTIAL_PERMS || seen[r]) {
                    fprintf(stderr, "partial rank is not bijective: %u,%u,%u -> %u\n",
                            a, b, c, r);
                    exit(1);
                }
                seen[r] = 1;

                uint8_t aa, bb, cc;
                unrank_partial(r, &aa, &bb, &cc);
                if (aa != a || bb != b || cc != c) {
                    fprintf(stderr,
                            "partial rank round-trip failed: %u,%u,%u -> %u -> %u,%u,%u\n",
                            a, b, c, r, aa, bb, cc);
                    exit(1);
                }
            }
        }
    }

    for (uint16_t r = 0; r < PARTIAL_PERMS; ++r) {
        if (!seen[r]) {
            fprintf(stderr, "partial rank %u was never generated\n", r);
            exit(1);
        }
    }
}

static void build_transitions(uint16_t perm_q[3][PERMUTATIONS],
                              uint16_t ori_q[3][ORIENTATIONS],
                              uint8_t pp_q[3][PARTIAL_PERMS])
{
    state_t s = {{0}, {0}};
    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        unrank_perm(p, s.p);
        memset(s.o, 0, sizeof s.o);
        for (uint8_t f = 0; f < 3; ++f) {
            state_t n = quarter_turn(s, f);
            perm_q[f][p] = rank_perm(n.p);
        }
    }

    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        for (uint8_t i = 0; i < CUBIES; ++i)
            s.p[i] = i;
        unrank_ori(o, s.o);
        for (uint8_t f = 0; f < 3; ++f) {
            state_t n = quarter_turn(s, f);
            ori_q[f][o] = rank_ori(n.o);
        }
    }

    uint8_t dest_of_source[3][CUBIES];
    for (uint8_t f = 0; f < 3; ++f)
        for (uint8_t dest = 0; dest < CUBIES; ++dest)
            dest_of_source[f][source[f][dest]] = dest;

    for (uint16_t pp = 0; pp < PARTIAL_PERMS; ++pp) {
        uint8_t a, b, c;
        unrank_partial((uint8_t)pp, &a, &b, &c);
        for (uint8_t f = 0; f < 3; ++f) {
            pp_q[f][pp] = rank_partial(dest_of_source[f][a],
                                       dest_of_source[f][b],
                                       dest_of_source[f][c]);
        }
    }
}

static uint8_t build_perm_dist(uint16_t perm_q[3][PERMUTATIONS],
                               uint8_t dist[PERMUTATIONS])
{
    uint16_t queue[PERMUTATIONS];
    uint16_t head = 0, tail = 1;
    uint8_t maxd = 0;
    memset(dist, 0xff, PERMUTATIONS);
    dist[0] = 0;
    queue[0] = 0;

    while (head < tail) {
        const uint16_t p = queue[head++];
        const uint8_t nd = (uint8_t)(dist[p] + 1);
        for (uint8_t f = 0; f < 3; ++f) {
            uint16_t np = p;
            for (uint8_t t = 0; t < 3; ++t) {
                np = perm_q[f][np];
                if (dist[np] == 0xff) {
                    dist[np] = nd;
                    if (nd > maxd) maxd = nd;
                    queue[tail++] = np;
                }
            }
        }
    }
    if (tail != PERMUTATIONS) {
        fprintf(stderr, "permutation BFS incomplete: %u/%u\n", tail, PERMUTATIONS);
        exit(1);
    }
    return maxd;
}

static uint8_t build_pdb(uint16_t ori_q[3][ORIENTATIONS],
                         uint8_t pp_q[3][PARTIAL_PERMS],
                         uint8_t dist[PDB_STATES])
{
    uint32_t *queue = malloc((size_t)PDB_STATES * sizeof *queue);
    if (!queue) {
        fputs("out of memory\n", stderr);
        exit(1);
    }
    memset(dist, 0xff, PDB_STATES);
    uint32_t head = 0, tail = 1;
    uint8_t maxd = 0;
    dist[0] = 0;
    queue[0] = 0;

    while (head < tail) {
        const uint32_t idx = queue[head++];
        const uint16_t o = (uint16_t)(idx / PARTIAL_PERMS);
        const uint8_t pp = (uint8_t)(idx % PARTIAL_PERMS);
        const uint8_t nd = (uint8_t)(dist[idx] + 1);

        for (uint8_t f = 0; f < 3; ++f) {
            uint16_t no = o;
            uint8_t npp = pp;
            for (uint8_t t = 0; t < 3; ++t) {
                no = ori_q[f][no];
                npp = pp_q[f][npp];
                const uint32_t ni = (uint32_t)no * PARTIAL_PERMS + npp;
                if (dist[ni] == 0xff) {
                    dist[ni] = nd;
                    if (nd > maxd) maxd = nd;
                    queue[tail++] = ni;
                }
            }
        }
    }
    free(queue);
    if (tail != PDB_STATES) {
        fprintf(stderr, "PDB BFS incomplete: %u/%u\n", tail, PDB_STATES);
        exit(1);
    }
    return maxd;
}

static void emit_u8_1d(FILE *out, const char *name, const uint8_t *a, size_t n)
{
    fprintf(out, "static const uint8_t %s[%zu] = {\n", name, n);
    for (size_t i = 0; i < n; ++i) {
        if ((i & 15u) == 0) fputs("    ", out);
        fprintf(out, "%u%s", a[i], i + 1 == n ? "" : ",");
        if ((i & 15u) == 15u || i + 1 == n) fputc('\n', out);
        else fputc(' ', out);
    }
    fputs("};\n\n", out);
}

static void emit_u8_2d(FILE *out, const char *name, const uint8_t *a,
                       size_t rows, size_t cols)
{
    fprintf(out, "static const uint8_t %s[%zu][%zu] = {\n", name, rows, cols);
    for (size_t r = 0; r < rows; ++r) {
        fputs("    {\n", out);
        for (size_t c = 0; c < cols; ++c) {
            if ((c & 15u) == 0) fputs("        ", out);
            fprintf(out, "%u%s", a[r * cols + c], c + 1 == cols ? "" : ",");
            if ((c & 15u) == 15u || c + 1 == cols) fputc('\n', out);
            else fputc(' ', out);
        }
        fprintf(out, "    }%s\n", r + 1 == rows ? "" : ",");
    }
    fputs("};\n\n", out);
}

static void emit_u16_2d(FILE *out, const char *name, const uint16_t *a,
                        size_t rows, size_t cols)
{
    fprintf(out, "static const uint16_t %s[%zu][%zu] = {\n", name, rows, cols);
    for (size_t r = 0; r < rows; ++r) {
        fputs("    {\n", out);
        for (size_t c = 0; c < cols; ++c) {
            if ((c % 12u) == 0) fputs("        ", out);
            fprintf(out, "%u%s", a[r * cols + c], c + 1 == cols ? "" : ",");
            if ((c % 12u) == 11u || c + 1 == cols) fputc('\n', out);
            else fputc(' ', out);
        }
        fprintf(out, "    }%s\n", r + 1 == rows ? "" : ",");
    }
    fputs("};\n\n", out);
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "usage: %s [output-header]\n", argv[0]);
        return 2;
    }
    char default_path[32];
    const char *path = argc == 2 ? argv[1] : default_output_path(default_path);
    static uint16_t perm_q[3][PERMUTATIONS];
    static uint16_t ori_q[3][ORIENTATIONS];
    static uint8_t pp_q[3][PARTIAL_PERMS];
    static uint8_t perm_dist[PERMUTATIONS];
    static uint8_t pdb[PDB_STATES];
    static uint8_t packed[PDB_PACKED_BYTES];

    validate_partial_ranking();
    build_transitions(perm_q, ori_q, pp_q);
    const uint8_t pmax = build_perm_dist(perm_q, perm_dist);
    const uint8_t hmax = build_pdb(ori_q, pp_q, pdb);
    if (hmax > 15) {
        fprintf(stderr, "PDB max distance %u does not fit a nibble\n", hmax);
        return 1;
    }

    memset(packed, 0, sizeof packed);
    for (uint32_t i = 0; i < PDB_STATES; ++i) {
        if ((i & 1u) == 0)
            packed[i >> 1] = (uint8_t)(pdb[i] & 0x0f);
        else
            packed[i >> 1] |= (uint8_t)((pdb[i] & 0x0f) << 4);
    }
    for (uint32_t i = 0; i < PDB_STATES; ++i) {
        uint8_t got = (i & 1u) ? (uint8_t)(packed[i >> 1] >> 4)
                               : (uint8_t)(packed[i >> 1] & 0x0f);
        if (got != pdb[i]) {
            fprintf(stderr, "packed verification failed at %u\n", i);
            return 1;
        }
    }

    FILE *out = fopen(path, "w");
    if (!out) {
        perror(path);
        return 1;
    }
    fputs("#ifndef PDB_TABLE_H\n#define PDB_TABLE_H\n\n#include <stdint.h>\n\n", out);
    fprintf(out, "#define TABLE_PERM_MAX_DISTANCE %u\n", pmax);
    fprintf(out, "#define TABLE_PDB_MAX_DISTANCE %u\n\n", hmax);
    emit_u16_2d(out, "perm_q", &perm_q[0][0], 3, PERMUTATIONS);
    emit_u16_2d(out, "ori_q", &ori_q[0][0], 3, ORIENTATIONS);
    emit_u8_2d(out, "pp_q", &pp_q[0][0], 3, PARTIAL_PERMS);
    emit_u8_1d(out, "perm_dist", perm_dist, PERMUTATIONS);
    emit_u8_1d(out, "pdb_table", packed, PDB_PACKED_BYTES);
    fputs("#endif\n", out);
    if (fclose(out) != 0) {
        perror(path);
        return 1;
    }

    fprintf(stderr, "generated %s\n", path);
    fprintf(stderr, "perm max=%u, PDB max=%u, PDB states=%u, packed=%u bytes\n",
            pmax, hmax, (unsigned)PDB_STATES, (unsigned)PDB_PACKED_BYTES);
    return 0;
}
