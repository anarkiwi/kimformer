/* Story paging, dynamic memory, byte/word access. */
#include <string.h>

#include "zint.h"

uint8_t page_map[512];
uint8_t *dyn;
uint16_t static_base;
uint8_t dyn_pages;
uint8_t zstate;
uint8_t pc_hi, str_hi;
uint16_t pc_page ZP, str_page;
uint8_t str_off;
uint8_t *pc_base ZP;
uint8_t pc_off ZP;
char story_name[17];
uint16_t story_pages;

static uint8_t frames[255];
static uint8_t nframes, hand;
static uint8_t dyn_hi;
static uint8_t story_open;
static uint8_t own_lo[256], own_hi[256], used[256];
static uint8_t data_page, data_hi;

void zfatal(uint8_t code) {
  if (ZRUNNING)
    zstate = code;
}

uint8_t zframe(void) {
  uint16_t p;
  uint8_t h;
  for (;;) {
    h = frames[hand];
    if (++hand == nframes)
      hand = 0;
    if (h == pc_hi || h == str_hi)
      continue;
    if (!used[h])
      break;
    used[h] = 0;
  }
  if (h == data_hi)
    data_hi = 0;
  p = (uint16_t)own_hi[h] << 8 | own_lo[h];
  if (page_map[p] == h)
    page_map[p] = 0;
  return h;
}

uint8_t zpage(uint16_t p) {
  uint8_t h;
  if (p >= story_pages) {
    zfatal(ZM_ERR_ADDRESS);
    p = 0;
  }
  h = page_map[p];
  if (!h) {
    h = zframe();
    st_seek(p);
    st_next(PAGE_PTR(h));
    page_map[p] = h;
    own_lo[h] = p;
    own_hi[h] = p >> 8;
  }
  used[h] = 1;
  return h;
}

uint8_t zb(uint16_t a) {
  uint8_t p = a >> 8;
  if (a < static_base)
    return dyn[a];
  if (p != data_page || !data_hi) {
    data_hi = zpage(p);
    data_page = p;
  }
  return PAGE_PTR(data_hi)[(uint8_t)a];
}

uint16_t zw(uint16_t a) {
  if (a < static_base - 1)
    return (uint16_t)dyn[a] << 8 | dyn[a + 1];
  return (uint16_t)zb(a) << 8 | zb(a + 1);
}

NOINLINE uint8_t *zdyn(uint16_t a, uint16_t len) {
  if (a >= static_base || static_base - a < len) {
    zfatal(ZM_ERR_ADDRESS);
    return dyn;
  }
  return dyn + a;
}

NOINLINE void zsb(uint16_t a, uint8_t v) {
  if (a < static_base)
    dyn[a] = v;
  else
    zfatal(ZM_ERR_ADDRESS);
}

NOINLINE void zsw(uint16_t a, uint16_t v) {
  if (a < static_base - 1) {
    dyn[a] = v >> 8;
    dyn[a + 1] = v;
  } else
    zfatal(ZM_ERR_ADDRESS);
}

NOINLINE uint16_t umul(uint16_t a, uint16_t b) {
  uint16_t r = 0;
  for (; b; b >>= 1, a <<= 1)
    if (b & 1)
      r += a;
  return r;
}

void pc_load(void) {
  if (pc_page < story_pages)
    pc_hi = zpage(pc_page);
  else {
    pc_hi = dyn_hi;
    if (zstate == ZS_RUN)
      zstate = ZS_PAST;
  }
  pc_base = PAGE_PTR(pc_hi);
}

NOINLINE void pc_next(void) {
  pc_page++;
  pc_load();
}

void pc_set(uint16_t page, uint8_t off) {
  page &= 0x1FF;
  pc_off = off;
  if (page != pc_page) {
    pc_page = page;
    pc_load();
  }
}

NOINLINE uint16_t pc_word_slow(void) {
  uint8_t b = pc_byte();
  return b << 8 | pc_byte();
}

NOINLINE void pc_rel(int16_t d) {
  uint16_t lo = (uint16_t)pc_off + (uint8_t)d;
  pc_off = lo;
  d = (d >> 8) + (lo >> 8);
  if (d)
    pc_set(pc_page + d, pc_off);
}

void str_set(uint16_t page, uint8_t off) {
  str_page = page & 0x1FF;
  str_off = off;
  str_hi = 0;
}

static void zdots(uint8_t n) {
  if (!(n & 7))
    plat_putc('.');
}

static uint8_t load_dyn(uint8_t p, uint8_t dots) {
  for (; p < dyn_pages; p++) {
    if (dots)
      zdots(p);
    if (!st_next(PAGE_PTR(dyn_hi + p)))
      return 0;
  }
  return 1;
}

uint8_t zreload(void) {
  uint8_t f2 = dyn[H_FLAGS2 + 1] & 3;
  st_seek(0);
  if (!load_dyn(0, 0))
    return 0;
  dyn[H_FLAGS2 + 1] = (dyn[H_FLAGS2 + 1] & ~3) | f2;
  return 1;
}

uint8_t zverify(void) {
  uint8_t *b = PAGE_PTR(zframe());
  uint16_t len = zw(H_LENGTH), p, sum = 0;
  uint16_t pages = len >> 7;
  uint8_t rem = len << 1, i;
  st_seek(0);
  for (p = 0; p < pages || (p == pages && rem); p++) {
    st_next(b);
    i = p ? 0 : 0x40;
    if (p == pages) {
      while (i < rem)
        sum += b[i++];
    } else {
      do
        sum += b[i];
      while (++i);
    }
  }
  return sum == zw(H_CHECKSUM);
}

uint8_t zm_load(const char *name) {
  uint8_t n, i, start = 0, len = 0, best = 0, blen = 0;
  uint16_t p, w;
  for (i = 0; i < sizeof story_name - 1 && (story_name[i] = name[i]); i++)
    ;
  if (story_open)
    st_close();
  story_open = 0;
  if (st_open(name))
    return ZM_ERR_OPEN;
  story_open = 1;
  n = plat_pages(frames, sizeof frames);
  for (i = 0; i < n; i++) {
    if (i && frames[i] == frames[i - 1] + 1)
      len++;
    else {
      start = i;
      len = 1;
    }
    if (len > blen) {
      blen = len;
      best = start;
    }
  }
  if (!blen)
    return ZM_ERR_MEMORY;
  dyn_hi = frames[best];
  dyn = PAGE_PTR(dyn_hi);
  st_seek(0);
  if (!st_next(dyn))
    return ZM_ERR_OPEN;
  if (dyn[H_VERSION] != 3)
    return ZM_ERR_VERSION;
  static_base = (uint16_t)dyn[H_STATIC] << 8 | dyn[H_STATIC + 1];
  w = (static_base >> 8) + !!(uint8_t)static_base;
  if (static_base < 0x40 || w > blen)
    return ZM_ERR_MEMORY;
  dyn_pages = w;
  w = (uint16_t)dyn[H_LENGTH] << 8 | dyn[H_LENGTH + 1];
  story_pages = w ? (w + 127) >> 7 : 512;
  nframes = n - dyn_pages;
  for (i = best; i < nframes; i++)
    frames[i] = frames[i + dyn_pages];
  if (nframes < 3)
    return ZM_ERR_MEMORY;
  memset(page_map, 0, sizeof page_map);
  memset(own_lo, 0, sizeof own_lo);
  memset(own_hi, 0, sizeof own_hi);
  memset(used, 0, sizeof used);
  pc_hi = str_hi = hand = data_hi = 0;
  pc_page = 0xFFFF;
  for (i = 0; i < dyn_pages; i++)
    page_map[i] = dyn_hi + i;
  zdots(0);
  if (!load_dyn(1, 1))
    return ZM_ERR_OPEN;
  for (p = dyn_pages, i = 0; i < nframes && p < story_pages; i++, p++) {
    zdots(p);
    if (!st_next(PAGE_PTR(frames[i])))
      break;
    page_map[p] = frames[i];
    own_lo[frames[i]] = p;
    own_hi[frames[i]] = p >> 8;
  }
  plat_putc('\n');
  return ZM_OK;
}
