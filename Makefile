#---------------------------------------------------------------------
# Makefile for vanitysearch
#
# Author : Jean-Luc PONS (modified for Linux multi-GPU build)

SRC = Base58.cpp IntGroup.cpp main.cpp Random.cpp \
      Timer.cpp Int.cpp IntMod.cpp Point.cpp SECP256K1.cpp \
      Vanity.cpp GPU/GPUGenerate.cpp hash/ripemd160.cpp \
      hash/sha256.cpp hash/sha512.cpp hash/ripemd160_sse.cpp \
      hash/sha256_sse.cpp Bech32.cpp Wildcard.cpp \
      Pool/PoolConfig.cpp Pool/PoolClient.cpp Pool/SecurePoolClient.cpp Pool/Logger.cpp

OBJDIR = obj

gpu=1

GENCODE = \
-gencode arch=compute_60,code=sm_60 \
-gencode arch=compute_61,code=sm_61 \
-gencode arch=compute_75,code=sm_75 \
-gencode arch=compute_80,code=sm_80 \
-gencode arch=compute_86,code=sm_86 \
-gencode arch=compute_89,code=sm_89 \
-gencode arch=compute_90,code=sm_90 \
-gencode arch=compute_100,code=sm_100 \
-gencode arch=compute_100,code=compute_100

# CUDA 13 removed cudaDeviceProp.computeMode. The upstream client only uses
# that member for a human-readable diagnostic string in PrintCudaInfo(), never
# for search logic. Build a temporary CUDA-13-compatible source copy that
# replaces only that diagnostic expression with the literal "Default".
CUDA13_ENGINE_SRC = GPU/.GPUEngine.cuda13.cu

# Keep PoolClient.cpp itself untouched. A temporary build-only copy renames
# exactly three upstream method definitions so SecurePoolClient.cpp can expose
# hardened wrappers using the original public names. This avoids global macros
# that can collide with C++ standard-library identifiers such as basic_ios::init.
# The build-only copy also removes the upstream "Example Encryption" demo so
# the hardened client does not invoke legacy RSA encryption during init().
UPSTREAM_POOL_SRC = Pool/.PoolClient.upstream.cpp

SELFTEST_BIN = secure-winner-selftest
RANGE_SELFTEST_BIN = secure-range-selftest
SELFTEST_LFLAGS = -lpthread -lcurl -lssl -lcrypto

ifdef gpu

OBJET = $(addprefix $(OBJDIR)/, \
        Base58.o IntGroup.o main.o Random.o Timer.o Int.o \
        IntMod.o Point.o SECP256K1.o Vanity.o GPU/GPUGenerate.o \
        hash/ripemd160.o hash/sha256.o hash/sha512.o \
        hash/ripemd160_sse.o hash/sha256_sse.o \
        GPU/GPUEngine.o Bech32.o Wildcard.o \
        Pool/PoolConfig.o Pool/PoolClient.o Pool/SecurePoolClient.o Pool/Logger.o)

else

OBJET = $(addprefix $(OBJDIR)/, \
        Base58.o IntGroup.o main.o Random.o Timer.o Int.o \
        IntMod.o Point.o SECP256K1.o Vanity.o GPU/GPUGenerate.o \
        hash/ripemd160.o hash/sha256.o hash/sha512.o \
        hash/ripemd160_sse.o hash/sha256_sse.o Bech32.o Wildcard.o \
		Pool/PoolConfig.o Pool/PoolClient.o Pool/SecurePoolClient.o Pool/Logger.o)

endif

CXX        = g++-9
CUDA       = /usr/local/cuda-12.8
CXXCUDA    = /usr/bin/g++-9
NVCC       = $(CUDA)/bin/nvcc

ifdef gpu
ifdef debug
CXXFLAGS   = -DWITHGPU -mssse3 -Wno-write-strings -g -I. -I$(CUDA)/include
else
CXXFLAGS   = -DWITHGPU -mssse3 -Wno-write-strings -O2 -I. -I$(CUDA)/include
endif
LFLAGS     = -lpthread -lcurl -lssl -lcrypto -L$(CUDA)/lib64 -lcudart
else
ifdef debug
CXXFLAGS   = -m64 -mssse3 -Wno-write-strings -g -I. -I$(CUDA)/include
else
CXXFLAGS   = -m64 -mssse3 -Wno-write-strings -O2 -I. -I$(CUDA)/include
endif
LFLAGS     = -lpthread -lcurl -lssl -lcrypto
endif

ifdef BUILD_HASH
CXXFLAGS  += -DBUILD_HASH=\"$(BUILD_HASH)\"
endif

#--------------------------------------------------------------------

ifdef gpu
ifdef debug
$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	@sed 's/sComputeMode\[deviceProp.computeMode\]/"Default"/g' GPU/GPUEngine.cu > $(CUDA13_ENGINE_SRC)
	$(NVCC) -G -maxrregcount=0 --ptxas-options=-v --compile \
	--compiler-options -fPIC -ccbin $(CXXCUDA) -m64 -g \
	-I$(CUDA)/include $(GENCODE) \
	-o $(OBJDIR)/GPU/GPUEngine.o -c $(CUDA13_ENGINE_SRC)
	@rm -f $(CUDA13_ENGINE_SRC)
else
$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	@sed 's/sComputeMode\[deviceProp.computeMode\]/"Default"/g' GPU/GPUEngine.cu > $(CUDA13_ENGINE_SRC)
	$(NVCC) -maxrregcount=0 --ptxas-options=-v --compile \
	--compiler-options -fPIC -ccbin $(CXXCUDA) -m64 -O2 \
	-I$(CUDA)/include $(GENCODE) \
	-o $(OBJDIR)/GPU/GPUEngine.o -c $(CUDA13_ENGINE_SRC)
	@rm -f $(CUDA13_ENGINE_SRC)
endif
endif

$(OBJDIR)/Pool/PoolClient.o: Pool/PoolClient.cpp
	@sed \
		-e 's/PoolClient::init()/PoolClient::upstream_init()/g' \
		-e 's/PoolClient::getRange(/PoolClient::upstream_getRange(/g' \
		-e 's/PoolClient::onKeyFound(/PoolClient::upstream_onKeyFound(/g' \
		-e '/std::string encryptedTest = encryptData("Hello from Btcpuzzle.info! Good luck on puzzles!");/d' \
		-e '/logMessage(SUCCESS, ("Example Encryption   => " + encryptedTest).c_str());/d' \
		Pool/PoolClient.cpp > $(UPSTREAM_POOL_SRC)
	$(CXX) $(CXXFLAGS) -o $@ -c $(UPSTREAM_POOL_SRC)
	@rm -f $(UPSTREAM_POOL_SRC)

$(OBJDIR)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

all: VanitySearch

VanitySearch: $(OBJET)
	@echo Making vanitysearch...
	$(CXX) $(OBJET) $(LFLAGS) -o vanitysearch

# Offline/local harness for the hardened winner path. It intentionally links
# the exact same PoolClient/SecurePoolClient objects used by the production
# binary, but it never requests a pool range and never starts pool pinging.
$(SELFTEST_BIN): $(OBJDIR)/Pool/PoolConfig.o $(OBJDIR)/Pool/PoolClient.o $(OBJDIR)/Pool/SecurePoolClient.o $(OBJDIR)/Pool/Logger.o tests/secure_winner_selftest.cpp
	@echo Making secure winner self-test...
	$(CXX) $(CXXFLAGS) tests/secure_winner_selftest.cpp \
		$(OBJDIR)/Pool/PoolConfig.o $(OBJDIR)/Pool/PoolClient.o \
		$(OBJDIR)/Pool/SecurePoolClient.o $(OBJDIR)/Pool/Logger.o \
		$(SELFTEST_LFLAGS) -o $(SELFTEST_BIN)

secure-selftest: $(SELFTEST_BIN)

# Offline/local harness for the exact range-validation function used by the
# production getRange() wrapper. It supplies synthetic RangeData values and
# therefore performs no HTTP request or pool communication.
$(RANGE_SELFTEST_BIN): $(OBJDIR)/Pool/PoolConfig.o $(OBJDIR)/Pool/PoolClient.o $(OBJDIR)/Pool/SecurePoolClient.o $(OBJDIR)/Pool/Logger.o tests/secure_range_selftest.cpp
	@echo Making secure range self-test...
	$(CXX) $(CXXFLAGS) tests/secure_range_selftest.cpp \
		$(OBJDIR)/Pool/PoolConfig.o $(OBJDIR)/Pool/PoolClient.o \
		$(OBJDIR)/Pool/SecurePoolClient.o $(OBJDIR)/Pool/Logger.o \
		$(SELFTEST_LFLAGS) -o $(RANGE_SELFTEST_BIN)

$(OBJET): | $(OBJDIR) $(OBJDIR)/GPU $(OBJDIR)/hash $(OBJDIR)/Pool

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/GPU: $(OBJDIR)
	cd $(OBJDIR) && mkdir -p GPU

$(OBJDIR)/hash: $(OBJDIR)
	cd $(OBJDIR) && mkdir -p hash

$(OBJDIR)/Pool: $(OBJDIR)
	cd $(OBJDIR) && mkdir -p Pool

clean:
	@rm -f obj/*.o
	@rm -f obj/GPU/*.o
	@rm -f obj/hash/*.o
	@rm -f obj/Pool/*.o
	@rm -f $(CUDA13_ENGINE_SRC)
	@rm -f $(UPSTREAM_POOL_SRC)
	@rm -f $(SELFTEST_BIN)
	@rm -f $(RANGE_SELFTEST_BIN)
