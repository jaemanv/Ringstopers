// Offline sanity test: feeds a growing 1.85 kHz "feedback" tone plus noise
// through the plugin and checks that a notch is placed and the tone is cut.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <random>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    RingstopProcessor proc;
    const double sr = 48000.0; const int block = 256;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    std::mt19937 rng (1); std::normal_distribution<float> noise (0.0f, 0.02f);
    const double f0 = 1850.0; double phase = 0;
    double inPow = 0, outPow = 0; int measured = 0;

    for (int b = 0; b < (int) (6.0 * sr / block); ++b)
    {
        const double t = b * block / sr;
        const float amp = (float) juce::jmin (0.5, 0.005 * std::pow (10.0, t));  // grows 20 dB/s
        float toneIn[block];
        for (int i = 0; i < block; ++i)
        {
            toneIn[i] = amp * (float) std::sin (phase); phase += juce::MathConstants<double>::twoPi * f0 / sr;
            const float x = toneIn[i] + noise (rng);
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
        if (t > 4.0)   // measure tone attenuation in the last 2 s via correlation
        {
            double ci = 0, co = 0, ph = phase - block * juce::MathConstants<double>::twoPi * f0 / sr;
            double si = 0, so = 0;
            for (int i = 0; i < block; ++i)
            {
                const double s = std::sin (ph), c = std::cos (ph); ph += juce::MathConstants<double>::twoPi * f0 / sr;
                ci += toneIn[i] * s; si += toneIn[i] * c;
                co += buf.getSample (0, i) * s; so += buf.getSample (0, i) * c;
            }
            inPow += ci * ci + si * si; outPow += co * co + so * so; ++measured;
        }
        juce::Thread::sleep (1);   // let the analysis thread keep up (real time is ~5 ms per block)
    }

    int active = 0;
    for (auto& s : proc.sharedSlots)
        if (s.depthDb.load() > 0)
        {
            ++active;
            std::cout << "Notch at " << s.freqHz.load() << " Hz, depth " << s.depthDb.load() << " dB\n";
        }
    const double reduction = 10.0 * std::log10 (inPow / (outPow + 1e-12));
    std::cout << "Active notches: " << active << "\nTone reduction: " << reduction << " dB\n";
    proc.releaseResources();
    return (active >= 1 && reduction > 10.0) ? 0 : 1;
}
