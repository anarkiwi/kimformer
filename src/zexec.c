/* Operand decoding, opcodes, call stack, save/restore/restart. */
#include <string.h>

#include "zint.h"

#ifdef ZM_TRACE
#include <stdio.h>
uint8_t zm_trace;
#endif

#define OP2(n) (n)
#define OP1(n) (0x20 + (n))
#define OP0(n) (0x30 + (n))
#define OPV(n) (0x40 + (n))
#define SLT(x, y) ((int16_t)(x) < (int16_t)(y))
#define CS_BYTES 1536
#define A opnd[0]
#define B opnd[1]
#define FRAME_HDR 6

/* Save file header: the first 16 bytes identify the story. */
static struct {
  uint8_t magic[4], release[2], serial[6], checksum[2];
  uint16_t dsize, csp;
  uint8_t esp, esb, nloc, pc[3];
} sh, rh;
static char sname[17];

/* Evaluation stack: word i is es_hi[i] << 8 | es_lo[i], esp words in use,
 * the current routine's from esb.  Locals 1..nloc of the current routine are
 * lv_lo/lv_hi[1..nloc].  A call pushes onto the byte stack cs the caller's
 * locals (low, high byte each), then its PC page (low, high), PC offset,
 * store variable, esb and nloc. */
static uint8_t es_lo[256], es_hi[256];
static uint8_t lv_lo[16], lv_hi[16];
static uint8_t cs[CS_BYTES];
static uint8_t esp ZP, esb ZP, nloc ZP;
static uint16_t csp;
static uint16_t opnd[4] ZP;
static uint8_t nops ZP;
static uint8_t *gv_lo ZP, *gv_hi ZP;
static uint8_t gmax;
static uint16_t globals, rng;
static uint16_t op_page ZP;
static uint8_t op_off ZP;

static NOINLINE void push(uint16_t v) {
  uint8_t i = esp;
  if (i == 0xFF) {
    zfatal(ZM_ERR_STACK);
    return;
  }
  es_lo[i] = v;
  es_hi[i] = v >> 8;
  esp = i + 1;
}

/* Index of the top of the evaluation stack, 0 (and an error) if empty. */
static uint8_t top(void) {
  if (esp <= esb) {
    zfatal(ZM_ERR_STACK);
    return 0;
  }
  return esp - 1;
}

static NOINLINE uint16_t pop(void) {
  uint8_t i = top();
  if (ZRUNNING)
    esp = i;
  return es_hi[i] << 8 | es_lo[i];
}

/* Global v (16-gmax) is at gv_lo[2v] for v < 128, gv_hi[2v - 256] above. */
INLINE uint8_t *gvar(uint8_t v) { return v & 0x80 ? gv_hi : gv_lo; }

static NOINLINE uint16_t var_get(uint8_t v) {
  uint8_t i, *g;
  if (v >= 16) {
    if (v > gmax)
      return zw(globals + ((v - 16) << 1));
    g = gvar(v);
    i = v << 1;
    return g[i] << 8 | g[(uint8_t)(i + 1)];
  }
  if (!v)
    return pop();
  if (v > nloc) {
    zfatal(ZM_ERR_STACK);
    return 0;
  }
  return lv_hi[v] << 8 | lv_lo[v];
}

static NOINLINE void var_set(uint8_t v, uint16_t x) {
  uint8_t i, *g;
  if (v >= 16) {
    if (v > gmax) {
      zsw(globals + ((v - 16) << 1), x);
      return;
    }
    g = gvar(v);
    i = v << 1;
    g[i] = x >> 8;
    g[(uint8_t)(i + 1)] = x;
  } else if (!v)
    push(x);
  else if (v > nloc)
    zfatal(ZM_ERR_STACK);
  else {
    lv_lo[v] = x;
    lv_hi[v] = x >> 8;
  }
}

/* var_get with the locals inline. */
INLINE uint16_t var_fast(uint8_t v) {
  if ((uint8_t)(v - 1) < nloc)
    return lv_hi[v] << 8 | lv_lo[v];
  return var_get(v);
}

static uint16_t ind_get(uint8_t v) {
  uint8_t i;
  if (v)
    return var_get(v);
  i = top();
  return es_hi[i] << 8 | es_lo[i];
}

static void ind_set(uint8_t v, uint16_t x) {
  uint8_t i;
  if (v)
    var_set(v, x);
  else {
    i = top();
    es_lo[i] = x;
    es_hi[i] = x >> 8;
  }
}

INLINE void store(uint16_t x) {
  uint8_t v = pc_byte();
  if ((uint8_t)(v - 1) < nloc) {
    lv_lo[v] = x;
    lv_hi[v] = x >> 8;
  } else
    var_set(v, x);
}

static void zret(uint16_t x) {
  uint8_t *p, n, v, i, j;
  if (csp < FRAME_HDR || (n = cs[csp - 1]) > 15 ||
      csp < (uint16_t)(FRAME_HDR + (n << 1)) || cs[csp - 2] > esb) {
    zfatal(ZM_ERR_STACK);
    return;
  }
  csp -= FRAME_HDR + (n << 1);
  p = cs + csp;
  for (i = 1, j = 0; i <= n; i++, j += 2) {
    lv_lo[i] = p[j];
    lv_hi[i] = p[j + 1];
  }
  nloc = n;
  esp = esb;
  esb = p[j + 4];
  v = p[j + 3];
  pc_set(p[j + 1] << 8 | p[j], p[j + 2]);
  var_set(v, x);
}

/* Branch data after a test with result cond: bit 7 the sense, bit 6 a
 * 6-bit offset, else a 14-bit signed offset; 0 and 1 return. */
static void branch(uint8_t cond) {
  uint8_t b = pc_byte(), hi = 0, lo = b & 0x3F;
  if (!(b & 0x40)) {
    hi = lo;
    if (hi & 0x20)
      hi |= 0xC0;
    lo = pc_byte();
  }
  if (!cond)
    b ^= 0x80;
  if (b & 0x80) {
    if (!hi && lo < 2)
      zret(lo);
    else
      pc_rel((hi << 8 | lo) - 2);
  }
}

static void zcall(void) {
  uint8_t v = pc_byte(), n, i, j, *p;
  uint16_t w;
  if (!opnd[0]) {
    var_set(v, 0);
    return;
  }
  if (csp + FRAME_HDR + (nloc << 1) > CS_BYTES) {
    zfatal(ZM_ERR_STACK);
    return;
  }
  p = cs + csp;
  for (i = 1, j = 0; i <= nloc; i++, j += 2) {
    p[j] = lv_lo[i];
    p[j + 1] = lv_hi[i];
  }
  p[j] = pc_page;
  p[j + 1] = pc_page >> 8;
  p[j + 2] = pc_off;
  p[j + 3] = v;
  p[j + 4] = esb;
  p[j + 5] = nloc;
  csp += j + FRAME_HDR;
  esb = esp;
  pc_set(opnd[0] >> 7, opnd[0] << 1);
  n = pc_byte();
  if (n > 15) {
    zfatal(ZM_ERR_STACK);
    n = 0;
  }
  nloc = n;
  for (i = 1; i <= n; i++) {
    w = pc_word();
    if (i < nops)
      w = opnd[i];
    lv_lo[i] = w;
    lv_hi[i] = w >> 8;
  }
}

static void zinit(void) {
  dyn[H_FLAGS1] = (dyn[H_FLAGS1] & ~0x30) | (PLAT_STATUS ? 0 : 0x10);
  dyn[0x1E] = 6;
  dyn[0x1F] = 'A';
  dyn[0x32] = 1;
  dyn[0x33] = 1;
  globals = zw(H_GLOBALS);
  obj_base = zw(H_OBJECTS) + 53;
  gmax = 15;
  if (globals >= 32 && globals < static_base) {
    gv_lo = dyn + globals - 32;
    gv_hi = gv_lo + 0x100;
    gmax = (static_base - globals) >> 1 > 240
               ? 255
               : 15 + ((static_base - globals) >> 1);
  }
}

/* An RNG seed from the platform: xorshift needs a nonzero state. */
static uint16_t zseed(void) {
  uint16_t e = plat_entropy();
  return e ? e : 0xACE1;
}

static void zstart(void) {
  uint16_t a = zw(H_PC);
  zinit();
  esp = esb = nloc = 0;
  csp = 0;
  zstreams_reset();
  pc_set(a >> 8, a);
}

/* Moves the evaluation stack, the locals and the call stack to or from the
 * save file. */
static void stack_xfer(void) {
  sv_xfer(es_lo, esp);
  sv_xfer(es_hi, esp);
  sv_xfer(lv_lo, sizeof lv_lo);
  sv_xfer(lv_hi, sizeof lv_hi);
  sv_xfer(cs, csp);
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
  memcpy(sh.magic, "KZ3\2", 4);
  memcpy(sh.release, dyn + H_RELEASE, 2);
  memcpy(sh.serial, dyn + H_SERIAL, 6);
  memcpy(sh.checksum, dyn + H_CHECKSUM, 2);
  sh.dsize = static_base;
  sh.csp = csp;
  sh.esp = esp;
  sh.esb = esb;
  sh.nloc = nloc;
  sh.pc[0] = pc_page >> 8;
  sh.pc[1] = pc_page;
  sh.pc[2] = pc_off;
  if (save) {
    sv_xfer((uint8_t *)&sh, sizeof sh);
    sv_xfer(dyn, static_base);
    stack_xfer();
    return !sv_close();
  }
  sv_xfer((uint8_t *)&rh, sizeof rh);
  for (n = 0; n < 16 && ((uint8_t *)&rh)[n] == ((uint8_t *)&sh)[n]; n++)
    ;
  if (n < 16 || rh.csp > CS_BYTES || rh.esb > rh.esp || rh.nloc > 15) {
    sv_close();
    return 0;
  }
  f2 = dyn[H_FLAGS2 + 1] & 3;
  sv_xfer(dyn, static_base);
  csp = rh.csp;
  esp = rh.esp;
  esb = rh.esb;
  nloc = rh.nloc;
  stack_xfer();
  if (sv_close())
    zfatal(ZM_ERR_OPEN);
  dyn[H_FLAGS2 + 1] = (dyn[H_FLAGS2 + 1] & ~3) | f2;
  zinit();
  pc_set((uint16_t)rh.pc[0] << 8 | rh.pc[1], rh.pc[2]);
  return 1;
}

/* An operand of type t: 0 large constant, 1 small constant, 2 variable. */
static uint16_t operand(uint8_t t) {
  if (!t)
    return pc_word();
  if (t == 1)
    return pc_byte();
  return var_fast(pc_byte());
}

static void exec(uint8_t code) {
  uint16_t r = 0;
  uint8_t c;
  switch (code) {
  case OP2(1):
    c = A == B || (nops > 2 && (A == opnd[2] || (nops > 3 && A == opnd[3])));
    goto br;
  case OP2(2):
    c = SLT(A, B);
    goto br;
  case OP2(3):
    c = SLT(B, A);
    goto br;
  case OP2(4):
    r = ind_get(A) - 1;
    ind_set(A, r);
    c = SLT(r, B);
    goto br;
  case OP2(5):
    r = ind_get(A) + 1;
    ind_set(A, r);
    c = SLT(B, r);
    goto br;
  case OP2(6):
    c = obj_rel(A, OBJ_PARENT) == B;
    goto br;
  case OP2(7):
    c = (A & B) == B;
    goto br;
  case OP2(8):
    r = A | B;
    goto st;
  case OP2(9):
    r = A & B;
    goto st;
  case OP2(10):
  case OP2(11):
  case OP2(12):
    c = obj_attr(A, B, code - OP2(10));
    if (code == OP2(10))
      goto br;
    return;
  case OP2(13):
    ind_set(A, B);
    return;
  case OP2(14):
    obj_insert(A, B);
    return;
  case OP2(15):
    r = A + (B << 1);
    r = r < static_base - 1 ? dyn[r] << 8 | dyn[r + 1] : zw(r);
    goto st;
  case OP2(16):
    r = A + B;
    r = r < static_base ? dyn[r] : zb(r);
    goto st;
  case OP2(17):
    r = prop_get(A, B);
    goto st;
  case OP2(18):
    r = prop_addr(A, B);
    goto st;
  case OP2(19):
    r = prop_next(A, B);
    goto st;
  case OP2(20):
    r = A + B;
    goto st;
  case OP2(21):
    r = A - B;
    goto st;
  case OP2(22):
    r = umul(A, B);
    goto st;
  case OP2(23):
  case OP2(24):
    if (!B) {
      zfatal(ZM_ERR_OPCODE);
      return;
    }
    if (B == 0xFFFF)
      r = code == OP2(23) ? -A : 0;
    else
      r = code == OP2(23) ? (int16_t)A / (int16_t)B : (int16_t)A % (int16_t)B;
    goto st;
  case OP1(0):
    c = !A;
    goto br;
  case OP1(1):
  case OP1(2):
    c = obj_rel(A, code == OP1(1) ? OBJ_SIBLING : OBJ_CHILD);
    store(c);
    goto br;
  case OP1(3):
    r = obj_rel(A, OBJ_PARENT);
    goto st;
  case OP1(4):
    r = prop_len(A);
    goto st;
  case OP1(5):
  case OP1(6):
    ind_set(A, ind_get(A) + (code == OP1(5) ? 1 : -1));
    return;
  case OP1(7):
    str_set(A >> 8, A);
    zprint_str();
    return;
  case OP1(9):
    obj_remove(A);
    return;
  case OP1(10):
    r = obj_name(A);
    str_set(r >> 8, r);
    zprint_str();
    return;
  case OP1(11):
    zret(A);
    return;
  case OP1(12):
    pc_rel(A - 2);
    return;
  case OP1(13):
    str_set(A >> 7, A << 1);
    zprint_str();
    return;
  case OP1(14):
    r = ind_get(A);
    goto st;
  case OP1(15):
    r = ~A;
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
    pc_load();
    if (code == OP0(3)) {
      zputc(ZNL);
      zret(1);
    }
    return;
  case OP0(12):
    if (PLAT_STATUS)
      zstatus();
    return;
  case OP0(4):
  case OPV(10):
  case OPV(11):
  case OPV(20):
  case OPV(21):
    return;
  case OP0(5):
  case OP0(6):
    c = zsave(code == OP0(5));
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
    c = zverify();
    goto br;
  case OPV(0):
    zcall();
    return;
  case OPV(1):
    zsw(A + (B << 1), opnd[2]);
    return;
  case OPV(2):
    zsb(A + B, opnd[2]);
    return;
  case OPV(3):
    prop_put(A, B, opnd[2]);
    return;
  case OPV(4):
    zsread(A, B);
    return;
  case OPV(5):
    zputc(A);
    return;
  case OPV(6):
    zprint_num(A);
    return;
  case OPV(7):
    if (A & 0x8000 || !A) {
      rng = A ? A : zseed();
      goto st;
    }
    rng ^= rng << 7;
    rng ^= rng >> 9;
    rng ^= rng << 8;
    r = rng % A + 1;
    goto st;
  case OPV(8):
    push(A);
    return;
  case OPV(9):
    ind_set(A, pop());
    return;
  case OPV(19):
    zstream(A, B);
    return;
  default:
    zfatal(ZM_ERR_OPCODE);
    return;
  }
st:
  store(r);
  return;
br:
  branch(c);
}

/* After the PC ran past the story: an error unless it has come back. */
static NOINLINE uint8_t zresume(void) {
  if (zstate == ZS_PAST)
    zstate = pc_page < story_pages ? ZS_RUN : ZM_ERR_ADDRESS;
  return zstate == ZS_RUN;
}

uint8_t zm_run(void) {
  uint8_t op, code, t, v;
  uint16_t w;
  zstate = ZS_RUN;
  rng = zseed();
  zstart();
  for (;;) {
    op_page = pc_page;
    op_off = pc_off;
    if (zstate != ZS_RUN && !zresume())
      break;
    op = pc_byte();
    if (op < 0x80) {
      w = pc_word();
      v = w >> 8;
      opnd[0] = op & 0x40 ? var_fast(v) : v;
      v = w;
      opnd[1] = op & 0x20 ? var_fast(v) : v;
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
      nops = 0;
      if (t < 0xC0) {
        opnd[0] = operand(t >> 6);
        nops = 1;
        if ((t & 0x30) != 0x30) {
          opnd[1] = operand(t >> 4 & 3);
          nops = 2;
          if ((t & 0x0C) != 0x0C) {
            opnd[2] = operand(t >> 2 & 3);
            nops = 3;
            if ((t & 3) != 3) {
              opnd[3] = operand(t & 3);
              nops = 4;
            }
          }
        }
      }
      code = (op & 0x20 ? OPV(0) : OP2(0)) + (op & 0x1F);
    }
#ifdef ZM_TRACE
    if (zm_trace) {
      fprintf(stderr, "%03X%02X %02X", op_page, op_off, op);
      for (t = 0; t < nops; t++)
        fprintf(stderr, " %04X", opnd[t]);
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
