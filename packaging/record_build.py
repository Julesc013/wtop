#!/usr/bin/env python3
"""Record observed CMake toolchain inputs without executing the payload."""
import argparse
import hashlib
import json
import os
import platform
import re
import xml.etree.ElementTree as ET
from pathlib import Path


def observed_sdk(build: Path, settings: dict, definitions: dict) -> tuple[str, list[str]]:
    """Resolve the SDK selected by the generated project, not the installed newest SDK."""
    values = set()
    sources = []
    for description, mapping in (("CMakeCache", settings), ("CMakeCCompiler", definitions)):
        value = mapping.get("CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION", "")
        if value:
            values.add(value); sources.append(description)
    for project in sorted(build.glob("wtop.vcxproj")):
        for element in ET.parse(project).iter():
            if element.tag.rsplit("}", 1)[-1] == "WindowsTargetPlatformVersion" and element.text:
                values.add(element.text.strip()); sources.append(project.name)
    for generated in sorted((build / "CMakeFiles").glob("*/CMakeSystem.cmake")):
        for value in re.findall(r'set\(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "([^"\r\n]+)"\)', generated.read_text(encoding="utf-8")):
            values.add(value); sources.append("CMakeSystem.cmake")
    if len(values) > 1:
        raise ValueError("generated build files disagree about the Windows SDK: " + ", ".join(sorted(values)))
    if not values:
        return "UNVERIFIED", sources
    value = values.pop()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+", value):
        raise ValueError("Windows SDK is not an exact observed four-part version")
    return value, sorted(set(sources))


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
    sdk, sdk_sources = observed_sdk(args.build, settings, definitions)
    observed = {
        "compiler": {key: definitions.get(key, "UNVERIFIED") for key in
                     ("CMAKE_C_COMPILER", "CMAKE_C_COMPILER_ID", "CMAKE_C_COMPILER_VERSION", "CMAKE_C_COMPILER_ARCHITECTURE_ID")},
        "linker": settings.get("CMAKE_LINKER", "UNVERIFIED"),
        "sdk": sdk,
        "sdk_sources": sdk_sources,
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
