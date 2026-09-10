Infovox 230 v1.12 SAPI5 Voices
==============================

Sixty voices in twelve languages, available to any Windows program that uses
speech: NVDA, JAWS, Narrator, Balabolka, Microsoft Word, and anything else that
speaks through the Windows speech interface (SAPI 5).

Languages: American English, British English, Danish, Dutch, Finnish, French,
German, Icelandic, Italian, Norwegian, Castilian Spanish, Swedish.
Speakers, in every language: Male, Female, Child, Giant, Zombie.

The engine is Infovox 230 version 1.12, made by Telia Promotor AB and released
in November 1998. This is a full copy: the demonstration mode is gone and there
is no licence to install or expire. See "The demonstration mode" below.

You choose which voices to install; only the ones you chose are here. See
"Choosing what is installed".


Checking it works
-----------------

Start menu, under "Infovox 230 v1.12":

  Infovox 230 v1.12 Configuration  define voices of your own, and change every
                                   setting the engine has
  Speak a test sentence            speaks aloud with the first Infovox voice
  List the Infovox voices          prints every voice, its language and gender
  Refresh the voice list           republishes the voices after editing
                                   voices.ini
  Read me                          this file

If the voices do not appear in your program, close and reopen that program
first: most programs read the voice list only once, when they start.


The demonstration mode
----------------------

Infovox 230 was sold with a per-language licence, enforced by CrypKey and, on
Windows 95, a Rainbow Sentinel dongle. Without one the engine still spoke, but
every few sentences it interrupted itself to say "this is a demonstration of
speech synthesis" -- in whichever of the twelve languages was selected -- and
then the year it was built.

Telia Promotor's text-to-speech division became Babel-Infovox AB and then
closed; there has been no way to buy a licence for this product since the late
nineties. The engine shipped here has that interruption removed: three edits to
Ivx230nt.dll, each one byte-for-byte the same length as what it replaced, which
turn off the demonstration counter and the injection it drove.

The licence machinery itself is left in place and still runs. It still fails to
find an authorisation, still concludes "unlicensed", and nothing now reads that
answer. Nothing you install has to be authorised, kept alive or renewed.

The edits are described in full, with the disassembly they came from, in
tools/patch_engine.py in the source repository.


Choosing what is installed
--------------------------

The installer's "Select components" page offers the twelve languages, and inside
each language its five voices, one tick at a time. Nothing has to be taken as a
job lot: American English Male on its own is a legitimate installation, and so
is every voice of all twelve languages.

Two things follow from a tick.

A language carries its own pronunciation rules, and those are what take up room
-- from 17 KB for Castilian Spanish to 334 KB for British English, and about
1.6 MB for all twelve. A language you do not tick is not put on the disk at all.
The size of each is shown against it on that page. (The whole product, with
every language, is under 5 MB installed.)

A voice costs nothing but a name in the Windows voice list, because the five
speakers of a language differ only in four numbers and all five read the same
rule file. Ticking a language takes all five of its voices; open it and untick
the ones you do not want.

There are three ready-made choices on the same page, and you can start from one
and adjust it:

  All 60 voices, in all 12 languages
  The English voices only -- American and British, 10 voices
  American English Male and Female only

To change your mind later, run the installer again. It starts from what you
chose last time, and a language you clear is removed from the disk and its
voices from the Windows voice list.

What you chose is written to installed.ini in this folder, which is how the
speech interface knows which voices to publish. The file explains itself if you
open it. Deleting it publishes every built-in voice again -- but a voice whose
language was never installed has no rule file to speak from, so it would then be
a name in the voice list that cannot speak. Running the installer again is the
better way round.

One exception, for when you want a single voice back without running the
installer: a voice you name as a section in voices.ini is published whether or
not installed.ini lists it, as long as its language is installed. Naming a voice
is taken as asking for it. See "Adding your own voices by hand" below.


How it is put together
----------------------

The Infovox 230 speech engine is a 32-bit COM server written for Microsoft's
Speech API 3, the system that preceded SAPI 5. Rather than load it inside
whatever program is speaking, it runs in a separate background program,
Infovox23012Server.exe, which starts by itself the first time something speaks.
Two things follow from that:

  * 64-bit programs can use the voices even though the engine is 32-bit;
  * if the engine ever fails, it cannot take your screen reader down with it.

Two speech interfaces are installed, a 32-bit one and a 64-bit one, so programs
of either kind can use the voices. Both talk to the same background engine.

No SAPI 4 runtime is installed, present or required. The old speech system the
engine was written for is not used: the worker loads Ivx230nt.dll by path and
asks the dll's own entry point for what it needs.

The engine also reads none of its settings from the Windows registry. It expects
to find its rule-file paths and its whole voice table under
HKEY_LOCAL_MACHINE\Software\Telia Promotor\Infovox 230\1.1, and it reaches the
registry through exactly ten imported functions; those ten are pointed at a tree
that exists only in the worker's memory. On a normal start the engine makes
about a thousand configuration reads and not one of them reaches Windows. You
can see that for yourself:

  Infovox23012Diag registry

The only registry entries this product creates are the ones Windows speech
itself requires in order to list a voice at all.


What you can control
--------------------

From any speech program, in the usual way:

  Voice     any of the sixty
  Rate      the full range the engine supports, about 15 to 500 words a minute
  Pitch     about 30 to 250 hertz, starting from each voice's own pitch
  Volume    0 to 100

The engine reports where it is in the text as it speaks, so programs that
highlight the current word while reading will do so, and it accepts phonetic
input in both the international alphabet and its own -- which is what a
pronunciation dictionary uses.

Everything else -- which voices exist, what each one sounds like, how far the
rate and pitch controls reach, and the engine's own behaviour -- is set in the
configuration utility, described below.

Responsiveness, which matters most when arrowing quickly through text: from
asking for a line to the first sample of it existing is about 4 milliseconds,
and speech is synthesised around a hundred times faster than it is spoken, so
the engine is never what you are waiting for.

That took two changes. The engine's text worker, having dealt with a message and
found nothing more to do, slept for a tenth of a second before going back to
waiting -- and a request arriving during that tenth of a second waited it out.
Measured through the speech interface, that was 98 milliseconds from asking to
the first sample. The sleep is now five milliseconds, which makes it 4. And the
engine puts about a tenth of a second of inaudible padding before everything it
says, which is dropped rather than played.

Two things this engine cannot do, so you know not to look for them: it produces
no mouth-shape (viseme) information for talking-head animation, and its own
pause tag does nothing. Pauses asked for in speech markup are produced by
inserting real silence instead, which also makes their length exact.

One thing worth knowing about the sound: the engine surrounds every utterance
with inaudible padding -- about a tenth of a second before and eight tenths of a
second after. Left alone that is dead air before and after everything a screen
reader says, so both are trimmed. Pauses inside a sentence are left exactly as
the engine made them; only the run of silence at each end is dropped, and the
trailing one only once there is nothing further to speak.

Infovox23012Diag writes the engine's audio untrimmed, so a clip saved with it is
about nine tenths of a second longer than the same words through Windows speech.
That is the diagnostic showing you the raw engine output, not a fault.


The configuration utility
-------------------------

Start menu, Infovox 230 v1.12, "Infovox 230 v1.12 Configuration"; and on the
desktop too, if you asked the installer for an icon there.

This is where voices are made. The five speakers of each language differ only in
four numbers the engine reads -- base pitch, loudness contour, breathiness, and
which of five vocal tract shapes to use -- and those numbers are yours. You can
define as many as 256 voices of your own alongside the sixty built in, in any of
the twelve languages, and you can change the built-in voices as well.

The main window lists every voice. Choose one and:

  New voice        start a new voice, taking its settings from the one selected
  Change           alter the selected voice
  Duplicate        copy it under a new name, so the original is left alone
  Rename           give one of your own voices a different name
  Delete           remove one of your voices, or put a built-in one back as it
                   was made
  Preview          speak the voice, as it now stands

Preview is worth knowing about: it speaks the settings as they are at that
moment, before anything has been kept, so a voice can be listened to and
adjusted until it is right. It restarts the speech engine to do it, which takes
about a second, and a screen reader speaking through an Infovox voice will pause
while that happens.

Save writes the voices to a file. "Publish voices to Windows" is the separate
step that puts them in the Windows voice list where your programs will find
them; it needs administrator permission, and Windows asks for it at that point
rather than the utility demanding it for everything else. A program that is
already running will not see a new voice until it is restarted; nothing else
needs restarting, and the voice speaks the first time it is asked to.

Voices can be kept for just you (%LOCALAPPDATA%\Infovox23012SAPI\voices.ini) or
for everybody (voices.ini in the installation folder). "Just me" needs no
special permission and is what the utility starts with; for "All users", start
the utility with "Run as administrator".

Every per-voice setting the engine has is on the voice page:

  Pitch          base pitch. The engine works out the pitch in hertz as
                 3 x Pitch - 49 and clamps it to 30-250, so useful values run
                 from about 27 to 99. The box beside it shows the hertz as you
                 type
  Loudness       the loudness contour, 0 to 100. Higher is more forceful and
                 more strongly stressed. The engine calls it Dynamic
  Breathiness    0 to 100. The engine calls it Aspiration
  Vocal tract    which of five shapes. This changes the character of a voice
                 more than anything else here: it is what separates Male,
                 Female, Giant, Child and Zombie
  Based on       take another voice's settings first, then change these
  Language       which of the twelve, and what the voice reports itself as
  Gender, Age    what programs report; neither affects the sound
  Speaker name   the name the engine is given. It has to begin with the
                 language of the voice's rule file -- the engine silently drops
                 a mode whose name it cannot place, and that is a voice which
                 appears in Windows and says nothing, so this is checked and
                 corrected for you, with a note in the log
  Library file   data files the engine will look for. Only Danish uses one
  Phoneme file   (darules.dll); only Swedish names a phoneme symbol set

Engine settings covers everything that is not a property of one voice: whether
the padding at each end of an utterance is trimmed and at what level, whether
positions in the text are reported while speaking, how long a wedged engine is
waited for, how far the rate and pitch controls in your programs reach, the
sentence Preview speaks, and how much detail is written to the log.

Everything is kept when you close the utility -- it asks first, and asks whether
to publish, so nothing is written or announced to Windows behind your back.


Using it with a screen reader
-----------------------------

The utility was built to be driven without seeing it:

  * every control can be reached with the Tab key, and Shift+Tab back;
  * every control has a name of its own, taken from the text beside it, so
    nothing is announced as just "edit" or "combo box";
  * everything is a standard Windows control -- nothing is drawn by hand;
  * each number box takes the up and down arrow keys as well as typed digits;
  * anything the utility has to tell you appears in a read-only box you can put
    the cursor in and read line by line, not in a label that a screen reader
    would skip: the details of the selected voice, the status of the last thing
    you did, the file being edited, and the notes on each page;
  * alt with the underlined letter reaches every button and box directly;
  * when a preview starts, focus moves to the "Stop speaking" button, so the way
    to stop it is under your hands already;
  * nothing is said with colour, position or shape alone.

Checked rather than claimed: all three dialogs are walked through MSAA -- the
same IAccessible interface NVDA and JAWS read -- and through GetNextDlgTabItem,
which is the Tab route itself. Across the three, 62 interactive controls: none
without an accessible name, and none off the Tab route. The one control Windows
skips is "Stop speaking", because it is disabled until there is something to
stop. The check is tools/check_accessibility.py in the source repository, and it
can be re-run against the installed program.


Adding your own voices by hand
------------------------------

The utility writes an ordinary ini file, and you can write it yourself instead.
See voices.example.ini in this folder: copy it to voices.ini -- either here, or
in %LOCALAPPDATA%\Infovox23012SAPI\ for just yourself -- edit it, then use
"Refresh the voice list" from the Start menu. The utility keeps your comments
and anything in the file it does not recognise, so the two ways of working can
be mixed.

Pitch is worth one note, because the number is not in hertz: the engine works
out the pitch as 3 x Pitch - 49, and clamps the result to between 30 and 250
hertz, so useful values run from about 27 to 99. The built-in male voice uses
50, which is 101 hertz.

You can call your voice anything. Behind the scenes it is given a name starting
with its language, because the engine checks that and quietly ignores any voice
whose name it does not recognise; that renaming is done for you and does not
change what you or your programs see. The same check is applied to a SpeakerName
you write yourself, and to a built-in voice you have moved to another language,
so that neither can leave you with a voice that is in the list and cannot speak.
When it has to correct one, it says so in the log.

The same file's [Settings] section holds the engine-wide settings the utility's
Engine settings page writes. voices.example.ini lists all of them.

A section named after a built-in voice does two things: it changes that voice,
and it asks for it. So if the installer left out, say, American English Child
but you installed American English, adding

  [Infovox 1.12 American English Child]
  Pitch = 92

to voices.ini puts that voice back as well as raising its pitch. A section
naming a voice whose language was never installed does nothing, because there is
no rule file for it to speak from; install the language first.


Logs
----

Everything the voices do is logged to:

  %LOCALAPPDATA%\Infovox23012SAPI\infovox23012.log

One file, written by all three parts (the 32-bit interface, the 64-bit
interface, and the background engine), each line marked with which one wrote it,
so a single utterance can be followed all the way through. The installer's own
log is beside it as install.log.

The log is capped at 8 MB and rolls over to infovox23012.log.1.

To turn the detail up or down, use the configuration utility: Engine settings,
"Log detail". It writes a file called loglevel.txt in that same folder holding a
single number, which you can also write yourself:

  0  off
  1  errors only
  2  errors and warnings
  3  normal (the default)
  4  detailed -- every utterance, with the rate, pitch and voice used
  5  everything, including each setting the engine reads and each word boundary

Level 5 is large but is the one to use when reporting a problem. The setting is
read once when a program starts speaking, so restart the speaking program after
changing it.

You can also set the environment variable INFOVOX23012_LOG_LEVEL, which
overrides the file.

The engine has a log of its own, from 1998, which can be turned on by setting
INFOVOX23012_ENGINE_LOG=1 before the background program starts. It writes
IVX230_<pid>.LOG into the engine folder and traces the engine's own internals --
the mode table it built, the licence check, the audio device, every utterance.
It is verbose and rarely needed, but it is the only view there is of what the
engine believes about itself, and it is what most of this wrapper was worked out
from.


What changed
------------

1.0.1  Speech is no longer cut off at higher speaking rates.

       Above about a quarter of the rate range, words were cut off at the end
       of an utterance and sometimes in the middle. The engine hands its audio
       over in pieces whose boundaries are not sample boundaries, and at some
       rates one of those pieces is a single byte. Windows speech refuses a
       part sample outright, and that ended the utterance wherever the stray
       byte happened to fall. It is now carried over to the next piece.

       Check it on your own machine, with no administrator needed:
         Infovox23012SapiTest rates

1.0.0  First release: sixty voices in twelve languages, 32- and 64-bit.


Diagnosing a problem
--------------------

Infovox23012Diag.exe, in this folder, drives the engine two ways so a fault can
be placed:

  Infovox23012Diag list                    every voice this installation has
  Infovox23012Diag registry                confirms the engine reads no registry
  Infovox23012Diag speak out.wav "text"    drives the engine directly
  Infovox23012Diag raw out.wav "text"      directly, with nothing added to the
                                           text -- for trying a control tag
  Infovox23012Diag phonemes out.wav "..."  phonetic input, ipa or eng
  Infovox23012Diag worker out.wav "text"   drives it the way the speech
                                           interface does, through the
                                           background program
  Infovox23012Diag all outdir              one clip per installed voice
  Infovox23012Diag workerall outdir        the same, through the background
                                           program
  Infovox23012Diag latency [N]             how long from asking to the first
                                           sample, N times
  Infovox23012Diag stop                    stops the background program

If "speak" works but "worker" does not, the engine is fine and the problem is in
the connection to the background program. If neither works, the log will say
why. The 64-bit copies of these tools are in the x64 subfolder; the 64-bit one
can only go through the background program, which is precisely the path it needs
to prove.

"list" is also the quickest way to see what was installed: it prints the voices
this installation actually has, which is what your programs will be offered. If
a voice you expected is missing, the log says which of the two reasons it was --
not chosen when you installed, or chosen but with its rule file absent.

Infovox23012SapiTest.exe goes through Windows speech itself, which is the last
link in the chain:

  Infovox23012SapiTest list                voices as Windows reports them
  Infovox23012SapiTest say "text"          speaks aloud
  Infovox23012SapiTest speak out.wav       speaks to a file
  Infovox23012SapiTest all outdir --verbose
  Infovox23012SapiTest sandbox out.wav     speaks through Windows speech using a
                                           voice it registers for you alone and
                                           then removes. Needs no administrator,
                                           so it works before installing
  Infovox23012SapiTest rates               speaks the same sentence at all
                                           twenty-one speaking rates and checks
                                           none of them is cut short


Where this came from
--------------------

The architecture -- a self-registering SAPI5 COM server, a 32-bit worker so
64-bit programs can reach a 32-bit engine, one log shared by every component --
comes from the BestSpeech SAPI5 wrapper by way of the Infovox 230 v2.2 wrapper:

  https://github.com/gozaltech/bstspeech-sapi        the original
  https://github.com/joshknnd1982/BstSpeech-sapi     the fork it grew from
  https://github.com/joshknnd1982/infovox230-v22-sapi5   the v2.2 wrapper

Everything specific to version 1.12 is new, and none of the v2.2 engine is here:
the payload is the v1.12 release, and tools/verify_payload.py in the source
repository checks every file of it against the distribution.

Source for this project:

  https://github.com/joshknnd1982/infovox23012-sapi5


Licence
-------

The wrapper, the background program, the configuration utility and the tools are
free software under the GNU General Public License; see the LICENSE file in the
source repository.

The Infovox 230 speech engine itself is not part of this project. It was made by
Telia Promotor AB, whose text-to-speech division became Babel-Infovox AB; both
are long defunct.
