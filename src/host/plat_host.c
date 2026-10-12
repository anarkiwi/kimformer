/* Host platform: stdio console, files in the current directory, a simulated
 * 64K arena. */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../zint.h"
#include "host.h"

uint8_t plat_arena[0x10000];
uint8_t host_ram[256];
int host_frames = -1;
uint8_t plat_status_on;
int host_seeded, host_noecho;
uint16_t host_seed;
FILE *host_stats;
static uint32_t seeks;

static FILE *story, *save;
static long story_pos;
static int save_write, save_err;
static struct dirent **names;
static int nnames, dpos;

void plat_putc(uint8_t c) { putchar(c); }

uint8_t plat_getc(void) {
  int c = getchar();
  if (c == EOF)
    exit(0);
  if (!host_noecho)
    putchar(c);
  return c & 0x7F;
}

uint8_t plat_pages(uint8_t *hi, uint8_t max) {
  unsigned h, n = 0, cap = 256, s, len;
  uint8_t hd[0x1C];
  if (host_frames >= 0 && story && pread(fileno(story), hd, sizeof hd, 0) > 0) {
    s = 8 - ((hd[0] >> 2) + 1);
    len = hd[0x1A] << 8 | hd[0x1B];
    len = len ? (len + (1u << s) - 1) >> s : 256u << (8 - s);
    cap = hd[14] + !!hd[15] + host_frames + (len - 1) / 256;
  }
  for (h = 0; h < 256 && n < max && n < cap; h++)
    if (host_ram[h])
      hi[n++] = h;
  return n;
}

uint16_t plat_entropy(void) {
  if (host_seeded)
    return host_seed;
  return (uint16_t)(time(NULL) ^ getpid());
}

uint8_t st_open(const char *name) {
  story = fopen(name, "rb");
  story_pos = 0;
  return !story;
}

uint8_t st_seek(uint16_t page) {
  if (page != story_pos) {
    story_pos = page;
    seeks++;
    return fseek(story, page * 256L, SEEK_SET) != 0;
  }
  return 0;
}

uint16_t st_next(uint8_t *dst) {
  size_t n = fread(dst, 1, 256, story);
  memset(dst + n, 0, 256 - n);
  story_pos++;
  return n;
}

void st_close(void) {
  fclose(story);
  story = NULL;
}

uint8_t sv_open(const char *name, uint8_t write) {
  save = fopen(name, write ? "wb" : "rb");
  save_write = write;
  save_err = 0;
  return !save;
}

uint16_t sv_xfer(uint8_t *buf, uint16_t n) {
  size_t m = save_write ? fwrite(buf, 1, n, save) : fread(buf, 1, n, save);
  save_err |= m != n;
  return m;
}

uint8_t sv_close(void) {
  save_err |= fclose(save) != 0;
  save = NULL;
  return save_err;
}

static int visible(const struct dirent *d) {
  return d->d_name[0] != '.' && strlen(d->d_name) <= 16;
}

uint8_t dir_open(void) {
  while (nnames > 0)
    free(names[--nnames]);
  free(names);
  names = NULL;
  nnames = scandir(".", &names, visible, alphasort);
  dpos = 0;
  return nnames < 0;
}

uint8_t dir_next(char *name) {
  if (dpos >= nnames)
    return 0;
  strcpy(name, names[dpos++]->d_name);
  return 1;
}

/* One line per read: frames, pinned pages, page loads, dirty pages, most
 * dirty pages, shadow loads and write-backs, story seeks (cumulative). */
void plat_stats(void) {
  uint8_t i;
  if (!host_stats)
    return;
  for (i = 0; i < ZS_N; i++)
    fprintf(host_stats, "%u ", (unsigned)zstat[i]);
  fprintf(host_stats, "%u\n", (unsigned)seeks);
  fflush(host_stats);
}

void plat_exit(void) { exit(0); }
