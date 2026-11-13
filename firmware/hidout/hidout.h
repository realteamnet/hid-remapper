#ifndef _HIDOUT_H_
#define _HIDOUT_H_

// register holding the selected table lines: bit n = line n (0 = nothing sent)
#ifndef HIDOUT_REGISTER
#define HIDOUT_REGISTER 1
#endif

// expression (1-8) holding the table: per line <length> <report words> <targets>
#ifndef HIDOUT_TABLE
#define HIDOUT_TABLE 8
#endif

void hidout_task();

#endif
