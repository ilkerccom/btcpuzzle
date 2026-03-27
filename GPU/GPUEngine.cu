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

#ifndef WIN64
#include <unistd.h>
#include <stdio.h>
#endif

#include "GPUEngine.h"
#include <cuda.h>
#include <cuda_runtime.h>

#include <stdint.h>
#include "../hash/sha256.h"
#include "../hash/ripemd160.h"
#include "../Timer.h"

#include "GPUGroup.h"
#include "GPUMath.h"
#include "GPUHash.h"
#include "GPUBase58.h"
#include "GPUWildcard.h"
#include "GPUCompute.h"

// ---------------------------------------------------------------------------------------

/*__global__ void comp_keys(uint32_t mode, address_t* address, uint32_t* lookup32, uint64_t* keys, uint32_t maxFound, uint32_t* found) {

	int xPtr = (blockIdx.x * blockDim.x) * 8;
	int yPtr = xPtr + 4 * blockDim.x;
	ComputeKeys(mode, keys + xPtr, keys + yPtr, address, lookup32, maxFound, found);

}

__global__ void comp_keys_p2sh(uint32_t mode, address_t* address, uint32_t* lookup32, uint64_t* keys, uint32_t maxFound, uint32_t* found) {

	int xPtr = (blockIdx.x * blockDim.x) * 8;
	int yPtr = xPtr + 4 * blockDim.x;
	ComputeKeysP2SH(mode, keys + xPtr, keys + yPtr, address, lookup32, maxFound, found);

}*/

// Andrew kernel, STEP_SIZE not used
__global__ void comp_keys_comp(address_t* sAddress, uint32_t* lookup32, uint64_t* keys, uint32_t* out) {

	int xPtr = (blockIdx.x * blockDim.x) * 8;
	int yPtr = xPtr + 4 * blockDim.x;

	uint64_t* startx = keys + xPtr;
	uint64_t* starty = keys + yPtr;

	uint64_t dx[GRP_SIZE / 2 + 1][4];
	uint64_t px[4];
	uint64_t py[4];
	uint64_t pyn[4];
	uint64_t sx[4];
	uint64_t sy[4];
	uint64_t dy[4];
	uint64_t _s[4];
	uint64_t _p2[4];

	uint32_t h[5];

	// Load starting key
	__syncthreads();
	Load256A(sx, startx);
	Load256A(sy, starty);		
	Load256(px, sx);
	Load256(py, sy);

	//for (uint32_t j = 0; j < STEP_SIZE / GRP_SIZE; j++) {

	// Fill group with delta x
	uint32_t i;
	for (i = 0; i < HSIZE; i++)
		ModSub256(dx[i], Gx[i], sx);
	ModSub256(dx[i], Gx[i], sx);  // For the first point
	ModSub256(dx[i + 1], _2Gnx, sx);  // For the next center point

	// Compute modular inverse
	_ModInvGrouped(dx);

	// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
	// We compute key in the positive and negative way from the center of the group

	// Check starting point
	//CheckHashComp(sAddress, px, (uint8_t)(py[0] & 1), j * GRP_SIZE + (GRP_SIZE / 2), lookup32, maxFound, out);
	_GetHash160Comp(px, (uint8_t)(py[0] & 1), (uint8_t*)h);
	//CheckPoint(h, j * GRP_SIZE + (GRP_SIZE / 2), 0, true, sAddress, lookup32, maxFound, out, P2PKH);
	CheckPointCompLookupOnly(h, GRP_SIZE / 2, sAddress, lookup32, out);

	ModNeg256(pyn, py);

	for (i = 0; i < HSIZE; i++) {

		// P = StartPoint + i*G
		Load256(px, sx);
		Load256(py, sy);
		ModSub256(dy, Gy[i], py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p2 = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

		ModSub256(py, Gx[i], px);
		_ModMult(py, _s);             // py = - s*(ret.x-p2.x)
		ModSub256(py, Gy[i]);         // py = - p2.y - s*(ret.x-p2.x);  

		//CheckHashComp(sAddress, px, (uint8_t)(py[0] & 1), j * GRP_SIZE + (GRP_SIZE / 2 + (i + 1)), lookup32, maxFound, out);
		_GetHash160Comp(px, (uint8_t)(py[0] & 1), (uint8_t*)h);
		//CheckPoint(h, j * GRP_SIZE + (GRP_SIZE / 2 + (i + 1)), 0, true, sAddress, lookup32, maxFound, out, P2PKH);
		CheckPointCompLookupOnly(h, GRP_SIZE / 2 + (i + 1), sAddress, lookup32, out);

		// P = StartPoint - i*G, if (x,y) = i*G then (x,-y) = -i*G
		Load256(px, sx);
		ModSub256(dy, pyn, Gy[i]);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;		

		ModSub256(py, px, Gx[i]);
		_ModMult(py, _s);             // py = s*(ret.x-p2.x)
		ModSub256(py, Gy[i], py);     // py = - p2.y - s*(ret.x-p2.x);

		//CheckHashComp(sAddress, px, (uint8_t)(py[0] & 1), j * GRP_SIZE + (GRP_SIZE / 2 - (i + 1)), lookup32, maxFound, out);
		_GetHash160Comp(px, (uint8_t)(py[0] & 1), (uint8_t*)h);
		//CheckPoint(h, j * GRP_SIZE + (GRP_SIZE / 2 - (i + 1)), 0, true, sAddress, lookup32, maxFound, out, P2PKH);
		CheckPointCompLookupOnly(h, GRP_SIZE / 2 - (i + 1), sAddress, lookup32, out);
	}

	// First point (startP - (GRP_SZIE/2)*G)
	Load256(px, sx);
	Load256(py, sy);
	ModNeg256(dy, Gy[i]);
	ModSub256(dy, py);

	_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
	_ModSqr(_p2, _s);              // _p = pow2(s)

	ModSub256(px, _p2, px);
	ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;	

	ModSub256(py, px, Gx[i]);
	_ModMult(py, _s);             // py = s*(ret.x-p2.x)
	ModSub256(py, Gy[i], py);     // py = - p2.y - s*(ret.x-p2.x);

	//CheckHashComp(sAddress, px, (uint8_t)(py[0] & 1), j * GRP_SIZE + (0), lookup32, maxFound, out);
	_GetHash160Comp(px, (uint8_t)(py[0] & 1), (uint8_t*)h);
	//CheckPoint(h, j * GRP_SIZE + (0), 0, true, sAddress, lookup32, maxFound, out, P2PKH);
	CheckPointCompLookupOnly(h, 0, sAddress, lookup32, out);

	i++;

	// Next start point (startP + GRP_SIZE*G)
	Load256(px, sx);
	Load256(py, sy);
	ModSub256(dy, _2Gny, py);

	_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
	_ModSqr(_p2, _s);             // _p2 = pow2(s)

	ModSub256(px, _p2, px);
	ModSub256(px, _2Gnx);         // px = pow2(s) - p1.x - p2.x;

	ModSub256(py, _2Gnx, px);
	_ModMult(py, _s);             // py = - s*(ret.x-p2.x)
	ModSub256(py, _2Gny);         // py = - p2.y - s*(ret.x-p2.x);  

	//}

	// Update starting point
	__syncthreads();
	Store256A(startx, px);
	Store256A(starty, py);
}

/*__global__ void comp_keys_pattern(uint32_t mode, address_t* pattern, uint64_t* keys, uint32_t maxFound, uint32_t* found) {

	int xPtr = (blockIdx.x * blockDim.x) * 8;
	int yPtr = xPtr + 4 * blockDim.x;
	ComputeKeys(mode, keys + xPtr, keys + yPtr, NULL, (uint32_t*)pattern, maxFound, found);

}

__global__ void comp_keys_p2sh_pattern(uint32_t mode, address_t* pattern, uint64_t* keys, uint32_t maxFound, uint32_t* found) {

	int xPtr = (blockIdx.x * blockDim.x) * 8;
	int yPtr = xPtr + 4 * blockDim.x;
	ComputeKeysP2SH(mode, keys + xPtr, keys + yPtr, NULL, (uint32_t*)pattern, maxFound, found);

}*/

//#define FULLCHECK
#ifdef FULLCHECK

// ---------------------------------------------------------------------------------------

__global__ void chekc_mult(uint64_t* a, uint64_t* b, uint64_t* r) {

	_ModMult(r, a, b);
	r[4] = 0;

}

// ---------------------------------------------------------------------------------------

__global__ void chekc_hash160(uint64_t* x, uint64_t* y, uint32_t* h) {

	_GetHash160(x, y, (uint8_t*)h);
	_GetHash160Comp(x, y, (uint8_t*)(h + 5));

}

// ---------------------------------------------------------------------------------------

__global__ void get_endianness(uint32_t* endian) {

	uint32_t a = 0x01020304;
	uint8_t fb = *(uint8_t*)(&a);
	*endian = (fb == 0x04);

}

#endif //FULLCHECK

// ---------------------------------------------------------------------------------------

using namespace std;

std::string toHex(unsigned char* data, int length) {

	string ret;
	char tmp[3];
	for (int i = 0; i < length; i++) {
		if (i && i % 4 == 0) ret.append(" ");
		sprintf(tmp, "%02hhX", (int)data[i]);
		ret.append(tmp);
	}
	return ret;

}

int _ConvertSMVer2Cores(int major, int minor) {

	// Defines for GPU Architecture types (using the SM version to determine
	// the # of cores per SM
	typedef struct {
		int SM;  // 0xMm (hexidecimal notation), M = SM Major version,
		// and m = SM minor version
		int Cores;
	} sSMtoCores;

	sSMtoCores nGpuArchCoresPerSM[] = {
		{0x60,  64},
		{0x61, 128},
		{0x62, 128},
		{0x70,  64},
		{0x72,  64},
		{0x75,  64},
		{0x80,  64},
		{0x86,  128},
		{0x89,  128},
		{0x90,  114},
		{0x100, 128},
		{0x120, 128},
		{-1, -1} };

	int index = 0;

	while (nGpuArchCoresPerSM[index].SM != -1) {
		if (nGpuArchCoresPerSM[index].SM == ((major << 4) + minor)) {
			return nGpuArchCoresPerSM[index].Cores;
		}

		index++;
	}

	return 0;
}

GPUEngine::GPUEngine(int nbThreadGroup, int nbThreadPerGroup, int gpuId, uint32_t maxFound) {

	// Initialise CUDA  
	initialised = false;
	cudaError_t err;

	int numBlocks;

	int deviceCount = 0;
	cudaError_t error_id = cudaGetDeviceCount(&deviceCount);

	if (error_id != cudaSuccess) {
		fprintf(stderr, "GPUEngine: CudaGetDeviceCount %s\n", cudaGetErrorString(error_id));
		return;
	}

	// This function call returns 0 if there are no CUDA capable devices.
	if (deviceCount == 0) {
		fprintf(stderr, "GPUEngine: There are no available device(s) that support CUDA\n");
		return;
	}

	err = cudaSetDevice(gpuId);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: %s\n", cudaGetErrorString(err));
		return;
	}

	// Andrew mod
	// set cpu spinwait flag to prevent 100% cpu usage
	err = cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: %s\n", cudaGetErrorString(err));
		return;
	}

	cudaDeviceProp deviceProp;
	cudaGetDeviceProperties(&deviceProp, gpuId);

	//numBlocks = deviceProp.multiProcessorCount * 64;

	int numBlocksMin = deviceProp.multiProcessorCount * 64;
	numBlocks = 64;
	while (numBlocks <= numBlocksMin)
	{
		numBlocks *= 2;
	}

#ifdef _DEBUG
	numBlocks = 4;
#endif

	this->numThreadsGPU = numBlocks * NUM_THREADS_PER_BLOCK;
	this->maxFound = maxFound;
	this->outputSize = (maxFound * ITEM_SIZE + 4);

	char tmp[512];
	sprintf(tmp, "GPU #%d %s (%dx%d cores) Grid(%dx%d)",
		gpuId, deviceProp.name, deviceProp.multiProcessorCount,
		_ConvertSMVer2Cores(deviceProp.major, deviceProp.minor),
		numThreadsGPU / NUM_THREADS_PER_BLOCK, NUM_THREADS_PER_BLOCK);
	deviceName = std::string(tmp);

	// Allocate memory
	err = cudaMalloc((void**)&inputAddress, _64K * 2);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Allocate address memory: %s\n", cudaGetErrorString(err));
		return;
	}
	err = cudaHostAlloc(&inputAddressPinned, _64K * 2, cudaHostAllocWriteCombined | cudaHostAllocMapped);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Allocate address pinned memory: %s\n", cudaGetErrorString(err));
		return;
	}
	err = cudaMalloc((void**)&inputKey, numThreadsGPU * 32 * 2);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Allocate input memory: %s\n", cudaGetErrorString(err));
		return;
	}
	err = cudaHostAlloc(&inputKeyPinned, numThreadsGPU * 32 * 2, cudaHostAllocWriteCombined | cudaHostAllocMapped);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Allocate input pinned memory: %s\n", cudaGetErrorString(err));
		return;
	}
	err = cudaMalloc((void**)&outputBuffer, outputSize);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Allocate output memory: %s\n", cudaGetErrorString(err));
		return;
	}
	err = cudaHostAlloc(&outputBufferPinned, outputSize, cudaHostAllocMapped);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Allocate output pinned memory: %s\n", cudaGetErrorString(err));
		return;
	}

	searchMode = SEARCH_COMPRESSED;
	searchType = P2PKH;
	initialised = true;
	pattern = "";
	hasPattern = false;
	inputAddressLookUp = NULL;
}

int GPUEngine::GetGroupSize() {
	return GRP_SIZE;
}

void GPUEngine::PrintCudaInfo() {

	cudaError_t err;

	const char* sComputeMode[] =
	{
	  "Multiple host threads",
	  "Only one host thread",
	  "No host thread",
	  "Multiple process threads",
	  "Unknown",
	   NULL
	};

	int deviceCount = 0;
	cudaError_t error_id = cudaGetDeviceCount(&deviceCount);

	if (error_id != cudaSuccess) {
		fprintf(stderr, "GPUEngine: CudaGetDeviceCount %s\n", cudaGetErrorString(error_id));
		return;
	}

	// This function call returns 0 if there are no CUDA capable devices.
	if (deviceCount == 0) {
		fprintf(stderr, "GPUEngine: There are no available device(s) that support CUDA\n");
		return;
	}

	for (int i = 0; i < deviceCount; i++) {

		err = cudaSetDevice(i);
		if (err != cudaSuccess) {
			fprintf(stderr, "GPUEngine: cudaSetDevice(%d) %s\n", i, cudaGetErrorString(err));
			return;
		}

		cudaDeviceProp deviceProp;
		cudaGetDeviceProperties(&deviceProp, i);
		fprintf(stdout, "GPU #%d %s (%dx%d cores) (Cap %d.%d) (%.1f MB) (%s)\n",
			i, deviceProp.name, deviceProp.multiProcessorCount,
			_ConvertSMVer2Cores(deviceProp.major, deviceProp.minor),
			deviceProp.major, deviceProp.minor, (double)deviceProp.totalGlobalMem / 1048576.0,
			sComputeMode[deviceProp.computeMode]);
	}
}

GPUEngine::~GPUEngine() {

	cudaFree(inputKey);
	cudaFree(inputAddress);
	if (inputAddressLookUp) cudaFree(inputAddressLookUp);
	cudaFreeHost(outputBufferPinned);
	cudaFree(outputBuffer);
}

int GPUEngine::GetNumThreadsGPU() {
	return numThreadsGPU;
}

void GPUEngine::SetSearchMode(int searchMode) {
	this->searchMode = searchMode;
}

void GPUEngine::SetSearchType(int searchType) {
	this->searchType = searchType;
}

void GPUEngine::SetAddress(std::vector<address_t> addresses) {

	memset(inputAddressPinned, 0, _64K * 2);
	for (int i = 0; i < (int)addresses.size(); i++)
		inputAddressPinned[addresses[i]] = 1;

	// Fill device memory
	cudaMemcpy(inputAddress, inputAddressPinned, _64K * 2, cudaMemcpyHostToDevice);

	// We do not need the input pinned memory anymore
	cudaFreeHost(inputAddressPinned);
	inputAddressPinned = NULL;
	lostWarning = false;

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: SetAddress: %s\n", cudaGetErrorString(err));
	}
}

void GPUEngine::SetPattern(const char* pattern) {

	strcpy((char*)inputAddressPinned, pattern);

	// Fill device memory
	cudaMemcpy(inputAddress, inputAddressPinned, _64K * 2, cudaMemcpyHostToDevice);

	// We do not need the input pinned memory anymore
	cudaFreeHost(inputAddressPinned);
	inputAddressPinned = NULL;
	lostWarning = false;

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: SetPattern: %s\n", cudaGetErrorString(err));
	}

	hasPattern = true;
}

void GPUEngine::SetAddress(std::vector<LADDRESS> addresses, uint32_t totalAddress) {

	// Allocate memory for the second level of lookup tables
	cudaError_t err = cudaMalloc((void**)&inputAddressLookUp, (_64K + totalAddress) * 4);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Allocate address lookup memory: %s\n", cudaGetErrorString(err));
		return;
	}
	err = cudaHostAlloc(&inputAddressLookUpPinned, (_64K + totalAddress) * 4, cudaHostAllocWriteCombined | cudaHostAllocMapped);
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Allocate address lookup pinned memory: %s\n", cudaGetErrorString(err));
		return;
	}

	uint32_t offset = _64K;
	memset(inputAddressPinned, 0, _64K * 2);
	memset(inputAddressLookUpPinned, 0, _64K * 4);
	for (int i = 0; i < (int)addresses.size(); i++) {
		int nbLAddress = (int)addresses[i].lAddresses.size();
		inputAddressPinned[addresses[i].sAddress] = (uint16_t)nbLAddress;
		inputAddressLookUpPinned[addresses[i].sAddress] = offset;
		for (int j = 0; j < nbLAddress; j++) {
			inputAddressLookUpPinned[offset++] = addresses[i].lAddresses[j];
		}
	}

	if (offset != (_64K + totalAddress)) {
		fprintf(stderr, "GPUEngine: Wrong totalAddress %d!=%d!\n", offset - _64K, totalAddress);
		return;
	}

	// Fill device memory
	cudaMemcpy(inputAddress, inputAddressPinned, _64K * 2, cudaMemcpyHostToDevice);
	cudaMemcpy(inputAddressLookUp, inputAddressLookUpPinned, (_64K + totalAddress) * 4, cudaMemcpyHostToDevice);

	// We do not need the input pinned memory anymore
	cudaFreeHost(inputAddressPinned);
	inputAddressPinned = NULL;
	cudaFreeHost(inputAddressLookUpPinned);
	inputAddressLookUpPinned = NULL;
	lostWarning = false;

	err = cudaGetLastError();
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: SetAddress (large): %s\n", cudaGetErrorString(err));
	}
}

bool GPUEngine::callKernel() {

	// Reset nbFound
	cudaMemset(outputBuffer, 0, 4);
			
	comp_keys_comp << < numThreadsGPU / NUM_THREADS_PER_BLOCK, NUM_THREADS_PER_BLOCK >> >
		(inputAddress, inputAddressLookUp, inputKey, outputBuffer);		

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Kernel: %s\n", cudaGetErrorString(err));
		return false;
	}
	return true;
}

bool GPUEngine::SetKeys(Point* p) {

	// Sets the starting keys for each thread
	// p must contains numThreadsGPU public keys
	for (int i = 0; i < numThreadsGPU; i += NUM_THREADS_PER_BLOCK) {
		for (int j = 0; j < NUM_THREADS_PER_BLOCK; j++) {

			inputKeyPinned[8 * i + j + 0 * NUM_THREADS_PER_BLOCK] = p[i + j].x.bits64[0];
			inputKeyPinned[8 * i + j + 1 * NUM_THREADS_PER_BLOCK] = p[i + j].x.bits64[1];
			inputKeyPinned[8 * i + j + 2 * NUM_THREADS_PER_BLOCK] = p[i + j].x.bits64[2];
			inputKeyPinned[8 * i + j + 3 * NUM_THREADS_PER_BLOCK] = p[i + j].x.bits64[3];

			inputKeyPinned[8 * i + j + 4 * NUM_THREADS_PER_BLOCK] = p[i + j].y.bits64[0];
			inputKeyPinned[8 * i + j + 5 * NUM_THREADS_PER_BLOCK] = p[i + j].y.bits64[1];
			inputKeyPinned[8 * i + j + 6 * NUM_THREADS_PER_BLOCK] = p[i + j].y.bits64[2];
			inputKeyPinned[8 * i + j + 7 * NUM_THREADS_PER_BLOCK] = p[i + j].y.bits64[3];
		}
	}

	// Fill device memory
	cudaMemcpy(inputKey, inputKeyPinned, numThreadsGPU * 32 * 2, cudaMemcpyHostToDevice);

	// We do not need the input pinned memory anymore
	cudaFreeHost(inputKeyPinned);
	inputKeyPinned = NULL;

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: SetKeys: %s\n", cudaGetErrorString(err));
	}

	return callKernel();
}

bool GPUEngine::Launch(std::vector<ITEM>& addressFound, bool spinWait) {

	addressFound.clear();

	// Get the result
	if (spinWait) {

		cudaMemcpy(outputBufferPinned, outputBuffer, outputSize, cudaMemcpyDeviceToHost);
	}
	else {

		// Use cudaMemcpyAsync to avoid default spin wait of cudaMemcpy wich takes 100% CPU
		cudaEvent_t evt;
		cudaEventCreate(&evt);

		//cudaMemcpy(outputAddressPinned, outputAddress, 4, cudaMemcpyDeviceToHost);
		cudaMemcpyAsync(outputBufferPinned, outputBuffer, 4, cudaMemcpyDeviceToHost, 0);

		cudaEventRecord(evt, 0);
		while (cudaEventQuery(evt) == cudaErrorNotReady) {
			// Sleep 1 ms to free the CPU
			Timer::SleepMillis(1);
		}
		cudaEventDestroy(evt);
	}

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		fprintf(stderr, "GPUEngine: Launch: %s\n", cudaGetErrorString(err));
		return false;
	}

	// Look for address found
	uint32_t nbFound = outputBufferPinned[0];
	if (nbFound > maxFound) {
		// address has been lost
		if (!lostWarning) {
			fprintf(stdout, "\nWarning, %d items lost\nHint: Search with less addresses, less threads (-g) or increase maxFound (-m)\n", (nbFound - maxFound));
			lostWarning = true;
		}
		nbFound = maxFound;
	}

	// When can perform a standard copy, the kernel is eneded
	cudaMemcpy(outputBufferPinned, outputBuffer, nbFound * ITEM_SIZE + 4, cudaMemcpyDeviceToHost);

	for (uint32_t i = 0; i < nbFound; i++) {
		uint32_t* itemPtr = outputBufferPinned + (i * ITEM_SIZE32 + 1);
		ITEM it;
		it.thId = itemPtr[0];
		int16_t* ptr = (int16_t*)&(itemPtr[1]);
		it.endo = ptr[0] & 0x7FFF;
		it.mode = (ptr[0] & 0x8000) != 0;
		it.incr = ptr[1];
		it.hash = (uint8_t*)(itemPtr + 2);
		addressFound.push_back(it);
	}

	return callKernel();
}

bool GPUEngine::CheckHash(uint8_t* h, vector<ITEM>& found, int tid, int incr, int endo, int* nbOK) {

	bool ok = true;

	// Search in found by GPU
	bool f = false;
	int l = 0;
	//printf("Search: %s\n", toHex(h,20).c_str());
	while (l < found.size() && !f) {
		f = ripemd160_comp_hash(found[l].hash, h);
		if (!f) l++;
	}
	if (f) {
		found.erase(found.begin() + l);
		*nbOK = *nbOK + 1;
	}
	else {
		ok = false;
		fprintf(stdout, "Expected item not found %s (thread=%d, incr=%d, endo=%d)\n",
			toHex(h, 20).c_str(), tid, incr, endo);
		if (found[l].hash != NULL)
			fprintf(stdout, "%s\n", toHex(found[l].hash, 20).c_str());
		else
			fprintf(stdout, "NULL\n");
	}

	return ok;
}

bool GPUEngine::Check(Secp256K1* secp) {

	uint8_t h[20];
	int i = 0;
	int j = 0;
	bool ok = true;

	if (!initialised)
		return false;

	fprintf(stdout, "GPU: %s\n", deviceName.c_str());

#ifdef FULLCHECK

	// Get endianess
	get_endianness << <1, 1 >> > (outputAddress);
	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		printf("GPUEngine: get_endianness: %s\n", cudaGetErrorString(err));
		return false;
	}
	cudaMemcpy(outputAddressPinned, outputAddress, 1, cudaMemcpyDeviceToHost);
	littleEndian = *outputAddressPinned != 0;
	printf("Endianness: %s\n", (littleEndian ? "Little" : "Big"));

	// Check modular mult
	Int a;
	Int b;
	Int r;
	Int c;
	a.Rand(256);
	b.Rand(256);
	c.ModMulK1(&a, &b);
	memcpy(inputKeyPinned, a.bits64, BIFULLSIZE);
	memcpy(inputKeyPinned + 5, b.bits64, BIFULLSIZE);
	cudaMemcpy(inputKey, inputKeyPinned, BIFULLSIZE * 2, cudaMemcpyHostToDevice);
	chekc_mult << <1, 1 >> > (inputKey, inputKey + 5, (uint64_t*)outputAddress);
	cudaMemcpy(outputAddressPinned, outputAddress, BIFULLSIZE, cudaMemcpyDeviceToHost);
	memcpy(r.bits64, outputAddressPinned, BIFULLSIZE);

	if (!c.IsEqual(&r)) {
		printf("\nModular Mult wrong:\nR=%s\nC=%s\n",
			toHex((uint8_t*)r.bits64, BIFULLSIZE).c_str(),
			toHex((uint8_t*)c.bits64, BIFULLSIZE).c_str());
		return false;
	}

	// Check hash 160C
	uint8_t hc[20];
	Point pi;
	pi.x.Rand(256);
	pi.y.Rand(256);
	secp.GetHash160(pi, false, h);
	secp.GetHash160(pi, true, hc);
	memcpy(inputKeyPinned, pi.x.bits64, BIFULLSIZE);
	memcpy(inputKeyPinned + 5, pi.y.bits64, BIFULLSIZE);
	cudaMemcpy(inputKey, inputKeyPinned, BIFULLSIZE * 2, cudaMemcpyHostToDevice);
	chekc_hash160 << <1, 1 >> > (inputKey, inputKey + 5, outputAddress);
	cudaMemcpy(outputAddressPinned, outputAddress, 64, cudaMemcpyDeviceToHost);

	if (!ripemd160_comp_hash((uint8_t*)outputAddressPinned, h)) {
		printf("\nGetHask160 wrong:\n%s\n%s\n",
			toHex((uint8_t*)outputAddressPinned, 20).c_str(),
			toHex(h, 20).c_str());
		return false;
	}
	if (!ripemd160_comp_hash((uint8_t*)(outputAddressPinned + 5), hc)) {
		printf("\nGetHask160Comp wrong:\n%s\n%s\n",
			toHex((uint8_t*)(outputAddressPinned + 5), 20).c_str(),
			toHex(h, 20).c_str());
		return false;
	}

#endif //FULLCHECK

	Point* p = new Point[numThreadsGPU];
	Point* p2 = new Point[numThreadsGPU];
	Int k;

	// Check kernel
	int nbFoundCPU[6];
	int nbOK[6];
	vector<ITEM> found;
	bool searchComp;

	if (searchMode == SEARCH_BOTH) {
		fprintf(stdout, "Warning, Check function does not support BOTH_MODE, use either compressed or uncompressed");
		return true;
	}

	searchComp = (searchMode == SEARCH_COMPRESSED) ? true : false;

	uint32_t seed = (uint32_t)time(NULL);
	fprintf(stdout, "Seed: %u\n", seed);
	rseed(seed);
	memset(nbOK, 0, sizeof(nbOK));
	memset(nbFoundCPU, 0, sizeof(nbFoundCPU));
	for (int i = 0; i < numThreadsGPU; i++) {
		k.Rand(64);
		p[i] = secp->ComputePublicKey(&k);
		// Group starts at the middle
		k.Add((uint64_t)GRP_SIZE / 2);
		p2[i] = secp->ComputePublicKey(&k);
	}

	std::vector<address_t> prefs;
	prefs.push_back(0xFEFE);
	prefs.push_back(0x1234);
	SetAddress(prefs);
	SetKeys(p2);
	double t0 = Timer::get_tick();
	Launch(found, true);
	double t1 = Timer::get_tick();
	//Timer::printResult((char *)"Key", 6*STEP_SIZE*numThreadsGPU, t0, t1);
	Timer::printResult((char*)"Key", 1 * STEP_SIZE * numThreadsGPU, t0, t1);

	//for (int i = 0; i < found.size(); i++) {
	//  printf("[%d]: thId=%d incr=%d\n", i, found[i].thId,found[i].incr);
	//  printf("[%d]: %s\n", i,toHex(found[i].hash,20).c_str());
	//}

	fprintf(stdout, "ComputeKeys() found %d items , CPU check...\n", (int)found.size());

	//Int beta,beta2;
	//beta.SetBase16((char *)"7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee");
	//beta2.SetBase16((char *)"851695d49a83f8ef919bb86153cbcb16630fb68aed0a766a3ec693d68e6afa40");

	// Check with CPU
	for (j = 0; (j < numThreadsGPU); j++) {
		for (i = 0; i < STEP_SIZE; i++) {

			Point pt;//, pt1, pt2;
			pt = p[j];
			//pt1 = p[j];
			//pt2 = p[j];
			//pt1.x.ModMulK1(&beta);
			//pt2.x.ModMulK1(&beta2);
			p[j] = secp->NextKey(p[j]);

			// Point and endo
			secp->GetHash160(P2PKH, searchComp, pt, h);
			address_t pr = *(address_t*)h;
			if (pr == 0xFEFE || pr == 0x1234) {
				nbFoundCPU[0]++;
				ok &= CheckHash(h, found, j, i, 0, nbOK + 0);
			}
			/*
			secp->GetHash160(P2PKH, searchComp, pt1, h);
			pr = *(address_t *)h;
			if (pr == 0xFEFE || pr == 0x1234) {
			  nbFoundCPU[1]++;
			  ok &= CheckHash(h, found, j, i, 1, nbOK + 1);
			}
			secp->GetHash160(P2PKH, searchComp, pt2, h);
			pr = *(address_t *)h;
			if (pr == 0xFEFE || pr == 0x1234) {
			  nbFoundCPU[2]++;
			  ok &= CheckHash(h, found, j, i, 2, nbOK + 2);
			}

			// Symetrics
			pt.y.ModNeg();
			pt1.y.ModNeg();
			pt2.y.ModNeg();

			secp->GetHash160(P2PKH, searchComp, pt, h);
			pr = *(address_t *)h;
			if (pr == 0xFEFE || pr == 0x1234) {
			  nbFoundCPU[3]++;
			  ok &= CheckHash(h, found, j, -i, 0, nbOK + 3);
			}

			secp->GetHash160(P2PKH, searchComp, pt1, h);
			pr = *(address_t *)h;
			if (pr == 0xFEFE || pr == 0x1234) {
			  nbFoundCPU[4]++;
			  ok &= CheckHash(h, found, j, -i, 1, nbOK + 4);
			}
			secp->GetHash160(P2PKH, searchComp, pt2, h);
			pr = *(address_t *)h;
			if (pr == 0xFEFE || pr == 0x1234) {
			  nbFoundCPU[5]++;
			  ok &= CheckHash(h, found, j, -i, 2, nbOK + 5);
			}
			*/
		}
	}

	if (ok && found.size() != 0) {
		ok = false;
		fprintf(stdout, "Unexpected item found !\n");
	}

	if (!ok) {

		int nbF = nbFoundCPU[0] + nbFoundCPU[1] + nbFoundCPU[2] +
			nbFoundCPU[3] + nbFoundCPU[4] + nbFoundCPU[5];
		fprintf(stdout, "CPU found %d items\n", nbF);

		fprintf(stdout, "GPU: point   correct [%d/%d]\n", nbOK[0], nbFoundCPU[0]);
		/*
		printf("GPU: endo #1 correct [%d/%d]\n", nbOK[1] , nbFoundCPU[1]);
		printf("GPU: endo #2 correct [%d/%d]\n", nbOK[2] , nbFoundCPU[2]);

		printf("GPU: sym/point   correct [%d/%d]\n", nbOK[3] , nbFoundCPU[3]);
		printf("GPU: sym/endo #1 correct [%d/%d]\n", nbOK[4] , nbFoundCPU[4]);
		printf("GPU: sym/endo #2 correct [%d/%d]\n", nbOK[5] , nbFoundCPU[5]);
		*/
		fprintf(stdout, "GPU/CPU check Failed !\n");
	}

	if (ok) fprintf(stdout, "GPU/CPU check OK\n");

	delete[] p;
	delete[] p2;
	return ok;
}


