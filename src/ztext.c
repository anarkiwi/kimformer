/* ZSCII decoding and encoding, output wrapping, windows, input,
 * tokenising. */
#include "vt100.h"
#include "zint.h"

uint8_t zm_width = 80;

#define STATUS_LEFT 64
#define STATUS_RIGHT 24

static const char alpha0[] = "abcdefghijklmnopqrstuvwxyz"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "  0123456789.,!?_#'\"/\\-:()";
static uint8_t alpha[78];

static uint8_t col, pend, wlen, wbuf[64];
static uint8_t screen_off, nstream3;
static char *cap;
static uint8_t ncap, capmax;
static uint16_t s3_table[16], s3_len[16];
static uint8_t zwin, zsplit, urow, ucol, zunbuf, zstyle, zterm;

/* The alphabet table: the story's (version 5 and later) or the default. */
void ztext_init(void) {
  uint16_t t = zver > 4 ? zw(H_ALPHABET) : 0;
  uint8_t i;
  for (i = 0; i < sizeof alpha; i++)
    alpha[i] = t ? zb(t + i) : alpha0[i];
}

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

/* Moves the terminal cursor to the current window's: the upper window's
 * position, or the end of the text on the bottom row. */
static void zcursor(void) {
  if (zwin)
    vt_goto(urow, ucol);
  else
    vt_goto(ZROWS, col + 1);
}

/* c in the upper window: unbuffered, clipped at the right edge, shown
 * only on a VT100. */
static NOINLINE void upper(uint8_t c) {
  if (c == ZNL) {
    ucol = 1;
    urow++;
    if (PLAT_STATUS)
      vt_goto(urow, 1);
  } else if (ucol <= zm_width) {
    ucol++;
    if (PLAT_STATUS)
      plat_putc(c < 32 || c > 126 ? '?' : c);
  }
}

void zputc(uint8_t c) {
  if (cap) {
    if (ncap < capmax && c >= 32 && c < 127)
      cap[ncap++] = c;
    return;
  }
  if (nstream3) {
    uint8_t k = nstream3 - 1;
    if (c)
      zsb(s3_table[k] + 2 + s3_len[k]++, c);
    return;
  }
  if (screen_off | zwin) {
    if (!screen_off && c)
      upper(c);
    return;
  }
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
  if (wlen == sizeof wbuf || zunbuf)
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

/* A key as ZSCII: Enter is 13, BS and DEL 8, the VT100 cursor keys
 * (ESC [ or ESC O, then A-D) 129-132. */
uint8_t zgetc(void) {
  uint8_t c = plat_getc();
  if (c == '\n' || c == '\r')
    return ZNL;
  if (c == 127)
    return 8;
  if (c == 27) {
    c = plat_getc();
    if (c == '[' || c == 'O') {
      c = plat_getc();
      if (c >= 'A' && c <= 'D')
        return (c += 129 - 'A') > 130 ? c ^ 7 : c;
    }
    return 27;
  }
  return c;
}

/* Key c ends input: it is in the story's terminating characters table. */
static uint8_t is_term(uint8_t c) {
  uint16_t t = zver > 4 && ZRUNNING ? zw(H_TERMS) : 0;
  uint8_t k;
  if (t)
    while ((k = zb(t++)))
      if (k == c || k == 255)
        return 1;
  return 0;
}

uint8_t zreadline(uint8_t *dst, uint8_t max, uint8_t n) {
  uint8_t c;
  zflush();
  for (;;) {
    c = zgetc();
    if (c == ZNL || (c > 128 && is_term(c)))
      break;
    if (c == 8) {
      if (n) {
        n--;
        plat_putc(' ');
        plat_putc(8);
      }
    } else if (c >= 32 && c < 127 && n < max)
      dst[n++] = c;
  }
  zterm = c;
  col = 0;
  return n;
}

static uint16_t dict_seps;
static uint8_t nseps;

static uint8_t is_sep(uint8_t c) {
  uint8_t i;
  for (i = 0; i < nseps; i++)
    if (zb(dict_seps + i) == c)
      return 1;
  return 0;
}

/* Encodes s[0..len) as a dictionary key (6 Z-characters in 4 bytes, 9 in 6
 * from version 4); returns the key length. */
static uint8_t zencode(const uint8_t *s, uint8_t len, uint8_t *key) {
  uint8_t zc[12], k = 0, nz = zver > 3 ? 9 : 6, j, c, e;
  for (j = 0; j < len && k < nz; j++) {
    c = s[j];
    for (e = 0; e < sizeof alpha && (alpha[e] != c || e == 52 || e == 53); e++)
      ;
    if (e < 26)
      zc[k++] = e + 6;
    else {
      zc[k++] = e < 52 ? 4 : 5;
      if (e < sizeof alpha)
        zc[k++] = e < 52 ? e - 20 : e - 46;
      else {
        zc[k++] = 6;
        zc[k++] = c >> 5;
        zc[k++] = c & 31;
      }
    }
  }
  while (k < nz)
    zc[k++] = 5;
  for (j = k = 0; k < nz; k += 3, j += 2) {
    key[j] = zc[k] << 2 | zc[k + 1] >> 3;
    key[j + 1] = zc[k + 1] << 5 | zc[k + 2];
  }
  key[j - 2] |= 0x80;
  return j;
}

/* The dictionary entry of s[0..len), 0 if none: binary search, or linear
 * when the entry count is negative (an unsorted user dictionary). */
static uint16_t lookup(const uint8_t *s, uint8_t len) {
  uint8_t key[6], kl = zencode(s, len, key), elen, j, c = 0, lin = 0;
  uint16_t lo = 0, hi, mid, e, base = dict_seps + nseps;
  elen = zb(base);
  hi = zw(base + 1);
  base += 3;
  if (hi & 0x8000) {
    hi = -hi;
    lin = 1;
  }
  while (lo < hi) {
    mid = lin ? lo : (lo + hi) >> 1;
    e = base + umul(mid, elen);
    for (j = 0; j < kl && (c = zb(e + j)) == key[j]; j++)
      ;
    if (j == kl)
      return e;
    if (lin || c < key[j])
      lo = mid + 1;
    else
      hi = mid;
  }
  return 0;
}

/* Splits t[i..end) into words at spaces and dictionary d's separators and
 * writes their entries, lengths and positions to the parse buffer; with
 * flag, the entries of words not found are left alone. */
static void tokenise(const uint8_t *t, uint8_t i, uint8_t end, uint16_t parse,
                     uint16_t d, uint8_t flag) {
  uint8_t maxw = zb(parse), nw = 0, s, c;
  uint16_t e, q;
  nseps = zb(d);
  dict_seps = d + 1;
  while (i < end) {
    c = t[i];
    if (c == ' ') {
      i++;
      continue;
    }
    s = i++;
    if (!is_sep(c))
      while (i < end && (c = t[i]) != ' ' && !is_sep(c))
        i++;
    if (nw < maxw) {
      e = lookup(t + s, i - s);
      q = parse + 2 + (nw << 2);
      if (e || !flag) {
        zsw(q, e);
        zsb(q + 2, i - s);
        zsb(q + 3, s);
      }
      nw++;
    }
  }
  zsb(parse + 1, nw);
}

/* A 256-byte work buffer: the free top of the call stack or a borrowed
 * frame. */
static uint8_t *zwork(void) {
  uint8_t *t = zscratch();
  return t ? t : PAGE_PTR(str_hi = zframe());
}

uint8_t zsread(uint16_t text, uint16_t parse) {
  uint8_t *t, *b, max = zb(text), i0 = zver > 4 ? 2 : 1, n = 0, i, c;
  zdyn(text, max + i0);
  if (parse)
    zdyn(parse, 2 + (zb(parse) << 2));
  if (!ZRUNNING)
    return 0;
  if (PLAT_STATUS && zver == 3) {
    zflush();
    zstatus();
  }
#ifdef ZM_STATS
  plat_stats();
#endif
  t = b = zdynw(text, max + i0);
  if (!b)
    t = zwork();
  if (max > 253)
    max = 253;
  if (i0 == 2) {
    n = zb(text + 1);
    if (n > max)
      n = 0;
    for (i = 0; !b && i < n; i++)
      t[2 + i] = zb(text + 2 + i);
  } else if (max)
    max--;
  n = zreadline(t + i0, max, n);
  for (i = i0; i < i0 + n; i++)
    if ((c = t[i]) >= 'A' && c <= 'Z')
      t[i] = c + 32;
  if (i0 == 2)
    t[1] = n;
  else
    t[n + 1] = 0;
  for (i = 1; !b && i <= n + 1; i++)
    zsb(text + i, t[i]);
  if (parse)
    tokenise(t, i0, i0 + n, parse, zw(H_DICT), 0);
  return zterm;
}

void ztokenise(uint16_t text, uint16_t parse, uint16_t dict, uint8_t flag) {
  uint8_t *t = zwork(), n = zb(text + 1), i;
  if (n > 253)
    n = 253;
  for (i = 0; i < n; i++)
    t[2 + i] = zb(text + 2 + i);
  tokenise(t, 2, 2 + n, parse, dict ? dict : zw(H_DICT), flag);
}

void zencode_text(uint16_t text, uint8_t len, uint16_t from, uint16_t dst) {
  uint8_t s[9], key[6], i;
  for (i = 0; i < len && i < sizeof s; i++)
    s[i] = zb(text + from + i);
  zencode(s, i, key);
  for (i = 0; i < sizeof key; i++)
    zsb(dst + i, key[i]);
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

void zscreen_reset(void) {
  zwin = zsplit = zunbuf = 0;
  urow = ucol = 1;
  if (PLAT_STATUS) {
    if (zstyle)
      vt_style(0);
    vt_region(zver > 3 ? 1 : 2);
    zcursor();
  }
  zstyle = 0;
}

void zsplit_window(uint8_t n) {
  if (n > ZROWS - 1)
    n = ZROWS - 1;
  if (n == zsplit)
    return;
  zflush();
  zsplit = n;
  if (urow > zsplit)
    urow = ucol = 1;
  if (PLAT_STATUS) {
    vt_region(zsplit + 1);
    zcursor();
  }
}

void zset_window(uint8_t w) {
  zflush();
  zwin = w != 0;
  urow = ucol = 1;
  if (PLAT_STATUS)
    zcursor();
}

void zerase_window(int16_t w) {
  zflush();
  if (w == -1)
    zsplit = zwin = 0;
  if (w < 0 || w == 1)
    urow = ucol = 1;
  if (w < 1)
    col = pend = 0;
  if (!PLAT_STATUS)
    return;
  if (w < 0) {
    vt_region(zsplit + 1);
    vt_csi("2J");
  } else if (!w) {
    vt_goto(zsplit + 1, 1);
    vt_csi("J");
  } else if (w == 1 && zsplit) {
    vt_goto(zsplit, zm_width);
    vt_csi("1J");
  }
  zcursor();
}

void zerase_line(void) {
  if (PLAT_STATUS && zwin)
    vt_csi("K");
}

void zset_cursor(uint16_t row, uint16_t c) {
  if (!zwin || (row | c) & 0xFF00)
    return;
  urow = row;
  ucol = c;
  if (PLAT_STATUS)
    vt_goto(urow, ucol);
}

void zget_cursor(uint16_t t) {
  zsw(t, zwin ? urow : ZROWS);
  zsw(t + 2, zwin ? ucol : col + 1);
}

void zset_style(uint8_t s) {
  zflush();
  zstyle = s ? zstyle | s : 0;
  if (PLAT_STATUS)
    vt_style(zstyle);
}

void zbuffer_mode(uint8_t on) {
  zflush();
  zunbuf = !on;
}

void zprint_table(uint16_t a, uint16_t w, uint16_t h, uint16_t skip) {
  uint8_t r0 = urow, c0 = ucol;
  uint16_t r, i;
  for (r = 0; r < h; r++, a += skip) {
    if (r && zwin)
      zset_cursor(r0 + r, c0);
    else if (r)
      zputc(ZNL);
    for (i = 0; i < w; i++)
      zputc(zb(a++));
  }
}

/* One key for read_char; the terminal's echo of it is undone. */
uint8_t zread_char(void) {
  uint8_t c;
  zflush();
  c = zgetc();
  if (c == ZNL && !zwin)
    col = pend = 0;
  if (PLAT_STATUS) {
    zcursor();
    if (c >= 32 && c < 127) {
      plat_putc(' ');
      zcursor();
    }
  } else if (c >= 32 && c < 127 && !zwin)
    col++;
  return c;
}

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
