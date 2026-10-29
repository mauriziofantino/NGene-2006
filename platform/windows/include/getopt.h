#ifndef NGENE_WINDOWS_GETOPT_H
#define NGENE_WINDOWS_GETOPT_H
struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};
enum { no_argument, required_argument, optional_argument };
extern char *optarg;
extern int optind, opterr, optopt;
int getopt_long(int argc, char *const argv[], const char *options, const struct option *longOptions,
                int *longIndex);
#endif
