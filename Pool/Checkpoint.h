// Checkpoint persistence for resumable scan progress (pure std C++, no GPU/CUDA).
#ifndef POOL_CHECKPOINT_H
#define POOL_CHECKPOINT_H

#include <string>
#include <vector>
#include <utility>

struct Checkpoint {
    int puzzle = 0;
    std::string hex;
    std::string rangeStart;
    std::string rangeEnd;
    std::string targetAddress;
    std::vector<std::string> proofAddresses;
    int numThreads = 0;
    std::string offsetHex;                                        // per-thread advance, hex
    std::vector<std::pair<std::string, std::string>> foundProof;  // (address, privKeyHex)
};

namespace CheckpointIO {
    // Atomic write (temp file + rename). Returns false on I/O error.
    bool save(const std::string& path, const Checkpoint& cp);
    // Returns true only if a well-formed checkpoint was read.
    bool load(const std::string& path, Checkpoint& out);
    // Remove checkpoint (and any leftover temp). No error if absent.
    void clear(const std::string& path);
}

#endif // POOL_CHECKPOINT_H
