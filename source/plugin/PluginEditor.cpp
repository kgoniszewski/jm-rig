#include "PluginEditor.h"

namespace jmrig
{

namespace
{
    constexpr int baseWidth = 880, baseHeight = 600;

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

    addKnob (cabKnobs[0], ParamIDs::cabBlend,   "IR Blend");
    addKnob (cabKnobs[1], ParamIDs::cabLowCut,  "Low Cut");
    addKnob (cabKnobs[2], ParamIDs::cabHighCut, "High Cut");

    addToggle (bright, ParamIDs::ampBright, "Bright");
    addToggle (bypass, ParamIDs::bypass,    "Bypass");
    addToggle (cabOn,  ParamIDs::cabOn,     "Cab");

    setUpIrSlot (irA);
    setUpIrSlot (irB);
    refreshIrNames();
    startTimerHz (4); // names can change from session recall, not just from here

    setResizable (true, true);
    setResizeLimits (baseWidth / 2, baseHeight / 2, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (baseWidth, baseHeight);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
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

void PluginEditor::setUpIrSlot (IrSlotControls& c)
{
    c.load.onClick = [this, slot = c.slot] { chooseImpulseResponse (slot); };
    c.load.setTooltip ("Load a WAV, AIFF or FLAC impulse response");
    addAndMakeVisible (c.load);

    c.reset.onClick = [this, slot = c.slot]
    {
        processor.clearImpulseResponse (slot);
        refreshIrNames();
    };
    c.reset.setTooltip ("Back to the built-in cab");
    addAndMakeVisible (c.reset);
}

void PluginEditor::chooseImpulseResponse (PluginProcessor::IrSlot slot)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a cab impulse response",
                                                       juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac");

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (chooserFlags, [this, slot] (const juce::FileChooser& chooser)
    {
        // On iPadOS the result is a security-scoped URL, not a plain path,
        // so read it through the URL rather than as a File.
        const auto url = chooser.getURLResult();

        if (url.isEmpty())
            return;

        juce::MemoryBlock data;

        if (auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)))
            stream->readIntoMemoryBlock (data);

        const auto name = url.getFileName().upToLastOccurrenceOf (".", false, false);
        const auto error = processor.loadImpulseResponse (slot, std::move (data),
                                                          name.isNotEmpty() ? name : "Custom IR");

        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load IR", error);

        refreshIrNames();
    });
}

void PluginEditor::refreshIrNames()
{
    for (auto* c : { &irA, &irB })
    {
        const auto name = processor.getImpulseResponseName (c->slot);
        const auto textToShow = c->title + ": " + (name.isNotEmpty() ? name : juce::String ("Built-in cab"));

        if (c->load.getButtonText() != textToShow)
            c->load.setButtonText (textToShow);

        c->reset.setEnabled (name.isNotEmpty());
    }
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    const auto scale = (float) getWidth() / baseWidth;

    for (auto r : { ampPanel, cabPanel })
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r, 10.0f * scale);
        g.setColour (panelEdge);
        g.drawRoundedRectangle (r, 10.0f * scale, 2.0f * scale);
    }

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
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, s (72), s (20));
        k.slider.setBounds (r);
    };

    // Amp row: Input | amp knobs | Output.
    auto ampRow = area.removeFromTop (s (260));
    const auto sideWidth = s (110);
    placeKnob (input,  ampRow.removeFromLeft (sideWidth).reduced (s (6)));
    placeKnob (output, ampRow.removeFromRight (sideWidth).reduced (s (6)));

    ampPanel = ampRow.reduced (s (8)).toFloat();
    auto knobs = ampRow.reduced (s (20));
    const auto knobWidth = knobs.getWidth() / (int) ampKnobs.size();

    for (auto& k : ampKnobs)
        placeKnob (k, knobs.removeFromLeft (knobWidth).reduced (s (4)));

    // Cab row: IR slots | blend, low cut, high cut | on/off.
    area.removeFromTop (s (8));
    cabPanel = area.reduced (s (8), s (4)).toFloat();
    auto cabRow = area.reduced (s (20), s (12));

    auto slots = cabRow.removeFromLeft (s (300));
    const auto slotHeight = slots.getHeight() / 2;

    for (auto* c : { &irA, &irB })
    {
        auto r = slots.removeFromTop (slotHeight).withSizeKeepingCentre (slots.getWidth(), s (48));
        c->reset.setBounds (r.removeFromRight (s (48)).reduced (s (2)));
        c->load.setBounds (r.reduced (s (2)));
    }

    cabOn.button.setBounds (cabRow.removeFromRight (s (100)).withSizeKeepingCentre (s (88), s (48)));

    const auto cabKnobWidth = cabRow.getWidth() / (int) cabKnobs.size();

    for (auto& k : cabKnobs)
        placeKnob (k, cabRow.removeFromLeft (cabKnobWidth).reduced (s (4)));
}

} // namespace jmrig
