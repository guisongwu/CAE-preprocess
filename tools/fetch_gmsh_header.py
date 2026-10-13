#!/usr/bin/env python3
"""Fetch one pinned official header; no installation or system modification."""
import hashlib
from pathlib import Path
import sys
from urllib.request import Request, urlopen

VERSION = '4.12.1'
URL = 'https://raw.githubusercontent.com/gmsh-project/gmsh/gmsh_4_12_1/api/gmsh.h'
SHA256 = 'af694b54a7b166e93c26f4afa4485f9fe22d3bc60287a535f6ffe5387d1b9f94'
DESTINATION = Path(__file__).resolve().parents[1]/'.deps'/f'gmsh-{VERSION}'/'include'/'gmsh.h'


def verify(data):
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError('Header SHA256 mismatch; refusing to save')
    if f'#define GMSH_API_VERSION "{VERSION}"'.encode() not in data:
        raise RuntimeError('Header API version mismatch; refusing to save')


def main():
    if DESTINATION.exists():
        verify(DESTINATION.read_bytes())
        print(f'Already verified: {DESTINATION}')
        return
    request = Request(URL, headers={'User-Agent': 'cae-preprocess-demo/0.1'})
    with urlopen(request, timeout=45) as response:
        data = response.read(1_000_000)
    verify(data)
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation also protects against a concurrent fetch.
    with DESTINATION.open('xb') as output:
        output.write(data)
    print(f'Saved: {DESTINATION}\nSHA256: {SHA256}')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(1)
