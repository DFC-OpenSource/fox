#include <argp.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fox.h"

const char *argp_program_version = "fox v1.3";
const char *argp_program_bug_address = "Ivan L. Picoli <ivpi@itu.dk>";

static char doc_global[] = "\n*** FOX v1.3 ***\n"
        " \n A tool for testing Open-Channel SSDs\n\n"
        " Available commands:\n"
        "  run              Run FOX based on command line parameters.\n"
        "  erase            Erases a specific range of physical blocks.\n"
        "  write            Writes to a specific range of physical pages.\n"
        "  read             Reads from a specific range of physical pages.\n"
        "\n Examples:"
        "\n  fox run <parameters>     - custom configuration"
        "\n  fox run --help           - show available parameters"
        "\n  fox <without parameters> - run with default configuration\n"
        " \n Initial release developed by Ivan L. Picoli, <ivpi@itu.dk>\n\n";

static char doc_erase[] =
        "\nUse this command for erasing a range of physical blocks.\n"
        "\n Example:"
        "\n     - Erases block 10 in LUN 0, Channel 0:"
        "\n        fox erase -c 0 -l 0 -b 10\n"
        "\n     - Erases 5 blocks sequentially starting in block 10 in LUN 0, "
        "Channel 0:"
        "\n        fox erase -c 0 -l 0 -b 10 -s 5";

static char doc_write[] =
        "\nUse this command for programming a range of physical pages. "
        "Be sure the block is erased before programming.\n"
        "\n Example:"
        "\n     - Writes page 5 in block 10, LUN 0, Channel 0:"
        "\n        fox write -c 0 -l 0 -b 10 -p 5\n"
        "\n     - Writes 20 pages sequentially starting in page 5, block 10, "
        "LUN 0, Channel 0:"
        "\n        fox write -c 0 -l 0 -b 10 -p 5 -s 20";

static char doc_read[] =
        "\nUse this command for readind a range of physical pages. "
        "Be sure the page is programmed before reading.\n"
        "\n Example:"
        "\n     - Reads page 5 in block 10, LUN 0, Channel 0:"
        "\n        fox read -c 0 -l 0 -b 10 -p 5\n"
        "\n     - Reads 20 pages sequentially starting in page 5, block 10, "
        "LUN 0, Channel 0:"
        "\n        fox read -c 0 -l 0 -b 10 -p 5 -s 20\n"
        "\n Hints: Use -v for printing the data in the screen."
        "\n        Use -o for creating an binary output file.";

static char doc_run[] =
        "\nUse this command to run FOX based on parameters by the"
        " command line.\n"
        "\n Example:"
        "\n     fox run <parameters>\n"
        "\n     fox run -c 8 -l 4 -b 2 -p 128 -j 8 -m 2 -r 75 -v 8 -e 2 -o\n"
        "\nIf parameters are not provided, the default values will be used:"
        "\n     device   = /dev/nvme0n1"
        "\n     runtime  = 0 (only 1 iteration will be performed)"
        "\n     channels = 1"
        "\n     luns     = 1"
        "\n     blocks   = 1"
        "\n     pages    = 1"
        "\n     jobs     = 1"
        "\n     read     = 100"
        "\n     write    = 0"
        "\n     vector   = 1 page = <sectors per page * number of planes>"
        "\n     sleep    = 0"
        "\n     memcmp   = disabled"
        "\n     output   = disabled"
        "\n     engine   = 1 (sequential)";

static struct argp_option opt_run[] = {
    {"device", 'd', "<char>", 0,"Device name. e.g: /dev/nvme0n1"},
    {"runtime", 't', "<int>", 0, "Runtime in seconds. If 0 or not present, "
    "the workload will finish when all pages are done in a given geometry."},
    {"channels", 'c', "<int>", 0, "Number of channels."},
    {"luns", 'l', "<int>", 0, "Number of LUNs per channel."},
    {"blocks", 'b', "<int>", 0, "Number of blocks per LUN."},
    {"pages", 'p', "<int>", 0, "Number of pages per block."},
    {"jobs", 'j', "<int>", 0, "Number of jobs. Jobs are executed in parallel "
    "and the geometry of the device is split among threaded jobs."},
    {"read", 'r', "<0-100>", 0, "Percentage of read. Read+write must sum 100."},
    {"write", 'w',"<0-100>",0, "Percentage of write. Read+write must sum 100."},
    {"write-seq", 'n',"<int>",0, "Number of pages to be written in sequence "
    "before a read command. The number of reads in sequence will be "
    "calculated based on the percentage of reads (-r) and write-seq (-n). "
    "e.g.(1) -w 75 -r 25 -n 150 = 150 writes, 50 reads, 150 w, 50 r... e.g.(2) "
    "-w 25 -r 75 -n 1 = 1 write, 3 reads, 1 w, 3 r..."},
    {"vector", 'v',"<int>",0, "Number of physical sectors per I/O. This value"
    " must be multiple of <sectors per page * number of planes>. e.g: if"
    " device has 4 sectors per page and 2 planes, this value must be multiple"
    " of 8. The maximum value is the device maximum sectors per I/O."},
    {"sleep", 's', "<int>", 0, "Maximum delay between I/Os. Jobs sleep between "
    "I/Os in a maximum of <sleep> u-seconds."},
    {"memcmp", 'm', "<int>", 0, "If included, this argument it enables buffer "
    "comparison between write and read buffers. Data types available: "
    "(1)random data, (2)human readable, (3)geometry based. These cases only "
    "support geometry based data: 100% reads, Engine 3 (isolation)."},
    {"output", 'o', NULL, OPTION_ARG_OPTIONAL, "If present, a set of output "
    "files will be generated. (1)metadata, (2)per I/O information, "
    "(3)real time average information"},
    {"engine", 'e', "<int>", 0, "I/O engine ID. (1)sequential, (2)round-robin,"
    " (3)isolation. Please check documentation for detailed information."},
    {0}
};

static struct argp_option opt_erase[] = {
    {"device", 'd', "<char>", 0,"Device name. e.g: /dev/nvme0n1"},
    {"channel", 'c', "<int>", 0, "Target channel."},
    {"lun", 'l', "<int>", 0, "Target LUN within the channel."},
    {"block", 'b', "<int>", 0, "Target block within the LUN."},
    {"sequence", 's', "<int>", 0, "Number of sequential blocks to be erased."},
    {0}
};

static struct argp_option opt_write[] = {
    {"device", 'd', "<char>", 0,"Device name. e.g: /dev/nvme0n1"},
    {"channel", 'c', "<int>", 0, "Target channel."},
    {"lun", 'l', "<int>", 0, "Target LUN within the channel."},
    {"block", 'b', "<int>", 0, "Target block within the LUN."},
    {"page", 'p', "<int>", 0, "Target page within the block."},
    {"sequence", 's',"<int>",0, "Number of sequential pages to be programmed."},
    {"random", 'r',"<int>",OPTION_ARG_OPTIONAL,"Generates random buffer data."},
    {"verbose", 'v', "<int>", OPTION_ARG_OPTIONAL, "Print status message."},
    {"output", 'o', "<int>", OPTION_ARG_OPTIONAL, "Creates a binary output file"
    " containing the write content. The file is created under ./output."},
    {0}
};

static struct argp_option opt_read[] = {
    {"device", 'd', "<char>", 0,"Device name. e.g: /dev/nvme0n1"},
    {"channel", 'c', "<int>", 0, "Target channel."},
    {"lun", 'l', "<int>", 0, "Target LUN within the channel."},
    {"block", 'b', "<int>", 0, "Target block within the LUN."},
    {"page", 'p', "<int>", 0, "Target page within the block."},
    {"sequence", 's',"<int>",0, "Number of sequential pages to be read."},
    {"verbose", 'v', "<int>", OPTION_ARG_OPTIONAL, "Prints a minimum page "
    "content to the screen."},
    {"output", 'o', "<int>", OPTION_ARG_OPTIONAL, "Creates a binary output file"
    " containing the read content. The file is created under ./output."},
    {0}
};

static error_t parse_opt_run (int key, char *arg, struct argp_state *state)
{
    struct fox_argp *args = state->input;

    switch (key) {
        case 'd':
            if (!arg || strlen(arg) == 0 || strlen(arg) > CMDARG_LEN)
                argp_usage(state);
            strcpy(args->devname,arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_D;
            break;
        case 't':
            if (!arg)
                argp_usage(state);
            args->runtime = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_T;
            break;
        case 'c':
            if (!arg)
                argp_usage(state);
            args->channels = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_C;
            break;
        case 'l':
            if (!arg)
                argp_usage(state);
            args->luns = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_L;
            break;
        case 'b':
            if (!arg)
                argp_usage(state);
            args->blks = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_B;
            break;
        case 'p':
            if (!arg)
                argp_usage(state);
            args->pgs = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_P;
            break;
        case 'j':
            if (!arg)
                argp_usage(state);
            args->nthreads = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_J;
            break;
        case 'r':
            if (!arg)
                argp_usage(state);
            args->r_factor = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_R;
            break;
        case 'w':
            if (!arg)
                argp_usage(state);
            args->w_factor = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_W;
            break;
        case 'n':
            if (!arg)
                argp_usage(state);
            args->w_sequence = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_N;
            break;
        case 'v':
            if (!arg)
                argp_usage(state);
            args->vector = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_V;
            break;
        case 's':
            if (!arg)
                argp_usage(state);
            args->max_delay = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_S;
            break;
        case 'm':
            args->memcmp = (!arg) ? WB_RANDOM : atoi (arg);
            if (args->memcmp < 0 || args->memcmp > 3)
                argp_usage(state);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_M;
            break;
        case 'o':
            args->output = 1;
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_O;
            break;
        case 'e':
            if (!arg)
                argp_usage(state);
            args->engine = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_E;
            break;
        case ARGP_KEY_END:
        case ARGP_KEY_ARG:
        case ARGP_KEY_NO_ARGS:
        case ARGP_KEY_ERROR:
        case ARGP_KEY_SUCCESS:
        case ARGP_KEY_FINI:
        case ARGP_KEY_INIT:
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static error_t parse_opt_io (int key, char *arg, struct argp_state *state)
{
    struct fox_argp *args = state->input;

    switch (key) {
        case 'd':
            if (!arg || strlen(arg) == 0 || strlen(arg) > CMDARG_LEN)
                argp_usage(state);
            strcpy(args->devname,arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_D;
            break;
        case 'c':
            if (!arg)
                argp_usage(state);
            args->io_ch = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_C;
            break;
        case 'l':
            if (!arg)
                argp_usage(state);
            args->io_lun = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_L;
            break;
        case 'b':
            if (!arg)
                argp_usage(state);
            args->io_blk = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_B;
            break;
        case 'p':
            if (!arg)
                argp_usage(state);
            args->io_pg = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_P;
            break;
        case 's':
            if (!arg)
                argp_usage(state);
            args->io_seq = atoi (arg);
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_S;
            break;
        case 'r':
            args->io_random = 1;
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_R;
            break;
        case 'v':
            args->io_verb = 1;
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_V;
            break;
        case 'o':
            args->io_out = 1;
            args->arg_num++;
            args->arg_flag |= CMDARG_FLAG_O;
            break;
        case ARGP_KEY_END:
        case ARGP_KEY_ARG:
        case ARGP_KEY_NO_ARGS:
        case ARGP_KEY_ERROR:
        case ARGP_KEY_SUCCESS:
        case ARGP_KEY_FINI:
        case ARGP_KEY_INIT:
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static void cmd_prepare(struct argp_state *state, struct fox_argp *args,
                                              char *cmd, struct argp *argp_cmd)
{
    /* Remove the first arg from the parser */
    int argc = state->argc - state->next + 1;
    char** argv = &state->argv[state->next - 1];
    char* argv0 = argv[0];

    argv[0] = malloc(strlen(state->name) + strlen(cmd) + 2);
    if(!argv[0])
        argp_failure(state, 1, ENOMEM, 0);

    sprintf(argv[0], "%s %s", state->name, cmd);

    argp_parse(argp_cmd, argc, argv, ARGP_IN_ORDER, &argc, args);

    free(argv[0]);
    argv[0] = argv0;
    state->next += argc - 1;
}

static struct argp argp_run     = {opt_run, parse_opt_run, 0, doc_run};
static struct argp argp_erase   = {opt_erase, parse_opt_io, 0, doc_erase};
static struct argp argp_write   = {opt_write, parse_opt_io, 0, doc_write};
static struct argp argp_read    = {opt_read, parse_opt_io, 0, doc_read};

error_t parse_opt (int key, char *arg, struct argp_state *state)
{
    struct fox_argp *args = state->input;

    switch(key)
    {
        case ARGP_KEY_ARG:
            if (strcmp(arg, "run") == 0) {

                args->cmdtype = CMDARG_RUN;
                cmd_prepare(state, args, "run", &argp_run);

            } else if (strcmp(arg, "erase") == 0) {

                args->cmdtype = CMDARG_ERASE;
                cmd_prepare(state, args, "erase", &argp_erase);

            } else if (strcmp(arg, "write") == 0) {

                args->cmdtype = CMDARG_WRITE;
                cmd_prepare(state, args, "write", &argp_write);

            } else if (strcmp(arg, "read") == 0) {

                args->cmdtype = CMDARG_READ;
                cmd_prepare(state, args, "read", &argp_read);

            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp_global={NULL, parse_opt,"fox [<cmd> [cmd-options]]",
                                                                   doc_global};

int fox_argp_init (int argc, char **argv, struct fox_argp *args_global)
{
    if (argc == 1)
        return FOX_RUN_MODE;

    argp_parse(&argp_global, argc, argv, ARGP_IN_ORDER, NULL, args_global);

    switch (args_global->cmdtype)
    {
        case CMDARG_RUN:
            return FOX_RUN_MODE;
        case CMDARG_ERASE:
        case CMDARG_WRITE:
        case CMDARG_READ:
            return FOX_IO_MODE;
        default:
            printf("Invalid command, please use --help to see more info.\n");
    }

    return -1;
}