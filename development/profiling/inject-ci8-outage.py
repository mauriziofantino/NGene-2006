"""Replace a bounded RF interval with deterministic uncorrelated CI8 noise.

This is an artificial fade, with sample count and subsequent bytes preserved.
It does not model a USB discontinuity, oscillator drift, or real multipath.
"""
import argparse
import hashlib
import json
import pathlib
import random

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=pathlib.Path)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--start', type=float, default=55.0)
p.add_argument('--duration', type=float, default=0.4)
p.add_argument('--length', type=float, default=70.0)
p.add_argument('--fs', type=float, default=8183800)
a = p.parse_args()
assert a.source.resolve() != a.output.resolve()
offset = round(a.start*a.fs)*2
count = round(a.duration*a.fs)*2
length = round(a.length*a.fs)*2
assert 0 <= offset < offset+count <= length <= a.source.stat().st_size
with a.source.open('rb') as source, a.output.open('xb') as output:
    left = length
    while left:
        block = source.read(min(left, 4*1024*1024))
        assert block
        output.write(block)
        left -= len(block)
    output.seek(offset)
    output.write(random.Random(731).randbytes(count))
metadata = dict(source=str(a.source.resolve()), output=str(a.output.resolve()),
                fs=a.fs, offset=offset, noise_bytes=count, bytes=length, seed=731,
                sha256=hashlib.file_digest(a.output.open('rb'), 'sha256').hexdigest())
a.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
print(json.dumps(metadata))
