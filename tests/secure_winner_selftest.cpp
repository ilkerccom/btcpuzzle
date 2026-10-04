#include "Pool/PoolClient.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
constexpr const char* kPuzzle71Address = "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU";
constexpr const char* kKnownTestPrivateKey =
    "0000000000000000000000000000000000000000000000000000000000000001";

std::string readAll(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: secure-winner-selftest <test-public-key.pem>\n";
        return 2;
    }

    const std::string publicKey = readAll(argv[1]);
    if (publicKey.empty()) {
        std::cerr << "SELFTEST: could not read test public key\n";
        return 3;
    }

    PoolConfig cfg;
    cfg.userToken = "SELFTEST-NO-NETWORK";
    cfg.workerName = "secure-winner-selftest";
    cfg.gpuName = "NO-GPU";
    cfg.targetPuzzle = 71;
    cfg.gpuIndex = 0;
    cfg.untrustedComputer = true;
    cfg.publicKeyString = publicKey;
    cfg.telegramShare = false;
    cfg.apiShare = false;
    cfg.saveKeyToBtcPuzzle = false;
    cfg.customRange = "none";
    cfg.securityHash = "SELFTEST";

    PoolClient client(cfg);
    if (!client.init()) {
        std::cerr << "SELFTEST: secure client init failed\n";
        return 4;
    }

    std::cout << "SELFTEST: injecting known non-secret test key into secure winner path\n";
    std::cout.flush();

    // This intentionally simulates only the callback that would occur after the
    // solver has already verified a matching target. It does not request a pool
    // range, does not start pinging, and does not contact btcpuzzle.info.
    // PoolClient::onKeyFound() is expected to encrypt, write the winner file,
    // and terminate the process with exit status 0 via secureExit().
    client.onKeyFound(kPuzzle71Address, kKnownTestPrivateKey);

    std::cerr << "SELFTEST: ERROR secure winner handler unexpectedly returned\n";
    return 5;
}
