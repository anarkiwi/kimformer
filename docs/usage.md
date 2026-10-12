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
- An SD card with story files of version 3, 4, 5 or 8 (`.z3`, `.z4`, `.z5`,
  `.z8`), e.g. Zork I or games written with Inform 5/6 or PunyInform. Inform
  7 and Dialog games run, but too slowly at 1 MHz to play.
- A VT100/ANSI terminal, 80x24: in version 3, line 1 is the status line (location, and
  score and moves or the time), lines 2-24 scroll. `make kim VT100=0` builds
  for a plain TTY instead, without a status line. Version 4 and later games
  draw their own status line, menus and quote boxes in the upper window,
  which needs the VT100; on a plain TTY that text is not shown (see
  [Playing](#playing)).

## Building

```
pip install -e .
make kim
```

| File | |
| --- | --- |
| `build/kimformer.bin` | raw image, load at $2000, start at $2000 |
| `build/kimformer.ptp` | the same as MOS paper tape, for the monitor's `L` |
| `build/kimformer.prg` | the same with a $2000 load address, for the SD card |

`make kim VT100=0` leaves out the status line and its escape sequences, for
a terminal that is not VT100 compatible. `make kim JIFFY=0` makes the
interpreter use the standard serial protocol even with a JiffyDOS device; see
[JiffyDOS](#jiffydos). `make kim` prints `__kim_end` (the
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

The KIMDOS shell at $0400, to load `KIMFORMER.PRG` from the card, is a
[KIMDOS release](https://github.com/anarkiwi/kimdos/releases) asset:
`kimtape load --port /dev/ttyUSB0 https://github.com/anarkiwi/kimdos/releases/latest/download/kimdos-0400.ptp --go 0400`.
[kimtape](https://github.com/anarkiwi/kimtape) is the only tool needed
(`pip install kimtape`); Zork I is not included.

## The SD card

Copy the story files (`.z3`, `.z4`, `.z5`, `.z8`) to the card. sd2iec shows
FAT names upper case, and a file whose extension it does not know (`.z5`,
`.sav`) as a `PRG` file under its full name, so `zork1.z3` lists as
`"ZORK1.Z3" PRG`. Keep names to 8.3 so
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
$2000-`__kim_end`, so use the $0400 build, `kimdos-0400.ptp` from the KIMDOS
releases (also in the kimdos Python package, `kimdos.build.packaged(0x400)`).
Copy `build/kimformer.prg` to the card as `KIMFORMER.PRG`, then load and
start the shell by paper tape:

```
kimtape load --port /dev/ttyUSB0 https://github.com/anarkiwi/kimdos/releases/latest/download/kimdos-0400.ptp --go 0400
```

In the shell, load the interpreter (the shell prints the inclusive range) and
return to the monitor:

```
>L KIMFORMER
$2000-$xxxx
>Q
```

and in the monitor type `2000`, space, `G`. The interpreter uses $0400-$13FF
as cache, so the shell has to be loaded again afterwards.
`tests/test_kim.py::test_bootstrap_from_disk` runs this sequence on the
simulator.

## Playing

```
KIMFORMER Z-MACHINE
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
could not be opened or read, 2 not version 3, 4, 5 or 8, 3 not enough
memory).

Version 4 and later games:

- On a VT100 the game owns the screen: its status line and quote boxes are
  drawn in the upper window at the top, and the story text scrolls below it,
  always continuing on the bottom line. Bold and reverse video work; italic
  shows underlined; colours are ignored; a beep is BEL.
- Cursor keys work in menus and as line terminators where the game asks for
  them. The KIM-1 TTY echoes every key: for single-key input the interpreter
  moves the cursor back and blanks it, but Enter still moves down a line.
- On a plain TTY (`VT100=0`) the upper window is not shown, so the status
  line, quote boxes and the text of menus are missing; the story text is all
  there.
- `undo` is not available (games say so); timed input never times out, so
  real-time games wait for a key.

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
every address line, is skipped. The start of the story up to its global
variables and object table (Zork I: 13 pages) must fit one unbroken run of
RAM pages; every other page found is a cache frame for the rest of the story,
which is paged in from the card on demand. A page the game writes stays in
RAM, so the frames must hold the rest of dynamic memory (Zork I: 32 pages),
one page of page map per 64K of story beyond the first, and 3 more.

| Board | Pages | Zork I (340 pages) |
| --- | --- | --- |
| PAL-II defaults | 16 at $04-$13, 110 at $91-$FE | 112 cache frames: 3.0 page loads a move over the whole game |
| RAM $0000-$03FF, $2000-$C3FF | 51 at $91-$C3 | 37 cache frames: 58 page loads a move |
| RAM $0000-$03FF, $2000-$C0FF | 48 at $91-$C0 | `Error 3`: too few cache frames |

The Library Of Horror (version 5, 162 pages, 14 of them dynamic, 11 pinned)
fits in the PAL-II's RAM whole, with 115 cache frames.

With fewer frames the game runs the same but reads the card more often: a
page miss costs a seek and a 256-byte read, about 0.10 s with JiffyDOS and
0.17 s without.

## JiffyDOS

The interpreter speaks JiffyDOS to an sd2iec (or any drive that answers it)
and the standard serial protocol to the rest, deciding on every command; it
reads the story about 1.7 times as fast. If a drive misbehaves with it,
`make kim JIFFY=0` builds an interpreter that always uses the standard
protocol (the KIMDOS shell's `J0` does the same for the shell). The JiffyDOS
code takes two pages of the image, two cache frames.

## Performance

Measured on the simulated KIM-1 (1 MHz, the KIMDOS drive model's IEC timing,
the TTY at 8300 cycles per character, about 1200 baud) with Zork I and the 15
opening moves of `tests/test_host.py`. `make bench` (`python -m kimformer.prof
--bench`) plays them with a fixed entropy source and reports, per move, the
interpreter's cycles without story I/O and console time, the I/O cycles and
the seeks:

Story I/O columns give JiffyDOS / the standard protocol (`make bench`
against a drive with and without JiffyDOS: `--no-jiffy`):

| | Interpreter | With story I/O |
| --- | --- | --- |
| load (126 pages) | | 11.8 s / 20.3 s |
| one move, PAL-II | 0.75-1.18 s, mean 1.01 s | 0.92-2.35 s / 0.99-3.22 s, 1-11 seeks |

The TTY adds 8.3 ms per character at 1200 baud.

`make bench` before and after the interpreter core was reworked for the
6502 (fixed locals and split 8-bit evaluation stack, zero page PC pointer,
inline operand and local access, clock replacement):

| | Interpreter cycles per move | Image end | PAL-II cache frames |
| --- | --- | --- | --- |
| before | 2,021,291 (1,546,987-2,324,577) | $7700 | 107 |
| after | 997,653 (739,986-1,166,768) | $7500 | 109 |

and before and after versions 4, 5 and 8 were added on unified paging (the
earlier build with dynamic memory pinned, the `PAGEDYN=1` experiment, this
build):

| | Interpreter cycles per move | Image end | PAL-II cache frames |
| --- | --- | --- | --- |
| dynamic memory pinned | 996,901 | $7700 | 107 |
| `PAGEDYN=1` | 1,004,713 | $7900 | 137 |
| versions 3, 4, 5, 8 | 1,010,774 (752,014-1,175,926) | $9100 | 112 |

The Library Of Horror (PunyInform, version 5), played to its end with
`python -m kimformer.prof --bench --walk tests/stories/library_of_horror.walk
STORY`: 1.36M interpreter cycles per turn on average (median 1.39M, at most
3.2M), 48 seeks in 45 inputs.

For reference, Infocom's own C64 interpreter (Version H, hand-written 6502)
takes about 1.0M cycles of interpreter time per Zork I move (release 88,
nine opening moves in VICE, page loads excluded; about 0.9M without the C64's
interrupt and video overhead).

Dynamic memory is paged with static memory; [paging.md](paging.md) has the
measurements that chose this design.

The version 3 status line is about 85 characters a move, 0.7 s at 1200 baud; a faster
terminal line or `VT100=0` saves most of that.
