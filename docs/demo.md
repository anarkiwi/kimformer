# Demo adventure

`games/demo.inf` is a five-room Inform 6 adventure compiled to a version 3
story file (about 4.5K) with no standard library: it reads input with `sread`
and parses verb + optional noun from the dictionary addresses in the parse
buffer. It is small enough to load quickly over the IEC bus and touches most
of the interpreter:

| Feature | Where |
| --- | --- |
| object tree (`insert_obj`, `get_child`/`get_sibling`/`get_parent`, `jin`) | moving, scope, nested contents listing |
| attributes | `light`, `portable`, `container`, `open`, `locked`, `scenery`, `scored` |
| properties (`get_prop`, `get_prop_addr`, `get_prop_len`, variable property numbers) | exits, `name` matching, points |
| routines as property values, indirect `call` with arguments | `react` on the lamp and the meadow |
| `print_obj`, `print_paddr`, `print_addr`, `print_char`, `print_num` | names, tables of strings, verb echo, unknown words, score |
| `output_stream` 3 | capitalised object names |
| abbreviations | compiled with `-e` |
| `random` | birdsong in the meadow |
| `save`, `restore`, `restart`, `verify`, `quit` | meta verbs |
| status line globals | `location`, `score`, `turns` are globals 0-2 |

## Map

```
Cellar (dark)
  | up/down
Hall
  | north/south, through the locked oak door
Porch
  | north/south
Meadow --east/west-- Shed (lamp; box holding the key)
```

Verbs: `look`/`l`, `n`/`s`/`e`/`w`/`u`/`d` (or `north`...), `go <dir>`,
`take`/`get`, `drop`, `inventory`/`i`, `examine`/`x`, `open`, `unlock`,
`light`, `score`, `save`, `restore`, `restart`, `verify`, `quit`/`q`.
Words after the verb other than `the`, `a`, `with` are taken as the noun.

Score (50): unlock the door 10, see the cellar 10, take the crown 20, carry
it back to the meadow 10 (wins).

## Build and check

```
make tools-image   # docker/Dockerfile: Inform 6.43 and dfrotz 2.55
make demo          # build/demo.z3
make demo-check    # play games/demo.walkthrough in dfrotz, match games/demo.expect
```

`games/demo.walkthrough` is the winning input, one command per line (save and
restore are each followed by a file name). It also restarts once, enters the
cellar in the dark, saves, changes state and restores. `games/demo.expect`
lists lines the transcript must contain in order; `games/expect.awk` checks a
transcript against it: `awk -f games/expect.awk games/demo.expect transcript`.
