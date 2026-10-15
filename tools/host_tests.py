#!/usr/bin/env python3
"""Run the host-side unit tests for firmware/lib/core without a system C++ compiler.

`pio test -e native` needs gcc/clang on PATH. This machine has none, so this script bootstraps one:
  - a throwaway venv under .hosttest/ with the `ziglang` wheel (zig c++ is a self-contained compiler)
  - shallow clones of Unity (test framework) and ArduinoJson (header-only) into .hosttest/
then compiles lib/core + test/test_core and runs it. Everything lands in .hosttest/ (git-ignored).

Usage:  python tools/host_tests.py
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")
WORK = os.path.join(ROOT, ".hosttest")
VENV = os.path.join(WORK, "venv")
PY = os.path.join(VENV, "Scripts" if os.name == "nt" else "bin", "python" + (".exe" if os.name == "nt" else ""))


def run(cmd, **kw):
    print("+", " ".join(cmd))
    return subprocess.run(cmd, check=True, **kw)


def ensure():
    os.makedirs(WORK, exist_ok=True)
    if not os.path.exists(PY):
        run([sys.executable, "-m", "venv", VENV])
        run([PY, "-m", "pip", "install", "-q", "ziglang"])
    for name, url, args in (
        ("unity", "https://github.com/ThrowTheSwitch/Unity.git", []),
        ("ArduinoJson", "https://github.com/bblanchon/ArduinoJson.git", ["--branch", "v7.4.3"]),
    ):
        if not os.path.isdir(os.path.join(WORK, name)):
            run(["git", "clone", "-q", "--depth", "1", *args, url, os.path.join(WORK, name)])


def main():
    ensure()
    zig = [PY, "-m", "ziglang"]
    out = os.path.join(WORK, "out")
    os.makedirs(out, exist_ok=True)
    unity_o = os.path.join(out, "unity.o")
    if not os.path.exists(unity_o):
        run(zig + ["cc", "-c", os.path.join(WORK, "unity", "src", "unity.c"), "-o", unity_o])
    exe = os.path.join(out, "test_core" + (".exe" if os.name == "nt" else ""))
    core = [os.path.join(FW, "lib", "core", f) for f in sorted(os.listdir(os.path.join(FW, "lib", "core"))) if f.endswith(".cpp")]
    run(zig + ["c++", "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-nullability-completeness",
               "-I" + os.path.join(FW, "lib", "core"), "-I" + os.path.join(WORK, "ArduinoJson", "src"),
               "-I" + os.path.join(WORK, "unity", "src"),
               os.path.join(FW, "test", "test_core", "test_core.cpp"), *core, unity_o, "-o", exe])
    sys.exit(subprocess.run([exe]).returncode)


if __name__ == "__main__":
    main()
