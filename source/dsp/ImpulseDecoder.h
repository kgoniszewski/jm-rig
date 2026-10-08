#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "CabStage.h"

namespace jmrig
{

/** Turns the bytes of an IR file (WAV, AIFF, FLAC) into a mono buffer the
    cab can load. Not for the audio thread: decoding allocates. */
class ImpulseDecoder
{
public:
    /** Large enough for any real cab IR at 192 kHz, small enough to reject a
        song picked by mistake before decoding it. */
    static constexpr size_t maxFileBytes = 8 * 1024 * 1024;

    struct Result
    {
        juce::AudioBuffer<float> impulse; // channel 0 of the file only
        double sampleRate = 0.0;
        juce::String error;               // empty on success

        bool ok() const noexcept { return error.isEmpty(); }
    };

    ImpulseDecoder() { formats.registerBasicFormats(); }

    Result decode (const juce::MemoryBlock& fileData)
    {
        Result r;

        if (fileData.isEmpty())
            return withError (std::move (r), "The file is empty.");

        if (fileData.getSize() > maxFileBytes)
            return withError (std::move (r), "That file is too large to be a cab IR.");

        std::unique_ptr<juce::AudioFormatReader> reader (
            formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (fileData, false)));

        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return withError (std::move (r), "Not a WAV, AIFF or FLAC file JM Rig can read.");

        // Read one sample past the limit so CabStage knows to fade the cut.
        const auto maxSamples = (juce::int64) (CabStage::maxImpulseSeconds * reader->sampleRate) + 1;
        const auto length = (int) juce::jmin (reader->lengthInSamples, maxSamples);

        r.impulse.setSize (1, length);
        reader->read (&r.impulse, 0, length, 0, true, false);
        r.sampleRate = reader->sampleRate;
        return r;
    }

private:
    static Result withError (Result r, const juce::String& message)
    {
        r.error = message;
        return r;
    }

    juce::AudioFormatManager formats;
};

} // namespace jmrig
