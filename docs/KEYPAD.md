# Typing on the keypad (s22-t9d)

Reference for `tools/s22-t9d.c`, the keypad text input daemon.

Text input is multi-tap: tap a key repeatedly to cycle through its
characters; the character is final when you pause (about 0.9 s) or press a
different key. A tap on the same key within that time replaces the last
character, so you see each candidate as you go.

## Letter and number keys

| Key | Taps cycle through | Hold (½ s) |
|---|---|---|
| 1 | `.` `,` `?` `!` `-` `/` `_` `:` `;` `'` `"` `1` | `1` |
| 2 | `a` `b` `c` `2` | `2` |
| 3 | `d` `e` `f` `3` | `3` |
| 4 | `g` `h` `i` `4` | `4` |
| 5 | `j` `k` `l` `5` | `5` |
| 6 | `m` `n` `o` `6` | `6` |
| 7 | `p` `q` `r` `s` `7` | `7` |
| 8 | `t` `u` `v` `8` | `8` |
| 9 | `w` `x` `y` `z` `9` | `9` |
| 0 | space, `0` | `0` |
| `*` | `*` `+` `=` `@` `#` `$` `%` `&` `\|` `~` `` ` `` `^` `\` `(` `)` `[` `]` `{` `}` `<` `>` | (no hold) |

To type two letters from the same key (like `ll`), wait for the first to
settle, then tap again.

## Modes: `#`

Each tap of `#` moves to the next mode:

| Mode | Letters | Number keys |
|---|---|---|
| `abc` | lower case | letters (hold for the digit) |
| `Abc` | the **next** letter is a capital, then back to `abc` | letters |
| `ABC` | capitals until you change mode | letters |
| `123` | – | each tap is the digit itself |

`abc` → `Abc` → `ABC` → `123` → `abc`. In `123` mode, `*` still gives the
symbols, and `#` still changes mode (type a `#` character from the `*` list).

## Other keys

| Key | Does |
|---|---|
| **C** | Backspace (hold to repeat) |
| **OK** (centre of the D-pad) | Enter |
| **Call** (green) | Enter |
| **D-pad** | arrow keys (hold to repeat) |
| **◁ Back** | Esc |
| **○ Home** | Tab (shell completion) |
| **□ Recents** | Ctrl for the next character: □ then a letter. Press □ again to cancel |
| **End** (red) | tap: nothing; **hold: power off** |
| Volume up/down, speaker key, side key | passed through unchanged (no use on the console yet) |

Ctrl examples: □ `222` → Ctrl-C (stop a program), □ `3` → Ctrl-D (log
out / end of input), □ `666` → Ctrl-O (nano: save), □ `99` → Ctrl-X
(nano: exit), □ `555` → Ctrl-L (clear the screen), □ `777` → Ctrl-R (shell
history search).

## The indicator (top-right corner)

While you type, a small box in the top-right of the screen shows:

```
 abc 6:m[n]o6        mode, then the key's characters; the highlighted one is what you get
 abc ^               ^ = Ctrl is armed for the next character
 abc pw *** 7:pq[r]s7   at a password prompt: one * per character typed
```

At a password prompt only the character being chosen is shown, and only
until it is final; everything already typed is a `*`, so you can count
them. `C` removes one, Enter starts over. The box clears itself after a
few seconds without typing.

## Settings

`/etc/conf.d/s22-t9d`, then `rc-service s22-t9d restart`:

```
T9D_ARGS="-t 900 -l 500"   # -t: ms to tap the same key again, -l: ms to hold for a digit
T9D_ARGS="-q"              # no indicator
T9D_ARGS="-b 8000"         # keypad backlight on for 8 s after the last key (default 5 s)
T9D_ARGS="-b 0"            # leave the keypad backlight alone
```

The keypad backlight comes on at the first key press and goes off a few
seconds after the last one.

`rc-service s22-t9d stop` gives the raw keypad back (digits only, no
letters); `start` brings T9 back.

