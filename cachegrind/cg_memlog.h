#ifndef CG_MEMLOG_H
#define CG_MEMLOG_H

// For cache simulation
typedef struct {
    Int size;  // bytes
    Int assoc;
    Int line_size;  // bytes
} cache_t;

typedef struct {
    HChar cmdline[1024];
    cache_t I1;
    cache_t D1;
    cache_t LL;
} mem_log_header_t;

typedef enum {
    CACHE_HIT_L1,
    CACHE_MISS_L1,
    CACHE_MISS_LL,
    CACHE_STORE,
    CACHE_LOAD
} CacheHitType;

static inline Int cache_hit_char(CacheHitType hit_type)
{
    switch (hit_type) {
    case CACHE_HIT_L1:
        return 'H';
    case CACHE_MISS_L1:
        return 'm';
    case CACHE_MISS_LL:
        return 'M';
    case CACHE_STORE:
        return 'S';
    case CACHE_LOAD:
        return 'L';
    default:
        return '?';
    }
}

// Double buffered logging system
#define BUFFER_SIZE 1024  // Number of entries per buffer
typedef enum {
    ACCESS_READ,
    ACCESS_WRITE,
    ACCESS_INSTR,
    ACCESS_STORE,
    ACCESS_LOAD
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
    case ACCESS_STORE:
        return 'S';
    case ACCESS_LOAD:
        return 'L';
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

#endif  // CG_MEMLOG_H