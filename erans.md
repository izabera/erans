enumerative rANS
================

any sequence of N symbols from an alphabet S can be represented as:
- a histogram of a multiset (how many times each symbol appears)
- the permutation index of the multiset (a value up to the multinomial
  coefficient)

enumerative rANS is an efficient practical implementation of a rANS variant
that encodes the exact histogram and the permutation index, for a 256 symbol
alphabet (bytes), and a reasonable max block size of 2^24 symbols (16MiB).

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
    for M, s in enumerate(reversed(data), 1):   # M starting at 1
        counts[s] += 1        # <--- increment first!  important!
                              # the encoder must use the same suffix counts the
                              # decoder will reconstruct from total - prefix
        c = sum(counts[:s])
        f = counts[s]
        state = (state // f) * M + (state % f) + c
    return state, counts
```

like with normal rANS, you can keep the state in [L, L*m) and renormalize/emit
bytes as you progress.

the backward traversal accumulates suffix counts, i.e. counts of symbols from
the current position to the end.

the decoder receives the final counts, and reconstructs the bytes from the cdf.

note: the encoder processes position i and needs the suffix counts from [i, N),
including symbol s at position i itself.  the decoder will need to derive the
same suffix counts from the total, so the count in the encoder must be bumped
before computing c and f.

```
def erans_decode(state, length, total):
    prefix = [0] * 256
    out = []
    for _ in range(length):
        # reconstruct suffix counts
        suffix = [total[i] - prefix[i] for i in range(256)]
        M = sum(suffix)

        slot = state % M
        acc = 0
        for s in range(256):
            if acc + suffix[s] > slot:
                c = acc
                f = suffix[s]
                break
            acc += suffix[s]

        state = (state // M) * f + slot - c
        out.append(s)
        prefix[s] += 1
    return out
```

the pseudocode above looks incredibly slow.
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
