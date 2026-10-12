/* Minimal platform for link and size checks of the core on the 6502. */
#include "../plat.h"
#include "../zm.h"

static volatile uint8_t io;

void plat_putc(uint8_t c) { io = c; }
uint8_t plat_getc(void) { return io; }
uint8_t plat_pages(uint8_t *hi, uint8_t max) {
  uint8_t n;
  for (n = 0; n < max && n < 0xA0; n++)
    hi[n] = 0x60 + n;
  return n;
}
uint16_t plat_entropy(void) { return io; }
uint8_t st_open(const char *name) { return name[0] == io; }
uint8_t st_seek(uint16_t page) { return page == io; }
uint16_t st_next(uint8_t *dst) { return dst[0] = io; }
void st_close(void) { io = 0; }
uint8_t sv_open(const char *name, uint8_t write) { return name[0] == write; }
uint16_t sv_xfer(uint8_t *buf, uint16_t n) { return buf[0] = n; }
uint8_t sv_close(void) { return io; }
uint8_t dir_open(void) { return io; }
uint8_t dir_next(char *name) { return name[0] = io; }
void plat_exit(void) { io = 1; }

int main(void) {
  launcher();
  return 0;
}
