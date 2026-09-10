#!/usr/bin/env python3
"""Check that every engine file shipped here came off the Infovox 230 v1.12 disc.

The engine payload in bin/engine is the 1998 product, unchanged apart from the
three documented edits tools/patch_engine.py makes to Ivx230nt.dll.  Nothing in
it comes from the Infovox 230 v2.2 release or from that engine's SAPI5 wrapper,
and this is what says so: the SHA-1 of every file as it was on the distribution,
recorded before anything was touched.

    python tools/verify_payload.py

Exit code 0 means every file matches.
"""

import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ENGINE = os.path.join(ROOT, "bin", "engine")

# SHA-1 of each file as it was unpacked from the Infovox 230 v1.12 distribution,
# before it was renamed to lower case and before Ivx230nt.dll was patched.
DISTRIBUTION = {
    "amrules.ivx": "1bf3f7b561cb130f96649d71d88afd626a5b2f8c",
    "blrules.ivx": "1a3fc52803d6fec3f701664e59aa0891d9a733c1",
    "darules.dll": "e4500f3a3f336959646629d747b7a9c12df8c91f",
    "darules.ivx": "e979f06bc623ebdc703b72624309655f8d3de933",
    "durules.ivx": "d085b8dfc9f6e2f87bce3f2e2946fd6f31683988",
    "firules.ivx": "70f00cc1498a67aa16eae8fc5c0402c0abc4077a",
    "frrules.ivx": "c643866fc6e7a6a39afadb087c73f1a8dfee4ec7",
    "gerules.ivx": "4cd85c4b20714455559246cfd28c36bbc9a97884",
    "icrules.ivx": "d928486f1d4f0abf5a699a1c16714e39e0cf2965",
    "itrules.ivx": "2cc2090ce1cc47dc154241342d92b6dd7bf3c27a",
    "norules.ivx": "996dfb5929dd89ffb37a306646162c7ebbeb5656",
    "sprules.ivx": "17fd12e7e8f2bfa7a95f76be8908a90d17d6e44e",
    "swrules.ivx": "5ba36f99216c1e1a143ff72d769384ea22bc5413",
    "sx32w.dll": "fbc8c2a9ba04381e6090e9dd6ee690c7492acb1d",
    # Not compared directly: patch_engine.py edits it. Both states are known.
    "ivx230nt.dll": None,
}

ENGINE_ORIGINAL = "bd36077569dcc95e344664e7f25b1a339996d8bb"
ENGINE_PATCHED = "70284e80512c3b9c88bb9326eff08251e8731e14"

# The same files in the Infovox 230 v2.2 release, so a file that came from there
# by accident is named rather than merely failing to match. darules.dll is the
# one file the two releases share byte for byte -- the Danish text normaliser
# was never rebuilt -- so it is not evidence of anything and is left out.
V22_RELEASE = {
    "6cbb2fc83b0e8f32440dee08a08fd3bb62090562": "v2.2 AMRULES.IVX",
    "a9520a6d5974ad2c03b38429bb074600b98a89e0": "v2.2 BLRULES.IVX",
    "c0476ae385409d445752290d9582a0dd8fbbd6a3": "v2.2 DARULES.IVX",
    "c8c27b789acdab3400413a81044380e06dd9bf22": "v2.2 Frrules.IVX",
    "bfd6b558f278524bbdb72825cc6b65d0dc6ac599": "v2.2 Gerules.ivx",
    "ce74ef678e5de3026802aa8ad2aff8d1a3a937f2": "v2.2 ICRULES.IVX",
    "ae03eea333ee7defc297003f250b4967250357d7": "v2.2 ITRULES.IVX",
    "8ed8d7ad594dea02cc7c3955186bd896a66a2426": "v2.2 Ivx230nt.dll",
    "8246005e6eda75f438701bad330405d4bc4fe69d": "v2.2 NORULES.IVX",
    "374a13394db6b6026d832f48b98959444a74ca2f": "v2.2 SPRULES.IVX",
    "c26473a08e199320560d5824d7907bdc7a8d4279": "v2.2 Swrules.ivx",
    "260835497c5d5e942420b05c87291e721073625b": "v2.2 Sx32w.dll",
    "75f2a718e54c1aedd4d485eb4ec28bc4a5c04256": "v2.2 duRULES.IVX",
    "714d2d0760bdc4d6513f0225c6c6d19553443139": "v2.2 fiRULES.IVX",
}


def sha1(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def main():
    if not os.path.isdir(ENGINE):
        print("no engine payload at %s" % ENGINE, file=sys.stderr)
        return 2

    problems = 0
    present = sorted(os.listdir(ENGINE))
    for name in present:
        path = os.path.join(ENGINE, name)
        if not os.path.isfile(path):
            continue
        key = name.lower()
        digest = sha1(path)

        if key not in DISTRIBUTION:
            print("%-16s %s  NOT PART OF THE ENGINE -- remove it" % (name, digest))
            problems += 1
            continue

        if key == "ivx230nt.dll":
            if digest == ENGINE_PATCHED:
                note = "v1.12 disc + the three documented patches"
            elif digest == ENGINE_ORIGINAL:
                note = "v1.12 disc, UNPATCHED -- run tools/patch_engine.py"
                problems += 1
            else:
                note = "NEITHER the v1.12 disc nor its patched form"
                problems += 1
        elif digest == DISTRIBUTION[key]:
            note = "v1.12 disc, byte for byte"
        else:
            note = "DOES NOT MATCH the v1.12 disc"
            problems += 1

        if digest in V22_RELEASE:
            note = "*** this is the %s ***" % V22_RELEASE[digest]
            problems += 1
        print("%-16s %s  %s" % (name, digest, note))

    missing = [n for n in DISTRIBUTION if n not in [p.lower() for p in present]]
    for name in sorted(missing):
        print("%-16s %s" % (name, "MISSING"))
        problems += 1

    print()
    if problems:
        print("%d problem(s)." % problems)
    else:
        print("All %d engine files are the Infovox 230 v1.12 originals." % len(DISTRIBUTION))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
