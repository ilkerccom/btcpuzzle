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

#ifndef GPUENGINEH
#define GPUENGINEH

#include <vector>
#include "../SECP256k1.h"

#define SEARCH_COMPRESSED 0
#define SEARCH_UNCOMPRESSED 1
#define SEARCH_BOTH 2

static const char* searchModes[] = { "Compressed","Uncompressed","Compressed or Uncompressed" };

// Number of key per thread (must be a multiple of GRP_SIZE) per kernel call
// Should be the same as GRP_SIZE, not used on compressed only
#define STEP_SIZE 1024

// Number of thread per block
#ifdef _DEBUG
#define NUM_THREADS_PER_BLOCK 4
#else
#if STEP_SIZE <= 512
#define NUM_THREADS_PER_BLOCK 512
#elif STEP_SIZE > 512
#define NUM_THREADS_PER_BLOCK 256
#endif
#endif

#define ITEM_SIZE 28
#define ITEM_SIZE32 (ITEM_SIZE/4)
#define _64K 65536

typedef uint16_t address_t;
typedef uint32_t addressl_t;

typedef struct {
	uint32_t thId;
	int16_t  incr;
	int16_t  endo;
	uint8_t* hash;
	bool mode;
} ITEM;

// Second level lookup
typedef struct {
	address_t sAddress;
	std::vector<addressl_t> lAddresses;
} LADDRESS;

class GPUEngine {

public:

	GPUEngine(int nbThreadGroup, int nbThreadPerGroup, int gpuId, uint32_t maxFound);
	~GPUEngine();
	void SetAddress(std::vector<address_t> addresses);
	void SetAddress(std::vector<LADDRESS> addresses, uint32_t totalAddress);
	bool SetKeys(Point* p);
	void SetSearchMode(int searchMode);
	void SetSearchType(int searchType);
	void SetPattern(const char* pattern);
	bool Launch(std::vector<ITEM>& addressFound, bool spinWait);
	int GetNumThreadsGPU();
	int GetGroupSize();

	bool Check(Secp256K1* secp);
	std::string deviceName;

	static void PrintCudaInfo();
	static void GenerateCode(Secp256K1* secp, int size);

private:

	bool callKernel();
	static void ComputeIndex(std::vector<int>& s, int depth, int n);
	static void Browse(FILE* f, int depth, int max, int s);
	bool CheckHash(uint8_t* h, std::vector<ITEM>& found, int tid, int incr, int endo, int* ok);

	int numThreadsGPU;
	address_t* inputAddress;
	address_t* inputAddressPinned;
	uint32_t* inputAddressLookUp;
	uint32_t* inputAddressLookUpPinned;
	uint64_t* inputKey;
	uint64_t* inputKeyPinned;
	uint32_t* outputBuffer;
	uint32_t* outputBufferPinned;
	bool initialised;
	uint32_t searchMode;
	uint32_t searchType;
	bool littleEndian;
	bool lostWarning;
	uint32_t maxFound;
	uint32_t outputSize;
	std::string pattern;
	bool hasPattern;
};

#endif // GPUENGINEH
