#include "PluginEditor.h"

namespace jmrig
{

namespace
{
    constexpr int baseWidth = 880, baseHeight = 400;

    const juce::Colour background { 0xff17171a };
    const juce::Colour panel      { 0xff26201a }; // dark tolex brown
    const juce::Colour panelEdge  { 0xff8a7350 }; // gold piping
    const juce::Colour text       { 0xffe9e2d4 };
    const juce::Colour accent     { 0xffd9a441 };
}

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3c352c));
    lookAndFeel.setColour (juce::Slider::thumbColourId, text);
    lookAndFeel.setColour (juce::Label::textColourId, text);
    lookAndFeel.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3c352c));
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, accent);
    lookAndFeel.setColour (juce::TextButton::textColourOffId, text);
    lookAndFeel.setColour (juce::TextButton::textColourOnId, background);
    setLookAndFeel (&lookAndFeel);

    addKnob (input,  ParamIDs::inputGain,  "Input");
    addKnob (output, ParamIDs::outputGain, "Output");

    const std::array<std::pair<const char*, const char*>, 6> amp {{
        { ParamIDs::ampGain,     "Gain" },
        { ParamIDs::ampBass,     "Bass" },
        { ParamIDs::ampMid,      "Mid" },
        { ParamIDs::ampTreble,   "Treble" },
        { ParamIDs::ampPresence, "Presence" },
        { ParamIDs::ampMaster,   "Master" },
    }};

    for (size_t i = 0; i < amp.size(); ++i)
        addKnob (ampKnobs[i], amp[i].first, amp[i].second);

    addToggle (bright, ParamIDs::ampBright, "Bright");
    addToggle (bypass, ParamIDs::bypass,    "Bypass");

    setResizable (true, true);
    setResizeLimits (baseWidth / 2, baseHeight / 2, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (baseWidth, baseHeight);
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel (nullptr);
}

void PluginEditor::addKnob (Knob& k, const char* paramID, const juce::String& labelText)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 20);
    k.slider.setVelocityBasedMode (false);
    k.slider.setMouseDragSensitivity (220);
    addAndMakeVisible (k.slider);

    k.label.setText (labelText, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getValueTreeState(), paramID, k.slider);
}

void PluginEditor::addToggle (Toggle& t, const char* paramID, const juce::String& labelText)
{
    t.button.setButtonText (labelText);
    t.button.setClickingTogglesState (true);
    addAndMakeVisible (t.button);

    t.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getValueTreeState(), paramID, t.button);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    const auto scale = (float) getWidth() / baseWidth;

    g.setColour (panel);
    g.fillRoundedRectangle (ampPanel, 10.0f * scale);
    g.setColour (panelEdge);
    g.drawRoundedRectangle (ampPanel, 10.0f * scale, 2.0f * scale);

    g.setColour (text);
    g.setFont (juce::FontOptions (22.0f * scale, juce::Font::bold));
    g.drawText ("JM RIG", getLocalBounds().removeFromTop (juce::roundToInt (48 * scale)),
                juce::Justification::centred);
}

void PluginEditor::resized()
{
    const auto scale = (float) getWidth() / baseWidth;
    const auto s = [scale] (int v) { return juce::roundToInt ((float) v * scale); };

    auto area = getLocalBounds().reduced (s (16));
    area.removeFromTop (s (40));

    auto footer = area.removeFromBottom (s (52));
    bypass.button.setBounds (footer.removeFromRight (s (120)).reduced (s (4)));
    bright.button.setBounds (footer.removeFromRight (s (120)).reduced (s (4)));

    const auto placeKnob = [&] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setFont (juce::FontOptions (15.0f * scale));
        k.label.setBounds (r.removeFromTop (s (24)));
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, s (64), s (20));
        k.slider.setBounds (r);
    };

    const auto sideWidth = s (110);
    placeKnob (input,  area.removeFromLeft (sideWidth).reduced (s (6)));
    placeKnob (output, area.removeFromRight (sideWidth).reduced (s (6)));

    ampPanel = area.reduced (s (8)).toFloat();
    auto knobs = area.reduced (s (20));
    const auto knobWidth = knobs.getWidth() / (int) ampKnobs.size();

    for (auto& k : ampKnobs)
        placeKnob (k, knobs.removeFromLeft (knobWidth).reduced (s (4)));
}

} // namespace jmrig
