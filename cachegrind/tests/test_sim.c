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
#define vg_log2 log2
#define vg_tool_panic printf
#define vg_malloc(desc, sz) calloc(1, sz)
#include "../cg_arch.h"
#include "../cg_sim.c"
//#include "cg_branchpred.c"

/*------------------------------------------------------------*/
/*--- Constants                                            ---*/
/*------------------------------------------------------------*/

/* Set to 1 for very verbose debugging */
#define DEBUG_CG 1

char *tobin(UWord w, char *buf, int n)
{
    const int bits = sizeof(w) * 8;
    int e = bits > n - 1 ? n - 1 : bits;
    int i;
    // printf("n %d bits %d e %d\n", n, bits, e);
    for (i = 0; i < e; i++)
    {
        buf[i] = ((1 << i) & w) ? '1' : '0';
    }
    buf[i] = 0;
    // printf("n %d bits %d e %d i %d buf %s\n", n, bits, e, i, buf);
    return buf;
}

void test_count_bits()
{
    UInt u = 0x808;
    printf("count bits 0x%x - %d\n", u, count_bits(u));
    u |= 0x7000;
    printf("count bits 0x%x - %d\n", u, count_bits(u));
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
    for (int i = 0; tests[i].b; i++)
    {
        struct Test *t = tests + i;
        d = set_used(t->a, t->s, t->l, &w);
        char *s = tobin(w, buf, words);
        printf("set used: addr = %lu, size = %d, cache_line_sz = %d:  w = %s d = %d\n",
               t->a, t->s, t->l, s, d);
        if (strncmp(t->b, s, sizeof(buf)))
        {
            printf("Failed: expected %s got %s\n", t->b, s);
            exit(1);
        }
    }
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
            printf("Failed: I CacheLine stats != m1 %d, mL %d, l1w %d, llcw %d\n",
                   t->m1, t->mL, t->l1w, t->llcw);
            exit(1);
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
            printf("Failed: D CacheLine stats != m1 %d, mL %d, l1w %d, llcw %d\n",
                   t->m1, t->mL, t->l1w, t->llcw);
            exit(1);
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
            printf("Failed: I (gen) CacheLine stats != m1 %d, mL %d, l1w %d, llcw %d\n",
                   t->m1, t->mL, t->l1w, t->llcw);
            exit(1);
        }
    }

    printf("Caches test - success!\n");
}

int main(int argc, char **argv)
{
    test_count_bits();
    test_set_used();
    test_caches();
    return 0;
}
