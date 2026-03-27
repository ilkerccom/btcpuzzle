/*
 * This file is part of the VanitySearch distribution (https://github.com/JeanLucPons/VanitySearch).
 * Copyright (c) 2019 Jean Luc PONS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Timer.h"
#include "Vanity.h"
#include "SECP256k1.h"
#include <fstream>
#include <string>
#include <string.h>
#include <stdexcept>
#include "hash/sha512.h"
#include "hash/sha256.h"
#include "Pool/PoolConfig.h"
#include "Pool/PoolClient.h"
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <regex>
#include "Pool/Logger.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif


class VanitySearch;

#define RELEASE "1.17 Linux with BitCrack integration - btcpuzzle.info official client"

bool g_poolMode = true;
PoolConfig g_poolConfig;

using namespace std;

#ifdef WITH_GPU
if (gpuId.size() > 0) {
	// Get GPU name from CUDA
	cudaDeviceProp deviceProp;
	cudaGetDeviceProperties(&deviceProp, gpuId[0]);
	gpuName = std::string(deviceProp.name);
}
#endif

// ------------------------------------------------------------------------------------------

void printUsage() {

	fprintf(stdout, "VanitySearch [-check] [-u] [-b] [-stop] [-i inputfile]\n");
	fprintf(stdout, "             [-gpuId gpuId1[,gpuId2,...]] [-o outputfile] [-check] [address]\n");
	fprintf(stdout, " address: address to search\n");
	fprintf(stdout, " -u: Search uncompressed addresses\n");
	fprintf(stdout, " -b: Search both uncompressed or compressed addresses\n");
	fprintf(stdout, " -stop: Stop when all addresses are found\n");
	fprintf(stdout, " -i inputfile: Get list of addresses to search from specified file\n");
	fprintf(stdout, " -o outputfile: Output results to the specified file\n");
	fprintf(stdout, " -gpu gpuId1,gpuId2,...: List of GPU(s) to use, default is 0\n");
	fprintf(stdout, " -l: List cuda enabled devices\n");
	fprintf(stdout, " -check: Check CPU and GPU kernel vs CPU\n");
	fprintf(stdout, " --keyspace START \n");
	fprintf(stdout, "            START:END \n");
	fprintf(stdout, "            START:+COUNT \n");
	fprintf(stdout, "            :+COUNT \n");
	fprintf(stdout, "            :END \n");
	fprintf(stdout, "            Where START, END, COUNT are in hex format\n");
	exit(-1);
}

int getInt(string name, char* v) {

	int r;

	try {

		r = std::stoi(string(v));

	}
	catch (std::invalid_argument&) {

		fprintf(stderr, "[ERROR] Invalid %s argument, number expected\n", name.c_str());
		exit(-1);
	}

	return r;
}

void getInts(string name, vector<int>&tokens, const string & text, char sep) {

	size_t start = 0, end = 0;
	tokens.clear();
	int item;

	try {

		while ((end = text.find(sep, start)) != string::npos) {
			item = std::stoi(text.substr(start, end - start));
			tokens.push_back(item);
			start = end + 1;
		}

		item = std::stoi(text.substr(start));
		tokens.push_back(item);

	}
	catch (std::invalid_argument&) {

		fprintf(stderr, "[ERROR] Invalid %s argument, number expected\n", name.c_str());
		exit(-1);
	}
}

void getKeySpace(const string & text, BITCRACK_PARAM * bc, Int & maxKey)
{
	size_t start = 0, end = 0;
	string item;

	try
	{
		if ((end = text.find(':', start)) != string::npos)
		{
			item = std::string(text.substr(start, end));
			start = end + 1;
		}
		else
		{
			item = std::string(text);
		}

		if (item.length() == 0)
		{
			bc->ksStart.SetInt32(1);
		}
		else if (item.length() > 64)
		{
			fprintf(stderr, "[ERROR] keyspaceSTART: invalid privkey (64 length)\n");
			exit(-1);
		}
		else
		{
			item.insert(0, 64 - item.length(), '0');
			for (int i = 0; i < 32; i++)
			{
				unsigned char my1ch = 0;
				if (sscanf(&item[2 * i], "%02hhX", &my1ch)) {};
				bc->ksStart.SetByte(31 - i, my1ch);
			}
		}

		if (start != 0 && (end = text.find('+', start)) != string::npos)
		{
			item = std::string(text.substr(end + 1));
			if (item.length() > 64 || item.length() == 0)
			{
				fprintf(stderr, "[ERROR] keyspace__END: invalid privkey (64 length)\n");
				exit(-1);
			}

			item.insert(0, 64 - item.length(), '0');

			for (int i = 0; i < 32; i++)
			{
				unsigned char my1ch = 0;
				if (sscanf(&item[2 * i], "%02hhX", &my1ch)) {};
				bc->ksFinish.SetByte(31 - i, my1ch);
			}

			bc->ksFinish.Add(&bc->ksStart);
		}
		else if (start != 0)
		{
			item = std::string(text.substr(start));

			if (item.length() > 64 || item.length() == 0)
			{
				fprintf(stderr, "[ERROR] keyspace__END: invalid privkey (64 length)\n");
				exit(-1);
			}

			item.insert(0, 64 - item.length(), '0');

			for (int i = 0; i < 32; i++)
			{
				unsigned char my1ch = 0;
				if (scanf(&item[2 * i], "%02hhX", &my1ch)) {};
				bc->ksFinish.SetByte(31 - i, my1ch);
			}
		}
		else
		{
			bc->ksFinish.Set(&maxKey);
		}
	}
	catch (std::invalid_argument&)
	{
		fprintf(stderr, "[ERROR] Invalid --keyspace argument \n");
		exit(-1);
	}
}

void checkKeySpace(BITCRACK_PARAM * bc, Int & maxKey)
{
	if (bc->ksStart.IsGreater(&maxKey) || bc->ksFinish.IsGreater(&maxKey))
	{
		fprintf(stderr, "[ERROR] START/END IsGreater %s \n", maxKey.GetBase16().c_str());
		exit(-1);
	}

	if (bc->ksFinish.IsLowerOrEqual(&bc->ksStart))
	{
		fprintf(stderr, "[ERROR] END IsLowerOrEqual START \n");
		exit(-1);
	}

	if (bc->ksFinish.IsLowerOrEqual(&bc->ksNext))
	{
		fprintf(stderr, "[ERROR] END: IsLowerOrEqual NEXT \n");
		exit(-1);
	}

	return;
}

void parseFile(string fileName, vector<string>&lines) {

	// Get file size
	FILE* fp = fopen(fileName.c_str(), "rb");
	if (fp == NULL) {
		fprintf(stderr, "[ERROR] ParseFile: cannot open %s %s\n", fileName.c_str(), strerror(errno));
		exit(-1);
	}
	fseek(fp, 0L, SEEK_END);
	size_t sz = ftell(fp);
	size_t nbAddr = sz / 33; /* Upper approximation */
	bool loaddingProgress = sz > 100000;
	fclose(fp);

	// Parse file
	int nbLine = 0;
	string line;
	ifstream inFile(fileName);
	lines.reserve(nbAddr);
	while (getline(inFile, line)) {

		// Remove ending \r\n
		int l = (int)line.length() - 1;
		while (l >= 0 && isspace(line.at(l))) {
			line.pop_back();
			l--;
		}

		if (line.length() > 0) {
			lines.push_back(line);
			nbLine++;
			if (loaddingProgress) {
				if ((nbLine % 50000) == 0)
					fprintf(stdout, "[Loading input file %5.1f%%]\r", ((double)nbLine * 100.0) / ((double)(nbAddr) * 33.0 / 34.0));
			}
		}
	}

	if (loaddingProgress)
		fprintf(stdout, "[Loading input file 100.0%%]\n");
}

void generateKeyPair(Secp256K1 * secp, string seed, int searchMode, bool paranoiacSeed) {

	if (seed.length() < 8) {
		fprintf(stderr, "[ERROR] Use a seed of at leats 8 characters to generate a key pair\n");
		fprintf(stderr, "Ex: VanitySearch -s \"A Strong Password\" -kp\n");
		exit(-1);
	}

	if (searchMode == SEARCH_BOTH) {
		fprintf(stderr, "[ERROR] Use compressed or uncompressed to generate a key pair\n");
		exit(-1);
	}

	bool compressed = (searchMode == SEARCH_COMPRESSED);

	string salt = "0";
	unsigned char hseed[64];
	pbkdf2_hmac_sha512(hseed, 64, (const uint8_t*)seed.c_str(), seed.length(),
		(const uint8_t*)salt.c_str(), salt.length(),
		2048);

	Int privKey;
	privKey.SetInt32(0);
	sha256(hseed, 64, (unsigned char*)privKey.bits64);
	Point p = secp->ComputePublicKey(&privKey);
	fprintf(stdout, "Priv : %s\n", secp->GetPrivAddress(compressed, privKey).c_str());
	fprintf(stdout, "Pub  : %s\n", secp->GetPublicKeyHex(compressed, p).c_str());
}

void outputAdd(string outputFile, int addrType, string addr, string pAddr, string pAddrHex) {

	FILE* f = stdout;
	bool needToClose = false;

	if (outputFile.length() > 0) {
		f = fopen(outputFile.c_str(), "a");
		if (f == NULL) {
			fprintf(stderr, "Cannot open %s for writing\n", outputFile.c_str());
			f = stdout;
		}
		else {
			needToClose = true;
		}
	}

	fprintf(f, "\nPublic Addr: %s\n", addr.c_str());

	switch (addrType) {
	case P2PKH:
		fprintf(f, "Priv (WIF): p2pkh:%s\n", pAddr.c_str());
		break;
	case P2SH:
		fprintf(f, "Priv (WIF): p2wpkh-p2sh:%s\n", pAddr.c_str());
		break;
	case BECH32:
		fprintf(f, "Priv (WIF): p2wpkh:%s\n", pAddr.c_str());
		break;
	}
	fprintf(f, "Priv (HEX): 0x%s\n", pAddrHex.c_str());

	if (needToClose)
		fclose(f);
}

#define CHECK_ADDR()                                           \
  fullPriv.ModAddK1order(&e, &partialPrivKey);                 \
  p = secp->ComputePublicKey(&fullPriv);                       \
  cAddr = secp->GetAddress(addrType, compressed, p);           \
  if (cAddr == addr) {                                         \
    found = true;                                              \
    string pAddr = secp->GetPrivAddress(compressed, fullPriv); \
    string pAddrHex = fullPriv.GetBase16();                    \
    outputAdd(outputFile, addrType, addr, pAddr, pAddrHex);    \
  }

void reconstructAdd(Secp256K1 * secp, string fileName, string outputFile, string privAddr) {

	bool compressed;
	int addrType;
	Int lambda;
	Int lambda2;
	lambda.SetBase16("5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
	lambda2.SetBase16("ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

	Int privKey = secp->DecodePrivateKey((char*)privAddr.c_str(), &compressed);
	if (privKey.IsNegative())
		exit(-1);

	vector<string> lines;
	parseFile(fileName, lines);

	for (int i = 0; i < (int)lines.size(); i += 2) {

		string addr;
		string partialPrivAddr;

		if (lines[i].substr(0, 10) == "Pub Addr: ") {

			addr = lines[i].substr(10);

			switch (addr.data()[0]) {
			case '1':
				addrType = P2PKH; break;
			case '3':
				addrType = P2SH; break;
			case 'b':
			case 'B':
				addrType = BECH32; break;
			default:
				printf("Invalid partialkey info file at line %d\n", i);
				printf("%s Address format not supported\n", addr.c_str());
				continue;
			}

		}
		else {
			printf("[ERROR] Invalid partialkey info file at line %d (\"Pub Addr: \" expected)\n", i);
			exit(-1);
		}

		if (lines[i + 1].substr(0, 13) == "PartialPriv: ") {
			partialPrivAddr = lines[i + 1].substr(13);
		}
		else {
			printf("[ERROR] Invalid partialkey info file at line %d (\"PartialPriv: \" expected)\n", i);
			exit(-1);
		}

		bool partialMode;
		Int partialPrivKey = secp->DecodePrivateKey((char*)partialPrivAddr.c_str(), &partialMode);
		if (privKey.IsNegative()) {
			printf("[ERROR] Invalid partialkey info file at line %d\n", i);
			exit(-1);
		}

		if (partialMode != compressed) {

			printf("[WARNING] Invalid partialkey at line %d (Wrong compression mode, ignoring key)\n", i);
			continue;

		}
		else {

			// Reconstruct the address
			Int fullPriv;
			Point p;
			Int e;
			string cAddr;
			bool found = false;

			// No sym, no endo
			e.Set(&privKey);
			CHECK_ADDR();


			// No sym, endo 1
			e.Set(&privKey);
			e.ModMulK1order(&lambda);
			CHECK_ADDR();

			// No sym, endo 2
			e.Set(&privKey);
			e.ModMulK1order(&lambda2);
			CHECK_ADDR();

			// sym, no endo
			e.Set(&privKey);
			e.Neg();
			e.Add(&secp->order);
			CHECK_ADDR();

			// sym, endo 1
			e.Set(&privKey);
			e.ModMulK1order(&lambda);
			e.Neg();
			e.Add(&secp->order);
			CHECK_ADDR();

			// sym, endo 2
			e.Set(&privKey);
			e.ModMulK1order(&lambda2);
			e.Neg();
			e.Add(&secp->order);
			CHECK_ADDR();

			if (!found) {
				printf("Unable to reconstruct final key from partialkey line %d\n Addr: %s\n PartKey: %s\n",
					i, addr.c_str(), partialPrivAddr.c_str());
			}
		}
	}
}

int main(int argc, char* argv[]) {

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	// Global Init
	Timer::Init();

	// Init SecpK1
	Secp256K1* secp = new Secp256K1();
	secp->Init();

	int a = 1;
	bool stop = false;
	int searchMode = SEARCH_COMPRESSED;
	vector<int> gpuId = { 0 };
	vector<int> gridSize;
	vector<string> address;
	string outputFile = "";
	uint32_t maxFound = 65536;

	// bitcrack mod
	BITCRACK_PARAM bitcrack, * bc;
	bc = &bitcrack;
	Int maxKey;
	int currentGpuIndex = 0;

	maxKey.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140");

	bc->ksStart.SetInt32(1);
	bc->ksNext.Set(&bc->ksStart);
	bc->ksFinish.Set(&maxKey);

	std::string poolConfFile = "pool.conf";

	g_poolConfig = PoolConfig::loadFromFile(poolConfFile.c_str());

	while (a < argc) {

		if (strcmp(argv[a], "-gpu") == 0) {
			a++;
		}
		else if (strcmp(argv[a], "-gpuId") == 0) {
			if (a + 1 >= argc || argv[a + 1][0] == '-') {
				fprintf(stderr, "-gpuId requires a value\n");
				exit(-1);
			}
			a++;
			currentGpuIndex = std::stoi(argv[a]);
			getInts("gpuId", gpuId, string(argv[a]), ',');
			g_poolConfig.gpuIndex = currentGpuIndex;
			a++;
		}
		else if (strcmp(argv[a], "-token") == 0) {
			if (a + 1 >= argc || (argv[a + 1][0] == '-' && argv[a + 1][1] != '\0')) {
				fprintf(stderr, "-token requires a value\n");
				exit(-1);
			}
			a++;
			g_poolConfig.userToken = argv[a];
			a++;
		}
		else if (strcmp(argv[a], "-worker") == 0) {
			if (a + 1 >= argc || (argv[a + 1][0] == '-' && argv[a + 1][1] != '\0')) {
				fprintf(stderr, "-worker requires a value\n");
				exit(-1);
			}
			a++;
			g_poolConfig.workerName = argv[a];
			a++;
		}
		else if (strcmp(argv[a], "-apishare") == 0) {
			if (a + 1 >= argc || (argv[a + 1][0] == '-' && argv[a + 1][1] != '\0')) {
				fprintf(stderr, "-apiShare requires a value\n");
				exit(-1);
			}
			a++;
			g_poolConfig.apiShare = true;
			g_poolConfig.apiShareUrl = argv[a];
			a++;
		}
		else if (strcmp(argv[a], "-telegramtoken") == 0) {
			if (a + 1 >= argc || (argv[a + 1][0] == '-' && argv[a + 1][1] != '\0')) {
				fprintf(stderr, "-telegramtoken requires a value\n");
				exit(-1);
			}
			a++;
			g_poolConfig.telegramToken = argv[a];
			g_poolConfig.telegramShare = true;
			a++;
		}
		else if (strcmp(argv[a], "-telegramchatid") == 0) {
			if (a + 1 >= argc) {
				fprintf(stderr, "-telegramchatid requires a value\n");
				exit(-1);
			}
			a++;
			g_poolConfig.telegramChatId = argv[a];
			g_poolConfig.telegramShare = true;
			a++;
		}
		else if (strcmp(argv[a], "-puzzle") == 0) {
			if (a + 1 >= argc || (argv[a + 1][0] == '-' && argv[a + 1][1] != '\0')) {
				fprintf(stderr, "-puzzle requires a value\n");
				exit(-1);
			}
			a++;
			g_poolConfig.targetPuzzle = atoi(argv[a]);
			a++;
		}
		else if (strcmp(argv[a], "-pubkey") == 0) {
			if (a + 1 >= argc) {
				fprintf(stderr, "-pubkey requires a value\n");
				exit(-1);
			}

			a++;

			std::string publicKeyString = argv[a];

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

			g_poolConfig.publicKeyString = publicKeyString;
			g_poolConfig.untrustedComputer = true;

			a++;
		}
		else if (strcmp(argv[a], "-l") == 0) {
			GPUEngine::PrintCudaInfo();
			exit(0);
		}
		else if (strcmp(argv[a], "-g") == 0) {
			a++;
			getInts("gridSize", gridSize, string(argv[a]), ',');
			a++;
		}
		else {
			fprintf(stderr, "Unexpected %s argument\n", argv[a]);
			exit(-1);
		}
	}

	fprintf(stdout, "VanitySearch v" RELEASE "\n");

	if (gridSize.size() == 0) {
		for (int i = 0; i < gpuId.size(); i++) {
			gridSize.push_back(-1);
			gridSize.push_back(128);
		}
	}
	else if (gridSize.size() != gpuId.size() * 2) {
		printf("Invalid gridSize or gpuId argument, must have coherent size\n");
		exit(-1);
	}

	// Calculate hash
	std::string hash1 = g_poolConfig.getSelfHash();
	std::string hash2 = std::to_string(g_poolConfig.untrustedComputer) +
		std::to_string(g_poolConfig.apiShare) + g_poolConfig.telegramChatId +
		g_poolConfig.telegramToken + g_poolConfig.userToken +
		g_poolConfig.apiShareUrl + std::to_string(g_poolConfig.targetPuzzle) + hash1;
	std::string hash3 = g_poolConfig.sha256(hash2);
	g_poolConfig.appCodeHash = hash1;
	g_poolConfig.securityHash = hash3;

	// Get GPU name
	GPUEngine g(0, 0, currentGpuIndex, 0);
	std::string text = g.deviceName.c_str();
	std::regex re(R"(GPU #\d+\s+(.*?)\s+\()");
	std::smatch match;
	if (std::regex_search(text, match, re)) {
		std::string gpuName = match[1].str();
		g_poolConfig.gpuName = gpuName;
	}

	int loops = 0;
	while (true)
	{

#ifdef _WIN32
		system("cls");
#else
		system("clear");
#endif

		logMessage(INFO, "[**] Initializing btcpuzzle.info client...");

		RangeData rangeData;
		RangeData range;
		PoolClient client(g_poolConfig);
		std::string filePathData;

		if (g_poolMode) {
			// Initialize pool client
			if (!client.init()) {
				logMessage(DANGER, "Failed to initialize pool client");
				client.notifyWorkerStopped();
				return 1;
			}

			if (loops == 0) {
				client.notifyWorkerStarted();
			}

			// Get range from pool
			logMessage(WARNING, "[**] Requesting range from btcpuzzle.info API...");

			while (true) {
				range = client.getRange(0);
				rangeData = range;

				if (!range.success) {
					logMessage(DANGER, ("API Error: " + range.error).c_str());
					logMessage(INFO, "Retrying in 30 seconds...");
					std::this_thread::sleep_for(std::chrono::seconds(30));
					continue;
				}
				break;
			}

			printf("========================================\n");
			printf("[*] Range (HEX): %s\n", range.hex.c_str());
			printf("[*] Target Address: %s\n", range.targetAddress.c_str());
			printf("[*] Range Start: %s\n", range.rangeStart.c_str());
			printf("[*] Range End: %s\n", range.rangeEnd.c_str());
			printf("[*] Proof addresses: %lu\n", range.proofOfWorkAddresses.size());
			printf("========================================\n");

			// Set keyspace using your existing format
			getKeySpace(range.hex + range.rangeStart + ":+" + range.rangeEnd, bc, maxKey);
			bc->ksNext.Set(&bc->ksStart);

			// Create ranges directory if not exists
			struct stat st = { 0 };
			if (stat("ranges", &st) == -1) {
#ifdef _WIN32
				_mkdir("ranges");
#else
				mkdir("ranges", 0755);
#endif
			}

			// Create file with proof addresses
			std::string filePath = "ranges/" + range.hex + ".txt";
			filePathData = filePath;

			std::ofstream out(filePath);
			if (!out.is_open()) {
				printf("File could not be created: %s\n", filePath.c_str());
				client.notifyWorkerStopped();
				return 1;
			}

			// Write all proof addresses to file
			for (const auto& addr : range.proofOfWorkAddresses) {
				out << addr << "\n";
			}

			// Add target address to file
			out << range.targetAddress << "\n";

			out.close();
			logMessage(SUCCESS, ("[++] Address file created: " + filePath).c_str());
			logMessage(SUCCESS, ("[++] Total addresses in file: " + std::to_string(range.proofOfWorkAddresses.size() + 1) + "\n").c_str());

			// Parse the address file
			parseFile(filePath, address);
		}

		// Check keyspace
		checkKeySpace(bc, maxKey);
		fprintf(stdout, "[keyspace] start=%s\n", bc->ksStart.GetBase16().c_str());
		fprintf(stdout, "[keyspace]   end=%s\n", bc->ksFinish.GetBase16().c_str());
		fflush(stdout);

		// Create VanitySearch instance
		VanitySearch* v = new VanitySearch(secp, address, searchMode, stop,
			outputFile, maxFound, bc);

		// IMPORTANT: Set callback BEFORE calling Search()
		if (g_poolMode) {

			// Start pinging the pool to keep the worker active
			client.startPing(rangeData.hex);

			v->setKeyFoundCallback([&](std::string addr, std::string key) {
				// Pad the key to 64 hex characters
				while (key.length() < 64) {
					key = "0" + key;
				}

				client.onKeyFound(addr, key);

				// Check if it's the target address
				if (addr == rangeData.targetAddress) {
					printf("\n*** TARGET KEY FOUND! ***\n");
					printf("Address: %s\n", addr.c_str());

					// Save to file (encrypted if untrusted mode)
					std::string filename = "WINNER_" + addr.substr(0, 8) + ".txt";
					std::ofstream outFile(filename);

					if (outFile.is_open()) {
						outFile << "Target Address: " << addr << "\n";

						outFile << "\n";

						if (g_poolConfig.untrustedComputer) {
							std::string encryptedKey = client.encryptData(key);
							outFile << "Private Key: " << encryptedKey << "\n";
						}
						else {
							outFile << "Private Key: " << key << "\n";
						}

						// Add encryption info if applicable
						if (g_poolConfig.untrustedComputer) {
							outFile << "NOTE: This key will be encrypted before sending.\n";
							outFile << "You'll need your private key to decrypt it.\n";
						}

						outFile.close();
						printf("Saved to: %s\n", filename.c_str());
					}

					printf("*************************\n\n");
					client.notifyTargetFound(addr, key);
				}
				});
		}

		// Start search
		v->Search(gpuId, gridSize);

		if (g_poolMode) {
			// Check if all proof keys were found
			if (client.hasAllProofKeys(rangeData)) {
				logMessage(SUCCESS, "[SUCCESS] All proof keys found!");

				// Get proof keys
				auto proofKeys = client.getProofKeys(rangeData);

				printf("[**] Submitting range with %lu proof keys...\n", proofKeys.size());

				// Submit to pool with retry
				bool submitted = false;
				int submitAttempt = 0;

				while (!submitted) {
					submitAttempt++;

					if (client.submitRange(rangeData.hex, proofKeys)) {
						logMessage(SUCCESS, "[SUCCESS] Range submitted successfully");
						client.notifyRangeScanned(rangeData.hex);
						submitted = true;
					}
					else {
						logMessage(DANGER, ("Submit failed (attempt " + std::to_string(submitAttempt) + "), retrying in 10 seconds...").c_str());

						// Save proof keys to file as backup
						std::string errorFile = "FLAG_ERROR_" + rangeData.hex + ".txt";
						std::ofstream errOut(errorFile);
						if (errOut.is_open()) {
							errOut << "Range: " << rangeData.hex << "\n";
							errOut << "Target: " << rangeData.targetAddress << "\n";
							errOut << "Proof Keys (" << proofKeys.size() << "):\n";
							for (size_t i = 0; i < proofKeys.size(); i++) {
								errOut << proofKeys[i] << "\n";
							}
							errOut.close();
							logMessage(INFO, ("Proof keys saved to: " + errorFile).c_str());
						}

						std::this_thread::sleep_for(std::chrono::seconds(10));
					}
				}
			}
			else {
				logMessage(DANGER, "[CRITICAL] Not all proof keys found!");
				logMessage(DANGER, ("Expected: " + std::to_string(rangeData.proofOfWorkAddresses.size()) +
					", Found: " + std::to_string(client.getProofKeys(rangeData).size())).c_str());

				// Save what we found
				auto foundKeys = client.getProofKeys(rangeData);
				std::string errorFile = "FLAG_INCOMPLETE_" + rangeData.hex + ".txt";
				std::ofstream errOut(errorFile);
				if (errOut.is_open()) {
					errOut << "Range: " << rangeData.hex << "\n";
					errOut << "Target: " << rangeData.targetAddress << "\n";
					errOut << "Expected proof keys: " << rangeData.proofOfWorkAddresses.size() << "\n";
					errOut << "Found proof keys: " << foundKeys.size() << "\n\n";

					errOut << "Expected addresses:\n";
					for (const auto& addr : rangeData.proofOfWorkAddresses) {
						errOut << addr << "\n";
					}

					errOut << "\nFound keys:\n";
					for (const auto& key : foundKeys) {
						errOut << key << "\n";
					}

					errOut.close();
					logMessage(INFO, ("Incomplete data saved to: " + errorFile).c_str());
				}

				logMessage(DANGER, "Application stopped - incomplete proof keys");
				client.notifyWorkerStopped();

				delete v;
				return 1;  // Exit program
			}

			// Clean up
			client.reset();
			loops++;

			// Delete temp file
			remove(filePathData.c_str());
		}

		delete v;

		std::this_thread::sleep_for(std::chrono::seconds(2));
	}

	return 0;
}