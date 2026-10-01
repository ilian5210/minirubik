CC ?= cc
CFLAGS ?= -O3 -std=c99 -Wall -Wextra -Wpedantic
CLANG_FORMAT := $(shell command -v clang-format-20 2>/dev/null || \
	command -v clang-format 2>/dev/null)
C_SOURCES := $(wildcard *.c *.h tests/*.c)
SAMPLE_STATE := 21345671111111
VECTORS := tests/solutions.txt
CHECKER := tests/check_solver
CHECKER_SRC := tests/check_solver.c

# One per rejection path: short, long, cubie digit low, cubie digit high,
# orientation digit low, orientation digit high, non-digit, duplicate,
# orientation-sum invariant violation.
INVALID_STATES := 1234567111111 123456711111111 02345671111111 82345671111111 \
	12345671111110 12345671111114 1234567111111a 11345671111111 12345671111112

.PHONY: all check clean indent

all: solver mini

# pdb_table.h is generated data used directly by solver.c. Listing it as a
# dependency prevents stale solver binaries after the table is regenerated.
solver: solver.c pdb_table.h
	$(CC) $(CFLAGS) solver.c -o $@

mini: mini.c
	$(CC) $(CFLAGS) mini.c -o $@

$(CHECKER): $(CHECKER_SRC)
	$(CC) $(CFLAGS) $(CHECKER_SRC) -o $(CHECKER)

# Check semantic correctness instead of requiring one particular optimal path.
# The C checker verifies that every returned path solves the cube and that its
# length equals the known optimal length stored in tests/solutions.txt.
check: solver $(VECTORS) $(CHECKER)
	./$(CHECKER) ./solver $(VECTORS)
	@for bad in $(INVALID_STATES); do \
		./solver "$$bad" >/dev/null 2>&1; \
		status=$$?; \
		test $$status -eq 2 || { \
			echo "./solver $$bad: expected status 2, got $$status"; exit 1; }; \
	done
	@./solver >/dev/null 2>&1; status=$$?; \
		test $$status -eq 2 || { \
			echo "./solver with no argument: expected status 2, got $$status"; exit 1; }
	@./solver $(SAMPLE_STATE) $(SAMPLE_STATE) >/dev/null 2>&1; status=$$?; \
		test $$status -eq 2 || { \
			echo "./solver with two arguments: expected status 2, got $$status"; exit 1; }
	@./solver $(SAMPLE_STATE) >&- 2>/dev/null; status=$$?; \
		test $$status -eq 1 || { \
			echo "./solver with stdout closed: expected status 1, got $$status"; exit 1; }
	@echo "invalid input rejected with status 2, unwritable stdout with status 1"

indent:
ifeq ($(CLANG_FORMAT),)
	$(error clang-format 20 not found)
endif
	@$(CLANG_FORMAT) --version | grep -q 'version 20' || \
		{ echo "error: clang-format version 20 required"; exit 1; }
	$(CLANG_FORMAT) -i $(C_SOURCES)

clean:
	$(RM) solver mini $(CHECKER)
