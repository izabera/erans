# TARGET = znver5
# TARGET = diamondrapids
TARGET = native

CXX = clang++
MCAFLAGS = -mcpu=$(TARGET)
CXXFLAGS = -march=$(TARGET) -std=c++23 -O3 -ffast-math -Wall -Wextra -Wno-psabi -ggdb3

# add a horrible probe because clang is a bit too conservative sometimes
ZMM_PROBE = $(shell echo 'void f(i32x16 &p){p+=p;}' | \
  $(CXX) $(CXXFLAGS) -include types.hpp $(1) -S -x c++ - -o - | grep -c zmm)
ifeq ($(call ZMM_PROBE,),0)
  ifneq ($(call ZMM_PROBE,-mprefer-vector-width=512),0)
    CXXFLAGS += -mprefer-vector-width=512
  endif
endif

cli:

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
	rm -rf *.[os] *.mca
.PHONY: clean
