/* Z-machine interpreter for story versions 3, 4, 5 and 8. */
#ifndef ZM_H
#define ZM_H

#include <stdint.h>

/* Status codes from zm_load and zm_run. */
enum {
  ZM_OK = 0,      /* story ended with @quit */
  ZM_ERR_OPEN,    /* story file could not be opened or read */
  ZM_ERR_VERSION, /* not a version 3, 4, 5 or 8 story */
  ZM_ERR_MEMORY,  /* dynamic memory does not fit the free RAM */
  ZM_ERR_OPCODE,  /* illegal opcode */
  ZM_ERR_STACK,   /* evaluation or call stack overflow/underflow */
  ZM_ERR_ADDRESS, /* write outside dynamic memory, or read past the story */
  ZM_ERR_OBJECT,  /* object number 0 used where an object is required */
};

/* Output line width used for word wrapping (default 80). */
extern uint8_t zm_width;

/* Open the story, load its dynamic memory and fill the page cache.  Prints
 * load progress.  Returns ZM_OK or an error code. */
uint8_t zm_load(const char *name);

/* Run the loaded story until @quit or a fatal error; on error prints a
 * diagnostic with the code and the PC.  Returns ZM_OK or the error. */
uint8_t zm_run(void);

/* Interactive story chooser: lists *.Z3, *.Z4, *.Z5 and *.Z8 files, lets the
 * user pick one, then loads and runs it; repeats until the user quits.  Returns
 * on Q. */
void launcher(void);

#endif
