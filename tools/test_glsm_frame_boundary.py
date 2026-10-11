#!/usr/bin/env python3
"""Run the GLES3 regression through libretro.c's actual frame-boundary guards.

Requires a C compiler and EGL/GLES3 development libraries. Run from any
directory: python3 tools/test_glsm_frame_boundary.py
Pass --libretro-source FILE to verify the regression against an older revision.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--libretro-source", type=Path,
                        default=root / "libretro/libretro.c")
    args = parser.parse_args()
    source = args.libretro_source.read_text()
    start = source.index("static void glsm_exit(void)")
    end = source.index("/* RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE", start)
    # Remove only the surrounding HAVE_OPENGL/HAVE_OPENGLES closing #endif.
    functions = source[start:end].rstrip()
    assert functions.endswith("#endif")
    functions = functions[:-len("#endif")]
    with tempfile.TemporaryDirectory(prefix="glsm-frame-") as directory:
        directory = Path(directory)
        header = directory / "frame_boundary.h"
        header.write_text(functions)
        binary = directory / "regression"
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c99", "-DHAVE_OPENGLES",
            "-DHAVE_OPENGLES3", "-DHAVE_THR_AL", "-DHAVE_PARALLEL",
            f'-DGLSM_FRAME_BOUNDARY_SOURCE="{header}"',
            "-I" + str(root / "libretro-common/include"),
            str(root / "tools/test_glsm_gles3.c"),
            str(root / "libretro-common/glsym/glsym_es3.c"),
            str(root / "libretro-common/glsym/rglgen.c"),
            "-lEGL", "-lGLESv2", "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True,
                       env={**os.environ, "EGL_PLATFORM": "surfaceless"})


if __name__ == "__main__":
    main()
