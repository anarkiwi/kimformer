/* VT100 primitives: the v3 status line, and the cursor addressing, scroll
 * region, erasing and SGR styles of the v4+ screen model. */
#ifndef VT100_H
#define VT100_H

#include <stdint.h>

/* Set the scroll region to rows 2-24 and put the cursor on row 24. */
void vt100_open(void);
/* Reset the scroll region; the cursor ends on row 24. */
void vt100_close(void);
/* Cursor to row r, column c (1-based). */
void vt_goto(uint8_t r, uint8_t c);
/* Scroll region rows top-24 (the cursor is homed). */
void vt_region(uint8_t top);
/* ESC [ s. */
void vt_csi(const char *s);
/* Z-machine text style s: bit 0 reverse, 1 bold, 2 italic (underlined). */
void vt_style(uint8_t s);

#endif
