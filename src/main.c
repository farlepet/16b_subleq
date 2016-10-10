#include <stdio.h>
#include <fcntl.h>
#include <wchar.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>

#include <cpu.h>

int16_t prog[] = {
    0, 0, 0,
    12, 0, 6,
    0, 0x101, 9,
    0x100, 0x100, 6,
    'C'
};

uint16_t entrypoint = 0;

uint16_t mem_size = 0x1000;

/*
 * c: directly output characters
 * C: output HEX and characters
 * h: output hex
 * d: output decimal
 */
char out_fmt = 'C';

char *binfile = NULL;
char *dbgfile = NULL;

int debug = 0;
char *dbg_src_dir = "./";
char **dbg_files = NULL;
uint64_t dbg_offset = 0;
uint32_t *dbg_addr = NULL;
uint32_t *dbg_line = NULL;
uint16_t *dbg_file = NULL;
uint32_t dbg_n_lines = 0;
uint16_t *dbg_lines_per_file = NULL;
uint16_t dbg_n_files = 0;
uint32_t *dbg_file_n_lines = NULL;
char ***dbg_file_lines = NULL;

int stepping = 0;

void handle_opts(int, char**);
int count_lines(FILE *);
int dbg_get_idx(uint16_t);

char ngetc()
{
    char ch;
    ssize_t ret = read (0, &ch, 1);
    if(ret > 0) return ch;
    else return 0;
}

int main(int argc, char **argv) {
    handle_opts(argc, argv);

    cpu_t *cpu = cpu_create(mem_size, entrypoint);
    if(binfile) {
        FILE *bin = fopen(binfile, "r");
        if(!bin) {
            printf("Could not open binary file %s!", binfile);
            return 1;
        }
        fread(cpu->mem + entrypoint, mem_size - entrypoint, 1, bin);
        fclose(bin);
    } else {
        memcpy(cpu->mem + entrypoint, prog, sizeof(prog));
    }

    FILE *dbg = NULL;
    if(dbgfile) {
        dbg = fopen(dbgfile, "r");
        if(!dbg) {
            printf("Could not open debug file %s!", dbgfile);
            return 1;
        }
        debug = 1;
        uint64_t tmp = 0;;
        fread(&tmp, 2, 1, dbg); // Magic number
        if(tmp != 0xBFDE) {
            printf("Debug file is not in the correct format! Magic: 0x%X\n", (uint32_t)tmp);
            return 1;
        }
        fread(&dbg_offset, 8, 1, dbg); // Program offset
        fread(&dbg_n_files, 2, 1, dbg); // Number of source files
        dbg_files = (char **)malloc(dbg_n_files * sizeof(char *));
        dbg_lines_per_file = (uint16_t *)malloc(dbg_n_files * 2);
        uint32_t i;
        for(i = 0; i < dbg_n_files; i++) {
            tmp = 0;
            fread(&tmp, 2, 1, dbg); // filename length
            dbg_files[i] = (char *)malloc(tmp + 1); // + 1 for \0
            dbg_files[i][tmp] = '\0';
            fread(dbg_files[i], tmp, 1, dbg);
            fread(dbg_lines_per_file + i * 2, 2, 1, dbg);
            dbg_n_lines += dbg_lines_per_file[i];
            //printf("DBG file: %s with %d lines of code\n", dbg_files[i], dbg_lines_per_file[i]);
        }
        dbg_addr = (uint32_t *)malloc(dbg_n_lines * 4);
        dbg_line = (uint32_t *)malloc(dbg_n_lines * 4);
        dbg_file = (uint16_t *)malloc(dbg_n_lines * 2);
        int cpos = 0, cfile = 0;
        for(i = 0; i < dbg_n_lines; i++) {
            //printf("File offset: 0x%lX\n", ftell(dbg));
            fread(&(dbg_addr[i]), 4, 1, dbg);
            fread(&(dbg_line[i]), 4, 1, dbg);
            dbg_file[i] = cfile;
            //printf("addr: %d line: %d\n", dbg_addr[i], dbg_line[i]);
            cpos++;
            if(cpos >= dbg_lines_per_file[cfile]) {
                cpos = 0;
                cfile++;
            }
        }
        //printf("Debug file loaded!\n");
        //printf("  %d files\n", dbg_n_files);
        //printf("  %d lines\n", dbg_n_lines);

        //printf("Loading source files...\n");
        dbg_file_lines = (char ***)malloc(dbg_n_files * sizeof(char ***));
        dbg_file_n_lines = (uint32_t *)malloc(dbg_n_files * sizeof(uint32_t *));
        for(i = 0; i < dbg_n_files; i++) {
            char *file = malloc(strlen(dbg_src_dir) + strlen(dbg_files[i]) + 1);
            sprintf(file, "%s%s", dbg_src_dir, dbg_files[i]);
            //printf("  -> %s\n", file);
            FILE *dfile = fopen(file, "r");
            if(!dfile) {
                //puts("      -> Could not open file!\n");
                printf("Could not load source file %s\n", file);
                return 1;
            }
            dbg_file_n_lines[i] = count_lines(dfile);
            //printf("      -> %d lines\n", dbg_file_n_lines[i]);
            fseek(dfile, 0, SEEK_SET);
            dbg_file_lines[i] = (char **)malloc(dbg_file_n_lines[i] * sizeof(char **));
            uint32_t line;
            char l[1024];
            for(line = 0; line < dbg_file_n_lines[i]; line++) {
                fgets(l, 1024, dfile);
                int len = strlen(l);
                dbg_file_lines[i][line] = (char *)malloc(len + 1);
                dbg_file_lines[i][line][len] = '\0';
                memcpy(dbg_file_lines[i][line], l, len);
                //printf("line: %s", dbg_file_lines[i][line]);
            }
            fclose(dfile);
        }
    }

    WINDOW *w = initscr();
    cbreak();
    nodelay(w, TRUE);
    noecho();

    int r; char ch;
    cpu->mem[0x100] = -1;
    do {
        if((ch = getch()) != ERR) {
            cpu->mem[0x102] = 1;
            cpu->mem[0x103] = ch;
        }
        if(debug) {
            int idx = dbg_get_idx(cpu->ip);
            if(idx < 0) {
                printf("%2X> *\n", cpu->ip);
            } else {
                int file = dbg_file[idx];
                int line = dbg_line[idx];
                char *str = dbg_file_lines[file][line-1];
                printf("%4hX:%d> %s", cpu->ip, line, str);
            }
        }

        // Check output:
        if(cpu->mem[0x100] != -1) {
            switch(out_fmt) {
                case 'C':
                    wprintf(L"0x%hX [%lc]\n\r", cpu->mem[0x101], cpu->mem[0x101]);
                    break;

                case 'c':
                    putwchar(cpu->mem[0x101]);
                    if(cpu->mem[0x101] == 0x0A) printf("\r");
                    fflush(stdout);
                    break;

                case 'h':
                    printf("0x%04hX\n", cpu->mem[0x101]);
                    break;

                case 'd':
                    printf("%hd\n", cpu->mem[0x101]);
                    break;
            }
            cpu->mem[0x100] = -1; cpu->mem[0x101] = 0;
        }

        if(stepping) {
            //if(!debug) {
                printf("%04X> %04X[%04X] %04X[%04X] %04X[%04X]\n", cpu->ip, cpu->mem[cpu->ip], cpu->mem[cpu->mem[cpu->ip]], cpu->mem[cpu->ip + 1], cpu->mem[cpu->mem[cpu->ip + 1]], cpu->mem[cpu->ip + 2], cpu->mem[cpu->mem[cpu->ip + 2]]);
            //}
            getchar();
        }
    } while(!(r = cpu_cycle(cpu)));

    return 0; 
}

int dbg_get_idx(uint16_t address) {
    address -= dbg_offset;
    uint32_t i = 0;
    for(; i < dbg_n_lines; i++) {
        if(dbg_addr[i] == address) {
            return i;
        }
    }
    return -1;
}

int count_lines(FILE *f) {
    int lines = 0;
    int ch = 0;
    while((ch = fgetc(f)) != EOF) {
        if(ch == '\n') lines++;
    }
    return lines;
}

void usage(int ret) {
    puts(
    "USAGE: 16b_subleq [OPTIONS] -b binary\n"
    "  OPTIONS:\n"
    "    -b: Load a binary file to run\n"
    "    -e: Set entrypoint\n"
    "    -f: Set OUT format (c,C,h,d,anything else disables printing)\n"
    "    -d: Load a debug file for debugging\n"
    "    -s: Set source directory for debugging\n"
    "    -t: Step through the code one line at a time\n");
    exit(ret);
}

void handle_opts(int argc, char **argv) {
    if(argc > 1) {
        int i = 1;
        while(i < argc) {
            switch(argv[i][1]) {
                case 'b': // Load binary file
                    if(i == argc - 1) {
                        puts("You must provide an argument for -b!\n");
                        usage(1);
                    }
                    binfile = argv[++i];
                    break;

                case 'e': // Set entrypoint
                    if(i == argc - 1) {
                        puts("You must provide an argument for -e!\n");
                        usage(1);
                    }
                    entrypoint = strtol(argv[++i], NULL, 0);
                    break;

                case 'f': // Set `OUT` format
                    if(i == argc - 1) {
                        puts("You must provide an argument for -f!\n");
                        usage(1);
                    }
                    out_fmt = argv[++i][0];
                    break;

                case 'd': // Debug
                    if(i == argc - 1) {
                        puts("You must provide an argument for -d!\n");
                        usage(1);
                    }
                    dbgfile = argv[++i];
                    break;

                case 's': // Debug source file location
                    if(i == argc - 1) {
                        puts("You must provide an argument for -s!\n");
                        usage(1);
                    }
                    dbg_src_dir = argv[++i];
                    break;

                case 't': // Step through the code one line at a time
                    stepping = 1;
                    break;

                default:
                    printf("UNKNOWN ARGUMENT: %s\n", argv[i]);
                    usage(1);
            }
            i++;
        }
    }
}
