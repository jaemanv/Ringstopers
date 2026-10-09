#include "FeedbackDetector.h"
#include <algorithm>
#include <cmath>

namespace ringstop
{
FeedbackDetector::FeedbackDetector (SharedSlots& s)
    : juce::Thread ("Ringstop analysis"), shared (s)
{
    fifoBuffer.resize ((size_t) fifo.getTotalSize());
    history.resize (fftSize, 0.0f);
    fftData.resize (2 * fftSize, 0.0f);
    spectrumDb.resize (fftSize / 2, -120.0f);
    displaySpectrum.resize (fftSize / 2, -120.0f);
    window.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)   // Hann window
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
    candidates.reserve (32);
}

FeedbackDetector::~FeedbackDetector() { release(); }

void FeedbackDetector::prepare (double sampleRate)
{
    release();
    currentSampleRate.store (sampleRate);
    fifo.reset();
    std::fill (history.begin(), history.end(), 0.0f);
    historyWritePos = samplesSinceFrame = 0;
    candidates.clear();
    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    startThread (juce::Thread::Priority::normal);
}

void FeedbackDetector::release()
{
    if (isThreadRunning())
        stopThread (1000);
}

void FeedbackDetector::pushSamples (const float* data, int numSamples) noexcept
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    if (size1 > 0) std::copy (data, data + size1, fifoBuffer.data() + start1);
    if (size2 > 0) std::copy (data + size1, data + size1 + size2, fifoBuffer.data() + start2);
    fifo.finishedWrite (size1 + size2);
}

void FeedbackDetector::copySpectrum (std::vector<float>& dest, double& binHz) const
{
    const juce::ScopedLock sl (displayLock);
    dest = displaySpectrum;
    binHz = currentSampleRate.load() / fftSize;
}

void FeedbackDetector::run()
{
    while (! threadShouldExit())
    {
        drainFifo();

        const double now = juce::Time::getMillisecondCounterHiRes();
        if (clearRequested.exchange (false))
            for (int k = 0; k < kNumNotches; ++k) { slots[(size_t) k] = {}; publish (k); }

        releaseTick (now, now - lastTickMs);
        lastTickMs = now;
        wait (3);
    }
}

void FeedbackDetector::drainFifo()
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

    auto consume = [this] (const float* src, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            history[(size_t) historyWritePos] = src[i];
            historyWritePos = (historyWritePos + 1) % fftSize;
            if (++samplesSinceFrame >= hopSize)
            {
                samplesSinceFrame = 0;
                analyseFrame();
            }
        }
    };
    consume (fifoBuffer.data() + start1, size1);
    consume (fifoBuffer.data() + start2, size2);
    fifo.finishedRead (size1 + size2);
}

void FeedbackDetector::analyseFrame()
{
    ++frameCounter;

    // Unroll circular history into the FFT buffer, windowed
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = history[(size_t) ((historyWritePos + i) % fftSize)] * window[(size_t) i];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    // Hann coherent gain 0.5 → a full-scale sine reads ~0 dBFS
    const float norm = 4.0f / (float) fftSize;
    const int numBins = fftSize / 2;
    for (int i = 0; i < numBins; ++i)
        spectrumDb[(size_t) i] = 20.0f * std::log10 (fftData[(size_t) i] * norm + 1.0e-9f);

    {
        const juce::ScopedLock sl (displayLock);
        displaySpectrum = spectrumDb;
    }

    if (! enabled.load())
        return;

    const double sr    = currentSampleRate.load();
    const double binHz = sr / fftSize;
    const float  s     = strength.load();
    const auto&  sp    = spectrumDb;

    const int lo = std::max (13, (int) std::ceil (70.0 / binHz));
    const int hi = std::min (numBins - 14, (int) std::floor (14000.0 / binHz));
    if (hi <= lo) return;

    double sum = 0;
    for (int i = lo; i < hi; ++i) sum += sp[(size_t) i];
    const float mean = (float) (sum / (hi - lo));

    // Strength trades speed/aggressiveness against false triggers on held notes
    const float  pnprThresh = 22.0f - 12.0f * s;   // peak vs. neighbours (dB)
    const float  paprThresh = 32.0f - 12.0f * s;   // peak vs. spectrum average (dB)
    const float  floorDb    = -55.0f - 15.0f * s;  // ignore anything quieter
    const double frameMs    = 1000.0 * hopSize / sr;
    const int    needFrames = std::max (2, (int) std::round ((350.0 - 250.0 * s) / frameMs));
    const int    expireFrames = std::max (2, (int) std::round (60.0 / frameMs));

    struct Peak { int bin; float db; };
    Peak found[4]; int numFound = 0;

    for (int i = lo; i < hi; ++i)
    {
        const float v = sp[(size_t) i];
        if (v < floorDb) continue;
        if (v < sp[(size_t) i - 1] || v < sp[(size_t) i + 1] || v < sp[(size_t) i - 2] || v < sp[(size_t) i + 2]) continue;

        float nb = 0;
        for (int k = 4; k <= 12; ++k) nb += sp[(size_t) (i - k)] + sp[(size_t) (i + k)];
        nb /= 18.0f;
        if (v - nb < pnprThresh || v - mean < paprThresh) continue;

        // keep the 4 loudest
        if (numFound < 4) found[numFound++] = { i, v };
        else
        {
            auto* weakest = std::min_element (found, found + 4, [] (auto& a, auto& b) { return a.db < b.db; });
            if (v > weakest->db) *weakest = { i, v };
        }
    }

    for (int f = 0; f < numFound; ++f)
    {
        auto it = std::find_if (candidates.begin(), candidates.end(),
                                [&] (const Candidate& c) { return std::abs (c.bin - found[f].bin) <= 2; });
        if (it != candidates.end()) { it->bin = found[f].bin; ++it->count; it->lastSeenFrame = frameCounter; }
        else if (candidates.size() < 32) candidates.push_back ({ found[f].bin, 1, frameCounter });
    }

    candidates.erase (std::remove_if (candidates.begin(), candidates.end(),
                                      [&] (const Candidate& c) { return frameCounter - c.lastSeenFrame > expireFrames; }),
                      candidates.end());

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto& c : candidates)
    {
        if (c.count < needFrames) continue;

        // Parabolic interpolation for a frequency estimate finer than one bin
        const int i = c.bin;
        const float a = sp[(size_t) i - 1], b = sp[(size_t) i], d0 = sp[(size_t) i + 1];
        const float den = a - 2.0f * b + d0;
        const float delta = den != 0.0f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - d0) / den) : 0.0f;

        placeCut ((float) ((i + delta) * binHz), now);
        c.count = needFrames / 2;   // if it keeps ringing, deepen again soon
    }
}

void FeedbackDetector::placeCut (float freqHz, double nowMs)
{
    const float maxDepth = 9.0f + 15.0f * strength.load();   // 9..24 dB

    int index = -1;
    for (int k = 0; k < kNumNotches; ++k)
        if (slots[(size_t) k].active && std::abs (std::log2 (slots[(size_t) k].freqHz / freqHz)) < 0.04f)
            { index = k; break; }

    if (index >= 0)
    {
        auto& sl = slots[(size_t) index];
        sl.depthDb = std::min (maxDepth, sl.depthDb + 3.0f);
        sl.freqHz  = 0.7f * sl.freqHz + 0.3f * freqHz;
    }
    else
    {
        for (int k = 0; k < kNumNotches && index < 0; ++k)
            if (! slots[(size_t) k].active) index = k;

        if (index < 0)   // all in use: recycle the one quiet for longest
        {
            index = 0;
            for (int k = 1; k < kNumNotches; ++k)
                if (slots[(size_t) k].lastHitMs < slots[(size_t) index].lastHitMs) index = k;
        }
        slots[(size_t) index] = { true, freqHz, 6.0f, nowMs };
    }

    slots[(size_t) index].lastHitMs = nowMs;
    publish (index);
}

void FeedbackDetector::releaseTick (double nowMs, double elapsedMs)
{
    if (! autoRelease.load()) return;

    for (int k = 0; k < kNumNotches; ++k)
    {
        auto& sl = slots[(size_t) k];
        if (sl.active && nowMs - sl.lastHitMs > 20000.0)   // quiet for 20 s → fade out at 1 dB/s
        {
            sl.depthDb -= (float) (elapsedMs / 1000.0);
            if (sl.depthDb <= 1.0f) sl = {};
            publish (k);
        }
    }
}

void FeedbackDetector::publish (int k)
{
    const auto& sl = slots[(size_t) k];
    if (sl.active) shared[(size_t) k].freqHz.store (sl.freqHz);
    shared[(size_t) k].depthDb.store (sl.active ? sl.depthDb : 0.0f);
}
} // namespace ringstop
