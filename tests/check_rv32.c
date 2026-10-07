/* Host-only Ripes validation/measurement driver. No Python or target emulator.
 * Reuse the independent cubie/BFS oracle from the existing H1-H4 checker.
 * Each case copies the linked ELF, changing only named input data symbols.
 * All search instructions execute in the pinned Ripes process.
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#define main host_gate_main
#include "check_gates.c"
#undef main
#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    uint8_t *bytes;
    size_t size, input, expected, words, stores;
    unsigned text, data;
} image_t;
typedef struct {
    uint64_t iret, cycles, model_ms, rss;
    unsigned status;
    double wall;
} run_t;
static const char *ripes_path;
static const char *output_dir;
static uint8_t *exact_dist;

static void fail(const char *why)
{
    fprintf(stderr, "RV32 FAIL: %s\n", why);
    exit(1);
}
static double seconds(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) fail("clock_gettime");
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); fail("open file"); }
    if (fseek(f, 0, SEEK_END)) fail("seek file");
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET)) fail("file size");
    uint8_t *p = malloc((size_t)n + 1);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) fail("read file");
    fclose(f);
    p[n] = 0;
    *size = (size_t)n;
    return p;
}
static void write_file(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n || fclose(f)) fail("write file");
}
static uint32_t u32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static const uint8_t *section(const image_t *im, unsigned i)
{
    size_t off = u32(im->bytes + 32) + (size_t)i * u16(im->bytes + 46);
    if (i >= u16(im->bytes + 48) || off + 40 > im->size) fail("ELF section bounds");
    return im->bytes + off;
}
static size_t symbol(const image_t *im, const char *wanted)
{
    for (unsigned i = 0; i < u16(im->bytes + 48); ++i) {
        const uint8_t *s = section(im, i);
        if (u32(s + 4) != 2) continue; /* SHT_SYMTAB */
        const uint8_t *strings = section(im, u32(s + 24));
        size_t str_off = u32(strings + 16), str_size = u32(strings + 20);
        size_t off = u32(s + 16), size = u32(s + 20);
        if (off + size > im->size || str_off + str_size > im->size ||
            u32(s + 36) != 16) fail("ELF symbol table bounds");
        for (size_t n = 0; n < size; n += 16) {
            const uint8_t *p = im->bytes + off + n;
            unsigned name = u32(p);
            if (name >= str_size) fail("ELF symbol name");
            if (strcmp((char *)im->bytes + str_off + name, wanted)) continue;
            const uint8_t *owner = section(im, u16(p + 14));
            size_t pos = u32(owner + 16) + u32(p + 4) - u32(owner + 12);
            if (u32(owner + 4) == 8 || pos >= im->size) fail("symbol is not file-backed");
            return pos;
        }
    }
    return 0;
}
static int base_instruction(uint32_t ins)
{
    unsigned op = ins & 127, f3 = (ins >> 12) & 7, f7 = ins >> 25;
    switch (op) {
    case 0x37: case 0x17: case 0x6f: return 1;
    case 0x67: return f3 == 0;
    case 0x63: return f3 == 0 || f3 == 1 || f3 >= 4;
    case 0x03: return f3 <= 2 || f3 == 4 || f3 == 5;
    case 0x23: return f3 <= 2;
    case 0x13: return f3 == 1 ? f7 == 0 : f3 == 5 ? f7 == 0 || f7 == 32 : 1;
    case 0x33: return f7 == 0 || (f7 == 32 && (f3 == 0 || f3 == 5));
    case 0x73: return ins == 0x73; /* ecall, no CSR extensions */
    default: return 0;
    }
}
static image_t load_image(const char *path, int probe)
{
    image_t im = {0};
    im.bytes = read_file(path, &im.size);
    if (im.size < 52 || memcmp(im.bytes, "\177ELF\1\1", 6) ||
        u16(im.bytes + 18) != 243) fail("expected little-endian ELF32 RISC-V");
    for (unsigned i = 0; i < u16(im.bytes + 48); ++i) {
        const uint8_t *s = section(&im, i);
        unsigned flags = u32(s + 8), size = u32(s + 20), off = u32(s + 16);
        if (!(flags & 2)) continue; /* SHF_ALLOC */
        if (flags & 4) {
            if (off + (size_t)size > im.size || size % 4) fail("text bounds/alignment");
            for (unsigned j = 0; j < size; j += 4)
                if (!base_instruction(u32(im.bytes + off + j))) fail("non-RV32I instruction");
            im.text += size;
        } else im.data += size;
    }
    if (im.data > 131072) fail("static data exceeds 128 KiB");
    if (probe) {
        im.words = symbol(&im, "probe_words");
        im.stores = symbol(&im, "probe_stores");
        if (!im.words || !im.stores) fail("missing probe symbols");
    } else {
        im.input = symbol(&im, "input_state");
        im.expected = symbol(&im, "expected_length");
        if (!im.input || !im.expected || im.input + 32 > im.size) fail("missing input symbols");
    }
    return im;
}
static uint64_t json_number(const char *text, const char *key)
{
    char quoted[100];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    const char *p = strstr(text, quoted);
    if (!p || !(p = strchr(p, ':'))) fail("missing Ripes telemetry field");
    char *end;
    uint64_t v = strtoull(p + 1, &end, 10);
    if (end == p + 1) fail("invalid Ripes telemetry number");
    return v;
}
static run_t execute(image_t *im, const char *tag, const char *model, char **log)
{
    char elf[1024], report[1024], logfile[1024], errfile[1024];
    snprintf(elf, sizeof elf, "%s/current.elf", output_dir);
    snprintf(report, sizeof report, "%s/%s-%s.json", output_dir, tag, model);
    snprintf(logfile, sizeof logfile, "%s/%s-%s.log", output_dir, tag, model);
    snprintf(errfile, sizeof errfile, "%s/%s-%s.stderr.log", output_dir, tag, model);
    write_file(elf, im->bytes, im->size);
    /* Do not let a failed run reuse a report from an earlier execution. */
    unlink(report);
    int fd = open(logfile, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    int errfd = open(errfile, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || errfd < 0) fail("open simulator log");
    const double start = seconds();
    pid_t child = fork();
    if (child < 0) fail("fork Ripes");
    if (!child) {
        if (dup2(fd, STDOUT_FILENO) < 0 || dup2(errfd, STDERR_FILENO) < 0) _exit(126);
        close(fd); close(errfd);
        execlp(ripes_path, ripes_path, "--mode", "cli", "--src", elf, "-t", "elf",
            "--proc", model, "--iret", "--cycles", "--exectime", "--regs", "--json",
            "--output", report, "--timeout", "120000", (char *)NULL);
        _exit(127);
    }
    close(fd); close(errfd);
    int status;
    struct rusage usage;
    while (wait4(child, &status, 0, &usage) < 0) if (errno != EINTR) fail("wait4");
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr, "see %s\n", logfile);
        fail("Ripes process failed");
    }
    run_t r = {0};
    r.wall = seconds() - start;
    r.rss = (uint64_t)usage.ru_maxrss;
#ifndef __APPLE__
    r.rss *= 1024; /* Linux reports KiB; macOS reports bytes. */
#endif
    size_t n;
    char *json = (char *)read_file(report, &n);
    r.iret = json_number(json, "# instructions retired");
    r.cycles = json_number(json, "cycles");
    r.model_ms = json_number(json, "execution time (ms)");
    r.status = (unsigned)json_number(json, "x10");
    free(json);
    char *raw = (char *)read_file(logfile, &n);
    /* Ripes print_string may include NUL bytes. */
    size_t w = 0;
    for (size_t i = 0; i < n; ++i) if (raw[i]) raw[w++] = raw[i];
    raw[w] = 0;
    char exit_text[80];
    snprintf(exit_text, sizeof exit_text, "Program exited with code: %u", r.status);
    char *end = strstr(raw, exit_text);
    if (!end) fail("no normal target exit (possible timeout)");
    *end = 0;
    *log = raw;
    return r;
}
static int replay(char *text, const char *input, int distance)
{
    static const char *names[] = {"R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"};
    state_t s;
    for (unsigned i = 0; i < 7; ++i) {
        s.p[i] = (uint8_t)(input[i] - '1');
        s.o[i] = (uint8_t)(input[i + 7] - '1');
    }
    int count = 0;
    char *save;
    for (char *token = strtok_r(text, " \r\n\t", &save); token;
         token = strtok_r(NULL, " \r\n\t", &save)) {
        unsigned move = 0;
        while (move < 9 && strcmp(token, names[move])) ++move;
        if (move == 9 || ++count > 11) fail("invalid move output");
        for (unsigned n = 0; n <= move % 3; ++n) s = ref_quarter_turn(s, move / 3);
    }
    if (count != distance) fail("path length differs from exact BFS distance");
    for (unsigned i = 0; i < 7; ++i)
        if (s.p[i] != i || s.o[i]) fail("host cubie replay did not solve state");
    return count;
}
static FILE *report_file(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", output_dir, name);
    FILE *f = fopen(path, "w");
    if (!f) fail("open report");
    return f;
}
static run_t check_case(image_t *im, const char *tag, const char *input,
                        int expected, const char *model, FILE *csv)
{
    size_t len = strlen(input);
    if (len >= 32) fail("test input capacity");
    memset(im->bytes + im->input, 0, 32);
    memcpy(im->bytes + im->input, input, len);
    put32(im->bytes + im->expected, expected < 0 ? UINT32_MAX : (uint32_t)expected);
    char *log;
    run_t r = execute(im, tag, model, &log);
    unsigned wanted_status = expected < 0 ? 2 : 0;
    if (r.status != wanted_status) {
        fprintf(stderr, "state=%s expected=%d status=%u output=%s\n", input, expected, r.status, log);
        fail("target status");
    }
    if (expected >= 0) replay(log, input, expected);
    if (expected == 11 && r.iret > 50000000) fail("distance-11 instruction budget");
    if (csv) {
        fprintf(csv, "%s\t%s\t%s\t%d\t%u\t%llu\t%llu\t%u\t%u\n", tag, input, model,
            expected, r.status, (unsigned long long)r.iret, (unsigned long long)r.model_ms,
            im->text, im->data);
        fflush(csv);
    }
    free(log);
    return r;
}
static int distance_for(const char *s)
{
    state_t state;
    for (unsigned i = 0; i < 7; ++i) {
        state.p[i] = (uint8_t)(s[i] - '1');
        state.o[i] = (uint8_t)(s[i + 7] - '1');
    }
    return exact_dist[state_index(ref_rank_perm(state.p), ref_rank_ori(state.o))];
}
static void prepare_oracle(void)
{
    build_oracle();
    uint8_t diameter;
    uint32_t histogram[12];
    exact_dist = build_exact_distances(&diameter, histogram);
    if (!exact_dist || diameter != 11 || histogram[11] != 2644) fail("BFS domain mismatch");
}
static void header(FILE *f)
{
    fputs("case\tinput\tmodel\texact_length\tstatus\tiret\texecution_ms\ttext_bytes\tstatic_bytes\n", f);
}
static void smoke(image_t *im)
{
    static const char *cases[] = {
        "12345671111111", "25314672313211", "21345671111111", "12347651111111",
        "62345713133111", "24316572122213", "25713642221111", "24513763133333",
        "43752611332133", "25416373331111"
    };
    static const char *bad[] = {"", "1234567111111", "123456711111111", "02345671111111",
        "82345671111111", "12345671111110", "12345671111114", "1234567111111a",
        "11345671111111", "12345671111112"};
    FILE *csv = report_file("smoke.tsv");
    header(csv);
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        check_case(im, cases[i], cases[i], distance_for(cases[i]), "RV32_ISS", csv);
        if (i < 4) check_case(im, cases[i], cases[i], distance_for(cases[i]), "RV32_5S", csv);
    }
    /* Deterministic LCG over all ranks, with exact BFS lengths for each. */
    uint32_t seed = 20261006;
    for (unsigned i = 0; i < 32; ++i) {
        seed = seed * 1664525u + 1013904223u;
        uint32_t index = seed % STATES;
        char state[15];
        format_state(index / ORIENTATIONS, index % ORIENTATIONS, state);
        check_case(im, state, state, exact_dist[index], "RV32_ISS", csv);
        if (i < 8) check_case(im, state, state, exact_dist[index], "RV32_5S", csv);
    }
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        char tag[30];
        snprintf(tag, sizeof tag, "invalid-%u", i);
        check_case(im, tag, bad[i], -1, "RV32_ISS", csv);
    }
    /* Prove that a wrong expected length really fails on the target. */
    memset(im->bytes + im->input, 0, 32);
    memcpy(im->bytes + im->input, "25314672313211", 14);
    put32(im->bytes + im->expected, 0);
    char *log;
    run_t negative = execute(im, "wrong-expected-length", "RV32_ISS", &log);
    free(log);
    if (negative.status != 1) fail("target length assertion did not reject a wrong expectation");
    fclose(csv);
    puts("T5/T6/T7 PASS: 42 valid inputs, 10 invalid inputs, 12 five-stage cases; negative assertion passed");
}
static void distance11(image_t *im)
{
    FILE *summary = report_file("distance11-summary.txt");
    fputs("INCOMPLETE: distance-11 validation is running\n", summary);
    fclose(summary);
    FILE *csv = report_file("distance11.tsv");
    header(csv);
    uint64_t maximum = 0, minimum = UINT64_MAX, total = 0;
    unsigned count = 0;
    char worst[15] = "", best[15] = "";
    double start = seconds();
    for (unsigned p = 0; p < PERMUTATIONS; ++p)
        for (unsigned o = 0; o < ORIENTATIONS; ++o) {
            if (exact_dist[state_index(p, o)] != 11) continue;
            char state[15];
            format_state(p, o, state);
            run_t r = check_case(im, state, state, 11, "RV32_ISS", csv);
            if (r.iret > maximum) { maximum = r.iret; strcpy(worst, state); }
            if (r.iret < minimum) { minimum = r.iret; strcpy(best, state); }
            total += r.iret;
            if (++count % 100 == 0) {
                printf("distance-11: %u/2644, max=%llu, wall=%.1f s\n", count,
                    (unsigned long long)maximum, seconds() - start);
                fflush(stdout);
            }
        }
    fclose(csv);
    if (count != 2644) fail("incomplete distance-11 enumeration");
    summary = report_file("distance11-summary.txt");
    fprintf(summary, "PASS: %u/2644, every path optimal and replayed on target and host\n"
        "min_iret=%llu state=%s\nmax_iret=%llu state=%s\nmean_iret=%.2f\n"
        "limit=50000000\nwall_seconds=%.3f\ntext_bytes=%u\nstatic_bytes=%u\n",
        count, (unsigned long long)minimum, best, (unsigned long long)maximum, worst,
        (double)total / count, seconds() - start, im->text, im->data);
    fclose(summary);
    printf("distance-11 PASS: all %u; max=%llu at %s\n", count, (unsigned long long)maximum, worst);
}
static void compare(int argc, char **argv)
{
    if (argc != 7) fail("compare needs max, early, and C reference ELFs");
    static const char *variants[] = {"asm-max", "asm-early", "gcc-O2"};
    static const char *states[] = {"12345671111111", "25314672313211", "21345671111111", "12347651111111"};
    FILE *csv = report_file("comparison.tsv");
    header(csv);
    for (unsigned v = 0; v < 3; ++v) {
        image_t im = load_image(argv[4 + v], 0);
        for (unsigned i = 0; i < 4; ++i) {
            char tag[80];
            snprintf(tag, sizeof tag, "%s-%s", variants[v], states[i]);
            run_t r = check_case(&im, tag, states[i], distance_for(states[i]), "RV32_ISS", csv);
            printf("%s: iret=%llu text=%u static=%u\n", tag, (unsigned long long)r.iret, im.text, im.data);
        }
        free(im.bytes);
    }
    fclose(csv);
}
static void twist(int argc, char **argv)
{
    if (argc != 6) fail("twist needs branching and branchless ELFs");
    static const char *variants[] = {"branching", "branchless"};
    static const char *models[] = {"RV32_ISS", "RV32_5S"};
    static const char *states[] = {"12345671111111", "25314672313211", "21345671111111"};
    run_t runs[2][2][3];
    FILE *summary = report_file("twist-summary.txt");
    fputs("INCOMPLETE: branching/branchless validation is running\n", summary);
    fclose(summary);
    FILE *csv = report_file("twist.tsv");
    fputs("variant\tinput\tmodel\texact_length\tstatus\tiret\tcycles\texecution_ms\t"
        "text_bytes\tstatic_bytes\twall_seconds\n", csv);
    for (unsigned v = 0; v < 2; ++v) {
        image_t im = load_image(argv[4 + v], 0);
        for (unsigned m = 0; m < 2; ++m)
            for (unsigned s = 0; s < 3; ++s) {
                char tag[80];
                snprintf(tag, sizeof tag, "%s-%s", variants[v], states[s]);
                int distance = distance_for(states[s]);
                run_t r = check_case(&im, tag, states[s], distance, models[m], NULL);
                runs[v][m][s] = r;
                fprintf(csv, "%s\t%s\t%s\t%d\t%u\t%llu\t%llu\t%llu\t%u\t%u\t%.6f\n",
                    variants[v], states[s], models[m], distance, r.status,
                    (unsigned long long)r.iret, (unsigned long long)r.cycles,
                    (unsigned long long)r.model_ms, im.text, im.data, r.wall);
                fflush(csv);
                printf("twist %s %s %s: iret=%llu cycles=%llu model_ms=%llu text=%u static=%u\n",
                    variants[v], models[m], states[s], (unsigned long long)r.iret,
                    (unsigned long long)r.cycles, (unsigned long long)r.model_ms, im.text, im.data);
                fflush(stdout);
            }
        free(im.bytes);
    }
    fclose(csv);
    summary = report_file("twist-summary.txt");
    fputs("PASS: 2 variants x 2 processor models x 3 inputs; every path optimal, "
        "target length assertion and target/host cubie replay passed\n"
        "Change percentages are (branchless / branching - 1) * 100. "
        "Execution time is a single model measurement, not a timing benchmark.\n", summary);
    for (unsigned m = 0; m < 2; ++m)
        for (unsigned s = 0; s < 3; ++s) {
            run_t *a = &runs[0][m][s], *b = &runs[1][m][s];
            fprintf(summary, "model=%s input=%s branching_iret=%llu branchless_iret=%llu "
                "iret_change_percent=%.6f branching_cycles=%llu branchless_cycles=%llu "
                "cycles_change_percent=%.6f\n", models[m], states[s],
                (unsigned long long)a->iret, (unsigned long long)b->iret,
                a->iret ? 100.0 * ((double)b->iret / a->iret - 1.0) : 0.0,
                (unsigned long long)a->cycles, (unsigned long long)b->cycles,
                a->cycles ? 100.0 * ((double)b->cycles / a->cycles - 1.0) : 0.0);
        }
    fclose(summary);
}
static uint64_t median3(uint64_t a[3])
{
    if (a[0] > a[1]) { uint64_t t = a[0]; a[0] = a[1]; a[1] = t; }
    if (a[1] > a[2]) { uint64_t t = a[1]; a[1] = a[2]; a[2] = t; }
    return a[0] > a[1] ? a[0] : a[1];
}
static void probe(image_t *im)
{
    static const char *models[] = {"RV32_ISS", "RV32_5S"};
    static const unsigned spans[] = {4096, 1048576, 2097152, 4194304};
    const unsigned stores = 1048576;
    FILE *csv = report_file("probe.tsv"), *summary = report_file("probe-summary.txt");
    fputs("model\tspan_bytes\trepeat\tstores\tiret\texecution_ms\twall_seconds\tpeak_rss_bytes\n", csv);
    for (unsigned m = 0; m < 2; ++m) {
        uint64_t rss[4], rate[4];
        for (unsigned s = 0; s < 4; ++s) {
            uint64_t samples[3], rates[3];
            for (unsigned rep = 0; rep < 3; ++rep) {
                put32(im->bytes + im->words, spans[s] / 4);
                put32(im->bytes + im->stores, stores);
                char tag[80], *log;
                snprintf(tag, sizeof tag, "probe-%u-%u", spans[s], rep);
                run_t r = execute(im, tag, models[m], &log);
                free(log);
                uint64_t expected = (uint64_t)stores * 5 + stores / (spans[s] / 4) + 13;
                if (r.status || r.iret != expected || !r.model_ms) fail("probe count/status/time");
                samples[rep] = r.rss;
                rates[rep] = r.iret * 1000 / r.model_ms;
                fprintf(csv, "%s\t%u\t%u\t%u\t%llu\t%llu\t%.6f\t%llu\n", models[m],
                    spans[s], rep, stores, (unsigned long long)r.iret, (unsigned long long)r.model_ms,
                    r.wall, (unsigned long long)r.rss);
                fflush(csv);
            }
            rss[s] = median3(samples); rate[s] = median3(rates);
            printf("%s span=%u median_rss=%llu rate=%llu insn/s\n", models[m], spans[s],
                (unsigned long long)rss[s], (unsigned long long)rate[s]);
            fflush(stdout);
        }
        /* Linear regression through all four median points, with intercept. */
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (unsigned s = 0; s < 4; ++s) {
            double x = spans[s], y = (double)rss[s];
            sx += x; sy += y; sxx += x*x; sxy += x*y;
        }
        double slope = (4*sxy - sx*sy) / (4*sxx - sx*sx);
        double intercept = (sy - slope*sx) / 4;
        fprintf(summary, "%s\nregression_host_bytes_per_guest_byte=%.6f\n"
            "intercept_bytes=%.0f\nprojected_increment_for_18405414_bytes=%.0f\n"
            "projected_total_bytes=%.0f\nsmall_region_median_iret_per_second=%llu\n"
            "idealized_1e9_seconds=%.3f\n\n", models[m], slope, intercept,
            slope*18405414, intercept + slope*18405414,
            (unsigned long long)rate[0], 1e9 / rate[0]);
        fflush(summary);
    }
    fclose(csv); fclose(summary);
}

/* LED tables come from the actual linked renderer.  The oracle below models
 * corner positions and sticker normals in 3-D, rather than copying its tables.
 * Axes are x=right, y=up, z=front; face IDs are U,L,F,R,B,D.
 */
typedef struct {
    const uint8_t *cubie_faces, *facelets, *palette;
} led_tables_t;
static const int8_t corner_xyz[8][3] = {
    {-1,1,1}, {1,1,1}, {1,-1,1}, {-1,-1,1},
    {1,1,-1}, {1,-1,-1}, {-1,-1,-1}, {-1,1,-1}
};
static const int8_t face_normal[6][3] = {
    {0,1,0}, {-1,0,0}, {0,0,1}, {1,0,0}, {0,0,-1}, {0,-1,0}
};
static const int8_t face_horizontal[6][3] = {
    {1,0,0}, {0,0,1}, {1,0,0}, {0,0,-1}, {-1,0,0}, {1,0,0}
};
static const int8_t face_vertical[6][3] = {
    {0,0,1}, {0,-1,0}, {0,-1,0}, {0,-1,0}, {0,-1,0}, {0,0,-1}
};
static const uint8_t net_origin[6][2] = {
    {9,0}, {0,7}, {9,7}, {18,7}, {27,7}, {9,14}
};
static const uint8_t *led_symbol_data(const image_t *im, const char *name, size_t n)
{
    size_t off = symbol(im, name);
    if (!off || off > im->size || n > im->size - off) {
        fprintf(stderr, "missing/truncated renderer symbol: %s\n", name);
        fail("LED ELF symbol bounds");
    }
    return im->bytes + off;
}
static unsigned geometry_face(unsigned corner, unsigned slot)
{
    int x = corner_xyz[corner][0], y = corner_xyz[corner][1], z = corner_xyz[corner][2];
    if (!slot) return y > 0 ? 0 : 5;
    /* The two remaining normals follow a common corner handedness. */
    if ((slot == 1) == (x * y * z > 0)) return x > 0 ? 3 : 1;
    return z > 0 ? 2 : 4;
}
static unsigned geometry_slot(unsigned corner, unsigned face)
{
    for (unsigned k = 0; k < 3; ++k)
        if (geometry_face(corner, k) == face) return k;
    fail("geometry face does not meet corner");
    return 0;
}
static unsigned geometry_corner(const int8_t xyz[3])
{
    for (unsigned p = 0; p < 8; ++p)
        if (!memcmp(xyz, corner_xyz[p], 3)) return p;
    fail("geometry corner position");
    return 0;
}
static void geometry_rotate(const int8_t in[3], unsigned face, int8_t out[3])
{
    int8_t x = in[0], y = in[1], z = in[2];
    if (face == 0) { out[0] = x; out[1] = z; out[2] = -y; }
    else if (face == 1) { out[0] = -y; out[1] = x; out[2] = z; }
    else { out[0] = z; out[1] = y; out[2] = -x; }
}
static int geometry_affected(unsigned corner, unsigned face)
{
    return face == 0 ? corner_xyz[corner][0] > 0 :
           face == 1 ? corner_xyz[corner][2] < 0 : corner_xyz[corner][1] < 0;
}
static unsigned geometry_normal_face(const int8_t normal[3])
{
    for (unsigned f = 0; f < 6; ++f)
        if (!memcmp(normal, face_normal[f], 3)) return f;
    fail("geometry sticker normal");
    return 0;
}
static unsigned geometry_face_corner(unsigned face, unsigned row, unsigned col)
{
    int8_t xyz[3];
    for (unsigned axis = 0; axis < 3; ++axis)
        xyz[axis] = (int8_t)(face_normal[face][axis] +
            face_horizontal[face][axis] * (col ? 1 : -1) +
            face_vertical[face][axis] * (row ? 1 : -1));
    return geometry_corner(xyz);
}
static unsigned led_net_index(unsigned x, unsigned y)
{
    for (unsigned f = 0; f < 6; ++f)
        if ((x == net_origin[f][0] || x == net_origin[f][0] + 4u) &&
            (y == net_origin[f][1] || y == net_origin[f][1] + 3u))
            return 4 * f + 2 * ((y - net_origin[f][1]) / 3) + (x - net_origin[f][0]) / 4;
    fail("LED facelet outside six-face pixel net");
    return 0;
}
static uint8_t led_color(const led_tables_t *tables, unsigned cubie,
                         unsigned slot, unsigned orientation)
{
    return tables->cubie_faces[3 * cubie + (slot + orientation) % 3];
}
static void led_net(const led_tables_t *tables, const state_t *state, uint8_t net[24])
{
    for (unsigned i = 0; i < 24; ++i) {
        const uint8_t *d = tables->facelets + 4 * i;
        unsigned corner = d[2], cubie = corner ? state->p[corner - 1] + 1u : 0;
        unsigned orientation = corner ? state->o[corner - 1] : 0;
        net[led_net_index(d[0], d[1])] = led_color(tables, cubie, d[3], orientation);
    }
}
static void geometry_solved(uint8_t stickers[8][3])
{
    for (unsigned p = 0; p < 8; ++p)
        for (unsigned k = 0; k < 3; ++k) stickers[p][k] = (uint8_t)geometry_face(p, k);
}
static void geometry_turn(uint8_t stickers[8][3], unsigned face)
{
    uint8_t next[8][3];
    for (unsigned p = 0; p < 8; ++p) {
        int affected = geometry_affected(p, face);
        unsigned dest = p;
        int8_t rotated[3];
        if (affected) { geometry_rotate(corner_xyz[p], face, rotated); dest = geometry_corner(rotated); }
        for (unsigned k = 0; k < 3; ++k) {
            unsigned normal = geometry_face(p, k);
            if (affected) {
                geometry_rotate(face_normal[normal], face, rotated);
                normal = geometry_normal_face(rotated);
            }
            next[dest][geometry_slot(dest, normal)] = stickers[p][k];
        }
    }
    memcpy(stickers, next, sizeof next);
}
static void geometry_net(const uint8_t stickers[8][3], uint8_t net[24])
{
    for (unsigned f = 0; f < 6; ++f)
        for (unsigned row = 0; row < 2; ++row)
            for (unsigned col = 0; col < 2; ++col) {
                unsigned p = geometry_face_corner(f, row, col);
                net[4 * f + 2 * row + col] = stickers[p][geometry_slot(p, f)];
            }
}
static void led_net_invariants(const led_tables_t *tables, const uint8_t net[24])
{
    unsigned counts[6] = {0}, fixed = 0;
    for (unsigned i = 0; i < 24; ++i) {
        if (net[i] >= 6) fail("LED color ID outside palette");
        ++counts[net[i]];
        const uint8_t *d = tables->facelets + 4 * i;
        unsigned n = led_net_index(d[0], d[1]);
        if (!d[2]) {
            ++fixed;
            if (net[n] != n / 4) fail("LED fixed corner changed color");
        }
    }
    for (unsigned f = 0; f < 6; ++f)
        if (counts[f] != 4) fail("LED net must contain four stickers of each color");
    if (fixed != 3) fail("LED fixed corner does not have three facelets");
}
static void led_map(image_t *im)
{
    led_tables_t tables = {
        led_symbol_data(im, "led_cubie_faces", 24),
        led_symbol_data(im, "led_facelets", 96),
        led_symbol_data(im, "led_palette", 24)
    };
    for (unsigned f = 0; f < 6; ++f) {
        uint32_t rgb = u32(tables.palette + 4 * f);
        if (rgb >> 24) fail("LED palette value is not RGB24");
        for (unsigned before = 0; before < f; ++before)
            if (rgb == u32(tables.palette + 4 * before)) fail("LED palette colors are not distinct");
    }
    for (unsigned p = 0; p < 8; ++p)
        for (unsigned k = 0; k < 3; ++k)
            if (tables.cubie_faces[3 * p + k] != geometry_face(p, k))
                fail("LED cubie color order differs from physical corner normals");
    uint32_t seen = 0;
    for (unsigned i = 0; i < 24; ++i) {
        const uint8_t *d = tables.facelets + 4 * i;
        if (d[2] >= 8 || d[3] >= 3) fail("LED facelet corner/slot bounds");
        unsigned n = led_net_index(d[0], d[1]), f = n / 4;
        if (seen & (1u << n)) fail("duplicate LED facelet coordinates");
        seen |= 1u << n;
        unsigned p = geometry_face_corner(f, (n % 4) / 2, n % 2);
        if (d[2] != p || d[3] != geometry_slot(p, f))
            fail("LED net descriptor differs from exterior face geometry");
    }
    if (seen != 0xffffff) fail("incomplete LED face net");

    unsigned cases = 0;
    for (unsigned f = 0; f < 3; ++f)
        for (unsigned src = 1; src < 8; ++src) {
            int affected = geometry_affected(src, f);
            unsigned dest = src;
            int8_t rotated[3];
            if (affected) { geometry_rotate(corner_xyz[src], f, rotated); dest = geometry_corner(rotated); }
            if (ref_source[f][dest - 1] != src - 1) fail("cubie source differs from geometry");
            for (unsigned cubie = 0; cubie < 8; ++cubie)
                for (unsigned orientation = 0; orientation < 3; ++orientation)
                    for (unsigned slot = 0; slot < 3; ++slot) {
                        unsigned normal = geometry_face(src, slot);
                        if (affected) {
                            geometry_rotate(face_normal[normal], f, rotated);
                            normal = geometry_normal_face(rotated);
                        }
                        unsigned next_slot = geometry_slot(dest, normal);
                        unsigned next_orientation = (orientation + ref_twist[f][dest - 1]) % 3;
                        if (led_color(&tables, cubie, slot, orientation) !=
                            led_color(&tables, cubie, next_slot, next_orientation))
                            fail("LED corner orientation disagrees with geometric turn");
                        ++cases;
                    }
        }
    static const char *goldens[9] = {
        "UFUFLLLLFDFDRRRRUBUBDBDB", "UDUDLLLLFBFBRRRRFBFBDUDU", "UBUBLLLLFUFURRRRDBDBDFDF",
        "RRUUULULFFFFRDRDBBBBDDLL", "DDUURLRLFFFFRLRLBBBBDDUU", "LLUUDLDLFFFFRURUBBBBDDRR",
        "UUUULLBBFFLLRRFFBBRRDDDD", "UUUULLRRFFBBRRLLBBFFDDDD", "UUUULLFFFFRRRRBBBBLLDDDD"
    };
    const state_t solved = {{0,1,2,3,4,5,6}, {0}};
    uint8_t solved_stickers[8][3], solved_net[24];
    geometry_solved(solved_stickers); geometry_net(solved_stickers, solved_net);
    for (unsigned move = 0; move < 9; ++move) {
        state_t state = solved;
        uint8_t stickers[8][3], actual[24], expected[24];
        geometry_solved(stickers);
        unsigned f = move / 3, quarters = move % 3 + 1;
        for (unsigned n = 0; n < quarters; ++n) {
            state = ref_quarter_turn(state, (uint8_t)f); geometry_turn(stickers, f);
        }
        led_net(&tables, &state, actual); geometry_net(stickers, expected);
        if (memcmp(actual, expected, 24)) fail("LED move net differs from geometry");
        for (unsigned i = 0; i < 24; ++i)
            if ("ULFRBD"[actual[i]] != goldens[move][i]) fail("LED move net differs from golden");
        led_net_invariants(&tables, actual);
        for (unsigned n = quarters; n < 4; ++n) {
            state = ref_quarter_turn(state, (uint8_t)f); geometry_turn(stickers, f);
        }
        led_net(&tables, &state, actual); geometry_net(stickers, expected);
        if (memcmp(&state, &solved, sizeof state) || memcmp(actual, solved_net, 24) ||
            memcmp(expected, solved_net, 24)) fail("LED move/inverse or four-turn recovery");
        led_net_invariants(&tables, actual);
    }
    printf("LED map PASS: %u geometric sticker rotations; 9 move nets/goldens, RGB24 palette, "
        "four stickers per color, fixed corner, inverse/four-turn recovery; text=%u static=%u bytes\n",
        cases, im->text, im->data);
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "gates")) {
        char *args[] = {argv[0], "all", NULL};
        return host_gate_main(2, args);
    }
    if (argc == 3 && !strcmp(argv[1], "led-map")) {
        image_t im = load_image(argv[2], 0);
        led_map(&im); free(im.bytes); return 0;
    }
    if (argc < 5) {
        fprintf(stderr, "usage: %s smoke|distance11|compare|probe|twist RIPES OUTDIR ELF...\n"
            "       %s led-map RENDERER_ELF\n", argv[0], argv[0]);
        return 2;
    }
    ripes_path = argv[2]; output_dir = argv[3];
    if (mkdir(output_dir, 0755) && errno != EEXIST) fail("create output directory");
    if (!strcmp(argv[1], "probe")) {
        image_t im = load_image(argv[4], 1);
        probe(&im); free(im.bytes); return 0;
    }
    prepare_oracle();
    if (!strcmp(argv[1], "compare")) compare(argc, argv);
    else if (!strcmp(argv[1], "twist")) twist(argc, argv);
    else {
        image_t im = load_image(argv[4], 0);
        if (!strcmp(argv[1], "smoke")) smoke(&im);
        else if (!strcmp(argv[1], "distance11")) distance11(&im);
        else fail("unknown mode");
        free(im.bytes);
    }
    free(exact_dist);
    return 0;
}
