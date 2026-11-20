#ifndef _CMD_TABLE_H_
#define _CMD_TABLE_H_

// Command table: lines of numbers, each line handed to the first handler that claims it.
// Plain C99 with no other dependencies, so other code bases can use the same format.
// Format and rules: README.md, "Command table".

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CMD_TABLE_MAX_HANDLERS
#define CMD_TABLE_MAX_HANDLERS 8
#endif

#define CMD_TABLE_HASH_INIT 2166136261u

// decimal values are fixed point x1000 (0.25 = 250); hex values are raw 32-bit words
typedef struct {
    int32_t v;
    bool hex;
} cmd_value_t;

// called once before the lines of a new table
typedef void (*cmd_begin_fn)(void* ctx);
// called for each line until one returns true (claimed)
typedef bool (*cmd_line_fn)(const cmd_value_t* vals, size_t n, void* ctx);

bool cmd_table_add_handler(cmd_begin_fn begin, cmd_line_fn line, void* ctx);
void cmd_table_begin(void);
void cmd_table_line(const cmd_value_t* vals, size_t n);
uint32_t cmd_table_hash(uint32_t hash, const cmd_value_t* vals, size_t n);

#ifdef __cplusplus
}
#endif

#endif
