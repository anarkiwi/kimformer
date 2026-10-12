/* Minimal VT100 output, drawn with plat_putc; provides plat_status. */
#include "vt100.h"
#include "plat.h"
#include "zm.h"

static void put(const char *s) {
  while (*s)
    plat_putc(*s++);
}

static void esc(const char *s) {
  plat_putc(27);
  put(s);
}

void vt_csi(const char *s) {
  esc("[");
  put(s);
}

static void num(uint8_t n) {
  uint8_t t = 0;
  for (; n >= 10; n -= 10)
    t++;
  if (t)
    num(t);
  plat_putc('0' + n);
}

/* ESC [ a ; b f. */
static void csi2(uint8_t a, uint8_t b, uint8_t f) {
  vt_csi("");
  num(a);
  plat_putc(';');
  num(b);
  plat_putc(f);
}

void vt_goto(uint8_t r, uint8_t c) { csi2(r, c, 'H'); }

void vt_region(uint8_t top) { csi2(top, 24, 'r'); }

void vt_style(uint8_t s) {
  vt_csi("0");
  if (s & 1)
    put(";7");
  if (s & 2)
    put(";1");
  if (s & 4)
    put(";4");
  plat_putc('m');
}

void vt100_open(void) {
  vt_region(2);
  vt_goto(24, 1);
}

void vt100_close(void) {
  vt_csi("r");
  vt_goto(24, 1);
}

void plat_status(const char *left, const char *right) {
  uint8_t n = 0;
  while (right[n])
    n++;
  n = zm_width > n ? zm_width - n : 0;
  esc("7");
  vt_goto(1, 1);
  vt_csi("7m");
  for (; n; n--)
    plat_putc(*left ? *left++ : ' ');
  while (*right)
    plat_putc(*right++);
  vt_csi("0m");
  esc("8");
}
