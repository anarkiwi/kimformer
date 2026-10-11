/* Operand decoding, opcodes, call stack, save/restore/restart. */
#include <string.h>

#include "zint.h"

#ifdef ZM_TRACE
#include <stdio.h>
uint8_t zm_trace;
#endif

#define SAVE_HDR 23
#define OP2(n) (n)
#define OP1(n) (0x20 + (n))
#define OP0(n) (0x30 + (n))
#define OPV(n) (0x40 + (n))
#define SLT(x, y) (((x) ^ 0x8000) < ((y) ^ 0x8000))

static struct {
  uint8_t magic[4], release[2], serial[6], checksum[2];
  uint16_t dsize, sp, fp;
  uint8_t pc[3];
} sh, rh;
static char sname[17];

static uint16_t zstack[ZSTACK_WORDS];
static uint16_t sp, fp, sbase, globals, rng = 1;
static uint16_t opnd[4];
static uint8_t nops;
static uint16_t op_page;
static uint8_t op_off;

static NOINLINE void push(uint16_t v) {
  if (sp >= ZSTACK_WORDS)
    zfatal(ZM_ERR_STACK);
  else
    zstack[sp++] = v;
}

static NOINLINE uint16_t *top(void) {
  if (sp <= sbase) {
    zfatal(ZM_ERR_STACK);
    return zstack;
  }
  return zstack + sp - 1;
}

static NOINLINE uint16_t pop(void) {
  uint16_t v = *top();
  if (sp > sbase)
    sp--;
  return v;
}

static NOINLINE uint16_t *local(uint8_t v) {
  uint16_t i = fp + v - 1;
  if (i >= sbase) {
    zfatal(ZM_ERR_STACK);
    i = 0;
  }
  return zstack + i;
}

static uint16_t frame_base(void) {
  return fp ? fp + (zstack[fp - 1] >> 12) : 0;
}

static uint16_t var_get(uint8_t v) {
  if (!v)
    return pop();
  if (v < 16)
    return *local(v);
  return zw(globals + ((v - 16) << 1));
}

static NOINLINE void var_set(uint8_t v, uint16_t x) {
  if (!v)
    push(x);
  else if (v < 16)
    *local(v) = x;
  else
    zsw(globals + ((v - 16) << 1), x);
}

static uint16_t ind_get(uint8_t v) { return v ? var_get(v) : *top(); }

static void ind_set(uint8_t v, uint16_t x) {
  if (v)
    var_set(v, x);
  else
    *top() = x;
}

static void store(uint16_t v) { var_set(pc_byte(), v); }

static void zret(uint16_t v) {
  uint16_t w;
  if (!fp) {
    zfatal(ZM_ERR_STACK);
    return;
  }
  sp = fp;
  fp = zstack[--sp] & 0xFFF;
  sbase = frame_base();
  w = zstack[--sp];
  pc_set(zstack[--sp], w);
  var_set(w >> 8, v);
}

static void branch(uint8_t cond) {
  uint8_t b = pc_byte();
  uint16_t off = b & 0x3F;
  if (!(b & 0x40)) {
    off = off << 8 | pc_byte();
    if (off & 0x2000)
      off |= 0xC000;
  }
  if (!(b & 0x80) == !cond) {
    if (off < 2)
      zret(off);
    else
      pc_rel(off - 2);
  }
}

static void zcall(void) {
  uint8_t v = pc_byte(), n, i;
  uint16_t w;
  if (!opnd[0]) {
    var_set(v, 0);
    return;
  }
  push(pc_page);
  push((uint16_t)v << 8 | pc_off);
  pc_set(opnd[0] >> 7, opnd[0] << 1);
  n = pc_byte();
  if (n > 15)
    zfatal(ZM_ERR_STACK);
  push(fp | (uint16_t)n << 12);
  fp = sp;
  for (i = 1; i <= n && zstate == ZS_RUN; i++) {
    w = (uint16_t)pc_byte() << 8;
    w |= pc_byte();
    push(i < nops ? opnd[i] : w);
  }
  sbase = sp;
}

static void zinit(void) {
  dyn[H_FLAGS1] = (dyn[H_FLAGS1] | 0x10) & ~0x20;
  dyn[0x1E] = 6;
  dyn[0x1F] = 'A';
  dyn[0x32] = 1;
  dyn[0x33] = 1;
  globals = zw(H_GLOBALS);
}

static void zstart(void) {
  uint16_t a = zw(H_PC);
  zinit();
  sp = fp = sbase = 0;
  zstreams_reset();
  pc_set(a >> 8, a);
}

static uint8_t zsave(uint8_t save) {
  uint8_t n, f2;
  for (n = 0; story_name[n] && story_name[n] != '.' && n < 12; n++)
    sname[n] = story_name[n];
  memcpy(sname + n, ".SAV", 5);
  zputs("File [");
  zputs(sname);
  zputs("]: ");
  n = zreadline((uint8_t *)sname, 16);
  if (n)
    sname[n] = 0;
  if (sv_open(sname, save))
    return 0;
  memcpy(sh.magic, "KZ3\1", 4);
  memcpy(sh.release, dyn + H_RELEASE, 2);
  memcpy(sh.serial, dyn + H_SERIAL, 6);
  memcpy(sh.checksum, dyn + H_CHECKSUM, 2);
  sh.dsize = static_base;
  sh.sp = sp;
  sh.fp = fp;
  sh.pc[0] = pc_page >> 8;
  sh.pc[1] = pc_page;
  sh.pc[2] = pc_off;
  if (save) {
    sv_xfer((uint8_t *)&sh, SAVE_HDR);
    sv_xfer(dyn, static_base);
    sv_xfer((uint8_t *)zstack, sp << 1);
    return !sv_close();
  }
  sv_xfer((uint8_t *)&rh, SAVE_HDR);
  for (n = 0; n < 16 && ((uint8_t *)&rh)[n] == ((uint8_t *)&sh)[n]; n++)
    ;
  if (n < 16 || rh.sp > ZSTACK_WORDS || rh.fp > rh.sp || (rh.fp && rh.fp < 3)) {
    sv_close();
    return 0;
  }
  f2 = dyn[H_FLAGS2 + 1] & 3;
  sv_xfer(dyn, static_base);
  sv_xfer((uint8_t *)zstack, rh.sp << 1);
  if (sv_close())
    zfatal(ZM_ERR_OPEN);
  dyn[H_FLAGS2 + 1] = (dyn[H_FLAGS2 + 1] & ~3) | f2;
  zinit();
  sp = rh.sp;
  fp = rh.fp;
  sbase = frame_base();
  pc_set((uint16_t)rh.pc[0] << 8 | rh.pc[1], rh.pc[2]);
  return 1;
}

static uint16_t operand(uint8_t t) {
  uint16_t w;
  if (!t) {
    w = (uint16_t)pc_byte() << 8;
    return w | pc_byte();
  }
  if (t == 1)
    return pc_byte();
  return var_get(pc_byte());
}

static void exec(uint8_t code) {
  uint16_t a = opnd[0], b = opnd[1], r = 0;
  uint8_t i;
  switch (code) {
  case OP2(1):
    for (i = 1; i < nops; i++)
      r |= a == opnd[i];
    goto br;
  case OP2(2):
    r = SLT(a, b);
    goto br;
  case OP2(3):
    r = SLT(b, a);
    goto br;
  case OP2(4):
  case OP2(5):
    r = ind_get(a) + (code == OP2(4) ? -1 : 1);
    ind_set(a, r);
    r = code == OP2(4) ? SLT(r, b) : SLT(b, r);
    goto br;
  case OP2(6):
    r = obj_rel(a, OBJ_PARENT) == b;
    goto br;
  case OP2(7):
    r = (a & b) == b;
    goto br;
  case OP2(8):
    r = a | b;
    goto st;
  case OP2(9):
    r = a & b;
    goto st;
  case OP2(10):
  case OP2(11):
  case OP2(12):
    r = obj_attr(a, b, code - OP2(10));
    if (code == OP2(10))
      goto br;
    return;
  case OP2(13):
    ind_set(a, b);
    return;
  case OP2(14):
    obj_insert(a, b);
    return;
  case OP2(15):
    r = zw(a + (b << 1));
    goto st;
  case OP2(16):
    r = zb(a + b);
    goto st;
  case OP2(17):
    r = prop_get(a, b);
    goto st;
  case OP2(18):
    r = prop_addr(a, b);
    goto st;
  case OP2(19):
    r = prop_next(a, b);
    goto st;
  case OP2(20):
    r = a + b;
    goto st;
  case OP2(21):
    r = a - b;
    goto st;
  case OP2(22):
    r = (unsigned)a * b;
    goto st;
  case OP2(23):
  case OP2(24):
    if (!b) {
      zfatal(ZM_ERR_OPCODE);
      return;
    }
    if (b == 0xFFFF)
      r = code == OP2(23) ? -a : 0;
    else
      r = code == OP2(23) ? (int16_t)a / (int16_t)b : (int16_t)a % (int16_t)b;
    goto st;
  case OP1(0):
    r = !a;
    goto br;
  case OP1(1):
  case OP1(2):
    r = obj_rel(a, code == OP1(1) ? OBJ_SIBLING : OBJ_CHILD);
    store(r);
    goto br;
  case OP1(3):
    r = obj_rel(a, OBJ_PARENT);
    goto st;
  case OP1(4):
    r = prop_len(a);
    goto st;
  case OP1(5):
  case OP1(6):
    ind_set(a, ind_get(a) + (code == OP1(5) ? 1 : -1));
    return;
  case OP1(7):
    str_set(a >> 8, a);
    zprint_str();
    return;
  case OP1(9):
    obj_remove(a);
    return;
  case OP1(10):
    a = obj_name(a);
    str_set(a >> 8, a);
    zprint_str();
    return;
  case OP1(11):
    zret(a);
    return;
  case OP1(12):
    pc_rel(a - 2);
    return;
  case OP1(13):
    str_set(a >> 7, a << 1);
    zprint_str();
    return;
  case OP1(14):
    r = ind_get(a);
    goto st;
  case OP1(15):
    r = ~a;
    goto st;
  case OP0(0):
  case OP0(1):
    zret(code == OP0(0));
    return;
  case OP0(2):
  case OP0(3):
    str_set(pc_page, pc_off);
    zprint_str();
    pc_page = str_page;
    pc_off = str_off;
    pc_hi = str_hi;
    if (code == OP0(3)) {
      zputc(ZNL);
      zret(1);
    }
    return;
  case OP0(4):
  case OP0(12):
  case OPV(10):
  case OPV(11):
  case OPV(20):
  case OPV(21):
    return;
  case OP0(5):
  case OP0(6):
    r = zsave(code == OP0(5));
    goto br;
  case OP0(7):
    if (!zreload())
      zfatal(ZM_ERR_OPEN);
    zstart();
    return;
  case OP0(8):
    zret(pop());
    return;
  case OP0(9):
    pop();
    return;
  case OP0(10):
    zstate = ZM_OK;
    return;
  case OP0(11):
    zputc(ZNL);
    return;
  case OP0(13):
    r = zverify();
    goto br;
  case OPV(0):
    zcall();
    return;
  case OPV(1):
    zsw(a + (b << 1), opnd[2]);
    return;
  case OPV(2):
    zsb(a + b, opnd[2]);
    return;
  case OPV(3):
    prop_put(a, b, opnd[2]);
    return;
  case OPV(4):
    zsread(a, b);
    return;
  case OPV(5):
    zputc(a);
    return;
  case OPV(6):
    zprint_num(a);
    return;
  case OPV(7):
    if (a & 0x8000 || !a) {
      rng = a ? a : plat_entropy() | 1;
      goto st;
    }
    rng ^= rng << 7;
    rng ^= rng >> 9;
    rng ^= rng << 8;
    r = rng % a + 1;
    goto st;
  case OPV(8):
    push(a);
    return;
  case OPV(9):
    ind_set(a, pop());
    return;
  case OPV(19):
    zstream(a, b);
    return;
  default:
    zfatal(ZM_ERR_OPCODE);
    return;
  }
st:
  store(r);
  return;
br:
  branch(r);
}

uint8_t zm_run(void) {
  uint8_t op, code, t, i;
  zstate = ZS_RUN;
  zstart();
  while (zstate == ZS_RUN) {
    op_page = pc_page;
    op_off = pc_off;
    op = pc_byte();
    if (op < 0x80) {
      opnd[0] = operand(op & 0x40 ? 2 : 1);
      opnd[1] = operand(op & 0x20 ? 2 : 1);
      nops = 2;
      code = OP2(op & 0x1F);
    } else if (op < 0xC0) {
      t = op >> 4 & 3;
      nops = t != 3;
      if (nops)
        opnd[0] = operand(t);
      code = (nops ? OP1(0) : OP0(0)) + (op & 0x0F);
    } else {
      t = pc_byte();
      for (nops = 0, i = 0; i < 4 && (t & 0xC0) != 0xC0; i++, t <<= 2)
        opnd[nops++] = operand(t >> 6);
      code = (op & 0x20 ? OPV(0) : OP2(0)) + (op & 0x1F);
    }
#ifdef ZM_TRACE
    if (zm_trace) {
      fprintf(stderr, "%03X%02X %02X", op_page, op_off, op);
      for (i = 0; i < nops; i++)
        fprintf(stderr, " %04X", opnd[i]);
      fputc('\n', stderr);
    }
#endif
    exec(code);
  }
  if (zstate != ZM_OK) {
    zstreams_reset();
    zputs("\nError ");
    zprint_num(zstate);
    zputs(" at $");
    zprint_hex(op_page, 3);
    zprint_hex(op_off, 2);
    zputc(ZNL);
  }
  zflush();
  return zstate;
}
