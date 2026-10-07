---
title: "From Full-State BFS to an Optimal RV32I Pocket-Cube Solver"
---

# From Full-State BFS to an Optimal RV32I Pocket-Cube Solver

[toc]

## Results and current scope

The final solver computes optimal half-turn-metric solutions for the fixed-corner 2×2×2 cube. Search executes on the simulated RV32I processor; the host generates only coordinate transitions and abstract heuristic tables.

| Requirement or measurement | Result |
| --- | --- |
| Encoded reachable states | 3,674,160 |
| Independently established HTM diameter | 11 |
| Host correctness gates H1–H4 | All passed |
| Distance-11 states executed in RV32_ISS | All 2,644 passed |
| Largest tested distance-11 instruction count | 6,862,920 |
| Instruction limit per distance-11 state | 50,000,000 |
| Required vector, ordinary run | 1,629,348 retired instructions |
| Required vector, enabled length assertion | 1,629,349 retired instructions |
| Linked assembly code, .text | 1,836 bytes |
| Total .rodata + .data + .bss | 118,436 bytes |
| Static-data limit | 131,072 bytes |
| Five-stage GUI pipeline observation | Arithmetic writeback, load-use stall/forwarding, and byte-store update captured |
| LED playback | Actual input and solved frames captured after eleven computed moves |

The solver, CLI measurements, and LED renderer are implemented. The LED net has been executed in the actual five-stage GUI and restores all six faces after the solver's eleven moves. The renderer is compiled out of every instruction-budget measurement. GUI pipeline evidence is recorded separately below.

Project: [ilian5210/minirubik](https://github.com/ilian5210/minirubik). The published [RV32I source](https://github.com/ilian5210/minirubik/blob/d3b3c60/solver_rv32.S) and [target verifier](https://github.com/ilian5210/minirubik/blob/d3b3c60/tests/check_rv32.c) are pinned to commit [d3b3c60](https://github.com/ilian5210/minirubik/commit/d3b3c60). Build and GUI instructions are in [rv32/README.md](https://github.com/ilian5210/minirubik/blob/main/rv32/README.md).

The fork baseline is upstream [sysprog21/minirubik commit 3811ad0a87bd490e45099c3cb179ec33caf46cb5](https://github.com/sysprog21/minirubik/commit/3811ad0a87bd490e45099c3cb179ec33caf46cb5), “Import from internal tree.” This revision is the parent of the fork's first PDB change, [fecee00](https://github.com/ilian5210/minirubik/commit/fecee00). The hosted BFS baseline discussed below refers to that upstream revision, rather than an unpinned current branch.

## Stage 1 — Characterizing the hosted baseline

### 1.1 The mathematical model

Fix the front-upper-left corner in both position and orientation. This removes the 24 choices of whole-cube orientation. Label the remaining physical corners 1 through 7; arrays use internal labels 0 through 6.

A state consists of a permutation of seven cubies and a twist at each of their seven positions. Internal twists belong to Z/3Z and satisfy

```text
Σ(i=0..6) oᵢ ≡ 0 (mod 3)
```

Thus there are seven factorial permutations and six independent ternary orientation digits:

```text
|G| = 7! × 3⁶ = 5,040 × 729 = 3,674,160
G = ⟨R, B, D⟩
```

For each face, the new cubie and twist at destination i are

```text
p'ᵢ = p[source_f(i)]
o'ᵢ = (o[source_f(i)] + twist_f(i)) mod 3
```

The R and B twist additions sum to six; the D additions sum to zero. Each therefore preserves the twist sum. A single twisted corner is rejected because its sum is nonzero modulo three. This is a modulo-three invariant, not parity. An odd cubie permutation is allowed: a quarter turn is a four-cycle, which is itself odd.

The input contains exactly fourteen digits: seven cubie digits in 1–7, followed by seven orientation digits in 1–3. Orientation digit d represents internal value d−1. The solved string is 12345671111111. Parsing rejects missing or extra characters, out-of-range digits, duplicate cubies, and a nonzero twist sum.

### 1.2 Cayley graph and diameter

The generators are

```text
S = {R, R², R⁻¹, B, B², B⁻¹, D, D², D⁻¹}
```

Every generator costs one move in the half-turn metric. The graph has one vertex per group element and edges from g to gs for s in S. Every move has an inverse, so BFS from solved determines the exact solution distance to every reached state.

The independent host BFS produces this distribution:

| Distance | States |
| ---: | ---: |
| 0 | 1 |
| 1 | 9 |
| 2 | 54 |
| 3 | 321 |
| 4 | 1,847 |
| 5 | 9,992 |
| 6 | 50,136 |
| 7 | 227,536 |
| 8 | 870,072 |
| 9 | 1,887,748 |
| 10 | 623,800 |
| 11 | 2,644 |
| Total | 3,674,160 |

Reaching all encoded valid states establishes connectivity and completeness. The nonempty level 11 establishes that some state needs eleven moves; exhausting the domain without a deeper level establishes that none needs more. Vertex transitivity makes the maximum distance from solved equal to the graph diameter. The resulting histogram agrees with [Jaap Scherphuis's independently published Pocket Cube distribution](https://www.jaapsch.net/puzzles/cube2.htm), which also distinguishes HTM diameter 11 from quarter-turn-metric diameter 14.

### 1.3 Why the original BFS does not fit

The baseline builds a move-toward-solved table over the entire state space. Its three dominant allocations are:

| Allocation | Calculation | Bytes |
| --- | --- | ---: |
| FIFO queue | 3,674,160 × 4 | 14,696,640 |
| Move-per-state table | 3,674,160 × 1 | 3,674,160 |
| Factored quarter-turn tables | 3 × (5,040 + 729) × 2 | 34,614 |
| Sum of these allocations | | 18,405,414 |

These are allocation sizes, not a fresh native RSS measurement of the baseline. They exclude unrelated process overhead. The baseline processes 33,067,440 edges and 66,134,880 factored transition updates. Its order-of-one-billion-instruction cost is the assignment's estimate, not an observed baseline target run.

The original report's [section 7](https://github.com/sysprog21/minirubik/blob/main/report.md#7-implementation-notes-and-possible-improvements) argues for retaining the complete table as a verification artifact. That is useful on a hosted machine, where the full graph can be checked directly. It does not fit the target: even a four-bit complete distance table needs 1,837,080 bytes, about fourteen times the 128 KiB budget. Replacing the queue with repeated scans still leaves the complete result table and extensive construction work.

The final design therefore retains exhaustive BFS as a host oracle and replaces target enumeration with bounded search using small abstractions.

### 1.4 Measuring Ripes memory and simulation rate

The pinned [VSRTL address-space implementation](https://github.com/mortbopet/VSRTL/blob/8497dd14fe80e57efcff4c424a9a3b6363d93eb7/include/VSRTL/core/vsrtl_addressspace.h) stores guest bytes in an unordered map keyed by address. A word store touches four byte entries. Consequently guest allocation size alone does not predict host RSS.

The RV32I probe performs exactly 1,048,576 word stores per run. Only the touched span changes: 4 KiB, 1 MiB, 2 MiB, or 4 MiB. Each case starts a fresh Ripes process, and each span/model pair has three repetitions. No other test simulations ran concurrently during the recorded experiment.

Peak RSS comes from wait4().ru_maxrss. It is bytes on macOS and is converted from KiB on Linux. A line with an intercept is fitted to the four median RSS values. The slope estimates incremental host bytes per guest byte; the intercept accounts for fixed process/model overhead. Retired-instruction rates use Ripes' execution time, while the raw TSV also records wall time including startup.

For N stores and W words in the span, the loop's checked instruction count is

```text
I = 5N + floor(N/W) + 13
```

| Touched guest bytes | Median RSS, RV32_ISS | Median RSS, RV32_5S |
| ---: | ---: | ---: |
| 4,096 | 73,351,168 | 120,700,928 |
| 1,048,576 | 131,547,136 | 161,087,488 |
| 2,097,152 | 191,627,264 | 203,309,056 |
| 4,194,304 | 311,640,064 | 260,505,600 |

| Derived measurement | RV32_ISS | RV32_5S |
| --- | ---: | ---: |
| Incremental host bytes per guest byte | 56.946 | 33.258 |
| Fitted intercept, bytes | 72,486,551 | 125,338,238 |
| Additional host bytes projected for 18,405,414 guest bytes | 1,048,116,495 | 612,125,031 |
| Projected total RSS, bytes | 1,120,603,046 | 737,463,269 |
| Small-region retired instructions/second | 22,506,081 | 273,305 |
| Projected time for 10^9 instructions | 44.432 seconds | 3,658.916 seconds, about 61 minutes |

The time projection uses the 4 KiB control's median rate. It is an optimistic estimate: the BFS instruction mix and working set differ. The memory projection extrapolates beyond the largest measured span and assumes similar allocation behavior; rehashing can change the slope. These build-specific results quantify the target constraint without pretending that the full baseline was executed in Ripes.

## Stage 2 — Choosing a representation and optimal search

### 2.1 Three coordinates

The search stores full permutation rank p, full orientation rank o, and partial permutation rank pp:

| Coordinate | Domain | Meaning |
| --- | ---: | --- |
| p | 0–5,039 | Lehmer rank of seven cubies |
| o | 0–728 | Base-three rank of the first six twists |
| pp | 0–209 | Ordered positions of cubies 0, 1, and 2 |

For permutation digits c_i counting smaller cubies to the right,

```text
p = Σ(i=0..6) cᵢ × (6−i)!
```

For orientations,

```text
o = Σ(i=0..5) oᵢ × 3^(5−i)
o₆ ≡ −Σ(i=0..5) oᵢ (mod 3)
```

The partial permutation has 7×6×5=210 possibilities. If its selected cubies occupy positions a, b, c, remove earlier selected positions to obtain

```text
b′ = b − [b>a]
c′ = c − [c>a] − [c>b]
pp = 30a + 5b′ + c′
```

The partial coordinate is redundant with the full permutation, but carrying it makes the PDB lookup cheap: it advances directly through its own small transition table instead of being reconstructed at every node.

### 2.2 Pattern databases and admissibility

The heuristic is

```text
h(s) = max(d_P(p), d_A(o, pp))
```

The first abstraction retains only permutation. The second retains all orientations and the selected three cubie positions, forgetting the other cubie identities. It has 729×210=153,090 abstract states. Each abstract state represents 4!=24 complete states.

For either projection, every concrete move induces an abstract move with the same unit cost. Projecting a concrete solution of length L therefore yields an abstract path of length L. The shortest abstract distance cannot exceed L, so each component is admissible. Their maximum is admissible as well. Their sum is not used because it can charge one concrete move twice.

Both tables are built by host BFS from the abstract solved state. The maximum permutation distance is 7 and the maximum PDB distance is 9. Host gate H1 independently checks h(s) ≤ d(s) for every complete state.

### 2.3 Iterative deepening and safe pruning

The target uses a nonrecursive IDA* search. It begins at h(root), searches one bound completely, and increments the bound through 11. At depth g, it prunes only when h(s) exceeds bound−g.

On a shortest path of length L, each prefix satisfies g+h(s) ≤ L. Admissible pruning therefore preserves at least one shortest path at bound L. Bounds below L cannot contain a solution. The first successful bound is consequently optimal.

The search also skips the face used by the incoming move. Two consecutive moves of one face either cancel or compose to one nonidentity turn, so such a pair cannot be needed in a shortest solution. The first node has nine possible moves; subsequent nodes have six after this pruning.

Every bound has a finite tree, and the independently established diameter guarantees a solution by bound 11 for every valid state. Twelve fixed frames cover depths 0 through 11. The target needs neither recursion nor a heap.

### 2.4 Quantifying the final working set

| Read-only table | Entries | Bytes |
| --- | --- | ---: |
| perm_q | 3 × 5,040 unsigned halfwords | 30,240 |
| ori_q | 3 × 729 unsigned halfwords | 4,374 |
| pp_q | 3 × 210 bytes | 630 |
| perm_dist | 5,040 bytes | 5,040 |
| Packed PDB | 153,090 nibbles | 76,545 |
| Total table contents | | 116,829 |

| Allocated ELF section | Bytes |
| --- | ---: |
| .text | 1,836 |
| .rodata | 117,092 |
| .data | 0 |
| .bss | 1,344 |
| Static-data total | 118,436 |
| Remaining static-data budget | 12,636 |

The data total includes all descriptors and strings, replay buffers, the 768-byte search-frame area, and a 512-byte reserved stack. The assembly search uses an 80-byte call frame. Its routines use the same stack area sequentially, and the search makes no calls. Reserved stack memory is counted in .bss rather than silently excluded.

The linker asserts that .rodata+.data+.bss stays below 131,072 bytes. Alignment inside sections counts; the address gap between code and data is not an allocated data section. There is no target heap or full-state BFS queue. Exact-distance arrays exist only in the native checker.

## Stage 3 — Improving the C algorithm before translation

The final [C solver](https://github.com/ilian5210/minirubik/blob/a28d4fc6bd92da6a79c1829b2199209074b09c27/solver.c) already contains the target-oriented search design. [gen_tables.c](https://github.com/ilian5210/minirubik/blob/a28d4fc6bd92da6a79c1829b2199209074b09c27/gen_tables.c) generates its abstract distances and transitions in C on the host.

Three choices remove work from each target node. First, permutation and orientation evolve independently, so transitions operate on coordinates instead of constructing fourteen cubie bytes and re-ranking them. Second, one quarter-turn row per face generates all three turns by repeated lookup, avoiding nine separate move rows. Third, an explicit search structure bounds storage and removes recursive call overhead.

Constant-factor ranking arithmetic uses shifts and additions. Lehmer Horner factors are 6, 5, 4, 3, and 2; orientation accumulation multiplies by three; partial ranking uses 30a+5b'+c'. No variable multiply or division is needed.

The PDB stores two distances per byte. For logical index i=210o+pp, even i is the low nibble and odd i the high nibble. Since each row has 210 entries, its byte offset is

```text
byte offset = 105o + floor(pp/2)
105 = 64 + 32 + 8 + 1
```

Packing saves 76,545 bytes while adding a nibble selection to each lookup. H4 verifies both nibble positions against an unpacked oracle. All PDB entries are at most nine, so four bits suffice; an unfilled 0xff entry cannot be accepted as an ordinary generated distance.

Twist arithmetic also avoids division. Adding two values in 0..2 gives at most four; one conditional subtraction of three produces the correct reduced result.

The port only adds compilation guards to solver.c for its freestanding reference build. It retains the C search and generated table contents. The subsequent assembly early-rejection optimization is measured separately rather than presented as an earlier C change.

## Stage 4 — RV32I translation and measurement

### 4.1 Linking the original tables

The normal build has a short dependency chain:

```text
pdb_table.h -> tables.c -> tables.o --+
                                    |
solver_rv32.S -> RV32I assembler -----+--> solver.elf --> Ripes
                                    |
link.ld -----------------------------+
```

The table wrapper includes the original C header and exports const address descriptors. It produces data rather than search code. The assembler references those addresses, and the linker resolves them. The .S suffix enables preprocessing; it does not make C array declarations assembly syntax.

Once tables.o exists, changing only the inline input rebuilds the small assembly program and reuses the table object. No Python or table-conversion script is required.

### 4.2 Instructions that matter on RV32I

RV32I does not provide the M extension's multiplication and division. The packed-row calculation uses shifts/adds to form 105o. Nibble selection then uses a byte load, a shift of either zero or four, and a mask of fifteen. Unsigned halfword loads read the permutation/orientation transitions without sign extension.

Permutation ranking loads the seven cubies once and performs 21 unsigned comparisons. The assembly search keeps the current p/o/pp in s9/s10/s11. It uses s0 as a 64-byte frame cursor, s1 as remaining depth, s2 as bound, and s6 as the path cursor. Advancing the frame by 64 replaces repeated indexing across C arrays and entered-node flags.

The key early-rejection fragment is:

```asm
add  t0, s4, s9
lbu  t2, 0(t0)
bltu s1, t2, search_backtrack
```

Here s4 points to permutation distances. If this component already exceeds remaining depth, the packed PDB calculation can be skipped. The condition is exact because max(a,b)>r if and only if a>r or b>r. Search order and optimality remain unchanged.

All executable words are checked against the emitted RV32I subset. The build uses -march=rv32i -mabi=ilp32 -mno-relax and links with -nostdlib/-nostartfiles. The linked programs have no unresolved arithmetic-helper or runtime-library symbols.

Console characters use a7=11, diagnostic strings use a7=4, and exit uses a7=93 with status in a0, following [Ripes' environment-call interface](https://github.com/mortbopet/Ripes/blob/5b8a616edcb6f0a2ddb07e78951348b72497f1e1/docs/ecalls.md). The program records status in memory before exiting. It is not a native macOS executable.

### 4.3 Fair compiler comparison

The GCC reference compiles the full final C algorithm: parsing, validation, ranking, search, and answer formatting. Only the hosted argv/stdio interface is adapted to inline input and Ripes ecalls. Startup, exit, and independent cubie replay are shared assembly support. This measures the complete freestanding algorithm, not the hosted libc CLI.

GCC uses -O2 -ffreestanding with the same RV32I ABI and disabled relaxation. Its stack-usage report gives one static 272-byte frame, fitting the 512-byte reserve. The Makefile checks that report.

| Input or size | Assembly maximum variant | Assembly early rejection | Full C, GCC -O2 |
| --- | ---: | ---: | ---: |
| 12345671111111 | 555 | 555 | 726 |
| 25314672313211 | 1,430 | 1,400 | 1,747 |
| 21345671111111 | 1,825,539 | 1,629,349 | 2,914,480 |
| 12347651111111 | 7,867,986 | 6,862,920 | 12,553,151 |
| Linked .text bytes | 1,840 | 1,836 | 2,320 |
| Static-data bytes | 118,436 | 118,436 | 117,608 |

Instruction counts are `RV32_ISS --iret` with replay and the enabled expected-length assertion. In the hardest distance-11 input, early rejection saves 12.77% relative to the assembly maximum variant. The C version still evaluates both components, so comparison with the default assembly includes that optimization as well as translation choices.

The two assembly builds are retained experimental variants. They are not claimed to be separate historical commits. Wall time is kept in the raw results but retired instructions determine the assignment budget.

### 4.4 Measuring conditional versus branchless modulo three

The independent replay adds two twists in 0..2, so the sum is at most four. The default subtracts three, branches on a nonnegative result, and adds three back only for a negative result. It costs two or three instructions after the addition. The retained `BRANCHLESS_TWIST=1` experiment replaces that reduction with:

```asm
addi t2, t2, -3
srai t4, t2, 31
andi t4, t4, 3
add  t2, t2, t4
```

The sign mask restores three exactly when the subtraction went below zero. The four instructions use only RV32I. `make bench-twist` builds both complete renderer-free programs and measures them on both ISS and the five-stage model with the same inputs, length assertions, and replay checks. All twelve runs pass. October 7 measurements:

| Input | ISS iret, conditional | ISS iret, branchless | Five-stage cycles, conditional | Five-stage cycles, branchless |
| --- | ---: | ---: | ---: | ---: |
| 12345671111111 | 555 | 555 | 725 | 725 |
| 25314672313211 | 1,400 | 1,429 | 1,787 | 1,800 |
| 21345671111111 | 1,629,349 | 1,629,530 | 1,997,307 | 1,997,420 |

The linked text grows from 1,836 to 1,840 bytes; static data stays 118,436. Branchless reduction does not win on these complete programs, even in pipeline cycles. Eliminating a branch does not guarantee a gain: the extra ALU instructions exceed the eliminated control-flow cost here. The search uses orientation transition lookups, so this reduction runs in the short replay rather than every search node, explaining the small whole-query effect. The default keeps the conditional version. Raw retired instructions, cycles, and single-run execution times are retained in `build/rv32/twist/twist.tsv`; the times are not a statistical speed comparison.

## Correctness and exhaustive target verification

### Host gates H1–H4

The [independent native oracle](https://github.com/ilian5210/minirubik/blob/a28d4fc6bd92da6a79c1829b2199209074b09c27/tests/check_gates.c) rebuilds transitions from cubie source/twist rules rather than trusting the generated solver tables.

| Gate | What was verified | Recorded outcome |
| --- | --- | --- |
| H1 | h(s) ≤ exact BFS distance for every complete state | 3,674,160 passed |
| H2 | Complete population, reference agreement, maxima and solved entries | All five tables passed |
| H3 | Actual C search returns exact optimal length; independent replay solves the cube | 3,674,160 passed, 20 seconds |
| H4 | Packed access agrees with unpacked PDB for even and odd indices | 76,545 low and 76,545 high entries passed |

Transition maxima are p=5,039, o=728, and pp=209. Distance maxima are seven for permutation and nine for the PDB. The combined heuristic reaches nine and equals the exact distance on 417,367 states. H3's largest search-node count is 114,384 at 12347651111111.

H1 is checked separately from H3: correct search outputs alone do not establish that every heuristic value is a lower bound.

### Target gates T5–T7

Every successful target query replays its returned path using separate cubie-level source/twist rules, then checks the solved permutation and all seven twists before printing. The native checker parses the printed moves, verifies exact BFS length, and replays them independently as well.

Both assembly and full C pass 42 valid inputs and ten invalid inputs on ISS. The valid inputs include 32 deterministic pseudorandom states seeded with 20261006. Twelve cases per implementation also pass the five-stage model, including solved, one-move, required eleven-move, and worst distance-11 inputs. A deliberately wrong expected length produces failure status on the target.

For the required input 21345671111111, the actual optimal path is:

```text
R B' D2 R' B R' B' R D2 R B
```

Its eleven moves solve the cubie state. Another valid eleven-move path would also be optimal; correctness is checked by length and replay rather than assuming the baseline's path must be reproduced byte-for-byte.

### All 2,644 distance-11 states

The target checker enumerates the distance-11 set identified by independent BFS. For each case it copies the linked ELF and patches only named input-state and expected-length data. Instructions and solver tables remain identical; the host supplies no solution path or complete-state table to target search.

| Statistic | RV32_ISS instructions |
| --- | ---: |
| Minimum, state 73516243323113 | 318,832 |
| Mean | 912,810.60 |
| Maximum, state 12347651111111 | 6,862,920 |
| Required vector 21345671111111 | 1,629,349 |
| Per-state ceiling | 50,000,000 |

All 2,644 results have optimal length eleven and pass replay on target and host. Enumeration takes 420.364 seconds after host-oracle preparation. The largest count is 13.73% of the ceiling. This certifies the complete distance-11 target set; exhaustive execution of every possible RV32 input was not performed.

Normal builds set expected length to −1 and skip the extra comparison. Enabling it adds one retired assembly instruction, explaining the ordinary required-vector count of 1,629,348 versus the asserted count of 1,629,349. For these solver programs, the pinned five-stage model reports one fewer retired instruction at termination than ISS; budget checks use ISS as required.

Missing telemetry, abnormal termination, wrong target status, a wrong length, a replay failure, or a budget violation fails the checker. Simulator stderr is recorded separately from printed moves because macOS can emit process-policy warnings there.

## Instruction-level pipeline analysis

On October 7, the renderer-free solver ELF was loaded into the actual five-stage GUI with forwarding and hazard detection, M/C disabled, the **Extended** layout, and **View → Show processor signal values** enabled. The input was `21345671111111`. The following captures show startup and parsing instructions; the search-loop example discussed afterward is a separate source-based explanation.

The addresses below belong to this linked CLI ELF: `input_state = 0x10000`, `cube = 0x2cc70`, and `stack_end = 0x2ceb0`. They are inspection results rather than constants required by the solver. Rebuilding with LED support can move these symbols.

| Instruction and PC | IF | ID | EX | MEM | WB |
| --- | ---: | ---: | ---: | ---: | ---: |
| `addi sp,sp,-336`, 0x004 | 1 | 2 | 3 | 4 | 5 |
| `lbu t3,0(a0)`, 0x194 | 13 | 14 | 15 | 16 | 17 |
| `addi t3,t3,-49`, 0x198 | 14 | 15–16 | 17 | 18 | 19 |
| `sb t3,0(a1)`, 0x1b4 | 22 | 23 | 24 | 25 | 26 |

These cycle numbers describe the stage occupancy before the next clock edge. A write enabled in WB or MEM takes effect on that edge. The selected cycles below capture the decisive control and data signals.

### Arithmetic writeback

The preceding `auipc sp,0x2d` establishes `0x2d000` in x2. The startup instruction at 0x004 adjusts it by −336, giving `0x2ceb0`. IF fetches the instruction; ID reads x2 and decodes the immediate; EX adds the register operand and sign-extended immediate; MEM carries the result without a memory write; WB selects the ALU result and writes x2.

At cycle 5, the instruction is in WB. The writeback mux selects **ALURES=1**, its result is `0x2ceb0`, and the register file has **wr_en=1**, **wr_addr=2**. The register panel still shows the previous stack value at this instant because the enabled write commits on the following clock edge. The later cycle-26 capture shows x2 holding `0x2ceb0`.

![Cycle 5: arithmetic instruction in WB, ALU-result writeback enabled](https://hackmd.io/_uploads/HybkCA7oMx.jpg)

### Load-use stall and forwarding

The first input character is ASCII `'2'`, or `0x32`. The parser reads it with `lbu t3,0(a0)` and converts it to internal cubie index one with `addi t3,t3,-49`. IF and ID identify the instruction and base register; the load's EX stage calculates `a0+0`; MEM reads and zero-extends one byte; WB selects the memory result and enables writing x28 (`t3`).

At cycle 15, the load is in EX and its dependent `addi` is in ID. **hazardFEEnable=0** holds the PC and IF/ID, while **hazardIDEXClear=1** inserts a bubble. At cycle 16 the consumer remains in ID and the bubble occupies EX. This prevents it from computing with the old value of `t3`.

![Cycle 15: load in EX, dependent addi in ID, front end held](https://hackmd.io/_uploads/rJx41kNjfg.jpg)

At cycle 17, the load is in WB and the consumer is in EX. The writeback mux selects **MEMREAD=0**, with **data=0x32**, **wr_en=1**, and **wr_addr=28**. The EX operand-forwarding mux selects **WbStage=2**, supplying the newly loaded value directly to the ALU. The immediate operand is −49 and the EX result is **1**. The stall followed by forwarding therefore preserves the intended ASCII-to-index conversion.

![Cycle 17: memory-result writeback and WB-to-EX forwarding produce index one](https://hackmd.io/_uploads/HJ_oe1Vsfx.jpg)

### Byte store and the observed memory update

After the range and duplicate checks, `sb t3,0(a1)` stores that index in the first permutation byte. IF fetches it, ID reads the destination base and source data, EX adds base plus zero, MEM enables a one-byte write, and WB performs no register write.

At cycle 25, this `sb` is in MEM. Data memory shows **wr_en=1**, **addr=0x2cc70**, and **data_in=1**. The Memory table at the same address still shows word `0x00000000` and Byte 0 `0x00`, before the write edge. The register-file write enable is also one in this screenshot, but belongs to the older `or` instruction in WB; each stage's control must be associated with its own instruction.

![Cycle 25: sb enables a one-byte write while Memory still contains zero](https://hackmd.io/_uploads/rJ8hyJ4ozx.jpg)

After one Clock, cycle 26 has the `sb` in WB with **register wr_en=0**. Reopening **Memory → Go to section → Address... → 0x2cc70** refreshes the table: the word is now **0x00000001**, Byte 0 is **0x01**, and Bytes 1–3 remain **0x00**. The table initially retained the old display after stepping, so the before/after evidence uses the explicitly refreshed address view. No additional clock was taken during this refresh.

![Cycle 26: Memory shows the stored byte 0x01 and the store has no register writeback](https://hackmd.io/_uploads/r1LVW14ife.jpg)

This is the expected representation of the first input cubie: digit two becomes internal index one. The decoded SB control specifies a one-byte write, and the refreshed table agrees with that update. The independently tested parser and final cubie replay provide broader correctness checks beyond this single observed update.

The [processor wiring](https://github.com/mortbopet/Ripes/blob/5b8a616edcb6f0a2ddb07e78951348b72497f1e1/src/processors/RISC-V/rv5s/rv5s.h), [control definitions](https://github.com/mortbopet/Ripes/blob/5b8a616edcb6f0a2ddb07e78951348b72497f1e1/src/processors/RISC-V/rv_control.h), [mux enums](https://github.com/mortbopet/Ripes/blob/5b8a616edcb6f0a2ddb07e78951348b72497f1e1/src/processors/RISC-V/riscv.h), and [hazard unit](https://github.com/mortbopet/Ripes/blob/5b8a616edcb6f0a2ddb07e78951348b72497f1e1/src/processors/RISC-V/rv5s/rv5s_hazardunit.h) explain the observed selectors and enables. In particular, `memwb_reg.reg_do_write_out` controls register writeback, while `exmem_reg.mem_do_write_out` controls the memory write. They apply to different instructions in the same cycle.

In the search loop, the separate pair `lhu s9,0(t0)` / `sw s9,20(s0)` follows the same load-use hazard mechanism: the load obtains a new permutation coordinate from a transition row and the store saves it in the parent frame. Later, `sb t1,0(s6)` writes a solution move byte in 0..8. Frame advancement and backtracking preserve search coordinates; independent target replay checks that the resulting path solves the cube. These search operations are explained from the assembly and processor source, rather than being mislabeled as the startup/parser screenshots above.

## LED renderer and live solution playback

The GUI build instantiates LED Matrix 0 at Width=35 and Height=25. The renderer uses the exported `LED_MATRIX_0_BASE`, `LED_MATRIX_0_WIDTH`, and `LED_MATRIX_0_HEIGHT` symbols. Ripes writes these definitions into its temporary `ripes_system.h`; `make rv32-gui` includes and caches this header while linking the same original table object. The address is obtained from the actual peripheral configuration rather than a literal in the source.

Each LED receives one 32-bit RGB24 word. The address is `BASE + 4×(y×WIDTH+x)`, using row-major indexing. The in-GUI description shows a column-major expression, but the pinned [LED implementation](https://github.com/mortbopet/Ripes/blob/5b8a616edcb6f0a2ddb07e78951348b72497f1e1/src/io/ioledmatrix.cpp#L99) actually indexes y×width+x. Repeated addition forms the row offset on RV32I without multiplication.

One facelet occupies 4×3 pixels. A face occupies 8×6 pixels. The four middle faces use origins x=0,9,18,27 and y=7; U sits at (9,0), and D at (9,14). Three separator columns and two separator rows produce a 35×20 bounding net, with five unused rows at the bottom. The renderer clears the full peripheral to black and fills only the 24 facelets, so gaps and spare space remain black.

```text
       U
    L  F  R  B
       D
```

| Face | RGB24 | Pixel origin |
| --- | --- | --- |
| U | FFFFFF, white | (9,0) |
| L | FF8000, orange | (0,7) |
| F | 00FF00, green | (9,7) |
| R | FF0000, red | (18,7) |
| B | 0000FF, blue | (27,7) |
| D | FFFF00, yellow | (9,14) |

Physical cubies 0 through 7 carry ordered face triples `(U,F,L)`, `(U,R,F)`, `(D,F,R)`, `(D,L,F)`, `(U,B,R)`, `(D,R,B)`, `(D,B,L)`, `(U,L,B)`. A descriptor identifies a facelet's position corner and face slot. For a moving corner, the permutation selects its cubie and the orientation selects `(slot+orientation) mod 3` in that cubie's triple. The fixed corner 0 always uses cubie 0 and twist 0. Adding the orientation, rather than subtracting it, agrees with the source/twist move convention used by the solver.

The display preserves the input in a separate 14-byte cubie buffer, because the independent target replay check changes the verification cube to solved. It draws the input before search. After each printed move, it reads that exact byte from the computed solution buffer, applies one, two, or three quarter turns to the display cube, and redraws. A configurable integer delay allows each frame to be seen. The animation is produced by the search result; there is no prerecorded move sequence.

`make check-led` reads the three LED tables from the actual GUI ELF and compares them with an independent geometric oracle. With x right, y up, and z front, quarter turns are R:(x,z,-y), B:(-y,x,z), and D:(z,y,-x). All **1,512** sticker rotation cases pass, as do the nine HTM move nets, fixed-corner preservation, four stickers per color, six distinct RGB24 values, and move/inverse recovery. Deliberately corrupting the cubie table, net coordinates, or RGB24 palette produces a failure. This checks the linked data instead of validating a second copy of it.

The GUI ELF has **2,368 bytes of .text** and **118,596 bytes of static data**, still below 131,072. The CLI ELF remains **1,836 / 118,436 bytes**. On October 7, the CLI executable instruction bytes were compared with the ELF used for the complete distance-11 run and matched exactly; the assembly and full-C regression suites were also rerun successfully. The compile-time `RENDER=0/1` switch changes only display support and its calls. The CLI instantiates no LED peripheral, so no GUI rendering cost is included in the budget or compiler comparison.

The actual five-stage GUI was run with `21345671111111`, M/C disabled. Its initial and solved LED frames are shown below. On this macOS build the embedded I/O view sometimes retained an old painting; floating the LED window forced repainting and allowed live updates. The reproduction instructions therefore use the floating window.

| Input: 21345671111111 | After the eleven solution moves |
| --- | --- |
| <img alt="Actual input state on the Ripes LED peripheral" src="https://raw.githubusercontent.com/ilian5210/minirubik/main/rv32/images/led-input.png" width="300"> | <img alt="Actual solved state on the Ripes LED peripheral" src="https://raw.githubusercontent.com/ilian5210/minirubik/main/rv32/images/led-solved.png" width="300"> |

## Reproduction and development evidence

### Build and verification commands

```sh
make run-rv32
make run-rv32 RV32_STATE=25314672313211
make run-rv32 RV32_STATE=21345671111111 RV32_PROC=RV32_5S
make check-rv32
make check-rv32-all
make bench-rv32
make probe-rv32
make verify-rv32
make rv32-gui
make check-led
make bench-twist
```

Use make rv32 to build without running. The ELF is build/rv32/solver.elf. The verification suite invokes the actual Ripes executable; it does not implement a second RV32 emulator. Only verification needs a native C compiler.

| File | Purpose |
| --- | --- |
| solver.c | Final native C search algorithm |
| gen_tables.c and pdb_table.h | Host C generation and original linked table contents |
| [solver_rv32.S](https://github.com/ilian5210/minirubik/blob/d3b3c60/solver_rv32.S) | RV32I parsing, ranking, search, replay, output, and LED animation |
| rv32/tables.c and rv32/link.ld | Table-object exports and ELF layout/budget assertion |
| rv32/reference.c | Full freestanding C comparison |
| rv32/probe.S | Memory/rate measurement loop |
| tests/check_gates.c | Independent BFS and host gates |
| [tests/check_rv32.c](https://github.com/ilian5210/minirubik/blob/d3b3c60/tests/check_rv32.c) | Target execution, replay, all distance-11 cases, LED geometry, and measurements |

Generated logs stay under build/rv32/: gates-current.log, distance11/distance11.tsv and distance11-summary.txt, smoke and reference-smoke, comparison/comparison.tsv, probe/probe.tsv and probe-summary.txt, and environment.txt. They include raw telemetry and stderr rather than only the aggregate figures in this note.

### Pinned environment

The exhaustive distance-11 run, compiler comparison, and simulator probes were recorded on October 6, 2026. Modulo-three measurements, LED verification, regression checks, and GUI pipeline captures followed on October 7 on the same Apple M3 with 16 GiB RAM, Darwin 27.0.0 arm64. The cross compiler is riscv64-elf-gcc 16.2.0; the native checker uses Apple Clang 21.0.0 with its matching Command Line Tools SDK.

Ripes build: v2.2.6-106-g5b8a616-mac-universal2. Source revision: 5b8a616edcb6f0a2ddb07e78951348b72497f1e1. VSRTL revision: 8497dd14fe80e57efcff4c424a9a3b6363d93eb7.

Ripes executable SHA-256:

```text
bea887fcf020c1dda1f44177c19c27a13f3b93c625b17194ac37e3d421a34fc4
```

The generated table-header SHA-256 is:

```text
22ba7470f29c12ead43ad4acb3a341a1ccbbed0595687ef581ffb53b84bd5973
```

make rv32-info records flags, compiler versions, source hashes, and ELF hashes for reproduction.

### Actual development record

| Recorded date, Asia/Taipei | Repository evidence |
| --- | --- |
| 2026-09-30 15:55 | [fecee00](https://github.com/ilian5210/minirubik/commit/fecee00): PDB generator and precomputed table |
| 2026-09-30 16:14 | [56894a5](https://github.com/ilian5210/minirubik/commit/56894a5): heuristic lookup and replacement of full BFS with IDA* |
| 2026-10-01 17:22 | [82ba9df](https://github.com/ilian5210/minirubik/commit/82ba9df): executable-check correction |
| 2026-10-03 14:39 | [a28d4fc](https://github.com/ilian5210/minirubik/commit/a28d4fc): H1–H4 verification |
| 2026-10-06 | Local RV32 port, exhaustive distance-11 run, full C comparison, and simulator probes |
| 2026-10-07 | [d3b3c60](https://github.com/ilian5210/minirubik/commit/d3b3c60): publish RV32 solver, live LED renderer, target verifier, and measured modulo-three variants |
| 2026-10-07 | Actual GUI LED frames and pipeline captures added to this report, including the refreshed byte-store before/after observation |

This note consolidates work that was developed and tested locally. The repository commits and raw measurement artifacts provide the development evidence; this table does not imply that historical HackMD revisions already existed. The RV32 sources are now published, and the note is updated as LED and pipeline evidence becomes available rather than assigning artificial dates to earlier revisions.
