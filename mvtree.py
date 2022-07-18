#! /usr/bin/env python

# This is a quick hack to move jpeg files into
# the dir structure expected by the inkkplate
# vsmp

from pathlib import Path


def path_jpg(fname):
    f = str(fname)
    d1 = f[0:2]
    d2 = f[2:4]
    d3 = f[4:6]
    d4 = f[6:8]

    return Path(d1, d2, d3, d4, f)


files = Path('.').glob('*.jpg')
for file in files:
    target = path_jpg(file)
    print(file, target)
    target.parent.mkdir(parents=True, exist_ok=True)
    file.replace(target)
