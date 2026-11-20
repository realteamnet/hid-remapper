#ifndef _CMD_TABLE_REMAPPER_H_
#define _CMD_TABLE_REMAPPER_H_

// hid-remapper side of the command table (cmd_table.h): reads the table lines from the
// config's expressions and passes them on when they changed.

// checks 4 times a second; force = read now
void cmd_table_remapper_read(bool force);

#endif
