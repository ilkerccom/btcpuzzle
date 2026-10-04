#include "PoolClient.h"
#include "Logger.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

extern void logToFile(int gpuIndex, const std::string& msg);

namespace {
constexpr const char* kPuzzle71Address = "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU";

std::string winnerFileName() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    std::ostringstream ss;
    ss << "WINNER_P71_" << micros << ".enc";
    return ss.str();
}

#ifndef _WIN32
bool fsyncRetryEintr(int fd) {
    while (true) {
        if (::fsync(fd) == 0) return true;
        if (errno == EINTR) continue;
        return false;
    }
}
#endif

// Encrypt only the high-value winner path with OpenSSL's modern EVP interface.
// OAEP uses SHA-256 for both the OAEP digest and MGF1 digest. Any failure returns
// an empty string so the caller can fail closed without persisting plaintext.
std::string encryptWinnerOaepSha256(const std::string& publicKeyPem,
                                    const std::string& plaintext) {
    BIO* bio = BIO_new_mem_buf(publicKeyPem.data(), static_cast<int>(publicKeyPem.size()));
    if (bio == nullptr) return {};

    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (pkey == nullptr) return {};

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (ctx == nullptr) {
        EVP_PKEY_free(pkey);
        return {};
    }

    bool ok = EVP_PKEY_encrypt_init(ctx) > 0 &&
              EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) > 0 &&
              EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) > 0 &&
              EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, EVP_sha256()) > 0;

    size_t outLen = 0;
    if (ok) {
        ok = EVP_PKEY_encrypt(
                 ctx,
                 nullptr,
                 &outLen,
                 reinterpret_cast<const unsigned char*>(plaintext.data()),
                 plaintext.size()) > 0;
    }

    std::vector<unsigned char> out;
    if (ok && outLen > 0) {
        out.resize(outLen);
        ok = EVP_PKEY_encrypt(
                 ctx,
                 out.data(),
                 &outLen,
                 reinterpret_cast<const unsigned char*>(plaintext.data()),
                 plaintext.size()) > 0;
    } else {
        ok = false;
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    if (!ok) return {};
    return std::string(reinterpret_cast<const char*>(out.data()), outLen);
}

bool writeEncryptedWinner(const std::string& filename,
                          const std::string& address,
                          const std::string& ciphertext) {
    const std::string payload =
        std::string("BTCPUZZLE-SECURE-V1\n") +
        "puzzle=71\n" +
        "address=" + address + "\n" +
        "encryption=RSA-OAEP-SHA256-MGF1-SHA256\n" +
        "ciphertext_base64=" + ciphertext + "\n";

#ifdef _WIN32
    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    out.flush();
    return out.good();
#else
    const int fd = ::open(filename.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;

    size_t written = 0;
    while (written < payload.size()) {
        const ssize_t n = ::write(fd, payload.data() + written, payload.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        written += static_cast<size_t>(n);
    }

    const bool fileSynced = fsyncRetryEintr(fd);
    const bool fileClosed = (::close(fd) == 0);
    if (!fileSynced || !fileClosed) return false;

#ifdef __linux__
    // fsync() on the file persists its contents, but a newly-created filename
    // also lives in the containing directory. Sync the current directory before
    // declaring success so an abrupt power loss cannot leave a durable payload
    // without a durable directory entry.
    const int dirFd = ::open(".", O_RDONLY | O_DIRECTORY);
    if (dirFd < 0) return false;
    const bool dirSynced = fsyncRetryEintr(dirFd);
    const bool dirClosed = (::close(dirFd) == 0);
    return dirSynced && dirClosed;
#else
    return true;
#endif
#endif
}

void hardenProcess() {
#ifndef _WIN32
    // Files created from this point default to owner-only permissions.
    ::umask(0077);

    // Never leave a private key behind in a crash/core dump.
    struct rlimit coreLimit;
    coreLimit.rlim_cur = 0;
    coreLimit.rlim_max = 0;
    ::setrlimit(RLIMIT_CORE, &coreLimit);

#ifdef __linux__
    // Prevent ptrace/core-style inspection by unrelated same-user processes.
    ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
#endif
}

[[noreturn]] void secureExit(int code) {
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(code);
}
} // namespace

bool validateSecurePuzzle71Range(RangeData& result) {
    if (result.targetAddress != kPuzzle71Address) {
        result.success = false;
        result.error = "SECURITY: API targetAddress does not match hardcoded Puzzle 71 address";
        return false;
    }

    if (std::find(result.proofOfWorkAddresses.begin(),
                  result.proofOfWorkAddresses.end(),
                  kPuzzle71Address) != result.proofOfWorkAddresses.end()) {
        result.success = false;
        result.error = "SECURITY: Puzzle 71 target appeared in proofOfWorkAddresses";
        return false;
    }

    return true;
}

bool PoolClient::init() {
    hardenProcess();

    if (!upstream_init()) return false;

    // Hardened v1 is intentionally scoped to Puzzle 71 only.
    if (config.targetPuzzle != 71) {
        logMessage(DANGER, "[SECURITY] btcpuzzle-secure v1 only supports Puzzle 71");
        return false;
    }

    // A real winner must be encrypted locally before touching disk.
    if (!config.untrustedComputer || config.publicKeyString.empty() || publicKey == nullptr) {
        logMessage(DANGER, "[SECURITY] Puzzle 71 secure mode requires a valid RSA public key and untrusted_computer=true");
        return false;
    }

    // Winner secrets must never be sent to third-party notification endpoints
    // or stored in the btcpuzzle.info account by this hardened build.
    if (config.telegramShare || config.apiShare || config.saveKeyToBtcPuzzle) {
        logMessage(DANGER, "[SECURITY] Disable telegram_share, api_share and save_key for btcpuzzle-secure");
        return false;
    }

    logMessage(SUCCESS, "[SECURITY] Puzzle 71 hardened mode active");
    logMessage(SUCCESS, "[SECURITY] Winner key: local EVP RSA-OAEP-SHA256 encryption only, no network sharing");
    return true;
}

RangeData PoolClient::getRange(int gpuIndex) {
    RangeData result = upstream_getRange(gpuIndex);
    if (!result.success) return result;

    if (!validateSecurePuzzle71Range(result)) {
        logToFile(config.gpuIndex, std::string("SECURITY getRange(): ") + result.error);
    }

    return result;
}

void PoolClient::onKeyFound(const std::string& address, const std::string& privateKey) {
    if (address != kPuzzle71Address) {
        upstream_onKeyFound(address, privateKey);
        return;
    }

    // From this point forward, do not call any upstream target notification
    // function. Preserve the winner first; secureExit() below terminates every
    // process thread, including any in-flight ping, without waiting on network I/O.
    if (publicKey == nullptr || config.publicKeyString.empty()) {
        logToFile(config.gpuIndex, "SECURITY WINNER DETECTED but encryption key is unavailable; no secret written or transmitted");
        logMessage(DANGER, "[SECURITY] TARGET FOUND but local encryption is unavailable. Process terminating; plaintext was NOT saved.");
        secureExit(111);
    }

    const std::string encryptedRaw = encryptWinnerOaepSha256(config.publicKeyString, privateKey);
    if (encryptedRaw.empty()) {
        logToFile(config.gpuIndex, "SECURITY WINNER DETECTED but EVP OAEP-SHA256 encryption failed; no secret written or transmitted");
        logMessage(DANGER, "[SECURITY] TARGET FOUND but EVP OAEP-SHA256 encryption failed. Process terminating; plaintext was NOT saved.");
        secureExit(112);
    }

    const std::string encrypted = base64Encode(
        reinterpret_cast<const unsigned char*>(encryptedRaw.data()), encryptedRaw.size());

    if (encrypted.empty() || encrypted == privateKey) {
        logToFile(config.gpuIndex, "SECURITY WINNER DETECTED but ciphertext encoding failed; no secret written or transmitted");
        logMessage(DANGER, "[SECURITY] TARGET FOUND but ciphertext encoding failed. Process terminating; plaintext was NOT saved.");
        secureExit(112);
    }

    const std::string filename = winnerFileName();
    if (!writeEncryptedWinner(filename, address, encrypted)) {
        logToFile(config.gpuIndex, "SECURITY WINNER DETECTED but encrypted winner file could not be durably created; no secret transmitted");
        logMessage(DANGER, "[SECURITY] TARGET FOUND but encrypted winner file could not be durably created. Process terminating.");
        secureExit(113);
    }

    // Intentionally log only the event and public address. Never the ciphertext
    // and never the plaintext private key.
    logToFile(config.gpuIndex, std::string("SECURITY TARGET FOUND: ") + address + " | EVP OAEP-SHA256 encrypted locally | file+directory fsynced | process terminating");

    std::cout << "\n========================================\n";
    std::cout << "[SECURITY] PUZZLE 71 TARGET FOUND\n";
    std::cout << "[SECURITY] Winner secret encrypted locally with RSA-OAEP-SHA256.\n";
    std::cout << "[SECURITY] Saved and durably fsynced (file + directory) to: " << filename << "\n";
    std::cout << "[SECURITY] Process terminating immediately.\n";
    std::cout << "========================================\n";

    // Immediate process termination ensures the unsafe upstream target branch in
    // main.cpp is never reached after this callback returns and avoids waiting
    // for ping/network threads before winner persistence.
    secureExit(0);
}
