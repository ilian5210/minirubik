#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define CUBIES 7
#define OUTPUT_CAP 512

typedef struct {
    unsigned char p[CUBIES];
    unsigned char o[CUBIES];
} cube_t;

/* Independent quarter-turn model used only by the host-side regression test. */
static const unsigned char source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6}, /* R */
    {0, 1, 2, 4, 5, 6, 3}, /* B */
    {0, 2, 5, 3, 1, 4, 6}  /* D */
};

static const unsigned char twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0}
};

static int parse_state(const char *text, cube_t *cube)
{
    if (strlen(text) != 14)
        return 0;

    for (int i = 0; i < CUBIES; ++i) {
        if (text[i] < '1' || text[i] > '7')
            return 0;
        if (text[i + CUBIES] < '1' || text[i + CUBIES] > '3')
            return 0;
        cube->p[i] = (unsigned char)(text[i] - '1');
        cube->o[i] = (unsigned char)(text[i + CUBIES] - '1');
    }
    return 1;
}

static void quarter_turn(cube_t *cube, int face)
{
    cube_t old = *cube;
    for (int i = 0; i < CUBIES; ++i) {
        const int src = source[face][i];
        cube->p[i] = old.p[src];
        cube->o[i] = (unsigned char)((old.o[src] + twist[face][i]) % 3);
    }
}

static int decode_move(const char *token, int *face, int *turns)
{
    if (token[0] == 'R')
        *face = 0;
    else if (token[0] == 'B')
        *face = 1;
    else if (token[0] == 'D')
        *face = 2;
    else
        return 0;

    if (token[1] == '\0')
        *turns = 1;
    else if (token[1] == '2' && token[2] == '\0')
        *turns = 2;
    else if (token[1] == '\'' && token[2] == '\0')
        *turns = 3;
    else
        return 0;

    return 1;
}

static int solved(const cube_t *cube)
{
    for (int i = 0; i < CUBIES; ++i) {
        if (cube->p[i] != (unsigned char)i || cube->o[i] != 0)
            return 0;
    }
    return 1;
}

static int count_tokens(const char *text)
{
    int count = 0;
    int inside = 0;

    while (*text != '\0') {
        if (isspace((unsigned char)*text)) {
            inside = 0;
        } else if (!inside) {
            ++count;
            inside = 1;
        }
        ++text;
    }
    return count;
}

static void trim_line_end(char *text)
{
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r'))
        text[--n] = '\0';
}

static int run_solver(const char *solver, const char *state,
                      char output[OUTPUT_CAP])
{
    char command[OUTPUT_CAP];
    const int written = snprintf(command, sizeof(command), "%s %s", solver, state);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        fprintf(stderr, "checker: command too long\n");
        return 0;
    }

    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
        perror("popen");
        return 0;
    }

    size_t used = fread(output, 1, OUTPUT_CAP - 1, pipe);
    output[used] = '\0';

    if (used == OUTPUT_CAP - 1 && !feof(pipe)) {
        fprintf(stderr, "%s %s: output too long\n", solver, state);
        (void)pclose(pipe);
        return 0;
    }

    const int status = pclose(pipe);
    if (status == -1) {
        perror("pclose");
        return 0;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "%s %s: solver exit status %d\n", solver, state,
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return 0;
    }

    return 1;
}

static int verify_solution(const char *state_text, char *output,
                           int expected_len, int *actual_len)
{
    cube_t cube;
    if (!parse_state(state_text, &cube)) {
        fprintf(stderr, "checker: invalid test vector state %s\n", state_text);
        return 0;
    }

    *actual_len = 0;
    char *save = NULL;
    for (char *token = strtok_r(output, " \t\r\n", &save); token != NULL;
         token = strtok_r(NULL, " \t\r\n", &save)) {
        int face, turns;
        if (!decode_move(token, &face, &turns)) {
            fprintf(stderr, "%s: illegal move token '%s'\n", state_text, token);
            return 0;
        }
        for (int i = 0; i < turns; ++i)
            quarter_turn(&cube, face);
        ++*actual_len;
    }

    if (!solved(&cube)) {
        fprintf(stderr, "%s: returned path does not solve the cube\n", state_text);
        return 0;
    }

    if (*actual_len != expected_len) {
        fprintf(stderr, "%s: expected optimal length %d, got %d\n",
                state_text, expected_len, *actual_len);
        return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s SOLVER tests/solutions.txt\n", argv[0]);
        return 2;
    }

    FILE *vectors = fopen(argv[2], "r");
    if (vectors == NULL) {
        perror(argv[2]);
        return 2;
    }

    char line[OUTPUT_CAP];
    int lineno = 0;
    int checked = 0;

    while (fgets(line, sizeof(line), vectors) != NULL) {
        ++lineno;
        trim_line_end(line);

        char *cursor = line;
        while (isspace((unsigned char)*cursor))
            ++cursor;
        if (*cursor == '\0' || *cursor == '#')
            continue;

        char *separator = strchr(cursor, '|');
        if (separator == NULL) {
            fprintf(stderr, "%s:%d: missing '|' separator\n", argv[2], lineno);
            fclose(vectors);
            return 1;
        }

        *separator = '\0';
        const char *state = cursor;
        const char *reference = separator + 1;
        const int expected_len = count_tokens(reference);

        char output[OUTPUT_CAP];
        if (!run_solver(argv[1], state, output)) {
            fclose(vectors);
            return 1;
        }

        int actual_len;
        if (!verify_solution(state, output, expected_len, &actual_len)) {
            fclose(vectors);
            return 1;
        }

        printf("ok  depth=%2d  %s\n", actual_len, state);
        ++checked;
    }

    if (ferror(vectors)) {
        perror(argv[2]);
        fclose(vectors);
        return 1;
    }
    fclose(vectors);

    printf("%d vectors solved with optimal length\n", checked);
    return 0;
}
