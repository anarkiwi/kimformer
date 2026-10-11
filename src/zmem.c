/* Story paging, dynamic memory, byte/word access. */
#include <string.h>

#include "zint.h"

uint8_t page_map[512];
uint8_t *dyn;
uint16_t static_base;
uint8_t dyn_pages;
uint8_t zstate;
uint8_t pc_hi, str_hi;
uint16_t pc_page, str_page;
uint8_t pc_off, str_off;
char story_name[17];

static uint8_t frames[255];
static uint8_t nframes;
static uint8_t dyn_hi;
static uint8_t story_open;
static uint16_t story_pages;
static uint16_t lru_clock;
static uint8_t own_lo[256], own_hi[256], st_lo[256], st_hi[256];

void zfatal(uint8_t code) {
  if (zstate == ZS_RUN)
    zstate = code;
}

static uint8_t victim(void) {
  uint8_t i, h, best = 0;
  uint16_t m = 0xFFFF, t;
  for (i = 0; i < nframes; i++) {
    h = frames[i];
    t = (uint16_t)st_hi[h] << 8 | st_lo[h];
    if (h != pc_hi && h != str_hi && t <= m) {
      m = t;
      best = h;
    }
  }
  page_map[(uint16_t)own_hi[best] << 8 | own_lo[best]] = 0;
  own_lo[best] = own_hi[best] = 0;
  return best;
}

uint8_t zpage(uint16_t p) {
  uint8_t h;
  if (p >= story_pages) {
    zfatal(ZM_ERR_ADDRESS);
    p = 0;
  }
  h = page_map[p];
  if (!h) {
    h = victim();
    st_seek(p);
    st_next(PAGE_PTR(h));
    page_map[p] = h;
    own_lo[h] = p;
    own_hi[h] = p >> 8;
  }
  if (!++lru_clock) {
    memset(st_lo, 0, sizeof st_lo);
    memset(st_hi, 0, sizeof st_hi);
    lru_clock = 1;
  }
  st_lo[h] = lru_clock;
  st_hi[h] = lru_clock >> 8;
  return h;
}

uint8_t zb(uint16_t a) {
  if (a < static_base)
    return dyn[a];
  return PAGE_PTR(zpage(a >> 8))[(uint8_t)a];
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

NOINLINE void zsb(uint16_t a, uint8_t v) { *zdyn(a, 1) = v; }

NOINLINE void zsw(uint16_t a, uint16_t v) {
  uint8_t *p = zdyn(a, 2);
  p[0] = v >> 8;
  p[1] = v;
}

uint8_t pc_byte(void) {
  uint8_t b;
  if (!pc_hi)
    pc_hi = zpage(pc_page);
  b = PAGE_PTR(pc_hi)[pc_off];
  if (!++pc_off) {
    pc_page++;
    pc_hi = 0;
  }
  return b;
}

void pc_set(uint16_t page, uint8_t off) {
  pc_page = page & 0x1FF;
  pc_off = off;
  pc_hi = 0;
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
  uint8_t *b = PAGE_PTR(victim());
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
  memset(st_lo, 0, sizeof st_lo);
  memset(st_hi, 0, sizeof st_hi);
  pc_hi = str_hi = 0;
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
