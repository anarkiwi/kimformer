/* Interpreter internals shared by the core modules. */
#ifndef ZINT_H
#define ZINT_H

#include "plat.h"
#include "zm.h"

#define NOINLINE __attribute__((noinline))
#define INLINE static inline __attribute__((always_inline))
#ifdef __mos__
#define ZP __attribute__((section(".zp.bss")))
#else
#define ZP
#endif
#define ZS_RUN 0xFF
#define ZS_PAST 0xFE
#define ZRUNNING (zstate >= ZS_PAST)
#define ZNL 13

/* Header fields. */
#define H_VERSION 0x00
#define H_FLAGS1 0x01
#define H_RELEASE 0x02
#define H_PC 0x06
#define H_DICT 0x08
#define H_OBJECTS 0x0A
#define H_GLOBALS 0x0C
#define H_STATIC 0x0E
#define H_FLAGS2 0x10
#define H_SERIAL 0x12
#define H_ABBREV 0x18
#define H_LENGTH 0x1A
#define H_CHECKSUM 0x1C

#ifdef ZM_TRACE
extern uint8_t zm_trace;
#endif

/* zmem.c */
extern uint8_t page_map[512];
extern uint8_t *dyn;
extern uint16_t static_base;
extern uint8_t dyn_pages;
extern uint16_t story_pages;
extern uint8_t zstate;
extern uint8_t pc_hi, str_hi;
extern uint16_t pc_page ZP, str_page;
extern uint8_t str_off;
extern uint8_t *pc_base ZP;
extern uint8_t pc_off ZP;
extern char story_name[17];

void zfatal(uint8_t code);
uint8_t zpage(uint16_t page);
uint8_t zframe(void);
uint8_t zb(uint16_t addr);
uint16_t zw(uint16_t addr);
void zsb(uint16_t addr, uint8_t v);
void zsw(uint16_t addr, uint16_t v);
uint16_t umul(uint16_t a, uint16_t b);
uint8_t *zdyn(uint16_t addr, uint16_t len);
void pc_next(void);
void pc_load(void);
void pc_set(uint16_t page, uint8_t off);
void pc_rel(int16_t d);
void str_set(uint16_t page, uint8_t off);
uint8_t zreload(void);
uint8_t zverify(void);

/* The byte at the PC; advances it. */
INLINE uint8_t pc_byte(void) {
  uint8_t b = pc_base[pc_off];
  if (!++pc_off)
    pc_next();
  return b;
}

uint16_t pc_word_slow(void);

/* The big-endian word at the PC; advances it. */
INLINE uint16_t pc_word(void) {
  uint8_t o = pc_off;
  if (o >= 0xFE)
    return pc_word_slow();
  pc_off = o + 2;
  return pc_base[o] << 8 | pc_base[(uint8_t)(o + 1)];
}

/* ztext.c */
void zputc(uint8_t c);
void zputs(const char *s);
void zflush(void);
void zprint_str(void);
void zprint_num(uint16_t v);
void zprint_hex(uint16_t v, uint8_t digits);
uint8_t zreadline(uint8_t *dst, uint8_t max);
void zsread(uint16_t text, uint16_t parse);
void zstream(int16_t n, uint16_t table);
void zstreams_reset(void);
void zstatus(void);

/* zobj.c */
extern uint16_t obj_base;
uint8_t obj_rel(uint16_t o, uint8_t field);
uint8_t obj_attr(uint16_t o, uint8_t a, uint8_t op);
void obj_insert(uint16_t o, uint16_t d);
void obj_remove(uint16_t o);
uint16_t obj_name(uint16_t o);
uint16_t prop_get(uint16_t o, uint8_t p);
uint16_t prop_addr(uint16_t o, uint8_t p);
uint8_t prop_len(uint16_t addr);
uint8_t prop_next(uint16_t o, uint8_t p);
void prop_put(uint16_t o, uint8_t p, uint16_t v);

#define OBJ_PARENT 4
#define OBJ_SIBLING 5
#define OBJ_CHILD 6
#define ATTR_TEST 0
#define ATTR_SET 1
#define ATTR_CLEAR 2

#endif
