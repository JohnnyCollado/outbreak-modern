"""Check that Android .so files will load: every NEEDED library exists and every undefined dynamic symbol is defined
by one of them (tools/ob/android.sh verify step). Offline, against the NDK sysroot's stub libraries for the min API.

Usage: android_symcheck.py <ndk llvm bin dir> <sysroot lib dir for the API> <lib.so>...
A NEEDED library among the given .so files is checked against that file. Weak undefined symbols are allowed
(the loader leaves them null). Exit 0 = all resolve, 1 = something missing.
"""
import os
import subprocess
import sys


def run(tool, *args):
    return subprocess.run([tool, *args], check=True, capture_output=True, text=True).stdout


def needed(readelf, path):
    out = []
    for line in run(readelf, "-d", "--wide", path).splitlines():
        if "(NEEDED)" in line and "[" in line:
            out.append(line.split("[", 1)[1].split("]", 1)[0])
    return out


def defined(nm, path):
    names = set()
    for line in run(nm, "-D", "--defined-only", "--format=posix", path).splitlines():
        parts = line.split()
        if parts:
            names.add(parts[0].split("@")[0])
    return names


def undefined(nm, path):
    strong, weak = set(), set()
    for line in run(nm, "-D", "--undefined-only", "--format=posix", path).splitlines():
        parts = line.split()
        if len(parts) >= 2:
            (weak if parts[1] in ("w", "v") else strong).add(parts[0].split("@")[0])
    return strong, weak


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    bindir, sysroot, libs = argv[1], argv[2], argv[3:]
    readelf = os.path.join(bindir, "llvm-readelf.exe")
    nm = os.path.join(bindir, "llvm-nm.exe")
    ours = {os.path.basename(p): p for p in libs}
    bad = 0
    for lib in libs:
        deps = needed(readelf, lib)
        provided = set()
        missing_libs = []
        for dep in deps:
            path = ours.get(dep) or os.path.join(sysroot, dep)
            if not os.path.exists(path):
                missing_libs.append(dep)
                continue
            provided |= defined(nm, path)
        strong, weak = undefined(nm, lib)
        unresolved = sorted(s for s in strong if s not in provided)
        print(f"{os.path.basename(lib)}: NEEDED {', '.join(deps)}; {len(strong)} undefined (+{len(weak)} weak), "
              f"{len(unresolved)} unresolved, {len(missing_libs)} missing libraries")
        for dep in missing_libs:
            print(f"  missing library: {dep}")
        for sym in unresolved[:40]:
            print(f"  unresolved: {sym}")
        bad += len(unresolved) + len(missing_libs)
    print("symcheck: " + ("ok" if bad == 0 else f"FAIL ({bad})"))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
