/* Minimal VT100 status line, drawn with plat_putc; provides plat_status. */
#include "vt100.h"
#include "plat.h"
#include "zm.h"

static void esc(const char *s) {
  plat_putc(27);
  while (*s)
    plat_putc(*s++);
}

void vt100_open(void) {
  esc("[2;24r");
  esc("[24;1H");
}

void vt100_close(void) {
  esc("[r");
  esc("[24;1H");
}

void plat_status(const char *left, const char *right) {
  uint8_t n = 0;
  while (right[n])
    n++;
  n = zm_width > n ? zm_width - n : 0;
  esc("7");
  esc("[1;1H");
  esc("[7m");
  for (; n; n--)
    plat_putc(*left ? *left++ : ' ');
  while (*right)
    plat_putc(*right++);
  esc("[0m");
  esc("8");
}
