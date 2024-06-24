byteshuf is a linux tool that allows shuffling extremely large files or directories of files, using the algorithm behind the [terashuf](https://github.com/alexandres/terashuf) project. btyeshuf is a single standalone .c file, and can be built via `make N=...`, where N is the size of each byte chunk. Instead of shuffling lines of text, we shuffle groupings of N-bytes.

## Arguments
| Name | Flag | Purpose |
| -              | -    | - |
| `verbose`      | `-v` | Verbose reporting on the progress being made |
| `directory`    | `-d` | Directory strictly containing files we intend to shuffle |
| `input`        | `-i` | Location of the sole file we intend to shuffle |
| `output`       | `-o` | Output path. Each fileoutput file has a `.%d` suffix appended |
| `per-file`     | `-n` | Number of N-byte samples to output per final output file |
| `chunk-size`   | `-s` | Number of N-byte samples to output per intermediate output file |
| `read-header`  | `-r` | Skip fixed-size headers, by skipping x-bytes on all inputs files |
| `write-header` | `-w` | Writes a zero'ed out header of size x-bytes to each final output file |
