/* Host front end: zm [-w width] [-p pages] [-r lo-hi] [-s seed] [-d dir] [-t]
 * [story] */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../zint.h"
#include "host.h"

static int usage(void) {
  fputs("usage: zm [-w width] [-p pages] [-r lo-hi] [-s seed] [-d dir] [-t] "
        "[story]\n",
        stderr);
  return 64;
}

int main(int argc, char **argv) {
  int c, w, ranges = 0;
  unsigned lo, hi;
  const char *dir = ".";
  uint8_t r;
  while ((c = getopt(argc, argv, "w:p:r:s:d:t")) != -1) {
    switch (c) {
    case 'w':
      w = atoi(optarg);
      if (w < 1 || w > 255)
        return usage();
      zm_width = w;
      break;
    case 'p':
      host_frames = atoi(optarg);
      break;
    case 'r':
      if (sscanf(optarg, "%x-%x", &lo, &hi) != 2 || lo > hi || hi > 0xFF)
        return usage();
      for (ranges = 1; lo <= hi; lo++)
        host_ram[lo] = 1;
      break;
    case 's':
      host_seeded = 1;
      host_seed = atoi(optarg);
      break;
    case 'd':
      dir = optarg;
      break;
    case 't':
#ifdef ZM_TRACE
      zm_trace = 1;
#endif
      break;
    default:
      return usage();
    }
  }
  if (!ranges)
    for (lo = 0x20; lo < 0xFF; lo++)
      host_ram[lo] = 1;
  if (chdir(dir)) {
    perror(dir);
    return 66;
  }
  if (optind < argc) {
    r = zm_load(argv[optind]);
    if (r) {
      printf("Error %u\n", r);
      return r;
    }
    return zm_run();
  }
  launcher();
  return 0;
}
