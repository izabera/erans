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

        while state >= f * 256:
            b = state % 256
            encoded.append(b)
            state //= 256

        state = (state // f) * M + (state % f) + c

    # flush: drain state byte by byte.  the decoder will pull bytes back from
    # the tail until state >= M, which exactly recovers what we had here
    while state > 0:
        encoded.append(state % 256)
        state //= 256

    return shrub, encoded

def erans_decode_streaming(shrub, encoded):
    state = 0
    out = []

    for M in range(shrub.total(),0,-1):
        # rans is a stack: pop bytes in reverse order of emission (LIFO)
        # on the first iteration this also reconstructs the flushed state
        while state < M and encoded:
            state = (state << 8) | encoded.pop()

        slot = state % M
        c, f, s = shrub.cdf2sym(slot)
        shrub.dec(s)

        state = (state // M) * f + (slot - c)

        out.append(s)

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
    shrub, encoded = erans_encode_streaming(data)
    print(f"encoded: {shrub=} {encoded=}")
    print("======================")
    result = erans_decode_streaming(shrub, list(encoded))
    print(f"decoded: {result=}")
    assert result == data

    print("\n\n")
    print("round-trip tests")
    import random
    import time
    random.seed(0)
    cases = [
        ("empty",            ""),
        ("single",           "a"),
        ("two same",         "aa"),
        ("two diff",         "ab"),
        ("all same short",   "a" * 10),
        ("all same long",    "x" * 10000),                       # f == M every step
        ("two symbols",      "ab" * 500),
        ("ramp into new",    "a" * 50 + "b" * 50),               # long f==M prefix in reversed order
        ("binary",           "".join(random.choice("01") for _ in range(2000))),
        ("ascii",            "".join(chr(random.randint(32, 126)) for _ in range(5000))),
        ("full byte range",  "".join(chr(random.randint(0, 255)) for _ in range(5000))),
        ("skewed",           "".join(random.choice("aaaaaaaaab") for _ in range(2000))),
    ]
    for name, data in cases:
        # bignum
        bn_state, bn_shrub = erans_encode(data)
        assert erans_decode(bn_state, bn_shrub) == data, f"bignum failed: {name}"

        # streaming
        st_shrub, encoded = erans_encode_streaming(data)
        bn_bits = bn_state.bit_length()
        st_bits = 8 * len(encoded)
        # the decoder mutates the encoded list (pop), so measure first
        assert erans_decode_streaming(st_shrub, list(encoded)) == data, f"streaming failed: {name}"

        # both encoders must agree on the histogram
        assert bn_shrub.counts == st_shrub.counts, f"shrub mismatch: {name}"
        print(f"  {name:18s} N={len(data):5d}  bignum={bn_bits:6d}b  stream={st_bits:6d}b")

    print("\n\n")
    print("benchmarks (streaming mode)")
    def run_benchmark(name, data):
        start = time.perf_counter()
        shrub, encoded = erans_encode_streaming(data)
        enc_time = time.perf_counter() - start

        start = time.perf_counter()
        result = erans_decode_streaming(shrub, list(encoded))
        dec_time = time.perf_counter() - start

        assert result == data

        in_bytes = len(data)
        out_bytes = len(encoded)
        ratio = in_bytes / out_bytes

        enc_mbs = (in_bytes / 1024 / 1024) / enc_time if enc_time > 0 else 0
        dec_mbs = (in_bytes / 1024 / 1024) / dec_time if dec_time > 0 else 0

        print(f"  {name:18s} N={in_bytes:7d}  ratio={ratio:10.2f}x  enc={enc_mbs:6.3f}MB/s  dec={dec_mbs:6.3f}MB/s")

    # bignum version is too slow
    cases += [
        ("long random", "".join(chr(random.randint(0, 255)) for _ in range(1000000))),
        ("long skewed", "".join(random.choice("abcdefghij" + "a" * 90) for _ in range(1000000))),
    ]
    for name, data in cases:
        if len(data) >= 1000:
            run_benchmark(name, data)
