#include <argp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

const char *format = "tmp.byteshuf.partial.%d";

typedef struct Data {
    uint8_t contents[BYTES_PER];
} Data;

struct arguments {
    bool verbose;
    size_t per_file, chunk_size, read_header, write_header;
    char *input, *output, *directory;
};

static struct argp_option options[] = {
    { "verbose",      'v', 0,      0, "Produce verbose output",                      0 },
    { "directory",    'd', "DIR",  0, "Directory of files of unshuffled bytes",      1 },
    { "input",        'i', "FILE", 0, "Source file of unshuffled bytes",             1 },
    { "output",       'o', "STR",  0, "Output file prefix for shuffled bytes",       1 },
    { "per-file",     'n', "NUM",  0, "Samples per output file (134217728)",         2 },
    { "chunk-size",   's', "NUM",  0, "Samples per interim file (134217728)",        2 },
    { "read-header",  'r', "NUM",  0, "Bytes to skip when reading source files (0)", 3 },
    { "write-header", 'w', "NUM",  0, "Bytes to zero out for each output file (0)",  3 },
    { 0 },
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {

    struct arguments *arguments = state->input;

    switch (key) {
        case 'v': arguments->verbose      = true;        break;
        case 'd': arguments->directory    = strdup(arg); break;
        case 'i': arguments->input        = strdup(arg); break;
        case 'o': arguments->output       = strdup(arg); break;
        case 'n': arguments->per_file     = atoll(arg);  break;
        case 's': arguments->chunk_size   = atoll(arg);  break;
        case 'r': arguments->read_header  = atoll(arg);  break;
        case 'w': arguments->write_header = atoll(arg);  break;
        case ARGP_KEY_ARG: return 0;
        default: return ARGP_ERR_UNKNOWN;
    }

    return 0;
}


static bool is_directory(const char *fname) {
    struct stat path_stat;
    stat(fname, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

static FILE* open_file_in_directory(struct arguments *args, const char *fname) {

    char tempfile[512];
    sprintf(tempfile, "%s%s", args->directory, fname);
    return fopen(tempfile, "rb");
}


static uint64_t rand64() {

    // http://vigna.di.unimi.it/ftp/papers/xorshift.pdf

    static uint64_t seed = 1070372ull;
    seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
    return seed * 2685821657736338717ull;
}

static void swap_data(Data *data, int x, int y) {
    Data temp = data[x]; data[x] = data[y]; data[y] = temp;
}

static void shuffle_and_save(struct arguments *args, size_t count, Data *data, int *nfiles) {

    char tempfile[512];
    sprintf(tempfile, format, (*nfiles)++);

    for (size_t i = 0; i < count; i++)
        swap_data(data, i, rand64() % count);

    if (count) {
        FILE *fout = fopen(tempfile, "wb");
        fwrite(data, sizeof(Data), count, fout);
        fclose(fout);
    }

    if (args->verbose)
        printf("Read %zd entries and saved to %s\n", count, tempfile);
}

static size_t process_input_file(struct arguments *args, FILE *fin, Data *data, size_t leftovers, int *nfiles) {

    size_t entries_read;

    do {

        const size_t read_size = args->chunk_size - leftovers;
        entries_read = fread(&data[leftovers], sizeof(Data), read_size, fin);

        if (entries_read == read_size)
            shuffle_and_save(args, args->chunk_size, data, nfiles);

        leftovers = (entries_read == read_size) ? 0 : entries_read;

    } while (!leftovers && entries_read);

    return entries_read ? leftovers : 0;
}

static size_t read_all_input_files(struct arguments *args, int *nfiles) {

    size_t leftovers = 0;
    Data *data = malloc(sizeof(Data) * args->chunk_size);

    if (args->directory != NULL) {

        struct dirent *dirent;
        DIR *directory = opendir(args->directory);

        if (!directory) {
            printf("Unable to read directory: %s\n", args->directory);
            exit(EXIT_FAILURE);
        }

        while ((dirent = readdir(directory)) != NULL) {

            if (dirent->d_name[0] == '.')
                continue;

            FILE *fin = open_file_in_directory(args, dirent->d_name);

            if (args->read_header) {
                uint8_t header[args->read_header];
                size_t entries_read = fread(header, sizeof(uint8_t), args->read_header, fin);
            }

            leftovers = process_input_file(args, fin, data, leftovers, nfiles);
            fclose(fin);
        }

        closedir(directory);
    }

    if (args->input != NULL) {

        FILE *fin = fopen(args->input, "rb");

        if (args->read_header) {
            uint8_t header[args->read_header];
            size_t entries_read = fread(header, sizeof(uint8_t), args->read_header, fin);
        }

        leftovers = process_input_file(args, fin, data, leftovers, nfiles);
        fclose(fin);
    }

    if (leftovers)
        shuffle_and_save(args, leftovers, data, nfiles);

    return leftovers;
}


static FILE *open_output_file(struct arguments *args, int out_idx) {

    /// When using a single output file, simply open the output filename
    /// that was provided. Otherwise, open files appending .%d for the out_idx

    if (!args->per_file)
        return fopen(args->output, "wb");

    char fname[512];
    sprintf(fname, "%s.%d", args->output, out_idx);
    return fopen(fname, "wb");
}

static void pop_and_save(FILE *fout, FILE **partials, size_t *remaining, size_t total_remaining) {

    /// Randomly select a partial file, weighted based on the remaining entries in each
    /// partial file, and then pop off the top entry from that file and save it. Close
    /// partial files once all of the entries in the file have been popped off

    Data data;
    int input_idx = 0;
    uint64_t pop = rand64() % total_remaining;

    while (pop >= remaining[input_idx])
        pop -= remaining[input_idx++];

    if (fread(&data, sizeof(Data), 1, partials[input_idx]) != (size_t) 1)
        printf("Error trying to read files...\n");
    fwrite(&data, sizeof(Data), 1, fout);

    if (!--remaining[input_idx])
        fclose(partials[input_idx]);
}

static void close_output_file(struct arguments *args, int out_idx, FILE *fout, size_t total, size_t saved) {

    /// Close file and report progress on total outputs when in verbose mode

    if (args->verbose && !args->per_file)
        printf("Finished writing to %s (%zd of %zd)\n", args->output, saved, total);

    else if (args->verbose)
        printf("Finished writing to %s.%d (%zd of %zd)\n", args->output, out_idx, saved, total);

    fclose(fout);
}

static void output_from_partials(struct arguments *args, FILE **partials, size_t *remaining, int nfiles) {

    /// While there are still entries in the data files, grab one at a time
    /// randomly and save it to the output file. Use multiple, fixed length,
    /// output files unless told otherwise. Randomization is a function of the
    /// number of remaining entries in a given partial file at the current time

    const size_t total_entries = remaining[nfiles - 1] + args->chunk_size * (nfiles - 1);
    const size_t entries_per   = !args->per_file ? total_entries : args->per_file;
    const int cnt_output_files = !args->per_file ? 1 :  1 + (total_entries - 1) / args->per_file;

    size_t total_saved = 0;

    for (int out_idx = 0; out_idx < cnt_output_files; out_idx++) {

        FILE *fout = open_output_file(args, out_idx);

        if (args->write_header) {
            uint8_t header[args->write_header];
            memset(header, 0, args->write_header);
            fwrite(header, sizeof(uint8_t), args->write_header, fout);
        }

        for (size_t i = 0; i < entries_per && total_entries != total_saved; i++, total_saved++)
            pop_and_save(fout, partials, remaining, total_entries - total_saved);

        close_output_file(args, out_idx, fout, total_entries, total_saved);
    }
}


int main(int argc, char *argv[]) {

    struct argp argp = { options, parse_opt, "", "" };
    struct arguments arguments = { false, 134217728, 134217728, 0, 0, NULL, NULL, NULL };
    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    if (arguments.verbose) {
        const int file_size = sizeof(Data) * arguments.chunk_size / (1024 * 1024);
        printf("Shuffling in chunks of %d bytes\n", BYTES_PER);
        printf("Using tempfiles of size %dMB\n", file_size);
        printf("Saving %zd entries per output file\n", arguments.per_file);
        printf("Storing %zd entries per interim file\n", arguments.chunk_size);
    }

    int nfiles = 0;
    FILE **partials;
    size_t *remaining;
    size_t leftovers = read_all_input_files(&arguments, &nfiles);

    partials  = malloc(nfiles * sizeof(FILE* ));
    remaining = malloc(nfiles * sizeof(size_t));

    for (int i = 0; i < nfiles; i++) {

        char tempfile[512];
        sprintf(tempfile, format, i);

        partials[i]  = fopen(tempfile, "rb");
        remaining[i] = arguments.chunk_size;
    }

    if (leftovers) remaining[nfiles-1] = leftovers;

    output_from_partials(&arguments, partials, remaining, nfiles);
}
