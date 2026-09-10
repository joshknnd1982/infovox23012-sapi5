#!/usr/bin/env python3
"""Remove the demonstration nag and the CrypKey licence gate from Ivx230nt.dll.

Infovox 230 v1.12 is abandonware: Telia Promotor's text-to-speech division became
Babel-Infovox AB and then disappeared, and there has been no way to buy a licence
for it since the late nineties.  An unlicensed engine still speaks -- that was
always the point of the demonstration mode -- but every few sentences it
interrupts itself to say

    "this is a demonstration of speech synthesis"

followed by the build year, in whichever of the twelve languages is selected.
This script turns that off in the binary, so the engine behaves as a fully
licensed copy.

WHERE THE NAG LIVES
-------------------
Each <lang>rules.ivx carries two lexicon entries that no ordinary text can reach,
because the engine upper-cases its input and these are looked up as whole words
beginning with Z:

    ZDEMO -> "this is a demonstration of speech synthesis"
    ZII   -> "Department of Speech Communication"      (KTH, where it was made)

The sentence splitter injects the literal token "zdemo" into the text stream, and
then a four-character year built from the compiler's __DATE__ string.  Two
counters in the synthesiser object drive it:

    [core+0x118]  the period: how many sentences between nags. 0 = licensed
    [core+0x116]  the countdown, initialised to -period

    1001b55f  cmp  word [esi+0x118], 0     ; licensed?
    1001b567  je   1001b612                ; yes -- nothing to say
    1001b56d  cmp  word [esi+0x116], 0
    1001b575  jge  1001b5b2
    1001b577  mov  eax, ["zdem"]           ; 100489a8
    1001b57c  mov  cx,  ["o\0"]
    1001b597  call 1001f170                ; inject the word
    ...
    1001b5c7  mov  dl, [100489a6]          ; '8'  \ the last two digits of
    1001b5cd  mov  al, [100489a5]          ; '9'  / "Nov  9 1998" at 1004939c
    1001b5ef  call 1001fa10                ; inject "1998"

    1001a840  set_demo(core, period):
    1001a844      mov  eax, [esp+8]
    1001a848      mov  [ecx+0x118], ax     ; period
    1001a84f      neg  eax
    1001a851      mov  [ecx+0x116], ax     ; countdown

LATENCY
-------
A third patch, unrelated to the nag, is applied here because it is the same kind
of edit.  The engine's text worker, having dealt with a message and found its
queue empty, sleeps for a tenth of a second before going back to its blocking
wait:

    10012132  lea  ecx, [esi+0x68]
    10012135  call 1000b8b0            ; anything left to do?
    1001213a  test eax, eax
    1001213c  jle  10012147
    1001213e  ...                      ; yes: round the loop again
    10012147  push 100                 ; no: sleep, then go back to waiting
    10012149  call Sleep

A posted message does not interrupt Sleep, so an utterance handed over during
that tenth of a second waits it out.  Measured over the pipe the SAPI5 engine
uses: 98.5 ms median from "speak" to the first sample.  With the constant at 5
it is 3.7 ms, and 6.4 ms at worst -- a 26-fold difference in the gap between a
keypress and hearing the line.

The thread does not spin as a result: after the sleep it returns to a
MsgWaitForMultipleObjects with no timeout, so the sleep runs once per message
rather than continuously, and an idle worker still uses no CPU at all.  5 rather
than 1 because at 1 the pipeline became erratic -- median 2.4 ms but a worst case
of 176 ms and whole utterances taking a second and a half.

The audio is unchanged.  All sixty voices were rendered before and after; the
six files that differ are the six that also differ between two runs of the same
build, by a handful of least-significant bits.  This engine is not sample-exact
with itself.

THE PATCHES
-----------
Three, all in place and all the same length as what they replace, so nothing in
the file moves and no header has to be rebuilt:

  1. set_demo is made to store a period of zero whatever it is passed.  The
     engine then takes its own "licensed" branch, which is the state a paid-for
     copy is in -- not a special case anyone has to maintain.

  2. The test at 1001b55f is made an unconditional jump past the whole
     injection.  This is belt and braces: the period is set through a dispatch
     table, so a path that reaches it without going through set_demo would
     otherwise still nag.  With this, no path can.

The licence machinery itself is left alone deliberately.  AcquireLicense still
runs, still fails to find a CrypKey authorisation or a Sentinel dongle, and still
returns "demonstration" -- and nothing now reads that answer.  Cutting it out
would mean patching the dispatch table it is called through, for no gain: it
touches no file we ship, and it costs about a millisecond once per process.

Usage:
    python tools/patch_engine.py bin/engine/Ivx230nt.dll
    python tools/patch_engine.py --check bin/engine/Ivx230nt.dll
"""

import argparse
import hashlib
import shutil
import sys

# (name, file offset, expected bytes, replacement bytes)
PATCHES = [
    (
        "set_demo stores a period of zero",
        0x019C44,
        bytes.fromhex("8b 44 24 08".replace(" ", "")),  # mov eax, [esp+8]
        bytes.fromhex("33 c0 90 90".replace(" ", "")),  # xor eax, eax ; nop ; nop
    ),
    (
        "sentence splitter never injects the demonstration text",
        0x01A95F,
        bytes.fromhex(
            "66 83 be 18 01 00 00 00 0f 84 a5 00 00 00".replace(" ", "")
        ),  # cmp word [esi+118h],0 ; je 1001b612
        bytes.fromhex(
            "e9 ae 00 00 00 90 90 90 90 90 90 90 90 90".replace(" ", "")
        ),  # jmp 1001b612 ; nop x9
    ),
    (
        "text worker idles for 5 ms rather than 100 ms",
        0x011547,
        bytes.fromhex("6a 64".replace(" ", "")),  # push 100
        bytes.fromhex("6a 05".replace(" ", "")),  # push 5
    ),
]

ORIGINAL_SHA1 = "bd36077569dcc95e344664e7f25b1a339996d8bb"


def state(data):
    """'original', 'patched', 'partly patched', or 'unrecognised'.

    Judged one patch at a time, so a file carrying an earlier version of this
    script's work can be brought up to date rather than rejected.
    """
    done = 0
    todo = 0
    for _, off, original, patched in PATCHES:
        if data[off:off + len(patched)] == patched:
            done += 1
        elif data[off:off + len(original)] == original:
            todo += 1
        else:
            return "unrecognised"
    if todo == len(PATCHES):
        return "original"
    if done == len(PATCHES):
        return "patched"
    return "partly patched"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dll", help="path to Ivx230nt.dll")
    ap.add_argument("--check", action="store_true", help="report state and exit")
    ap.add_argument("--backup", metavar="PATH", help="copy the original here first")
    args = ap.parse_args(argv)

    data = bytearray(open(args.dll, "rb").read())
    sha1 = hashlib.sha1(data).hexdigest()
    where = state(data)
    print("%s\n  sha1  %s\n  state %s" % (args.dll, sha1, where))

    if where == "unrecognised":
        print(
            "\nThis is not the Infovox 230 v1.12 NT engine this script knows.\n"
            "The stock file has sha1 %s." % ORIGINAL_SHA1,
            file=sys.stderr,
        )
        return 2
    if args.check:
        return 0
    if where == "patched":
        print("\nAlready patched; nothing to do.")
        return 0

    if args.backup:
        shutil.copy2(args.dll, args.backup)
        print("  backup written to %s" % args.backup)

    for name, off, expect, repl in PATCHES:
        if data[off:off + len(repl)] == repl:
            print("  0x%06X  %s (already)" % (off, name))
            continue
        assert data[off:off + len(expect)] == expect
        data[off:off + len(repl)] = repl
        print("  0x%06X  %s" % (off, name))

    open(args.dll, "wb").write(bytes(data))
    print("  sha1  %s  (patched)" % hashlib.sha1(data).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
