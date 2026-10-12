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
#define ZROWS 24

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
#define H_TERMS 0x2E
#define H_ALPHABET 0x34
#define H_EXT 0x36

#ifdef ZM_TRACE
extern uint8_t zm_trace;
#endif

/* Dynamic memory beyond the pinned prefix (header, globals, object entries,
 * plus ZM_PIN_EXTRA pages) shares the frame pool with static memory; a
 * written page stays resident. */
#ifndef ZM_PIN_EXTRA
#define ZM_PIN_EXTRA 0
#endif

/* ZM_STATS: paging counters, reported by plat_stats before every read. */
#ifdef ZM_STATS
enum {
  ZS_FRAMES,
  ZS_PIN,
  ZS_LOADS,
  ZS_DIRTY,
  ZS_MAXDIRTY,
  ZS_SLOADS,
  ZS_SWB,
  ZS_N
};
extern uint32_t zstat[ZS_N];
void plat_stats(void);
#endif

/* zmem.c */
extern uint8_t pm0[256], pm_hi[8];
extern uint8_t *dyn;
extern uint16_t static_base, pin_base;
extern uint8_t dyn_pages, pin_pages, zm_pin_extra;
extern uint16_t story_pages;
extern uint8_t zstate, zver, zpk;
extern uint8_t pc_hi, str_hi;
extern uint16_t pc_page ZP, str_page;
extern uint8_t str_off;
extern uint8_t *pc_base ZP;
extern uint8_t pc_off ZP;
extern uint16_t pk_page, err_page;
extern uint8_t err_off;
extern uint8_t pk_off;
extern char story_name[17];
#define ZD_LOAD 1
#define ZD_DIRTY 2

/* The page map entry of story page p: the RAM page holding it, 0 if none.
 * Pages 0-255 (every data address) map through pm0, higher ones through
 * 256-entry chunks in frames, RAM pages pm_hi[1..]. */
INLINE uint8_t *pm_at(uint16_t p) {
  return p >> 8 ? PAGE_PTR(pm_hi[p >> 8]) + (uint8_t)p : pm0 + p;
}

/* Stops the story with error code, recording the PC. */
void zfatal(uint8_t code);
uint8_t zpage(uint16_t page);
uint8_t zframe(void);
uint8_t zdpage(uint8_t p, uint8_t mode);
uint8_t *zdynw(uint16_t a, uint16_t len);
uint8_t *zscratch(void);
uint8_t zb(uint16_t addr);
uint16_t zw(uint16_t addr);
void zsb(uint16_t addr, uint8_t v);
void zsw(uint16_t addr, uint16_t v);
uint16_t umul(uint16_t a, uint16_t b);
uint8_t *zdyn(uint16_t addr, uint16_t len);
void zunpack(uint16_t a);
void pc_next(void);
void pc_load(void);
void pc_set(uint16_t page, uint8_t off);
void pc_rel(int16_t d);
void str_set(uint16_t page, uint8_t off);
uint8_t zreload(void);
uint8_t zverify(void);
#ifdef ZM_STATS
void zs_ref(uint8_t p);
#define ZS_REF(p) zs_ref(p)
#else
#define ZS_REF(p)
#endif
/* zb and zw with a resident page read inline, its used flag left alone. */
INLINE uint8_t zbi(uint16_t a) {
  uint8_t h = pm0[a >> 8];
  ZS_REF(a >> 8);
  return h ? PAGE_PTR(h)[(uint8_t)a] : zb(a);
}
INLINE uint16_t zwi(uint16_t a) {
  uint8_t h = pm0[a >> 8], *q;
  ZS_REF(a >> 8);
  if (h && (uint8_t)a != 0xFF) {
    q = PAGE_PTR(h) + (uint8_t)a;
    return q[0] << 8 | q[1];
  }
  return zw(a);
}

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
uint8_t zgetc(void);
uint8_t zreadline(uint8_t *dst, uint8_t max, uint8_t n);
uint8_t zsread(uint16_t text, uint16_t parse);
void ztokenise(uint16_t text, uint16_t parse, uint16_t dict, uint8_t flag);
void zencode_text(uint16_t text, uint8_t len, uint16_t from, uint16_t dst);
void zstream(int16_t n, uint16_t table);
void zstreams_reset(void);
void zstatus(void);
void ztext_init(void);

/* Screen model (ztext.c): windows of version 4 and later. */
void zsplit_window(uint8_t n);
void zset_window(uint8_t w);
void zerase_window(int16_t w);
void zerase_line(void);
void zset_cursor(uint16_t row, uint16_t col);
void zget_cursor(uint16_t table);
void zset_style(uint8_t s);
void zbuffer_mode(uint8_t on);
void zprint_table(uint16_t a, uint16_t w, uint16_t h, uint16_t skip);
uint8_t zread_char(void);
void zscreen_reset(void);

/* zobj.c */
extern uint16_t obj_base;
uint16_t obj_rel(uint16_t o, uint8_t field);
uint8_t obj_attr(uint16_t o, uint8_t a, uint8_t op);
void obj_insert(uint16_t o, uint16_t d);
void obj_remove(uint16_t o);
uint16_t obj_name(uint16_t o);
uint16_t prop_get(uint16_t o, uint8_t p);
uint16_t prop_addr(uint16_t o, uint8_t p);
uint8_t prop_len(uint16_t addr);
uint8_t prop_next(uint16_t o, uint8_t p);
void prop_put(uint16_t o, uint8_t p, uint16_t v);

#define OBJ_PARENT 0
#define OBJ_SIBLING 1
#define OBJ_CHILD 2
#define ATTR_TEST 0
#define ATTR_SET 1
#define ATTR_CLEAR 2

#endif
