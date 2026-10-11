/* Minimal VT100 status line: row 1 in reverse video, rows 2-24 scroll. */
#ifndef VT100_H
#define VT100_H

/* Set the scroll region to rows 2-24 and put the cursor on row 24. */
void vt100_open(void);
/* Reset the scroll region; the cursor ends on row 24. */
void vt100_close(void);

#endif
