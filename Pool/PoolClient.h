// Pool API Client - handles communication with btcpuzzle.info

#ifndef POOL_CLIENT_H
#define POOL_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include "PoolConfig.h"
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

// Range data received from API
struct RangeData {
    std::string hex;
    std::string targetAddress;
    std::vector<std::string> proofOfWorkAddresses;
    std::vector<std::string> rewardAddresses;
    std::string rangeStart;
    std::string rangeEnd;
    bool success;
    std::string error;
};

// Found key information
struct FoundKey {
    std::string address;
    std::string privateKey;
    bool isProof;
    bool isReward;
    bool isTarget;
};

class PoolClient {
private:
    PoolConfig config;
    CURL* curl;
    RSA* publicKey;
    void* secureHandler;  // SecureKeyHandler* (forward declaration to avoid circular include)

    std::thread pingThread;
    std::atomic<bool> shouldPing;
    std::string currentRangeHex;

    std::map<std::string, FoundKey> foundKeys;
    int rangesScanned;
    int keysFound;
    time_t startTime;

    // HTTP request methods
    std::string httpGet(const std::string& url, const std::map<std::string, std::string>& headers = {});
    std::string httpPost(
        const std::string& url,
        const std::string& data,
        const std::map<std::string, std::string>& headers = {});
    std::string httpPut(
        const std::string& url,
        const std::string& data,
        const std::map<std::string, std::string>& headers,
        long& httpCode);

    // Simple JSON parsing (for basic operations without external library)
    std::string extractJsonValue(const std::string& json, const std::string& key);
    std::vector<std::string> extractJsonArray(const std::string& json, const std::string& key);

    // Calculate SHA256 hash of concatenated proof keys
    std::string calculateProofHash(const std::vector<std::string>& keys);

    bool loadPublicKeyFromString();
    std::string base64Encode(const unsigned char* data, size_t len);

    // Notification methods
    bool sendTelegram(const std::string& message);
    bool sendApiShare(const std::string& status, const std::map<std::string, std::string>& data);

    std::string trim(const std::string& str);

public:
    PoolClient(const PoolConfig& cfg);
    ~PoolClient();

    // Initialize client (setup CURL, load encryption keys if needed)
    bool init();

    // Get new range from pool API
    RangeData getRange(int gpuIndex);

    // Submit completed range with proof keys
    bool submitRange(const std::string& hex, const std::vector<std::string>& proofKeys);

    // Handle found key (called by VanitySearch callback)
    void onKeyFound(const std::string& address, const std::string& privateKey);

	// Submit encrypted key to pool (for untrusted computer mode, need save_key=true)
    bool submitKey(const std::string& encryptedKey);

	// Ping mechanism to keep worker active on pool
    void startPing(const std::string& hex);
    void stopPing();
    void pingLoop();
    bool sendPing(const std::string& hex);

    // Check if all proof keys for current range are found
    bool hasAllProofKeys(const RangeData& range);

    // Get list of found proof keys
    std::vector<std::string> getProofKeys(const RangeData& range);
    std::string encryptData(const std::string& data);
    

    // Send notifications
    bool notifyWorkerStarted();
    bool notifyWorkerStopped();
    bool notifyRangeScanned(const std::string& hex);
    bool notifyTargetFound(const std::string& address, const std::string& encryptedKey);

    // Reset for new range
    void reset();
};

// CURL write callback (used internally by httpGet/httpPost)
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

#endif // POOL_CLIENT_H