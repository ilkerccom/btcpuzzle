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

#include "Vanity.h"
#include "Base58.h"
#include "Bech32.h"
#include "hash/sha256.h"
#include "hash/sha512.h"
#include "IntGroup.h"
#include "Wildcard.h"
#include "Timer.h"
#include "hash/ripemd160.h"
#include <string.h>
#include <math.h>
#include <algorithm>
#include <thread>

#define GRP_SIZE 1024

using namespace std;

Point Gn[GRP_SIZE / 2];
Point _2Gn;

VanitySearch::VanitySearch(Secp256K1* secp, vector<std::string>& inputAddresses, int searchMode,
	bool stop, string outputFile, uint32_t maxFound, BITCRACK_PARAM* bc):inputAddresses(inputAddresses) 
{
	this->secp = secp;
	this->searchMode = searchMode;
	this->stopWhenFound = stop;
	this->outputFile = outputFile;
	this->numGPUs = 0;
	this->maxFound = maxFound;	
	this->searchType = -1;
	this->bc = bc;	
	
	addresses.clear();

	// Create a 65536 items lookup table
	ADDRESS_TABLE_ITEM t;
	t.found = true;
	t.items = NULL;
	for (int i = 0; i < 65536; i++)
		addresses.push_back(t);
	
	// Insert addresses
	bool loadingProgress = (inputAddresses.size() > 1000);
	if (loadingProgress)
		fprintf(stdout, "[Building lookup16   0.0%%]\r");

	nbAddress = 0;

	for (int i = 0; i < (int)inputAddresses.size(); i++) 
	{
		ADDRESS_ITEM it;
		std::vector<ADDRESS_ITEM> itAddresses;

		if (initAddress(inputAddresses[i], &it)) {
			bool* found = new bool;
			*found = false;
			it.found = found;
			itAddresses.push_back(it);
		}

		if (itAddresses.size() > 0) 
		{
			// Add the item to all correspoding addresses in the lookup table
			for (int j = 0; j < (int)itAddresses.size(); j++) 
			{
				address_t p = itAddresses[j].sAddress;

				if (addresses[p].items == NULL) {
					addresses[p].items = new vector<ADDRESS_ITEM>();
					addresses[p].found = false;
					usedAddress.push_back(p);
				}
				(*addresses[p].items).push_back(itAddresses[j]);
			}
			
			nbAddress++;
		}

		if (loadingProgress && i % 1000 == 0)
			fprintf(stdout, "[Building lookup16 %5.1f%%]\r", (((double)i) / (double)(inputAddresses.size() - 1)) * 100.0);
	}

	if (loadingProgress)
		fprintf(stdout, "\n");

	if (nbAddress == 0) 
	{
		fprintf(stderr, "[ERROR] VanitySearch: nothing to search !\n");
		exit(-1);
	}

	// Second level lookup
	uint32_t unique_sAddress = 0;
	uint32_t minI = 0xFFFFFFFF;
	uint32_t maxI = 0;
	for (int i = 0; i < (int)addresses.size(); i++) 
	{
		if (addresses[i].items) 
		{
			LADDRESS lit;
			lit.sAddress = i;
			if (addresses[i].items) 
			{
				for (int j = 0; j < (int)addresses[i].items->size(); j++) 
				{
					lit.lAddresses.push_back((*addresses[i].items)[j].lAddress);
				}
			}

			sort(lit.lAddresses.begin(), lit.lAddresses.end());
			usedAddressL.push_back(lit);
			if ((uint32_t)lit.lAddresses.size() > maxI) maxI = (uint32_t)lit.lAddresses.size();
			if ((uint32_t)lit.lAddresses.size() < minI) minI = (uint32_t)lit.lAddresses.size();
			unique_sAddress++;
		}

		if (loadingProgress)
			fprintf(stdout, "[Building lookup32 %.1f%%]\r", ((double)i * 100.0) / (double)addresses.size());
	}

	if (loadingProgress)
		fprintf(stdout, "\n");
	
	string searchInfo = string(searchModes[searchMode]);

	// Compute Generator table G[n] = (n+1)*G
	Point g = secp->G;
	Gn[0] = g;
	g = secp->DoubleDirect(g);
	Gn[1] = g;
	for (int i = 2; i < GRP_SIZE / 2; i++) {
		g = secp->AddDirect(g, secp->G);
		Gn[i] = g;
	}
	// _2Gn = CPU_GRP_SIZE*G
	_2Gn = secp->DoubleDirect(Gn[GRP_SIZE / 2 - 1]);

	// Constant for endomorphism
	// if a is a nth primitive root of unity, a^-1 is also a nth primitive root.
	// beta^3 = 1 mod p implies also beta^2 = beta^-1 mop (by multiplying both side by beta^-1)
	// (beta^3 = 1 mod p),  beta2 = beta^-1 = beta^2
	// (lambda^3 = 1 mod n), lamba2 = lamba^-1 = lamba^2
	beta.SetBase16("7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee");
	lambda.SetBase16("5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
	beta2.SetBase16("851695d49a83f8ef919bb86153cbcb16630fb68aed0a766a3ec693d68e6afa40");
	lambda2.SetBase16("ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

	startKey.Set(&bc->ksNext);	

	char* ctimeBuff;
	time_t now = time(NULL);
	ctimeBuff = ctime(&now);
	fflush(stdout);
}

bool VanitySearch::isSingularAddress(std::string pref) {

	// check is the given address contains only 1
	bool only1 = true;
	int i = 0;
	while (only1 && i < (int)pref.length()) {
		only1 = pref.data()[i] == '1';
		i++;
	}
	return only1;
}

bool VanitySearch::initAddress(std::string& address, ADDRESS_ITEM* it) {

	std::vector<unsigned char> result;
	string dummy1 = address;
	int nbDigit = 0;
	bool wrong = false;

	if (address.length() < 2) {
		fprintf(stdout, "Ignoring address \"%s\" (too short)\n", address.c_str());
		return false;
	}

	int aType = -1;

	switch (address.data()[0]) {
	case '1':
		aType = P2PKH;
		break;
	case '3':
		aType = P2SH;
		break;
	case 'b':
	case 'B':
		std::transform(address.begin(), address.end(), address.begin(), ::tolower);
		if (strncmp(address.c_str(), "bc1q", 4) == 0)
			aType = BECH32;
		break;
	}

	if (aType == -1) {
		fprintf(stdout, "Ignoring address \"%s\" (must start with 1 or 3 or bc1q)\n", address.c_str());
		return false;
	}

	if (searchType == -1) searchType = aType;
	if (aType != searchType) {
		fprintf(stdout, "Ignoring address \"%s\" (P2PKH, P2SH or BECH32 allowed at once)\n", address.c_str());
		return false;
	}

	if (aType == BECH32) {

		// BECH32
		uint8_t witprog[40];
		size_t witprog_len;
		int witver;
		const char* hrp = "bc";

		int ret = segwit_addr_decode(&witver, witprog, &witprog_len, hrp, address.c_str());

		// Try to attack a full address ?
		if (ret && witprog_len == 20) {
						
			it->isFull = true;
			memcpy(it->hash160, witprog, 20);
			it->sAddress = *(address_t*)(it->hash160);
			it->lAddress = *(addressl_t*)(it->hash160);
			it->address = (char*)address.c_str();
			it->addressLength = (int)address.length();
			return true;

		}

		if (address.length() < 5) {
			fprintf(stdout, "Ignoring address \"%s\" (too short, length<5 )\n", address.c_str());
			return false;
		}

		if (address.length() >= 36) {
			fprintf(stdout, "Ignoring address \"%s\" (too long, length>36 )\n", address.c_str());
			return false;
		}

		uint8_t data[64];
		memset(data, 0, 64);
		size_t data_length;
		if (!bech32_decode_nocheck(data, &data_length, address.c_str() + 4)) {
			fprintf(stdout, "Ignoring address \"%s\" (Only \"023456789acdefghjklmnpqrstuvwxyz\" allowed)\n", address.c_str());
			return false;
		}
		
		it->sAddress = *(address_t*)data;		
		it->isFull = false;
		it->lAddress = 0;
		it->address = (char*)address.c_str();
		it->addressLength = (int)address.length();

		return true;
	}
	else {

		// P2PKH/P2SH
		wrong = !DecodeBase58(address, result);

		if (wrong) {
			fprintf(stdout, "Ignoring address \"%s\" (0, I, O and l not allowed)\n", address.c_str());
			return false;
		}

		// Try to attack a full address ?
		if (result.size() > 21) {
			
			it->isFull = true;
			memcpy(it->hash160, result.data() + 1, 20);
			it->sAddress = *(address_t*)(it->hash160);
			it->lAddress = *(addressl_t*)(it->hash160);
			it->address = (char*)address.c_str();
			it->addressLength = (int)address.length();
			return true;
		}

		// Address containing only '1'
		if (isSingularAddress(address)) {

			if (address.length() > 21) {
				fprintf(stdout, "Ignoring address \"%s\" (Too much 1)\n", address.c_str());
				return false;
			}
			
			it->isFull = false;
			it->sAddress = 0;
			it->lAddress = 0;
			it->address = (char*)address.c_str();
			it->addressLength = (int)address.length();
			return true;
		}

		// Search for highest hash160 16bit address (most probable)
		while (result.size() < 25) {
			DecodeBase58(dummy1, result);
			if (result.size() < 25) {
				dummy1.append("1");
				nbDigit++;
			}
		}

		if (searchType == P2SH) {
			if (result.data()[0] != 5) {
				fprintf(stdout, "Ignoring address \"%s\" (Unreachable, 31h1 to 3R2c only)\n", address.c_str());
				return false;
			}
		}

		if (result.size() != 25) {
			fprintf(stdout, "Ignoring address \"%s\" (Invalid size)\n", address.c_str());
			return false;
		}

		it->sAddress = *(address_t*)(result.data() + 1);

		dummy1.append("1");
		DecodeBase58(dummy1, result);

		if (result.size() == 25) {
			it->sAddress = *(address_t*)(result.data() + 1);
			nbDigit++;
		}
		
		it->isFull = false;
		it->lAddress = 0;
		it->address = (char*)address.c_str();
		it->addressLength = (int)address.length();

		return true;
	}
}

void VanitySearch::enumCaseUnsentiveAddress(std::string s, std::vector<std::string>& list) {

	char letter[64];
	int letterpos[64];
	int nbLetter = 0;
	int length = (int)s.length();

	for (int i = 1; i < length; i++) {
		char c = s.data()[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
			letter[nbLetter] = tolower(c);
			letterpos[nbLetter] = i;
			nbLetter++;
		}
	}

	int total = 1 << nbLetter;

	for (int i = 0; i < total; i++) {

		char tmp[64];
		strcpy(tmp, s.c_str());

		for (int j = 0; j < nbLetter; j++) {
			int mask = 1 << j;
			if (mask & i) tmp[letterpos[j]] = toupper(letter[j]);
			else         tmp[letterpos[j]] = letter[j];
		}

		list.push_back(string(tmp));

	}

}

double log1(double x) {
	// Use taylor series to approximate log(1-x)
	return -x - (x * x) / 2.0 - (x * x * x) / 3.0 - (x * x * x * x) / 4.0;
}

string VanitySearch::GetExpectedTimeBitCrack(double keyRate, double keyCount, BITCRACK_PARAM* bc) {

	char tmp[128];
	string ret;

	double dTime, nbDay, nbYear;
	int iTime, nbHour, nbMin, nbSec;

	dTime = Timer::get_tick() - startTime;

	nbDay = dTime / 86400.0;
	if (nbDay >= 1) {

		nbYear = nbDay / 365.0;
		if (nbYear > 1) {
			if (nbYear < 5)
				sprintf(tmp, "[%.1fy", nbYear);
			else
				sprintf(tmp, "[%gy", nbYear);
		}
		else {
			sprintf(tmp, "[%.1fd", nbDay);
		}

	}
	else {

		iTime = (int)dTime;
		nbHour = (int)((iTime % 86400) / 3600);
		nbMin = (int)(((iTime % 86400) % 3600) / 60);
		nbSec = (int)(iTime % 60);

		sprintf(tmp, "[%02d:%02d:%02d", nbHour, nbMin, nbSec);
	}
	ret = string(tmp);

	sprintf(tmp, " RUN || END ");
	ret = ret + string(tmp);

	Int Range;
	Range.Sub(&bc->ksFinish, &bc->ksNext);
	Int countKey;
	countKey.SetInt32(0);
	countKey.Add((uint64_t)keyCount);
	Int rateKey;
	rateKey.SetInt32(0);
	rateKey.Add((uint64_t)keyRate);
	if (Range.IsGreaterOrEqual(&countKey)) {
		Int leftKey;
		leftKey.Sub(&Range, &countKey);
		leftKey.Div(&rateKey);
		Int maxuint32;
		maxuint32.SetInt32(4294967295);
		uint32_t diffTime = 4294967295;
		if (leftKey.IsLower(&maxuint32)) diffTime = leftKey.GetInt32();

		if (diffTime == 4294967295) {
			sprintf(tmp, "infinity]");
		}
		else {

			dTime = (double)diffTime;

			nbDay = dTime / 86400.0;
			if (nbDay >= 1) {

				nbYear = nbDay / 365.0;
				if (nbYear > 1) {
					if (nbYear < 5)
						sprintf(tmp, "%.1fy]", nbYear);
					else
						sprintf(tmp, "%gy]", nbYear);
				}
				else {
					sprintf(tmp, "%.1fd]", nbDay);
				}

			}
			else {

				iTime = (int)dTime;
				nbHour = (int)((iTime % 86400) / 3600);
				nbMin = (int)(((iTime % 86400) % 3600) / 60);
				nbSec = (int)(iTime % 60);

				sprintf(tmp, "%02d:%02d:%02d] ", nbHour, nbMin, nbSec);
			}
		}
	}
	else {
		sprintf(tmp, "...finishing]");
	}

	ret = ret + string(tmp);

	return ret;
}

// ----------------------------------------------------------------------------

void VanitySearch::output(string addr, string pAddr, string pAddrHex, std::string pubKey) {

#ifdef WIN64
	WaitForSingleObject(ghMutex, INFINITE);
#else
	pthread_mutex_lock(&ghMutex);
#endif

	if (keyCallback) {
		std::string paddedKey = pAddrHex;

		while (paddedKey.length() < 64) {
			paddedKey = "0" + paddedKey;
		}

		keyCallback(addr, paddedKey);
	}

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

	//fprintf(f, "\nPublic Addr: %s\n", addr.c_str());	
	//fprintf(stdout, "\nPublic Addr: %s\n", addr.c_str());
	//fprintf(stderr, "\nPublic Addr: %s\n", addr.c_str());

	switch (searchType) {
	case P2PKH:
		//fprintf(f, "Priv (WIF): p2pkh:%s\n", pAddr.c_str());
		//fprintf(stdout, "Priv (WIF): p2pkh:%s\n", pAddr.c_str());
		//fprintf(stderr, "Priv (WIF): p2pkh:%s\n", pAddr.c_str());
		break;
	case P2SH:
		//fprintf(f, "Priv (WIF): p2wpkh-p2sh:%s\n", pAddr.c_str());
		//fprintf(stdout, "Priv (WIF): p2wpkh-p2sh:%s\n", pAddr.c_str());
		//fprintf(stderr, "Priv (WIF): p2wpkh-p2sh:%s\n", pAddr.c_str());
		break;
	case BECH32:
		//fprintf(f, "Priv (WIF): p2wpkh:%s\n", pAddr.c_str());
		//fprintf(stdout, "Priv (WIF): p2wpkh:%s\n", pAddr.c_str());
		//fprintf(stderr, "Priv (WIF): p2wpkh:%s\n", pAddr.c_str());
		break;
	}

	//fprintf(f, "Priv (HEX): 0x%064s\n", pAddrHex.c_str());	
	//fprintf(stdout, "Priv (HEX): 0x%064s\n", pAddrHex.c_str());
	//fprintf(stderr, "Priv (HEX): 0x%064s\n", pAddrHex.c_str());

	//fprintf(f, "PubK (HEX): 0x%s\n", pubKey.c_str());
	//fprintf(stdout, "PubK (HEX): 0x%s\n", pubKey.c_str());

	fflush(f);
	fflush(stdout);
	//fflush(stderr);	

	if (needToClose)
		fclose(f);

#ifdef WIN64
	ReleaseMutex(ghMutex);
#else
	pthread_mutex_unlock(&ghMutex);
#endif
}

void VanitySearch::updateFound() {

	// Check if all addresses has been found
	// Needed only if stopWhenFound is asked
	if (stopWhenFound) 	{

		bool allFound = true;
		for (int i = 0; i < (int)usedAddress.size(); i++) {
			bool iFound = true;
			address_t p = usedAddress[i];
			if (!addresses[p].found) {
				if (addresses[p].items) {
					for (int j = 0; j < (int)addresses[p].items->size(); j++) {
						iFound &= *((*addresses[p].items)[j].found);
					}
				}
				addresses[usedAddress[i]].found = iFound;
			}
			allFound &= iFound;
		}

		endOfSearch = allFound;		
	}		
}

bool VanitySearch::checkPrivKey(string addr, Int& key, int32_t incr, int endomorphism, bool mode) {

	Int k(&key);	

	if (incr < 0) {
		k.Add((uint64_t)(-incr));
		k.Neg();
		k.Add(&secp->order);		
	}
	else {
		k.Add((uint64_t)incr);
	}

	// Endomorphisms
	switch (endomorphism) {
	case 1:
		k.ModMulK1order(&lambda);		
		break;
	case 2:
		k.ModMulK1order(&lambda2);		
		break;
	}

	// Check addresses
	Point p = secp->ComputePublicKey(&k);	

	string chkAddr = secp->GetAddress(searchType, mode, p);
	if (chkAddr != addr) {

		// Key may be the opposite one (negative zero or compressed key)
		k.Neg();
		k.Add(&secp->order);
		p = secp->ComputePublicKey(&k);
		
		string chkAddr = secp->GetAddress(searchType, mode, p);
		if (chkAddr != addr) {
			fprintf(stdout, "\nWarning, wrong private key generated !\n");
			fprintf(stdout, "  Addr :%s\n", addr.c_str());
			fprintf(stdout, "  Check:%s\n", chkAddr.c_str());
			fprintf(stdout, "  Endo:%d incr:%d comp:%d\n", endomorphism, incr, mode);
			return false;
		}

	}

	output(addr, secp->GetPrivAddress(mode, k), k.GetBase16(), secp->GetPublicKeyHex(mode, p));

	return true;
}

void VanitySearch::checkAddrSSE(uint8_t* h1, uint8_t* h2, uint8_t* h3, uint8_t* h4,
	int32_t incr1, int32_t incr2, int32_t incr3, int32_t incr4,
	Int& key, int endomorphism, bool mode) {

	vector<string> addr = secp->GetAddress(searchType, mode, h1, h2, h3, h4);

	for (int i = 0; i < (int)inputAddresses.size(); i++) {

		if (Wildcard::match(addr[0].c_str(), inputAddresses[i].c_str())) {

			// Found it !      
			if (checkPrivKey(addr[0], key, incr1, endomorphism, mode)) {
				nbFoundKey++;
				//patternFound[i] = true;
				updateFound();
			}
		}

		if (Wildcard::match(addr[1].c_str(), inputAddresses[i].c_str())) {

			// Found it !      
			if (checkPrivKey(addr[1], key, incr2, endomorphism, mode)) {
				nbFoundKey++;
				//patternFound[i] = true;
				updateFound();
			}
		}

		if (Wildcard::match(addr[2].c_str(), inputAddresses[i].c_str())) {

			// Found it !      
			if (checkPrivKey(addr[2], key, incr3, endomorphism, mode)) {
				nbFoundKey++;
				//patternFound[i] = true;
				updateFound();
			}
		}

		if (Wildcard::match(addr[3].c_str(), inputAddresses[i].c_str())) {

			// Found it !      
			if (checkPrivKey(addr[3], key, incr4, endomorphism, mode)) {
				nbFoundKey++;
				//patternFound[i] = true;
				updateFound();
			}
		}
	}
}

void VanitySearch::checkAddr(int prefIdx, uint8_t* hash160, Int& key, int32_t incr, int endomorphism, bool mode) {
	
	vector<ADDRESS_ITEM>* pi = addresses[prefIdx].items;	

	// Full addresses
	for (int i = 0; i < (int)pi->size(); i++) {

		if (stopWhenFound && *((*pi)[i].found))
			continue;

		if (ripemd160_comp_hash((*pi)[i].hash160, hash160)) {

			// Found it !
			*((*pi)[i].found) = true;
			// You believe it ?
			if (checkPrivKey(secp->GetAddress(searchType, mode, hash160), key, incr, endomorphism, mode)) {
				nbFoundKey++;
				updateFound();
			}
		}
	}	
}

#ifdef WIN64
DWORD WINAPI _FindKeyGPU(LPVOID lpParam) {
#else
void* _FindKeyGPU(void* lpParam) {
#endif
	TH_PARAM* p = (TH_PARAM*)lpParam;
	p->obj->FindKeyGPU(p);
	return 0;
}

void VanitySearch::checkAddresses(bool compressed, Int key, int i, Point p1) {

	unsigned char h0[20];
	Point pte1[1];
	Point pte2[1];

	// Point
	secp->GetHash160(searchType, compressed, p1, h0);
	address_t pr0 = *(address_t*)h0;
	if (addresses[pr0].items)
		checkAddr(pr0, h0, key, i, 0, compressed);	
}

void VanitySearch::checkAddressesSSE(bool compressed, Int key, int i, Point p1, Point p2, Point p3, Point p4) {

	unsigned char h0[20];
	unsigned char h1[20];
	unsigned char h2[20];
	unsigned char h3[20];
	Point pte1[4];
	Point pte2[4];
	address_t pr0;
	address_t pr1;
	address_t pr2;
	address_t pr3;

	// Point -------------------------------------------------------------------------
	secp->GetHash160(searchType, compressed, p1, p2, p3, p4, h0, h1, h2, h3);	

	pr0 = *(address_t*)h0;
	pr1 = *(address_t*)h1;
	pr2 = *(address_t*)h2;
	pr3 = *(address_t*)h3;

	if (addresses[pr0].items)
		checkAddr(pr0, h0, key, i, 0, compressed);
	if (addresses[pr1].items)
		checkAddr(pr1, h1, key, i + 1, 0, compressed);
	if (addresses[pr2].items)
		checkAddr(pr2, h2, key, i + 2, 0, compressed);
	if (addresses[pr3].items)
		checkAddr(pr3, h3, key, i + 3, 0, compressed);	
}

void VanitySearch::getGPUStartingKeys(Int& tRangeStart, Int& tRangeEnd, int groupSize, int numThreadsGPU, Int *privateKeys, Point *publicKeys) {
		
	Int tRangeDiffPerThread;
	Int tRangeStartInThread(tRangeStart);
	Int tRangeEndInThread;

	Int numThreads;
	numThreads.SetInt32(numThreadsGPU);
	tRangeDiffPerThread.Set(&tRangeEnd);
	if (tRangeDiffPerThread.IsOdd())
	{
		tRangeDiffPerThread.AddOne();
	}
	tRangeDiffPerThread.Sub(&tRangeStart);

	Int tRangeDiffGlobal(tRangeDiffPerThread);

	// this only needed if total threads not power of 2
	//tRangeDiffPerThread.Add(&numThreads);

	tRangeDiffPerThread.Div(&numThreads);
	
	firstGPUThreadLastPrivateKey.Set(&tRangeStart);
	firstGPUThreadLastPrivateKey.Add(&tRangeDiffPerThread);

	fprintf(stdout, "  Divide the global range %s into %d gpu threads with %s range per thread \n", tRangeDiffGlobal.GetBase16().c_str(), numThreadsGPU, tRangeDiffPerThread.GetBase16().c_str());

	for (int i = 0; i < numThreadsGPU; i++) {

		// compute end key in range
		tRangeEndInThread.Set(&tRangeStartInThread);
		tRangeEndInThread.Add(&tRangeDiffPerThread);

		privateKeys[i].Set(&tRangeStartInThread);

		// calculate middle private key and public key
		Int privateKey(privateKeys + i);

		privateKey.Add((uint64_t)(groupSize / 2));	// Starting key is at the middle of the group

		publicKeys[i] = secp->ComputePublicKey(&privateKey);

		// for display
		if (i == 0) {
			fprintf(stdout, "  Thread %07d: %s:%s (%s) \n", i, tRangeStartInThread.GetBase16().c_str(), tRangeEndInThread.GetBase16().c_str(), privateKey.GetBase16().c_str());
		}
		if (i == 1) {
			fprintf(stdout, "  Thread %07d: %s:%s (%s) \n", i, tRangeStartInThread.GetBase16().c_str(), tRangeEndInThread.GetBase16().c_str(), privateKey.GetBase16().c_str());
		}
		if (i == 2) {
			fprintf(stdout, "  Thread %07d: %s:%s (%s) \n", i, tRangeStartInThread.GetBase16().c_str(), tRangeEndInThread.GetBase16().c_str(), privateKey.GetBase16().c_str());
			fprintf(stdout, "          ... : \n");
		}
		if (i == numThreadsGPU - 3) {
			fprintf(stdout, "  Thread %07d: %s:%s (%s) \n", i, tRangeStartInThread.GetBase16().c_str(), tRangeEndInThread.GetBase16().c_str(), privateKey.GetBase16().c_str());
		}
		if (i == numThreadsGPU - 2) {
			fprintf(stdout, "  Thread %07d: %s:%s (%s) \n", i, tRangeStartInThread.GetBase16().c_str(), tRangeEndInThread.GetBase16().c_str(), privateKey.GetBase16().c_str());
		}
		if (i == numThreadsGPU - 1) {
			fprintf(stdout, "  Thread %07d: %s:%s (%s) \n\n", i, tRangeStartInThread.GetBase16().c_str(), tRangeEndInThread.GetBase16().c_str(), privateKey.GetBase16().c_str());
		}

		// increment thread range start by range diff per thread
		tRangeStartInThread.Add(&tRangeDiffPerThread);		
	}
}

void threadFunctionStartingKeys(Secp256K1* secp, int threadId, int groupSize, int numThreadsGPUperCPU, Int tRangeDiffPerGPUThread, Int* tRangeStartInThread, Int* privateKeys, Point* publicKeys)
{
	Int tRangeEndInThread;	
	
	for (int i = 0; i < numThreadsGPUperCPU; i++) {

		// compute end key in range
		tRangeEndInThread.Set(&tRangeStartInThread[threadId]);
		tRangeEndInThread.Add(&tRangeDiffPerGPUThread);

		privateKeys[threadId * numThreadsGPUperCPU + i].Set(&tRangeStartInThread[threadId]);

		// calculate middle private key and public key
		Int privateKey(privateKeys + (threadId * numThreadsGPUperCPU + i));

		privateKey.Add((uint64_t)(groupSize / 2));	// Starting key is at the middle of the group

		publicKeys[threadId * numThreadsGPUperCPU + i] = secp->ComputePublicKey(&privateKey);

		// increment thread range start by range diff per thread
		tRangeStartInThread[threadId].Add(&tRangeDiffPerGPUThread);
	}
}

void VanitySearch::getGPUStartingKeysMT(Int& tRangeStart, Int& tRangeEnd, int groupSize, int numThreadsGPU, Int* privateKeys, Point* publicKeys) {

	int numMachineThreadsCPU = std::thread::hardware_concurrency();
	int numThreadsCPU = 1;

	while (numThreadsCPU * 2 <= numMachineThreadsCPU)
	{
		numThreadsCPU *= 2;
	}	

	Int tNumThreadsCPU;
	tNumThreadsCPU.SetInt32(numThreadsCPU);

	Int tRangeDiffPerGPUThread;
	Int tRangeStartInCPUThread(tRangeStart);	

	Int tNumThreadsGPU;
	tNumThreadsGPU.SetInt32(numThreadsGPU);
	tRangeDiffPerGPUThread.Set(&tRangeEnd);
	if (tRangeDiffPerGPUThread.IsOdd())
	{
		tRangeDiffPerGPUThread.AddOne();
	}
	tRangeDiffPerGPUThread.Sub(&tRangeStart);

	Int tRangeDiffGlobal(&tRangeDiffPerGPUThread);

	// this only needed if total GPU threads not power of 2
	//tRangeDiffPerThread.Add(&numThreads);

	tRangeDiffPerGPUThread.Div(&tNumThreadsGPU);

	firstGPUThreadLastPrivateKey.Set(&tRangeStart);
	firstGPUThreadLastPrivateKey.Add(&tRangeDiffPerGPUThread);	

	Int* startPrivateKeyForCPUThread;
	startPrivateKeyForCPUThread = new Int[numThreadsCPU];	

	std::thread* threads = new std::thread[numThreadsCPU];

	Int tRangeDiffPerCPUThread(&tRangeDiffGlobal);
	tRangeDiffPerCPUThread.Div(&tNumThreadsCPU);

	// fprintf(stdout, " Global range: %s\n", tRangeDiffGlobal.GetBase16().c_str());
	// fprintf(stdout, " CPU Threads: %d (%s)\n", numThreadsCPU, tRangeDiffPerCPUThread.GetBase16().c_str());
	// fprintf(stdout, " GPU Threads: %d (%s)\n", numThreadsGPU, tRangeDiffPerGPUThread.GetBase16().c_str());
	// fprintf(stdout, " Start init...\n");	

	for (int j = 0; j < numThreadsCPU; j++)
	{
		startPrivateKeyForCPUThread[j].Set(&tRangeStartInCPUThread);		
		threads[j] = std::thread(threadFunctionStartingKeys, secp, j, groupSize, numThreadsGPU / numThreadsCPU, tRangeDiffPerGPUThread, startPrivateKeyForCPUThread, privateKeys, publicKeys);

		tRangeStartInCPUThread.Add(&tRangeDiffPerCPUThread);
	}

	// wait all cpu threads to finish
	for (int j = 0; j < numThreadsCPU; j++)
	{
		if (threads[j].joinable())
		{
			threads[j].join();
		}
	}	

	//fprintf(stdout, " End init...\n");
	//fflush(stdout);
}

void VanitySearch::FindKeyGPU(TH_PARAM* ph) {

	bool ok = true;

	// Global init
	int thId = ph->threadId;
	GPUEngine g(ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound);
	int numThreadsGPU = g.GetNumThreadsGPU();
	Point* publicKeys = new Point[numThreadsGPU];
	Int* privateKeys = new Int[numThreadsGPU];
	vector<ITEM> found;

	fprintf(stdout, "GPU: %s\n", g.deviceName.c_str());
	fflush(stdout);

	counters[thId] = 0;	
	
	g.SetSearchMode(searchMode);
	g.SetSearchType(searchType);	
	g.SetAddress(usedAddressL, nbAddress);

	//getGPUStartingKeys(bc->ksStart, bc->ksFinish, g.GetGroupSize(), numThreadsGPU, privateKeys, publicKeys);
	getGPUStartingKeysMT(bc->ksStart, bc->ksFinish, g.GetGroupSize(), numThreadsGPU, privateKeys, publicKeys);
	
	// copy to gpu
	ok = g.SetKeys(publicKeys);

	ph->hasStarted = true;

	// GPU Thread
	while (ok && !endOfSearch) {		

		// Call kernel		
		// launch without using events because cudaDeviceScheduleBlockingSync was set
		ok = g.Launch(found, true);
		//ok = g.Launch(found, false);

		for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {

			ITEM it = found[i];
			checkAddr(*(address_t*)(it.hash), it.hash, privateKeys[it.thId], it.incr, it.endo, it.mode);
		}

		if (ok) {
			for (int i = 0; i < numThreadsGPU; i++) {
				privateKeys[i].Add((uint64_t)STEP_SIZE);
			}

			if (privateKeys[0].IsGreaterOrEqual(&firstGPUThreadLastPrivateKey))
			{				
				fflush(stdout);
				//endOfSearch = true;

				counters[thId] += (uint64_t)(STEP_SIZE)*numThreadsGPU; // Point					

				break;
			}
			else
			{				
				counters[thId] += (uint64_t)(STEP_SIZE)*numThreadsGPU; // Point					
			}
		}
	}

	delete[] privateKeys;
	delete[] publicKeys;

	ph->isRunning = false;
}

bool VanitySearch::isAlive(TH_PARAM * p) {

	bool isAlive = true;
	int total = numGPUs;
	for (int i = 0; i < total; i++)
		isAlive = isAlive && p[i].isRunning;

	return isAlive;
}

bool VanitySearch::hasStarted(TH_PARAM * p) {

	bool hasStarted = true;
	int total = numGPUs;
	for (int i = 0; i < total; i++)
		hasStarted = hasStarted && p[i].hasStarted;

	return hasStarted;
}

uint64_t VanitySearch::getGPUCount() {

	uint64_t count = 0;
	for (int i = 0; i < numGPUs; i++) {
		count += counters[i];
	}
	return count;
}

void VanitySearch::saveProgress(TH_PARAM* p, Int& lastSaveKey, BITCRACK_PARAM* bc) {

	Int lowerKey;
	lowerKey.Set(&p[0].THnextKey);

	int total = numGPUs;
	for (int i = 0; i < total; i++) {
		if (p[i].THnextKey.IsLower(&lowerKey))
			lowerKey.Set(&p[i].THnextKey);
	}

	if (lowerKey.IsLowerOrEqual(&lastSaveKey)) return;
	lastSaveKey.Set(&lowerKey);
}

void VanitySearch::Search(std::vector<int> gpuId, std::vector<int> gridSize) {

	double t0;
	double t1;
	endOfSearch = false;
	numGPUs = ((int)gpuId.size());
	nbFoundKey = 0;

	memset(counters, 0, sizeof(counters));	

	TH_PARAM* params = (TH_PARAM*)malloc(numGPUs * sizeof(TH_PARAM));
	memset(params, 0, numGPUs * sizeof(TH_PARAM));
	
	std::thread* threads = new std::thread[numGPUs];

#ifdef WIN64
	ghMutex = CreateMutex(NULL, FALSE, NULL);
	mutex = CreateMutex(NULL, FALSE, NULL);
#else
	ghMutex = PTHREAD_MUTEX_INITIALIZER;
	mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

	// Launch GPU threads
	for (int i = 0; i < numGPUs; i++) {
		params[i].obj = this;
		params[i].threadId = i;
		params[i].isRunning = true;
		params[i].gpuId = gpuId[i];
		params[i].gridSizeX = gridSize[i];
		params[i].gridSizeY = gridSize[i+1];
		params[i].THnextKey.Set(&bc->ksNext);
		
		threads[i] = std::thread(_FindKeyGPU, params + i);
	}

	uint64_t lastCount = 0;
	uint64_t gpuCount = 0;	

	double timeout60sec = 0;
	Int lastSaveKey; 
	lastSaveKey.SetInt32(0);

	// Key rate smoothing filter
#define FILTER_SIZE 20
	double lastkeyRate[FILTER_SIZE];	
	uint32_t filterPos = 0;

	double keyRate = 0.0;	

	memset(lastkeyRate, 0, sizeof(lastkeyRate));	

	// Wait that all threads have started
	while (!hasStarted(params)) {
		Timer::SleepMillis(5);
	}

	t0 = Timer::get_tick();
	startTime = t0;

	while (isAlive(params)) {

		int delay = 1000;
		while (isAlive(params) && delay > 0) {
			Timer::SleepMillis(500);
			delay -= 500;
		}
		
		uint64_t count = getGPUCount();

		t1 = Timer::get_tick();
		keyRate = (double)(count - lastCount) / (t1 - t0);	
		//if (keyRate == 0.0) keyRate = 1.0;
		lastkeyRate[filterPos % FILTER_SIZE] = keyRate;		
		filterPos++;

		// KeyRate smoothing
		double avgKeyRate = 0.0;		
		uint32_t nbSample;
		for (nbSample = 0; (nbSample < FILTER_SIZE) && (nbSample < filterPos); nbSample++) {
			avgKeyRate += lastkeyRate[nbSample];			
		}
		avgKeyRate /= (double)(nbSample);		

		if (isAlive(params)) {			
			fprintf(stdout, "%.2f MK/s (2^%.2f) %s[%d]\r",
				avgKeyRate / 1000000.0, log2((double)count),
				GetExpectedTimeBitCrack(avgKeyRate, (double)count, bc).c_str(),
				nbFoundKey);	
			fflush(stdout);
		}

		timeout60sec += (t1 - t0);
		if (timeout60sec > 2.0) {	

			// Save LowerPrivKey as saveProgress
			/*saveProgress(params, lastSaveKey, bc);

			// Reached end of keyspace
			if (lastSaveKey.IsGreaterOrEqual(&bc->ksFinish)) {
				endOfSearch = true;
				fprintf(stdout, "[EXIT] Range search completed \n");	
				fflush(stdout);
			}*/

			timeout60sec = 0.0;
		}

		lastCount = count;		
		t0 = t1;
	}

	// wait all cpu threads to finish
	for (int j = 0; j < numGPUs; j++)
	{
		if (threads[j].joinable())
		{
			threads[j].join();
		}
	}	

	free(params);
	//free(patternFound);

	char* ctimeBuff;
	time_t now = time(NULL);
	ctimeBuff = ctime(&now);
	fprintf(stdout, "Current task END time: %s", ctimeBuff);
	fflush(stdout);
}

string VanitySearch::GetHex(vector<unsigned char> &buffer) {

	string ret;

	char tmp[128];
	for (int i = 0; i < (int)buffer.size(); i++) {
		sprintf(tmp, "%02hhX", buffer[i]);
		ret.append(tmp);
	}

	return ret;
}
