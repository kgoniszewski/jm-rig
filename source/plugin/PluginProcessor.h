#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/ImpulseDecoder.h"
#include "dsp/RigEngine.h"
#include "ParameterLayout.h"

namespace jmrig
{

class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    //==========================================================================
    // Cab IRs. Message thread (or the host's state thread); never the audio
    // thread. The raw file bytes are kept and saved inside the session, so a
    // session reopens with its IRs even where the original file is out of
    // reach (the iPad AUv3 sandbox, another computer).
    using IrSlot = CabStage::Slot;

    /** Decodes a WAV/AIFF/FLAC file's bytes and loads it. Returns an error
        message, or an empty string on success. */
    juce::String loadImpulseResponse (IrSlot slot, juce::MemoryBlock fileData, const juce::String& name);

    /** Back to the built-in generic cab. */
    void clearImpulseResponse (IrSlot slot);

    /** Empty when the slot uses the built-in cab. */
    juce::String getImpulseResponseName (IrSlot slot) const;

private:
    void render (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept;
    RigParameters readParameters (bool forceBypass) const noexcept;

    struct StoredIr
    {
        juce::String name;
        juce::MemoryBlock data;
    };

    StoredIr& stored (IrSlot slot) noexcept { return slot == IrSlot::a ? irA : irB; }
    const StoredIr& stored (IrSlot slot) const noexcept { return slot == IrSlot::a ? irA : irB; }

    juce::ValueTree createIrState() const;
    void restoreIrState (const juce::ValueTree&);

    ImpulseDecoder decoder;
    juce::CriticalSection irLock; // guards irA/irB; never taken on the audio thread
    StoredIr irA, irB;

    juce::AudioProcessorValueTreeState apvts;

    // Cached once in the constructor; reading these is a lock-free atomic load.
    struct RawParams
    {
        std::atomic<float>* inputGain = nullptr;
        std::atomic<float>* outputGain = nullptr;
        std::atomic<float>* ampGain = nullptr;
        std::atomic<float>* ampBass = nullptr;
        std::atomic<float>* ampMid = nullptr;
        std::atomic<float>* ampTreble = nullptr;
        std::atomic<float>* ampPresence = nullptr;
        std::atomic<float>* ampMaster = nullptr;
        std::atomic<float>* ampBright = nullptr;
        std::atomic<float>* cabOn = nullptr;
        std::atomic<float>* cabBlend = nullptr;
        std::atomic<float>* cabLowCut = nullptr;
        std::atomic<float>* cabHighCut = nullptr;
        std::atomic<float>* bypass = nullptr;
    } raw;

    RigEngine engine;
    juce::HeapBlock<float> monoScratch;
    int maxBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace jmrig
