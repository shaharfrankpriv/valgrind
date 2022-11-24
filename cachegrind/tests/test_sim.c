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
        UWord a;    // Address
        Int s;      // Size
        Int l;      // Cache Line Size
        char *b;    // Bitmap
        Int r;      // expected Result
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
    for (int i = 0; i < c->sets; i++) {
        int o = 0;
        const int words = 64 / 4 + 1;
        char buf[128];

        for (int j = 0; j < c->assoc; j++) {
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

void test_caches() {
    struct Test
    {
        UWord a;    // Address
        Int s;      // Size
        Int eml1;   // expected miss m1
        Int emll;   // expected miss llc
        Int ewl1;   // expected used words l1
        Int ewll;   // expected used words m1
    } itests[] = {
        {.a = 1, .s = 4, .eml1 = 1, .emll = 1, .ewl1 = 1, .ewll = 1},
        {.a = 8, .s = 4, .eml1 = 1, .emll = 1, .ewl1 = 1, .ewll = 1},
        {.a = 32, .s = 4, .eml1 = 1, .emll = 1, .ewl1 = 1, .ewll = 1},
        {.a = 2*64+32, .s = 4, .eml1 = 2, .emll = 2, .ewl1 = 5, .ewll = 2},
        {.a = 2*64+12, .s = 4, .eml1 = 2, .emll = 2, .ewl1 = 5, .ewll = 2},
        {.a = 48, .s = 4, .eml1 = 3, .emll = 2, .ewl1 = 7, .ewll = 2},
        {}
    };

    struct Test dtests[] = {
        {.a = 0, .s = 8, .eml1 = 1, .emll = 0, .ewl1 = 1, .ewll = 0},
        {.a = 60, .s = 8, .eml1 = 2, .emll = 1, .ewl1 = 2, .ewll = 1},
        {}
    };

    cache_t I1c = {.assoc = 1, .line_size = 64, .size = 64*2};
    cache_t D1c = {.assoc = 1, .line_size = 64, .size = 64*2};
    cache_t LLc = {.assoc = 2, .line_size = 64, .size = 64*4};
    
    cachesim_initcaches(I1c, D1c, LLc);
    sprintf(I1.desc_line, "I1");
    sprintf(D1.desc_line, "D1");
    sprintf(LL.desc_line, "LL");

    dump_cache_t2(&I1);
    dump_cache_t2(&D1);
    dump_cache_t2(&LL);

    CacheCC icc = {};
    for (struct Test *t = itests; t->s; t++) {
        printf("## I1: Access %lu, size %d\n", t->a, t->s);
        cachesim_I1_doref_NoX(t->a, t->s, &icc);
        dump_cache_t2(&I1);
        dump_cache_t2(&LL);
        dump_CacheCC(&icc);
        if (icc.m1 != t->eml1 || icc.mL != t->emll ||
            icc.l1_words != t->ewl1 || icc.llc_words != t->ewll) {
              printf("Failed: I CacheLine stats != eml1 %d, emll %d, ewl1 %d, ewll %d\n",
                        t->eml1, t->emll, t->ewl1, t->emll);
                exit(1);
            }
    }

    CacheCC dcc = {};
    for (struct Test *t = dtests; t->s; t++) {
        printf("## D1: Access %lu, size %d\n", t->a, t->s);
        cachesim_D1_doref(t->a, t->s, &dcc);
        dump_cache_t2(&D1);
        dump_cache_t2(&LL);
        dump_CacheCC(&dcc);
        if (dcc.m1 != t->eml1 || dcc.mL != t->emll ||
            dcc.l1_words != t->ewl1 || dcc.llc_words != t->ewll) {
              printf("Failed: D CacheLine stats != eml1 %d, emll %d, ewl1 %d, ewll %d\n",
                        t->eml1, t->emll, t->ewl1, t->emll);
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
