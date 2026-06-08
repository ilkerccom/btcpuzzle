// Pure partition arithmetic over Int (no GPU). Must stay in sync with
// VanitySearch::getGPUStartingKeysMT (Vanity.cpp).
#ifndef RANGEMATH_H
#define RANGEMATH_H

#include "Int.h"

namespace RangeMath {
    // Per-thread sub-range width: (end[+1 if odd] - start) / numThreads.
    Int perThreadDiff(Int& start, Int& end, int numThreads);
    // Start key of thread idx: start + diff*idx.
    Int threadStart(Int& start, Int& diff, int idx);
    // Per-thread scan advance derived from total device key count: count / numThreads.
    Int offsetFromCount(uint64_t totalCount, int numThreads);
}

#endif // RANGEMATH_H
