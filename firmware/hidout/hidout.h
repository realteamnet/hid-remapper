#ifndef _HIDOUT_H_
#define _HIDOUT_H_

// HIDOUT_REGISTER, +1: report bytes 0-7, byte 0 = report ID (0 = idle)
// next HIDOUT_TARGETS registers: 0xVVVV (any product of that vendor) or 0xVVVVPPPP; all 0 = any device
#ifndef HIDOUT_REGISTER
#define HIDOUT_REGISTER 1
#endif

#ifndef HIDOUT_TARGETS
#define HIDOUT_TARGETS 3
#endif

void hidout_task();

#endif
