#---------------------------------------------------------------------
# Makefile for vanitysearch
#
# Author : Jean-Luc PONS (modified for Linux multi-GPU build)

SRC = Base58.cpp IntGroup.cpp main.cpp Random.cpp \
      Timer.cpp Int.cpp IntMod.cpp Point.cpp SECP256K1.cpp \
      Vanity.cpp GPU/GPUGenerate.cpp hash/ripemd160.cpp \
      hash/sha256.cpp hash/sha512.cpp hash/ripemd160_sse.cpp \
      hash/sha256_sse.cpp Bech32.cpp Wildcard.cpp \
      Pool/PoolConfig.cpp Pool/PoolClient.cpp Pool/Logger.cpp

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

ifdef gpu

OBJET = $(addprefix $(OBJDIR)/, \
        Base58.o IntGroup.o main.o Random.o Timer.o Int.o \
        IntMod.o Point.o SECP256K1.o Vanity.o GPU/GPUGenerate.o \
        hash/ripemd160.o hash/sha256.o hash/sha512.o \
        hash/ripemd160_sse.o hash/sha256_sse.o \
        GPU/GPUEngine.o Bech32.o Wildcard.o \
        Pool/PoolConfig.o Pool/PoolClient.o Pool/Logger.o)

else

OBJET = $(addprefix $(OBJDIR)/, \
        Base58.o IntGroup.o main.o Random.o Timer.o Int.o \
        IntMod.o Point.o SECP256K1.o Vanity.o GPU/GPUGenerate.o \
        hash/ripemd160.o hash/sha256.o hash/sha512.o \
        hash/ripemd160_sse.o hash/sha256_sse.o Bech32.o Wildcard.o \
		Pool/PoolConfig.o Pool/PoolClient.o Pool/Logger.o)

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

#--------------------------------------------------------------------

ifdef gpu
ifdef debug
$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	$(NVCC) -G -maxrregcount=0 --ptxas-options=-v --compile \
	--compiler-options -fPIC -ccbin $(CXXCUDA) -m64 -g \
	-I$(CUDA)/include $(GENCODE) \
	-o $(OBJDIR)/GPU/GPUEngine.o -c GPU/GPUEngine.cu
else
$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	$(NVCC) -maxrregcount=0 --ptxas-options=-v --compile \
	--compiler-options -fPIC -ccbin $(CXXCUDA) -m64 -O2 \
	-I$(CUDA)/include $(GENCODE) \
	-o $(OBJDIR)/GPU/GPUEngine.o -c GPU/GPUEngine.cu
endif
endif

$(OBJDIR)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

all: VanitySearch

VanitySearch: $(OBJET)
	@echo Making vanitysearch...
	$(CXX) $(OBJET) $(LFLAGS) -o vanitysearch

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
	@echo Cleaning...
	@rm -f obj/*.o
	@rm -f obj/GPU/*.o
	@rm -f obj/hash/*.o
	@rm -f obj/Pool/*.o