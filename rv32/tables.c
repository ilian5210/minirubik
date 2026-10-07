/* Compile the original C tables and export their addresses to assembly. */
#include "../pdb_table.h"

const void *const face_tables[3][4] = {
    {perm_q[0], ori_q[0], pp_q[0], 0},
    {perm_q[1], ori_q[1], pp_q[1], 0},
    {perm_q[2], ori_q[2], pp_q[2], 0},
};

const uint8_t *const heuristic_tables[2] = {perm_dist, pdb_table};
