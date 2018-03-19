#define _GNU_SOURCE
#include <libaio.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <getopt.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>


static __inline__ unsigned long long rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

struct global_args_t {
        const char* testfile;
        const char* histfile;
        size_t size;
        size_t bs;
        size_t total;
        unsigned times;
        size_t vec_num;
        size_t iocbs_num;
        int verbose;
        int rewrite;
        char filler;
 } global_args;

size_t str2size(const char* str) {
        size_t num;
        char let = 0;
        int ret;
        sscanf(str, "%lu%c", &num, &let);
        switch (let) {
                case 'g':
                case 'G':
                        num *= 1024;
                case 'm':
                case 'M':
                        num *= 1024;
                case 'k':
                case 'K':
                        num *= 1024;
                default:
                        break;
        }

        return num;
}

static const char *optString = "b:t:n:i:V:H:T:F:rvh";

static const struct option longOpts[] = {
    { "blocksize", required_argument, NULL, 'b' },
    { "totalsize", required_argument, NULL, 't' },
    { "number", required_argument, NULL, 'n' },
    { "iocbsnum", required_argument, NULL, 'i' },
    { "vectornum", required_argument, NULL, 'V' },
    { "output", required_argument, NULL, 'o' },
    { "histout", required_argument, NULL, 'H' },
    { "testfile", required_argument, NULL, 'T' },
    { "fill", required_argument, NULL, 'F' },
    { "rewrite", no_argument, NULL, 'r' },
    { "verbose", no_argument, NULL, 'v' },
    { "help", no_argument, NULL, 'h' },
    { NULL, no_argument, NULL, 0 }
};

#define HIST_START 0
#define HIST_END 5000000
#define HIST_STEP 100000
#define HIST_SIZE ((HIST_END - HIST_START)/HIST_STEP)

void build_hist(uint64_t* results, FILE* hist, int len) {
        int i;
        uint64_t* hist_results = (uint64_t*) calloc (HIST_SIZE, sizeof(uint64_t));
        for (i = 0; i < len; i++) {
                if (results[i]/HIST_STEP < HIST_SIZE) {
                        hist_results[results[i]/HIST_STEP]++;
                } else {
                        hist_results[HIST_SIZE - 1]++;
                }
        }

        for (i = 0; i < HIST_SIZE; i++) {
                fprintf(hist, "%ld, %ld\n", i * HIST_STEP, hist_results[i]);
        }
}

#define MAXLEN 32

const char* help_msg = "";

static void display_usage() {
        puts(help_msg);
}

static void update_iocbs(struct iocb** iocbsp) {
        int i;
        for (i = 0; i < global_args.iocbs_num; i++) {
                iocbsp[i]->u.v.offset += global_args.total;
        }
}

int main(int argc, char* argv[]) {
        int i, ret = 0;
        struct iocb** iocbsp;
        int fd;
        FILE* hist;
        io_context_t ctx;
        struct io_event e;
        uint64_t* results;
        int opt, longind;

        global_args.testfile = "test";
        global_args.histfile = NULL;
        global_args.size = 0;
        global_args.bs = 0;
        global_args.total = 0;
        global_args.times = 0;
        global_args.vec_num = 0;
        global_args.iocbs_num = 0;
        global_args.verbose = 0;
        global_args.rewrite = 0;
       
        opt = getopt_long(argc, argv, optString, longOpts, &longind);
        while (opt != -1) {
                switch (opt) {
                        case 'h':
                                display_usage();
                                break;
                        case 'v':
                                global_args.verbose = 1;
                                break;
                        case 'r':
                                global_args.rewrite = 1;
                                break;
                        case 'b':
                                global_args.bs = str2size(optarg);
                                break;
                        case 't':
                                global_args.total = str2size(optarg);
                                break;
                        case 'n':
                                global_args.times = str2size(optarg);
                                break;
                        case 'i':
                                global_args.iocbs_num = str2size(optarg);
                                break;
                        case 'V':
                                global_args.vec_num = str2size(optarg);
                                break;
                        case 'T':
                                global_args.testfile = optarg;
                                break;
                        case 'F':
                                global_args.filler = optarg[0];
                                break;
                        case 'H':
                                global_args.histfile = optarg;
                                break;
                        default:
                                break;
                }
                opt = getopt_long(argc, argv, optString, longOpts, &longind);
        }

        if (global_args.total == 0) {
                fprintf(stderr, "Incorrect args: total == 0\n");
                exit(EXIT_FAILURE);
        }

        if (global_args.bs * global_args.iocbs_num * global_args.vec_num != global_args.total) {
                fprintf(stderr, "Incorrect args: bs * iocbsnum * vectornum != total\n");
                exit(EXIT_FAILURE);
        }

        fd = open(global_args.testfile, O_DIRECT | O_ASYNC | O_RDWR | O_CREAT, S_IRWXU);
        if (fd == -EEXIST) {
                fd = open(global_args.testfile, O_DIRECT | O_RDWR | O_DSYNC );
        } else {
                fallocate(fd, 0, 0, global_args.total * global_args.times);
        }

        iocbsp = (struct iocb**) malloc (global_args.iocbs_num * sizeof(struct iocb*));

        for (i = 0; i < global_args.iocbs_num; i++) {
                struct iovec* vecs;
                int j;
                iocbsp[i] = (struct iocb*) calloc (sizeof(struct iocb), 1);
                vecs = (struct iovec*) malloc (global_args.vec_num * sizeof(struct iovec));
                for (j = 0; j < global_args.vec_num; j++) {
                        posix_memalign(&(vecs[j].iov_base), 4096, global_args.bs);
                        vecs[j].iov_len = global_args.bs;
                }
                io_prep_pwritev(iocbsp[i], fd, vecs, global_args.vec_num, i * (global_args.total / global_args.iocbs_num));
        }

        memset(&ctx, 0, sizeof(ctx));
        io_setup(256, &ctx);

        results = (uint64_t*) calloc (global_args.times, sizeof(uint64_t));
        if (global_args.histfile != NULL) {
                hist = fopen(global_args.histfile, "w");
        }

        for (i = 0; i < global_args.times; i++) {

                int count = global_args.iocbs_num;
                int ret;
                unsigned long start;
                unsigned long end;

                if (global_args.verbose == 1) {
                        fprintf(stderr, "Submit starts\n", end - start);
                }

                start = rdtsc();
                ret = io_submit(ctx, global_args.iocbs_num, iocbsp);
                end = rdtsc();

                if (ret < 0) {
                        fprintf(stderr, "submit error!\n");
                        break;
                }

                results[i] = end - start;
                if (global_args.verbose == 1) {
                        fprintf(stderr, "Submit time = %lu\n", end - start);
                }

                while(count--) {
                        io_getevents(ctx, 1, 1, &e, NULL);
                }

                if (global_args.verbose == 1) {
                        fprintf(stderr, "Submit reaped\n", end - start);
                }

                if (global_args.rewrite == 0) {
                        update_iocbs(iocbsp);
                }
        }

        if (hist != 0) {
                build_hist(results, hist, global_args.times);
        }
        
        io_destroy(ctx);
}
