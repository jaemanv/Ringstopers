#include "PluginEditor.h"
#include <cmath>

using namespace ringstop;

//============================================================================== Spectrum
SpectrumView::SpectrumView (RingstopProcessor& p) : proc (p) { startTimerHz (30); }

void SpectrumView::timerCallback()
{
    proc.detector.copySpectrum (spectrum, binHz);
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (colours::well);
    g.fillRoundedRectangle (b, 8.0f);

    const float fMin = 50.0f, fMax = 16000.0f, dbMin = -100.0f, dbMax = 0.0f;
    auto X = [&] (float f)  { return b.getX() + std::log (f / fMin) / std::log (fMax / fMin) * b.getWidth(); };
    auto Y = [&] (float db) { return b.getY() + (1.0f - (juce::jlimit (dbMin, dbMax, db) - dbMin) / (dbMax - dbMin)) * b.getHeight(); };

    g.setFont (11.0f);
    for (float f : { 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f })
    {
        const float x = X (f);
        g.setColour (colours::line);
        g.drawVerticalLine ((int) x, b.getY(), b.getBottom());
        g.setColour (colours::muted);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    (int) x + 3, (int) b.getBottom() - 16, 40, 14, juce::Justification::left);
    }

    // Notch markers: line from top, length = depth
    const bool engaged = proc.apvts.getRawParameterValue ("engaged")->load() > 0.5f;
    for (int k = 0; k < kNumNotches; ++k)
    {
        const float target = proc.sharedSlots[(size_t) k].depthDb.load();
        if (target <= 0.0f) continue;
        const float x = X (proc.sharedSlots[(size_t) k].freqHz.load());
        const float h = target / 24.0f * b.getHeight() * 0.45f;
        g.setColour (colours::cut.withAlpha (engaged ? 0.9f : 0.3f));
        g.fillRect (x - 1.5f, b.getY(), 3.0f, h);
        juce::Path tri;
        tri.addTriangle (x - 6, b.getY() + h, x + 6, b.getY() + h, x, b.getY() + h + 8);
        g.fillPath (tri);
    }

    if (spectrum.empty()) return;
    juce::Path p;
    bool first = true;
    for (size_t i = (size_t) std::ceil (fMin / binHz); i < spectrum.size() && i * binHz < fMax; ++i)
    {
        const float x = X ((float) (i * binHz)), y = Y (spectrum[i]);
        if (first) { p.startNewSubPath (x, y); first = false; }
        else p.lineTo (x, y);
    }
    g.setColour (colours::amber);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

//============================================================================== Look and feel
RingstopLookAndFeel::RingstopLookAndFeel()
{
    setColour (juce::ToggleButton::textColourId, colours::ink);
    setColour (juce::ToggleButton::tickColourId, colours::amber);
    setColour (juce::ToggleButton::tickDisabledColourId, colours::muted);
    setColour (juce::TextButton::buttonColourId, colours::well);
    setColour (juce::TextButton::textColourOffId, colours::ink);
    setColour (juce::Label::textColourId, colours::ink);
    setColour (juce::Slider::textBoxTextColourId, colours::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void RingstopLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float a0, float a1, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (8.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto c = bounds.getCentre();
    const float angle = a0 + pos * (a1 - a0);

    juce::Path track, value;
    track.addCentredArc (c.x, c.y, r - 4, r - 4, 0, a0, a1, true);
    value.addCentredArc (c.x, c.y, r - 4, r - 4, 0, a0, angle, true);
    const juce::PathStrokeType stroke (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (colours::line);  g.strokePath (track, stroke);
    g.setColour (colours::amber); g.strokePath (value, stroke);

    const float capR = r * 0.62f;
    g.setColour (colours::well);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    juce::Line<float> ptr (c, c.getPointOnCircumference (capR * 0.85f, angle));
    g.setColour (colours::ink);
    g.drawLine (ptr, 3.5f);
}

//============================================================================== Editor
RingstopEditor::RingstopEditor (RingstopProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p)
{
    setLookAndFeel (&lnf);

    strength.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    strength.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 22);
    strength.setTextValueSuffix (" %");
    strengthLabel.setText ("Strength", juce::dontSendNotification);
    strengthLabel.setJustificationType (juce::Justification::centred);
    strengthLabel.setColour (juce::Label::textColourId, colours::muted);
    notchInfo.setColour (juce::Label::textColourId, colours::muted);

    for (auto* c : std::initializer_list<juce::Component*> { &spectrum, &strength, &strengthLabel,
                                                             &engaged, &autoRelease, &clear, &notchInfo })
        addAndMakeVisible (c);

    strengthAtt = std::make_unique<SA> (proc.apvts, "strength", strength);
    engagedAtt  = std::make_unique<BA> (proc.apvts, "engaged", engaged);
    releaseAtt  = std::make_unique<BA> (proc.apvts, "autorelease", autoRelease);
    clear.onClick = [this] { proc.detector.requestClear(); };

    setResizable (true, true);
    setResizeLimits (560, 380, 1400, 900);
    setSize (760, 460);
    startTimerHz (10);
}

RingstopEditor::~RingstopEditor() { setLookAndFeel (nullptr); }

void RingstopEditor::timerCallback()
{
    int used = 0;
    juce::StringArray freqs;
    for (auto& s : proc.sharedSlots)
        if (s.depthDb.load() > 0.0f)
        {
            ++used;
            const float f = s.freqHz.load();
            freqs.add ((f >= 1000 ? juce::String (f / 1000.0f, 2) + "k" : juce::String (juce::roundToInt (f)))
                       + " -" + juce::String (juce::roundToInt (s.depthDb.load())) + "dB");
        }
    notchInfo.setText (juce::String (used) + " of " + juce::String (kNumNotches) + " notches"
                       + (used > 0 ? ":  " + freqs.joinIntoString ("   ") : juce::String()),
                       juce::dontSendNotification);
    repaint (getLocalBounds().removeFromTop (48));
}

void RingstopEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    auto top = getLocalBounds().reduced (16, 0).removeFromTop (48);
    g.setColour (colours::ink);
    g.setFont (juce::Font (juce::FontOptions (28.0f, juce::Font::bold)));
    g.drawText ("Ringstop", top, juce::Justification::centredLeft);

    const bool on = proc.apvts.getRawParameterValue ("engaged")->load() > 0.5f;
    auto status = top.removeFromRight (140);
    g.setColour (on ? colours::ok : colours::amber);
    g.fillEllipse (status.getX() + 0.0f, status.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour (colours::muted);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText (on ? "Listening" : "Bypassed", status.withTrimmedLeft (18), juce::Justification::centredLeft);
}

void RingstopEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (36);

    auto bottom = area.removeFromBottom (150);
    spectrum.setBounds (area.withTrimmedBottom (10));

    notchInfo.setBounds (bottom.removeFromBottom (24));
    auto knob = bottom.removeFromLeft (140);
    strengthLabel.setBounds (knob.removeFromBottom (18));
    strength.setBounds (knob);

    bottom.removeFromLeft (20);
    auto col = bottom.withSizeKeepingCentre (bottom.getWidth(), 100);
    engaged.setBounds (col.removeFromTop (28).removeFromLeft (200));
    autoRelease.setBounds (col.removeFromTop (28).removeFromLeft (240));
    col.removeFromTop (8);
    clear.setBounds (col.removeFromTop (30).removeFromLeft (140));
}
