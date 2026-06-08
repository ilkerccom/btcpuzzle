#include "RangeMath.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if(!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while(0)

static bool eq(Int a, Int b) { return a.IsEqual(&b); }  // by value -> safe to call non-const

int main() {
    // start=0x100, end=0x140, N=4 -> diff=0x10
    Int start; start.SetBase16((char*)"100");
    Int end;   end.SetBase16((char*)"140");
    Int diff = RangeMath::perThreadDiff(start, end, 4);
    Int expDiff; expDiff.SetBase16((char*)"10");
    CHECK(eq(diff, expDiff));

    // thread starts: 0x100, 0x110, 0x120, 0x130
    const char* exp[4] = { "100", "110", "120", "130" };
    for (int i = 0; i < 4; i++) {
        Int s = RangeMath::threadStart(start, diff, i);
        Int e; e.SetBase16((char*)exp[i]);
        CHECK(eq(s, e));
    }

    // coverage with offset=0x6: [s_i, s_i+6) U [s_i+6, s_{i+1}) tiles range, no gaps
    Int offset; offset.SetBase16((char*)"6");
    for (int i = 0; i < 4; i++) {
        Int s     = RangeMath::threadStart(start, diff, i);
        Int sNext = RangeMath::threadStart(start, diff, i + 1);
        Int mid(&s); mid.Add(&offset);          // s_i + offset
        CHECK(mid.IsGreater(&s));                // pre-crash part non-empty
        CHECK(sNext.IsGreater(&mid));            // post part non-empty (offset < diff)
        Int chk(&s); chk.Add(&diff);            // s_i + diff == s_{i+1} (contiguous)
        CHECK(eq(chk, sNext));
    }

    // offsetFromCount: 0x40 keys over N=4 -> 0x10 per thread; floors 0x41 -> 0x10
    Int e10; e10.SetBase16((char*)"10");
    CHECK(eq(RangeMath::offsetFromCount(0x40, 4), e10));
    CHECK(eq(RangeMath::offsetFromCount(0x41, 4), e10));

    // guard: non-positive thread count returns zero (no division by zero)
    Int zero; zero.SetInt32(0);
    CHECK(eq(RangeMath::perThreadDiff(start, end, 0), zero));
    CHECK(eq(RangeMath::offsetFromCount(0x40, 0), zero));

    if (failures == 0) { printf("ALL RANGEMATH TESTS PASSED\n"); return 0; }
    printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
