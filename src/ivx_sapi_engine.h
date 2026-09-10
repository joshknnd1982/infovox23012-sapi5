#pragma once

// The SAPI5 engine object. One of these exists per voice a SAPI application has
// open; each owns its own connection to the worker, so two applications
// speaking at once cannot disturb each other.

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>

#include <string>
#include <vector>

#include "ivx_clsid.h"
#include "ivx_client.h"
#include "ivx_com.h"
#include "ivx_settings.h"

namespace ivx {
namespace sapi5 {

class __declspec(uuid(IVX_TTSENGINE_CLSID)) TtsEngine
    : public ISpTTSEngine,
      public ISpObjectWithToken {
public:
    TtsEngine();
    ~TtsEngine();

    void* interface_for(REFIID riid) noexcept
    {
        void* p = com::as_primary<ISpTTSEngine>(this, riid);
        return p ? p : com::as<ISpObjectWithToken>(this, riid);
    }

    STDMETHOD(Speak)(DWORD flags, REFGUID format_id, const WAVEFORMATEX* wave_format,
                     const SPVTEXTFRAG* fragments, ISpTTSEngineSite* site) override;
    STDMETHOD(GetOutputFormat)(const GUID* target_id, const WAVEFORMATEX* target_format,
                               GUID* output_id, WAVEFORMATEX** output_format) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* token) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** token) override;

private:
    // Text destined for the engine, plus a map from each character of it back to
    // where that character came from in the application's text. Control tags
    // occupy positions in the first and have no counterpart in the second, which
    // is how a word-boundary report from the engine becomes an offset the
    // application recognises.
    struct Run {
        std::wstring tagged;
        std::vector<uint32_t> source;
        int rate_step = 0;
        int pitch_step = 0;
        int volume_pct = 100;
        bool empty() const { return tagged.empty(); }
    };

    struct Action {
        enum Kind { SpeakText, Silence, Phonemes } kind = SpeakText;
        Run run;
        ULONG silence_ms = 0;
        bool ipa = false;
    };

    HRESULT ensure_format();
    HRESULT run_action(const Action& action, ISpTTSEngineSite* site);
    HRESULT write_silence(ULONG milliseconds, ISpTTSEngineSite* site);

    // The engine ends every utterance with about 0.8 seconds of inaudible
    // dither -- samples of plus or minus one. Passed straight through, that is
    // eight tenths of a second of dead air after every single thing a screen
    // reader says. feed_audio holds back any run of quiet samples and only
    // writes it out if real audio follows, so the padding inside a sentence is
    // preserved and the padding at the end is dropped.
    bool write_bytes(const void* data, unsigned long bytes, ISpTTSEngineSite* site);
    bool feed_audio(const void* data, unsigned long bytes, ISpTTSEngineSite* site);
    bool flush_quiet(ISpTTSEngineSite* site);
    void discard_quiet() { quiet_.clear(); }

    // Bytes per sample frame of the output format, never zero.
    unsigned block_align() const;

    // Eases the opening of a piece up from nothing, to take the click off
    // the voices that start abruptly. Repoints `data` at a scratch copy.
    void apply_onset_fade(const void*& data, unsigned long bytes);
    void emit_word(const Run& run, uint32_t tagged_index, unsigned long stream_offset,
                   ISpTTSEngineSite* site);
    void emit_bookmark(uint32_t number, unsigned long stream_offset, ISpTTSEngineSite* site);

    WorkerClient client_;

    // Everything the user has set that is not a property of one voice: whether
    // the trailing padding is trimmed and at what level, whether positions in
    // the text are reported, and how long a wedged engine is waited for. Read
    // from the catalogue this dll has already loaded, so it costs nothing.
    const EngineSettings& settings_;

    ISpObjectToken* token_ = nullptr;
    std::string mode_guid_;
    std::wstring voice_name_;

    FormatResponse format_ = {};
    bool have_format_ = false;

    // Live only for the duration of one Speak call.
    bool want_words_ = true;
    std::vector<std::wstring> bookmarks_;
    std::vector<BYTE> quiet_;

    // Trimming the lead-in off each piece the engine speaks: whether one is
    // still being skipped, and how much of it has been, which every audio
    // position reported for that piece has to be shifted back by.
    bool leading_ = true;
    unsigned long lead_dropped_ = 0;

    // The fade over the opening of each piece: how many samples of it are left,
    // how long it is in total, and somewhere to do it that is not the buffer
    // the worker reuses.
    unsigned long fade_remaining_ = 0;
    unsigned long fade_total_ = 0;
    std::vector<short> fade_scratch_;

    // Running totals for one Speak call, so that "everything the engine
    // produced was written once, dropped once, or is still held" can be
    // checked rather than assumed. See the end of Speak().
    unsigned long long received_ = 0;    // bytes arriving from the worker
    unsigned long long lead_total_ = 0;  // lead-in dropped, across all pieces
    unsigned long long generated_ = 0;   // silence this layer made itself

    // Up to one sample short of a whole one, held back from the end of a chunk
    // because the engine's chunk boundaries are not sample boundaries and the
    // host will not accept a part sample. See feed_audio().
    std::vector<BYTE> partial_;
    unsigned long stream_offset_ = 0;
    bool aborted_ = false;
};

}  // namespace sapi5
}  // namespace ivx
