# TARGET = znver5
# TARGET = diamondrapids
TARGET = native

CXX = clang++
CC = clang
MCAFLAGS = -mcpu=$(TARGET)
NAYUKI_DIR = ../Reference-arithmetic-coding/cpp
FSE_DIR = ../FiniteStateEntropy/lib
HTS_DIR = ../htscodecs
RYG_DIR = ../ryg_rans
CXXFLAGS = -march=$(TARGET) -std=c++23 -O3 -ffast-math -Wall -Wextra -Wno-psabi -ggdb3 -flto -I. -I$(NAYUKI_DIR) -I$(FSE_DIR) -I$(HTS_DIR) -I$(RYG_DIR) $(EXTRA_CXXFLAGS)
CFLAGS = -march=$(TARGET) -std=c99 -O3 -Wall -Wextra -ggdb3 -flto -I. -I$(FSE_DIR) -I$(HTS_DIR) $(EXTRA_CFLAGS)
LDFLAGS = -flto
LDLIBS = -lm
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

cli: shrub.o erans.o nayuki.o fse_wrapper.o hts_wrapper.o ryg_wrapper.o fse_compress.o fse_decompress.o entropy_common.o hist.o hts_arith_dynamic.o hts_rANS_static4x16pr.o hts_rANS_static32x16pr.o hts_utils.o hts_pack.o hts_rle.o cli.o utils.o ArithmeticCoder.o FrequencyTable.o BitIoStream.o

cli.o: cli.cpp erans.hpp fse_wrapper.hpp hts_wrapper.hpp nayuki.hpp ryg_wrapper.hpp types.hpp utils.hpp
erans.o: erans.cpp erans.hpp shrub.hpp types.hpp utils.hpp
nayuki.o: nayuki.cpp nayuki.hpp erans.hpp $(NAYUKI_DIR)/ArithmeticCoder.hpp $(NAYUKI_DIR)/BitIoStream.hpp $(NAYUKI_DIR)/FrequencyTable.hpp
fse_wrapper.o: fse_wrapper.cpp fse_wrapper.hpp erans.hpp $(FSE_DIR)/fse.h
hts_wrapper.o: hts_wrapper.cpp hts_wrapper.hpp erans.hpp types.hpp config.h $(HTS_DIR)/htscodecs/arith_dynamic.h $(HTS_DIR)/htscodecs/rANS_static4x16.h
ryg_wrapper.o: ryg_wrapper.cpp ryg_wrapper.hpp erans.hpp shrub.hpp types.hpp $(RYG_DIR)/rans64.h
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
hts_arith_dynamic.o: $(HTS_DIR)/htscodecs/arith_dynamic.c config.h
	$(CC) $(CFLAGS) -c $< -o $@
hts_rANS_static4x16pr.o: $(HTS_DIR)/htscodecs/rANS_static4x16pr.c config.h
	$(CC) $(CFLAGS) -c $< -o $@
hts_rANS_static32x16pr.o: $(HTS_DIR)/htscodecs/rANS_static32x16pr.c config.h
	$(CC) $(CFLAGS) -c $< -o $@
hts_utils.o: $(HTS_DIR)/htscodecs/utils.c config.h
	$(CC) $(CFLAGS) -c $< -o $@
hts_pack.o: $(HTS_DIR)/htscodecs/pack.c config.h
	$(CC) $(CFLAGS) -c $< -o $@
hts_rle.o: $(HTS_DIR)/htscodecs/rle.c config.h
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
	@ $(DIR) rm -f enwik*.$(CODEC)*
	@ $(DIR) $(PERF) $$dir/cli encode --codec=$(CODEC) enwik8 enwik8.$(CODEC)
	@ $(DIR) $(PERF) $$dir/cli decode --codec=$(CODEC) enwik8.$(CODEC) enwik8.$(CODEC).decoded
	@ $(DIR) cmp enwik8 enwik8.$(CODEC).decoded
	@ $(DIR) $(PERF) $$dir/cli encode --codec=$(CODEC) enwik9 enwik9.$(CODEC)
	@ $(DIR) $(PERF) $$dir/cli decode --codec=$(CODEC) enwik9.$(CODEC) enwik9.$(CODEC).decoded
	@ $(DIR) cmp enwik9 enwik9.$(CODEC).decoded
	@ $(DIR) rm -f enwik?.$(CODEC).decoded
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
