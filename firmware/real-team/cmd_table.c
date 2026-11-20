#include "cmd_table.h"

typedef struct {
    cmd_begin_fn begin;
    cmd_line_fn line;
    void* ctx;
} handler_t;

static handler_t handlers[CMD_TABLE_MAX_HANDLERS];
static size_t n_handlers = 0;

bool cmd_table_add_handler(cmd_begin_fn begin, cmd_line_fn line, void* ctx) {
    if ((n_handlers >= CMD_TABLE_MAX_HANDLERS) || (line == NULL)) {
        return false;
    }
    handlers[n_handlers].begin = begin;
    handlers[n_handlers].line = line;
    handlers[n_handlers].ctx = ctx;
    n_handlers++;
    return true;
}

void cmd_table_begin(void) {
    for (size_t i = 0; i < n_handlers; i++) {
        if (handlers[i].begin != NULL) {
            handlers[i].begin(handlers[i].ctx);
        }
    }
}

void cmd_table_line(const cmd_value_t* vals, size_t n) {
    if (n == 0) {
        return;
    }
    for (size_t i = 0; i < n_handlers; i++) {
        if (handlers[i].line(vals, n, handlers[i].ctx)) {
            return;
        }
    }
}

uint32_t cmd_table_hash(uint32_t hash, const cmd_value_t* vals, size_t n) {
    for (size_t i = 0; i < n; i++) {
        hash = (hash ^ (uint32_t) vals[i].v) * 16777619u;
        hash = (hash ^ (vals[i].hex ? 0x48u : 0x44u)) * 16777619u;
    }
    return (hash ^ 0xEEu) * 16777619u;
}
