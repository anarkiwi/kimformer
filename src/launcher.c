/* Story chooser shared by all platforms. */
#include "zint.h"

static uint8_t is_z3(const char *n) {
  uint8_t l = 0;
  while (n[l])
    l++;
  return l > 3 && (n[l - 3] & 0x7F) == '.' &&
         ((n[l - 2] & 0x7F) | 0x20) == 'z' && (n[l - 1] & 0x7F) == '3';
}

static uint8_t scan(uint8_t pick, char *name) {
  uint8_t n = 0, i;
  if (dir_open())
    return 0;
  while (dir_next(name)) {
    if (!is_z3(name))
      continue;
    if (++n == pick)
      return n;
    if (!pick) {
      zprint_num(n);
      zputs(". ");
      for (i = 0; name[i]; i++)
        zputc(name[i] & 0x7F);
      zputc(ZNL);
    }
  }
  return pick ? 0 : n;
}

void launcher(void) {
  char name[17];
  uint8_t buf[3], len, i, k, r;
  for (;;) {
    if (!scan(0, name)) {
      zputs("No stories\n");
      return;
    }
    zputs("Story (Q quits): ");
    len = zreadline(buf, sizeof buf);
    if (len && (buf[0] | 0x20) == 'q')
      return;
    for (i = k = 0; i < len && buf[i] >= '0' && buf[i] <= '9'; i++)
      k = k * 10 + buf[i] - '0';
    if (!k || !scan(k, name))
      continue;
    r = zm_load(name);
    if (r) {
      zputs("Error ");
      zprint_num(r);
      zputc(ZNL);
    } else
      zm_run();
  }
}
