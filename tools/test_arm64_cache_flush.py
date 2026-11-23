#!/usr/bin/env python3
"""Check dirty ARM64 code publication, including the last page of each bitmap."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=root /
                        'mupen64plus-core/src/device/r4300/new_dynarec/arm64/assem_arm64.c')
    args = parser.parse_args()
    source = args.source.read_text()
    start = source.index('static void do_clear_cache(void)\n{')
    end = source.index('\n}', start) + 2
    test = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef unsigned int u_int;
#define TARGET_SIZE_2 18
static unsigned char code[1 << TARGET_SIZE_2];
static unsigned char seen[sizeof(code)];
static void *base_addr_rx = code;
static u_int needs_clear_cache[2];
static unsigned calls;
static void cache_flush(char *start, char *end)
{
    size_t offset = (unsigned char *)start - code;
    size_t size = end - start;
    assert(offset + size <= sizeof(code));
    memset(seen + offset, 1, size);
    ++calls;
}
''' + source[start:end] + r'''
int main(void)
{
    static const u_int patterns[] = {1, 0x80000000U, 0xc0000000U,
                                    0xffffffffU, 0x80000005U};
    unsigned p, byte, group;
    for (p = 0; p < sizeof(patterns)/sizeof(patterns[0]); ++p) {
        memset(seen, 0, sizeof(seen));
        for (group = 0; group < 2; ++group)
            needs_clear_cache[group] = patterns[p];
        do_clear_cache();
        for (byte = 0; byte < sizeof(code); ++byte)
            assert(seen[byte] == !!(patterns[p] & (1U << ((byte >> 12) & 31))));
        assert(!needs_clear_cache[0] && !needs_clear_cache[1]);
        calls = 0;
        do_clear_cache();
        assert(!calls);
    }
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='arm64-flush-') as tmp:
        tmp = Path(tmp)
        cfile = tmp / 'test.c'
        binary = tmp / 'test.exe'
        cfile.write_text(test)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', str(cfile),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('ARM64 dirty code publication passed')


if __name__ == '__main__':
    main()
