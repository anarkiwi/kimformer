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
| `src/host/` | host platform: stdio console, files in a directory, simulated 64K arena |
| `src/kim/` | KIM-1 platform: monitor TTY, KIMDOS C library, RAM probe |
| `extern/kimdos` | KIMDOS (submodule): IEC/DOS library for llvm-mos, KIM-1 linker script |
| `games/` | Inform 6 source of the demo adventure |
| `kimformer/` | Python tooling: fixture fetching, KIM-1 simulator harness |
| `tests/` | pytest suites for the host build and the 6502 build |

## Memory

All story and RAM pages are 256 bytes. RAM pages are named by their high byte;
`PAGE_PTR(hi)` is the page's address. `plat_pages` lists the free RAM pages.

PAL-II default switch settings give RAM at $0000-$13FF and $2000-$FFFF:

| Range | Use |
| --- | --- |
| $0000-$001F | llvm-mos imaginary registers |
| $0020-$00EE | llvm-mos zero page variables |
| $00EF-$00FF | KIM-1 monitor |
| $0100-$01FF | 6502 stack |
| $0400-$13FF | page cache (if present) |
| $1700-$1FFF | RIOT RAM/IO, monitor ROM |
| $2000-image end | interpreter code, data, BSS, Z-stack |
| image end-$FEFF | dynamic memory, then page cache |
| $FF00-$FFFF | unused: the vector select switch overlays $FFFA-$FFFF with ROM |

The KIM platform probes every candidate page (write/read back two patterns)
so a board with less RAM still runs whatever fits.

`zm_load` takes the longest ascending run of consecutive free pages that holds
dynamic memory (header word $0E rounded up to a page) and loads story pages
0..n-1 there. Every other free page is a cache frame.

`page_map[512]` maps each story page (v3 stories are at most 128K) to the high
byte of the RAM page holding it, 0 when absent. Dynamic pages are pinned. A
miss evicts the least recently used frame (a 16-bit stamp per frame, set on
every lookup; stamps are reset when the counter wraps), never the page the PC
or the current string is executing from, then calls `st_seek` + `st_next`.

After dynamic memory, `zm_load` keeps reading the story sequentially into free
frames until they or the file run out: sequential reads need no seek.

Byte reads below the static base go straight to dynamic memory. The PC and
string decoder each keep a pointer to their current page and look up the next
page only when the offset wraps.

Addresses are page (`uint16_t`) plus offset (`uint8_t`); no 32-bit arithmetic
in the interpreter.

## Interpreter

- Version 3 only; all v3 opcodes.
- Header: Flags 1 bit 4 (status line not available) and bit 5 (no split
  screen) set; interpreter number 6, standard revision 1.1.
  `show_status`, `split_window`, `set_window`, `sound_effect` are no-ops;
  `output_stream` 2 (transcript) and 3 (memory) are supported, 4 is ignored;
  `input_stream` is ignored.
- Evaluation stack and call frames share one 1024-word Z-stack.
- Output is buffered a word at a time and wrapped at `zm_width`.
- `sread` lower-cases input, honours backspace (BS or DEL: removes the last
  character and prints space, backspace) and tokenises against the story
  dictionary (or the one given).
- `random`: xorshift; random(0) reseeds from `plat_entropy`, random(-n) seeds
  predictably.
- `save`/`restore` prompt for a file name (default: the story name with its
  extension replaced by `.SAV`) and write/read: magic `KZ3\1`, release (2),
  serial (6), checksum (2), dynamic size (2), stack pointer (2), frame pointer
  (2), PC (3), dynamic memory, used Z-stack words (little endian).
- `restart` reloads dynamic memory, preserving Flags 2 bits 0-1.
- `verify` sums the file through a borrowed cache frame.
- `quit` returns from `zm_run` to the launcher.

## KIM-1 platform

- Console via the monitor's `OUTCH`/`GETCH` (input is echoed by the TTY
  hardware; `'\n'` is sent as CR LF).
- Story: channel 2, opened with `<name>,P,R`. Seek is the sd2iec command
  `P` + chr($62) + 4-byte little-endian offset on channel 15. Reads are
  `kd_read` of 256 bytes.
- Save: channel 1 with `@0:<name>` (PRG); restore: channel 3 `<name>,P,R`.
- Directory: `kd_dir_*` from the KIMDOS C library.
- Entropy: the RIOT interval timer plus a counter run while waiting for keys.
- Exit: jump to the monitor (`$1C4F`).

File names from the drive are PETSCII. The launcher prints `c & 0x7F` and
matches the suffix `.Z3` case-insensitively on that.

## Tests

| Suite | What |
| --- | --- |
| `tests/test_host.py` | host build: CZECH compliance, the demo walkthrough, Zork I opening moves, save/restore, restart, small caches that force eviction |
| `tests/test_kim.py` | 6502 build on the KIMDOS py65 simulator with a simulated drive: launcher, demo walkthrough, Zork I first move, save/restore |

Fixtures that are not ours (CZECH, Zork I from historicalsource/zork1) are
downloaded once into `tests/fixtures/cache/` (gitignored).
