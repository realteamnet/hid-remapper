#ifndef _VDISK_H_
#define _VDISK_H_

#include <stdint.h>

#ifndef VDISK_EP_OUT
#define VDISK_EP_OUT 0x04
#endif
#ifndef VDISK_EP_IN
#define VDISK_EP_IN 0x84
#endif

// must match CONFIG_VERSION in config.cc
#define VDISK_CONFIG_VERSION 18

void vdisk_task();
// debug builds (VDISK_DEBUG): appends a line to DEBUG.TXT on the drive (the last 4 KB are kept)
void vdisk_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
const uint8_t* vdisk_configuration_descriptor(const uint8_t* base);

#endif
