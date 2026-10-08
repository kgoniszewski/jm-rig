#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace jmrig
{

/** Skeleton editor: all vector drawing, scales freely, and every control has
    a touch target of at least 44 pt at the smallest size (Apple's minimum).
    The real amp face arrives with the amp model in step 3. */
class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void addKnob (Knob&, const char* paramID, const juce::String& text);
    void addToggle (Toggle&, const char* paramID, const juce::String& text);

    PluginProcessor& processor;

    juce::LookAndFeel_V4 lookAndFeel;

    Knob input, output;
    std::array<Knob, 6> ampKnobs;
    Toggle bright, bypass;

    juce::Rectangle<float> ampPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace jmrig
