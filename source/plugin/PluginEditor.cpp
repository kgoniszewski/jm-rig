#include "PluginEditor.h"

namespace jmrig
{

namespace
{
    // Design grid. Everything is laid out in these units and scaled.
    constexpr float gridWidth = 1000.0f, gridHeight = 700.0f;

    // Window sizes on the desktop; hosts on iPadOS pick their own. The
    // smallest is 0.8 scale, where the smallest control is still 44 pt.
    constexpr int defaultWidth = 1000, defaultHeight = 700;
    constexpr int minimumWidth = 800, minimumHeight = 560;
}

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& state = processor.getValueTreeState();

    const std::array<std::pair<const char*, const char*>, 6> amp {{
        { ParamIDs::ampGain,     "Gain" },
        { ParamIDs::ampBass,     "Bass" },
        { ParamIDs::ampMid,      "Mid" },
        { ParamIDs::ampTreble,   "Treble" },
        { ParamIDs::ampPresence, "Presence" },
        { ParamIDs::ampMaster,   "Master" },
    }};

    for (const auto& [id, name] : amp)
        addAndMakeVisible (*ampKnobs.emplace_back (std::make_unique<RigKnob> (state, id, name, true)));

    const std::array<std::pair<const char*, const char*>, 3> cab {{
        { ParamIDs::cabBlend,   "A / B" },
        { ParamIDs::cabLowCut,  "Low Cut" },
        { ParamIDs::cabHighCut, "High Cut" },
    }};

    for (const auto& [id, name] : cab)
        addAndMakeVisible (*cabKnobs.emplace_back (std::make_unique<RigKnob> (state, id, name, false)));

    input  = std::make_unique<RigKnob> (state, ParamIDs::inputGain,  "Input",  false);
    output = std::make_unique<RigKnob> (state, ParamIDs::outputGain, "Output", false);
    addAndMakeVisible (*input);
    addAndMakeVisible (*output);

    addToggle (bright, ParamIDs::ampBright, "Bright");
    addToggle (bypass, ParamIDs::bypass,    "Bypass");
    addToggle (cabOn,  ParamIDs::cabOn,     "Cab On");

    setUpIrSlot (irA);
    setUpIrSlot (irB);
    refreshIrNames();
    startTimerHz (4); // names can change from session recall, not just from here

    setResizable (true, true);
    setResizeLimits (minimumWidth, minimumHeight, defaultWidth * 2, defaultHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) gridWidth / gridHeight);
    setSize (defaultWidth, defaultHeight);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PluginEditor::addToggle (Toggle& t, const char* paramID, const juce::String& labelText)
{
    t.button.setButtonText (labelText);
    t.button.setClickingTogglesState (true);
    t.button.getProperties().set (RigLookAndFeel::hasLed, true);
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
        const auto textToShow = c->title + ":  " + (name.isNotEmpty() ? name : juce::String ("Built-in cab"));

        if (c->load.getButtonText() != textToShow)
            c->load.setButtonText (textToShow);

        c->reset.setEnabled (name.isNotEmpty());
    }
}

juce::Rectangle<float> PluginEditor::contentArea() const
{
    auto area = getLocalBounds();

   #if JUCE_IOS
    // Full-screen app: keep clear of the rounded corners and the home bar.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            area = display->safeAreaInsets.subtractedFrom (area);
   #endif

    const auto bounds = area.toFloat();

    const auto s = juce::jmin (bounds.getWidth() / gridWidth, bounds.getHeight() / gridHeight);
    return juce::Rectangle<float> (gridWidth * s, gridHeight * s).withCentre (bounds.getCentre());
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    // Faceplate: tolex brown with gold piping.
    const auto corner = 14.0f * scale;
    g.setGradientFill (juce::ColourGradient (Palette::faceplateHi, faceplate.getX(), faceplate.getY(),
                                             Palette::faceplate, faceplate.getX(), faceplate.getBottom(), false));
    g.fillRoundedRectangle (faceplate, corner);

    {
        // A faint diagonal weave, like vinyl tolex.
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (faceplate, corner);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colours::black.withAlpha (0.08f));
        const auto step = 6.0f * scale;

        for (auto x = faceplate.getX() - faceplate.getHeight(); x < faceplate.getRight(); x += step)
            g.drawLine (x, faceplate.getBottom(), x + faceplate.getHeight(), faceplate.getY(), 1.0f);
    }

    g.setColour (Palette::piping);
    g.drawRoundedRectangle (faceplate.reduced (6.0f * scale), corner * 0.7f, 2.0f * scale);

    // Cab panel.
    g.setColour (Palette::cabPanel);
    g.fillRoundedRectangle (cabPanel, corner);
    g.setColour (Palette::piping.withAlpha (0.35f));
    g.drawRoundedRectangle (cabPanel, corner, 1.2f * scale);

    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (13.0f * scale, juce::Font::bold));
    g.drawText ("CAB", cabPanel.withHeight (34.0f * scale).reduced (20.0f * scale, 0.0f),
                juce::Justification::centredLeft, false);

    // Wordmark.
    g.setColour (Palette::accent);
    g.setFont (juce::FontOptions (34.0f * scale, juce::Font::bold));
    g.drawText ("JM RIG", logo, juce::Justification::centredLeft, false);
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (13.0f * scale));
    g.drawText (juce::String::fromUTF8 ("CLEAN AMP  \xc2\xb7  CAB"), logo.withTrimmedLeft (150.0f * scale), juce::Justification::centredLeft, false);
}

void PluginEditor::resized()
{
    const auto content = contentArea();
    scale = content.getWidth() / gridWidth;

    // Design-grid rectangle to window pixels.
    const auto grid = [&] (float x, float y, float w, float h)
    {
        return juce::Rectangle<float> (content.getX() + x * scale, content.getY() + y * scale, w * scale, h * scale);
    };
    const auto place = [] (juce::Component& c, juce::Rectangle<float> r) { c.setBounds (r.toNearestInt()); };

    // Header: wordmark, bypass.
    logo = grid (28, 18, 600, 60);
    place (bypass.button, grid (836, 22, 140, 56));

    // Faceplate: six amp knobs and the bright switch.
    faceplate = grid (20, 92, 960, 260);
    const auto knobWidth = 128.0f;
    auto x = 44.0f;

    for (auto& k : ampKnobs)
    {
        place (*k, grid (x, 137, knobWidth, 170));
        x += knobWidth;
    }

    place (bright.button, grid (x + 12, 179, 116, 56));

    // Bottom row: input, cab, output.
    place (*input,  grid (20,  400, 140, 210));
    place (*output, grid (840, 400, 140, 210));

    cabPanel = grid (176, 372, 648, 280);

    auto y = 410.0f;

    for (auto* c : { &irA, &irB })
    {
        place (c->load,  grid (196, y, 248, 56));
        place (c->reset, grid (450, y, 56, 56));
        y += 66.0f;
    }

    place (cabOn.button, grid (196, y, 140, 56));

    x = 520.0f;

    for (auto& k : cabKnobs)
    {
        place (*k, grid (x, 418, 96, 170));
        x += 98.0f;
    }
}

} // namespace jmrig
