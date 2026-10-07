CC ?= cc
CFLAGS ?= -O3 -std=c99 -Wall -Wextra -Wpedantic
CLANG_FORMAT := $(shell command -v clang-format-20 2>/dev/null || \
	command -v clang-format 2>/dev/null)
C_SOURCES := $(wildcard *.c *.h tests/*.c)
SAMPLE_STATE := 21345671111111
VECTORS := tests/solutions.txt
CHECKER := tests/check_solver
CHECKER_SRC := tests/check_solver.c
GATE_CHECKER := tests/check_gates
GATE_CHECKER_SRC := tests/check_gates.c

# One per rejection path: short, long, cubie digit low, cubie digit high,
# orientation digit low, orientation digit high, non-digit, duplicate,
# orientation-sum invariant violation.
INVALID_STATES := 1234567111111 123456711111111 02345671111111 82345671111111 \
	12345671111110 12345671111114 1234567111111a 11345671111111 12345671111112

.PHONY: all check h1 h2 h3 h4 gates-fast gates clean indent rv32 run-rv32 \
	check-rv32 check-rv32-all bench-rv32 probe-rv32 verify-rv32 rv32-info \
	rv32-gui check-led bench-twist

all: solver mini

# pdb_table.h is generated data used directly by solver.c. Listing it as a
# dependency prevents stale solver binaries after the table is regenerated.
solver: solver.c pdb_table.h
	$(CC) $(CFLAGS) solver.c -o $@

mini: mini.c
	$(CC) $(CFLAGS) mini.c -o $@

$(CHECKER): $(CHECKER_SRC)
	$(CC) $(CFLAGS) $(CHECKER_SRC) -o $(CHECKER)

$(GATE_CHECKER): $(GATE_CHECKER_SRC) solver.c pdb_table.h
	$(CC) $(CFLAGS) $(GATE_CHECKER_SRC) -o $(GATE_CHECKER)

# Fast semantic regression tests. These do not replace H1-H4.
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

# Host-side correctness gates required by the assignment.
h1: $(GATE_CHECKER)
	./$(GATE_CHECKER) h1

h2: $(GATE_CHECKER)
	./$(GATE_CHECKER) h2

h3: $(GATE_CHECKER)
	./$(GATE_CHECKER) h3

h4: $(GATE_CHECKER)
	./$(GATE_CHECKER) h4

# H1/H2/H4 are cheap. H3 runs the actual search over all 3,674,160 states.
gates-fast: $(GATE_CHECKER)
	./$(GATE_CHECKER) fast

gates: $(GATE_CHECKER)
	./$(GATE_CHECKER) all

# Link the RV32I assembly with C-compiled tables; no runtime libraries.
RISCV_CC ?= $(shell command -v riscv64-unknown-elf-gcc 2>/dev/null || \
	command -v riscv64-elf-gcc 2>/dev/null || \
	command -v riscv32-unknown-elf-gcc 2>/dev/null)
RV32_FLAGS := -march=rv32i -mabi=ilp32 -mno-relax
RV32_STATE ?= 21345671111111
RV32_EARLY_PERM ?= 1
RV32_PROC ?= RV32_ISS
RV32_RENDER_DELAY ?= 40000
RIPES_IO_HEADER ?= $(if $(TMPDIR),$(TMPDIR),/tmp/)ripes_system.h
RIPES ?= $(shell command -v Ripes 2>/dev/null || command -v ripes 2>/dev/null || \
	for app in /Applications/*Ripes*.app "$$HOME"/Applications/*Ripes*.app \
		"$$HOME"/Downloads/*Ripes*.app; do \
		if test -x "$$app/Contents/MacOS/Ripes"; then \
			printf '%s\n' "$$app/Contents/MacOS/Ripes"; break; \
		fi; \
	done)

build/rv32/tables.o: rv32/tables.c pdb_table.h Makefile
	@test -n "$(RISCV_CC)" || \
		{ echo "RISC-V GCC not found; set RISCV_CC to its executable path"; exit 1; }
	@mkdir -p build/rv32
	@"$(RISCV_CC)" $(RV32_FLAGS) -O2 -ffreestanding \
		-c rv32/tables.c -o $@

rv32: build/rv32/tables.o solver_rv32.S rv32/link.ld
	@test -n "$(RISCV_CC)" || \
		{ echo "RISC-V GCC not found; set RISCV_CC to its executable path"; exit 1; }
	@"$(RISCV_CC)" $(RV32_FLAGS) -DSTATE='"$(RV32_STATE)"' \
		-DEARLY_PERM=$(RV32_EARLY_PERM) -nostdlib -nostartfiles \
		-Wl,--no-relax -Wl,-T,rv32/link.ld -Wl,-Map,build/rv32/solver.map \
		solver_rv32.S build/rv32/tables.o -o build/rv32/solver.elf

# Ripes exports this header when a GUI peripheral is created/reconfigured.
# Cache it because a separate CLI process replaces the shared header.
rv32-gui: build/rv32/tables.o solver_rv32.S rv32/link.ld
	@if test -f "$(RIPES_IO_HEADER)" && \
		grep -Eq '^#define[[:space:]]+LED_MATRIX_0_BASE[[:space:]]' "$(RIPES_IO_HEADER)"; then \
		cp "$(RIPES_IO_HEADER)" build/rv32/ripes_system.h; \
	elif test -f build/rv32/ripes_system.h; then \
		echo "Using cached Ripes I/O map: build/rv32/ripes_system.h"; \
	else \
		echo "Create LED Matrix 0 (35 x 25) in Ripes GUI first"; exit 1; \
	fi
	@"$(RISCV_CC)" $(RV32_FLAGS) -DSTATE='"$(RV32_STATE)"' \
		-DEARLY_PERM=$(RV32_EARLY_PERM) -DRENDER=1 \
		-DRENDER_DELAY=$(RV32_RENDER_DELAY) -include build/rv32/ripes_system.h \
		-nostdlib -nostartfiles -Wl,--no-relax -Wl,-T,rv32/link.ld \
		-Wl,-Map,build/rv32/solver-led.map \
		solver_rv32.S build/rv32/tables.o -o build/rv32/solver-led.elf

check-led: rv32-gui build/rv32/check_rv32
	@./build/rv32/check_rv32 led-map build/rv32/solver-led.elf

run-rv32: rv32
	@test -n "$(RIPES)" || \
		{ echo "Ripes not found; set RIPES to its executable path"; exit 1; }
	@"$(RIPES)" --mode cli --src build/rv32/solver.elf -t elf \
		--proc $(RV32_PROC) --iret

# Reproducible host tests. Pair Apple's CLT compiler with its own SDK rather
# than mixing an older selected Xcode compiler with a newer system SDK.
ifneq ($(wildcard /Library/Developer/CommandLineTools/usr/bin/clang),)
RV32_HOST_CC ?= /Library/Developer/CommandLineTools/usr/bin/clang
RV32_HOST_FLAGS ?= -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
else
RV32_HOST_CC ?= $(CC)
RV32_HOST_FLAGS ?=
endif

build/rv32/check_rv32: tests/check_rv32.c tests/check_gates.c solver.c pdb_table.h Makefile
	@mkdir -p build/rv32
	@"$(RV32_HOST_CC)" $(CFLAGS) $(RV32_HOST_FLAGS) $< -o $@

build/rv32/reference.o: rv32/reference.c solver.c pdb_table.h Makefile
	@mkdir -p build/rv32
	@"$(RISCV_CC)" $(RV32_FLAGS) -O2 -ffreestanding -fstack-usage -c $< -o $@
	@awk 'BEGIN { bad=0 } $$2 > 512 || $$3 != "static" { bad=1 } \
		END { exit (bad || NR != 1) }' build/rv32/reference.su || \
		{ echo "Re-audit C call depth/512-byte stack for this compiler (reference.su)"; exit 1; }

build/rv32/reference.elf: solver_rv32.S build/rv32/reference.o rv32/link.ld
	@"$(RISCV_CC)" $(RV32_FLAGS) -DC_REFERENCE -nostdlib -nostartfiles \
		-Wl,--no-relax -Wl,-T,rv32/link.ld -Wl,-Map,build/rv32/reference.map \
		solver_rv32.S build/rv32/reference.o -o $@

build/rv32/solver-max.elf: solver_rv32.S build/rv32/tables.o rv32/link.ld
	@"$(RISCV_CC)" $(RV32_FLAGS) -DEARLY_PERM=0 -nostdlib -nostartfiles \
		-Wl,--no-relax -Wl,-T,rv32/link.ld solver_rv32.S build/rv32/tables.o -o $@

build/rv32/solver-branchless.elf: solver_rv32.S build/rv32/tables.o rv32/link.ld
	@"$(RISCV_CC)" $(RV32_FLAGS) -DBRANCHLESS_TWIST=1 -nostdlib -nostartfiles \
		-Wl,--no-relax -Wl,-T,rv32/link.ld solver_rv32.S build/rv32/tables.o -o $@

bench-twist: rv32 build/rv32/check_rv32 build/rv32/solver-branchless.elf
	@./build/rv32/check_rv32 twist "$(RIPES)" build/rv32/twist \
		build/rv32/solver.elf build/rv32/solver-branchless.elf

build/rv32/probe.elf: rv32/probe.S rv32/link.ld
	@mkdir -p build/rv32
	@"$(RISCV_CC)" $(RV32_FLAGS) -nostdlib -Wl,--no-relax -Wl,-T,rv32/link.ld $< -o $@

check-rv32: rv32 build/rv32/check_rv32 build/rv32/reference.elf
	@./build/rv32/check_rv32 smoke "$(RIPES)" build/rv32/smoke build/rv32/solver.elf
	@./build/rv32/check_rv32 smoke "$(RIPES)" build/rv32/reference-smoke build/rv32/reference.elf

check-rv32-all: rv32 build/rv32/check_rv32
	@./build/rv32/check_rv32 gates > build/rv32/gates-current.log
	@cat build/rv32/gates-current.log
	@./build/rv32/check_rv32 distance11 "$(RIPES)" build/rv32/distance11 build/rv32/solver.elf

bench-rv32: rv32 build/rv32/check_rv32 build/rv32/solver-max.elf build/rv32/reference.elf
	@./build/rv32/check_rv32 compare "$(RIPES)" build/rv32/comparison \
		build/rv32/solver-max.elf build/rv32/solver.elf build/rv32/reference.elf

probe-rv32: build/rv32/check_rv32 build/rv32/probe.elf
	@./build/rv32/check_rv32 probe "$(RIPES)" build/rv32/probe build/rv32/probe.elf

rv32-info: rv32 build/rv32/check_rv32
	@{ date -u; uname -a; "$(RISCV_CC)" --version; "$(RV32_HOST_CC)" --version; \
		printf 'RIPES=%s\nRV32_FLAGS=%s\nRV32_HOST_FLAGS=%s\n' \
			"$(RIPES)" "$(RV32_FLAGS)" "$(RV32_HOST_FLAGS)"; \
		shasum -a 256 "$(RIPES)" solver.c solver_rv32.S pdb_table.h \
			rv32/reference.c rv32/probe.S tests/check_rv32.c build/rv32/solver.elf; \
	} > build/rv32/environment.txt

# Keep simulator timing experiments sequential, even when the outer make uses -j.
verify-rv32:
	@$(MAKE) rv32-info
	@$(MAKE) check-rv32
	@$(MAKE) check-rv32-all
	@$(MAKE) bench-rv32
	@$(MAKE) bench-twist
	@$(MAKE) probe-rv32

indent:
ifeq ($(CLANG_FORMAT),)
	$(error clang-format 20 not found)
endif
	@$(CLANG_FORMAT) --version | grep -q 'version 20' || \
		{ echo "error: clang-format version 20 required"; exit 1; }
	$(CLANG_FORMAT) -i $(C_SOURCES)

clean:
	$(RM) solver mini $(CHECKER) $(GATE_CHECKER)
