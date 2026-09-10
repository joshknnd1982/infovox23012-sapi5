#!/usr/bin/env python3
"""Generate src/ivx_voices.inc, the built-in Infovox 230 v1.12 voice table.

WHERE THIS TABLE COMES FROM
---------------------------
The Infovox 230 engine has no voices of its own.  It reads its entire mode table
out of the registry at start-up, and whatever is written there is what it
offers -- which is why this wrapper can hand it a table from memory and never
touch the registry at all.  So the question "what are the sixty stock voices?"
is really "what did the original installer write?", and the answer is in
SETUP.INS, the compiled InstallShield script on the v1.12 distribution
(bin/orig/SETUP.INS).  Every number below was read out of it.

The script writes, under Software\\Telia Promotor\\Infovox 230\\1.1\\Modes, one
key per "<Language> <Speaker>" pair, for twelve languages and five speakers.
The values are ModeGUID, LanguageID, LibraryFile, PhSymFile, DiphoneFile,
MappingFile, LanguageFile, SpeakerName, SpeakerStyle, Gender, Age, Pitch,
Dynamic, Aspiration and FormantNo -- and of those the v1.12 engine reads all but
DiphoneFile and MappingFile, so those two are not carried here.  (That was
measured, not assumed: the engine's registry reads were logged for a whole
start-up and neither name appears.)

The speakers differ only in Gender, Age and the four synthesis parameters, and
most of those are the same in every language.  The exceptions are the
installer's own -- it tests the language name and writes a different number --
and they are spelled out in the tables below rather than smoothed over, because
they are what a 1998 fine-tuning pass concluded about these particular voices.

Regenerate with:

    python tools/gen_voice_table.py
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "src", "ivx_voices.inc")

# name, GUID language slot, LanguageID as the engine wants it, rule file,
# LibraryFile, PhSymFile. The slots are not contiguous -- 03, 0b and 0d are
# unused, and Castilian Spanish is 0c -- so they are written out rather than
# counted.
#
# Two languages carry something extra, and both are in the installer: Danish
# names darules.dll as its LibraryFile, a helper the engine loads to normalise
# Danish text, and Swedish names swphsym as its PhSymFile. Nothing else does.
LANGUAGES = [
    ("American English",  "00", "1033", "amrules.ivx", "",             ""),
    ("British English",   "01", "2057", "blrules.ivx", "",             ""),
    ("Danish",            "02", "6",    "darules.ivx", "darules.dll",  ""),
    ("Dutch",             "04", "19",   "durules.ivx", "",             ""),
    ("Finnish",           "05", "11",   "firules.ivx", "",             ""),
    ("French",            "06", "12",   "frrules.ivx", "",             ""),
    ("German",            "07", "7",    "gerules.ivx", "",             ""),
    ("Icelandic",         "08", "15",   "icrules.ivx", "",             ""),
    ("Italian",           "09", "16",   "itrules.ivx", "",             ""),
    ("Norwegian",         "0a", "20",   "norules.ivx", "",             ""),
    ("Castilian Spanish", "0c", "10",   "sprules.ivx", "",             ""),
    ("Swedish",           "0e", "29",   "swrules.ivx", "",             "swphsym"),
]

# speaker -> (GUID speaker slot, Gender, Age, Pitch, Dynamic, Aspiration,
#             FormantNo). Pitch and Dynamic are the defaults; PITCH_BY_LANGUAGE
#             and DYNAMIC_BY_LANGUAGE override them where the installer did.
SPEAKERS = [
    ("Male",   "00", "2", "30", 50, 32, "0", "0"),
    ("Female", "01", "1", "30", 73, 53, "0", "1"),
    ("Giant",  "02", "2", "30", 50, 25, "0", "2"),
    ("Child",  "03", "2", "6",  90, 57, "15", "3"),
    ("Zombie", "04", "2", "30", 50, 38, "0", "4"),
]

PITCH_BY_LANGUAGE = {
    "Female": {"Dutch": 75, "Finnish": 75, "Icelandic": 68},
}

DYNAMIC_BY_LANGUAGE = {
    "Male": {"Castilian Spanish": 30, "Italian": 30, "Finnish": 25, "German": 25},
    "Female": {"Danish": 51, "Finnish": 36, "Swedish": 47},
}


# SAPI5 wants a full LCID. The engine's LanguageID is sometimes a full LCID
# (1033, 2057) and sometimes a bare primary language id (10 = Spanish). Fill in
# SUBLANG_DEFAULT for the bare ones, which is the same rule NVDA's sapi4 driver
# uses, so a voice reports the same language whichever front end reads it.
def to_lcid(language_id):
    lid = int(language_id)
    return lid if lid > 0x3FF else 0x0400 | lid


# Display name in the Windows voice list. The engine's mode name already reads
# well ("American English Male"); the prefix is what makes it identifiable in a
# list next to other vendors' voices, and the version distinguishes it from the
# Infovox 230 v2.2 voices, which can be installed at the same time.
def display_name(mode_name):
    return "Infovox 1.12 " + mode_name


def c_str(s):
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ord(ch) < 0x20 or ord(ch) > 0x7E:
            raise ValueError("non-ASCII in %r; the engine's configuration is ANSI" % s)
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def modes():
    for language, lang_slot, language_id, rule_file, library, phsym in LANGUAGES:
        for speaker, spk_slot, gender, age, pitch, dynamic, aspiration, formant in SPEAKERS:
            pitch = PITCH_BY_LANGUAGE.get(speaker, {}).get(language, pitch)
            dynamic = DYNAMIC_BY_LANGUAGE.get(speaker, {}).get(language, dynamic)
            name = "%s %s" % (language, speaker)
            yield {
                "_name": name,
                "_language": language,
                "ModeGUID": "{c9c5eda0-7c89-11d0-03%s-%s0000000000}" % (lang_slot, spk_slot),
                "LanguageID": language_id,
                "LanguageFile": rule_file,
                "LibraryFile": library,
                "PhSymFile": phsym,
                "SpeakerName": name,
                "SpeakerStyle": "",
                "Gender": gender,
                "Age": age,
                "Pitch": str(pitch),
                "Dynamic": str(dynamic),
                "Aspiration": aspiration,
                "FormantNo": formant,
            }


def main():
    rows = sorted(modes(), key=lambda m: m["_name"])

    lines = [
        "// Generated by tools/gen_voice_table.py -- do not edit by hand.",
        "// The Infovox 230 v1.12 mode table (%d voices), as its own installer wrote it." % len(rows),
        "",
        "// clang-format off",
        "static const ivx::BuiltinVoice kBuiltinVoices[] = {",
    ]
    for m in rows:
        fields = [
            c_str(m["_name"]),                    # mode key name, and the engine's mode name
            c_str(display_name(m["_name"])),      # SAPI5 token name
            c_str(m["_language"]),                # what a custom mode's name must start with
            c_str(m["ModeGUID"]),
            c_str(m["LanguageID"]),
            "0x%04X" % to_lcid(m["LanguageID"]),
            c_str(m["LanguageFile"]),
            c_str(m["LibraryFile"]),
            c_str(m["PhSymFile"]),
            c_str(m["SpeakerName"]),
            c_str(m["SpeakerStyle"]),
            c_str(m["Gender"]),
            c_str(m["Age"]),
            c_str(m["Pitch"]),
            c_str(m["Dynamic"]),
            c_str(m["Aspiration"]),
            c_str(m["FormantNo"]),
        ]
        lines.append("    { " + ", ".join(fields) + " },")
    lines += ["};", "// clang-format on", ""]

    with open(OUT, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote %s (%d voices)" % (OUT, len(rows)))


if __name__ == "__main__":
    main()
