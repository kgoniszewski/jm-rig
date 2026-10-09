#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jmrig
{

/** Colours shared by the editor and its look and feel. */
namespace Palette
{
    inline const juce::Colour background { 0xff141416 };
    inline const juce::Colour faceplate  { 0xff2a2219 }; // dark tolex brown
    inline const juce::Colour faceplateHi { 0xff362c21 };
    inline const juce::Colour piping     { 0xff9a8158 }; // gold piping
    inline const juce::Colour cabPanel   { 0xff1f1f22 };
    inline const juce::Colour text       { 0xffeee6d6 };
    inline const juce::Colour textDim    { 0xffa49a88 };
    inline const juce::Colour accent     { 0xffe0ad4c };
    inline const juce::Colour ledOn      { 0xffff5a3c };
    inline const juce::Colour control    { 0xff3a332b };
}

/** Vector-only drawing so the face scales cleanly from a small Mac window to
    a full iPad screen. Knobs draw their own 0..10 scale when the slider's
    "scale" property is set; toggles draw an LED. */
class RigLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    /** Set on a slider to draw numbers 0..10 around it. */
    static constexpr const char* numberedScale = "jmrigNumberedScale";

    /** Set on a toggle button to draw an LED. */
    static constexpr const char* hasLed = "jmrigLed";
};

} // namespace jmrig
