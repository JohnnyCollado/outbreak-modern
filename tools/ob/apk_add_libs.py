"""Copy an aapt2-linked APK and add native libraries to it (tools/ob/android.sh package step).

Usage: apk_add_libs.py <in.apk> <lib dir> <out.apk>
<lib dir> holds <abi>/lib*.so (e.g. arm64-v8a/libps2EntryRunner.so); each goes in as lib/<abi>/<name>, deflated
(the manifest sets android:extractNativeLibs="true", so they need no page alignment). zipalign and apksigner run after.
"""
import os
import sys
import zipfile


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    src, lib_dir, out = argv[1], argv[2], argv[3]
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(out, "w") as zout:
        for info in zin.infolist():
            zout.writestr(info, zin.read(info.filename))
        added = 0
        for abi in sorted(os.listdir(lib_dir)):
            for name in sorted(os.listdir(os.path.join(lib_dir, abi))):
                if name.startswith("lib") and name.endswith(".so"):
                    zout.write(os.path.join(lib_dir, abi, name), f"lib/{abi}/{name}", zipfile.ZIP_DEFLATED)
                    added += 1
    print(f"apk_add_libs: {added} libraries -> {out}")
    return 0 if added else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
