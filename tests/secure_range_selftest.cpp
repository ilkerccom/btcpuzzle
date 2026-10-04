#include "Pool/PoolClient.h"

#include <iostream>
#include <string>

namespace {
constexpr const char* kPuzzle71Address = "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU";

bool expect(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "PASS: " << name << "\n";
        return true;
    }
    std::cout << "FAIL: " << name << "\n";
    return false;
}
} // namespace

int main() {
    bool ok = true;

    RangeData valid{};
    valid.success = true;
    valid.hex = "SELFTEST-RANGE";
    valid.targetAddress = kPuzzle71Address;
    valid.proofOfWorkAddresses = {
        "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH"
    };
    ok &= expect(validateSecurePuzzle71Range(valid) && valid.success,
                 "valid Puzzle 71 range accepted");

    RangeData targetMismatch{};
    targetMismatch.success = true;
    targetMismatch.hex = "SELFTEST-TARGET-MISMATCH";
    targetMismatch.targetAddress = "1FakeTargetAddressForOfflineSelfTest";
    targetMismatch.proofOfWorkAddresses = {
        "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH"
    };
    const bool mismatchAccepted = validateSecurePuzzle71Range(targetMismatch);
    ok &= expect(!mismatchAccepted && !targetMismatch.success &&
                     targetMismatch.error ==
                         "SECURITY: API targetAddress does not match hardcoded Puzzle 71 address",
                 "malicious targetAddress rejected fail-closed");

    RangeData proofInjection{};
    proofInjection.success = true;
    proofInjection.hex = "SELFTEST-PROOF-INJECTION";
    proofInjection.targetAddress = kPuzzle71Address;
    proofInjection.proofOfWorkAddresses = {
        "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH",
        kPuzzle71Address
    };
    const bool injectionAccepted = validateSecurePuzzle71Range(proofInjection);
    ok &= expect(!injectionAccepted && !proofInjection.success &&
                     proofInjection.error ==
                         "SECURITY: Puzzle 71 target appeared in proofOfWorkAddresses",
                 "Puzzle 71 proof-address injection rejected fail-closed");

    if (!ok) {
        std::cout << "SECURE_RANGE_SELFTEST=FAIL\n";
        return 1;
    }

    std::cout << "SECURE_RANGE_SELFTEST=PASS\n";
    return 0;
}
