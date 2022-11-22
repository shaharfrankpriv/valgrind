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
#include "cg_arch.h"
#include "cg_sim.c"
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
        UWord a;
        Int s;
        Int l;
        char *b;
        Int r;
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

int main(int argc, char **argv)
{
    test_count_bits();
    test_set_used();
    return 0;
}
