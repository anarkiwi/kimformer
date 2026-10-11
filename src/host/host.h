/* Host platform configuration, set from the command line. */
#ifndef HOST_H
#define HOST_H

#include <stdint.h>

extern uint8_t host_ram[256];
extern int host_frames;
extern int host_seeded;
extern uint16_t host_seed;

#endif
