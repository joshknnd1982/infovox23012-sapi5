#include "ivx_sapi_engine.h"

#include <algorithm>
#include <cmath>

#include "ivx_log.h"
#include "ivx_sapi_tokens.h"

namespace ivx {
namespace sapi5 {
namespace {

// A position in `Run::source` that came from a control tag rather than from the
// application's text.
constexpr uint32_t kNotFromSource = 0xFFFFFFFFu;

// Bookmark numbers start at one: measurement against the engine shows \mrk=0\
// is swallowed and never reported back.
constexpr uint32_t kFirstBookmark = 1;

int clamp(int v, int lo, int hi)
{
    return (std::max)(lo, (std::min)(hi, v));
}

bool is_word_char(wchar_t ch)
{
    return iswalnum(ch) != 0 || ch == L'\'' || ch == L'-';
}

std::string narrow(const std::wstring& s)
{
    if (s.empty()) {
        return std::string();
    }
    const int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr,
                        nullptr);
    return out;
}

// The marks the engine treats as the end of a sentence, and pauses after.
bool is_sentence_end(wchar_t ch)
{
    return ch == L'.' || ch == L'?' || ch == L'!';
}

// The single character for an ellipsis, which the engine has never heard of.
constexpr wchar_t kEllipsis = 0x2026;

// Appends text, escaping anything the engine would read as the start of a
// control tag, and records where each character came from.
//
// `collapse` folds a run of two or more sentence-ending marks down to the first
// of them, and spells the one-character ellipsis as the full stop it stands for.
//
// The engine pauses for about eight tenths of a second after every sentence end
// and keeps no memory of having just done it, so "wait..." is three sentence
// ends in a row and costs three pauses: two and a half seconds at the normal
// rate, and twenty-five at the slowest. In the middle of a sentence that is not
// heard as a pause but as speech having stopped.
//
// It is folded here, on the way in, rather than by shortening the silence on the
// way out -- which was tried first and does not work. The engine reports where
// it is in the text ahead of the audio for it: measured, it named the word after
// the pause while the pause itself was still arriving. Audio taken out after
// that is too late to correct a position already reported, and the word events
// come out in the wrong order and past the end of the stream, which is a screen
// reader highlighting the wrong word. Never asking for the extra pauses leaves
// every position the engine reports true, and needs no correction at all.
//
// A single full stop is left alone, so ordinary sentences pause exactly as they
// did. So is a run that is being spelled out, where each mark is its own word.
void append_text(std::wstring& tagged, std::vector<uint32_t>& source, const wchar_t* text,
                 ULONG length, ULONG src_base, bool collapse)
{
    bool after_sentence_end = false;
    for (ULONG i = 0; i < length; ++i) {
        wchar_t ch = text[i];

        // U+2026 and "..." are the same punctuation mark and should sound alike.
        // The engine says nothing at all for U+2026 -- not even a short pause --
        // so it is given the full stop it stands for.
        if (collapse && ch == kEllipsis) {
            ch = L'.';
        }
        if (collapse && is_sentence_end(ch)) {
            if (after_sentence_end) {
                continue;  // the pause for this one has already been asked for
            }
            after_sentence_end = true;
        } else {
            after_sentence_end = false;
        }

        if (ch == L'\\') {
            tagged += L"\\\\";
            source.push_back(src_base + i);
            source.push_back(src_base + i);
        } else {
            tagged += ch;
            source.push_back(src_base + i);
        }
    }
}

void append_tag(std::wstring& tagged, std::vector<uint32_t>& source, const std::wstring& tag)
{
    tagged += tag;
    source.insert(source.end(), tag.size(), kNotFromSource);
}

}  // namespace

TtsEngine::TtsEngine() : settings_(shared_catalog().settings()) {}

TtsEngine::~TtsEngine()
{
    if (token_) {
        token_->Release();
    }
}

STDMETHODIMP TtsEngine::SetObjectToken(ISpObjectToken* token)
{
    if (!token) {
        return E_INVALIDARG;
    }

    ISpDataKey* attributes = nullptr;
    if (FAILED(token->OpenKey(L"Attributes", &attributes)) || !attributes) {
        IVX_ERROR("sapi: the voice token has no Attributes key");
        return E_INVALIDARG;
    }

    LPWSTR value = nullptr;
    std::wstring name;
    std::string mode;
    if (SUCCEEDED(attributes->GetStringValue(L"Name", &value)) && value) {
        name = value;
        CoTaskMemFree(value);
        value = nullptr;
    }
    if (SUCCEEDED(attributes->GetStringValue(L"InfovoxModeGUID", &value)) && value) {
        mode = narrow(value);
        CoTaskMemFree(value);
        value = nullptr;
    }
    attributes->Release();

    // A token written by an older install, or one an application has cached,
    // may not carry the mode id; fall back to matching on the name.
    if (mode.empty() && !name.empty()) {
        const int index = shared_catalog().find_by_name(name);
        if (index >= 0) {
            mode = shared_catalog().voices()[static_cast<size_t>(index)].mode_guid;
        }
    }
    if (mode.empty()) {
        IVX_ERROR("sapi: cannot work out which voice \"%S\" is", name.c_str());
        return E_INVALIDARG;
    }

    if (token_) {
        token_->Release();
    }
    token_ = token;
    token_->AddRef();
    voice_name_ = name;
    mode_guid_ = mode;
    IVX_INFO("sapi: voice set to \"%S\" (%s)", voice_name_.c_str(), mode_guid_.c_str());
    return S_OK;
}

STDMETHODIMP TtsEngine::GetObjectToken(ISpObjectToken** token)
{
    if (!token) {
        return E_POINTER;
    }
    *token = nullptr;
    if (!token_) {
        return E_UNEXPECTED;
    }
    token_->AddRef();
    *token = token_;
    return S_OK;
}

HRESULT TtsEngine::ensure_format()
{
    if (have_format_) {
        return S_OK;
    }
    HelloResponse hello = {};
    if (client_.hello(&hello) && hello.samples_per_sec) {
        format_.samples_per_sec = hello.samples_per_sec;
        format_.avg_bytes_per_sec = hello.avg_bytes_per_sec;
        format_.channels = hello.channels;
        format_.bits = hello.bits;
        have_format_ = true;
        IVX_INFO("sapi: output format is %lu Hz, %u-bit, %u channel(s)",
                 static_cast<unsigned long>(format_.samples_per_sec), format_.bits,
                 format_.channels);
        return S_OK;
    }

    // The worker could not be reached. Report the format the engine has always
    // used rather than failing: an application that cannot negotiate a format
    // gives up on the voice entirely, and a later retry may well succeed.
    format_.samples_per_sec = 16000;
    format_.avg_bytes_per_sec = 32000;
    format_.channels = 1;
    format_.bits = 16;
    IVX_WARN("sapi: the worker did not answer; assuming 16000 Hz, 16-bit mono");
    return S_OK;
}

STDMETHODIMP TtsEngine::GetOutputFormat(const GUID*, const WAVEFORMATEX*, GUID* output_id,
                                        WAVEFORMATEX** output_format)
{
    if (!output_id || !output_format) {
        return E_POINTER;
    }
    *output_format = nullptr;
    *output_id = SPDFID_WaveFormatEx;

    ensure_format();

    auto* wfx = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!wfx) {
        return E_OUTOFMEMORY;
    }
    wfx->wFormatTag = WAVE_FORMAT_PCM;
    wfx->nChannels = format_.channels ? format_.channels : 1;
    wfx->nSamplesPerSec = format_.samples_per_sec ? format_.samples_per_sec : 16000;
    wfx->wBitsPerSample = format_.bits ? format_.bits : 16;
    wfx->nBlockAlign = static_cast<WORD>(wfx->nChannels * (wfx->wBitsPerSample / 8));
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
    wfx->cbSize = 0;
    *output_format = wfx;
    return S_OK;
}

void TtsEngine::emit_bookmark(uint32_t number, unsigned long stream_offset,
                              ISpTTSEngineSite* site)
{
    if (number < kFirstBookmark || number >= bookmarks_.size() + kFirstBookmark) {
        return;
    }
    const std::wstring& text = bookmarks_[number - kFirstBookmark];

    SPEVENT event = {};
    event.eEventId = SPEI_TTS_BOOKMARK;
    event.elParamType = SPET_LPARAM_IS_STRING;
    event.ulStreamNum = 0;
    event.ullAudioStreamOffset = stream_offset;
    event.lParam = reinterpret_cast<LPARAM>(text.c_str());
    event.wParam = static_cast<WPARAM>(_wtol(text.c_str()));
    const HRESULT hr = site->AddEvents(&event, 1);
    IVX_DEBUG("sapi: bookmark \"%S\" at stream offset %lu (%s)", text.c_str(), stream_offset,
              SUCCEEDED(hr) ? "queued" : hr_error(hr));
}

void TtsEngine::emit_word(const Run& run, uint32_t tagged_index, unsigned long stream_offset,
                          ISpTTSEngineSite* site)
{
    if (tagged_index >= run.source.size()) {
        return;
    }
    const uint32_t start = run.source[tagged_index];
    if (start == kNotFromSource) {
        return;
    }

    // Length is measured across the engine's copy of the text, where the only
    // difference from the application's is a doubled backslash.
    size_t end = tagged_index;
    while (end < run.tagged.size() && run.source[end] != kNotFromSource &&
           is_word_char(run.tagged[end])) {
        ++end;
    }
    const uint32_t last = end > tagged_index ? run.source[end - 1] : start;
    const ULONG length = last >= start ? (last - start + 1) : 1;

    SPEVENT event = {};
    event.eEventId = SPEI_WORD_BOUNDARY;
    event.elParamType = SPET_LPARAM_IS_UNDEFINED;
    event.ulStreamNum = 0;
    event.ullAudioStreamOffset = stream_offset;
    event.lParam = static_cast<LPARAM>(start);
    event.wParam = static_cast<WPARAM>(length);
    site->AddEvents(&event, 1);
    IVX_TRACE("sapi: word at source offset %lu length %lu, stream offset %lu",
              static_cast<unsigned long>(start), static_cast<unsigned long>(length),
              stream_offset);
}

bool TtsEngine::write_bytes(const void* data, unsigned long bytes, ISpTTSEngineSite* site)
{
    // Hand the audio over in small pieces rather than all at once.
    //
    // Write blocks while the host's buffer is full, and the host is playing in
    // real time, so a single call carrying a second of audio blocks for most of
    // a second -- and an abort raised during it is not noticed until it returns.
    // That was a third of a second of lag on every keypress when arrowing
    // through text. A piece worth about 30 ms bounds how long we can be stuck
    // between one look at GetActions and the next.
    const unsigned long per_second = format_.avg_bytes_per_sec ? format_.avg_bytes_per_sec : 32000;
    unsigned long piece = (per_second * 30) / 1000;
    piece &= ~1u;  // whole 16-bit samples
    if (piece < 256) {
        piece = 256;
    }

    const BYTE* p = static_cast<const BYTE*>(data);
    unsigned long remaining = bytes;
    while (remaining) {
        const DWORD actions = site->GetActions();
        if (actions & SPVES_ABORT) {
            IVX_DEBUG("sapi: the host asked to abort");
            aborted_ = true;
            return false;
        }
        if (actions & SPVES_SKIP) {
            IVX_DEBUG("sapi: the host asked to skip");
            site->CompleteSkip(0);
            aborted_ = true;
            return false;
        }
        ULONG written = 0;
        ULONG ask = static_cast<ULONG>((std::min)(remaining, piece));
        // Belt and braces: feed_audio already guarantees whole samples, and a
        // partial one here is what Write rejects outright.
        ask -= ask % block_align();
        if (ask == 0) {
            break;
        }
        const HRESULT hr = site->Write(p, ask, &written);
        if (FAILED(hr)) {
            IVX_ERROR("sapi: writing to the host failed: %s (ask=%lu remaining=%lu of %lu, "
                      "piece=%lu, stream_offset=%lu, written=%lu, fmt %lu Hz %u ch %u bit "
                      "avg=%lu)",
                      hr_error(hr), static_cast<unsigned long>(ask), remaining, bytes, piece,
                      stream_offset_, static_cast<unsigned long>(written),
                      static_cast<unsigned long>(format_.samples_per_sec), format_.channels,
                      format_.bits, format_.avg_bytes_per_sec);
            aborted_ = true;
            return false;
        }
        // Not every SAPI host fills in pcbWritten -- some leave it at zero and
        // some never touch it at all -- and Write either takes the whole buffer
        // or fails. Believing the out parameter therefore means either an
        // utterance that stops in the middle of a word, or, if it comes back
        // larger than what was offered, an unsigned subtraction that wraps and
        // a loop that walks off the end of the buffer. A successful call is
        // taken to have accepted what it was given.
        const ULONG took = (written == 0 || written > ask) ? ask : written;
        stream_offset_ += took;
        p += took;
        remaining -= took;
    }
    return true;
}

bool TtsEngine::flush_quiet(ISpTTSEngineSite* site)
{
    if (quiet_.empty()) {
        return true;
    }
    std::vector<BYTE> held;
    held.swap(quiet_);
    return write_bytes(held.data(), static_cast<unsigned long>(held.size()), site);
}

// Scales the opening samples of a piece up from nothing, in place in a scratch
// buffer, and points `data` at it. Raised cosine rather than a straight line:
// a linear ramp has a corner at each end, and a corner is what a click is.
//
// The buffer the worker hands over is reused, so it cannot be modified where it
// lies. The scratch copy lives as long as this object; only the first few
// milliseconds of an utterance ever pass through here.
void TtsEngine::apply_onset_fade(const void*& data, unsigned long bytes)
{
    const size_t count = bytes / sizeof(short);
    if (count == 0) {
        return;
    }
    fade_scratch_.assign(static_cast<const short*>(data),
                         static_cast<const short*>(data) + count);

    const size_t n = (std::min)(count, static_cast<size_t>(fade_remaining_));
    for (size_t i = 0; i < n; ++i) {
        const size_t done = fade_total_ - fade_remaining_ + i;
        const double phase = static_cast<double>(done) / fade_total_;
        const double gain = 0.5 - 0.5 * cos(phase * 3.14159265358979);
        fade_scratch_[i] = static_cast<short>(fade_scratch_[i] * gain);
    }
    fade_remaining_ -= static_cast<unsigned long>(n);
    data = fade_scratch_.data();
}

unsigned TtsEngine::block_align() const
{
    const unsigned channels = format_.channels ? format_.channels : 1;
    const unsigned bytes_per_sample = format_.bits ? (format_.bits / 8u) : 2u;
    const unsigned block = channels * bytes_per_sample;
    return block ? block : 1;
}

bool TtsEngine::feed_audio(const void* data, unsigned long bytes, ISpTTSEngineSite* site)
{
    // The engine hands its audio over in whatever pieces its internal buffering
    // produces, and those boundaries are not sample boundaries: at some speaking
    // rates a chunk arrives that is an odd number of bytes, and at least once
    // per utterance the chunk is a single byte.
    //
    // ISpTTSEngineSite::Write refuses anything that is not a whole number of
    // samples -- E_INVALIDARG, with nothing written -- and this engine treats
    // that as fatal to the utterance. Passed straight through, one stray byte
    // therefore ended the utterance wherever it happened to fall: mid-word, or
    // near the end, or after a few hundred bytes. It was the reason speech was
    // cut off above about a quarter of the rate range.
    //
    // So the odd tail is kept and put on the front of the next chunk. What
    // reaches Write is always whole samples.
    received_ += bytes;

    std::vector<BYTE> joined;
    const BYTE* p = static_cast<const BYTE*>(data);
    if (!partial_.empty()) {
        joined.reserve(partial_.size() + bytes);
        joined.assign(partial_.begin(), partial_.end());
        joined.insert(joined.end(), p, p + bytes);
        partial_.clear();
        p = joined.data();
        bytes = static_cast<unsigned long>(joined.size());
    }
    const unsigned long tail = bytes % block_align();
    if (tail) {
        IVX_TRACE("sapi: %lu byte(s) of a part sample held for the next chunk", tail);
        partial_.assign(p + bytes - tail, p + bytes);
        bytes -= tail;
    }
    if (bytes == 0) {
        return true;
    }
    data = p;

    // Only 16-bit PCM is inspected; anything else goes straight through, as
    // does everything when the user has asked to hear the engine untrimmed.
    if (format_.bits != 16 ||
        (!settings_.trim_trailing_silence && !settings_.trim_leading_silence)) {
        return flush_quiet(site) && write_bytes(data, bytes, site);
    }

    // Anything at or below this level is inaudible: the default of 16 out of
    // 32767 is about 66 decibels below full scale, and the padding being
    // trimmed sits at plus or minus one.
    const int quiet_level = settings_.silence_threshold;

    // The lead-in. Every piece the engine speaks starts with about a tenth of a
    // second of the same inaudible dither, and it is dropped rather than held:
    // there is nothing before it that it could turn out to be a pause between.
    // How much was dropped is remembered, because the engine's word and
    // bookmark positions are offsets into the audio it produced, and dropping
    // the front of that shifts every one of them.
    if (leading_ && settings_.trim_leading_silence) {
        const short* lead = static_cast<const short*>(data);
        const size_t lead_count = bytes / sizeof(short);
        size_t first_loud = lead_count;
        for (size_t i = 0; i < lead_count; ++i) {
            const int value = lead[i] < 0 ? -lead[i] : lead[i];
            if (value > quiet_level) {
                first_loud = i;
                break;
            }
        }
        const unsigned long dropped =
            static_cast<unsigned long>(first_loud * sizeof(short));
        lead_dropped_ += dropped;
        lead_total_ += dropped;
        if (first_loud == lead_count) {
            return true;  // the whole chunk was lead-in
        }
        leading_ = false;
        data = static_cast<const BYTE*>(data) + dropped;
        bytes -= dropped;
    }

    // Some voices start abruptly. Castilian Spanish goes from digital silence
    // to a fifth of full scale within ten samples -- 0, 32, 219, 723, 1549,
    // 2452, ... 6455 -- which is heard as a click at the start of every
    // utterance. American English, by contrast, opens at -21 and eases in. The
    // step is the engine's own, not something the trimming caused: the samples
    // immediately before it are exact zeros.
    //
    // A fade of a few milliseconds over the opening takes the edge off without
    // softening the speech; measured, it brings the sharpest step in the first
    // ten milliseconds of the Spanish voices down from about 2000 to about 150,
    // which is where the voices that never clicked already sat.
    if (fade_remaining_ > 0 && format_.bits == 16) {
        apply_onset_fade(data, bytes);
    }

    // `data` has just moved; everything below indexes from here, and reading
    // from where the chunk started instead would hand back bytes that have
    // already been written. That is audible: the run held for the next flush
    // began `dropped` bytes too early, so a tenth of a second of speech was
    // written twice and the voice stuttered -- "change-ge-ge" -- on whichever
    // utterance happened to have a gap in its first chunk of audio.
    const BYTE* const begin = static_cast<const BYTE*>(data);

    const short* samples = static_cast<const short*>(data);
    const size_t count = bytes / sizeof(short);
    if (!settings_.trim_trailing_silence) {
        return flush_quiet(site) && write_bytes(data, bytes, site);
    }
    size_t last_loud = count;  // count means "nothing loud in this chunk"
    for (size_t i = count; i-- > 0;) {
        const int value = samples[i] < 0 ? -samples[i] : samples[i];
        if (value > quiet_level) {
            last_loud = i;
            break;
        }
    }

    if (last_loud == count) {
        // Entirely quiet: hold it back in case this is the end of the utterance.
        quiet_.insert(quiet_.end(), begin, begin + bytes);
        return true;
    }

    // Real audio: whatever was held back was a pause inside the speech after
    // all, so it has to go out before this chunk.
    if (!flush_quiet(site)) {
        return false;
    }
    const unsigned long loud_bytes = static_cast<unsigned long>((last_loud + 1) * sizeof(short));
    if (!write_bytes(data, loud_bytes, site)) {
        return false;
    }
    if (loud_bytes < bytes) {
        quiet_.assign(begin + loud_bytes, begin + bytes);
    }
    return true;
}

HRESULT TtsEngine::write_silence(ULONG milliseconds, ISpTTSEngineSite* site)
{
    // The engine's own pause tag has no effect -- measured, not assumed -- so
    // silence is produced here, which also makes its length exact. Any padding
    // the previous piece left over is dropped first, for the same reason.
    discard_quiet();
    const unsigned long bytes_per_sec = format_.avg_bytes_per_sec ? format_.avg_bytes_per_sec
                                                                  : 32000;
    unsigned long remaining =
        static_cast<unsigned long>((static_cast<unsigned long long>(bytes_per_sec) *
                                    milliseconds) /
                                   1000);
    remaining &= ~1u;  // keep whole 16-bit samples

    static const BYTE zeros[4096] = {};
    while (remaining && !aborted_) {
        const DWORD actions = site->GetActions();
        if (actions & SPVES_ABORT) {
            aborted_ = true;
            break;
        }
        if (actions & SPVES_SKIP) {
            site->CompleteSkip(0);
            aborted_ = true;
            break;
        }
        ULONG chunk = static_cast<ULONG>((std::min<unsigned long>)(remaining, sizeof(zeros)));
        ULONG written = 0;
        const HRESULT hr = site->Write(zeros, chunk, &written);
        if (FAILED(hr)) {
            return hr;
        }
        // See write_bytes: pcbWritten is not to be believed.
        const ULONG took = (written == 0 || written > chunk) ? chunk : written;
        stream_offset_ += took;
        generated_ += took;
        remaining -= took;
    }
    IVX_DEBUG("sapi: wrote %lu ms of silence", static_cast<unsigned long>(milliseconds));
    return S_OK;
}

HRESULT TtsEngine::run_action(const Action& action, ISpTTSEngineSite* site)
{
    if (action.kind == Action::Silence) {
        return write_silence(action.silence_ms, site);
    }
    if (action.run.empty()) {
        return S_OK;
    }

    const Run& run = action.run;
    const unsigned long base_offset = stream_offset_;

    // Each piece the engine speaks has a lead-in of its own, so each starts
    // looking for one again.
    leading_ = true;
    lead_dropped_ = 0;

    // Each piece the engine speaks has an onset of its own, so each gets its
    // own fade. Worked out here rather than per chunk so a sample rate the
    // engine reported late cannot leave it stale.
    fade_total_ = settings_.onset_fade_ms > 0
                      ? static_cast<unsigned long>(
                            (static_cast<unsigned long long>(format_.samples_per_sec) *
                             settings_.onset_fade_ms) / 1000)
                      : 0;
    fade_remaining_ = fade_total_;

    auto on_audio = [&](const void* data, unsigned long bytes) -> bool {
        return feed_audio(data, bytes, site);
    };

    auto on_event = [&](const EventResponse& ev) {
        // The engine's offsets count the audio it produced; ours count the
        // audio that reached the application, which is shorter by whatever
        // lead-in was dropped. An event that lands inside the dropped part --
        // the engine reports the first word at offset zero -- belongs at the
        // start of what is left.
        const unsigned long at =
            base_offset + (ev.audio_offset > lead_dropped_ ? ev.audio_offset - lead_dropped_ : 0);
        if (ev.kind == EV_BOOKMARK) {
            emit_bookmark(ev.value, at, site);
        } else if (ev.kind == EV_WORD && ev.value > 0 && want_words_) {
            emit_word(run, ev.value - 1, at, site);
        }
    };

    auto on_format = [&](const FormatResponse& fmt) {
        if (fmt.samples_per_sec) {
            format_ = fmt;
            have_format_ = true;
        }
    };

    // Long text at the slowest rate really can take minutes; the ceiling exists
    // only so a wedged engine cannot hang the application for ever. Both halves
    // of it are the user's to change.
    const unsigned timeout = static_cast<unsigned>(
        settings_.timeout_base_ms +
        static_cast<long long>(run.tagged.size()) * settings_.timeout_per_char_ms);

    DoneResponse done = {};
    bool ok;
    if (action.kind == Action::Phonemes) {
        PhonemeRequest request = {};
        strncpy_s(request.mode_guid, mode_guid_.c_str(), _TRUNCATE);
        request.rate_step = run.rate_step;
        request.pitch_step = run.pitch_step;
        request.volume_pct = run.volume_pct;
        request.timeout_ms = timeout;
        request.ipa = action.ipa ? 1u : 0u;
        ok = client_.speak_phonemes(request, run.tagged, on_audio, on_format, &done);
    } else {
        SpeakRequest request = {};
        strncpy_s(request.mode_guid, mode_guid_.c_str(), _TRUNCATE);
        request.rate_step = run.rate_step;
        request.pitch_step = run.pitch_step;
        request.volume_pct = run.volume_pct;
        request.timeout_ms = timeout;
        ok = client_.speak(request, run.tagged, on_audio, on_event, on_format, &done);
    }

    if (!ok) {
        IVX_ERROR("sapi: the worker did not complete the utterance");
        return aborted_ ? S_OK : E_FAIL;
    }
    if (done.status == DONE_ENGINE_ERROR) {
        IVX_ERROR("sapi: the engine reported a failure for this utterance");
        return E_FAIL;
    }
    return S_OK;
}

STDMETHODIMP TtsEngine::Speak(DWORD flags, REFGUID, const WAVEFORMATEX*,
                              const SPVTEXTFRAG* fragments, ISpTTSEngineSite* site)
{
    if (!fragments || !site) {
        return E_INVALIDARG;
    }
    if (mode_guid_.empty()) {
        IVX_ERROR("sapi: Speak called before a voice was set");
        return E_UNEXPECTED;
    }

    ensure_format();

    bookmarks_.clear();
    quiet_.clear();
    partial_.clear();
    received_ = 0;
    lead_total_ = 0;
    generated_ = 0;
    stream_offset_ = 0;
    aborted_ = false;

    long base_rate = 0;
    site->GetRate(&base_rate);
    USHORT base_volume = 100;
    site->GetVolume(&base_volume);

    ULONGLONG interest = 0;
    site->GetEventInterest(&interest);
    // The host asks for the events it wants; the user can decline to send them
    // at all, which is the setting to reach for when a program's own word
    // highlighting is more trouble than it is worth.
    const bool want_sentences =
        settings_.sentence_events && (interest & SPFEI(SPEI_SENTENCE_BOUNDARY)) != 0;
    const bool want_words = settings_.word_events && (interest & SPFEI(SPEI_WORD_BOUNDARY)) != 0;
    want_words_ = want_words;

    IVX_DEBUG("sapi: Speak flags=0x%08lX rate=%ld volume=%u interest=0x%llX (sentences=%d words=%d)",
              flags, base_rate, base_volume, interest, want_sentences ? 1 : 0,
              want_words ? 1 : 0);

    // Build the whole utterance first. Prosody changes and silences split it
    // into separate pieces for the engine; everything else, bookmarks included,
    // stays in one piece so the engine's own phrasing survives.
    std::vector<Action> plan;
    Action current;
    bool current_started = false;

    auto flush = [&]() {
        if (current_started && !current.run.empty()) {
            plan.push_back(current);
        }
        current = Action();
        current_started = false;
    };

    for (const SPVTEXTFRAG* frag = fragments; frag; frag = frag->pNext) {
        const int rate_step = clamp(base_rate + frag->State.RateAdj, -10, 10);
        const int pitch_step = clamp(frag->State.PitchAdj.MiddleAdj, -10, 10);
        const int volume_pct = clamp((base_volume * frag->State.Volume) / 100, 0, 100);

        const bool prosody_changed = current_started && (current.run.rate_step != rate_step ||
                                                         current.run.pitch_step != pitch_step ||
                                                         current.run.volume_pct != volume_pct);

        switch (frag->State.eAction) {
            case SPVA_Silence: {
                flush();
                Action silence;
                silence.kind = Action::Silence;
                silence.silence_ms = frag->State.SilenceMSecs;
                plan.push_back(silence);
                continue;
            }

            case SPVA_Pronounce: {
                flush();
                Action phonemes;
                phonemes.kind = Action::Phonemes;
                phonemes.ipa = true;  // SAPI5 phoneme ids are IPA code points
                phonemes.run.rate_step = rate_step;
                phonemes.run.pitch_step = pitch_step;
                phonemes.run.volume_pct = volume_pct;
                if (frag->pTextStart && frag->ulTextLen) {
                    phonemes.run.tagged.assign(frag->pTextStart, frag->ulTextLen);
                    phonemes.run.source.assign(frag->ulTextLen, kNotFromSource);
                }
                if (!phonemes.run.empty()) {
                    plan.push_back(phonemes);
                }
                continue;
            }

            case SPVA_Bookmark: {
                if (prosody_changed) {
                    flush();
                }
                if (!current_started) {
                    current = Action();
                    current.run.rate_step = rate_step;
                    current.run.pitch_step = pitch_step;
                    current.run.volume_pct = volume_pct;
                    current_started = true;
                }
                std::wstring name;
                if (frag->pTextStart && frag->ulTextLen) {
                    name.assign(frag->pTextStart, frag->ulTextLen);
                }
                bookmarks_.push_back(name);
                wchar_t tag[24];
                _snwprintf_s(tag, _countof(tag), _TRUNCATE, L"\\mrk=%u\\",
                             static_cast<unsigned>(bookmarks_.size() - 1 + kFirstBookmark));
                append_tag(current.run.tagged, current.run.source, tag);
                continue;
            }

            case SPVA_Speak:
            case SPVA_SpellOut:
                break;

            default:
                continue;  // ParseNumber, ParseUnknown and friends carry no audio
        }

        if (!frag->pTextStart || frag->ulTextLen == 0) {
            continue;
        }

        if (prosody_changed) {
            flush();
        }
        if (!current_started) {
            current = Action();
            current.run.rate_step = rate_step;
            current.run.pitch_step = pitch_step;
            current.run.volume_pct = volume_pct;
            current_started = true;
        }

        if (want_sentences) {
            // Queued against the audio produced so far, which is where this
            // fragment will start playing.
            SPEVENT event = {};
            event.eEventId = SPEI_SENTENCE_BOUNDARY;
            event.elParamType = SPET_LPARAM_IS_UNDEFINED;
            event.ullAudioStreamOffset = stream_offset_;
            event.lParam = static_cast<LPARAM>(frag->ulTextSrcOffset);
            event.wParam = static_cast<WPARAM>(frag->ulTextLen);
            site->AddEvents(&event, 1);
        }

        if (frag->State.eAction == SPVA_SpellOut) {
            // The engine's letter mode tag does nothing (measured), so spelling
            // is done by separating the characters here.
            for (ULONG i = 0; i < frag->ulTextLen; ++i) {
                append_text(current.run.tagged, current.run.source, frag->pTextStart + i, 1,
                            frag->ulTextSrcOffset + i, false);
                append_tag(current.run.tagged, current.run.source, L" ");
            }
        } else {
            append_text(current.run.tagged, current.run.source, frag->pTextStart, frag->ulTextLen,
                        frag->ulTextSrcOffset, settings_.collapse_repeated_punctuation);
        }
    }
    flush();

    IVX_DEBUG("sapi: utterance planned as %u piece(s), %u bookmark(s)",
              static_cast<unsigned>(plan.size()), static_cast<unsigned>(bookmarks_.size()));

    for (const Action& action : plan) {
        if (aborted_) {
            break;
        }
        const DWORD actions = site->GetActions();
        if (actions & SPVES_ABORT) {
            break;
        }
        if (actions & SPVES_SKIP) {
            site->CompleteSkip(0);
            break;
        }
        // A rate or volume change between pieces is picked up here; SAPI raises
        // these while an utterance is in flight.
        if (actions & SPVES_RATE) {
            site->GetRate(&base_rate);
        }
        if (actions & SPVES_VOLUME) {
            site->GetVolume(&base_volume);
        }

        const HRESULT hr = run_action(action, site);
        if (FAILED(hr)) {
            return hr;
        }
    }

    // Whatever is still held back is the engine's end-of-utterance padding:
    // inaudible, and worth nearly a second of silence before the next thing is
    // spoken. Drop it.
    const size_t trimmed = quiet_.size();
    discard_quiet();

    IVX_DEBUG("sapi: Speak finished, %lu bytes written, %u bytes of trailing padding "
              "trimmed%s",
              stream_offset_, static_cast<unsigned>(trimmed), aborted_ ? " (aborted)" : "");

    // Everything the engine produced was either written to the host, dropped as
    // padding at one end or the other, or is still held. Nothing else is
    // legitimate -- in particular nothing may be written twice, which is what a
    // stutter sounds like and what no duration or word count would reveal. The
    // arithmetic is exact, so it is checked rather than assumed. Silence this
    // layer generates itself is not part of it, hence generated_.
    if (!aborted_) {
        const unsigned long long accounted = static_cast<unsigned long long>(stream_offset_) -
                                             generated_ + lead_total_ + trimmed + partial_.size();
        if (accounted != received_) {
            IVX_WARN("sapi: %llu bytes came from the engine but %llu are accounted for "
                     "(written %lu, generated %llu, lead %llu, trailing %u, held %u) -- "
                     "%lld byte(s) were written %s",
                     received_, accounted, stream_offset_, generated_, lead_total_,
                     static_cast<unsigned>(trimmed), static_cast<unsigned>(partial_.size()),
                     static_cast<long long>(accounted) - static_cast<long long>(received_),
                     accounted > received_ ? "twice" : "not at all");
        }
    }
    return S_OK;
}

}  // namespace sapi5
}  // namespace ivx
