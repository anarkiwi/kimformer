/* ZSCII decoding, abbreviations, output wrapping, input, tokenising. */
#include "zint.h"

uint8_t zm_width = 80;

#define STATUS_LEFT 64
#define STATUS_RIGHT 24

static const char alpha[] = "abcdefghijklmnopqrstuvwxyz"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "  0123456789.,!?_#'\"/\\-:()";

static uint8_t col, pend, wlen, wbuf[64];
static uint8_t screen_off, nstream3;
static char *cap;
static uint8_t ncap, capmax;
static uint16_t s3_table[16], s3_len[16];

static void emit_word(void) {
  uint8_t i;
  if (col && (uint16_t)col + pend + wlen > zm_width) {
    plat_putc('\n');
    col = pend = 0;
  }
  for (; pend; pend--, col++)
    plat_putc(' ');
  for (i = 0; i < wlen; i++)
    plat_putc(wbuf[i]);
  col += wlen;
  wlen = 0;
}

void zflush(void) { emit_word(); }

void zputc(uint8_t c) {
  if (cap) {
    if (ncap < capmax && c >= 32 && c < 127)
      cap[ncap++] = c;
    return;
  }
  if (nstream3) {
    uint8_t k = nstream3 - 1;
    zsb(s3_table[k] + 2 + s3_len[k]++, c);
    return;
  }
  if (screen_off)
    return;
  if (c == ZNL || c == ' ') {
    if (wlen)
      emit_word();
    if (c == ' ') {
      if (pend < zm_width)
        pend++;
    } else {
      plat_putc('\n');
      col = pend = 0;
    }
    return;
  }
  if (c < 32 || c > 126) {
    if (!c)
      return;
    c = '?';
  }
  wbuf[wlen++] = c;
  if (wlen == sizeof wbuf)
    emit_word();
}

void zputs(const char *s) {
  for (; *s; s++)
    zputc(*s == '\n' ? ZNL : (uint8_t)*s);
}

void zprint_num(uint16_t v) {
  static const uint16_t pow10[]
      __attribute__((section(".rodata.pow10"))) = {10000, 1000, 100, 10, 1};
  uint8_t i, d, lead = 1;
  if (v & 0x8000) {
    zputc('-');
    v = -v;
  }
  for (i = 0; i < 5; i++) {
    for (d = '0'; v >= pow10[i]; d++)
      v -= pow10[i];
    if (d != '0' || i == 4)
      lead = 0;
    if (!lead)
      zputc(d);
  }
}

void zprint_hex(uint16_t v, uint8_t digits) {
  while (digits--)
    zputc("0123456789ABCDEF"[(v >> (digits << 2)) & 15]);
}

static uint8_t str_byte(void) {
  uint8_t b;
  if (!str_hi)
    str_hi = zpage(str_page);
  b = PAGE_PTR(str_hi)[str_off];
  if (!++str_off) {
    str_page++;
    str_hi = 0;
  }
  return b;
}

/* Decodes the Z-string at the string position: 3 Z-characters a word, the
 * last word with bit 15 set; an abbreviation's string is decoded in the
 * middle of its word, which is resumed after it. */
void zprint_str(void) {
  uint8_t zc[3], sv[3] = {0, 0, 0}, k = 3, sk = 0, c, b, alph = 0, st = 0,
                 high = 0;
  uint8_t nest = 0, end = 0, sv_end = 0, sv_off = 0;
  uint16_t sv_page = 0, a;
  for (;;) {
    if (k == 3) {
      if (end) {
        if (!nest)
          break;
        nest = alph = 0;
        str_set(sv_page, sv_off);
        zc[0] = sv[0];
        zc[1] = sv[1];
        zc[2] = sv[2];
        k = sk;
        end = sv_end;
        continue;
      }
      if (!ZRUNNING)
        break;
      b = str_byte();
      end = b & 0x80;
      zc[0] = b >> 2 & 31;
      zc[1] = b << 3 & 24;
      b = str_byte();
      zc[1] |= b >> 5;
      zc[2] = b & 31;
      k = 0;
    }
    c = zc[k++];
    if (st) {
      if (st < 4) {
        if (!nest) {
          sv_page = str_page;
          sv_off = str_off;
          sv[0] = zc[0];
          sv[1] = zc[1];
          sv[2] = zc[2];
          sk = k;
          sv_end = end;
          a = zw(zw(H_ABBREV) + ((st - 1) << 6) + (c << 1));
          str_set(a >> 7, a << 1);
          nest = 1;
          k = 3;
          end = 0;
        }
        st = 0;
      } else if (st == 4) {
        high = c;
        st = 5;
      } else {
        zputc(high << 5 | c);
        st = 0;
      }
      continue;
    }
    if (!c)
      zputc(' ');
    else if (c < 4)
      st = c;
    else if (c < 6)
      alph = c == 4 ? 26 : 52;
    else {
      if (alph == 52 && c == 6)
        st = 4;
      else if (alph == 52 && c == 7)
        zputc(ZNL);
      else
        zputc(alpha[alph + c - 6]);
      alph = 0;
    }
  }
}

uint8_t zreadline(uint8_t *dst, uint8_t max) {
  uint8_t n = 0, c;
  zflush();
  for (;;) {
    c = plat_getc();
    if (c == '\r' || c == '\n')
      break;
    if (c == 8 || c == 127) {
      if (n) {
        n--;
        plat_putc(' ');
        plat_putc(8);
      }
    } else if (c >= 32 && c < 127 && n < max)
      dst[n++] = c;
  }
  col = 0;
  return n;
}

static uint16_t dict, dict_seps;
static uint8_t nseps;

static uint8_t is_sep(uint8_t c) {
  uint8_t i;
  for (i = 0; i < nseps; i++)
    if (zb(dict_seps + i) == c)
      return 1;
  return 0;
}

static uint16_t lookup(const uint8_t *s, uint8_t len) {
  uint8_t zc[9], k = 0, key[4], elen, j, c;
  uint16_t lo = 0, hi, mid, e, base;
  for (j = 0; j < len && k < 6; j++) {
    c = s[j];
    if (c >= 'a' && c <= 'z')
      zc[k++] = c - 'a' + 6;
    else {
      zc[k++] = 5;
      for (e = 54; e < 78 && alpha[e] != c; e++)
        ;
      if (e < 78)
        zc[k++] = e - 46;
      else {
        zc[k++] = 6;
        zc[k++] = c >> 5;
        zc[k++] = c & 31;
      }
    }
  }
  while (k < 6)
    zc[k++] = 5;
  key[0] = zc[0] << 2 | zc[1] >> 3;
  key[1] = zc[1] << 5 | zc[2];
  key[2] = 0x80 | zc[3] << 2 | zc[4] >> 3;
  key[3] = zc[4] << 5 | zc[5];
  elen = zb(dict_seps + nseps);
  hi = zw(dict_seps + nseps + 1);
  base = dict_seps + nseps + 3;
  while (lo < hi) {
    mid = (lo + hi) >> 1;
    e = base + umul(mid, elen);
    for (j = 0; j < 4 && (c = zb(e + j)) == key[j]; j++)
      ;
    if (j == 4)
      return e;
    if (c < key[j])
      lo = mid + 1;
    else
      hi = mid;
  }
  return 0;
}

void zsread(uint16_t text, uint16_t parse) {
  uint8_t *t, max = zb(text), maxw = zb(parse), i, s, c, nw = 0;
  uint16_t e;
#if ZM_PAGE_DYN
  uint8_t *b;
  zdyn(text, max + 1);
  zdyn(parse, 2 + (maxw << 2));
#else
  uint8_t *p;
  t = zdyn(text, max + 1);
  p = zdyn(parse, 2 + (maxw << 2));
#endif
  if (!ZRUNNING)
    return;
  if (PLAT_STATUS) {
    zflush();
    zstatus();
  }
#ifdef ZM_STATS
  plat_stats();
#endif
#if ZM_PAGE_DYN
  t = b = zdynw(text, max + 1);
  if (!b && !(t = zscratch()))
    t = PAGE_PTR(str_hi = zframe());
#endif
  i = max ? zreadline(t + 1, max - 1) : 0;
  t[i + 1] = 0;
  for (i = 1; (c = t[i]); i++)
    if (c >= 'A' && c <= 'Z')
      t[i] = c + 32;
#if ZM_PAGE_DYN
  for (s = 0; !b && s < i; s++)
    zsb(text + 1 + s, t[1 + s]);
#endif
  dict = zw(H_DICT);
  nseps = zb(dict);
  dict_seps = dict + 1;
  for (i = 1; (c = t[i]);) {
    if (c == ' ') {
      i++;
      continue;
    }
    s = i++;
    if (!is_sep(c))
      while ((c = t[i]) && c != ' ' && !is_sep(c))
        i++;
    if (nw < maxw) {
      e = lookup(t + s, i - s);
#if ZM_PAGE_DYN
      zsw(parse + 2 + (nw << 2), e);
      zsb(parse + 4 + (nw << 2), i - s);
      zsb(parse + 5 + (nw << 2), s);
#else
      p += 4;
      p[-2] = e >> 8;
      p[-1] = e;
      p[0] = i - s;
      p[1] = s;
#endif
      nw++;
    }
  }
#if ZM_PAGE_DYN
  zsb(parse + 1, nw);
#else
  zdyn(parse, 2)[1] = nw;
#endif
}

void zstream(int16_t n, uint16_t table) {
  switch (n) {
  case 1:
  case -1:
    screen_off = n < 0;
    break;
  case 2:
    dyn[H_FLAGS2 + 1] |= 1;
    break;
  case -2:
    dyn[H_FLAGS2 + 1] &= ~1;
    break;
  case 3:
    if (nstream3 == 16)
      zfatal(ZM_ERR_STACK);
    else {
      s3_table[nstream3] = table;
      s3_len[nstream3++] = 0;
    }
    break;
  case -3:
    if (nstream3) {
      nstream3--;
      zsw(s3_table[nstream3], s3_len[nstream3]);
    }
    break;
  }
}

void zstreams_reset(void) { screen_off = nstream3 = 0; }

static void capture(char *buf, uint8_t max) {
  cap = buf;
  ncap = 0;
  capmax = max;
}

static void captured(void) {
  cap[ncap] = 0;
  cap = 0;
}

void zstatus(void) {
  char left[STATUS_LEFT + 1], right[STATUS_RIGHT + 1];
  uint16_t g = zw(H_GLOBALS), a = zw(g), b;
  capture(left, STATUS_LEFT);
  if (a) {
    a = obj_name(a);
    str_set(a >> 8, a);
    zprint_str();
  }
  captured();
  capture(right, STATUS_RIGHT);
  a = zw(g + 2);
  b = zw(g + 4);
  if (zb(H_FLAGS1) & 2) {
    zputs("Time: ");
    zprint_num(a % 12 ? a % 12 : 12);
    zputc(':');
    if (b < 10)
      zputc('0');
    zprint_num(b);
    zputs(a < 12 ? " AM" : " PM");
  } else {
    zputs("Score: ");
    zprint_num(a);
    zputs("  Moves: ");
    zprint_num(b);
  }
  captured();
  plat_status(left, right);
}
