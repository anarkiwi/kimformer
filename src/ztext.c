/* ZSCII decoding, abbreviations, output wrapping, input, tokenising. */
#include "zint.h"

uint8_t zm_width = 80;

static const char alpha[] = "abcdefghijklmnopqrstuvwxyz"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "  0123456789.,!?_#'\"/\\-:()";

static uint8_t col, pend, wlen, wbuf[64];
static uint8_t screen_off, nstream3;
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
  static const uint16_t pow10[] = {10000, 1000, 100, 10, 1};
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

void zprint_str(void) {
  static const uint8_t shift[] = {0, 5, 10};
  uint16_t w = 0, sv_w = 0, sv_page = 0, a;
  uint8_t n = 0, c, alph = 0, st = 0, hi = 0, sv_n = 0, sv_off = 0, nest = 0;
  for (;;) {
    if (!n) {
      if (w & 0x8000) {
        if (!nest)
          break;
        nest = 0;
        str_set(sv_page, sv_off);
        w = sv_w;
        n = sv_n;
        alph = 0;
        continue;
      }
      if (zstate != ZS_RUN)
        break;
      w = (uint16_t)str_byte() << 8;
      w |= str_byte();
      n = 3;
    }
    c = (w >> shift[--n]) & 31;
    if (st) {
      if (st < 4) {
        if (!nest) {
          sv_page = str_page;
          sv_off = str_off;
          sv_w = w;
          sv_n = n;
          a = zw(zw(H_ABBREV) + ((st - 1) << 6) + (c << 1));
          str_set(a >> 7, a << 1);
          nest = 1;
          w = n = 0;
        }
        st = 0;
      } else if (st == 4) {
        hi = c;
        st = 5;
      } else {
        zputc(hi << 5 | c);
        st = 0;
      }
      continue;
    }
    if (!c)
      zputc(' ');
    else if (c < 4)
      st = c;
    else if (c < 6)
      alph = c - 3;
    else {
      if (alph == 2 && c == 6)
        st = 4;
      else if (alph == 2 && c == 7)
        zputc(ZNL);
      else
        zputc(alpha[alph * 26 + c - 6]);
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
    e = base + mid * elen;
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
  uint8_t *t, *p, max = zb(text), maxw = zb(parse), i, s, c, nw = 0;
  uint16_t e;
  t = zdyn(text, max + 1);
  p = zdyn(parse, 2 + (maxw << 2));
  if (zstate != ZS_RUN)
    return;
  i = max ? zreadline(t + 1, max - 1) : 0;
  t[i + 1] = 0;
  for (i = 1; (c = t[i]); i++)
    if (c >= 'A' && c <= 'Z')
      t[i] = c + 32;
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
      p += 4;
      p[-2] = e >> 8;
      p[-1] = e;
      p[0] = i - s;
      p[1] = s;
      nw++;
    }
  }
  zdyn(parse, 2)[1] = nw;
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
