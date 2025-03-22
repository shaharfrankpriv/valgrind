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
#include "pub_tool_libcprint.h"
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

    VG_(umsg)("%s (0x%lx): In segment 0x%lx-0x%lx perms=%s isCH=%d kind=%d\n", description, addr, seg->start, seg->end,
              perms, seg->isCH, seg->kind);
}

// Find stack segment using SP register
static void find_stack_segment(void)
{
    VG_(umsg)("\n=== STACK DETECTION ===\n");

    ThreadId tid;
    Addr sp;

    for (tid = 1; tid < VG_N_THREADS; tid++) {
        if (VG_(is_valid_tid)(tid)) {
            sp = VG_(get_SP)(tid);

            VG_(umsg)("Thread %d SP = 0x%lx\n", tid, sp);
            print_address_info(sp, "Stack pointer");
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

    const HChar* name;
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

// Direct stack and heap detection
static void detect_stack_and_heap(void)
{
    // Find stack segments first
    find_stack_segment();

    // Then find heap segments
    find_heap_segments();

    // Print separation line
    VG_(umsg)("===============================================================\n");
}

/* 
 * Add to cg_fini:
 *
 * detect_stack_and_heap();
 */
