#include "cmd_table_remapper.h"

#include <vector>

#include "cmd_table.h"
#include "globals.h"
#include "platform.h"

#define TABLE_CHECK_US 250000

// Every expression line (ended by eol) that holds only numbers is a table line;
// PUSH = decimal (x1000), PUSH_USAGE = hex.
void cmd_table_remapper_read(bool force) {
    static uint64_t next_check = 0;
    static uint32_t prev_hash = 0;
    static bool loaded = false;
    uint64_t now = get_time();
    if (!force && (now < next_check)) {
        return;
    }
    next_check = now + TABLE_CHECK_US;

    std::vector<std::vector<cmd_value_t>> lines;
    std::vector<cmd_value_t> line;
    uint32_t hash = CMD_TABLE_HASH_INIT;
    my_mutex_enter(MutexId::EXPRESSIONS);
    for (auto const& expr : expressions) {
        bool numbers_only = true;
        line.clear();
        for (size_t i = 0; i <= expr.size(); i++) {
            if ((i < expr.size()) && (expr[i].op != Op::EOL)) {
                if ((expr[i].op == Op::PUSH) || (expr[i].op == Op::PUSH_USAGE)) {
                    line.push_back({ (int32_t) expr[i].val, expr[i].op == Op::PUSH_USAGE });
                } else {
                    numbers_only = false;
                }
                continue;
            }
            if (numbers_only && !line.empty()) {
                hash = cmd_table_hash(hash, line.data(), line.size());
                lines.push_back(line);
            }
            numbers_only = true;
            line.clear();
        }
    }
    my_mutex_exit(MutexId::EXPRESSIONS);

    if (loaded && (hash == prev_hash)) {
        return;
    }
    loaded = true;
    prev_hash = hash;
    cmd_table_begin();
    for (auto const& l : lines) {
        cmd_table_line(l.data(), l.size());
    }
}
