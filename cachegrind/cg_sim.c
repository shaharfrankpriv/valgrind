
/*--------------------------------------------------------------------*/
/*--- Cache simulation                                    cg_sim.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Cachegrind, a Valgrind tool for cache
   profiling programs.

   Copyright (C) 2002-2017 Nicholas Nethercote
      njn@valgrind.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

/* Notes:
  - simulates a write-allocate cache
  - (block --> set) hash function uses simple bit selection
  - handling of references straddling two cache blocks:
      - counts as only one cache access (not two)
      - both blocks hit                  --> one hit
      - one block hits, the other misses --> one miss
      - both blocks miss                 --> one miss (not two)
*/

typedef struct {
   Int          size;                   /* bytes */
   Int          assoc;
   Int          line_size;              /* bytes */
   Int          sets;
   Int          sets_min_1;
   Int          line_size_bits;
   Int          tag_shift;
   HChar        desc_line[128];  /* large enough */
   UWord*       tags;
   UWord*       used;            /* SF: utilization bitmask per tag, 32bit granularity */
   Int          total_used;    /* percent of already accessed words */
} cache_t2;

/* By this point, the size/assoc/line_size has been checked. */
static void cachesim_initcache(cache_t config, cache_t2* c)
{
   Int i;

   c->size      = config.size;
   c->assoc     = config.assoc;
   c->line_size = config.line_size;

   c->sets           = (c->size / c->line_size) / c->assoc;
   c->sets_min_1     = c->sets - 1;
   c->line_size_bits = VG_(log2)(c->line_size);
   c->tag_shift      = c->line_size_bits + VG_(log2)(c->sets);

   if (c->assoc == 1) {
      VG_(sprintf)(c->desc_line, "%d B, %d B, direct-mapped",
                                 c->size, c->line_size);
   } else {
      VG_(sprintf)(c->desc_line, "%d B, %d B, %d-way associative",
                                 c->size, c->line_size, c->assoc);
   }

   c->tags = VG_(malloc)("cg.sim.ci.1",
                         sizeof(UWord) * c->sets * c->assoc);
   c->used = VG_(malloc)("cg.sim.ci.used",
                         sizeof(UWord) * c->sets * c->assoc);

   for (i = 0; i < c->sets * c->assoc; i++) {
      c->tags[i] = ~0;
      c->used[i] = 0;
    }
    c->total_used = 0;
}

/* SF: Brian Kernighan’s Algorithm */
__attribute__((always_inline))
static __inline__
Int count_bits(UWord n)
{
    Int count = 0;
    while (n) {
        n &= (n - 1);
        count++;
    }
    return count;
}

/* Set the given used bitmap according to the addr+size and line_size_bits.
 * Returns the size of bytes NOT accounted for. If the returned size is > 0
 * then its means that the touched area is spanned across the next line
 * as well.
 */
static __inline__
Int set_used(UWord addr, Int size, Int line_size, UWord* used)
{
  *used = 0;
  Int offset = addr & (line_size - 1); /* SF line_size must be pow of 2 */
  Int words = line_size / 4;
  int bits = 0;
  for (Int w = offset/4; w <= (offset + size -1)/4; w++, bits++) {
      if (w >= words) {
        return size - bits * 4 + offset % 4;
      }
      *used |= (1 << w);
  }
  return 0;
}

/* This attribute forces GCC to inline the function, getting rid of a
 * lot of indirection around the cache_t2 pointer, if it is known to be
 * constant in the caller (the caller is inlined itself).
 * Without inlining of simulator functions, cachegrind can get 40% slower.
 */
__attribute__((always_inline))
static __inline__
Bool cachesim_setref_is_miss(cache_t2* c, UInt set_no, UWord tag, UWord u)
{
   int i, j;
   UWord *set, *used;
   UWord prev_used = 0; /* SF: prev used - used if shuffled */
   int prev_bits = 0, post_bits = 0;

   set = &(c->tags[set_no * c->assoc]);
   used = &(c->used[set_no * c->assoc]);

   /* This loop is unrolled for just the first case, which is the most */
   /* common.  We can't unroll any further because it would screw up   */
   /* if we have a direct-mapped (1-way) cache.                        */
   if (tag == set[0]) {
      prev_bits = count_bits(used[0]);
      used[0] |= u;
      post_bits = count_bits(used[0]);
      c->total_used += post_bits - prev_bits;
      return 0;  /* hit */
   }

   /* If the tag is one other than the MRU, move it into the MRU spot  */
   /* and shuffle the rest down.                                       */
   for (i = 1; i < c->assoc; i++) {
      if (tag == set[i]) {
         prev_used = used[i];
         for (j = i; j > 0; j--) {
            set[j] = set[j - 1];
            used[j] = used[j - 1];
         }
         set[0] = tag;
         used[0] = prev_used | u;
         prev_bits = count_bits(prev_used);
         post_bits = count_bits(used[0]);
         c->total_used += post_bits - prev_bits;

         return 0;  /* hit */
      }
   }

   /* A miss;  install this tag as MRU, shuffle rest down. */
   prev_bits = count_bits(used[c->assoc - 1]);
   for (j = c->assoc - 1; j > 0; j--) {
      set[j] = set[j - 1];
      used[j] = used[j - 1];
   }
   set[0] = tag;
   used[0] = u;
   post_bits = count_bits(used[0]);
   c->total_used += post_bits - prev_bits;

   return 1; /* miss */
}

__attribute__((always_inline))
static __inline__
Bool cachesim_ref_is_miss(cache_t2* c, Addr a, UChar size)
{
   /* A memory block has the size of a cache line */
   UWord block1 =  a         >> c->line_size_bits;
   UWord block2 = (a+size-1) >> c->line_size_bits;
   UInt  set1   = block1 & c->sets_min_1;

   /* Tags used in real caches are minimal to save space.
    * As the last bits of the block number of addresses mapping
    * into one cache set are the same, real caches use as tag
    *   tag = block >> log2(#sets)
    * But using the memory block as more specific tag is fine,
    * and saves instructions.
    */
   UWord tag1   = block1;

   /* Access entirely within line. */
   if (block1 == block2) {
      UWord used = 0;
      if (set_used(a, size, c->line_size, &used) > 0) {
        VG_(tool_panic)("set_used didn't consume the block within one line");
      }
      return cachesim_setref_is_miss(c, set1, tag1, used);
    }

   /* Access straddles two lines. */
   else if (block1 + 1 == block2) {
      UInt  set2 = block2 & c->sets_min_1;
      UWord tag2 = block2;
      UWord used1 = 0, used2 = 0;
      Int left =  set_used(a, size, c->line_size, &used1);
      if (left <= 0) {
        VG_(tool_panic)("set_used consumed the block but access is across two lines");
      }
      /* note that set_used may return > 0 if more then two lines are accessed,
       * but we ignore such cases.
       */
      set_used(0, left, c->line_size, &used2);

      /* always do both, as state is updated as side effect */
      return (cachesim_setref_is_miss(c, set1, tag1, used1) |
              cachesim_setref_is_miss(c, set2, tag2, used2));
   }
   VG_(printf)("addr: %lx  size: %u  blocks: %lu %lu",
               a, size, block1, block2);
   VG_(tool_panic)("item straddles more than two cache sets");
   /* not reached */
   return 1;
}


static cache_t2 LL;
static cache_t2 I1;
static cache_t2 D1;

static void cachesim_initcaches(cache_t I1c, cache_t D1c, cache_t LLc)
{
   cachesim_initcache(I1c, &I1);
   cachesim_initcache(D1c, &D1);
   cachesim_initcache(LLc, &LL);
}

__attribute__((always_inline))
static __inline__
void cachesim_I1_doref_Gen(Addr a, UChar size, CacheCC* cc)
{
  cc->a++;  /* access */
   if (cachesim_ref_is_miss(&I1, a, size)) {
      cc->m1++;
      if (cachesim_ref_is_miss(&LL, a, size)) {
         cc->mL++;
      }
      cc->llc_words += LL.total_used;
      VG_(umsg)("cachesim_I1_doref_Gen: MISS: I1 used %d LL used %d\n", I1.total_used, LL.total_used);
   }
   cc->l1_words += I1.total_used;
   //VG_(umsg)("cachesim_I1_doref_Gen: I1 used %d LL used %d\n", I1.total_used, LL.total_used);
}

// common special case IrNoX
__attribute__((always_inline))
static __inline__
void cachesim_I1_doref_NoX(Addr a, UChar size, CacheCC* cc)
{
   UWord block  = a >> I1.line_size_bits;
   UInt  I1_set = block & I1.sets_min_1;

   cc->a++;  /* access */
   // use block as tag
   UWord used = 0;
   set_used(a, size, I1.line_size, &used);
   if (cachesim_setref_is_miss(&I1, I1_set, block, used)) {
      /* L1 miss */
      cc->m1++;
      UInt  LL_set = block & LL.sets_min_1;
      set_used(a, size, LL.line_size, &used);
      // can use block as tag as L1I and LL cache line sizes are equal
      if (cachesim_setref_is_miss(&LL, LL_set, block, used)) {
         /* LL miss */
         cc->mL++;
      }
      cc->llc_words += LL.total_used;
      VG_(umsg)("cachesim_I1_doref_NoX: MISS I1 used %d LL used %d\n", I1.total_used, LL.total_used);
   }
   //VG_(umsg)("cachesim_I1_doref_NoX: I1 used %d LL used %d\n", I1.total_used, LL.total_used);
   cc->l1_words += I1.total_used;
}

__attribute__((always_inline))
static __inline__
void cachesim_D1_doref(Addr a, UChar size, CacheCC* cc)
{
   cc->a++;  /* access */
   if (cachesim_ref_is_miss(&D1, a, size)) {
      /* L1d miss */
      cc->m1++;
      if (cachesim_ref_is_miss(&LL, a, size)) {
         /* LL miss */
         cc->mL++;
      }
      cc->llc_words += LL.total_used;
      VG_(umsg)("cachesim_D1_doref: MISS D1 used %d LL used %d\n", D1.total_used, LL.total_used);
   }
   //VG_(umsg)("cachesim_D1_doref: D1 used %d LL used %d\n", D1.total_used, LL.total_used);
   cc->l1_words += D1.total_used;
}

/* Check for special case IrNoX. Called at instrumentation time.
 *
 * Does this Ir only touch one cache line, and are L1I/LL cache
 * line sizes the same? This allows to get rid of a runtime check.
 *
 * Returning false is always fine, as this calls the generic case
 */
static Bool cachesim_is_IrNoX(Addr a, UChar size)
{
   UWord block1, block2;

   if (I1.line_size_bits != LL.line_size_bits) return False;
   block1 =  a         >> I1.line_size_bits;
   block2 = (a+size-1) >> I1.line_size_bits;
   if (block1 != block2) return False;

   return True;
}

/*--------------------------------------------------------------------*/
/*--- end                                                 cg_sim.c ---*/
/*--------------------------------------------------------------------*/
