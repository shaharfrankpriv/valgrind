/*--------------------------------------------------------------------*/
/*--- Memory segment scanner using Valgrind's find_nsegment API.   ---*/
/*--------------------------------------------------------------------*/

/* 
 * This code uses VG_(am_find_nsegment) to scan memory without relying on /proc.
 */

#include "pub_tool_basics.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_aspacemgr.h"
#include "pub_tool_mallocfree.h"

// Scan the memory address space to find segments
static void scan_memory_segments(void)
{
   VG_(umsg)("\n=== MEMORY SEGMENT MAP ===\n");
   VG_(umsg)("%-16s %-16s %-7s %s\n", 
          "START", "END", "PERMS", "TYPE");
   VG_(umsg)("---------------------------------------------------------------\n");
   
   // Track if we've already seen a segment
   UWord* seen_starts = NULL;
   UWord* seen_ends = NULL;
   Int seen_count = 0;
   Int seen_max = 1000;  // Maximum number of segments to track
   
   seen_starts = VG_(malloc)("seen.starts", seen_max * sizeof(UWord));
   seen_ends = VG_(malloc)("seen.ends", seen_max * sizeof(UWord));
   
   if (!seen_starts || !seen_ends) {
      VG_(umsg)("Error: Failed to allocate memory for segment tracking\n");
      if (seen_starts) VG_(free)(seen_starts);
      if (seen_ends) VG_(free)(seen_ends);
      return;
   }
   
   // Scan address space at regular intervals
   Int segments_found = 0;
   
   // Define scan ranges - we'll scan in chunks
   static const struct {
      Addr start;
      Addr end;
      Addr step;
   } scan_ranges[] = {
      // Low memory (code, data, bss) - scan densely
      { 0x400000, 0x1000000, 0x1000 },
      
      // Heap and nearby segments - scan moderately
      { 0x1000000, 0x10000000, 0x10000 },
      
      // Middle memory - scan sparsely
      { 0x10000000, 0x100000000ULL, 0x1000000 },
      
      // Higher memory - scan very sparsely
      { 0x100000000ULL, 0x7fffffffffff, 0x100000000ULL }
   };
   
   // Scan through ranges
   for (Int r = 0; r < sizeof(scan_ranges)/sizeof(scan_ranges[0]); r++) {
      for (Addr addr = scan_ranges[r].start; 
           addr < scan_ranges[r].end; 
           addr += scan_ranges[r].step) {
         
         const NSegment* seg = VG_(am_find_nsegment)(addr);
         if (!seg) continue;
         
         // Skip if no permissions
         if (!seg->hasR && !seg->hasW && !seg->hasX) continue;
         
         // Check if we've already seen this segment
         Bool already_seen = False;
         for (Int i = 0; i < seen_count; i++) {
            if (seg->start == seen_starts[i] && seg->end == seen_ends[i]) {
               already_seen = True;
               break;
            }
         }
         
         if (already_seen) continue;
         
         // Add to seen list if there's room
         if (seen_count < seen_max) {
            seen_starts[seen_count] = seg->start;
            seen_ends[seen_count] = seg->end;
            seen_count++;
         }
         
         // Format permissions
         HChar perms[4];
         perms[0] = seg->hasR ? 'r' : '-';
         perms[1] = seg->hasW ? 'w' : '-';
         perms[2] = seg->hasX ? 'x' : '-';
         perms[3] = '\0';
         
         // Determine segment type
         const HChar* seg_type = "unknown";
         if (seg->isCH) {
            seg_type = "heap";
         } else if (seg->hasX && seg->hasR) {
            seg_type = "code";
         } else if (seg->hasR && seg->hasW) {
            // Typical data segment
            if (addr > 0x7fffff000000ULL) {
               seg_type = "stack";
            } else {
               seg_type = "data";
            }
         } else if (seg->hasR && !seg->hasW) {
            seg_type = "rodata";
         }
         
         // Print segment info
         VG_(umsg)("0x%014lx 0x%014lx %-7s %s\n", 
                seg->start, seg->end, perms, seg_type);
         
         segments_found++;
      }
   }
   
   VG_(umsg)("Found %d memory segments\n", segments_found);
   VG_(umsg)("===============================================================\n");
   
   // Free tracking arrays
   VG_(free)(seen_starts);
   VG_(free)(seen_ends);
}

/* 
 * Add to cg_fini:
 *
 * scan_memory_segments();
 */
