/* KIM-1 platform: monitor TTY console, RAM probe, story and save files on a
 * CBM DOS drive (sd2iec) through the KIMDOS C library. */
#include <stdlib.h>
#include <string.h>

#include "../plat.h"
#include "../vt100.h"
#include "../zm.h"
#include "kimdos.h"

#define CH_SAVE 1
#define CH_STORY 2
#define CH_RESTORE 3
#define RIOT_TIMER (*(volatile uint8_t *)0x1706)
#define LOW_FIRST 0x04
#define LOW_END 0x14

extern char __kim_end[];

static uint16_t seed, st_page;
static uint8_t sv_ch, sv_err;
static char line[48];
static kd_dirent de;

void plat_putc(uint8_t c) {
  if (c == '\n')
    kd_putc('\r');
  kd_putc(c);
}

static void puts_nl(const char *s) {
  while (*s)
    plat_putc(*s++ & 0x7F);
  plat_putc('\n');
}

uint8_t plat_getc(void) {
  uint8_t c = kd_getc();
  seed = (seed << 3 | seed >> 13) ^ RIOT_TIMER;
  if (c == '\r')
    kd_putc('\n');
  return c;
}

uint16_t plat_entropy(void) { return seed ^ (uint16_t)RIOT_TIMER << 8; }

static uint8_t is_ram(uint8_t hi) {
  volatile uint8_t *p = PAGE_PTR(hi);
  uint8_t old = *p, ok;
  *p = 0x55;
  ok = *p == 0x55;
  *p = 0xAA;
  ok &= *p == 0xAA;
  *p = old;
  return ok;
}

uint8_t plat_pages(uint8_t *hi, uint8_t max) {
  uint8_t n = 0, h = LOW_FIRST;
  for (; n < max && h != 0xFF; h++) {
    if (h == LOW_END)
      h = (uint16_t)__kim_end >> 8;
    if (is_ram(h))
      hi[n++] = h;
  }
  return n;
}

/* Prints the drive status line and returns 1 if it reports an error. */
static uint8_t dos_error(void) {
  uint8_t code = kd_status(line);
  if (code < 20)
    return 0;
  puts_nl(code == 0xFF ? "No drive" : line);
  return 1;
}

static uint8_t dos_open(uint8_t ch, const char *name) {
  char buf[24];
  uint8_t n = 0;
  if (ch == CH_SAVE) {
    memcpy(buf, "@0:", 3);
    n = 3;
  }
  while (*name && n < 19)
    buf[n++] = *name++;
  if (ch != CH_SAVE) {
    memcpy(buf + n, ",P,R", 4);
    n += 4;
  }
  kd_open(ch, buf, n);
  if (!dos_error())
    return 0;
  kd_close(ch);
  return 1;
}

uint8_t st_open(const char *name) {
  st_page = 0;
  return dos_open(CH_STORY, name);
}

uint8_t st_seek(uint16_t page) {
  uint8_t cmd[6] = {'P', 0x60 | CH_STORY, 0, page, page >> 8, 0};
  if (page == st_page)
    return 0;
  st_page = page;
  kd_cmd(cmd, sizeof cmd);
  return dos_error();
}

uint16_t st_next(uint8_t *dst) {
  uint16_t n = kd_read(CH_STORY, dst, 256);
  memset(dst + n, 0, 256 - n);
  st_page++;
  return n;
}

void st_close(void) { kd_close(CH_STORY); }

uint8_t sv_open(const char *name, uint8_t write) {
  sv_ch = write ? CH_SAVE : CH_RESTORE;
  sv_err = 0;
  return dos_open(sv_ch, name);
}

uint16_t sv_xfer(uint8_t *buf, uint16_t n) {
  uint16_t m = 0;
  if (n)
    m = sv_ch == CH_SAVE ? kd_write(sv_ch, buf, n) : kd_read(sv_ch, buf, n);
  sv_err |= m != n;
  return m;
}

uint8_t sv_close(void) {
  kd_close(sv_ch);
  return dos_error() | sv_err;
}

uint8_t dir_open(void) {
  if (!kd_dir_open("", 0))
    return 0;
  dos_error();
  return 1;
}

uint8_t dir_next(char *name) {
  if (!kd_dir_next(&de)) {
    kd_dir_close();
    return 0;
  }
  memcpy(name, de.name, sizeof de.name);
  return 1;
}

void plat_exit(void) {
  if (PLAT_STATUS)
    vt100_close();
  _Exit(0);
}

int main(void) {
  if (PLAT_STATUS)
    vt100_open();
  puts_nl("KIMFORMER Z-MACHINE V3");
  launcher();
  plat_exit();
  return 0;
}
