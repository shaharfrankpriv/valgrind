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

#include <stdio.h>
#include "cg_arch.h"

static const char* argv0 = "cg_mem_log";
static VgFile* mem_log_file = NULL;
static LogBuffer buffer1;

void log_mem_access(Addr addr, UChar size, AccessType type, CacheHitType hit_type)
{
    VG_(fprintf)(mem_log_file, "%p %llu %d %llu\n", (void*)addr, (ULong)size, (Int)type, (ULong)hit_type);
}

static void open_mem_log_file(const HChar* filename)
{
    // Initialize buffers
    VG_(memset)(&buffer1, 0, sizeof(LogBuffer));

    HChar* cachegrind_mem_file = VG_(expand_file_name)("--cachegrind-mem-file", filename);

    // Copy filename

    VG_(printf)("Opening log file: %s\n", cachegrind_mem_file);
    // Open log file
    mem_log_file = VG_(fopen)(cachegrind_mem_file, VKI_O_RDONLY, VKI_S_IRUSR | VKI_S_IWUSR);
    if (mem_log_file == NULL) {
        VG_(umsg)("Failed to open log file: %s\n", cachegrind_mem_file);
        VG_(exit)(1);
    }
}

static void dump_buffer(LogEntry* buffer, Int n)
{
    VG_(printf)("Dumping buffer\n");
    for (Int i = 0; i < n; i++, buffer++) {
        VG_(printf)("%llu %p %llu %d %llu\n", buffer->timestamp, (void*)buffer->addr, (ULong)buffer->size,
                    (Int)buffer->type, (ULong)buffer->hit_type);
    }
}

static int read_buffer()
{
    char buffer[BUFFER_SIZE * sizeof(LogEntry)];
    VG_(printf)("Reading buffer\n");
    Int n = fread(buffer, sizeof(LogEntry), BUFFER_SIZE, mem_log_file);
    if (n <= 0) {
        return -1;
    }
    VG_(printf)("Read %d entries\n", n);
    dump_buffer(buffer, n);
    return n;
}

static void read_all_buffers()
{
    while (read_buffer() > 0) {
        ;
    }
    VG_(fclose)(mem_log_file);
    mem_log_file = NULL;
}

static void usage(void)
{
    VG_(printf)(
            "Usage: %s [options] <filename1> <filename2> ...\n"
            "    --help|-h               print this help message\n",
            argv0);
    VG_(exit)(1);
}

int main(int argc, char** argv)
{
    Int i;
    char* outfilename = NULL;
    Int outfileix = 0;

    if (argv[0])
        argv0 = argv[0];

    if (argc < 2)
        usage();

    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "-h") || streq(argv[i], "--help"))
            usage();
    }

    /* Scan args, all arguments are filenames */
    for (i = 1; i < argc; i++) {
        open_mem_log_file(argv[i]);
        read_all_buffers();
    }
}