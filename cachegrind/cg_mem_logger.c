#include "cg_arch.h"

/*------------------------------------------------------------*/
/*--- Double Buffered Logging System                        ---*/
/*------------------------------------------------------------*/

#define MAX_FILENAME_LEN 256

static LogBuffer buffer1;
static LogBuffer buffer2;
static LogBuffer* active_buffer;
static LogBuffer* inactive_buffer;
static int mem_log_fd = -1;
static Long guest_instrs_executed = 0;  // Global instruction counter

static void init_mem_logging(const HChar* filename)
{
    // Initialize buffers
    VG_(memset)(&buffer1, 0, sizeof(LogBuffer));
    VG_(memset)(&buffer2, 0, sizeof(LogBuffer));

    // Set up active/inactive buffers
    active_buffer = &buffer1;
    inactive_buffer = &buffer2;
    active_buffer->is_active = True;
    inactive_buffer->is_active = False;

    HChar* cachegrind_mem_file = VG_(expand_file_name)("--cachegrind-mem-file", filename);

    // Copy filename

    VG_(printf)("Opening log file: %s\n", cachegrind_mem_file);
    // Open log file
    SysRes o = VG_(open)(cachegrind_mem_file, VKI_O_CREAT | VKI_O_RDWR, 0600);
    if (sr_isError(o)) {
        VG_(umsg)("cannot create mem file %s\n", cachegrind_mem_file);
        VG_(exit)(1);
    } else {
        mem_log_fd = sr_Res(o);
    }
}

static void flush_mem_log_to_file(LogBuffer* buffer)
{
    if (buffer->count == 0)
        return;

    Int n = VG_(write)(mem_log_fd, buffer->entries, sizeof(LogEntry) * buffer->count);
    if (n != sizeof(LogEntry) * buffer->count) {
        VG_(umsg)("Failed to write to mem file\n");
        VG_(exit)(1);
    }
    // Reset buffer
    buffer->count = 0;
    VG_(memset)(buffer->entries, 0, sizeof(buffer->entries));
}

static void swap_memlog_buffers(void)
{
    LogBuffer* temp = active_buffer;
    active_buffer = inactive_buffer;
    inactive_buffer = temp;

    active_buffer->is_active = True;
    inactive_buffer->is_active = False;

    // Write inactive buffer to file
    flush_mem_log_to_file(inactive_buffer);
}

__attribute__((always_inline)) static __inline__ void log_mem_access(Addr addr, UChar size, AccessType type,
                                                                     CacheHitType hit_type)
{
    if (mem_log_fd < 0) {
        return;
    }

    // If active buffer is full, swap buffers
    if (active_buffer->count >= BUFFER_SIZE) {
        swap_memlog_buffers();
    }

    // Add entry to active buffer
    LogEntry* entry = &active_buffer->entries[active_buffer->count++];
    entry->addr = addr;
    entry->size = size;
    entry->type = type;
    entry->hit_type = hit_type;
    if (type == ACCESS_INSTR) {
        guest_instrs_executed++;
    }
    entry->timestamp = guest_instrs_executed;
    //VG_(printf)("Logged mem access: %llu %p %d %c %c\n", entry->timestamp, (void*)entry->addr, (int)entry->size,
    //            access_type_char(entry->type), cache_hit_char(entry->hit_type));
}

static void flush_mem_logging(void)
{
    // Write any remaining entries in both buffers
    flush_mem_log_to_file(active_buffer);
    flush_mem_log_to_file(inactive_buffer);

    // Close file
    VG_(close)(mem_log_fd);
    mem_log_fd = -1;
}
