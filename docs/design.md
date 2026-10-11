# Design

kimformer is a version 3 Z-machine (Z-Machine Standard 1.1) for the KIM-1
family, built with llvm-mos. Story files live on an SD card in an sd2iec (or
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
| `src/vt100.c` | VT100 status line (`plat_status`), shared by the KIM-1 and host platforms |
| `src/zint.h` | interpreter internals shared by the core modules |
| `src/host/` | host platform: stdio console, files in a directory, simulated 64K arena, optional VT100 status line |
| `src/stub/` | do-nothing platform for the 6502 link and size check |
| `src/kim/` | KIM-1 platform: monitor TTY, KIMDOS C library, RAM probe |
| `games/` | Inform 6 source of the demo adventure |
| `kimformer/` | Python tooling: fixture fetching, KIM-1 simulator harness, cycle profiler |
| `tests/` | pytest suites for the host build and the 6502 build |
| `tests/stories/` | Inform 6 probe stories: opcodes, streams, parsing and errors; the time status line |

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

`zm_load` takes the longest ascending run of consecutive free pages that holds
dynamic memory (header word $0E rounded up to a page) and loads story pages
0..n-1 there. Every other free page is a cache frame.

`page_map[512]` maps each story page (v3 stories are at most 128K) to the high
byte of the RAM page holding it, 0 when absent. Dynamic pages are pinned. Frames
are replaced by the clock algorithm: every lookup sets the frame's used flag; a
miss sweeps a hand over the frames, clearing used flags, to the first frame
not used since the last sweep that is not the page the PC or the current string
is executing from, then calls `st_seek` + `st_next`. At least three frames are
required.

After dynamic memory, `zm_load` keeps reading the story sequentially into free
frames until they or the file run out: sequential reads need no seek. It prints
a dot every 8 pages, then a line break.

Byte reads below the static base go straight to dynamic memory; reads above it
keep the last page looked up (dropped when its frame is replaced). The PC is a
zero page pointer to its page plus an 8-bit offset, so a fetch is one indexed
load; when the offset wraps the next page is looked up. A PC that runs past the
story's last page stops with `ZM_ERR_ADDRESS` before the next instruction
unless a jump has brought it back. The string decoder keeps its page's high
byte, cleared when the offset wraps and looked up on the next read.

Addresses are page (`uint16_t`) plus offset (`uint8_t`); no 32-bit arithmetic
in the interpreter.

`make ... PAGEDYN=1` (`ZM_PAGE_DYN`) builds an experiment in which only a
prefix of dynamic memory is pinned and the rest is paged with static memory;
see [paging.md](paging.md). The default build is unchanged.

## Interpreter

- Version 3 only; all v3 opcodes.
- Header: Flags 1 bit 5 (split screen available) cleared, bit 4 (status
  line not available) set unless the platform draws a status line
  (`PLAT_STATUS`); interpreter number 6, version `A`, standard revision 1.1.
  `split_window`, `set_window`, `sound_effect` are no-ops;
  `output_stream` 3 (memory, nested up to 16) is supported; 2 sets or clears
  the transcript bit (Flags 2 bit 0) only, as the console is the hardcopy;
  4 is ignored; `input_stream` is ignored.
- Variables and stacks are byte arrays indexed by 8-bit values, so each
  access is one absolute indexed load per byte:
  - the evaluation stack holds 255 words as separate low and high byte
    arrays with an 8-bit stack pointer; each routine's part starts at its
    base, below which a pull is `ZM_ERR_STACK`;
  - the current routine's locals are fixed arrays indexed by the variable
    number;
  - a call pushes the caller's locals, its PC (page and offset), the store
    variable, its stack base and local count onto a 1536-byte call stack
    (`ZM_ERR_STACK` when full) and loads the callee's locals; a return pops
    them;
  - globals are read through two zero page pointers (variables 16-127 and
    128-255) when the table lies in dynamic memory.
- Opcodes are decoded into a dense 0-95 code (2OP, 1OP, 0OP, VAR) and
  dispatched by one `switch`; results go through shared store and branch
  tails. The long form reads its two operand bytes as one word; the VAR
  form's type byte is decoded unrolled; locals are read and stored inline.
- Objects whose 9-byte entry lies in dynamic memory are accessed through a
  pointer; `loadw`/`loadb` read dynamic memory inline. Multiplication (`mul`,
  dictionary entries) is shift-and-add.
- Output is buffered a word at a time and wrapped at `zm_width`; spaces are
  held back and dropped at a line break.
- Fatal errors (`zm.h` codes) stop `zm_run` with `Error <code> at $<PC>`:
  division by zero and illegal opcodes are `ZM_ERR_OPCODE`; attribute numbers
  above 31, `put_prop`/`get_next_prop` of a missing property and object 0 are
  `ZM_ERR_OBJECT`; stack overflow/underflow, locals beyond the routine's count
  and a 17th nested stream 3 are `ZM_ERR_STACK`; writes at or above the static
  base and reads past the story are `ZM_ERR_ADDRESS`.
- `sread` lower-cases input, honours backspace (BS or DEL: removes the last
  character and prints space, backspace) and tokenises against the story
  dictionary (or the one given).
- `sread` stores at most byte 0 minus 1 characters, looks words up by binary
  search on their 4-byte encoding (6 Z-characters) and splits on spaces and
  the dictionary's word separators. The interpreter does not echo the line
  break; the platform's terminal does.
- Status line (with `PLAT_STATUS`): before every `sread` and on
  `show_status`, the short name of the object in global 0 is rendered into a
  buffer (`zputc` captures while a buffer is set) and the right side is
  `Score: S  Moves: M` from globals 1 and 2, or `Time: H:MM AM` (12-hour,
  spec 8.2.3.2) when Flags 1 bit 1 is set; both go to `plat_status`.
- `random`: 16-bit xorshift (7, 9, 8), seeded from `plat_entropy` when
  `zm_run` starts (0, which xorshift cannot use, becomes $ACE1); random(0)
  reseeds the same way, random(-n) seeds with n.
- `save`/`restore` prompt `File [NAME.SAV]: ` (default: the story name with its
  extension replaced by `.SAV`) and write/read: magic `KZ3\2`, release (2),
  serial (6), checksum (2) as in the header, then little endian dynamic size
  (2) and call stack size (2), evaluation stack pointer, routine stack base and
  local count (1 each), then PC (3, big endian), dynamic memory, the low then
  the high bytes of the used evaluation stack, the 16 low and 16 high local
  bytes, the used call stack. Restore checks the first 16 bytes against the
  loaded story and the sizes; a short read after that is `ZM_ERR_OPEN`. A
  return through a corrupt frame is `ZM_ERR_STACK`.
- `restart` reloads dynamic memory, preserving Flags 2 bits 0-1.
- `verify` sums the file through a borrowed cache frame.
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
matches the suffix `.Z3` case-insensitively on that.

## Tests

| Suite | What |
| --- | --- |
| `tests/test_host.py` | host build: CZECH compliance, the demo walkthrough, Zork I opening moves, save/restore, restart, small caches that force eviction, the probe story, load errors, launcher, status lines (`-v`) |
| `tests/test_fixtures.py` | fixture download, verification and Inform compilation |
| `tests/test_kim.py` | 6502 build on the KIMDOS py65 simulator (PAL-II RAM map) with the sd2iec drive model: launcher, re-run without reload, no drive, DOS errors, too little RAM, demo walkthrough, Zork I opening with save/restore and status lines, restore in a fresh session, a small-RAM run that evicts, RAM mirrored through an undecoded address line, the simulator CLI, cycle meter, profiler and benchmark |

Fixtures that are not ours (CZECH, Zork I from historicalsource/zork1, the
Inform 6 compiler source) are downloaded once into `tests/fixtures/cache/`
(gitignored) and checked against pinned SHA-256 digests. CZECH ships no
version 3 story, so it is compiled from `czech.inf` with Inform 6.
