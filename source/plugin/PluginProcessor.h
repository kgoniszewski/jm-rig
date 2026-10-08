#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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

private:
    void render (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept;
    RigParameters readParameters (bool forceBypass) const noexcept;

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
        std::atomic<float>* bypass = nullptr;
    } raw;

    RigEngine engine;
    juce::HeapBlock<float> monoScratch;
    int maxBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace jmrig
