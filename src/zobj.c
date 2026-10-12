/* Object tree and properties: 9-byte entries, 32 attributes and 31
 * properties in version 3; 14-byte entries, 48 attributes and 63 properties
 * with 2-byte size fields in version 4 and later. */
#include "zint.h"

uint16_t obj_base;

#define WIDE (zver > 3)

/* 14 * o, out of line so that it stays a multiplication by a constant. */
static NOINLINE uint16_t mul14(uint16_t o) { return (o << 4) - (o << 1); }

INLINE uint16_t oaddr(uint16_t o) {
  if (!o)
    zfatal(ZM_ERR_OBJECT);
  return obj_base + (WIDE ? mul14(o) : (o << 3) + o);
}

/* Offset of a tree field in an entry. */
static uint8_t ofield(uint8_t f) { return WIDE ? 6 + (f << 1) : 4 + f; }

/* A tree field at address a. */
static uint16_t rdf(uint16_t a) { return WIDE ? zw(a) : zb(a); }

static void wrf(uint16_t a, uint16_t v) {
  if (WIDE)
    zsw(a, v);
  else
    zsb(a, v);
}

uint16_t obj_rel(uint16_t o, uint8_t f) {
  uint16_t a;
  if (!o)
    zfatal(ZM_ERR_OBJECT);
  if (!WIDE) {
    a = obj_base + (o << 3) + o + 4 + f;
    return a < pin_base ? dyn[a] : zb(a);
  }
  a = obj_base + mul14(o) + 6 + (f << 1);
  return a < pin_base - 1 ? dyn[a] << 8 | dyn[a + 1] : zw(a);
}

uint8_t obj_attr(uint16_t o, uint8_t a, uint8_t op) {
  uint16_t ad;
  uint8_t m, v, *p;
  if (a >= (WIDE ? 48 : 32)) {
    zfatal(ZM_ERR_OBJECT);
    return 0;
  }
  ad = oaddr(o) + (a >> 3);
  m = 0x80 >> (a & 7);
  if (ad < pin_base) {
    p = dyn + ad;
    v = *p;
    if (op == ATTR_SET)
      *p = v | m;
    else if (op == ATTR_CLEAR)
      *p = v & ~m;
    return v & m;
  }
  v = zb(ad);
  if (op == ATTR_SET)
    zsb(ad, v | m);
  else if (op == ATTR_CLEAR)
    zsb(ad, v & ~m);
  return v & m;
}

void obj_remove(uint16_t o) {
  uint16_t a = oaddr(o), p = rdf(a + ofield(OBJ_PARENT)), l, x, n = 0;
  if (!p)
    return;
  l = oaddr(p) + ofield(OBJ_CHILD);
  while ((x = rdf(l)) != o && x && --n)
    l = oaddr(x) + ofield(OBJ_SIBLING);
  if (x == o)
    wrf(l, rdf(a + ofield(OBJ_SIBLING)));
  wrf(a + ofield(OBJ_PARENT), 0);
  wrf(a + ofield(OBJ_SIBLING), 0);
}

void obj_insert(uint16_t o, uint16_t d) {
  uint16_t a, da;
  obj_remove(o);
  a = oaddr(o);
  da = oaddr(d) + ofield(OBJ_CHILD);
  wrf(a + ofield(OBJ_PARENT), d);
  wrf(a + ofield(OBJ_SIBLING), rdf(da));
  wrf(da, o);
}

static uint16_t prop_table(uint16_t o) {
  return zw(oaddr(o) + (WIDE ? 12 : 7));
}

uint16_t obj_name(uint16_t o) { return prop_table(o) + 1; }

static uint16_t prop_first(uint16_t o) {
  uint16_t a = prop_table(o);
  return a + 1 + (zb(a) << 1);
}

/* The property number of the size byte s. */
static uint8_t pnum(uint8_t s) { return s & (WIDE ? 63 : 31); }

/* The data address of the property whose size byte s is at a; its length
 * goes to plen. */
static uint8_t plen;
static uint16_t pdata(uint16_t a, uint8_t s) {
  if (!WIDE)
    plen = (s >> 5) + 1;
  else if (s & 0x80) {
    s = zb(++a) & 63;
    plen = s ? s : 64;
  } else
    plen = (s >> 6) + 1;
  return a + 1;
}

/* The data address of property p of object o (plen its length), 0 if it
 * has none. */
static uint16_t prop_find(uint16_t o, uint8_t p) {
  uint16_t a = prop_first(o);
  uint8_t s;
  while ((s = zb(a)) && pnum(s) > p)
    a = pdata(a, s) + plen;
  return s && pnum(s) == p ? pdata(a, s) : 0;
}

uint16_t prop_get(uint16_t o, uint8_t p) {
  uint16_t a = prop_find(o, p);
  if (!a)
    return zw(zw(H_OBJECTS) + (p << 1) - 2);
  return plen > 1 ? zw(a) : zb(a);
}

uint16_t prop_addr(uint16_t o, uint8_t p) { return prop_find(o, p); }

uint8_t prop_len(uint16_t a) {
  uint8_t s;
  if (!a)
    return 0;
  s = zb(a - 1);
  if (!WIDE)
    return (s >> 5) + 1;
  if (s & 0x80)
    return (s & 63) ? s & 63 : 64;
  return (s >> 6) + 1;
}

uint8_t prop_next(uint16_t o, uint8_t p) {
  uint16_t a;
  if (!p)
    a = prop_first(o);
  else if ((a = prop_find(o, p)))
    a += plen;
  else {
    zfatal(ZM_ERR_OBJECT);
    return 0;
  }
  return pnum(zb(a));
}

void prop_put(uint16_t o, uint8_t p, uint16_t v) {
  uint16_t a = prop_find(o, p);
  if (!a)
    zfatal(ZM_ERR_OBJECT);
  else if (plen > 1)
    zsw(a, v);
  else
    zsb(a, v);
}
