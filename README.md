# Infovox 230 v1.12 SAPI5

A native Windows SAPI5 wrapper for **Infovox 230 version 1.12** — Telia
Promotor's formant synthesiser from November 1998 — in 32-bit and 64-bit form,
so its sixty voices work in any program that speaks through Windows: NVDA, JAWS,
Narrator, Balabolka, Word, and the rest.

Twelve languages — American and British English, Danish, Dutch, Finnish, French,
German, Icelandic, Italian, Norwegian, Castilian Spanish, Swedish — each with a
Male, Female, Child, Giant and Zombie speaker. The installer offers them one at
a time: any set of languages, and any set of voices inside each.

Windows 7 SP1 through Windows 11, on 32-bit and 64-bit Windows alike. That range
is held to at build time: everything is compiled against the Windows 7 header
surface, so reaching for a newer API stops the build rather than shipping a
binary that a Windows 7 machine will not start (see 1.0.4 below, which is what
that rule is there to prevent).

Installers are on the [releases page](https://github.com/joshknnd1982/infovox23012-sapi5/releases).

## This is the 1.12 engine, not the 2.2 one

Infovox 230 went through several releases and the last one, 2.2, already has a
[SAPI5 wrapper of its own](https://github.com/joshknnd1982/infovox230-v22-sapi5).
This is the 1998 product: a different engine binary, different rule files,
different registry layout, and — as it turned out — different behaviour in three
places that decide whether it makes any sound at all. The two can be installed
side by side; nothing here collides with them.

The engine payload is the v1.12 distribution and nothing else, and that is
checkable rather than merely asserted:

```bash
python tools/verify_payload.py
```

It compares every file in `bin/engine` against the SHA-1 it had on the
distribution, and carries the v2.2 hashes too so that a file from the wrong
release would be named rather than just fail. The one file the two releases
share byte for byte is `darules.dll`, the Danish text normaliser, which was
never rebuilt between them.

## A full copy

Infovox 230 was sold with a per-language licence enforced by CrypKey and, on
Windows 95, a Rainbow Sentinel dongle. Unlicensed, the engine still spoke — but
every few sentences it interrupted itself to say *"this is a demonstration of
speech synthesis"*, in whichever of the twelve languages was selected, followed
by the year it was built. Telia Promotor's speech division became Babel-Infovox
AB and then closed; there has been no way to buy a licence since the nineties.

The nag turns out not to be a recording or a string in the dll. Every
`<lang>rules.ivx` carries two lexicon entries that ordinary text cannot reach,
because the engine upper-cases its input and looks these up as whole words
beginning with Z:

```
ZDEMO -> "this is a demonstration of speech synthesis"
ZII   -> "Department of Speech Communication"     (KTH, where it was made)
```

The sentence splitter injects the literal token `zdemo` into the text stream and
then a four-character year built from the compiler's `__DATE__` string, paced by
two counters in the synthesiser object. [`tools/patch_engine.py`](tools/patch_engine.py)
turns that off with two in-place edits, and a third that has nothing to do with
licensing but everything to do with latency — see below. All three are the same
length as what they replace, so nothing in the file moves.

The licence machinery is left running on purpose. It still looks for a CrypKey
authorisation, still fails, and still concludes "demonstration"; nothing reads
that answer any more. Cutting it out would mean patching the dispatch table it
is called through, for no gain.

| | stock engine | patched |
| --- | --- | --- |
| Sixteen sentences, American English Male | 35.9 s | 21.2 s |

The difference is the nag.

## What was different about 1.12

Four things, each of which was the whole reason for a silence or a crash.

**It reads `HKEY_LOCAL_MACHINE`.** The 2.2 engine reads its configuration from
`HKCU`; 1.12 reads `HKLM\Software\Telia Promotor\Infovox 230\1.1` — its
installer was a machine-wide one. It reaches the registry through exactly ten
imported ANSI functions, and those ten slots in its import address table are
rewritten to point at a tree in this process's memory
([`src/ivx_vregistry.cpp`](src/ivx_vregistry.cpp)), so on a normal start the
engine makes about a thousand configuration reads and none of them reach
Windows. Both hives are now served from the one tree.

```bash
Infovox23012Diag registry
```

**`IAudio::UnClaim` recursed until the stack was gone.** Telling the engine its
device has stopped, from inside the call in which the engine is releasing that
device, is how a real sound card behaves — and this engine's
`XNotifySink::AudioStop` unclaims the device again, which stops it again, until
the stack is gone. Its own trace calls the condition "AudioStop out of sync"; it
never gets far enough to say so. One notification is what it wants,
so it gets exactly one.

**`TextDataDone` does not mean the audio is finished.** It arrives within a
millisecond of handing the text over — it means the engine has copied the text,
not that it has said it. Waiting on it alone tore the engine down while it was
still working, and every single utterance came back with zero bytes: sixty
voices in the Windows list, all selecting cleanly, all silent. The end of an
utterance is `TextDataDone` **and** `ITTSNotifySink::AudioStop`, with a short
grace period for an utterance that turns out to have nothing to say.

**Two of its configuration values are inert.** 1.12's own installer writes
`DiphoneFile` and `MappingFile`, and the engine never reads either — established
by logging every registry read it makes across a whole start-up. They are not in
the voice table, not in the configuration utility, and not in the documentation,
because a setting that does nothing is worse than one that is missing. The
thirteen it does read are all there.

## Latency

The number that matters when someone is arrowing down a list is how long after
the keypress the first sample exists. Everything after that is playing rather
than waiting.

| | stock | now |
| --- | --- | --- |
| "speak" to first sample, median of 20 | 98.5 ms | **4.0 ms** |
| worst of 20 | 99.6 ms | 6.8 ms |
| synthesis speed | ~8× real time | ~100× real time |

Measured with `QueryPerformanceCounter` over the same pipe the SAPI5 engine uses
(`Infovox23012Diag latency`), because `GetTickCount`'s 15.6 ms tick reports a
flat "15 ms" for anything under a tick and would hide the whole effect.

Two things were in the way.

The engine's text worker, having dealt with a message and found its queue empty,
slept for a tenth of a second before returning to its blocking wait — and a
posted message does not interrupt `Sleep`, so an utterance handed over during
that tenth of a second waited it out:

```
10012132  lea  ecx, [esi+0x68]
10012135  call 1000b8b0            ; anything left to do?
1001213a  test eax, eax
1001213c  jle  10012147
1001213e  ...                      ; yes: round the loop again
10012147  push 100                 ; no: sleep, then go back to waiting
10012149  call Sleep
```

That constant is now 5. Not 1: at 1 the pipeline became erratic — median 2.4 ms
but a worst case of 176 ms and whole utterances taking a second and a half. The
thread does not spin as a result, because after the sleep it returns to a
`MsgWaitForMultipleObjects` with no timeout, so the sleep runs once per message
and an idle worker still uses no CPU.

The audio is unchanged by it. All sixty voices were rendered before and after;
the six files that differ are the six that also differ between two runs of the
*same* build, by a handful of least-significant bits. This engine is not
sample-exact with itself.

And the engine puts about a tenth of a second of inaudible padding before
everything it says — for every piece an utterance is split into, so a sentence
with a pause in it gets two. That is dropped rather than played, and every audio
position the engine reports is shifted back by however much was dropped, so word
highlighting still lands on the right word.

## Releases

**1.0.5** — an ellipsis no longer stops the voice for three seconds.

Reported as: "when the engine encounters a word followed by an ellipsis there is
a 3000 ms delay before the next word is spoken."

The engine pads about eight tenths of a second after every sentence end, and
keeps no memory of having just done it. `...` is three sentence ends in a row, so
it gets three lots of padding — in the middle of a sentence, where it is not
heard as a pause but as speech having stopped. Measured through SAPI5, `Hello...
world` against `Hello. world`:

| SAPI rate | full stop | ellipsis, before | ellipsis, now |
| --- | --- | --- | --- |
| −10 | 8906 ms | 24906 ms | **8904 ms** |
| −5 | 2872 ms | 8032 ms | **2868 ms** |
| 0 | 890 ms | 2490 ms | **889 ms** |
| +5 | 479 ms | 1338 ms | **476 ms** |
| +10 | 267 ms | 747 ms | **265 ms** |

A run of two or more full stops, question marks or exclamation marks is now
spoken as one of them, so an ellipsis costs exactly what a single full stop
costs, at every rate. `?!?!` and `!!!` were the same fault and are fixed with it.
`3.14` and `e.g.` are runs of one and are untouched, as is a run being spelled
out, where each mark is its own word. `CollapseRepeatedPunctuation=0` restores
the engine's own behaviour.

The single ellipsis character `…` had the opposite fault — the engine has never
heard of it and produced no pause at all, not even a short one — so it is now
given the full stop it stands for and sounds like the typed spelling.

*Why the punctuation and not the silence.* Shortening the silence on the way out
is the obvious fix, gives an exact millisecond bound, and does not work. The
engine reports where it is in the text **ahead** of the audio for it: measured, it
named the word after the pause while the pause itself was still arriving. Audio
removed after that is too late to correct a position already reported, and the
word events came out both in the wrong order and past the end of the stream —

```
word at source offset 9  ... stream offset 88000     <- "bravo", queued first
pause shortened by 48592 byte(s)
word at source offset 15 ... stream offset 46768     <- "charlie", earlier!
Speak finished, 70524 bytes written                  <- 88000 is past the end
```

which is a screen reader highlighting the wrong word. Never asking for the extra
pauses leaves every position the engine reports true and needs no correction at
all; the same trace now reads 0, 36800, 44160 against a 67916-byte stream, with
the source offsets of the text as the application wrote it.

The cost of choosing it is that the bound is a full stop rather than a number of
milliseconds: at the two slowest rate settings an ellipsis still pauses for
several seconds, because that is what an ordinary full stop does there.

**1.0.4** — the voices speak again on Windows 7.

Reported as: "the engine completely crashes SAPI when I select one of the
installed voices". The installation log said which Windows: `6.1.7601 SP1`.

1.0.3 opted the worker out of Windows 11's timer throttling by calling
`SetProcessInformation`, which arrived in Windows 8. Naming a function is what
puts it in a binary's import table, and an import the loader cannot resolve is
not a call that fails — it is a process Windows refuses to start. So on Windows 7
the worker was created and was gone before the first instruction of `main`: no
error code, no window, and nothing in the log, because the log is opened on that
first line. Every client that asked it to speak waited six seconds, started
another one, and after four tries gave up:

```
client: started the worker (C:\Program Files\Infovox23012\Infovox23012Server.exe)
client: the worker never became reachable on \\.\pipe\Infovox23012TTS_1
sapi: the worker did not answer; assuming 16000 Hz, 16-bit mono
```

Only `ivx_engine.cpp` names that API, and only the worker and the 32-bit
diagnostics link it — which is why on Windows 7 the engine still registered and
all fifteen voices still appeared in the voice list, and it was only speech that
was missing. Everything the reporter could see worked.

It is now looked up with `GetProcAddress` and skipped where it does not exist.
Windows 11 still opts out and still starts speaking in about 4 ms; Windows 7 and
8 have no such throttling, so there was never anything there to opt out of.

*So it cannot happen again.* The build now compiles against the Windows 7 SP1
header surface (`_WIN32_WINNT=0x0601`), which turns naming anything newer into a
compile error rather than a machine that will not speak. It found nothing else —
`SetProcessInformation` was the only one. Every shipped binary now imports
nothing newer than Vista, in either architecture, and `dumpbin /imports` on
`Infovox23012Server.exe` is how that is checked.

**1.0.3** — no click at the start of a voice, and no lost speech when changing
voice.

*The click.* Some voices start abruptly: Castilian Spanish goes from digital
silence to a quarter of full scale in half a millisecond, where American English
takes sixty. The step is the engine's own — the samples immediately before it are
exact zeros — and it is heard as a click before every utterance. A raised-cosine
fade over the first few milliseconds takes it off without softening the speech;
rise time to a tenth of peak goes from 0.25–0.50 ms to 2.4–9.8 ms, which is where
the voices that never clicked already sat. `OnsetFadeMs` in `[Settings]` changes
the length or turns it off. It turned out not to be only Spanish:
`Infovox23012SapiTest onsets` measures all sixty and flagged French and Italian
voices too.

*The lost speech.* Every time a SAPI host changes voice it makes a new engine
object, and each one opens its own connection to the worker — so arrowing through
the voice list is a burst of connections. The worker's accept loop created the
next pipe instance only after handing the accepted one to a thread, and in that
window the pipe name does not exist. A client arriving then does not get "busy",
which it would wait out; it gets `ERROR_FILE_NOT_FOUND`, which is
indistinguishable from "no worker is running", so it went off to start one and
slept a tenth of a second — or gave up, and the line was never spoken. The next
instance is now created before the accepted one is handed on, so the name is
never without a listener; the client retries quickly before concluding nobody is
home; and if the worker does go away before any of an utterance has been heard,
the client reconnects and asks again rather than dropping it.

Measured over 600 voice changes, each run against a freshly started worker:

| | utterances lost | delayed over 60 ms |
| --- | --- | --- |
| before | 9 | 6 |
| after | **0** | 9 |

Nothing is lost now; at worst a line waits while a worker starts.

*And a third thing found on the way.* Latency was bimodal — the median time from
asking for a line to the first sample of it was either 3.6 ms or 16.4 ms, with
nothing changed between runs. 16.4 ms is one Windows timer tick: Windows 11 power
throttling was quietly ignoring the worker's request for 1 ms timer resolution,
because a windowless background process is exactly what that throttling is aimed
at. The worker now opts out with `PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION`,
and eight consecutive runs measure 3.6–4.0 ms.

**1.0.2** — no more stutter at high speaking rates.

Reported as: "around 80% rate it begins to stutter — it says change-ge-ge".

Trimming the inaudible lead-in off each piece of speech moves the pointer to the
audio forward, and one thing downstream of it was still reading from where the
chunk had started. The run of quiet held back for the next flush therefore began
a lead-in too early, so the last tenth of a second of what had *just* been
written went out a second time. It only bit when the chunk carrying the lead-in
also ended in a gap, which depends on the words and on the rate — hence a bug
that seemed to appear above a particular speed.

Measured on the reported phrase, identical input, before and after:

| SAPI rate | repeated blocks before | after |
| --- | --- | --- |
| 4 | 10, each 90 ms | none |
| 6 | 7, each 67 ms | none |
| 8 | 5, each 54 ms | none |

Neither a duration check nor the byte accounting could see this: the same
*number* of bytes is written either way, just the wrong ones — a shifted read,
not a longer one. `Infovox23012SapiTest rates` now reads each utterance back and
looks for a stretch of audio that repeats one shortly before it, across four
sentences at all twenty-one rate steps. That is a harder test than it sounds,
because a formant synthesiser makes bit-exact periodic waveforms during a
sustained vowel; what separates a copy from a steady tone is that in a steady
tone the period *before* the match looks much the same, and in a copy it looks
nothing like it.

**1.0.1** — speech no longer stops mid-word above a quarter of the rate range.

The engine hands its audio over in whatever pieces its internal buffering
produces, and those boundaries are not sample boundaries: at some speaking rates
a chunk arrives that is an odd number of bytes, and at least once per utterance
that chunk is a single byte. `ISpTTSEngineSite::Write` refuses anything that is
not a whole number of samples — `E_INVALIDARG`, nothing written — and the SAPI5
engine treats a failed write as fatal to the utterance. Passed straight through,
one stray byte ended the utterance wherever it happened to fall: mid-word, near
the end, or after a few hundred bytes. Whether it fell at all depended on the
rate, which is why it looked like a rate bug. The odd tail is now kept and put
on the front of the next chunk, so what reaches `Write` is always whole samples.

Measured, same twelve-word sentence, before and after:

| SAPI rate | before | after |
| --- | --- | --- |
| 0 | 3.35 s | 3.35 s |
| 2 | 0.04 s | 2.59 s |
| 4 | 0.03 s | 2.07 s |
| 6 | 0.07 s | 1.59 s |
| 8 | 0.06 s | 1.27 s |

`Infovox23012SapiTest rates` is the regression check: it speaks the same
sentence at all twenty-one rate steps and fails if any of them is cut short.
It needs no administrator.

**1.0.0** — first release: sixty voices in twelve languages, 32- and 64-bit.

## Layout

| Path | What it is |
| --- | --- |
| [`tools/patch_engine.py`](tools/patch_engine.py) | the three edits to `Ivx230nt.dll`, with the disassembly they came from |
| [`tools/verify_payload.py`](tools/verify_payload.py) | proves every engine file is the v1.12 original |
| [`tools/gen_voice_table.py`](tools/gen_voice_table.py) | the sixty voices, read out of v1.12's own `SETUP.INS` |
| [`src/ivx_vregistry.cpp`](src/ivx_vregistry.cpp) | the in-memory registry and the import-table patch |
| [`src/ivx_engine.cpp`](src/ivx_engine.cpp) | loads and drives the engine; the capturing audio sink |
| [`src/ivx_catalog.cpp`](src/ivx_catalog.cpp) | the voice table, built-in and user-defined |
| [`src/ivx_server.cpp`](src/ivx_server.cpp) | the 32-bit worker |
| [`src/ivx_client.cpp`](src/ivx_client.cpp) | the client half of the pipe protocol |
| [`src/ivx_sapi_engine.cpp`](src/ivx_sapi_engine.cpp) | `ISpTTSEngine`: fragments in, audio and events out |
| [`src/ivx_sapi_tokens.cpp`](src/ivx_sapi_tokens.cpp) | publishing the voices to SAPI5 |
| [`src/ivx_config_app.cpp`](src/ivx_config_app.cpp) | `Infovox23012Config`, the configuration utility |
| [`src/ivx_config.rc`](src/ivx_config.rc) | its dialogs, where the tab order and the labelling live |
| [`tools/check_accessibility.py`](tools/check_accessibility.py) | walks those dialogs through MSAA |
| [`tools/ivx_probe.cpp`](tools/ivx_probe.cpp) | `Infovox23012Diag`, which drives the engine both ways |
| [`tools/ivx_sapi_test.cpp`](tools/ivx_sapi_test.cpp) | `Infovox23012SapiTest`, which drives it through SAPI5 |
| [`installer/Infovox23012SAPI.iss`](installer/Infovox23012SAPI.iss) | the installer |
| [`docs/README.txt`](docs/README.txt) | the documentation that ships with the product |
| [`bin/engine/`](bin/engine) | the engine payload |
| [`bin/orig/`](bin/orig) | the unpatched engine, the installer script the voice table came from, and the 1998 documentation |

## Out of process

Both SAPI5 engines, 32-bit and 64-bit, are thin clients of one 32-bit worker
that owns the engine. That is what lets 64-bit hosts use a 32-bit engine, and it
means an engine from 1998 that faults or wedges cannot take a screen reader down
with it.

## Install only what you want

The installer's components page lists the twelve languages and, inside each, its
five voices, and every one of them can be ticked or left out on its own. A
language is what costs disk space — it carries its own pronunciation rules, 17 KB
for Spanish to 334 KB for British English — and an unticked language is never
written. A voice costs only a line in the Windows voice list.

The choice is recorded in `installed.ini` and read back by
[`src/ivx_catalog.cpp`](src/ivx_catalog.cpp), which publishes those voices and no
others; the components, the files and that record are all generated from the same
mode table the engine is given, by
[`tools/gen_installer_voices.py`](tools/gen_installer_voices.py), so the
installer and the engine cannot come to disagree about what a voice is called.
Re-running setup starts from the previous choice and removes what you clear.

## Voices you make yourself

The five speakers of each language differ only in four numbers the engine
reads — base pitch, loudness contour, breathiness, and which of five vocal tract
shapes — so a user can define their own, up to 256 of them alongside the sixty.
`Infovox23012Config.exe` is the utility for it: every per-voice setting the
engine has, every engine-wide setting, and a Preview that speaks a voice before
it is kept.

It is built for people who cannot see the screen. The dialogs are resource
templates of standard Win32 controls; nothing is owner-drawn and nothing is
conveyed by colour, position or shape alone. Every control carries `WS_TABSTOP`,
the up-down spinners included, and each of those is given a name of its own in
code so it is never announced as an anonymous spin button. Every control is
immediately preceded in the template by the static text that names it, which is
how MSAA derives an accessible name. Everything the utility has to say goes in a
read-only edit box that takes focus and can be reviewed line by line, rather than
in a label a screen reader would skip.

Checked rather than claimed:

```bash
python tools/check_accessibility.py output\Infovox23012Config.exe
```

That runs the utility, walks all three dialogs through `IAccessible` — the same
interface NVDA and JAWS read — and through `GetNextDlgTabItem`, which is the tab
route itself. Across the three: **62 interactive controls, none without an
accessible name, none off the tab route.** The one control Windows skips is
"Stop speaking", because it is disabled until there is something to stop.

## What the engine can and cannot do

Measured against this engine rather than assumed.

Works: `\Spd=` rate, `\Pit=` pitch, `\Vol=` volume, `\mrk=` bookmarks,
word-position reporting, sentence boundaries, and phoneme input in both IPA and
the engine's own alphabet — which is what a pronunciation dictionary uses.

Does not work, and is handled here instead: `\Pau=` produces no pause at all
(`\Pau=1000\` gives audio the same length as no tag), so silence is generated as
exact PCM; the engine reports no viseme information, so none is offered; a run of
sentence ends stacks a pause for each one and `…` gets none at all, so a run is
spoken as one mark and `…` as a full stop.

Two more measured facts about it that decide how it has to be driven:

- The engine reports where it is in the text **ahead** of the audio for it — it
  names the word after a pause while the pause is still arriving. Anything that
  removes audio after the fact therefore cannot correct the positions already
  reported. See 1.0.5.
- It pads after every sentence end without noticing it has just done so, so the
  padding stacks; the padding after the last one is the trailing silence that
  gets trimmed.

Two more things worth knowing:

- A control tag changes engine state **permanently**, across utterances. Every
  utterance therefore states its rate, pitch and volume in full rather than
  relying on what the last one left behind.
- Loudness is handed to the audio *device* (`IAudio::LevelSet`), not applied to
  the samples. A capture sink that only stores the level makes every volume
  control a silent no-op, so the sink scales the PCM itself.

## Building

```bash
build_all.bat
```

Needs Visual Studio 2022 (or the Build Tools), CMake 3.20+, and Inno Setup 6 for
the installer. Output lands in `output\`, staged in the layout the installer
expects, with `Infovox23012SAPI_Setup.exe` beside it.

Two files in the tree are generated. Neither is regenerated by the build; run
them by hand after changing the mode table:

```bash
python tools/gen_voice_table.py       # src/ivx_voices.inc, the engine's table
python tools/gen_installer_voices.py  # installer/voices.iss, the installer's
```

## Where this came from

The architecture — a self-registering SAPI5 COM server, a 32-bit worker so
64-bit hosts can reach a 32-bit engine, one log file shared by every component,
an Inno Setup installer that registers both bitnesses — comes from the
**BestSpeech SAPI5 wrapper** by way of the **Infovox 230 v2.2 wrapper**:

- [gozaltech/bstspeech-sapi](https://github.com/gozaltech/bstspeech-sapi) — the original
- [joshknnd1982/BstSpeech-sapi](https://github.com/joshknnd1982/BstSpeech-sapi) — the fork it grew from
- [joshknnd1982/infovox230-v22-sapi5](https://github.com/joshknnd1982/infovox230-v22-sapi5) — the v2.2 wrapper

Everything specific to version 1.12 is new: the HKLM registry root, the audio
notification recursion, the completion condition, the mode table taken from
v1.12's own installer, the demonstration removal, and the latency work.

## Licence

GPL; see [LICENSE](LICENSE). The Infovox 230 engine itself is not part of this
project — it was made by Telia Promotor AB, whose text-to-speech division became
Babel-Infovox AB; both are long defunct.
