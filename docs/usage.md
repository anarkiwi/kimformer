# Usage

## What you need

- A KIM-1, PAL-II or replica with a serial terminal on the TTY port. A PAL-II
  with its default switch settings (RAM at $0000-$13FF and $2000-$FFFF,
  vectors from the monitor ROM) runs everything; see [Memory](#memory) for
  smaller boards.
- The [KIMDOS adapter](https://github.com/anarkiwi/kimdos/blob/main/docs/hardware.md)
  on the user port and an sd2iec as device 8. Story files are read with the
  sd2iec `P` (position) command on ordinary files, which Commodore drives do
  not support.
- An SD card with version 3 story files (`.z3`), e.g. Zork I.
- A VT100/ANSI terminal, 80x24: line 1 is the status line (location, and
  score and moves or the time), lines 2-24 scroll. `make kim VT100=0` builds
  for a plain TTY instead, without a status line.

## Building

```
git submodule update --init
make kim
```

| File | |
| --- | --- |
| `build/kimformer.bin` | raw image, load at $2000, start at $2000 |
| `build/kimformer.ptp` | the same as MOS paper tape, for the monitor's `L` |
| `build/kimformer.prg` | the same with a $2000 load address, for the SD card |

`make kim VT100=0` leaves out the status line and its escape sequences, for
a terminal that is not VT100 compatible. `make kim` prints `__kim_end` (the
end of the image and its stack) and the free pages above it, and fails if
fewer than `KIM_MIN_PAGES` (96: Zork I's 45 pages of dynamic memory and a
cache) remain.

### Release images

Each [GitHub release](https://github.com/anarkiwi/kimformer/releases) carries
the images, so no toolchain is needed:

| Asset | |
| --- | --- |
| `kimformer.ptp` | paper tape, VT100 build: `kimtape load --port /dev/ttyUSB0 kimformer.ptp --go 2000` |
| `kimformer.prg` | the same for the SD card, copied as `KIMFORMER.PRG` |
| `kimformer-tty.ptp`, `kimformer-tty.prg` | the plain TTY build (`VT100=0`); copy the `.prg` as `KIMFORMER.PRG` |
| `demo.z3` | the [demo adventure](demo.md), for the SD card |

[kimtape](https://github.com/anarkiwi/kimtape) is the only tool needed
(`pip install git+https://github.com/anarkiwi/kimtape`); Zork I is not
included.

## The SD card

Copy the `.z3` files to the card. sd2iec shows FAT names upper case, and a
file whose extension it does not know (`.z3`, `.sav`) as a `PRG` file under
its full name, so `zork1.z3` lists as `"ZORK1.Z3" PRG`. Keep names to 8.3 so
they fit the 16-character CBM name. The menu lists the drive's current
directory, so stories can live in a subdirectory selected with the KIMDOS
shell (`@CD:GAMES`) before starting the interpreter.

To load the interpreter from the card, copy `build/kimformer.prg` as
`KIMFORMER.PRG`; it lists as `"KIMFORMER" PRG`.

## Loading the interpreter

Paper tape, from a PC on the TTY line:

```
kimtape load --port /dev/ttyUSB0 build/kimformer.ptp --go 2000
```

From the SD card with the KIMDOS shell: the shell must sit outside
$2000-`__kim_end`, so build it for $0400 (`make ORG=0400` in KIMDOS; it fits
below $1400) and load and start it at $0400. Then:

```
>L KIMFORMER
$2000-$6152
>Q
```

and in the monitor type `2000`, space, `G`. The interpreter uses $0400-$13FF
as cache, so the shell has to be loaded again afterwards.

## Playing

```
KIMFORMER Z-MACHINE V3
1. DEMO.Z3
2. ZORK1.Z3
Story (Q quits): 2
....................
ZORK I: The Great Underground Empire
...
```

On a VT100 the interpreter first sets the scroll region to lines 2-24 and
starts on line 24; before each input it draws the status line on line 1 in
reverse video (saving and restoring the cursor), and it resets the scroll
region when it returns to the monitor. It sends no other escape sequences.

Type the story's number and Enter. A dot is printed for every 8 pages (2K)
read while loading. Input may be upper or lower case; BS or DEL deletes the
last character. Output is wrapped at 80 columns.

- `save` and `restore` ask `File [ZORK1.SAV]: `; Enter takes the name in
  brackets. A save replaces an existing file of that name (`@0:`). A restore
  only accepts a file saved from the same story release.
- Drive errors print the drive's status line, e.g. `62,FILE NOT FOUND,00,00`
  for a restore of a missing file, and the game reports the failure.
- `quit` (and `y`) returns to the menu; `Q` at the menu returns to the KIM-1
  monitor. `2000 G` starts the interpreter again without reloading it.
- `restart` and `verify` work as in the story's manual. `verify` reads the
  whole file from the card.

Fatal interpreter errors print `Error <code> at $<PC>` and return to the
menu; loading errors print `Error <code>` (codes in `src/zm.h`: 1 the file
could not be opened or read, 2 not version 3, 3 not enough memory).

## Smoke test on a board

`python -m kimformer.board` loads the interpreter with the monitor, checks it
and plays a script over the TTY line, as a check of a board, its SD card and
a build:

```
python -m kimformer.board --port /dev/ttyUSB0 [--baud 1200] [--image build/kimformer.ptp]
    [--no-load] [--story DEMO.Z3] [--script FILE] [--expect FILE] [--timeout S] [--log FILE]
```

Press RS first. The image is loaded, verified (and reloaded if it does not
verify) and started at $2000; `--no-load` starts the image already in memory.
The story is picked from the menu by name, then each line of the script is
typed once the interpreter waits for input (the `>` prompt, after the status
line on a VT100 build, or a file name prompt). With no `--script` the card
needs `DEMO.Z3` (from `make demo` or a release): it plays
`games/demo.walkthrough` and checks `games/demo.expect`. Each line's time from
sending to the next prompt is printed; the transcript, without escape
sequences, goes to `logs/board-<time>.log`. The exit status is 1 if an
expected line is missing; no prompt within `--timeout` seconds of a line is
an error.

## Memory

The interpreter probes every page of $0400-$13FF and `__kim_end`-$FEFF
(inverts a byte and reads it back, then restores it) and uses those that
hold. A page that mirrors other memory, as on a board that does not decode
every address line, is skipped. A story's dynamic memory (Zork I: 45 pages)
must fit one unbroken run of RAM pages; every other page found is a cache
frame for the rest of the story, which is paged in from the card on demand.
At least 3 cache frames are needed.

| Board | Pages | Zork I |
| --- | --- | --- |
| PAL-II defaults | 16 at $04-$13, 138 at $75-$FE | 109 cache frames of 340 story pages |
| RAM $0000-$03FF, $2000-$AFFF | 59 at $75-$AF | 14 cache frames: 25-48 page loads a move |
| RAM $0000-$03FF, $2000-$A3FF | 47 at $75-$A3 | `Error 3`: too few cache frames |

With fewer frames the game runs the same but reads the card more often: a
page miss costs a seek and a 256-byte read, about 0.17 s.

## Performance

Measured on the simulated KIM-1 (1 MHz, the KIMDOS drive model's IEC timing,
the TTY at 8300 cycles per character, about 1200 baud) with Zork I and the 15
opening moves of `tests/test_host.py`. `make bench` (`python -m kimformer.prof
--bench`) plays them with a fixed entropy source and reports, per move, the
interpreter's cycles without story I/O and console time, the I/O cycles and
the seeks:

| | Interpreter | With story I/O | With TTY output |
| --- | --- | --- | --- |
| start to the menu | 0.2 s | | 0.8 s |
| load (154 pages) | | 23.2 s | 27.5 s with the opening text |
| one move, PAL-II | 0.74-1.17 s, mean 1.00 s | 0.80-1.99 s, 0-4 page loads | 2.2-6.5 s |
| one move, 14 cache frames | 0.75-1.19 s | 5.8-11.6 s, 25-48 page loads | |

`make bench` before and after the interpreter core was reworked for the
6502 (fixed locals and split 8-bit evaluation stack, zero page PC pointer,
inline operand and local access, clock replacement):

| | Interpreter cycles per move | Image end | PAL-II cache frames |
| --- | --- | --- | --- |
| before | 2,021,291 (1,546,987-2,324,577) | $7700 | 107 |
| after | 997,653 (739,986-1,166,768) | $7500 | 109 |

For reference, Infocom's own C64 interpreter (Version H, hand-written 6502)
takes about 1.0M cycles of interpreter time per Zork I move (release 88,
nine opening moves in VICE, page loads excluded; about 0.9M without the C64's
interrupt and video overhead).

The status line is about 85 characters a move, 0.7 s at 1200 baud; a faster
terminal line or `VT100=0` saves most of that.
