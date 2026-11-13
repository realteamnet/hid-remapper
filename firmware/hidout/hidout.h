#ifndef _HIDOUT_H_
#define _HIDOUT_H_

#include <stdint.h>

// register holding the selected table lines: bit n = line n (0 = nothing sent).
// Table lines: lines of only numbers in any expression, <length> <report words> <targets>.
#ifndef HIDOUT_REGISTER
#define HIDOUT_REGISTER 1
#endif

void hidout_task();
// logs what a device sends back shortly after hidout sent it a report
void hidout_report_received(uint8_t dev_addr, uint8_t instance, const uint8_t* report, uint16_t len);

#endif
