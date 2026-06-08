#include "RangeMath.h"

namespace RangeMath {

Int perThreadDiff(Int& start, Int& end, int numThreads) {
    if (numThreads <= 0) { Int z; z.SetInt32(0); return z; }
    Int diff(&end);
    if (diff.IsOdd()) diff.AddOne();
    diff.Sub(&start);
    Int n; n.SetInt32((uint32_t)numThreads);
    diff.Div(&n);
    return diff;
}

Int threadStart(Int& start, Int& diff, int idx) {
    Int d(&diff);
    Int k; k.SetInt32((uint32_t)idx);
    d.Mult(&k);          // d = diff * idx
    Int s(&start);
    s.Add(&d);           // s = start + diff*idx
    return s;
}

Int offsetFromCount(uint64_t totalCount, int numThreads) {
    if (numThreads <= 0) { Int z; z.SetInt32(0); return z; }
    Int c(totalCount);   // Int(uint64_t)
    Int n; n.SetInt32((uint32_t)numThreads);
    c.Div(&n);
    return c;
}

} // namespace RangeMath
