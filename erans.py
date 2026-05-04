#!/usr/bin/env python3

class Shrub:
    def __init__(self):
        self.counts = [0] * 256
    def __repr__(self):
        counts = {chr(s): self.counts[s] for s in range(256) if self.counts[s] > 0}
        return str(counts)
    def total(self):
        return sum(self.counts)
    def inc(self, s):
        self.counts[ord(s)] += 1
    def dec(self, s):
        self.counts[ord(s)] -= 1
    def sym2cdf(self, s):
        s = ord(s)
        c = sum(self.counts[:s])
        f = self.counts[s]
        return c, f
    def cdf2sym(self, target):
        c, f, s = 0, 0, 0
        for s in range(256):
            if c + self.counts[s] > target:
                f = self.counts[s]
                break
            c += self.counts[s]
        return c, f, chr(s)

def erans_encode(data):
    shrub = Shrub()
    state = 1

    for M, s in enumerate(reversed(data), 1): # M starting at 1
        shrub.inc(s)                          # increment first so count[s] > 0
        c, f = shrub.sym2cdf(s)               # use new counts
        state = (state // f) * M + (state % f) + c
        # print(f"encoding: {state=} {c=} {f=} {M=} {shrub=}")
    return state, shrub

def erans_decode(state, shrub):
    out = []
    for M in reversed(range(1, shrub.total()+1)): # M from length to 1
        slot = state % M
        c, f, s = shrub.cdf2sym(slot)             # deduce with current counts
        shrub.dec(s)                              # then decrement
        state = (state // M) * f + slot - c
        # print(f"decoding: {state=} {slot=} {c=} {f=} {M=} {shrub=} {out=}")
        out.append(s)
    return ''.join(out)

def erans_encode_streaming(data):
    shrub = Shrub()
    state = 1
    encoded = []

    for M, s in enumerate(reversed(data), 1):
        shrub.inc(s)
        c, f = shrub.sym2cdf(s)

        # the post-encode state is in [M, 256*M)

        # for the encoding step    state' = (state//f)*M + (state%f) + c
        # to land in [M, 256*M), the pre-encode state must be in [f, 256*f)

        # coming from the previous step, state is in [M_prev, 256*M_prev) where M_prev = M - 1
        # upper bound: shift bytes out while state >= 256*f
        # lower bound: automatic when f <= M_prev = M - 1, since state >= M_prev >= f.

        # the one case that breaks is f == M (all symbols seen so far are this one):
        # then the symbol is fully predicted, the ideal code length is 0, and we just leave state alone
        if f < M:
            while state >= f * 256:
                b = state % 256
                encoded.append(b)
                state //= 256

            state = (state // f) * M + (state % f) + c

    return state, shrub, encoded

def erans_decode_streaming(state, shrub, encoded):
    M = shrub.total()
    out = []

    while M > 0:
        slot = state % M
        c, f, s = shrub.cdf2sym(slot)
        shrub.dec(s)

        # mirror of the encoder: skip the step entirely when f == M, since the encoder didn't change state for that symbol
        if f < M:
            state = (state // M) * f + (slot - c)

        out.append(s)
        M -= 1

        # rans is a stack: pop bytes in reverse order of emission (LIFO)
        while state < M and encoded:
            b = encoded.pop()
            state = (state << 8) | b

    return ''.join(out)


if __name__ == "__main__":
    data = "this is some test data blah blah blah"

    print("bignum mode")
    state, shrub = erans_encode(data)
    print(f"encoded: {state=} {shrub=}")
    print("======================")
    result = erans_decode(state, shrub)
    print(f"decoded: {result=}")
    assert result == data

    print("\n\n")
    print("streaming mode")
    state, shrub, encoded = erans_encode_streaming(data)
    print(f"encoded: {state=} {shrub=} {encoded=}")
    print("======================")
    result = erans_decode_streaming(state, shrub, encoded)
    print(f"decoded: {result=}")
    assert result == data
