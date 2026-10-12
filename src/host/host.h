/* Host platform configuration, set from the command line. */
#ifndef HOST_H
#define HOST_H

#include <stdint.h>
#include <stdio.h>

extern uint8_t host_ram[256];
extern int host_frames;
extern int host_seeded, host_noecho;
extern uint16_t host_seed;
extern FILE *host_stats;

#endif
