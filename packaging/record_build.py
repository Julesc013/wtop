#!/usr/bin/env python3
"""Record observed CMake toolchain inputs without executing the payload."""
import argparse
import hashlib
import json
import os
import platform
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    cache = (args.build / "CMakeCache.txt").read_text(encoding="utf-8")
    compilers = sorted((args.build / "CMakeFiles").glob("*/CMakeCCompiler.cmake"))
    if len(compilers) != 1:
        parser.error("expected exactly one generated C compiler description")
    compiler_bytes = compilers[0].read_bytes()
    definitions = dict(re.findall(r'set\((CMAKE_[A-Z0-9_]+) "([^"\r\n]*)"\)', compiler_bytes.decode("utf-8")))
    settings = dict(re.findall(r'^([^#/:][^:\r\n]*):[^=\r\n]*=(.*)$', cache, re.MULTILINE))
    compiler_id = definitions.get("CMAKE_C_COMPILER_ID", "UNVERIFIED")
    observed = {
        "compiler": {key: definitions.get(key, "UNVERIFIED") for key in
                     ("CMAKE_C_COMPILER", "CMAKE_C_COMPILER_ID", "CMAKE_C_COMPILER_VERSION", "CMAKE_C_COMPILER_ARCHITECTURE_ID")},
        "linker": settings.get("CMAKE_LINKER", "UNVERIFIED"),
        "sdk": settings.get("CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION", definitions.get("CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION", "UNVERIFIED; inspect CMake configure log")),
        "runtime": "MSVC MultiThreaded Release target property; inspect PE dependencies independently" if compiler_id == "MSVC" else "UNVERIFIED; inspect PE runtime imports",
        "flags": {key: value for key, value in settings.items() if key.startswith("CMAKE_C_FLAGS") and not key.endswith("-ADVANCED")},
        "build_command": ["cmake", "--build", str(args.build), "--config", "Release", "--parallel", "2"],
        "generator": settings.get("CMAKE_GENERATOR", "UNVERIFIED"),
        "generated_compiler_description_sha256": hashlib.sha256(compiler_bytes).hexdigest(),
        "cmake_cache_sha256": hashlib.sha256(cache.encode("utf-8")).hexdigest(),
        "host": {"system": platform.system(), "release": platform.release(), "version": platform.version(),
                 "machine": platform.machine(), "runner_image": os.environ.get("ImageVersion", "unrecorded")},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(observed, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
