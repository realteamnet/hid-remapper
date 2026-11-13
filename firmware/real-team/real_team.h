#ifndef _REAL_TEAM_H_
#define _REAL_TEAM_H_

#include <stdint.h>

// main loop, once per iteration
void real_team_task();
// every input report from a device on the host port
void real_team_report_received(uint8_t dev_addr, uint8_t instance, const uint8_t* report, uint16_t len);
// the configuration descriptor to send to the computer
const uint8_t* real_team_configuration_descriptor(const uint8_t* base);

#endif
