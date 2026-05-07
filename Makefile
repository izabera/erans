# TARGET = znver5
# TARGET = diamondrapids
TARGET = native

CXX = clang++
MCAFLAGS = -mcpu=$(TARGET)
CXXFLAGS = -march=$(TARGET) -std=c++23 -O3 -ffast-math -Wall -Wextra -Wno-psabi -ggdb3 -flto
LDFLAGS = -flto -static
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

cli: shrub.o erans.o cli.o lemire.o utils.o

roundtrip: cli enwik8 enwik9
	rm -f enwik*.erans*
	perf stat ./cli encode enwik8 enwik8.erans
	perf stat ./cli encode enwik9 enwik9.erans
	perf stat ./cli decode enwik8.erans enwik8.erans.decoded
	perf stat ./cli decode enwik9.erans enwik9.erans.decoded
	cmp enwik8 enwik8.erans.decoded
	cmp enwik8 enwik8.erans.decoded
	wc -c enwik*
	echo roundtrip ok

.PHONY: roundtrip

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
