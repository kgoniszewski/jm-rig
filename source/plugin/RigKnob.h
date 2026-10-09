#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "RigLookAndFeel.h"

namespace jmrig
{

/** A knob for fingers and mice: the whole component is the touch target,
    drag up/down or sideways to turn it, double-tap to reset it. The caption
    shows the name, and the value while it is being turned (a finger covers
    the knob itself). */
class RigKnob final : public juce::Component
{
public:
    RigKnob (juce::AudioProcessorValueTreeState& state, const char* paramID, juce::String nameToShow, bool numbered)
        : name (std::move (nameToShow))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.getProperties().set (RigLookAndFeel::numberedScale, numbered);
        slider.onDragStart = [this] { repaint(); };
        slider.onDragEnd = [this] { repaint(); };
        slider.onValueChange = [this] { if (slider.isMouseButtonDown()) repaint(); };
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);

        // The attachment also sets double-tap to return to the default.
        parameter = state.getParameter (paramID);
    }

    void paint (juce::Graphics& g) override
    {
        const auto turning = slider.isMouseButtonDown();
        const auto caption = turning ? valueText() : name;

        g.setColour (turning ? Palette::accent : Palette::text);
        g.setFont (juce::FontOptions (captionHeight() * 0.62f, turning ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (caption, getLocalBounds().removeFromBottom (juce::roundToInt (captionHeight())),
                          juce::Justification::centred, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom (juce::roundToInt (captionHeight()));
        slider.setBounds (r);

        // Full travel is about twice the knob's height, so big knobs on an
        // iPad and small ones in a Mac window feel the same under a finger.
        slider.setMouseDragSensitivity (juce::jmax (120, r.getHeight() * 2));
    }

    juce::Slider slider;

private:
    juce::String valueText() const
    {
        if (parameter == nullptr)
            return name;

        auto text = parameter->getText (parameter->convertTo0to1 ((float) slider.getValue()), 16);
        const auto unit = parameter->getLabel();

        if (unit.isNotEmpty() && ! text.endsWith (unit))
            text << " " << unit;

        return text;
    }

    float captionHeight() const { return juce::jlimit (18.0f, 30.0f, (float) getHeight() * 0.17f); }

    const juce::String name;
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigKnob)
};

} // namespace jmrig
