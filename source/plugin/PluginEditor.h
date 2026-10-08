#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace jmrig
{

/** Skeleton editor: all vector drawing, scales freely, and every control has
    a touch target of at least 44 pt at the smallest size (Apple's minimum).
    The real amp face arrives with the amp model in step 3. */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
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

    /** "Load IR" button showing the loaded file's name, plus a reset button
        that returns the slot to the built-in cab. */
    struct IrSlotControls
    {
        IrSlotControls (PluginProcessor::IrSlot s, juce::String t) : slot (s), title (std::move (t)) {}

        const PluginProcessor::IrSlot slot;
        const juce::String title;
        juce::TextButton load, reset { juce::CharPointer_UTF8 ("\xc3\x97") }; // ×
    };

    void addKnob (Knob&, const char* paramID, const juce::String& text);
    void addToggle (Toggle&, const char* paramID, const juce::String& text);
    void setUpIrSlot (IrSlotControls&);
    void chooseImpulseResponse (PluginProcessor::IrSlot);
    void refreshIrNames();
    void timerCallback() override { refreshIrNames(); }

    PluginProcessor& processor;

    juce::LookAndFeel_V4 lookAndFeel;

    Knob input, output;
    std::array<Knob, 6> ampKnobs;
    std::array<Knob, 3> cabKnobs;
    Toggle bright, bypass, cabOn;
    IrSlotControls irA { PluginProcessor::IrSlot::a, "IR A" };
    IrSlotControls irB { PluginProcessor::IrSlot::b, "IR B" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<float> ampPanel, cabPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace jmrig
