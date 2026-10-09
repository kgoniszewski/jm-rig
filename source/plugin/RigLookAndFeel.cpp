#include "RigLookAndFeel.h"

namespace jmrig
{

RigLookAndFeel::RigLookAndFeel()
{
    setColour (juce::Label::textColourId, Palette::text);
    setColour (juce::TextButton::buttonColourId, Palette::control);
    setColour (juce::TextButton::buttonOnColourId, Palette::control);
    setColour (juce::TextButton::textColourOffId, Palette::text);
    setColour (juce::TextButton::textColourOnId, Palette::text);
    setColour (juce::AlertWindow::backgroundColourId, Palette::cabPanel);
    setColour (juce::AlertWindow::textColourId, Palette::text);
}

void RigLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto numbered = (bool) slider.getProperties()[numberedScale];
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    const auto pointOnCircle = [centre] (float r, float a)
    {
        return centre + juce::Point<float> (r * std::sin (a), -r * std::cos (a));
    };

    // Scale: 11 ticks, numbered 0..10 on the amp's knobs.
    const auto tickInner = radius * (numbered ? 0.74f : 0.84f);
    const auto tickOuter = radius * (numbered ? 0.80f : 0.94f);

    for (int i = 0; i <= 10; ++i)
    {
        const auto a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        g.setColour (Palette::piping.withAlpha (i % 5 == 0 ? 1.0f : 0.6f));
        g.drawLine ({ pointOnCircle (tickInner, a), pointOnCircle (tickOuter, a) }, radius * (i % 5 == 0 ? 0.04f : 0.025f));

        if (numbered)
        {
            const auto p = pointOnCircle (radius * 0.91f, a);
            const auto box = radius * 0.24f;
            g.setFont (juce::FontOptions (radius * 0.17f));
            g.setColour (Palette::textDim);
            g.drawText (juce::String (i), juce::Rectangle<float> (box, box).withCentre (p),
                        juce::Justification::centred, false);
        }
    }

    // Value arc just inside the scale.
    const auto arcRadius = tickInner - radius * 0.05f;
    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    const juce::PathStrokeType stroke (radius * 0.035f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (track, stroke);
    g.setColour (slider.isEnabled() ? Palette::accent : Palette::textDim);
    g.strokePath (value, stroke);

    // Knob body: black, domed, with a cream pointer.
    const auto bodyRadius = arcRadius - radius * 0.08f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (body.translated (0.0f, radius * 0.04f).expanded (radius * 0.02f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4744), centre.x, body.getY(),
                                             juce::Colour (0xff111111), centre.x, body.getBottom(), false));
    g.fillEllipse (body);

    const auto cap = body.reduced (bodyRadius * 0.22f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2c2a), cap.getX(), cap.getY(),
                                             juce::Colour (0xff080808), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);

    g.setColour (Palette::text);
    g.drawLine ({ pointOnCircle (bodyRadius * 0.25f, angle), pointOnCircle (bodyRadius * 0.92f, angle) },
                juce::jmax (1.5f, radius * 0.05f));
}

void RigLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto corner = juce::jmin (8.0f, bounds.getHeight() * 0.2f);
    auto fill = Palette::control;

    if (down)
        fill = fill.darker (0.3f);
    else if (highlighted)
        fill = fill.brighter (0.1f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (button.getToggleState() && (bool) button.getProperties()[hasLed] ? Palette::accent.withAlpha (0.8f)
                                                                                  : Palette::piping.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds, corner, 1.2f);

    if ((bool) button.getProperties()[hasLed])
    {
        const auto d = juce::jmin (bounds.getHeight() * 0.28f, 14.0f);
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ bounds.getX() + bounds.getHeight() * 0.45f,
                                                                     bounds.getCentreY() });
        const auto on = button.getToggleState();

        if (on)
        {
            g.setColour (Palette::ledOn.withAlpha (0.3f));
            g.fillEllipse (led.expanded (d * 0.4f));
        }

        g.setColour (on ? Palette::ledOn : juce::Colour (0xff3a1a14));
        g.fillEllipse (led);
    }
}

void RigLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    auto bounds = button.getLocalBounds();

    if ((bool) button.getProperties()[hasLed])
        bounds.removeFromLeft (juce::roundToInt ((float) bounds.getHeight() * 0.7f));

    g.setFont (juce::FontOptions (juce::jmin (17.0f, (float) button.getHeight() * 0.38f)));
    g.setColour (button.isEnabled() ? Palette::text : Palette::textDim);
    g.drawFittedText (button.getButtonText(), bounds.reduced (6, 2), juce::Justification::centred, 1);
}

} // namespace jmrig
