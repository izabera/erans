enumerative rANS
================

tldr:
-----

- adaptive rANS variant
- single pass streaming encoder
- 256 symbols
- nice data structures

try it:

```
make cli
make roundtrip
```

The CLI also supports Nayuki's reference arithmetic coders using the same
outer frame format and 16MiB input blocks. The static arithmetic path uses the
same compact histogram trailer as erans, while the adaptive arithmetic path
uses Nayuki's EOF symbol because the outer frame does not store raw block
sizes:

```
./cli --codec erans encode input output.erans
./cli --codec nayuki-static encode input output.arith-static
./cli --codec nayuki-adaptive encode input output.arith-adaptive
```

Use the same `--codec` value when decoding one of these files.


----------


any sequence of N symbols from an alphabet S can be represented as:
- a histogram of a multiset (how many times each symbol appears)
- the permutation index of the multiset (a value up to the multinomial
  coefficient)

enumerative rANS is an efficient practical implementation of a rANS variant
that encodes the exact histogram and the permutation index, for a 256 symbol
alphabet (bytes), and a reasonable max block size of 2^24 symbols (16MiB).
(this is purely a limitation of this implementation: there is no size limit in
the algorithm itself).

the encoder is single pass, and fully streamable.

it achieves the [optimal bound of enumerative coding][cover], conditional for a
prescribed histogram: the compressed size approaches the log of the multinomial
coefficient, strictly less than the shannon entropy for the same data.


----------


the core idea is not exactly revolutionary: we just encode each symbol against
the distribution of symbols so far, *including* the current position (excluding
it has all kinds of problems related to encoding zeros).

```
# c_i(s) = count of symbol s up to position i (including i)
p_i(s) = c_i(s) / i

# the ideal code length for a symbol in this scheme
-log2(p_i(s)) = log2(i) - log2(c_i(s))

# sum from 1 to N
L = ∑ log2(i) - ∑ log2(c_i(s))
```

the multinomial coefficient satisfies
```
M = N! / ∏ n_s! = ∏ (i / c_i(s))
```

the second form follows because for each symbol s, the values c_i(s) when s
appears at position i enumerate 1, 2, ..., n_s.  when grouped by symbol, the
product over all positions is ∏ n_s!.

```
# taking logs
log2(M) = ∑ log2(i) - ∑ log2(c_i(s)) = L
```

so the ideal code length equals log2 of the multinomial coefficient.

by playing with stirling's approximation, this can be shown to be strictly
lower than NH bits (N times the shannon empirical entropy).

for an alphabet of size K:

```
# stirling's approximation
log2(n!) = n log2(n) - n log2(e) + log2(2π n)/2 + O(1/n)

# applied to the log of the multinomial coefficient
∑ log2(n_s!) = ∑ n_s log2(n_s) - N log2(e) + (K/2) log2(2π) + (1/2) ∑ log2(n_s)

# the N log2(e) terms cancel out
log2(M) = N log2(N) - ∑ n_s log2(n_s)
        + log2(2π N)/2 - (K/2) log2(2π) - (1/2) ∑ log2(n_s)

# with p_s = n_s / N and  H = -∑ p_s log2(p_s)
∑ n_s log2(n_s) = N log2(N) + N ∑ p_s log2(p_s) = N log2(N) - NH
∑ log2(n_s)     = K log2(N) + ∑ log2(p_s)

# the N log2(N) terms cancel too
log2(M) = NH - ((K-1)/2) log2(N) - ((K-1)/2) log2(2π) - (1/2) ∑ log2(p_s) + O(K/N)
        = NH - ((K-1)/2) log2(N)                                          + O(1)

# (for fixed K, the log(2π) and ∑ log2(p_s) terms are absorbed into O(1))
```

ultimately this is just a perfectly adaptive rANS.  obviously there is no free
lunch, and the minimal description length still has to pay for the histogram.

similar approaches have been [known for ages][witten], mostly with arithmetic
encoding, but they're generally considered too expensive for practical uses.
with rANS instead of AC, and with a couple of modern optimisations, the result
is an encoder that's efficient enough to actually use...  *almost* :3



the standard two pass encoder
-----------------------------

a standard rANS encoder first calculates the histogram, quantizes and lightly
"massages" the frequencies (they'll be stored as multiples of 1/2^k, and they
are adjusted to sum up to a power of 2 to make the maths more efficient), then
traverses the stream again and encodes the symbols.  symbols with probabilities
below 1/2^k either get assigned probability 1/2^k or some special encoding.

```
def rans_encode(data):
    # calculate final frequencies
    counts = [0] * 256
    for s in data:
        counts[s] += 1
    M = len(data)

    # optionally make frequencies sum up to a power of 2

    # encode backwards
    state = 1
    for s in reversed(data):
        c = sum(counts[:s])
        f = counts[s]
        state = (state // f) * M + (state % f) + c
    return state, counts

def rans_decode(state, length, counts):
    M = sum(counts)
    # build reverse lookup table
    sym_at = []
    for s in range(256):
        sym_at.extend([s] * counts[s])

    out = []
    for _ in range(length):
        slot = state % M
        s = sym_at[slot]
        c = sum(counts[:s])
        f = counts[s]
        state = (state // M) * f + slot - c
        out.append(s)
    return out
```

the encoding overhead from this quantization scales with N * the KL divergence
from the real distribution, but this is usually rather tiny in practice.



single pass encoding
--------------------

instead of precomputing the distribution, we can encode and maintain the running
counts directly in one pass:

```
def erans_encode(data):
    counts = [0] * 256
    state = 1
    for M, s in enumerate(data, 1): # M starting at 1
        counts[s] += 1              # <--- increment first!  important!
                                    # the encoder must use the same prefix counts the
                                    # decoder will see when it walks back from the end
        c = sum(counts[:s])
        f = counts[s]
        state = (state // f) * M + (state % f) + c
    return state, counts
```

this is the bignum form: state grows without bound as we encode.  the streaming
version that emits bytes as we go is described below; the renorm range needs to
shift with M because, unlike standard rANS, M changes every step.

the encoder reads forward and accumulates prefix counts, i.e. counts of symbols
from the start up to and including the current position.  this means encoding
is a single forward pass over the input, it never needs to seek back.

the decoder receives the final counts, and reconstructs the bytes from the cdf.
it then walks from the end of the stream back to the start, in lifo fashion: at
the first decoding step it produces the last input symbol.

note: the encoder processes position i and needs the prefix counts including
symbol s at position i itself.  the decoder, walking backward, sees those same
counts before it dec's the symbol off, so the count in the encoder must be
bumped before computing c and f.

```
def erans_decode(state, length, total):
    counts = list(total)
    out = [None] * length
    for M in range(length, 0, -1):
        slot = state % M
        acc = 0
        for s in range(256):
            if acc + counts[s] > slot:
                c = acc
                f = counts[s]
                break
            acc += counts[s]

        state = (state // M) * f + slot - c
        out[M - 1] = s   # decoder emits last symbol first
        counts[s] -= 1
    return out
```



streaming
---------

(this uses a structure i'm calling a shrub, see below for details).

we want state in a fixed range, with the overflow going out as bytes.  the
post-encode invariant is state in [M, 256*M).  the C step

    state' = (state // f) * M + (state % f) + c

lands in this range iff the pre-encode state is in [f, 256*f).

coming out of the previous step, state is in [M-1, 256*(M-1)).  shifting bytes
out while state >= 256*f handles the upper bound.  the lower bound is automatic
when f <= M-1, since state >= M-1 >= f.

the one boundary is f = M (all symbols seen so far are this one).  this can
only happen during a contiguous prefix of the encoder pass (which, since the
encoder reads forward, is also a contiguous prefix of the data) and during it:

  - c = 0 (only one symbol exists in the shrub)
  - state stays at 1, since (1 // M) * M + (1 % M) + 0 = 1
  - the renorm condition state >= 256*M never fires at state = 1

so the formula degenerates to a no-op on its own, no special case needed.

```
def erans_encode_streaming(data):
    shrub = Shrub()
    state = 1
    encoded = []

    for M, s in enumerate(data, 1):
        shrub.inc(s)
        c, f = shrub.sym2cdf(s)

        while state >= f * 256:
            encoded.append(state % 256)
            state //= 256

        state = (state // f) * M + (state % f) + c

    # flush the remaining state byte by byte
    while state > 0:
        encoded.append(state % 256)
        state //= 256

    return shrub, encoded
```

note that this is fully streamable: input is read forward symbol-by-symbol,
output bytes come out as we go, and we don't need to know the length up front.
the only thing the encoder can't emit until the end is the histogram itself,
which is naturally appended as a trailer.

no length prefix is needed for the flush.  the encoder's final state is in
[M_final, 256*M_final), so when the decoder pulls bytes until state >= M_final,
that pull stops at exactly the encoder's final state.

the decoder mirrors this.  rANS is a stack: the encoder pushes bytes by
appending, so the decoder must pop from the tail (LIFO).

```
def erans_decode_streaming(shrub, encoded):
    state = 0
    N = shrub.total()
    out = [None] * N

    for M in range(N, 0, -1):
        # pull bytes (LIFO) to keep state >= M
        # on the first iteration this also reconstructs the flushed final state from the tail
        while state < M and encoded:
            state = (state << 8) | encoded.pop()

        slot = state % M
        c, f, s = shrub.cdf2sym(slot)
        shrub.dec(s)

        state = (state // M) * f + (slot - c)
        # decoder walks the input backward: first iteration recovers the last
        # input symbol, last iteration the first
        out[M - 1] = s

    return out
```

the f = M case mirrors on the decoder side too: state stays at 1 throughout the
all-same tail, slot = 1 % M = 1 always finds the only symbol with nonzero
count, and the C step gives state = (1 // M) * M + 1 - 0 = 1.

the renorm step allows erans to be streamable and implementable with finite
precision arithmetic, but it introduces some overhead.
to minimise it, the real code keeps the state in `[M*K, M*K*256)`.
this doesn't really change anything else, and all the maths is identical.

anyway, the pseudocode above looks incredibly slow.
thankfully, modern hardware can do better.



the shrub data structure
------------------------

a shrub is a tree that's short and wide.  in this specific case, it's also a
2 level tree with branching factor of 16 that can efficiently track a cdf.  it
supports incrementing/decrementing counts, going from slot to cdf, and back.
all its operations are branchless, and the runtime is O(1), data independent.

the lower level consists of 256 32bit counters divided in 16 groups of 16.
each group is a tiny cdf, entirely independent from the others.  with avx512
we can efficiently add a vector of 1's to another vector, and most importantly
we can apply a mask to it, so to increase the counts of all bytes from slots 9
to 15, we create a bitmask with those bits set, then use a single masked add to
increment them all at once.  we then do the same on the top level.

decrementing in the decoder is likewise straightforward.

there you go.  the whole thing is 2 adds/subs.  fenwick trees could never.

going from byte to cdf is just `top[byte >> 4] + bottom[byte & 0xf]`.

since the cdf is monotonic, we can go in the opposite direction with a simple
vector compare, which produces a mask of all indices <= target.

my fiancé came up with the name and it is perfect.



the integer division problem
----------------------------

during both encoding and decoding, M changes at every step.  luckily for us,
32-bit integer divisions [can be performed rather efficiently][lemire], at the
cost of a (pretty large) table of reciprocal values, which needs to be computed
only once.  this table is then traversed linearly by both the encoder and the
decoder, which effectively hides all cache misses.




the histogram
-------------

the number of possible histograms (K non-negative integers summing to N) is
C(N+K-1, K-1).  applying stirling as before gives the lower bound:
```
log2(C(N+K-1,K-1)) = (K-1) log2(e N/(K-1)) - log2(2π(K-1))/2 + O(K/N)
```

but in this implementation, the histogram is encoded with rice:

we have 256 counts in [0,M] that sum up to M, so their average is M/256.
we can pick B = max(0, ceil(log2(M)-8)), as the cut point for rice, store the
lower B bits of each count in binary, and 256 values in unary for the top parts,
each stored as k 0 bits followed by a single 1 bit.

when N is a power of 2, the sum of the top parts is <= 256, so we store at most
256 0s and 256 1s.  in other cases we just have fewer zeros.

since this implementation limits the maximum block size to 2^24 elements,
the histogram always takes at most 576 bytes.

this isn't quite the optimal theoretical lower bound, which would be ~555 bytes,
but it's not too bad for something that can be decoded with two ctz in a loop.




comparison with other encoders
------------------------------

encoding a quantized distribution takes O(1) space, while the exact histogram
takes O(log N).  erans always produces smaller codes than alternative encoders
if the divergence of using a quantized distribution (N * KL) exceeds the cost
of encoding the exact histogram, which will always happen with large enough N
since the cost of the histogram grows with O(log N).

in practice, the distribution of the data is rarely constant, so splitting in
smaller blocks is a common and very effective technique, which helps offset
the effects of the quantization...  at the cost of an additional O(N) space to
store the additional frequency tables.

on the other hand, standard rANS encoders have the advantage of being able to
update the frequency table on the fly, which may produce better results when
combined with a predictor that can approximate the local distribution.  erans
is completely not adaptable, so if your distribution does vary significantly,
you'd still pay the cost of all the histogram up to that point.

these days the most widely used entropy-0 encoder is probably fse, which is a
tANS encoder.  the distribution in tANS is completely static and not adaptable
at all, but decoding it is just stupid fast.

fse has a default table log of 12, up to a maximum of 15.  its default block
size is 32KB, up to a max of 2^32, rarely used in practice.

todo todo todo todo todo fill in this section, add benchmarks etc




is this worth it?
-----------------

maaaaaaaaaaaaaaaaaaaaaybe.  realistically, probably not.

the optimisations in erans make it practically viable, but fse decodes in a
handful of cycles per byte, which really is just too good.

but i thought it was interesting nonetheless, and i couldn't find this exact
idea in any existing implementations.



[cover]: https://ieeexplore.ieee.org/document/1054929
[witten]: https://web.stanford.edu/class/ee398a/handouts/papers/WittenACM87ArithmCoding.pdf
[lemire]: https://arxiv.org/abs/1902.01961
