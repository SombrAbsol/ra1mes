<!--
SPDX-FileCopyrightText: 2026 SombrAbsol

SPDX-License-Identifier: MIT
-->

# ra1mes
<a href="https://github.com/SombrAbsol/ra1mes/actions/workflows/ci.yml"><img src="https://github.com/SombrAbsol/ra1mes/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
<a href="https://opensource.org/license/mit"><img src="https://img.shields.io/badge/license-MIT-blue" alt="License: MIT (Expat)"></a>

DAT text file converter for *Pokémon Ranger*.

This game stores its texts in DAT files, an extension it also uses for files holding other kinds of data. This tool allows you to convert DAT text files to JSON and vice versa, writing their control codes as readable markup. You can [download the latest build](#download) or [build the program from source](#building).

For more information about the DAT text format, see [the documentation](/docs/dat.md). To convert the texts of *Pokémon Ranger: Shadows of Almia* and *Pokémon Ranger: Guardian Signs*, see [ra23mes](https://github.com/SombrAbsol/ra23mes).

## Download
|        | Linux | macOS | Windows |
| ------ | ----- | ----- | ------- |
| Latest | [Download](https://github.com/SombrAbsol/ra1mes/releases/download/latest/ra1mes-linux.zip) | [Download](https://github.com/SombrAbsol/ra1mes/releases/download/latest/ra1mes-macos.zip) | [Download](https://github.com/SombrAbsol/ra1mes/releases/download/latest/ra1mes-windows.zip) |

## Usage
### Dumping the ROM
You can dump your own *Pokémon Ranger* ROM from:
* [a console from the Nintendo DS or Nintendo 3DS family](https://dumping.guide/carts/nintendo/ds) (Game Card release)
* [a Wii U](https://wiki.hacks.guide/wiki/Wii_U:VC_Extract) (Virtual Console release)

### Getting the DAT text files
To get the DAT text files, Windows users can run [TinkeDSi](https://github.com/R-YaTian/TinkeDSi), but [NDSFactory](https://github.com/Luca1991/NDSFactory) can also be run on macOS and Linux:
1. Download [the latest NDSFactory release](https://github.com/Luca1991/NDSFactory/releases/latest), then extract the archive and run the executable
2. Open the program, load your ROM, then scroll down until you see the `FAT Files Address` field. Take note or copy its value
3. Press the `Extract Everything` button and choose where to save your files
4. Once the process is complete, go to the `Fat Tools` tab, and fill in the first three fields with the requested files you just extracted (`fat_data.bin`, `fnt.bin` and `fat.bin`) and the fourth with the value from the previous `FAT Files Address` field
5. Press the `Extract FAT Data!` button and choose where to save your files
6. Go to your output directory. DAT text files are in the `text` and `mission` directories

DAT text files have the `.dat` extension, except in the European release, which has one file per language with the extensions `.ukd` (English), `.frd` (French), `.ded` (German), `.itd` (Italian) and `.esd` (Spanish). These files have the same format as the `.dat` ones.

The game also has DAT files holding other kinds of data, which ra1mes cannot convert.

### Running ra1mes
> [!IMPORTANT]
> ra1mes is a command-line program and must be run in a terminal.

* To convert a DAT text file to JSON, or a JSON file to DAT, run `ra1mes <infile>`. The conversion is chosen from the contents of the input file, whatever its extension
* To convert every DAT text file and JSON file in a directory, run `ra1mes <indir>`. Text files are recognized by their structure, and other files, such as DAT files holding other data, are skipped
* To convert several files or directories at once, list them, for instance `ra1mes a.frd b.frd` or `ra1mes *.frd`. Wildcards such as `*` and `?` also work on Windows

By default, the output file is written next to the input file with the same name, replacing its extension with `.json` or `.dat`. However, you can optionally specify the output file of a single input file with `-o` or `--output`, or a directory to write the output files in with `--output-dir`, for instance:
* `ra1mes in.frd -o out.json` will output the JSON file as `out.json`
* `ra1mes in.json -o in.frd` will output the DAT file as `in.frd`
* `ra1mes indir --output-dir=outdir` or `ra1mes *.frd --output-dir=outdir` will write the output files in `outdir`, created if missing

`-` as input reads the standard input, written to the standard output unless `-o` is given, and `-o -` writes to the standard output.

The Japanese release uses another font, whose characters `0x20` to `0x7E` are fullwidth. Run `ra1mes -j <in>` or `ra1mes --japanese <in>` (with `<in>` being an input file or directory) to convert its DAT text files.

If an output file already exists, you will be asked whether to replace it. Run `ra1mes -y <in>` or `ra1mes --yes <in>` to replace existing files without asking.

Run `ra1mes -h` or `ra1mes --help` to list all the options.

## Building
### Dependencies
* `clang` or `gcc`
* `make`

### Steps
1. If you don't already have them, install the dependencies
2. Clone this repository by running `git clone https://github.com/SombrAbsol/ra1mes`, or [download the ZIP archive](https://github.com/SombrAbsol/ra1mes/archive/refs/heads/main.zip) and extract it
3. Go to the repository directory and build the program by running `make`

> [!TIP]
> Running `make` or `make release` will generate a release build. The downloadable releases are static builds generated using this recipe. If you want to generate a native or a debug build, run `make native` or `make debug`. Native builds are optimized for your specific CPU for better performance but may not be compatible with other systems, while debug builds include debugging symbols that help diagnose issues but run slower.
>
> Unix-like operating systems (such as Linux and macOS) can run `sudo make install` to install ra1mes system-wide. It installs the program already built, preferably a release or native build, or generates a release build first if there is none. Use `sudo make uninstall` to remove it.
>
> If you need to rebuild the program, run `make clean` or delete the `build` directory.

## Credits
* ra1mes by [SombrAbsol](https://github.com/SombrAbsol)

## License
ra1mes is free software. You can redistribute it and/or modify it under the [terms of the Expat License](/LICENSE) as published by the Massachusetts Institute of Technology.
