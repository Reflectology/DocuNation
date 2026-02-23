# DocuNation
Documentation generation
DocuNation is a zero-dependency C17 documentation generator designed to turn entire source trees into searchable reference bundles (text, JSON, HTML) in a single pass. It ships as one self-contained binary (`docunation`) and powers large corpus runs.

## Features
- Parses C sources for functions, structs/unions/enums, typedefs, macros, and globals
- Reads preceding comments as docstrings and preserves signatures/return types
- Emits colored console summaries plus JSON and HTML artifacts
- Bulk mode (`-R`/`-O`) walks whole directories, mirrors folder structure, and writes `txt/`, `json/`, `html/`, and an `index.html`
- Safe string utilities and large buffers (`MAX_SIG=8192`) guard against overlap/overflow on huge prototypes
- Requires only the system C toolchain (no external libs)

## Build
```sh
cc -O2 -o docunation docunation.c
```
Optional: wrap this in CMake or a container build to keep the workflow consistent across repos.

## Usage
```sh
./docunation path/to/file.c          # Pretty text output
./docunation -j path/to/file.c       # JSON document on stdout
./docunation -h path/to/file.c       # HTML page on stdout
```

### Bulk Documentation
```sh
./docunation -R /path/to/src -O /path/to/out
```
This produces:
- `/path/to/out/txt/*.txt`
- `/path/to/out/json/*.json`
- `/path/to/out/html/*.html`
- `/path/to/out/index.html` (table linking every source file to its outputs)
