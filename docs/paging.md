# Unified paging of dynamic memory

`make ... PAGEDYN=1` builds `ZM_PAGE_DYN`. In this build, dynamic memory shares
the cache frame pool with static and high memory. It is the first step toward
a v5 interpreter, where
the v5 corpus survey found that a median turn touches 26 dynamic pages
and writes 9. The default build is unchanged: its KIM-1 image is byte for byte
the same.

## Design

- **Pinned prefix.** Story pages 0 up to the end of the global variables and
  of the object entries (the first object's property table address) are
  loaded at `zm_load` into a run of consecutive RAM pages. This prefix holds
  the header, the globals and the object entries. Pointer fast paths use
  `pin_base` where the default build uses the static base: the global
  variable pointers, object entries, `zdynw`. Zork I pins 13 of its 45
  dynamic pages. `ZM_PIN_EXTRA` at build time, or `zm -P n`, pins n more.
- **Every other page** is in `page_map` and the clock pool, like static
  pages.
  - Reads: `zb`/`zw` index `page_map` directly. A data address is 16 bits,
    so the page index is 8 bits. A resident page needs no call, and pinned
    pages are in `page_map` too.
  - `loadw` and `loadb` do this lookup inline (`zwi`/`zbi`) and leave the
    used flag alone.
  - The pages after the prefix are loaded on demand and prefilled like
    static pages.
- **Writes.** `zsb`/`zsw` look the page up the same way and set its dirty bit
  (bit 1 of `used`).
  - The clock never takes a frame whose dirty bit is set, so no write-back is
    ever needed.
  - The pool must hold every unpinned dynamic page plus 3 frames. This is
    checked at load.
- **`sread`** reads into the text buffer in place when the buffer lies in one
  page. Otherwise it reads into the free top of the call stack (or a borrowed
  frame) and copies the line out with `zsb`. The parse entries go through
  `zsw`/`zsb`.
- **Save, restore, restart.**
  - `save` writes dynamic memory a page at a time. A page that is not
    resident is loaded from the story, since a clean page equals the
    original.
  - `restore` maps each page without loading it (except the last page, which
    is partly static) and marks it dirty.
  - `restart` drops the unpinned dynamic pages and reloads the prefix.
- **Counters.** The host build counts page loads, dirty pages and their
  maximum (`zm -S`). For a `PAGEDYN=1` build it also runs a shadow clock pool
  that has the same frames and the same reference stream but evicts dirty
  pages. That pool counts the loads and the write-backs a swapping
  implementation would make.
  - `python -m kimformer.walk WALK --stats SEED` summarises these counters
    per move.
  - `python -m kimformer.prof --bench --walk WALK --seed SEED` gives the
    cycles of the same game on the simulated KIM-1.

## Results

Zork I r119 on the simulated PAL-II (`make bench`, JiffyDOS). A is the
default build and B is `PAGEDYN=1`. Reduced RAM is
`--ram 0000-13FF,2000-ABFF`, which leaves A 24 frames. The host build runs
with the same pages (`-r 04-13 -r 77-FE`/`79-FE`, `-AB` reduced).

| | A, PAL-II | B, PAL-II | A, reduced | B, reduced |
| --- | --- | --- | --- | --- |
| Image end (`__kim_end`) | $7700 | $7900 | $7700 | $7900 |
| Pinned pages | 45 | 13 | 45 | 13 |
| Cache frames | 107 | 137, up to 32 of them dynamic | 24 | 54 |
| Load to first prompt (cycles, with I/O) | 13.63M | 13.62M | 8.17M | 8.22M |
| `make bench`: interpreter cycles per move | 996,901 | 1,004,713 (+0.8%) | 1,011,691 | 1,019,792 (+0.8%) |
| `make bench`: seeks over 15 moves | 32 | 49 | 460 | 438 |
| Full walk (389 reads, seed 16): interpreter cycles per move | 1,228,570 | 1,235,041 (+0.5%) | 1,243,879 | 1,255,074 (+0.9%) |
| Full walk: page loads per move | 1.40 | 1.64 | 43.3 | 43.8 |
| Full walk: seeks per move | 1.14 | 1.37 | 31.2 | 33.5 |
| Full walk: story I/O cycles per move | 135K | 158K | 4.09M | 4.18M |
| Most dirty pages over the walk | | 27 | | 27 |
| Evict-dirty shadow: loads per move | | 1.70 | | 40.8 |
| Evict-dirty shadow: write-backs per move (most in one move) | | 0.046 (1) | | 3.93 (9) |

Cycles come from the simulator. Loads and the shadow come from the host build.
Seeks agree between the two to within 1%.

- **Load time.** Both builds fill every frame at load, so load time is the
  same. B needs only the 13 pinned pages before it could start; A needs 45.
- **B with A's RAM** (2 more pages, host): 1.29 seeks per move at full RAM and
  32.1 at reduced RAM. So most of B's extra I/O on Zork I comes from the
  replacement policy and the larger image, not from paging dynamic memory as
  such. Zork's dynamic memory is small and almost all of it is hot (27 of
  the 32 unpinned pages are written).

### Where the cost came from, and remedies

The first version read paged dynamic memory through the one-entry page cache
that static reads also use (`data_page`).
`kimformer.prof --lines` showed where the time went:

- **Calls replaced inline reads.** `zb`, `zsb`, the write helper and `zpage`
  grew by 1.03M cycles over the 15 moves. In `zexec.c`, `loadw`/`loadb`
  shrank, because their inline `dyn[]` reads had become calls.
- **The hottest line** was the 16-bit `page_map[p]` index in `zpage`.
- **The hot pages** are Zork's parser tables at $2400-$29FF, at the end of
  dynamic memory, not near the prefix. Over the 15 moves they take 8.7K
  reads and 5.5K writes. Pages $25 and $28 alternate, so the one-entry cache
  kept missing.

| Variant | Cycles per move, PAL-II | Reduced | Image end |
| --- | --- | --- | --- |
| A (default) | 996,901 | 1,011,691 | $7700 |
| B through the shared one-entry data cache | 1,091,603 (+9.5%) | 1,103,711 | $7B00 |
| ... with a separate one-entry cache for dynamic pages | 1,092,869 | 1,105,247 | $7B00 |
| ... pinning 8 more pages | 1,089,570 | 1,102,583 | $7B00 |
| ... pinning 16 more pages | 1,087,379 | 1,101,164 | $7B00 |
| ... pinning all 45 | 1,001,600 | 1,013,456 | $7B00 |
| B with a direct `page_map` lookup in `zb` and in writes | 1,063,705 (+6.7%) | 1,078,644 | $7A00 |
| ... pinning 8 more pages | 1,061,948 | 1,078,424 | $7A00 |
| B with direct lookups everywhere and `loadw`/`loadb` inline (adopted) | 1,004,713 (+0.8%) | 1,019,792 | $7900 |
| ... pinning all 45 | 999,224 (+0.2%) | 1,013,761 | $7900 |

- **A second one-entry cache, or more pinned prefix pages, does not help:**
  the hot pages lie past the prefix.
- **The direct lookup does help.** It replaces the cache test with
  `page_map[a >> 8]` (an 8-bit indexed load), and it makes pinned and paged
  pages the same path. That removes nearly all of the cost.
- **Size.** The adopted B adds 528 bytes of code (end of code $62F8 to
  $6508), which is 2 pages of image. `make mos-check` text grows from 12,936
  to 14,080 bytes, over `MOS_TEXT_MAX`; the check runs the default build
  only.

## Recommendation

- **v5: adopt unified paging.**
  - **CPU.** With direct `page_map` lookups, paging dynamic memory costs under
    1% of interpreter time, so CPU is no argument against it.
  - **RAM.** Only the header, globals and object entries need to be pinned
    and contiguous (13 pages for Zork I).
  - **Writes.** Keeping dirty pages resident with no swap file is enough for
    Zork I: at most 27 pages are dirty over the whole game. A v5 game then
    needs a pool of at least its written set plus its turn working set.
  - **Swap file.** Add one only if games need it. At 54 frames, an evicting
    pool would trade 3.9 write-backs per move for 3.0 fewer loads (the
    write-backs are about 9% of its I/O). At full RAM it would write back 18 pages over the whole
    game.
  - **Restore.** In this experiment restore makes every dynamic page dirty. A
    v5 should count restored pages against the same limit, or compare them
    with the story.
- **v3: do not switch on its own merits.**
  - **No gain.** v3 games have small, hot dynamic memory, so unified paging
    gains nothing for them. On Zork I it costs 0.5-0.9% CPU, 2 pages of
    image, and 7-20% more seeks at the same RAM.
  - **If v5 shares the code.** Should v5 share one memory layer with v3, the
    v3 cost is small enough that code sharing should decide, not
    performance.
