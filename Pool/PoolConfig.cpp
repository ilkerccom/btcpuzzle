// Configuration implementation for VanitySearch Pool Mode for btcpuzzle.info

#include "PoolConfig.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <ctime>
#include "Logger.h"
#include <vector>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#endif

std::string PoolConfig::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool PoolConfig::parseBool(const std::string& value) {
    std::string v = trim(value);
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    return (v == "true" || v == "1" || v == "yes");
}

std::string PoolConfig::getSelfHash() {
    std::ifstream file;

#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    file.open(path, std::ios::binary);
#else
    file.open("/proc/self/exe", std::ios::binary);
#endif

    if (!file) return "";

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    std::vector<char> buffer(8192);
    while (file.good()) {
        file.read(buffer.data(), buffer.size());
        SHA256_Update(&ctx, buffer.data(), file.gcount());
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    char out[65];
    for (int i = 0; i < 32; i++)
        sprintf(out + i * 2, "%02x", hash[i]);
    out[64] = 0;

    return std::string(out);
}

PoolConfig PoolConfig::loadFromFile(const std::string& filepath) {
    PoolConfig config;
    std::ifstream file(filepath);

    if (!file.is_open()) {
        std::cerr << "Config file not found: " << filepath << std::endl;
        std::cerr << "Creating default configuration..." << std::endl;
        return config;
    }

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);

        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos) {
            std::cerr << "Warning: Invalid line " << lineNum << std::endl;
            continue;
        }

        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));

        // Remove quotes if present
        if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"') {
            value = value.substr(1, value.size() - 2);
        }

        // Parse configuration values
        if (key == "user_token") config.userToken = value;
        else if (key == "worker_name") config.workerName = value;
        else if (key == "target_puzzle") config.targetPuzzle = std::stoi(value);
        else if (key == "gpu_index") config.gpuIndex = std::stoi(value);
        else if (key == "untrusted_computer") config.untrustedComputer = parseBool(value);
        else if (key == "public_key") {
            std::string publicKeyString = value;

            size_t pos = 0;
            while ((pos = publicKeyString.find('|', pos)) != std::string::npos) {
                publicKeyString.replace(pos, 1, "\n");
                pos++;
            }

            pos = 0;
            while ((pos = publicKeyString.find('@', pos)) != std::string::npos) {
                publicKeyString.replace(pos, 1, "\n");
                pos++;
            }

            config.publicKeyString = publicKeyString;
        }
        else if (key == "telegram_share") config.telegramShare = parseBool(value);
        else if (key == "telegram_token") config.telegramToken = value;
        else if (key == "telegram_chat_id") config.telegramChatId = value;
        else if (key == "telegram_share_eachkey") config.telegramShareEachKey = parseBool(value);
        else if (key == "api_share") config.apiShare = parseBool(value);
        else if (key == "api_share_url") config.apiShareUrl = value;
        else if (key == "custom_range") config.customRange = value;
    }

    file.close();

    // Auto-generate worker name if empty
    if (config.workerName.empty()) {
        config.workerName = "worker" + std::to_string(time(nullptr) % 100000);
    }

    return config;
}

bool PoolConfig::validate(std::string& error) const {
    // Check required fields
    if (userToken.empty()) {
        error = "user_token is required!";
        return false;
    }

    if (userToken == "YOUR_TOKEN_HERE") {
        error = "Please set your actual user token in pool.conf";
        return false;
    }

    // Check security configuration
    if (untrustedComputer) {
        bool hasNotification = telegramShare || apiShare;
        bool hasEncryption = !publicKeyString.empty();

        if (!hasNotification && !hasEncryption) {
            error = "Untrusted computer mode requires either:\n"
                "  - Telegram share (telegram_share=true)\n"
                "  - API share (api_share=true)\n"
                "  - RSA public key (public_key_path=...)\n"
                "Otherwise, found keys will be lost!";
            return false;
        }
    }

    // Check telegram config if enabled
    if (telegramShare && (telegramToken.empty() || telegramChatId.empty())) {
        error = "Telegram share enabled but token or chat_id is missing";
        return false;
    }

    // Check API share config if enabled
    if (apiShare && apiShareUrl.empty()) {
        error = "API share enabled but api_share_url is missing";
        return false;
    }

    return true;
}

std::string PoolConfig::sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input.c_str(), input.size(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    return ss.str();
}