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

        # renorm:
        # we need state_after = (state//f)*M + (state%f) + c
        # to be in [M, 256*M).
        # this is guaranteed if state_before is in [f, 256*f)
        limit = f * 256
        while state >= limit:
            b = state % 256
            encoded.append(b)
            state //= 256
            print(f"emitted: {b=} {state=}")

        # standard rans encoding step
        state = (state // f) * M + (state % f) + c
        print(f"encoding: {state=} {c=} {f=} {M=} {shrub=} {encoded=}")

    return state, shrub, encoded

def erans_decode_streaming(state, shrub, encoded):
    M = shrub.total()
    ptr = 0
    out = []

    while M > 0:
        # slot is always < M because we ensure state >= M
        slot = state % M
        c, f, s = shrub.cdf2sym(slot)
        shrub.dec(s)

        # standard rans decoding step
        state = (state // M) * f + (slot - c)
        out.append(s)

        print(f"decoding: {state=} {slot=} {c=} {f=} {M=} {shrub=} {out=}")

        M -= 1

        # renorm:
        # if state dropped below the new M, pull bytes until state >= M
        while state < M and ptr < len(encoded):
            b = encoded[ptr]
            state = (state << 8) | b
            print(f"pulled: {b=} {state=}")
            ptr += 1

    return ''.join(out)


if __name__ == "__main__":
    data = "this is some test data blah blah blah"

    print("bignum mode")
    state, shrub = erans_encode(data)
    print(f"encoded: {state=} {shrub=}")
    print("======================")
    result = erans_decode(state, shrub)
    print(f"decoded: {result=}")

    print("\n\n")
    print("streaming mode")
    state, shrub, encoded = erans_encode_streaming(data)
    print(f"encoded: {state=} {shrub=} {encoded=}")
    print("======================")
    result = erans_decode_streaming(state, shrub, encoded)
    print(f"decoded: {result=}")
