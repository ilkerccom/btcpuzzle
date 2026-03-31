// Pool API Client Implementation for btcpuzzle.info

#include "PoolClient.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include "Logger.h"
#include <fstream>
#include <vector>
#include <mutex>

std::mutex logMutex;
std::mutex pingMutex;

void logToFile(int gpuIndex, const std::string& msg) {
	std::lock_guard<std::mutex> lock(logMutex);
	std::ofstream f("poolclient.log", std::ios::app);
	if (!f.is_open()) return;
	auto now = std::time(nullptr);
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
	f << "[" << buf << "][GPU" << gpuIndex << "] " << msg << "\n";
}

PoolClient::PoolClient(const PoolConfig& cfg)
	: config(cfg), curl(nullptr), publicKey(nullptr),
	rangesScanned(0), keysFound(0), shouldPing(false) {}

PoolClient::~PoolClient() {

	stopPing();

	if (curl) {
		curl_easy_cleanup(curl);
	}
	if (publicKey) {
		RSA_free(publicKey);
	}
	curl_global_cleanup();
}

bool PoolClient::init() {
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();

	if (!curl) {
		logMessage(DANGER, "Failed to initialize CURL");
		logToFile(config.gpuIndex, "ERROR init(): Failed to initialize CURL");
		return false;
	}

	// Load public key if untrusted mode
	if (config.untrustedComputer && !config.publicKeyString.empty()) {
		if (!loadPublicKeyFromString()) {
			return false;
		}
	}

	logMessage(INFO, "The btcpuzzle.info client has been initialized.");
	std::cout << "========================================" << std::endl;
	std::cout << "Client               => Btcpuzzle.info Client " << POOL_VERSION << std::endl;
	std::cout << "Worker               => " << config.workerName << std::endl;
	std::cout << "Puzzle               => Puzzle " << config.targetPuzzle << std::endl;
	std::cout << "App Code Hash        => " << config.appCodeHash << std::endl;
	std::cout << "Security Hash        => " << config.securityHash << std::endl;
	std::cout << "GPU                  => " << config.gpuName << std::endl;
	std::cout << "Custom Range         => " << config.customRange << std::endl;
	std::cout << "========================================" << std::endl;
	if (config.untrustedComputer) {
		logMessage(SUCCESS, "Untrusted Computer   => Activated");

		std::string encryptedTest = encryptData("Hello from Btcpuzzle.info! Good luck on puzzles!");
		logMessage(SUCCESS, ("Example Encryption   => " + encryptedTest).c_str());
	}
	else {
		logMessage(WARNING, "Untrusted Computer   => Disabled");
	}
	if (config.apiShare) {
		logMessage(SUCCESS, ("API Share            => Activated: " + config.apiShareUrl).c_str());
	}
	else {
		logMessage(WARNING, "API Share            => Disabled");
	}
	if (config.telegramShare) {
		logMessage(SUCCESS, ("Telegram Share       => Activated: " + config.telegramChatId).c_str());
	}
	else {
		logMessage(WARNING, "Telegram Share       => Disabled");
	}
	if (config.saveKeyToBtcPuzzle) {
		logMessage(INFO, "Save Key To Account  => Enabled");
	}
	else {
		logMessage(WARNING, "Save Key To Account  => Disabled");
	}
	std::cout << "========================================" << std::endl;

	// Pool client init
	logToFile(config.gpuIndex, "Btcpuzzle.info client init() " + config.workerName);

	return true;
}

bool PoolClient::loadPublicKeyFromString() {
	if (config.publicKeyString.empty()) {
		logMessage(DANGER, "No public key provided");
		logToFile(config.gpuIndex, "ERROR loadPublicKeyFromString(): No public key provided");
		return false;
	}

	// Create BIO from string
	BIO* bio = BIO_new_mem_buf(config.publicKeyString.c_str(), -1);
	if (!bio) {
		std::cerr << "Failed to create BIO from public key\n";
		logToFile(config.gpuIndex, "ERROR loadPublicKeyFromString(): Failed to create BIO from public key");
		return false;
	}

	// Read RSA public key
	publicKey = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
	BIO_free(bio);

	if (!publicKey) {
		logMessage(DANGER, "Failed to parse public key");
		logMessage(DANGER, "Untrusted mode requires valid public key.");
		logMessage(DANGER, "Make sure it starts with: -----BEGIN PUBLIC KEY-----");
		logToFile(config.gpuIndex, "ERROR loadPublicKeyFromString(): Failed to parse public key (PEM_read_bio_RSA_PUBKEY returned null)");
		std::cerr << "\n" + config.publicKeyString + "\n\n";
		return false;
	}

	logMessage(SUCCESS, "[OK] Public key loaded for encryption");
	return true;
}

// Base64 encode function
std::string PoolClient::base64Encode(const unsigned char* data, size_t len) {
	BIO* bio, * b64;
	BUF_MEM* bufferPtr;

	b64 = BIO_new(BIO_f_base64());
	bio = BIO_new(BIO_s_mem());
	bio = BIO_push(b64, bio);

	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
	BIO_write(bio, data, len);
	BIO_flush(bio);
	BIO_get_mem_ptr(bio, &bufferPtr);

	std::string result(bufferPtr->data, bufferPtr->length);
	BIO_free_all(bio);

	return result;
}

// Encrypt data with public key
std::string PoolClient::encryptData(const std::string& data) {
	if (!publicKey) {
		// No encryption if no public key
		return data;
	}

	int rsaLen = RSA_size(publicKey);
	std::vector<unsigned char> encrypted(rsaLen);

	// Encrypt with RSA public key
	int result = RSA_public_encrypt(
		data.length(),
		(unsigned char*)data.c_str(),
		encrypted.data(),
		publicKey,
		RSA_PKCS1_OAEP_PADDING
	);

	if (result == -1) {
		std::cerr << "Encryption failed\n";
		logToFile(config.gpuIndex, "ERROR encryptData(): RSA_public_encrypt failed (result == -1)");
		return "";
	}

	// Convert to base64
	return base64Encode(encrypted.data(), result);
}

std::string PoolClient::httpGet(const std::string& url,
	const std::map<std::string, std::string>& headers) {
	CURL* curl = curl_easy_init();
	std::string response;

	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

		// Header ekle
		struct curl_slist* chunk = NULL;
		for (const auto& h : headers) {
			std::string line = h.first + ": " + h.second;
			chunk = curl_slist_append(chunk, line.c_str());
		}
		if (chunk) {
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
		}

		CURLcode res = curl_easy_perform(curl);

		if (chunk) curl_slist_free_all(chunk);
		curl_easy_cleanup(curl);

		if (res != CURLE_OK) {
			printf("CURL error: %s\n", curl_easy_strerror(res));
			logToFile(config.gpuIndex, std::string("ERROR httpGet(") + url + "): " + curl_easy_strerror(res));
			return "";
		}
	}

	return response;
}

std::string PoolClient::httpPost(const std::string& url,
	const std::string& data,
	const std::map<std::string, std::string>& headers) {
	std::string response;
	if (!curl) return response;

	curl_easy_reset(curl);

	struct curl_slist* chunk = NULL;

	// Header ekle
	for (const auto& h : headers) {
		std::string line = h.first + ": " + h.second;
		chunk = curl_slist_append(chunk, line.c_str());
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	CURLcode res = curl_easy_perform(curl);

	if (chunk) curl_slist_free_all(chunk);

	if (res != CURLE_OK) {
		printf("CURL error: %s\n", curl_easy_strerror(res));
		logToFile(config.gpuIndex, std::string("ERROR httpPost(") + url + "): " + curl_easy_strerror(res));
		return "";
	}

	return response;
}

std::string PoolClient::httpPut(const std::string& url,
	const std::string& data,
	const std::map<std::string, std::string>& headers,
	long& httpCode) {

	std::string response;
	if (!curl) return response;

	struct curl_slist* chunk = NULL;

	// Header ekle
	for (const auto& h : headers) {
		std::string line = h.first + ": " + h.second;
		chunk = curl_slist_append(chunk, line.c_str());
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

	// PUT ayarı
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	CURLcode res = curl_easy_perform(curl);

	if (chunk) curl_slist_free_all(chunk);

	if (res != CURLE_OK) {
		httpCode = 0;
		printf("CURL error: %s\n", curl_easy_strerror(res));
		logToFile(config.gpuIndex, std::string("ERROR httpPut(") + url + "): " + curl_easy_strerror(res));
		return "";
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	return response;
}

std::string PoolClient::extractJsonValue(const std::string& json, const std::string& key) {
	std::string searchKey = "\"" + key + "\":";
	size_t pos = json.find(searchKey);
	if (pos == std::string::npos) return "";

	pos += searchKey.length();
	while (pos < json.length() && isspace(json[pos])) pos++;

	if (json[pos] == '"') {
		pos++;
		size_t endPos = json.find('"', pos);
		if (endPos == std::string::npos) return "";
		return json.substr(pos, endPos - pos);
	}
	else {
		size_t endPos = pos;
		while (endPos < json.length() &&
			(isalnum(json[endPos]) || json[endPos] == '.' || json[endPos] == '-')) {
			endPos++;
		}
		return json.substr(pos, endPos - pos);
	}
}

std::vector<std::string> PoolClient::extractJsonArray(const std::string& json, const std::string& key) {
	std::vector<std::string> result;
	std::string searchKey = "\"" + key + "\":";

	size_t pos = json.find(searchKey);
	if (pos == std::string::npos) return result;

	size_t arrayStart = json.find('[', pos);
	size_t arrayEnd = json.find(']', arrayStart);

	if (arrayStart == std::string::npos || arrayEnd == std::string::npos) return result;

	std::string arrayContent = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

	std::stringstream ss(arrayContent);
	std::string item;
	while (std::getline(ss, item, ',')) {
		size_t start = item.find('"');
		size_t end = item.rfind('"');
		if (start != std::string::npos && end != std::string::npos && start < end) {
			result.push_back(item.substr(start + 1, end - start - 1));
		}
	}

	return result;
}

std::string PoolClient::calculateProofHash(const std::vector<std::string>& keys) {
	// Concatenate all keys
	std::string concatenated;
	for (const auto& key : keys) {
		concatenated += key;
	}

	// Calculate SHA256
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, concatenated.c_str(), concatenated.length());
	SHA256_Final(hash, &sha256);

	// Convert to hex string
	std::stringstream ss;
	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
	}

	return ss.str();
}

RangeData PoolClient::getRange(int gpuIndex) {
	RangeData result;
	result.success = false;

	// Build API URL
	std::stringstream urlStream;
	urlStream << "https://api.btcpuzzle.info"
		<< "/puzzle/" << config.targetPuzzle
		<< "/range";

	std::map<std::string, std::string> headers;
	headers["UserToken"] = config.userToken;
	headers["SecurityHash"] = config.securityHash;
	headers["WorkerName"] = config.workerName;
	headers["CustomRange"] = config.customRange;
	headers["GPUName"] = config.gpuName;

	std::string response = httpGet(urlStream.str(), headers);

	if (response.empty()) {
		result.error = "No response from API";
		logToFile(config.gpuIndex, "ERROR getRange(): No response from API | URL: " + urlStream.str());
		return result;
	}

	// Parse JSON
	result.hex = extractJsonValue(response, "hex");
	result.targetAddress = extractJsonValue(response, "targetAddress");
	result.rangeStart = extractJsonValue(response, "workloadStart");
	result.rangeEnd = extractJsonValue(response, "workloadEnd");
	result.proofOfWorkAddresses = extractJsonArray(response, "proofOfWorkAddresses");

	result.success = !result.hex.empty() && !result.targetAddress.empty();

	if (!result.success) {
		result.error = extractJsonValue(response, "error");
		if (result.error.empty()) {
			result.error = "Invalid API response";
		}
		logToFile(config.gpuIndex, "ERROR getRange(): " + result.error + " | Response: " + response);
	}

	return result;
}

bool PoolClient::submitRange(const std::string& hex, const std::vector<std::string>& proofKeys) {

	std::string proofHash = calculateProofHash(proofKeys);

	std::string url = "https://api.btcpuzzle.info/puzzle/" + std::to_string(config.targetPuzzle) + "/range";

	std::map<std::string, std::string> headers;

	headers["HEX"] = hex;
	headers["WorkerName"] = config.workerName;
	headers["GPUName"] = config.gpuName;
	headers["HashedProofKey"] = proofHash;
	headers["UserToken"] = config.userToken;
	headers["GPUCount"] = "1";

	long httpCode = 0;
	std::string response = httpPut(url, "", headers, httpCode);

	if (response.empty()) {
		logToFile(config.gpuIndex, "ERROR submitRange(hex=" + hex + "): Empty response from API");
		return false;
	}

	if (httpCode != 200) {
		logToFile(config.gpuIndex, "ERROR submitRange(hex=" + hex + "): HTTP " + std::to_string(httpCode) + " | Response: " + response);
	}

	if (httpCode == 200) {
		rangesScanned++;
	}

	return true;
}

bool PoolClient::submitKey(const std::string& encryptedKey) {

	std::string url = "https://api.btcpuzzle.info/puzzle/" + std::to_string(config.targetPuzzle) + "/save-private-key";

	std::map<std::string, std::string> headers;

	headers["Content-Type"] = "application/json";
	headers["Accept"] = "application/json";
	headers["UserToken"] = config.userToken;

	std::string body = "{\"name\":\"" + config.workerName + "\",\"encryptedKey\":\"" + encryptedKey + "\"}";

	std::string response = httpPost(url, body, headers);

	if (response.empty()) {
		logToFile(config.gpuIndex, "ERROR submitKey(encryptedKey=" + encryptedKey + "): Empty response from API");
		return false;
	}

	return true;
}

// Also update onKeyFound to save encrypted key to file
void PoolClient::onKeyFound(const std::string& address, const std::string& privateKey) {
	FoundKey fk;
	fk.address = address;
	fk.privateKey = privateKey;
	fk.isProof = false;
	fk.isReward = false;
	fk.isTarget = false;

	foundKeys[address] = fk;
	keysFound++;

	std::cout << "[+] Key found: " << address << std::endl;

	// If this is the target address, also save to file
	// (Check this in the callback in main.cpp instead)
}

bool PoolClient::hasAllProofKeys(const RangeData& range) {
	int found = 0;
	for (const auto& addr : range.proofOfWorkAddresses) {
		if (foundKeys.find(addr) != foundKeys.end()) {
			found++;
		}
	}
	return found == (int)range.proofOfWorkAddresses.size();
}

std::vector<std::string> PoolClient::getProofKeys(const RangeData& range) {
	std::vector<std::string> proofKeys;
	for (const auto& addr : range.proofOfWorkAddresses) {
		if (foundKeys.find(addr) != foundKeys.end()) {
			proofKeys.push_back(foundKeys[addr].privateKey);
		}
	}
	return proofKeys;
}

bool PoolClient::notifyWorkerStarted() {

	std::string hash = config.securityHash;
	std::string shortedHash = hash.substr(0, 4) + "..." + hash.substr(hash.size() - 4);

	if (config.telegramShare) {
		std::string msg = config.workerName + " started job! (" + shortedHash + ")";
		bool ok = sendTelegram(msg);
		if (!ok) logToFile(config.gpuIndex, "ERROR notifyWorkerStarted(): Telegram send failed for worker " + config.workerName);
		return ok;
	}

	if (config.apiShare) {
		std::map<std::string, std::string> data;
		bool ok = sendApiShare("workerStarted", data);
		if (!ok) logToFile(config.gpuIndex, "ERROR notifyWorkerStarted(): API share send failed for worker " + config.workerName);
		return ok;
	}
	return true;
}

bool PoolClient::notifyWorkerStopped() {

	std::string hash = config.securityHash;
	std::string shortedHash = hash.substr(0, 4) + "..." + hash.substr(hash.size() - 4);

	if (config.telegramShare) {
		std::string msg = config.workerName + " went offline! (" + shortedHash + ")";
		bool ok = sendTelegram(msg);
		if (!ok) logToFile(config.gpuIndex, "ERROR notifyWorkerStopped(): Telegram send failed for worker " + config.workerName);
		return ok;
	}
	if (config.apiShare) {
		std::map<std::string, std::string> data;
		bool ok = sendApiShare("workerExited", data);
		if (!ok) logToFile(config.gpuIndex, "ERROR notifyWorkerStopped(): API share send failed for worker " + config.workerName);
		return ok;
	}
	return true;
}

bool PoolClient::notifyRangeScanned(const std::string& hex) {
	std::string hash = config.securityHash;
	std::string shortedHash = hash.substr(0, 4) + "..." + hash.substr(hash.size() - 4);

	if (config.telegramShare) {
		std::string msg = u8"✅ " + hex + " scanned by " + config.workerName + " (" + shortedHash + ")";
		bool ok = sendTelegram(msg);
		if (!ok) logToFile(config.gpuIndex, "ERROR notifyRangeScanned(hex=" + hex + "): Telegram send failed");
		return ok;
	}

	if (config.apiShare) {
		std::map<std::string, std::string> data;
		data["HEX"] = hex;
		bool ok = sendApiShare("rangeScanned", data);
		if (!ok) logToFile(config.gpuIndex, "ERROR notifyRangeScanned(hex=" + hex + "): API share send failed");
		return ok;
	}

	return true;
}

bool PoolClient::notifyTargetFound(const std::string& address, const std::string& key) {
	logMessage(SUCCESS, "\n========================================");
	logMessage(SUCCESS, u8"🎯 TARGET KEY FOUND!");
	logMessage(SUCCESS, "========================================");
	logMessage(SUCCESS, ("Address: " + address).c_str());

	std::string keyToSend = key;
	bool isEncrypted = false;

	// Encrypt if untrusted mode
	if (config.untrustedComputer && publicKey) {
		std::string encryptedKey = encryptData(key);

		if (!encryptedKey.empty()) {
			keyToSend = encryptedKey;
			isEncrypted = true;
			std::cout << "Private Key: ENCRYPTED" << std::endl;
			std::cout << "Private Key: " << keyToSend << std::endl;
		}
		else {
			std::cerr << "[ERROR] Encryption failed!" << std::endl;
			logToFile(config.gpuIndex, "ERROR notifyTargetFound(address=" + address + "): Encryption failed, sending plaintext");
			std::cout << "Private Key: " << keyToSend << std::endl;
		}
	}
	else {
		std::cout << "Private Key: " << keyToSend << std::endl;
	}

	std::cout << "========================================\n";

	// CRITICAL: Retry until successful
	bool telegramSuccess = false;
	bool apiShareSuccess = false;

	// Send to Telegram with retry
	if (config.telegramShare) {
		std::string message = u8"🎯 TARGET KEY FOUND!\n\n";
		message += "Puzzle: " + std::to_string(config.targetPuzzle) + "\n";
		message += "Worker: " + config.workerName + "\n\n";
		message += "Address:\n" + address + "\n\n";

		if (isEncrypted) {
			message += "Encrypted Private Key:\n" + keyToSend;
		}
		else {
			message += "Private Key:\n" + keyToSend;
		}

		logMessage(INFO, "Sending to Telegram...");

		int attempt = 0;
		while (!telegramSuccess) {
			attempt++;
			logMessage(INFO, ("Telegram attempt #" + std::to_string(attempt)).c_str());

			telegramSuccess = sendTelegram(message);

			if (telegramSuccess) {
				logMessage(SUCCESS, "Telegram notification sent!");
				break;
			}
			else {
				logToFile(config.gpuIndex, "ERROR notifyTargetFound(): Telegram attempt #" + std::to_string(attempt) + " failed for address " + address);
				logMessage(DANGER, "Telegram failed, retrying in 5 seconds...");
				std::this_thread::sleep_for(std::chrono::seconds(5));
			}
		}
	}

	// Send to API share with retry
	if (config.apiShare) {
		std::map<std::string, std::string> data;
		data["HEX"] = currentRangeHex;
		data["PrivateKey"] = keyToSend;
		data["Address"] = address;
		data["Encrypted"] = isEncrypted ? "true" : "false";

		logMessage(INFO, "Sending to API share...");

		int attempt = 0;
		while (!apiShareSuccess) {
			attempt++;
			logMessage(INFO, ("API share attempt #" + std::to_string(attempt)).c_str());

			apiShareSuccess = sendApiShare("keyFound", data);

			if (apiShareSuccess) {
				logMessage(SUCCESS, "API share notification sent!");
				break;
			}
			else {
				logToFile(config.gpuIndex, "ERROR notifyTargetFound(): API share attempt #" + std::to_string(attempt) + " failed for address " + address);
				logMessage(DANGER, "API share failed, retrying in 5 seconds...");
				std::this_thread::sleep_for(std::chrono::seconds(5));
			}
		}
	}

	// Save key to btcPuzzle.info account 
	// RSA public key required - (public_key=)
	// Untrusted computer mode must be enabled (untrusted_computer=true)
	// (!!!!!) The key is encrypted using the RSA Public Key provided by the user, and only the user can decrypt it using their own RSA Private Key.
	/*
		The "private key" found for the puzzle is encrypted using RSA before being sent and stored. The user can view this encrypted key in the btcpuzzle.info panel. Later, only the user can decrypt this content using their own "RSA Private Key." Remember: No one—including the btcpuzzle.info administrators—can access or decrypt this key.
	*/
	if (config.saveKeyToBtcPuzzle && config.untrustedComputer && isEncrypted) {
		submitKey(keyToSend);
	}

	return (telegramSuccess || apiShareSuccess ||
		(!config.telegramShare && !config.apiShare));
}

std::string PoolClient::trim(const std::string& str) {
	size_t start = str.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = str.find_last_not_of(" \t\r\n");
	return str.substr(start, end - start + 1);
}

bool PoolClient::sendTelegram(const std::string& message) {
	if (!config.telegramShare) return false;

	std::string escaped = message;
	auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
		size_t pos = 0;
		while ((pos = s.find(from, pos)) != std::string::npos) {
			s.replace(pos, from.size(), to);
			pos += to.size();
		}
		};
	replaceAll(escaped, "\\", "\\\\");
	replaceAll(escaped, "\"", "\\\"");
	replaceAll(escaped, "\n", "\\n");
	replaceAll(escaped, "\r", "\\r");

	std::stringstream url;
	url << "https://api.telegram.org/bot" << config.telegramToken << "/sendMessage";

	std::stringstream json;
	json << "{\"chat_id\":\"" << config.telegramChatId << "\",\"text\":\"" << escaped << "\"}";

	std::map<std::string, std::string> headers;
	headers["Content-Type"] = "application/json";

	std::string response = httpPost(url.str(), json.str(), headers);

	if (response.empty()) {
		logToFile(config.gpuIndex, "ERROR sendTelegram(): Empty response from Telegram API (chat_id=" + config.telegramChatId + ")");
	}

	return !response.empty();
}

bool PoolClient::sendApiShare(const std::string& status, const std::map<std::string, std::string>& data) {

	if (!config.apiShare || config.apiShareUrl.empty()) {
		return false;
	}

	if (!curl) {
		std::cerr << "[ERROR] CURL not initialized\n";
		logToFile(config.gpuIndex, "ERROR sendApiShare(status=" + status + "): CURL not initialized");
		return false;
	}

	// Create header list
	struct curl_slist* headers = NULL;

	// Add Status header
	headers = curl_slist_append(headers, ("Status: " + status).c_str());

	// Add data from map as headers
	for (const auto& kv : data) {
		std::string header = kv.first + ": " + kv.second;
		headers = curl_slist_append(headers, header.c_str());
	}

	// Add config headers
	headers = curl_slist_append(headers, ("Targetpuzzle: " + std::to_string(config.targetPuzzle)).c_str());
	headers = curl_slist_append(headers, ("Workername: " + config.workerName).c_str());
	headers = curl_slist_append(headers, ("Customrange: " + config.customRange).c_str());
	headers = curl_slist_append(headers, ("Securityhash: " + config.securityHash).c_str());

	// Setup CURL
	std::string response;
	curl_easy_setopt(curl, CURLOPT_URL, config.apiShareUrl.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");  // Empty body, data in headers
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

	// Perform request
	CURLcode res = curl_easy_perform(curl);
	curl_slist_free_all(headers);

	if (res != CURLE_OK) {
		std::cerr << "[ERROR] API share request failed: " << curl_easy_strerror(res) << std::endl;
		std::cerr << "URL: " << config.apiShareUrl << std::endl;
		logToFile(config.gpuIndex, std::string("ERROR sendApiShare(status=") + status + "): CURL error: " + curl_easy_strerror(res) + " | URL: " + config.apiShareUrl);
		return false;
	}

	// Check response
	std::string trimmedResponse = trim(response);
	std::transform(trimmedResponse.begin(), trimmedResponse.end(), trimmedResponse.begin(), ::tolower);

	bool isSuccess = (trimmedResponse == "true" || trimmedResponse == "1");

	if (!isSuccess) {
		std::cerr << "[ERROR] API share failed." << std::endl;
		std::cerr << "URL: " << config.apiShareUrl << std::endl;
		std::cerr << "Puzzle: " << config.targetPuzzle << std::endl;
		std::cerr << "Worker: " << config.workerName << std::endl;
		logToFile(config.gpuIndex, "ERROR sendApiShare(status=" + status + "): Server returned: '" + trimmedResponse + "' | URL: " + config.apiShareUrl + " | Worker: " + config.workerName);
	}

	return isSuccess;
}

bool PoolClient::sendPing(const std::string& hex) {
	if (hex.empty()) return false;

	// Build URL: /pool/{puzzle}/range/ping
	std::stringstream urlStream;
	urlStream << config.apiUrl << "/puzzle/"
		<< config.targetPuzzle << "/range/ping";

	// Create CURL handle (thread-safe)
	CURL* pingCurl = curl_easy_init();
	if (!pingCurl) {
		logToFile(config.gpuIndex, "ERROR sendPing(hex=" + hex + "): Failed to init CURL handle");
		return false;
	}

	// Setup headers
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, ("hex: " + hex).c_str());
	headers = curl_slist_append(headers, ("userToken: " + config.userToken).c_str());
	headers = curl_slist_append(headers, ("workerName: " + config.workerName).c_str());
	headers = curl_slist_append(headers, ("gpuName: " + config.gpuName).c_str());

	// Setup CURL for PATCH request
	std::string response;
	curl_easy_setopt(pingCurl, CURLOPT_URL, urlStream.str().c_str());
	curl_easy_setopt(pingCurl, CURLOPT_CUSTOMREQUEST, "PATCH");
	curl_easy_setopt(pingCurl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(pingCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(pingCurl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(pingCurl, CURLOPT_TIMEOUT, 10L);

	// Perform request
	CURLcode res = curl_easy_perform(pingCurl);

	// Get HTTP status code
	long httpCode = 0;
	curl_easy_getinfo(pingCurl, CURLINFO_RESPONSE_CODE, &httpCode);

	// Cleanup
	curl_slist_free_all(headers);
	curl_easy_cleanup(pingCurl);

	// Check result
	if (res == CURLE_OK && httpCode == 200) {
		return true;
	}
	else {
		logToFile(config.gpuIndex, std::string("ERROR sendPing(hex=") + hex + "): " +
			(res != CURLE_OK ? curl_easy_strerror(res) : "HTTP " + std::to_string(httpCode)));
		return false;
	}
}

void PoolClient::pingLoop() {
	logToFile(config.gpuIndex, "PING pingLoop(): Started | worker=" + config.workerName);

	while (shouldPing) {
		for (int i = 0; i < 120; i++) {
			if (!shouldPing) break;
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		std::string hexSnapshot;
		{
			std::lock_guard<std::mutex> lock(pingMutex);
			hexSnapshot = currentRangeHex;
		}

		if (shouldPing && !hexSnapshot.empty()) {
			bool ok = sendPing(hexSnapshot);
			if (!ok) {
				logToFile(config.gpuIndex, "ERROR pingLoop(): sendPing failed | worker=" + config.workerName + " | hex=" + hexSnapshot);
			}
		}
	}

	logToFile(config.gpuIndex, "PING pingLoop(): Exited | worker=" + config.workerName);
}

void PoolClient::startPing(const std::string& hex) {
	{
		std::lock_guard<std::mutex> lock(pingMutex);
		currentRangeHex = hex;
	}

	if (!shouldPing) {
		shouldPing = true;
		pingThread = std::thread(&PoolClient::pingLoop, this);

		std::cout << "[PING] Thread started for worker: " << config.workerName << std::endl;
		std::cout.flush();
		logToFile(config.gpuIndex, "PING startPing(): Thread started | worker=" + config.workerName + " | hex=" + hex);
	}
	else {
		std::cout << "[PING] Range updated to: " << hex << " for worker: " << config.workerName << std::endl;
		std::cout.flush();
		logToFile(config.gpuIndex, "PING startPing(): Range updated | worker=" + config.workerName + " | hex=" + hex);
	}
}

void PoolClient::stopPing() {
	if (shouldPing) {
		logToFile(config.gpuIndex, "PING stopPing(): Stopping | worker=" + config.workerName);

		shouldPing = false;

		if (pingThread.joinable()) {
			pingThread.join();
		}

		{
			std::lock_guard<std::mutex> lock(pingMutex);
			currentRangeHex.clear();
		}

		logToFile(config.gpuIndex, "PING stopPing(): Stopped | worker=" + config.workerName);
	}
}

void PoolClient::reset() {
	foundKeys.clear();
}