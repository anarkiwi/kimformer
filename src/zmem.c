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
#if !ZM_PAGE_DYN
static uint8_t data_page, data_hi;
#endif

#if ZM_PAGE_DYN
#define DIRTY 2
#define USE(h) (used[h] |= 1)
#define UNUSE(h) (used[h] &= DIRTY)
uint16_t pin_base;
uint8_t pin_pages, zm_pin_extra = ZM_PIN_EXTRA;
#else
#define USE(h) (used[h] = 1)
#define UNUSE(h) (used[h] = 0)
#define pin_pages dyn_pages
#endif

#ifdef ZM_STATS
uint32_t zstat[ZS_N];
#define ZS_INC(i) zstat[i]++
#else
#define ZS_INC(i)
#endif

#if defined(ZM_STATS) && ZM_PAGE_DYN
/* Shadow of the frame pool under clock replacement that evicts dirty pages,
 * counting the loads and write-backs it would make.  op: 0 reference, 1
 * write, 2 drop, 3 reference that leaves the used flag of a resident page. */
static uint16_t sh_pg[255];
static uint8_t sh_fl[255], sh_slot[512], sh_hand;

static void shadow(uint16_t p, uint8_t op) {
  uint8_t s = sh_slot[p], i;
  uint16_t q;
  if (p < pin_pages)
    return;
  if (op == 2) {
    if (s) {
      sh_pg[s - 1] = 0xFFFF;
      sh_fl[s - 1] = sh_slot[p] = 0;
    }
    return;
  }
  if (op == 3 && s)
    return;
  if (!s) {
    zstat[ZS_SLOADS]++;
    for (;;) {
      i = sh_hand;
      if (++sh_hand == nframes)
        sh_hand = 0;
      q = sh_pg[i];
      if (q == pc_page || (str_hi && q == str_page))
        continue;
      if (!(sh_fl[i] & 1))
        break;
      sh_fl[i] &= ~1;
    }
    if (sh_fl[i] & DIRTY)
      zstat[ZS_SWB]++;
    if (q != 0xFFFF)
      sh_slot[q] = 0;
    sh_pg[i] = p;
    sh_fl[i] = 0;
    sh_slot[p] = s = i + 1;
  }
  sh_fl[s - 1] |= op == 1 ? 1 | DIRTY : 1;
}

void zs_ref(uint8_t p) { shadow(p, 3); }

static void shadow_init(void) {
  uint8_t i, h;
  uint16_t p;
  memset(sh_slot, 0, sizeof sh_slot);
  sh_hand = 0;
  for (i = 0; i < nframes; i++) {
    h = frames[i];
    p = (uint16_t)own_hi[h] << 8 | own_lo[h];
    sh_fl[i] = 0;
    sh_pg[i] = page_map[p] == h ? p : 0xFFFF;
    if (page_map[p] == h)
      sh_slot[p] = i + 1;
  }
}
#define SHADOW(p, op) shadow(p, op)
#else
#define SHADOW(p, op) (void)(p)
#endif

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
    UNUSE(h);
  }
#if !ZM_PAGE_DYN
  if (h == data_hi)
    data_hi = 0;
#endif
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
  SHADOW(p, 0);
  h = page_map[p];
  if (!h) {
    h = zframe();
    ZS_INC(ZS_LOADS);
    st_seek(p);
    st_next(PAGE_PTR(h));
    page_map[p] = h;
    own_lo[h] = p;
    own_hi[h] = p >> 8;
  }
  USE(h);
  return h;
}

#if ZM_PAGE_DYN
static void zdirty(uint8_t h, uint16_t p) {
  if (!(used[h] & DIRTY)) {
    used[h] |= DIRTY;
#ifdef ZM_STATS
    if (++zstat[ZS_DIRTY] > zstat[ZS_MAXDIRTY])
      zstat[ZS_MAXDIRTY] = zstat[ZS_DIRTY];
#endif
  }
  SHADOW(p, 1);
}

/* The frame of dynamic page p: looked up (ZD_LOAD) or mapped without
 * loading, and marked dirty (ZD_DIRTY). */
uint8_t zdpage(uint8_t p, uint8_t mode) {
  uint8_t h;
  if (p < pin_pages)
    return dyn_hi + p;
  if (mode & ZD_LOAD)
    h = zpage(p);
  else {
    SHADOW(p, 0);
    h = page_map[p];
    if (!h) {
      h = zframe();
      page_map[p] = h;
      own_lo[h] = p;
      own_hi[h] = 0;
    }
  }
  if (mode & ZD_DIRTY)
    zdirty(h, p);
  return h;
}

/* The frame of page p for writing: dynamic pages beyond the pinned ones are
 * marked dirty. */
static uint8_t zwpage(uint8_t p) {
  uint8_t h = page_map[p];
  if (!h)
    h = zpage(p);
  if (p >= pin_pages)
    zdirty(h, p);
  return h;
}

/* Dynamic memory a..a+len-1 for writing if it is pinned or in one page (made
 * dirty, so it stays resident), else 0. */
uint8_t *zdynw(uint16_t a, uint16_t len) {
  if (a + len <= pin_base)
    return dyn + a;
  if (a + len <= static_base && !((a ^ (a + len - 1)) >> 8))
    return PAGE_PTR(zwpage(a >> 8)) + (uint8_t)a;
  return 0;
}

uint8_t zb(uint16_t a) {
  uint8_t p = a >> 8, h = page_map[p];
  if (!h)
    return PAGE_PTR(zpage(p))[(uint8_t)a];
  USE(h);
  SHADOW(p, 0);
  return PAGE_PTR(h)[(uint8_t)a];
}

uint16_t zw(uint16_t a) {
  uint8_t p = a >> 8, h = page_map[p], *q;
  if (!h || (uint8_t)a == 0xFF)
    return (uint16_t)zb(a) << 8 | zb(a + 1);
  USE(h);
  SHADOW(p, 0);
  q = PAGE_PTR(h) + (uint8_t)a;
  return q[0] << 8 | q[1];
}

NOINLINE void zsb(uint16_t a, uint8_t v) {
  if (a < static_base)
    PAGE_PTR(zwpage(a >> 8))[(uint8_t)a] = v;
  else
    zfatal(ZM_ERR_ADDRESS);
}

NOINLINE void zsw(uint16_t a, uint16_t v) {
  zsb(a, v >> 8);
  zsb(a + 1, v);
}
#else
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
#endif

NOINLINE uint8_t *zdyn(uint16_t a, uint16_t len) {
  if (a >= static_base || static_base - a < len) {
    zfatal(ZM_ERR_ADDRESS);
    return dyn;
  }
  return dyn + a;
}

#if !ZM_PAGE_DYN
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
#endif

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
  for (; p < pin_pages; p++) {
    if (dots)
      zdots(p);
    if (!st_next(PAGE_PTR(dyn_hi + p)))
      return 0;
  }
  return 1;
}

uint8_t zreload(void) {
  uint8_t f2 = dyn[H_FLAGS2 + 1] & 3;
#if ZM_PAGE_DYN
  uint8_t p, h;
  for (p = pin_pages; p < dyn_pages; p++)
    if ((h = page_map[p])) {
#ifdef ZM_STATS
      zstat[ZS_DIRTY] -= used[h] >> 1;
#endif
      SHADOW(p, 2);
      page_map[p] = used[h] = 0;
      if (h == pc_hi)
        pc_page = 0xFFFF;
    }
#endif
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

#if ZM_PAGE_DYN
static uint16_t dw(uint16_t a) { return (uint16_t)dyn[a] << 8 | dyn[a + 1]; }

/* Extends the pinned prefix to the page holding byte end - 1, within dynamic
 * memory and at most max pages. */
static uint8_t pin_to(uint16_t end, uint8_t max) {
  uint8_t p = pin_pages;
  uint16_t n = (end >> 8) + !!(uint8_t)end;
  if (n > dyn_pages)
    n = dyn_pages;
  if (n > max)
    n = max;
  if (n <= p)
    return 1;
  pin_pages = n;
  pin_base = n < dyn_pages ? n << 8 : static_base;
  return load_dyn(p, 0);
}
#endif

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
#if ZM_PAGE_DYN
  if (static_base < 0x40)
    return ZM_ERR_MEMORY;
  dyn_pages = w;
  pin_pages = 1;
  pin_base = static_base < 0x100 ? static_base : 0x100;
  w = dw(H_OBJECTS) + 71;
  p = dw(H_GLOBALS) + 480;
  if (!pin_to(p > w ? p : w, blen) ||
      (w <= pin_base && !pin_to(dw(w - 2), blen)) ||
      !pin_to((pin_pages + zm_pin_extra) << 8, blen))
    return ZM_ERR_OPEN;
#else
  if (static_base < 0x40 || w > blen)
    return ZM_ERR_MEMORY;
  dyn_pages = w;
#endif
  w = (uint16_t)dyn[H_LENGTH] << 8 | dyn[H_LENGTH + 1];
  story_pages = w ? (w + 127) >> 7 : 512;
  nframes = n - pin_pages;
  for (i = best; i < nframes; i++)
    frames[i] = frames[i + pin_pages];
  if (nframes < dyn_pages - pin_pages + 3)
    return ZM_ERR_MEMORY;
  memset(page_map, 0, sizeof page_map);
  memset(own_lo, 0, sizeof own_lo);
  memset(own_hi, 0, sizeof own_hi);
  memset(used, 0, sizeof used);
#if ZM_PAGE_DYN
  pc_hi = str_hi = hand = 0;
#else
  pc_hi = str_hi = hand = data_hi = 0;
#endif
  pc_page = 0xFFFF;
  for (i = 0; i < pin_pages; i++)
    page_map[i] = dyn_hi + i;
#if ZM_PAGE_DYN
  for (i = 0; i < pin_pages; i++)
    zdots(i);
#else
  zdots(0);
  if (!load_dyn(1, 1))
    return ZM_ERR_OPEN;
#endif
  for (p = pin_pages, i = 0; i < nframes && p < story_pages; i++, p++) {
    zdots(p);
    if (!st_next(PAGE_PTR(frames[i])))
      break;
    page_map[p] = frames[i];
    own_lo[frames[i]] = p;
    own_hi[frames[i]] = p >> 8;
  }
#ifdef ZM_STATS
  memset(zstat, 0, sizeof zstat);
  zstat[ZS_FRAMES] = nframes;
  zstat[ZS_PIN] = pin_pages;
#if ZM_PAGE_DYN
  shadow_init();
#endif
#endif
  plat_putc('\n');
  return ZM_OK;
}
