#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "RigKnob.h"
#include "RigLookAndFeel.h"

namespace jmrig
{

/** The amp face. Laid out on a 1000 x 700 design grid (the iPad's landscape
    shape) and scaled to fit whatever size the host or window gives it,
    letterboxed if the shape differs. At the smallest window size every
    control is still at least 44 pt, Apple's minimum touch target; full
    screen on an 11" iPad the amp knobs are about 150 pt. */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
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

    void addToggle (Toggle&, const char* paramID, const juce::String& text);
    void setUpIrSlot (IrSlotControls&);
    void chooseImpulseResponse (PluginProcessor::IrSlot);
    void refreshIrNames();
    void timerCallback() override { refreshIrNames(); }

    /** The design-grid area, scaled and centred in the window (and inside the
        screen's safe area when running as the iPad app). */
    juce::Rectangle<float> contentArea() const;

    PluginProcessor& processor;
    RigLookAndFeel lookAndFeel;

    std::vector<std::unique_ptr<RigKnob>> ampKnobs, cabKnobs;
    std::unique_ptr<RigKnob> input, output;
    Toggle bright, bypass, cabOn;
    IrSlotControls irA { PluginProcessor::IrSlot::a, "IR A" };
    IrSlotControls irB { PluginProcessor::IrSlot::b, "IR B" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Panel outlines in window coordinates, set by resized().
    juce::Rectangle<float> faceplate, cabPanel, logo;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace jmrig
