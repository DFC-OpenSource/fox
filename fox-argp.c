#include <argp.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fox.h"

const char *argp_program_version = "fox v1.0";
const char *argp_program_bug_address = "Ivan L. Picoli <ivpi@itu.dk>";

enum cmdtypes {
    CMDARG_RUN = 1
};

#define FOX_RUN_MODE         0x0

static char doc_global[] = "\n*** FOX v1.0 ***\n"
        " \n A tool for testing Open-Channel SSDs\n\n"
        " Available commands:\n"
        "  run              Run FOX based on command line parameters.\n"
        "\n Examples:"
        "\n  fox run <parameters>     - custom configuration"
        "\n  fox --help               - show available parameters"
        "\n  fox <without parameters> - run with default configuration\n"
        " \n Initial release developed by Ivan L. Picoli, <ivpi@itu.dk>\n\n";

static char doc_run[] =
        "\nUse this command to run FOX based on parameters by the"
        " command line.\n"
        "\n Example:"
        "\n     fox run <parameters>\n"
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
    {"vector", 'v',"<int>",0, "Number of physical sectors per I/O. This value"
    " must be multiple of <sectors per page * number of planes>. e.g: if"
    " device has 4 sectors per page and 2 planes, this value must be multiple"
    " of 8. The maximum value is the device maximum sectors per I/O."},
    {"sleep", 's', "<int>", 0, "Maximum delay between I/Os. Jobs sleep between "
    "I/Os in a maximum of <sleep> u-seconds."},
    {"memcmp", 'm', NULL, OPTION_ARG_OPTIONAL, "If present, it enables buffer "
    "comparison between write and read buffers. Not all cases are suitable "
    "for memory comparison. Cases not supported: 100% reads, Engine 3 "
    "(isolation)."},
    {"output", 'o', NULL, OPTION_ARG_OPTIONAL, "If present, a set of output "
    "files will be generated. (1)metadata, (2)per I/O information, "
    "(3)real time average information"},
    {"engine", 'e', "<int>", 0, "I/O engine ID. (1)sequential, (2)round-robin,"
    " (3)isolation. Please check documentation for detailed information."},
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
            args->memcmp = 1;
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

static struct argp argp_run = {opt_run, parse_opt_run, 0, doc_run};

error_t parse_opt (int key, char *arg, struct argp_state *state)
{
    struct fox_argp *args = state->input;

    switch(key)
    {
        case ARGP_KEY_ARG:
            if (strcmp(arg, "run") == 0) {
                args->cmdtype = CMDARG_RUN;
                cmd_prepare(state, args, "run", &argp_run);
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
        default:
            printf("Invalid command, please use --help to see more info.\n");
    }

    return -1;
}
