#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef signed long Word;
typedef unsigned long UWord;
typedef unsigned char Bool;
#define True ((Bool)1)
#define False ((Bool)0)
typedef signed int Int;
typedef unsigned int UInt;
typedef unsigned long long int ULong;
typedef signed char Char;
typedef size_t SizeT;
typedef char HChar;
typedef long Addr;
typedef unsigned char UChar;

#define VG_(x) vg_##x
#define vg_sprintf sprintf
#define vg_printf printf
#define vg_umsg printf
#define vg_log2 log2
#define vg_tool_panic printf
#define vg_malloc(desc, sz) calloc(1, sz)
#include "../cg_arch.h"
#include "../cg_sim.c"
//#include "cg_branchpred.c"

/*------------------------------------------------------------*/
/*--- Constants                                            ---*/
/*------------------------------------------------------------*/

#include <stdio.h>
#include <stdarg.h>

void Panic(const char *fmt, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    fprintf(stderr, "Panic: %s\n", buffer);
    exit(1);
}

char *tobin(UWord w, char *buf, int n)
{
    const int bits = sizeof(w) * 8;
    int e = bits > n - 1 ? n - 1 : bits;
    int i;
    for (i = 0; i < e; i++)
    {
        buf[i] = ((1 << i) & w) ? '1' : '0';
    }
    buf[i] = 0;
    return buf;
}

void test_count_bits()
{
    struct Test {
        UInt u;
        Int bits;
    } tests [] = {
        {0x808, 2},
        {0x7808, 5},
        {0xffff, 16},
        {~0, 32},
        {0, 0},
        {0, -1}
    };

    printf("*** count_bits() test...\n");

    for (struct Test *t = tests; t->bits >= 0; t++) {
        int c = count_bits(t->u);
        printf("count bits 0x%x - %d\n", t->u, c);
        if (c != t->bits) {
            Panic("Test count_bits: u %u expected %d bits, got %d", t->u, t->bits, c);
        }
    }
    printf("*** count_bits() - OK.\n");
}

void test_set_used()
{
    struct Test
    {
        UWord a; // Address
        Int s;   // Size
        Int l;   // Cache Line Size
        char *b; // Bitmap
        Int r;   // expected Result
    } tests[] = {
        {0, 4, 64, "1000000000000000", 0},
        {2, 4, 64, "1100000000000000", 0},
        {0, 5, 64, "1100000000000000", 0},
        {32, 4, 64, "0000000010000000", 0},
        {32, 8, 64, "0000000011000000", 0},
        {32, 16, 64, "0000000011110000", 0},
        {32, 12, 64, "0000000011100000", 0},
        {60, 8, 64, "0000000000000001", 4},
        {62, 4, 64, "0000000000000001", 2},
        {62, 8, 64, "0000000000000001", 6},
        {62, 8, 64, "0000000000000001", 6},
        {56, 10, 64, "0000000000000011", 2},
        {56, 20, 64, "0000000000000011", 12},
        {}, // end marker

    };
    char buf[128];
    UWord w;
    Int d;
    const int words = 64 / 4 + 1;

    printf("*** set_used() test...\n");
    for (int i = 0; tests[i].b; i++)
    {
        struct Test *t = tests + i;
        d = set_used(t->a, t->s, t->l, &w);
        char *s = tobin(w, buf, words);
        printf("set used: addr = %lu, size = %d, cache_line_sz = %d:  w = %s d = %d\n",
               t->a, t->s, t->l, s, d);
        if (strncmp(t->b, s, sizeof(buf))) {
            Panic("Failed: expected %s got %s", t->b, s);
        }
    }
    printf("*** set_used() - OK.\n");
}

void dump_cache_t2(cache_t2 *c)
{
    char tagstr[1024];
    printf("desc %s assoc %d line_size %d line_size_bits %d sets %d size %d tag_shift %d\n",
           c->desc_line, c->assoc, c->line_size, c->line_size_bits, c->sets, c->size, c->tag_shift);
    for (int i = 0; i < c->sets; i++)
    {
        int o = 0;
        const int words = 64 / 4 + 1;
        char buf[128];

        for (int j = 0; j < c->assoc; j++)
        {
            char *s = tobin(c->used[i * c->assoc + j], buf, words);
            o += sprintf(tagstr + o, "%ld (%s) ", c->tags[i * c->assoc + j], s);
        }
        printf("  Set: %d Tags: %s\n", i, tagstr);
    }
}

void dump_CacheCC(CacheCC *cc)
{
    printf("CacheCC: accesses %llu miss1 %llu missLL %llu words1 %llu workdsLL %llu\n",
           cc->a, cc->m1, cc->mL, cc->l1_words, cc->llc_words);
}

void test_caches()
{
    struct Test
    {
        char *desc;
        UWord a;  // Address
        Int s;    // Size
        Bool m1;  // expected miss m1
        Bool mL;  // expected miss llc
        Int l1w;  // expected used words l1
        Int llcw; // expected used words m1
    } itests[] = {
        {"first line 0, unaligned word 0+1", .a = 1, .s = 4, .m1 = 1, .mL = 1, .l1w = 2, .llcw = 2},
        {"line 0 (hit), word 2", .a = 8, .s = 4, .m1 = 0, .mL = 0, .l1w = 3, .llcw = 2},
        {"line 0 (hit), word 8", .a = 32, .s = 4, .m1 = 0, .mL = 0, .l1w = 4, .llcw = 2},
        {"line 2 (miss), word 8", .a = 2 * 64 + 32, .s = 4, .m1 = 1, .mL = 1, .l1w = 5, .llcw = 3},
        {"line 2 (hit), word 3", .a = 2 * 64 + 12, .s = 4, .m1 = 0, .mL = 0, .l1w = 6, .llcw = 3},
        {"line 0 (hit), word 6 (shuffle MRU)", .a = 48, .s = 4, .m1 = 0, .mL = 0, .l1w = 7, .llcw = 3},
        {"line 0 (hit), word 6 (revisit word)", .a = 48, .s = 4, .m1 = 0, .mL = 0, .l1w = 7, .llcw = 3},
        {"line 1 (miss), word 0 (replace LRU)", .a = 64, .s = 4, .m1 = 1, .mL = 1, .l1w = 6, .llcw = 4},
        {"line 2 (l1 miss, LL hit), word 0 (replace LRU)", .a = 64 * 2, .s = 4, .m1 = 1, .mL = 0, .l1w = 2, .llcw = 5},
        {}};

    struct Test dtests[] = {
        {"line 0, word 0+1+2 (d1 miss, ll hit)", .a = 0, .s = 12, .m1 = 1, .mL = 0, .l1w = 3, .llcw = 6},
        {"cross line 0+1, last+first words", .a = 60, .s = 8, .m1 = 1, .mL = 0, .l1w = 5, .llcw = 7},
        {}};

    struct Test igtests[] = {
        {"line 1+2, words first + last (i1 miss, ll hit)", .a = 64+60, .s = 8, .m1 = 0, .mL = 0, .l1w = 3, .llcw = 7},
        {"cross line 0+1, 2 last+first words", .a = 56, .s = 12, .m1 = 1, .mL = 0, .l1w = 3, .llcw = 8},
        {}};

    cache_t I1c = {.assoc = 2, .line_size = 64, .size = 64 * 2};
    cache_t D1c = {.assoc = 2, .line_size = 64, .size = 64 * 2};
    cache_t LLc = {.assoc = 2, .line_size = 64, .size = 64 * 4};

    printf("*** Caches test...\n");

    cachesim_initcaches(I1c, D1c, LLc);
    sprintf(I1.desc_line, "I1");
    sprintf(D1.desc_line, "D1");
    sprintf(LL.desc_line, "LL");

    dump_cache_t2(&I1);
    dump_cache_t2(&D1);
    dump_cache_t2(&LL);

    for (struct Test *t = itests; t->s; t++)
    {
        CacheCC icc = {};
        printf("## I1: %s: Access %lu, size %d\n", t->desc, t->a, t->s);
        cachesim_I1_doref_NoX(t->a, t->s, &icc);
        dump_cache_t2(&I1);
        dump_cache_t2(&LL);
        dump_CacheCC(&icc);

        if (icc.m1 != t->m1 || icc.mL != t->mL ||
            icc.l1_words != t->l1w || icc.llc_words != t->llcw)
        {
            Panic("I CacheLine stats != m1 %d, mL %d, l1w %d, llcw %d",
                   t->m1, t->mL, t->l1w, t->llcw);
        }
    }

    for (struct Test *t = dtests; t->s; t++)
    {
        CacheCC dcc = {};
        printf("## D1: %s: Access %lu, size %d\n", t->desc, t->a, t->s);
        cachesim_D1_doref(t->a, t->s, &dcc);
        dump_cache_t2(&D1);
        dump_cache_t2(&LL);
        dump_CacheCC(&dcc);
        if (dcc.m1 != t->m1 || dcc.mL != t->mL ||
            dcc.l1_words != t->l1w || dcc.llc_words != t->llcw)
        {
            Panic("D CacheLine stats != m1 %d, mL %d, l1w %d, llcw %d",
                   t->m1, t->mL, t->l1w, t->llcw);
        }
    }

    for (struct Test *t = igtests; t->s; t++)
    {
        CacheCC icc = {};
        printf("## I1 (gen): %s: Access %lu, size %d\n", t->desc, t->a, t->s);
        cachesim_I1_doref_Gen(t->a, t->s, &icc);
        dump_cache_t2(&I1);
        dump_cache_t2(&LL);
        dump_CacheCC(&icc);

        if (icc.m1 != t->m1 || icc.mL != t->mL ||
            icc.l1_words != t->l1w || icc.llc_words != t->llcw)
        {
            Panic("I (gen) CacheLine stats != m1 %d, mL %d, l1w %d, llcw %d",
                   t->m1, t->mL, t->l1w, t->llcw);
        }
    }

    printf("*** Caches test - OK.\n");
}

void test_word_usage()
{
    cache_t I1c = {.assoc = 2, .line_size = 64, .size = 64 * 128};
    cache_t D1c = {.assoc = 2, .line_size = 64, .size = 64 * 2};
    cache_t LLc = {.assoc = 2, .line_size = 64, .size = 64 * 128};

    cachesim_initcaches(I1c, D1c, LLc);
    sprintf(I1.desc_line, "I1");
    sprintf(D1.desc_line, "D1");
    sprintf(LL.desc_line, "LL");

    printf("*** Check word usage...\n");
    CacheCC icc = {};
    for (int i = 0; i < I1c.size * 64; i+= 8) {
        //printf("## I1 (gen): Access even words %d, size %d\n", i, 4);
        cachesim_I1_doref_Gen(i, 4, &icc);
    }
    dump_CacheCC(&icc);
    /* avg of l1 will not be %50 as each new cache line starts empty (1 words) */
    float avg_usage_l1 = (icc.l1_words * 1.0/ icc.a) * 4 * 100/ I1c.size;
    float avg_usage_llc = (icc.llc_words * 1.0/ icc.a) * 4 * 100/ LLc.size;
    printf("## I1 (gen): L1 Average usage: %.2f LL Average usage: %.2f\n", avg_usage_l1, avg_usage_llc);
    if (avg_usage_l1 < 49 || avg_usage_l1 > 50) {
        Panic("test_word_usage: expected usage percent %%49-50 got %%%.2f\n", avg_usage_l1);
    }

    CacheCC ccc = {};
    cachesim_I1_doref_Gen(0, 4, &ccc);
    int even = I1c.size / 2 / 4;
    int even_line = 64 / 4/ 2;
    if (ccc.l1_words < even - even_line || ccc.l1_words > even) {
        Panic("test_word_usage: expected even words %d-%d found %llu\n",
            even-even_line, even, ccc.l1_words);
    }
    dump_CacheCC(&ccc);
    printf("*** Check word usage - OK.\n");
}

void test_array()
{
    cache_t I1c = {.assoc = 1, .line_size = 32, .size = 2 * 32};
    cache_t D1c = {.assoc = 1, .line_size = 32, .size = 2 * 32};
    cache_t LLc = {.assoc = 1, .line_size = 32, .size = 2 * 32};

    cachesim_initcaches(I1c, D1c, LLc);
    sprintf(I1.desc_line, "I1");
    sprintf(D1.desc_line, "D1");
    sprintf(LL.desc_line, "LL");
    dump_cache_t2(&D1);
    dump_cache_t2(&LL);

    printf("*** Check array pattern...\n");
    CacheCC icc = {};
    for (int i = 4; i < 64 * 3200; i+= 4) {
        //printf("## I1 (gen): Access even words %d, size %d\n", i, 4);
        cachesim_I1_doref_Gen(i, 12, &icc);
        cachesim_D1_doref(1000000+i, 12, &icc);
    }
    dump_CacheCC(&icc);
    printf("array counts: a %llu wl1 %llu wllc %llu l1u %%%.2f llcu %%%.2f\n",
        icc.a, icc.l1_words, icc.llc_words, icc.l1_words * 400.0 / icc.a / I1.size, icc.llc_words * 400.0 / icc.a / LL.size);
}

int main(int argc, char **argv)
{
    test_count_bits();
    test_set_used();
    test_caches();
    test_word_usage();
    test_array();
    return 0;
}
