# TARGET = znver5
# TARGET = diamondrapids
TARGET = native

CXX = clang++
CC = clang
MCAFLAGS = -mcpu=$(TARGET)
NAYUKI_DIR = ../Reference-arithmetic-coding/cpp
FSE_DIR = ../FiniteStateEntropy/lib
CXXFLAGS = -march=$(TARGET) -std=c++23 -O3 -ffast-math -Wall -Wextra -Wno-psabi -ggdb3 -flto -I$(NAYUKI_DIR) -I$(FSE_DIR) $(EXTRA_CXXFLAGS)
CFLAGS = -march=$(TARGET) -std=c99 -O3 -Wall -Wextra -ggdb3 -flto -I$(FSE_DIR) $(EXTRA_CFLAGS)
LDFLAGS = -flto
LINK.o = $(CXX) $(LDFLAGS) $(TARGET_ARCH)
ifeq ($(findstring clang,$(CXX)),clang)
	LDFLAGS += -fuse-ld=lld
endif

# add a horrible probe because clang is a bit too conservative sometimes
ZMM_PROBE = $(shell echo 'void f(i32x16 &p){p+=p;}' | \
  $(CXX) $(CXXFLAGS) -include types.hpp $(1) -S -x c++ - -o - | grep -c zmm)
ifeq ($(call ZMM_PROBE,),0)
  ifneq ($(call ZMM_PROBE,-mprefer-vector-width=512),0)
    CXXFLAGS += -mprefer-vector-width=512
  endif
endif

cli: shrub.o erans.o nayuki.o fse_wrapper.o fse_compress.o fse_decompress.o entropy_common.o hist.o cli.o utils.o ArithmeticCoder.o FrequencyTable.o BitIoStream.o

cli.o: cli.cpp erans.hpp fse_wrapper.hpp nayuki.hpp types.hpp utils.hpp
erans.o: erans.cpp erans.hpp shrub.hpp types.hpp utils.hpp
nayuki.o: nayuki.cpp nayuki.hpp erans.hpp $(NAYUKI_DIR)/ArithmeticCoder.hpp $(NAYUKI_DIR)/BitIoStream.hpp $(NAYUKI_DIR)/FrequencyTable.hpp
fse_wrapper.o: fse_wrapper.cpp fse_wrapper.hpp erans.hpp $(FSE_DIR)/fse.h
shrub.o: shrub.cpp shrub.hpp erans.hpp types.hpp
utils.o: types.hpp utils.cpp utils.hpp
fse_compress.o: $(FSE_DIR)/fse_compress.c $(FSE_DIR)/fse.h $(FSE_DIR)/hist.h
	$(CC) $(CFLAGS) -c $< -o $@
fse_decompress.o: $(FSE_DIR)/fse_decompress.c $(FSE_DIR)/fse.h
	$(CC) $(CFLAGS) -c $< -o $@
entropy_common.o: $(FSE_DIR)/entropy_common.c $(FSE_DIR)/fse.h
	$(CC) $(CFLAGS) -c $< -o $@
hist.o: $(FSE_DIR)/hist.c $(FSE_DIR)/hist.h
	$(CC) $(CFLAGS) -c $< -o $@
ArithmeticCoder.o: $(NAYUKI_DIR)/ArithmeticCoder.cpp $(NAYUKI_DIR)/ArithmeticCoder.hpp $(NAYUKI_DIR)/BitIoStream.hpp $(NAYUKI_DIR)/FrequencyTable.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
FrequencyTable.o: $(NAYUKI_DIR)/FrequencyTable.cpp $(NAYUKI_DIR)/FrequencyTable.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
BitIoStream.o: $(NAYUKI_DIR)/BitIoStream.cpp $(NAYUKI_DIR)/BitIoStream.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

TMPDIR = /dev/shm
DIR = dir=$$(pwd); cd $(TMPDIR);

$(TMPDIR)/enwik%: enwik%
	cp $< $@

CODEC = erans
roundtrip: cli $(TMPDIR)/enwik8 $(TMPDIR)/enwik9
	@ $(DIR) rm -f enwik*.erans*
	@ $(DIR) $(PERF) $$dir/cli encode --codec=$(CODEC) enwik8 enwik8.erans
	@ $(DIR) $(PERF) $$dir/cli decode --codec=$(CODEC) enwik8.erans enwik8.erans.decoded
	@ $(DIR) cmp enwik8 enwik8.erans.decoded
	@ $(DIR) $(PERF) $$dir/cli encode --codec=$(CODEC) enwik9 enwik9.erans
	@ $(DIR) $(PERF) $$dir/cli decode --codec=$(CODEC) enwik9.erans enwik9.erans.decoded
	@ $(DIR) cmp enwik9 enwik9.erans.decoded
	@ $(DIR) wc -c enwik*
	@ echo roundtrip ok

perf: PERF = perf stat
perf: roundtrip

.PHONY: roundtrip perf

mca: vec.s
	llvm-mca $(MCAFLAGS) $< | awk -f mca.awk

.PHONY: mca

vec.s: vec.cpp types.hpp
	$(CXX) $(CXXFLAGS) -S -masm=intel $< -o $@

fn-%.s: vec.s
	{ echo .intel_syntax noprefix; sed "/$*/,/cfi_endproc/!d;/cfi_endproc/q" $<; } > $@

%.mca: fn-%.s
	llvm-mca $(MCAFLAGS) $< > $@

corpus: enwik8 enwik9 # calgary silesia
.PHONY: corpus

enwik%: enwik%.zip
	unzip $@
%.zip:
	wget https://www.mattmahoney.net/dc/$@

clean:
	rm -rf *.[os] *.mca cli
.PHONY: clean
