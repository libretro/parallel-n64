#!/usr/bin/env python3
"""Exercise the real frame re-entry and VI-return code with ARM64 stop aliases.

--main-source accepts an older main.c for the negative control.
Runs with a native C compiler on Windows or Linux; no ARM instructions execute.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--main-source', type=Path,
                        default=root / 'mupen64plus-core/src/main/main.c')
    args = parser.parse_args()
    source = args.main_source.read_text()
    start = source.index('    if (l_setup_done) {')
    end = source.index('\n    emumode =', start)
    reentry = source[start:end]
    source = (root / 'libretro/libretro.c').read_text()
    start = source.index('int retro_return(bool just_flipping)\n{')
    end = source.index('\n}', start) + 2
    vi_return = source[start:end]
    test = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Select the production ARM64 aliases without cross-compiling this harness. */
#define __aarch64__ 1
#include "device/device.h"
#include "device/r4300/r4300.h"
static struct device g_dev;
static bool stop_stepping, frame_latched;
int frame_break;
static int l_setup_done = 1;
static int use_jit;
static int frames;
int *r4300_stop(struct r4300_core *cpu)
{
    return use_jit ? &cpu->new_dynarec_hot_state.stop : &cpu->stop;
}
''' + vi_return + r'''
void run_device(struct device *dev)
{
    (void)dev;
    assert(!*r4300_stop(&g_dev.r4300));
    retro_return(false);
    assert(stop_stepping && frame_break);
    assert(*r4300_stop(&g_dev.r4300));
    ++frames;
}
static int slice(void)
{
''' + reentry + r'''
    return -1;
}
int main(void)
{
    int mode, i;
    for (mode = 0; mode < 2; ++mode) {
        use_jit = mode;
        for (i = 0; i < 3; ++i) {
            stop_stepping = false;
            frame_break = 0;
            assert(slice() == M64ERR_SUCCESS);
        }
    }
    assert(frames == 6);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='frame-reentry-') as tmp:
        tmp = Path(tmp)
        cfile = tmp / 'test.c'
        cfile.write_text(test)
        for flags in ([], ['-DNEW_DYNAREC=4']):
            binary = tmp / 'test.exe'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', *flags,
                            '-I' + str(root / 'mupen64plus-core/src'),
                            '-I' + str(root / 'libretro-common/include'),
                            str(cfile), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print('ARM64 frame re-entry: interpreter-only and dynarec builds passed')


if __name__ == '__main__':
    main()
