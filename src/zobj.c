/* Object tree and properties. */
#include "zint.h"

uint16_t obj_base;

static uint16_t oaddr(uint16_t o) {
  if (!o)
    zfatal(ZM_ERR_OBJECT);
  return obj_base + (o << 3) + o;
}

/* The object entry at a when it lies in dynamic memory, else 0. */
static uint8_t *odyn(uint16_t a) {
  return a < static_base && static_base - a >= 9 ? dyn + a : 0;
}

uint8_t obj_rel(uint16_t o, uint8_t field) {
  uint16_t a = oaddr(o);
  uint8_t *p = odyn(a);
  return p ? p[field] : zb(a + field);
}

uint8_t obj_attr(uint16_t o, uint8_t a, uint8_t op) {
  uint16_t ad;
  uint8_t m, v, *p;
  if (a > 31) {
    zfatal(ZM_ERR_OBJECT);
    return 0;
  }
  ad = oaddr(o);
  p = odyn(ad);
  ad += a >> 3;
  m = 0x80 >> (a & 7);
  if (p) {
    p += a >> 3;
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
  uint16_t a = oaddr(o), l;
  uint8_t p = zb(a + OBJ_PARENT), s = zb(a + OBJ_SIBLING), x, n;
  if (!p)
    return;
  l = oaddr(p) + OBJ_CHILD;
  for (n = 255; (x = zb(l)) != o && x && n; n--)
    l = oaddr(x) + OBJ_SIBLING;
  if (x == o)
    zsb(l, s);
  zsb(a + OBJ_PARENT, 0);
  zsb(a + OBJ_SIBLING, 0);
}

void obj_insert(uint16_t o, uint16_t d) {
  uint16_t a, da;
  obj_remove(o);
  a = oaddr(o);
  da = oaddr(d) + OBJ_CHILD;
  zsb(a + OBJ_PARENT, d);
  zsb(a + OBJ_SIBLING, zb(da));
  zsb(da, o);
}

uint16_t obj_name(uint16_t o) { return zw(oaddr(o) + 7) + 1; }

static uint16_t prop_first(uint16_t o) {
  uint16_t a = zw(oaddr(o) + 7);
  return a + 1 + (zb(a) << 1);
}

static uint16_t prop_find(uint16_t o, uint8_t p) {
  uint16_t a = prop_first(o);
  uint8_t s;
  while ((s = zb(a)) && (s & 31) > p)
    a += (s >> 5) + 2;
  return s && (s & 31) == p ? a : 0;
}

uint16_t prop_get(uint16_t o, uint8_t p) {
  uint16_t a = prop_find(o, p);
  if (!a)
    return zw(zw(H_OBJECTS) + (p << 1) - 2);
  return zb(a) >> 5 ? zw(a + 1) : zb(a + 1);
}

uint16_t prop_addr(uint16_t o, uint8_t p) {
  uint16_t a = prop_find(o, p);
  return a ? a + 1 : 0;
}

uint8_t prop_len(uint16_t a) { return a ? (zb(a - 1) >> 5) + 1 : 0; }

uint8_t prop_next(uint16_t o, uint8_t p) {
  uint16_t a;
  if (!p)
    a = prop_first(o);
  else if ((a = prop_find(o, p)))
    a += (zb(a) >> 5) + 2;
  else {
    zfatal(ZM_ERR_OBJECT);
    return 0;
  }
  return zb(a) & 31;
}

void prop_put(uint16_t o, uint8_t p, uint16_t v) {
  uint16_t a = prop_find(o, p);
  if (!a)
    zfatal(ZM_ERR_OBJECT);
  else if (zb(a) >> 5)
    zsw(a + 1, v);
  else
    zsb(a + 1, v);
}
