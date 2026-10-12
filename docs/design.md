# Design

kimformer is a Z-machine for story versions 3, 4, 5 and 8 (Z-Machine
Standard 1.1) for the KIM-1 family, built with llvm-mos. Story files live on an SD card in an sd2iec (or
any drive that implements the sd2iec `P` command on ordinary files) attached
through the KIMDOS adapter; the interpreter pages them in on demand.

## Layout

| Path | |
| --- | --- |
| `src/zm.h` | interpreter API |
| `src/plat.h` | platform interface |
| `src/zmem.c` | story paging, dynamic memory, byte/word access |
| `src/ztext.c` | ZSCII decoding, abbreviations, output wrapping, input, tokenising |
| `src/zobj.c` | object tree and properties |
| `src/zexec.c` | operand decoding, opcodes, call stack, save/restore/restart |
| `src/launcher.c` | story chooser, shared by all platforms |
| `src/vt100.c` | VT100 output: the v3 status line (`plat_status`), cursor addressing, scroll region, erasing and styles |
| `src/zint.h` | interpreter internals shared by the core modules |
| `src/host/` | host platform: stdio console, files in a directory, simulated 64K arena, optional VT100 status line |
| `src/stub/` | do-nothing platform for the 6502 link and size check |
| `src/kim/` | KIM-1 platform: monitor TTY, KIMDOS C library, RAM probe |
| `games/` | Inform 6 source of the demo adventure |
| `kimformer/` | Python tooling: fixture fetching, KIM-1 simulator harness, cycle profiler |
| `tests/` | pytest suites for the host build and the 6502 build |
| `tests/stories/` | Inform 6 probe stories (v3: opcodes, streams, parsing and errors; the time status line; v5: tables, tokenise, input, windows, saves), walk files for Zork I and The Library Of Horror |

## Memory

All story and RAM pages are 256 bytes. RAM pages are named by their high byte;
`PAGE_PTR(hi)` is the page's address. `plat_pages` lists the free RAM pages.

PAL-II default switch settings give RAM at $0000-$13FF and $2000-$FFFF:

| Range | Use |
| --- | --- |
| $0000-$001F | llvm-mos imaginary registers |
| $0020-$00EE | zero page variables: the interpreter's hot state (`ZP` in `zint.h`), then the LTO allocator's (`-mlto-zp`) |
| $00EF-$00FF | KIM-1 monitor |
| $0100-$01FF | 6502 stack |
| $0400-$13FF | page cache (if present) |
| $1700-$1FFF | RIOT RAM/IO, monitor ROM |
| $2000-image end | interpreter code, data, BSS, Z-stack |
| image end-$FEFF | dynamic memory, then page cache |
| $FF00-$FFFF | unused: the vector select switch overlays $FFFA-$FFFF with ROM |

The KIM platform probes every candidate page so a board with less RAM still
runs whatever fits, and skips mirrors on a board that does not decode every
address line (see [KIM-1 platform](#kim-1-platform)).

Story pages 0 up to the end of the global variables and of the object
entries (the first object's property table address) are pinned: `zm_load`
takes the longest ascending run of consecutive free pages and loads them
there (Zork I: 13 pages). Every other free page is a cache frame, shared by
the rest of dynamic memory and by static and high memory; see
[paging.md](paging.md).

The page map gives, for each story page, the high byte of the RAM page
holding it (0 when absent): `pm0[256]` covers pages 0-255, so every data
address is one 8-bit indexed load; pages 256 and up (v3 stories reach page
511, v4/v5 1023, v8 2047) map through 256-entry chunks taken from the frame
pool at load, one per 64K of story beyond the first. Pinned pages are in the
map too. Frames are replaced by the clock algorithm: every lookup sets the
frame's used flag; a miss sweeps a hand over the frames, clearing used
flags, to the first frame not used since the last sweep that is not the page
the PC or the current string is executing from and is not dirty, then calls
`st_seek` + `st_next`. A written dynamic page is dirty and stays resident,
so the pool must hold every unpinned dynamic page plus 3 frames (checked at
load, else `ZM_ERR_MEMORY`).

After the pinned prefix, `zm_load` keeps reading the story sequentially into
free frames until they or the file run out: sequential reads need no seek.
It prints a dot every 8 pages, then a line break.

Byte and word reads index the page map directly; `loadw`/`loadb` do it
inline and leave the used flag alone. The PC is a zero page pointer to its
page plus an 8-bit offset, so a fetch is one indexed load; when the offset
wraps the next page is looked up. A PC that runs past the story's last page
stops with `ZM_ERR_ADDRESS` before the next instruction unless a jump has
brought it back. The string decoder keeps its page's high byte, cleared when
the offset wraps and looked up on the next read.

Addresses are page (`uint16_t`) plus offset (`uint8_t`); no 32-bit arithmetic
in the interpreter. A packed address P is the byte address P << 1 (v3),
P << 2 (v4, v5) or P << 3 (v8); the file length in the header is scaled the
same way.

## Interpreter

- Versions 3, 4, 5 and 8, dispatched on header byte 0 (other versions are
  `ZM_ERR_VERSION`); every opcode of those versions except the v6 ones. Opcodes
  of later versions are not rejected in earlier stories.
- Header, version 3: Flags 1 bit 5 (split screen available) cleared, bit 4
  (status line not available) set unless the platform draws a status line
  (`PLAT_STATUS`). Version 4 and later: Flags 1 sets bold, italic and
  fixed-space on a VT100 (fixed-space only on a plain TTY) and clears
  colours, pictures, sound and timed input; Flags 2 clears pictures, undo,
  mouse, colours, sound and menus; screen height 24 rows on a VT100 (255 on a
  plain TTY), width `zm_width`, the same in units, font size 1x1, default
  colours 1; the header extension's Flags 3 is cleared. All versions:
  interpreter number 6, version `A`, standard revision 1.1.
- Variables and stacks are byte arrays indexed by 8-bit values, so each
  access is one absolute indexed load per byte:
  - the evaluation stack holds 255 words as separate low and high byte
    arrays with an 8-bit stack pointer; each routine's part starts at its
    base, below which a pull is `ZM_ERR_STACK`;
  - the current routine's locals are fixed arrays indexed by the variable
    number;
  - a call pushes the caller's locals, its PC (page and offset), the store
    variable, its stack base and a frame byte (local count, argument count
    << 4, bit 7 when the result is discarded) onto a 1536-byte call stack
    (`ZM_ERR_STACK` when full) and loads the callee's locals (initial values
    from the routine header before version 5, zero from version 5); a return
    pops them. `check_arg_count` reads the argument count; `catch` returns
    the call stack size and `throw` pops frames down to it, then returns;
  - globals are read through two zero page pointers (variables 16-127 and
    128-255) when the table lies in the pinned prefix.
- Opcodes are decoded into a dense code (2OP 0-31, 1OP 32-47, 0OP 48-63, VAR
  64-95, EXT 96-127) and dispatched by one `switch` for the version 3 and
  call opcodes, and a second, out of line, for the rest; results go through
  shared store and branch tails. The long form reads its two operand bytes as
  one word; the VAR form's type byte is decoded unrolled, `call_vs2` and
  `call_vn2` read a second one; locals are read and stored inline.
- Objects: 9-byte entries, 32 attributes, 31 properties in version 3;
  14-byte entries, 48 attributes, 63 properties and the 2-byte property size
  format from version 4. Fields in the pinned prefix are read directly.
  Multiplication (`mul`, dictionary entries) is shift-and-add.
- Output is buffered a word at a time and wrapped at `zm_width`; spaces are
  held back and dropped at a line break. ZSCII outside 32-126 prints `?`.
  `print_unicode` prints ASCII and `?` otherwise; `check_unicode` returns 3
  for 32-126, else 0. The story's alphabet table (v5 header $34) is used for
  decoding and encoding.
- Fatal errors (`zm.h` codes) stop `zm_run` with `Error <code> at $<PC>`, the
  PC when the error was found: division by zero and illegal opcodes are
  `ZM_ERR_OPCODE`; attribute numbers above 31 (47), `put_prop`/
  `get_next_prop` of a missing property and object 0 are `ZM_ERR_OBJECT`;
  stack overflow/underflow, locals beyond the routine's count, a bad `throw`
  frame and a 17th nested stream 3 are `ZM_ERR_STACK`; writes at or above the
  static base and reads past the story are `ZM_ERR_ADDRESS`.
- Input: `sread`/`aread` lower-case input, honour backspace (BS or DEL:
  removes the last character and prints space, backspace) and tokenise
  against the story dictionary (or the one given). Version 3 and 4 buffers
  hold at most byte 0 minus 1 characters, zero terminated; version 5 buffers
  byte 0 characters (at most 253) with the count in byte 1, and characters
  already in the buffer (byte 1) are kept as typed; `aread` stores the
  terminating key, and its parse buffer may be 0. Words are looked up by
  binary search on their 4-byte (v3) or 6-byte (v4+) encoding, or linearly in
  a user dictionary with a negative entry count; `tokenise` takes a user
  dictionary and with its flag leaves the entries of unknown words alone;
  `encode_text` uses the same encoder. Words split on spaces and the
  dictionary's word separators. The interpreter does not echo the line
  break; the platform's terminal does.
- Keys: Enter is 13, BS and DEL 8, the VT100 cursor keys (ESC `[` or ESC `O`
  then A-D) 129-132; another key after ESC is 27 and is lost. A cursor key
  in the story's terminating characters table (header $2E, 255 meaning all)
  ends `aread`. `read_char` returns one key; timed input never times out
  (the time and routine operands are ignored, Flags 1 bit 7 is clear).
- Screen model (version 4 and later, `PLAT_STATUS`): the upper window is rows
  1 to n of the screen, written with cursor addressing, unbuffered and
  clipped at `zm_width`; the lower window is the scroll region below it, and
  its text always continues on the bottom row, as on a teletype, so a
  cleared lower window fills from the bottom. `split_window` sets the scroll
  region (`ESC[n+1;24r`, only when the split changes), `set_window` moves the
  cursor to the upper window's top left or back to the end of the lower
  window's text, `erase_window` clears with `ESC[2J` (-1 also unsplits, -2
  keeps the split), `ESC[J` from the lower window's top (0) or `ESC[1J` up to
  the end of the upper window (1), `erase_line` 1 is `ESC[K`, `set_cursor`
  and `get_cursor` work in the upper window (the lower window's cursor is the
  bottom row), `set_text_style` sends `ESC[0;7;1;4m` for its reverse, bold
  and italic (underlined) bits, `buffer_mode 0` writes the lower window a
  character at a time. `read_char` puts the cursor back and blanks the key
  the terminal echoed. `set_colour` and `set_true_colour` are ignored;
  `set_font` returns 1 for fonts 0 and 1 and 0 for the others;
  `sound_effect` 1 and 2 send BEL, others are ignored.
- Plain TTY (`VT100=0`) and version 3: the upper window's text is not shown
  (its cursor is still tracked for `get_cursor`); `split_window` and
  `set_window` are no-ops in version 3.
- Status line (version 3 with `PLAT_STATUS`): before every `sread` and on
  `show_status`, the short name of the object in global 0 is rendered into a
  buffer (`zputc` captures while a buffer is set) and the right side is
  `Score: S  Moves: M` from globals 1 and 2, or `Time: H:MM AM` (12-hour,
  spec 8.2.3.2) when Flags 1 bit 1 is set; both go to `plat_status`.
- Streams: `output_stream` 3 (memory, nested up to 16) is supported; 2 sets
  or clears the transcript bit (Flags 2 bit 0) only, as the console is the
  hardcopy; 4 is ignored; `input_stream` is ignored.
- `random`: 16-bit xorshift (7, 9, 8), seeded from `plat_entropy` when
  `zm_run` starts (0, which xorshift cannot use, becomes $ACE1); random(0)
  reseeds the same way, random(-n) seeds with n.
- `save`/`restore` (the 0OP branch forms in v3, store forms in v4, the EXT
  forms from v5, which store 0, 1, or 2 after a restore) prompt
  `File [NAME.SAV]: ` (default: the story name with its extension replaced
  by `.SAV`) and write/read: magic `KZ3\2`, release (2), serial (6), checksum
  (2) as in the header, then little endian dynamic size (2) and call stack
  size (2), evaluation stack pointer, routine stack base and the frame byte's
  local and argument counts (1 each), then PC (3, big endian), dynamic
  memory, the low then the high bytes of the used evaluation stack, the 16
  low and 16 high local bytes, the used call stack. Restore checks the first
  16 bytes against the loaded story and the sizes; a short read after that is
  `ZM_ERR_OPEN`. A return through a corrupt frame is `ZM_ERR_STACK`. Saves
  with a table operand (auxiliary data) return 0.
- `save_undo` returns -1 (unavailable) and `restore_undo` 0.
- `restart` reloads dynamic memory, preserving Flags 2 bits 0-1.
- `verify` sums the file through a borrowed cache frame; `piracy` branches.
- `quit` returns from `zm_run` to the launcher.

## KIM-1 platform

`src/kim/plat_kim.c`, linked with the KIMDOS C library sources and its KIM-1
link script (`kim1/link.ld`), from the kimdos Python package
(`python -m kimdos.clib`): image at $2000, 1K soft stack
up to the page-aligned `__kim_end`.

- Console: `kd_putc`/`kd_getc` (the monitor's `OUTCH`/`GETCH`); `'\n'` is
  sent as CR LF. The TTY echoes input, CR included, so `plat_getc` sends LF
  after a CR.
- Status line (default; `make kim VT100=0` for a plain TTY): `src/vt100.c`.
  Start-up sets the scroll region to lines 2-24 (`ESC[2;24r`) and moves to
  line 24; `plat_status` sends `ESC 7`, `ESC[1;1H`, `ESC[7m`, the left text
  padded so the right text ends at `zm_width`, `ESC[0m`, `ESC 8`; exit
  resets the region (`ESC[r`) and moves to line 24.
- RAM: `plat_pages` probes $04-$13 and `__kim_end`/256-$FE in ascending
  order. At offset 0 of each page it inverts the byte, reads it back and
  checks that the byte at offset 0 of each page whose number differs in one
  bit did not change, then restores and reads back the byte. A board that
  ignores an address line mirrors a page onto exactly such a neighbour, so a
  mirror of the image, of RAM below it or of a lower candidate is skipped; a
  higher candidate neighbour is not checked, as it is skipped itself when its
  turn comes. Page $17 (I/O) is not compared.
- Story: channel 2, opened with `<name>,P,R`. `st_seek` skips a seek to the
  current page, otherwise sends the sd2iec command `P` + chr($62) + 4-byte
  little-endian offset on channel 15 (as `kd_seek` does, but reading the
  status line itself so it can show it). `st_next` is `kd_read` of 256
  bytes.
- Save: channel 1 with `@0:<name>` (PRG, replacing); restore: channel 3
  `<name>,P,R`. A short transfer or a drive error after the close fails
  `sv_close`.
- Errors: after every open, seek and save close the drive status is read; a
  code of 20 or more prints the status line (`No drive` if no device
  answers).
- Directory: `kd_dir_*` from the KIMDOS C library, channel 0. A listing the
  launcher leaves unfinished is closed by the next open of channel 0.
- Entropy: the RIOT interval timer ($1706) is read after every key and
  folded into a 16-bit value (rotate left 3, xor); `plat_entropy` mixes in a
  fresh timer read. The timer runs freely at the clock rate, so the
  sub-millisecond timing of keystrokes decides the seed.
- Exit: `_Exit`, a jump to the monitor (`$1C4F`). The image can be started
  again with `2000 G`: no initialised variable is written (the RNG state is
  BSS, seeded at story start), and BSS is cleared at start.

File names from the drive are PETSCII. The launcher prints `c & 0x7F` and
matches the suffixes `.Z3`, `.Z4`, `.Z5` and `.Z8` case-insensitively on
that.

## Tests

| Suite | What |
| --- | --- |
| `tests/test_host.py` | host build: CZECH compliance, the demo walkthrough, Zork I opening moves, save/restore, restart, small caches that force eviction, the probe story, load errors, launcher, status lines (`-v`) |
| `tests/test_zork_full.py` | a complete winning Zork I game |
| `tests/test_v5.py` | host build, versions 4, 5 and 8: CZECH (v4, v5, v8), praxix, TerpEtude, the v5 probe story (tables, tokenise, keys, preloaded input, windows and their VT100 output, saves), the demo for v5, a complete winning game of The Library Of Horror (memory sizes, VT100, save/restore), and, with `DFROTZ` set, the v5 demo and The Library Of Horror against dfrotz |
| `tests/test_fixtures.py` | fixture download, verification and Inform compilation |
| `tests/test_kim.py` | 6502 build on the KIMDOS py65 simulator (PAL-II RAM map) with the sd2iec drive model: launcher, re-run without reload, no drive, DOS errors, too little RAM, demo walkthrough, Zork I opening with save/restore and status lines, restore in a fresh session, a small-RAM run that evicts, RAM mirrored through an undecoded address line, the simulator CLI, cycle meter, profiler and benchmark, a winning game of The Library Of Horror |

Fixtures that are not ours are downloaded once into `tests/fixtures/cache/`
(gitignored) and checked against pinned SHA-256 digests; none is committed:

| Fixture | Source | Licence |
| --- | --- | --- |
| Zork I r119 | historicalsource/zork1 | MIT |
| CZECH 0.8 (`czech.inf`, `czech.z5`, reference transcripts) | IF Archive | use, distribution and modification by anyone, notice preserved |
| praxix | IF Archive | public domain |
| TerpEtude | IF Archive (`etude.tar.Z`, unpacked with `gzip -dc`) | freely distributed with its source |
| PunyInform 5.14.1 (The Library Of Horror, its library) | GitHub release | MIT |
| Inform 6.43 source | GitHub release | Artistic 2.0 |

CZECH ships no version 3, 4 or 8 story, so those are compiled from
`czech.inf` with Inform 6; The Library Of Horror is compiled for version 5
with the PunyInform library.
