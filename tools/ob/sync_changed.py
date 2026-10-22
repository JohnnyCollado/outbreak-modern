"""Copy generated sources into the runner tree, touching only files whose content changed.

usage: sync_changed.py DEST_DIR [--prune] SRC...

SRC is a file or a glob pattern (quoted, expanded here: ~5,700 generated files would overflow
the Windows command line). A file identical to the one already in DEST_DIR is left alone, so
its mtime stays and ninja does not recompile it. With --prune, files in DEST_DIR with a
source's extension but not among the sources are deleted (a function that left the recompile).
Prints one line of counts; exits 1 if a pattern matches nothing or two sources share a name.
"""
import filecmp
import glob
import os
import shutil
import sys


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    dest, rest = argv[0], argv[1:]
    prune = rest[0] == "--prune"
    if prune:
        rest = rest[1:]
    srcs = []
    for pat in rest:
        hits = sorted(glob.glob(pat))
        if not hits:
            sys.exit("sync_changed: no match for %s" % pat)
        srcs += hits
    os.makedirs(dest, exist_ok=True)
    copied = unchanged = pruned = 0
    names = set()
    for src in srcs:
        name = os.path.basename(src)
        if name in names:
            sys.exit("sync_changed: duplicate name %s" % name)
        names.add(name)
        dst = os.path.join(dest, name)
        if os.path.isfile(dst) and filecmp.cmp(src, dst, shallow=False):
            unchanged += 1
        else:
            shutil.copyfile(src, dst)
            copied += 1
    if prune:
        exts = {os.path.splitext(n)[1] for n in names}
        for name in os.listdir(dest):
            if os.path.splitext(name)[1] in exts and name not in names:
                os.remove(os.path.join(dest, name))
                pruned += 1
    print("copied %d, unchanged %d, pruned %d" % (copied, unchanged, pruned))


if __name__ == "__main__":
    main(sys.argv[1:])
