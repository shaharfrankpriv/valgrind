
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

__attribute__((always_inline)) static __inline__ void log_mem_access(Addr addr, UChar size, AccessType type,
                                                                     CacheHitType hit_type);

typedef struct {
    Int size; /* bytes */
    Int assoc;
    Int line_size; /* bytes */
    Int sets;
    Int sets_min_1;
    Int line_size_bits;
    Int tag_shift;
    HChar desc_line[128]; /* large enough */
    UWord* tags;
    UWord* used;                       /* SF: utilization bitmask per tag, 32bit granularity */
    UChar* dirty;                      /* SF: dirty flag per tag, cache line granularity */
    Int total_used;                    /* percent of already accessed words */
    UWord total_dirty_read_evictions;  /* total number of dirty cache lines evicted */
    UWord total_dirty_write_evictions; /* total number of dirty cache lines evicted */
    UWord total_read_loads;            /* total number of loads due to read */
    UWord total_write_loads;           /* total number of loads due to write */
    Bool is_llc;                       /* Is this a Last Level Cache? */
} cache_t2;

int cache_flush(cache_t2* c);

/* By this point, the size/assoc/line_size has been checked. */
static void cachesim_initcache(cache_t config, cache_t2* c)
{
    Int i;

    c->size = config.size;
    c->assoc = config.assoc;
    c->line_size = config.line_size;

    c->sets = (c->size / c->line_size) / c->assoc;
    c->sets_min_1 = c->sets - 1;
    c->line_size_bits = VG_(log2)(c->line_size);
    c->tag_shift = c->line_size_bits + VG_(log2)(c->sets);

    if (c->assoc == 1) {
        VG_(sprintf)(c->desc_line, "%d B, %d B, direct-mapped", c->size, c->line_size);
    } else {
        VG_(sprintf)(c->desc_line, "%d B, %d B, %d-way associative", c->size, c->line_size, c->assoc);
    }

    c->tags = VG_(malloc)("cg.sim.ci.1", sizeof(UWord) * c->sets * c->assoc);
    c->used = VG_(malloc)("cg.sim.ci.used", sizeof(UWord) * c->sets * c->assoc);
    c->dirty = VG_(malloc)("cg.sim.ci.dirty", sizeof(UChar) * c->sets * c->assoc);
    for (i = 0; i < c->sets * c->assoc; i++) {
        c->tags[i] = ~0;
        c->used[i] = 0;
        c->dirty[i] = 0;
    }
    c->total_used = 0;
}

/*
 * Flushes the cache.
 * Returns the number of dirty lines evicted.
 */
int cache_flush(cache_t2* c)
{
    int i;
    int dirty_lines = 0;
    for (i = 0; i < c->sets * c->assoc; i++) {
        if (c->dirty[i]) {
            dirty_lines++;
            if (c->is_llc) {
                log_mem_access(c->tags[i] << c->line_size_bits, c->line_size, ACCESS_FLUSH, CACHE_STORE);
            }
            c->dirty[i] = 0;
        }
    }
    return dirty_lines;
}

/* SF: Brian Kernighan’s Algorithm */
__attribute__((always_inline)) static __inline__ Int count_bits(UWord n)
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
static __inline__ Int mark_used_bits(UWord addr, Int size, Int line_size, UWord* used)
{
    *used = 0;
    Int offset = addr & (line_size - 1); /* SF line_size must be pow of 2 */
    Int words = line_size / 4;
    int bits = 0;
    for (Int w = offset / 4; w <= (offset + size - 1) / 4; w++, bits++) {
        if (w >= words) {
            return size - bits * 4 + offset % 4;
        }
        *used |= (1 << w);
    }
    return 0;
}

/* Mark the given tag as dirty. This is used to mark the LL cache entry as dirty if the L1 cache entry is dirty such
that the LL cache entry is flushed when it is evicted. */
__attribute__((always_inline)) static __inline__ void cachesim_set_mark_dirty(cache_t2* c, UInt set_no, UWord tag)
{
    int i;

    UWord* set = &(c->tags[set_no * c->assoc]);
    UChar* dirty = &(c->dirty[set_no * c->assoc]);

    for (i = 1; i < c->assoc; i++) {
        if (tag != set[i]) {
            continue;
        }
        dirty[i] = 1;
        return;
    }
    //VG_(printf)("set_mark_dirty: tag not found cache: %s is llc: %d set_no: %u tag: %lx\n", c->desc_line, c->is_llc,
    //            set_no, tag);
    //VG_(tool_panic)("set_mark_dirty: tag not found");
}

/* This attribute forces GCC to inline the function, getting rid of a
 * lot of indirection around the cache_t2 pointer, if it is known to be
 * constant in the caller (the caller is inlined itself).
 * Without inlining of simulator functions, cachegrind can get 40% slower.
 */
__attribute__((always_inline)) static __inline__ Bool cachesim_setref_is_miss(cache_t2* c, UInt set_no, UWord tag,
                                                                              UWord u, AccessType access_type,
                                                                              UWord* evicted_tag)
{
    int i, j;
    UWord *set, *used;
    UChar* dirty;
    UWord prev_used = 0;  /* SF: prev used - used if shuffled */
    UChar prev_dirty = 0; /* SF: prev dirty - dirty if shuffled */
    int prev_bits = 0, post_bits = 0;

    if (evicted_tag != NULL) {
        *evicted_tag = ~0;  // no tag
    }

    set = &(c->tags[set_no * c->assoc]);
    used = &(c->used[set_no * c->assoc]);
    dirty = &(c->dirty[set_no * c->assoc]);
    /* This loop is unrolled for just the first case, which is the most */
    /* common.  We can't unroll any further because it would screw up   */
    /* if we have a direct-mapped (1-way) cache.                        */
    if (tag == set[0]) {
        prev_bits = count_bits(used[0]);
        used[0] |= u;
        post_bits = count_bits(used[0]);
        c->total_used += post_bits - prev_bits;
        if (access_type == ACCESS_WRITE) {
            dirty[0] = 1;
        }
        return 0; /* hit */
    }

    /* If the tag is one other than the MRU, move it into the MRU spot  */
    /* and shuffle the rest down.                                       */
    for (i = 1; i < c->assoc; i++) {
        if (tag != set[i]) {
            continue;
        }
        // position found, move it to the MRU spot
        prev_used = used[i];
        prev_dirty = dirty[i];
        // push all records from 1..to i down one place
        for (j = i; j > 0; j--) {
            set[j] = set[j - 1];
            used[j] = used[j - 1];
            dirty[j] = dirty[j - 1];
        }
        // insert the new record at the MRU spot [0]
        set[0] = tag;
        used[0] = prev_used | u;
        dirty[0] = prev_dirty;
        // update the total used & dirty
        prev_bits = count_bits(prev_used);
        post_bits = count_bits(used[0]);
        c->total_used += post_bits - prev_bits;
        if (access_type == ACCESS_WRITE) {
            dirty[0] = 1;
        }

        return 0; /* hit */
    }

    /* A miss;  install this tag as MRU (c->assoc - 1), shuffle rest down. */

    // If the entry we evict is dirty will need to flush it
    int mru_index = c->assoc - 1;
    if (evicted_tag != NULL) {
        *evicted_tag = set[mru_index];
    }
    if (dirty[mru_index] != 0) {
        if (access_type == ACCESS_READ) {
            c->total_dirty_read_evictions++;
        } else {
            c->total_dirty_write_evictions++;
        }
        if (c->is_llc) {
            log_mem_access(set[mru_index] << c->line_size_bits, c->line_size, ACCESS_STORE, CACHE_STORE);
        }
    }

    // Shuffle all records down one place
    for (j = c->assoc - 1; j > 0; j--) {
        set[j] = set[j - 1];
        used[j] = used[j - 1];
        dirty[j] = dirty[j - 1];
    }

    // Update the total read/write loads & the memory load is llc
    if (access_type == ACCESS_READ) {
        c->total_read_loads++;
    } else {
        c->total_write_loads++;
    }
    if (c->is_llc) {
        log_mem_access(tag << c->line_size_bits, c->line_size, ACCESS_LOAD, CACHE_LOAD);
    }
    // Insert the new record at the MRU spot [0]
    set[0] = tag;
    used[0] = u;
    dirty[0] = access_type == ACCESS_WRITE ? 1 : 0;
    post_bits = count_bits(used[0]);
    c->total_used += post_bits;  // evicted records are not counted, we care just about the current record

    return 1; /* miss, evicted tag is returned in evicted_tag */
}

__attribute__((always_inline)) static __inline__ void cachesim_mark_dirty(cache_t2* c, Addr a, UChar size)
{
    // Access type is used to mark the cache line as dirty.

    /* A memory block has the size of a cache line */
    UWord block1 = a >> c->line_size_bits;
    UWord block2 = (a + size - 1) >> c->line_size_bits;
    UInt set1 = block1 & c->sets_min_1;
    UWord tag1 = block1;

    cachesim_set_mark_dirty(c, set1, tag1);

    if (block1 != block2) {
        UInt set2 = block2 & c->sets_min_1;
        UWord tag2 = block2;
        cachesim_set_mark_dirty(c, set2, tag2);
    }
}

__attribute__((always_inline)) static __inline__ void cachesim_evict_tag(cache_t2* c, UWord tag)
{
    // Access type is used to mark the cache line as dirty.

    /* A memory block has the size of a cache line */
    UInt set_no = tag & c->sets_min_1;

    int i, j;
    UWord *set, *used;
    UChar* dirty;

    set = &(c->tags[set_no * c->assoc]);
    used = &(c->used[set_no * c->assoc]);
    dirty = &(c->dirty[set_no * c->assoc]);

    /* If the tag is one other than the MRU, move it into the MRU spot  */
    /* and shuffle the rest down.                                       */
    for (i = 0; i < c->assoc; i++) {
        if (tag != set[i]) {
            continue;
        }
        if (c->is_llc && dirty[i]) {
            log_mem_access(set[i] << c->line_size_bits, c->line_size, ACCESS_STORE, CACHE_STORE);
        }
        // push all records from i..to i up one place
        for (j = i; j < c->assoc - 1; j++) {
            set[j] = set[j + 1];
            used[j] = used[j + 1];
            dirty[j] = dirty[j + 1];
        }
        // clear the LRU record
        set[c->assoc - 1] = ~0;
        used[c->assoc - 1] = 0;
        dirty[c->assoc - 1] = 0;
    }
}

__attribute__((always_inline)) static __inline__ Bool cachesim_ref_is_miss(cache_t2* c, Addr a, UChar size,
                                                                           AccessType access_type,
                                                                           cache_t2* child_cache)
{
    // Access type is used to mark the cache line as dirty.

    /* A memory block has the size of a cache line */
    UWord block1 = a >> c->line_size_bits;
    UWord block2 = (a + size - 1) >> c->line_size_bits;
    UInt set1 = block1 & c->sets_min_1;

    /* Tags used in real caches are minimal to save space.
    * As the last bits of the block number of addresses mapping
    * into one cache set are the same, real caches use as tag
    *   tag = block >> log2(#sets)
    * But using the memory block as more specific tag is fine,
    * and saves instructions.
    */
    UWord tag1 = block1;

    /* Access entirely within line. */
    if (block1 == block2) {
        UWord used = 0;
        if (mark_used_bits(a, size, c->line_size, &used) > 0) {
            VG_(tool_panic)("set_used didn't consume the block within one line");
        }
        UWord evicted_tag = ~0;
        Bool miss = cachesim_setref_is_miss(c, set1, tag1, used, access_type, &evicted_tag);
        if (evicted_tag != ~0 && child_cache != NULL) {
            cachesim_evict_tag(child_cache, evicted_tag);
        }
        return miss;
    }

    /* Access straddles two lines. */
    else if (block1 + 1 == block2) {
        UInt set2 = block2 & c->sets_min_1;
        UWord tag2 = block2;
        UWord used1 = 0, used2 = 0;
        Int left = mark_used_bits(a, size, c->line_size, &used1);
        if (left <= 0) {
            VG_(tool_panic)("set_used consumed the block but access is across two lines");
        }
        /* note that set_used may return > 0 if more then two lines are accessed,
            * but we ignore such cases.
            */
        mark_used_bits(0, left, c->line_size, &used2);

        /* always do both, as state is updated as side effect */
        UWord evicted_tag1 = ~0;
        UWord evicted_tag2 = ~0;
        Bool miss1 = cachesim_setref_is_miss(c, set1, tag1, used1, access_type, &evicted_tag1);
        Bool miss2 = cachesim_setref_is_miss(c, set2, tag2, used2, access_type, &evicted_tag2);
        if (evicted_tag1 != ~0 && child_cache != NULL) {
            cachesim_evict_tag(child_cache, evicted_tag1);
        }
        if (evicted_tag2 != ~0 && child_cache != NULL) {
            cachesim_evict_tag(child_cache, evicted_tag2);
        }
        return miss1 || miss2;
    }
    VG_(printf)("addr: %lx  size: %u  blocks: %lu %lu", a, size, block1, block2);
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
    LL.is_llc = True;
}

__attribute__((always_inline)) static __inline__ void cachesim_I1_doref_Gen(Addr a, UChar size, CacheCC* cc)
{
    cc->a++; /* access */
    CacheHitType hit_type = CACHE_HIT_L1;
    if (cachesim_ref_is_miss(&I1, a, size, ACCESS_INSTR, NULL)) {
        hit_type = CACHE_MISS_L1;
        cc->m1++;

        // Inst cache is not inclusive, not need to pass child cache
        if (cachesim_ref_is_miss(&LL, a, size, ACCESS_INSTR, NULL)) {
            hit_type = CACHE_MISS_LL;
            cc->mL++;
        }
        //VG_(umsg)("cachesim_I1_doref_Gen: MISS: I1 used %d LL used %d\n", I1.total_used, LL.total_used);
    }
    log_mem_access(a, size, ACCESS_INSTR, hit_type);
    cc->llc_words += LL.total_used;
    cc->l1_words += I1.total_used;
    //VG_(umsg)("cachesim_I1_doref_Gen: -I1 used %d LL used %d\n", I1.total_used, LL.total_used);
}

// common special case IrNoX
__attribute__((always_inline)) static __inline__ void cachesim_I1_doref_NoX(Addr a, UChar size, CacheCC* cc)
{
    UWord block = a >> I1.line_size_bits;
    UInt I1_set = block & I1.sets_min_1;

    cc->a++; /* access */
    CacheHitType hit_type = CACHE_HIT_L1;
    // use block as tag
    UWord used = 0;
    mark_used_bits(a, size, I1.line_size, &used);
    UWord evicted_tag = ~0;
    if (cachesim_setref_is_miss(&I1, I1_set, block, used, ACCESS_INSTR, NULL)) {
        /* L1 miss */
        cc->m1++;
        hit_type = CACHE_MISS_L1;
        UInt LL_set = block & LL.sets_min_1;
        mark_used_bits(a, size, LL.line_size, &used);
        // Inst cache is not inclusive, not need to pass child cache
        // can use block as tag as L1I and LL cache line sizes are equal
        if (cachesim_setref_is_miss(&LL, LL_set, block, used, ACCESS_INSTR, &evicted_tag)) {
            /* LL miss */
            hit_type = CACHE_MISS_LL;
            cc->mL++;
            // It is possible that the I ref caused a LL cache line to be evicted so invalidate the D1 cache line
            // as well, as D1 is inclusive
            if (evicted_tag != ~0) {
                cachesim_evict_tag(&D1, evicted_tag);
            }
        }
        //VG_(umsg)("cachesim_I1_doref_NoX: MISS I1 used %d LL used %d\n", I1.total_used, LL.total_used);
    }
    //VG_(umsg)("cachesim_I1_doref_NoX: -I1 used %d LL used %d\n", I1.total_used, LL.total_used);
    log_mem_access(a, size, ACCESS_INSTR, hit_type);
    cc->llc_words += LL.total_used;
    cc->l1_words += I1.total_used;
}

__attribute__((always_inline)) static __inline__ void cachesim_D1_doref(Addr a, UChar size, CacheCC* cc,
                                                                        AccessType access_type)
{
    cc->a++; /* access */
    CacheHitType hit_type = CACHE_HIT_L1;
    if (cachesim_ref_is_miss(&D1, a, size, access_type, NULL)) {
        /* L1d miss */
        cc->m1++;
        hit_type = CACHE_MISS_L1;
        // Data cache is inclusive, need to pass child cache to ensure D1 entry is evicted if LL entry is evicted
        if (cachesim_ref_is_miss(&LL, a, size, access_type, &D1)) {
            /* LL miss */
            hit_type = CACHE_MISS_LL;
            cc->mL++;
        }
        //VG_(umsg)("cachesim_D1_doref: MISS D1 used %d LL used %d\n", D1.total_used, LL.total_used);
    } else {
        // HIT - if write, propagate the dirty bit to the LL cache line as well
        if (access_type == ACCESS_WRITE) {
            cachesim_mark_dirty(&LL, a, size);
        }
    }
    //VG_(umsg)("cachesim_D1_doref: -D1 used %d LL used %d\n", D1.total_used, LL.total_used);
    log_mem_access(a, size, access_type, hit_type);
    cc->l1_words += D1.total_used;
    cc->llc_words += LL.total_used;
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

    if (I1.line_size_bits != LL.line_size_bits)
        return False;
    block1 = a >> I1.line_size_bits;
    block2 = (a + size - 1) >> I1.line_size_bits;
    if (block1 != block2)
        return False;

    return True;
}

/*--------------------------------------------------------------------*/
/*--- end                                                 cg_sim.c ---*/
/*--------------------------------------------------------------------*/
