#include "pub_tool_basics.h"
#include "pub_tool_clientstate.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_machine.h"  // VG_(fnptr_to_fnentry)
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_oset.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_xarray.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cg_arch.h"

static const char* argv0 = "cg_mem_log";
static int mem_log_fd = -1;
static LogBuffer buffer1;
static int mem_log_debug = 0;

typedef struct MemStats {
    Int total_size;
    Int total_count;
    Int total_accesses;
    Int total_hits;
    Int total_misses;
} MemStats;

enum MemType {
    DATA_READ,
    DATA_WRITE,
    INSTRUCTION_READ,
    INSTRUCTION_WRITE,
    STACK_READ,
    STACK_WRITE,
    HEAP_READ,
    HEAP_WRITE,
    OTHER,
};

static MemStats mem_stats[OTHER + 1];
static void Debug(const char* fmt, ...)
{
    if (!mem_log_debug)
        return;

    char str[256] = "Debug: ";
    va_list args;
    va_start(args, fmt);
    vsnprintf(str + 7, sizeof(str) - 7, fmt, args);
    va_end(args);
    fprintf(stderr, "%s\n", str);
}

static void open_mem_log_file(const HChar* filename)
{
    // Initialize buffers
    memset(&buffer1, 0, sizeof(LogBuffer));

    // Copy filename

    Debug("Opening log file: %s", filename);
    // Open log file
    int o = open(filename, O_RDONLY);
    if (o < 0) {
        fprintf(stderr, "cannot open mem file '%s': %m\n", filename);
        exit(1);
    } else {
        mem_log_fd = o;
    }
}

static void dump_buffer(LogEntry* buffer, Int n)
{
    Debug("Dumping buffer\n");
    for (Int i = 0; i < n; i++, buffer++) {
        printf("%llu %p %d %c %c\n", buffer->timestamp, (void*)buffer->addr, (int)buffer->size,
               access_type_char(buffer->type), cache_hit_char(buffer->hit_type));
    }
}

static int read_buffer(void)
{
    LogEntry buffer[BUFFER_SIZE];
    Debug("Reading buffer");
    Int n = read(mem_log_fd, (void*)buffer, sizeof(buffer));
    if (n <= 0) {
        return -1;
    }
    Debug("Read %d entries", n);
    dump_buffer(buffer, n / sizeof(LogEntry));
    return n;
}

static void read_all_buffers(void)
{
    while (read_buffer() > 0) {
        ;
    }
    close(mem_log_fd);
    mem_log_fd = -1;
}

static void usage(void)
{
    fprintf(stderr,
            "Usage: %s [options] <filename1> <filename2> ...\n"
            "    --help|-h               print this help message\n",
            argv0);
    exit(1);
}

int main(int argc, char** argv)
{
    Int i;

    if (argv[0])
        argv0 = argv[0];

    for (i = 1; i < argc; i++) {
        if (argv[i] == NULL) {
            break;
        }

        if (argv[i][0] != '-') {
            break;
        }

        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            mem_log_debug = True;
            continue;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            continue;
        }
    }

    argc -= i;
    argv += i;

    if (argc < 1)
        usage();

    /* Scan args, all arguments are filenames */
    for (i = 0; i < argc; i++) {
        open_mem_log_file(argv[i]);
        read_all_buffers();
    }
}