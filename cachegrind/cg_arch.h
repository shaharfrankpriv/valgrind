
/*--------------------------------------------------------------------*/
/*--- Arch-specific declarations, cache configuration.   cg_arch.h ---*/
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

#ifndef __CG_ARCH_H
#define __CG_ARCH_H

// For cache simulation
typedef struct {
    Int size;  // bytes
    Int assoc;
    Int line_size;  // bytes
} cache_t;

typedef enum {
    CACHE_HIT_L1,
    CACHE_MISS_L1,
    CACHE_MISS_LL
} CacheHitType;

static inline Int cache_hit_char(CacheHitType hit_type)
{
    switch (hit_type) {
    case CACHE_HIT_L1:
        return 'H';
    case CACHE_MISS_L1:
        return 'L';
    case CACHE_MISS_LL:
        return 'M';
    default:
        return '?';
    }
}

// Double buffered logging system
#define BUFFER_SIZE 1024  // Number of entries per buffer
typedef enum {
    ACCESS_READ,
    ACCESS_WRITE,
    ACCESS_INSTR
} AccessType;

static inline Int access_type_char(AccessType atype)
{
    switch (atype) {
    case ACCESS_READ:
        return 'R';
    case ACCESS_WRITE:
        return 'W';
    case ACCESS_INSTR:
        return 'I';
    default:
        return '?';
    }
}

typedef struct {
    Addr addr;
    UChar size;
    AccessType type;
    CacheHitType hit_type;
    ULong timestamp;  // High resolution timestamp
} LogEntry;

typedef struct {
    LogEntry entries[BUFFER_SIZE];
    Int count;
    Bool is_active;
} LogBuffer;

typedef struct {
    ULong a;         /* total # memory accesses of this kind */
    ULong m1;        /* misses in the first level cache */
    ULong mL;        /* misses in the second level cache */
    ULong l1_words;  /* number of different 32 bit words accessed in
                         l1 evicted lines */
    ULong llc_words; /* number of different 32 bit words accessed in
                         llc evicted lines */
} CacheCC;

#define MIN_LINE_SIZE 16

// clo_*c used in the call to VG_(str_clo_cache_opt) should be statically
// initialized to UNDEFINED_CACHE.
#define UNDEFINED_CACHE {-1, -1, -1}

// If arg is a command line option configuring I1 or D1 or LL cache,
// then parses arg to set the relevant cache_t elements.
// Returns True if arg is a cache command line option, False otherwise.
Bool VG_(str_clo_cache_opt)(const HChar* arg, cache_t* clo_I1c, cache_t* clo_D1c, cache_t* clo_LLc);

// Checks the correctness of the auto-detected caches.
// If a cache has been configured by command line options, it
// replaces the equivalent auto-detected cache.
// Note that an invalid auto-detected cache will make Valgrind exit
// with an fatal error, except if the invalid auto-detected cache
// will be replaced by a command line defined cache.
void VG_(post_clo_init_configure_caches)(cache_t* I1c, cache_t* D1c, cache_t* LLc, cache_t* clo_I1c, cache_t* clo_D1c,
                                         cache_t* clo_LLc);

void VG_(print_cache_clo_opts)(void);

#endif  // __CG_ARCH_H

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
