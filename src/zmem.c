/* Story paging, dynamic memory, byte/word access. */
#include <string.h>

#include "zint.h"

uint8_t pm0[256], pm_hi[8];
uint8_t *dyn;
uint16_t static_base, pin_base;
uint8_t dyn_pages, pin_pages, zm_pin_extra = ZM_PIN_EXTRA;
uint8_t zstate, zver, zpk;
uint8_t pc_hi, str_hi;
uint16_t pc_page ZP, str_page;
uint8_t str_off;
uint8_t *pc_base ZP;
uint8_t pc_off ZP;
uint16_t pk_page;
uint8_t pk_off;
char story_name[17];
uint16_t story_pages;

static uint8_t frames[255];
static uint8_t nframes, hand;
static uint8_t dyn_hi;
static uint8_t story_open;
static uint8_t own_lo[256], own_hi[256], used[256];

#define DIRTY 2
#define USE(h) (used[h] |= 1)
#define UNUSE(h) (used[h] &= DIRTY)

#ifdef ZM_STATS
uint32_t zstat[ZS_N];
#define ZS_INC(i) zstat[i]++

/* Shadow of the frame pool under clock replacement that evicts dirty pages,
 * counting the loads and write-backs it would make.  op: 0 reference, 1
 * write, 2 drop, 3 reference that leaves the used flag of a resident page. */
static uint16_t sh_pg[255];
static uint8_t sh_fl[255], sh_slot[2048], sh_hand;

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
    sh_pg[i] = *pm_at(p) == h ? p : 0xFFFF;
    if (*pm_at(p) == h)
      sh_slot[p] = i + 1;
  }
}
#define SHADOW(p, op) shadow(p, op)
#else
#define ZS_INC(i)
#define SHADOW(p, op) (void)(p)
#endif

uint16_t err_page;
uint8_t err_off;

void zfatal(uint8_t code) {
  if (ZRUNNING) {
    zstate = code;
    err_page = pc_page;
    err_off = pc_off;
  }
}

uint8_t zframe(void) {
  uint8_t h, *m;
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
  m = pm_at((uint16_t)own_hi[h] << 8 | own_lo[h]);
  if (*m == h)
    *m = 0;
  return h;
}

uint8_t zpage(uint16_t p) {
  uint8_t h, *m;
  if (p >= story_pages) {
    zfatal(ZM_ERR_ADDRESS);
    p = 0;
  }
  SHADOW(p, 0);
  m = pm_at(p);
  h = *m;
  if (!h) {
    h = zframe();
    ZS_INC(ZS_LOADS);
    st_seek(p);
    st_next(PAGE_PTR(h));
    *m = h;
    own_lo[h] = p;
    own_hi[h] = p >> 8;
  }
  USE(h);
  return h;
}

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
    h = pm0[p];
    if (!h) {
      h = zframe();
      pm0[p] = h;
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
  uint8_t h = pm0[p];
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
  uint8_t p = a >> 8, h = pm0[p];
  if (!h)
    return PAGE_PTR(zpage(p))[(uint8_t)a];
  USE(h);
  SHADOW(p, 0);
  return PAGE_PTR(h)[(uint8_t)a];
}

uint16_t zw(uint16_t a) {
  uint8_t p = a >> 8, h = pm0[p], *q;
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

NOINLINE uint8_t *zdyn(uint16_t a, uint16_t len) {
  if (a >= static_base || static_base - a < len) {
    zfatal(ZM_ERR_ADDRESS);
    return dyn;
  }
  return dyn + a;
}

NOINLINE uint16_t umul(uint16_t a, uint16_t b) {
  uint16_t r = 0;
  for (; b; b >>= 1, a <<= 1)
    if (b & 1)
      r += a;
  return r;
}

/* pk_page and pk_off of the packed address a. */
void zunpack(uint16_t a) {
  uint8_t i = zpk, top = 0, hi = a >> 8, lo = a;
  do {
    top = top << 1 | hi >> 7;
    hi = hi << 1 | lo >> 7;
    lo <<= 1;
  } while (--i);
  pk_page = (uint16_t)top << 8 | hi;
  pk_off = lo;
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
  str_page = page;
  str_off = off;
  str_hi = 0;
}

static void zdots(uint8_t n) {
  if (!(n & 7))
    plat_putc('.');
}

static uint8_t load_dyn(uint8_t p) {
  for (; p < pin_pages; p++)
    if (!st_next(PAGE_PTR(dyn_hi + p)))
      return 0;
  return 1;
}

uint8_t zreload(void) {
  uint8_t f2 = dyn[H_FLAGS2 + 1] & 3, p, h;
  for (p = pin_pages; p < dyn_pages; p++)
    if ((h = pm0[p])) {
#ifdef ZM_STATS
      zstat[ZS_DIRTY] -= used[h] >> 1;
#endif
      SHADOW(p, 2);
      pm0[p] = used[h] = 0;
      if (h == pc_hi)
        pc_page = 0xFFFF;
    }
  st_seek(0);
  if (!load_dyn(0))
    return 0;
  dyn[H_FLAGS2 + 1] = (dyn[H_FLAGS2 + 1] & ~3) | f2;
  return 1;
}

uint8_t zverify(void) {
  uint8_t *b = PAGE_PTR(zframe());
  uint16_t len = zw(H_LENGTH), p, sum = 0;
  uint16_t pages = len >> (8 - zpk);
  uint8_t rem = len << zpk, i;
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
  return load_dyn(p);
}

uint8_t zm_load(const char *name) {
  uint8_t n, i, start = 0, len = 0, best = 0, blen = 0, npm;
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
  zver = dyn[H_VERSION];
  if (zver < 3 || zver == 6 || zver == 7 || zver > 8)
    return ZM_ERR_VERSION;
  zpk = (zver >> 2) + 1;
  static_base = dw(H_STATIC);
  w = (static_base >> 8) + !!(uint8_t)static_base;
  if (static_base < 0x40 || w > 0xFF)
    return ZM_ERR_MEMORY;
  dyn_pages = w;
  pin_pages = 1;
  pin_base = static_base < 0x100 ? static_base : 0x100;
  w = dw(H_OBJECTS) + (zver == 3 ? 71 : 140);
  p = dw(H_GLOBALS) + 480;
  if (!pin_to(p > w ? p : w, blen) ||
      (w <= pin_base && !pin_to(dw(w - 2), blen)) ||
      !pin_to((pin_pages + zm_pin_extra) << 8, blen))
    return ZM_ERR_OPEN;
  w = dw(H_LENGTH);
  i = 8 - zpk;
  story_pages = w ? (w >> i) + !!(w & ((1 << i) - 1)) : 256 << zpk;
  npm = (story_pages - 1) >> 8;
  nframes = n - pin_pages;
  for (i = best; i < nframes; i++)
    frames[i] = frames[i + pin_pages];
  if (nframes < (uint16_t)npm + dyn_pages - pin_pages + 3)
    return ZM_ERR_MEMORY;
  nframes -= npm;
  memset(pm0, 0, sizeof pm0);
  for (i = 0; i < npm; i++)
    memset(PAGE_PTR(pm_hi[i + 1] = frames[nframes + i]), 0, 0x100);
  memset(own_lo, 0, sizeof own_lo);
  memset(own_hi, 0, sizeof own_hi);
  memset(used, 0, sizeof used);
  pc_hi = str_hi = hand = 0;
  pc_page = 0xFFFF;
  for (i = 0; i < pin_pages; i++) {
    pm0[i] = dyn_hi + i;
    zdots(i);
  }
  for (p = pin_pages, i = 0; i < nframes && p < story_pages; i++, p++) {
    zdots(p);
    if (!st_next(PAGE_PTR(frames[i])))
      break;
    *pm_at(p) = frames[i];
    own_lo[frames[i]] = p;
    own_hi[frames[i]] = p >> 8;
  }
#ifdef ZM_STATS
  memset(zstat, 0, sizeof zstat);
  zstat[ZS_FRAMES] = nframes;
  zstat[ZS_PIN] = pin_pages;
  shadow_init();
#endif
  plat_putc('\n');
  return ZM_OK;
}
