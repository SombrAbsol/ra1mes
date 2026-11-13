<!--
SPDX-FileCopyrightText: 2026 SombrAbsol

SPDX-License-Identifier: MIT
-->

# DAT Text File Format
Used in *Pokémon Ranger* to store texts. The game also uses the DAT extension for files holding other kinds of data, which follow other formats. The European release names the text files after their language (`.ukd`, `.frd`, `.ded`, `.itd` and `.esd`), but their format is the same.

## Format specifications
### Overview
* 32-bit and 16-bit little-endian integer values
* Strings are stored as sequences of 16-bit character and control codes
* Each string ends with a `0x0000` terminator, followed by `0x0000` padding so that its size is a multiple of 4 bytes

### Header
```rust
{
  u32 table_size // size of the offset table in bytes, a multiple of 4
}
```

Immediately followed by the offset table, then by the string data.

### Offset table
```rust
{
  u16 offset[0]
  u16 offset[1]
  // ...
  u16 offset[n-1]
}
```

Offsets are given in 4-byte units, relative to the start of the string data. The first offset is 0, and a string ends where the next one starts, the last one at the end of the file. If the number of strings is odd, the table is padded with a zero offset, which is not a string.

### String data
```rust
{
  u16 codes[] // 0x0000-terminated
  // 0x0000 padding
}
```

## Character codes
Codes `0x0020` to `0x01BF` are glyphs of the font. The Latin and Japanese fonts share the kana, the symbols and the ISO-8859-15 range (`0x0160` to `0x01BF`), but the Japanese font uses fullwidth forms for `0x0020` to `0x007E`. The full tables are listed in [tables.h](/include/tables.h).

## Control codes
When converted to JSON, control codes are written as markup. Characters used by the markup (`<`, `>`, `[`, `]`, `{` and `}`) are written as `<<>`, `<>>`, `<[>`, `<]>`, `<{>` and `<}>`. Unknown codes are written as their hexadecimal value, for instance `<0x1234>`.

| Codes | Markup | Description |
|---|---|---|
| `000A` | line break | New line |
| `000D 000C` | `<$>` | New dialog page after button press, followed by an implied line break |
| `000C` | `<*>` | New dialog page, followed by an implied line break |
| `0004`, `0005` | `{`, `}` | Start and end of the speaker's name |
| `0018` | `<@>` | Sound effect |
| `0007 X` | `<wfX>` | Wait, X being a number |
| `0002 T 0003` | `<myname>`, `<poke1>`, `<num1>`, etc. | Insertion of a variable, such as the player name, a Pokémon name or a number |
| `0002 0702 X 0003` | `<pokeNAME>` or `<poke#X>` | Name of the Pokémon species X |
| `FF01`, `FF05`, `FF06`, `FF07`, `FF0B` | `<r>`, `<dc>`, `<nb>`, `<nr>`, `<df>` | Commands without arguments |
| `FF0C X` | `<i>`, `<iconBURN>`, etc., or `<icon#X>` | Display icon X |
| `FF0D X` | `<pvNAME>` or `<pv#X>` | Play the cry X of a Pokémon |
| `FF10 X` | `<MSG_H>`, `<MSG_ALERT>`, `<MSG_SYSTEM>` or `<frame#X>` | Dialog frame X |
| `FFF4 X` | `<dicMSG_X>` | Dictionary entry X |
| `FFF5 F X` | `<FLAG X>` | Set flag F to the value X |
| `FF27 F` | `[FLAG]` | Condition on flag F |
| `FF21 X`, `FF22 X`, `FF25 X` | `[FLAG == X]`, `[FLAG < X]`, `[FLAG >= X]` | Comparison of the flag of the condition with the value X. Further comparisons are written `[ == X]` |
| `FF20` | `[/]` | End of a condition |
| `0153`, `0154`, `0155` | `<er>`, `<re>`, `<e>` | Small raised "er", "re" and "e" |

Flags are written by their name when known, or as `FLAG#X` otherwise. The values of `FLAG_G_MSG_SEQUENCE` are written by their name when known.
