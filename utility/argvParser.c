/**
 * @file argvParser.c
 *
 * @author Mario Nicola
 *
 * @date 06/08/2014
 *
 * @version 1.0.0
 *
 * @brief Functions used to parse the command line arguments.
 *        From a previous implementation by Maurizio Fantino.
 */
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include "argvParser.h"
#include "ngeneConfiguration.h"
#include "dspWorkerPool.h"
#include "ioFunctions.h"
#include "closeManager.h"
extern char *optarg; ///< required by the getopt_long linux function used in handle_cmdline
// It simply gets the executable file name and it stores the value
// in the corresponding place in the receiver data structure

void GetMainName(char *argv[], TNGeneConfiguration *cfg)
{
    /* Get the name of the main file as it has been given from the command line
     * Deletes folder(s) and store just the name of the executable file. */
    cfg->progname = (const char *)strrchr(argv[0], '/');
    cfg->progname = cfg->progname ? (cfg->progname + 1) : argv[0];
}
// This function handle the parameters given as command line to the executable file
// and it sets the corresponding variable and receiver data structure

int HandleCmdLine(int argc, char *argv[], TNGeneConfiguration *cfg)
{
    /* Add here more options if necessary */
    struct option opts[] = {
        {"version", 0, 0, 'V'}, {"textual", 0, 0, 't'},  {"ncurses", 0, 0, 'n'},
        {"input", 1, 0, 'i'},   {"frontend", 1, 0, 'f'}, {"googleEarth", 1, 0, 'g'},
        {"logfile", 1, 0, 'l'}, {"verbose", 0, 0, 'v'},  {"credits", 0, 0, 'c'},
        {"workers", 1, 0, 'W'}, {"help", 0, 0, 'h'},    {NULL, 0, 0, 0}};
    // and its relative help output
    const char *optsHelp[] = {
        "Print the actual version of NGene receiver.",
        "Use the textual interface.",
        "Use the ncurses terminal interface (default).",
        "Process the specified recording instead of USB (configuration unchanged).",
        "Select sige or hackrf for this run (configuration unchanged).",
        "Record the receiver output as a GoogleEarth file.",
        "Log all the operation a logging file.",
        "Verbose output.",
        "Print the credits.",
        "Elaborate the channels on this number of DSP workers, from 1 to 4.",
        "Print this message."};
    struct option *opt;
    const char **hlp;
    int max, size;
    int i;
    long int requestedWorkers; /**< workers parsed from the -W option (see dspWorkerPool.h) */
    char *conversionEnd;       /**< first character not converted by strtol() */
    /* Setting default values for the flags */
    cfg->userInterface = RX_UI_NCURSES;
    cfg->stdlog = stdout;
    pthread_mutex_init(&(cfg->stdLogMutex), NULL);
    cfg->logFilename[0] = '\0';
    cfg->googleEarthFilename[0] = '\0';
    cfg->googleEarthFilenameValid = FALSE_FLAG;
    cfg->verbose = FALSE_FLAG;
    cfg->configurationValid = FALSE_FLAG;
    cfg->inputFileOverride = NULL;
    cfg->frontEndOverride = 0;
    /* one worker elaborates the channels on the main thread, as the historical execution */
    cfg->numberOfDspWorkers = DSP_WORKERS_MINIMUM;
    /* Parse options */
    while (1) {
        i = getopt_long(argc, argv, "Vtni:f:g:l:vcrW:h", opts, NULL);
        if (i == -1) {
            break;
        }
        switch (i) {
        case 'V':
            cfg->userInterface = RX_UI_TEXT;
            fprintf(stdout, "\n\nNGene Classic %s \"%s\"\n", NGENE_VERSION_INDEX,
                    NGENE_VERSION_NAME);
            fprintf(
                stdout,
                "Copyright (c) 2007-2025 Navigation Signal Analysis and Simulation group - NavSAS\n\n");
            exit(EXIT_SUCCESS);
            break;
        case 't':
            cfg->userInterface = RX_UI_TEXT;
            break;
        case 'n':
            cfg->userInterface = RX_UI_NCURSES;
            break;
        case 'i':
            cfg->inputFileOverride = optarg;
            break;
        case 'f':
            if (strcasecmp(optarg, "sige") == 0)
                cfg->frontEndOverride = NGENE_FE_SIGE;
            else if (strcasecmp(optarg, "hackrf") == 0)
                cfg->frontEndOverride = NGENE_FE_HACKRF;
            else {
                fprintf(stderr, "Unknown frontend: %s (use sige or hackrf)\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'g':
            // Get the googleEarth file name
            cfg->googleEarthFilename[0] = '\0';
            strcat(cfg->googleEarthFilename, optarg);
            cfg->googleEarthFilenameValid = TRUE_FLAG;
            break;
        case 'l':
            // Get the log file name and path
            cfg->logFilename[0] = '\0';
            strcat(cfg->logFilename, optarg);
            break;
        case 'W':
            // Number of DSP workers elaborating the channels of one execution cycle. The pool is
            // bounded: each worker owns one DSP scratch (see receiver/dspWorkerPool.h)
            if (optarg == NULL) {
                fprintf(stderr, "Missing number of DSP workers (use %d to %d)\n", DSP_WORKERS_MINIMUM,
                        DSP_WORKERS_MAXIMUM);
                exit(EXIT_FAILURE);
            }
            requestedWorkers = strtol(optarg, &(conversionEnd), 10);
            if ((conversionEnd == optarg) || ((*conversionEnd) != '\0') ||
                (requestedWorkers < (long int)DSP_WORKERS_MINIMUM) ||
                (requestedWorkers > (long int)DSP_WORKERS_MAXIMUM)) {
                fprintf(stderr, "Invalid number of DSP workers: %s (use %d to %d)\n", optarg,
                        DSP_WORKERS_MINIMUM, DSP_WORKERS_MAXIMUM);
                exit(EXIT_FAILURE);
            }
            cfg->numberOfDspWorkers = (int)requestedWorkers;
            break;
        case 'v':
            cfg->verbose = TRUE_FLAG;
            break;
        case 'c':
            cfg->userInterface = RX_UI_TEXT;
            fprintf(stdout, "\n                   NGene Classic GPS/Galileo receiver\n\n");
            fprintf(
                stdout,
                "Copyright (c) 2007-2025 Navigation Signal Analysis and Simulation group - NavSAS\n\n");
            fprintf(stdout, "                                ;.\n");
            fprintf(stdout, "                       :cl,    ,x,\n");
            fprintf(stdout, "                      .kkd'    ox'\n");
            fprintf(stdout, "                      ;kkk:    do.\n");
            fprintf(stdout, "                      dkcxk'  .ko.\n");
            fprintf(stdout, "                     .ko'.kd. 'kc.\n");
            fprintf(stdout, "                     :k:. ,kl cx,\n");
            fprintf(stdout, "                .,;:cxd:;,.;kdkl. .dddddl.       ',::cc::;'.\n");
            fprintf(stdout, "               ,ccc:cd:cccc;;ol, .xkkkkkko.    .cccc:;,;cccc.\n");
            fprintf(stdout, "               ;cccc;:,';,''.    okkkcokkkc    .cccc:,..,,,''\n");
            fprintf(stdout, "                ':ccccccc:,.    :kkko;.dkkk;    .;cccccccc:'\n");
            fprintf(stdout, "                 ...';;:cccc'  ,kkkkl:,lkkkx'    .,:,,;:cccc:.\n");
            fprintf(stdout, "              .cclc,   'cccc;..kkkkkkkkkkkkkd. ,cdkx,  .;cllc'\n");
            fprintf(stdout, "               .:cccc::ccccc'.dkkkdc;;;;;xkkkl  ,dkxc::clxxc:.\n");
            fprintf(stdout, "                 .,;;:::;,'. .:ccc;.     .:ccc'  :kd:::;lkl'\n");
            fprintf(stdout, "                                                 'kd'  .xo,\n");
            fprintf(stdout, "                                                  xx,  od;\n");
            fprintf(stdout, "                                                  :kc.dd:.\n");
            fprintf(stdout, "                                                   oxxd;\n");
            fprintf(stdout, "                                                   :kd:.\n");
            fprintf(stdout, "                                                   .'.\n");
            exit(EXIT_SUCCESS);
            break;
        case 'h':
            cfg->userInterface = RX_UI_TEXT;
            fprintf(stdout, "\n         NGene Classic %s \"%s\"\n\n", NGENE_VERSION_INDEX,
                    NGENE_VERSION_NAME);
            fprintf(
                stdout,
                "Copyright (c) 2007-2025 Navigation Signal Analysis and Simulation group - NavSAS\n\n");
            fprintf(stdout, "Usage: %s [OPTIONS]\n\n", cfg->progname);
            max = 0;
            for (opt = opts; opt->name; opt++) {
                size = strlen(opt->name);
                if (size > max)
                    max = size;
            }
            for (opt = opts, hlp = optsHelp; opt->name; opt++, hlp++) {
                fprintf(stdout, "  -%c, --%s", opt->val, opt->name);
                size = strlen(opt->name);
                for (; size < max; size++)
                    fprintf(stdout, " ");
                fprintf(stdout, "  %s\n", *hlp);
            }
            fprintf(stdout, "\n");
            exit(EXIT_SUCCESS);
            break;
        default:
            cfg->userInterface = RX_UI_TEXT;
            fprintf(stderr, "Use -h for help\n");
            exit(EXIT_FAILURE);
            break;
        }
    }
    return SUCCESS_RETURN;
}
