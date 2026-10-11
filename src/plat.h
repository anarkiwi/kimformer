/* Platform interface: everything the interpreter needs from the machine. */
#ifndef PLAT_H
#define PLAT_H

#include <stdint.h>

/* Story and RAM pages are 256 bytes.  Every RAM page is named by its high
 * byte in a 64K address space; PAGE_PTR turns that into a pointer.  On the
 * 6502 the address space is the machine's own; the host build simulates it
 * with an arena. */
#ifdef __mos__
#define PAGE_PTR(hi) ((uint8_t *)((uint16_t)(hi) << 8))
#else
extern uint8_t plat_arena[0x10000];
#define PAGE_PTR(hi) (plat_arena + ((uint16_t)(hi) << 8))
#endif

/* Console.  plat_putc takes ASCII with '\n' as the line break.  plat_getc
 * blocks for one key and returns 7-bit ASCII; the platform echoes it if the
 * terminal does not. */
void plat_putc(uint8_t c);
uint8_t plat_getc(void);

/* Free RAM: fills hi[] with the high bytes of the RAM pages the interpreter
 * may use, ascending, and returns how many (at most max). */
uint8_t plat_pages(uint8_t *hi, uint8_t max);

/* A 16-bit value that differs between runs, for seeding the RNG. */
uint16_t plat_entropy(void);

/* Story file.  st_open opens a name returned by dir_next (or typed by the
 * user) for reading; st_seek positions at 256 * page; st_next reads the next
 * 256 bytes into dst and returns how many were in the file (the rest of dst
 * is zeroed, 0 past the end).  st_seek to the current position costs
 * nothing.  Returns: 0 on success. */
uint8_t st_open(const char *name);
uint8_t st_seek(uint16_t page);
uint16_t st_next(uint8_t *dst);
void st_close(void);

/* Save files.  sv_open(name, 1) creates or replaces, sv_open(name, 0) opens
 * for reading; sv_xfer moves n bytes in the opened direction and returns how
 * many moved; sv_close returns 0 if everything was transferred. */
uint8_t sv_open(const char *name, uint8_t write);
uint16_t sv_xfer(uint8_t *buf, uint16_t n);
uint8_t sv_close(void);

/* Directory: dir_open starts a listing of the current directory; dir_next
 * copies the next file name, zero terminated, at most 16 characters, in the
 * form st_open accepts, and returns 0 at the end. */
uint8_t dir_open(void);
uint8_t dir_next(char *name);

/* Leave the program. */
void plat_exit(void);

#endif
