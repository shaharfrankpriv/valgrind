/*--------------------------------------------------------------------*/
/*--- Direct stack and heap detection for Cachegrind.             ---*/
/*--------------------------------------------------------------------*/

/* 
 * This code uses direct methods to identify stack and heap.
 */

#include "pub_tool_aspacemgr.h"
#include "pub_tool_basics.h"
#include "pub_tool_guest.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_machine.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_threadstate.h"

// Print information about a specific memory address
static void print_address_info(Addr addr, const HChar* description)
{
    const NSegment* seg = VG_(am_find_nsegment)(addr);
    if (!seg) {
        VG_(umsg)("%s (0x%lx): Not in any valid segment\n", description, addr);
        return;
    }

    HChar perms[4];
    perms[0] = seg->hasR ? 'r' : '-';
    perms[1] = seg->hasW ? 'w' : '-';
    perms[2] = seg->hasX ? 'x' : '-';
    perms[3] = '\0';

    VG_(umsg)("%s (0x%lx): In segment 0x%lx-0x%lx perms=%s isCH=%u kind=%u\n", description, addr, seg->start, seg->end,
              perms, seg->isCH, seg->kind);
}

// Find stack segment using SP register
static void find_stack_segment(void)
{
    VG_(umsg)("\n=== STACK DETECTION ===\n");

    ThreadId tid;
    Addr sp;

    for (tid = 1; tid < VG_N_THREADS; tid++) {
        // Simply check if tid is in valid range
        // VG_N_THREADS is the max number of threads
        if (tid >= 1 && tid < VG_N_THREADS) {
            sp = VG_(get_SP)(tid);

            // Only report if SP seems valid (non-zero)
            if (sp != 0) {
                VG_(umsg)("Thread %u SP = 0x%lx\n", tid, sp);
                print_address_info(sp, "Stack pointer");
            }
        }
    }
}

// Find heap segments directly
static void find_heap_segments(void)
{
    VG_(umsg)("\n=== HEAP DETECTION ===\n");

    // Common heap start addresses
    static const struct {
        Addr addr;
        const HChar* desc;
    } heap_addrs[] = {
            {0x08000000, "Common heap base (0x8000000)"        },
            {0x08048000, "Another common heap area (0x8048000)"},
            {0x00603000, "Classic brk-based heap (0x603000)"   },
            {0x01000000, "mmap-based allocations (0x1000000)"  },
            {0x00602000, "Another brk point (0x602000)"        },
            {0x00601000, "Early brk point (0x601000)"          }
    };

    // Check known heap locations
    for (Int i = 0; i < sizeof(heap_addrs) / sizeof(heap_addrs[0]); i++) {
        print_address_info(heap_addrs[i].addr, heap_addrs[i].desc);
    }

    // Scan all segments for client heap (isCH) flag
    VG_(umsg)("\nScanning memory for segments with isCH flag set:\n");

    Bool found_ch = False;

    // Scan common heap ranges more densely
    for (Addr addr = 0x601000; addr < 0x610000; addr += 0x1000) {
        const NSegment* seg = VG_(am_find_nsegment)(addr);
        if (seg && seg->isCH) {
            HChar perms[4];
            perms[0] = seg->hasR ? 'r' : '-';
            perms[1] = seg->hasW ? 'w' : '-';
            perms[2] = seg->hasX ? 'x' : '-';
            perms[3] = '\0';

            VG_(umsg)("HEAP: 0x%lx-0x%lx perms=%s size=%lu bytes\n", seg->start, seg->end, perms,
                      seg->end - seg->start);
            found_ch = True;
        }
    }

    for (Addr addr = 0x8000000; addr < 0x10000000; addr += 0x10000) {
        const NSegment* seg = VG_(am_find_nsegment)(addr);
        if (seg && seg->isCH) {
            HChar perms[4];
            perms[0] = seg->hasR ? 'r' : '-';
            perms[1] = seg->hasW ? 'w' : '-';
            perms[2] = seg->hasX ? 'x' : '-';
            perms[3] = '\0';

            VG_(umsg)("HEAP: 0x%lx-0x%lx perms=%s size=%lu bytes\n", seg->start, seg->end, perms,
                      seg->end - seg->start);
            found_ch = True;
        }
    }

    if (!found_ch) {
        VG_(umsg)("No segments with isCH flag found. This is unusual.\n");
    }
}

// Dump target program's /proc/PID/maps
static void dump_target_proc_maps(void)
{
    Int pid;
    SysRes fd;
    HChar procmaps_path[64];
    HChar buf[8192];  // Increased buffer size
    Int n_read;
    HChar *line, *next_line;
    Bool is_valgrind_map;
    const HChar* region_type;
    Bool direct_read_failed = False;

    // First approach: Try reading /proc/self/maps which should be the client program's maps
    VG_(umsg)("\n=== TARGET PROGRAM MEMORY MAPS ===\n");

    // Construct the path to the maps file
    VG_(strcpy)(procmaps_path, "/proc/self/maps");

    VG_(umsg)("Attempting to read: %s\n", procmaps_path);

    // Open the maps file
    fd = VG_(open)(procmaps_path, VKI_O_RDONLY, 0);
    if (sr_isError(fd)) {
        VG_(umsg)("Error: Cannot open %s (error code: %ld)\n", procmaps_path, (long)sr_Err(fd));
        direct_read_failed = True;
    }

    if (!direct_read_failed) {
        VG_(umsg)("File opened successfully, reading contents...\n");
        VG_(umsg)("    Address Range         Perms Offset   Dev   Inode      Path/Region\n");
        VG_(umsg)("    --------------------  ----- -------- ----- --------- --------------------\n");

        // Read and print the maps file contents in chunks
        while (True) {
            n_read = VG_(read)(sr_Res(fd), buf, sizeof(buf) - 1);

            if (n_read < 0) {
                VG_(umsg)("Error reading maps file (error code: %d)\n", n_read);
                direct_read_failed = True;
                break;
            }

            if (n_read == 0) {
                // End of file reached
                break;
            }

            // Null-terminate the buffer
            buf[n_read] = '\0';

            // Process the buffer line by line
            line = buf;
            while (line && *line) {
                // Find the end of the current line
                next_line = VG_(strchr)(line, '\n');
                if (next_line) {
                    *next_line = '\0';
                    next_line++;
                }

                // Check if this is likely a Valgrind-related mapping
                is_valgrind_map = False;
                if (VG_(strstr)(line, "vgpreload") || VG_(strstr)(line, "valgrind") || VG_(strstr)(line, "vex")) {
                    is_valgrind_map = True;
                }

                // Determine region type based on patterns
                region_type = "";

                if (VG_(strstr)(line, "[stack]")) {
                    region_type = "[STACK]";
                } else if (VG_(strstr)(line, "[heap]")) {
                    region_type = "[HEAP]";
                } else if (VG_(strstr)(line, ".text") || (VG_(strstr)(line, " r-x") && !is_valgrind_map)) {
                    region_type = "[CODE]";
                } else if (VG_(strstr)(line, ".data") || (VG_(strstr)(line, " rw-") && !is_valgrind_map)) {
                    region_type = "[DATA]";
                } else if (VG_(strstr)(line, ".bss")) {
                    region_type = "[BSS]";
                } else if (VG_(strstr)(line, "anon") || VG_(strstr)(line, "vvar") || VG_(strstr)(line, "vdso")) {
                    region_type = "[SPECIAL]";
                }

                // Print the line, with classification
                if (is_valgrind_map) {
                    VG_(umsg)("V   %s %s\n", line, region_type);  // Valgrind mapping
                } else {
                    VG_(umsg)("    %s %s\n", line, region_type);  // Client program mapping
                }

                // Move to the next line
                line = next_line;
            }
        }

        // Close the file
        VG_(close)(sr_Res(fd));
    }

    // Fallback: Try to use system command if direct read failed
    if (direct_read_failed) {
        VG_(umsg)("Direct read failed, trying system command fallback...\n");

        // Get the client process PID
        pid = VG_(getpid)();

        // Use system command to get maps
        VG_(sprintf)(buf, "/bin/cat /proc/%d/maps", pid);
        VG_(umsg)("Executing: %s\n", buf);

        Int res = VG_(system)(buf);
        if (res != 0) {
            VG_(umsg)("System command failed with error code: %d\n", res);

            // One more fallback - try with /proc/self/maps
            VG_(strcpy)(buf, "/bin/cat /proc/self/maps");
            VG_(umsg)("Executing: %s\n", buf);
            res = VG_(system)(buf);

            if (res != 0) {
                VG_(umsg)("All approaches to read memory maps failed.\n");
            }
        }
    }

    VG_(umsg)("\n=== END OF TARGET PROGRAM MEMORY MAPS ===\n\n");
}

// Direct stack and heap detection
static void detect_stack_and_heap(void)
{
    // Find stack segments first
    find_stack_segment();

    // Then find heap segments
    find_heap_segments();

    // Dump target program's memory maps
    dump_target_proc_maps();

    // Print separation line
    VG_(umsg)("===============================================================\n");
}

/* 
 * Add to cg_fini:
 *
 * detect_stack_and_heap();
 */
