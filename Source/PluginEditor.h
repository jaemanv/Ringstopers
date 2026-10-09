#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace ringstop::colours
{
    const juce::Colour panel  { 0xff1c2331 }, well { 0xff0f141d }, ink { 0xffe6e9ef },
                       muted  { 0xff8d97aa }, line { 0xff2d374a }, amber { 0xfff2a33a },
                       cut    { 0xffff5d73 }, ok   { 0xff4cc38a };
}

class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (RingstopProcessor& p);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    RingstopProcessor& proc;
    std::vector<float> spectrum;
    double binHz = 11.7;
};

class RingstopLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RingstopLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

class RingstopEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RingstopEditor (RingstopProcessor&);
    ~RingstopEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    RingstopProcessor& proc;
    RingstopLookAndFeel lnf;
    SpectrumView spectrum;

    juce::Slider strength;
    juce::Label  strengthLabel;
    juce::ToggleButton engaged { "Engaged" }, autoRelease { "Auto-release notches" };
    juce::TextButton clear { "Clear notches" };
    juce::Label notchInfo;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> strengthAtt;
    std::unique_ptr<BA> engagedAtt, releaseAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RingstopEditor)
};
