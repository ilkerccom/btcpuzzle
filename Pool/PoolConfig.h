// Configuration management for VanitySearch Pool Mode for btcpuzzle.info

#ifndef POOL_CONFIG_H
#define POOL_CONFIG_H
#define POOL_VERSION "v1.0.0"

#include <string>

class PoolConfig {
public:
    // API Configuration
    std::string apiUrl = "https://api.btcpuzzle.info";
    std::string userToken;
    std::string workerName;
    std::string gpuName;
    int targetPuzzle = 71;

    // GPU Settings
    int gpuIndex = 0;

    // Security Settings
    bool untrustedComputer = false;
    std::string publicKeyString;

    // Telegram Notifications
    bool telegramShare = false;
    std::string telegramToken;
    std::string telegramChatId;

    // API Share (custom webhook)
    bool apiShare = false;
    std::string apiShareUrl;

    // Other Settings
    std::string customRange = "none";
    std::string securityHash = "";

    // Application code hash
    std::string appCodeHash = "";

    // Methods
    static PoolConfig loadFromFile(const std::string& filepath);
    bool validate(std::string& error) const;
    std::string getSelfHash();
    std::string sha256(const std::string& input);

private:
    static std::string trim(const std::string& str);
    static bool parseBool(const std::string& value);
};

#endif // POOL_CONFIG_H