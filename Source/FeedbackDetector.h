#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace ringstop
{
constexpr int   kNumNotches = 16;
constexpr float kNotchQ     = 20.0f;

// Written by the analysis thread, read by the audio thread. Lock-free.
struct NotchSlotShared
{
    std::atomic<float> freqHz  { 1000.0f };
    std::atomic<float> depthDb { 0.0f };    // positive cut amount; 0 = slot unused
};

using SharedSlots = std::array<NotchSlotShared, kNumNotches>;

/** Finds feedback in the (post-notch) signal on a background thread and
    tells the audio thread where to place notch filters. */
class FeedbackDetector : private juce::Thread
{
public:
    static constexpr int fftOrder = 12;              // 4096-point FFT
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = 512;

    explicit FeedbackDetector (SharedSlots& slotsToControl);
    ~FeedbackDetector() override;

    void prepare (double sampleRate);
    void release();

    // Audio thread: never blocks, never allocates. Drops samples if the FIFO is full.
    void pushSamples (const float* data, int numSamples) noexcept;

    // Any thread
    void setStrength (float zeroToOne) noexcept   { strength.store (zeroToOne); }
    void setEnabled (bool shouldDetect) noexcept  { enabled.store (shouldDetect); }
    void setAutoRelease (bool shouldRelease) noexcept { autoRelease.store (shouldRelease); }
    void requestClear() noexcept                  { clearRequested.store (true); }

    // GUI: copy the latest magnitude spectrum (dBFS per bin).
    void copySpectrum (std::vector<float>& dest, double& binHz) const;

private:
    void run() override;
    void drainFifo();
    void analyseFrame();
    void placeCut (float freqHz, double nowMs);
    void releaseTick (double nowMs, double elapsedMs);
    void publish (int slotIndex);

    struct SlotState { bool active = false; float freqHz = 0, depthDb = 0; double lastHitMs = 0; };
    struct Candidate { int bin; int count; int lastSeenFrame; };

    SharedSlots& shared;
    std::array<SlotState, kNumNotches> slots;
    std::vector<Candidate> candidates;

    juce::AbstractFifo fifo { 1 << 15 };
    std::vector<float> fifoBuffer;

    std::vector<float> history;          // circular, fftSize long
    int historyWritePos = 0;
    int samplesSinceFrame = 0;
    int frameCounter = 0;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, fftData, spectrumDb;

    mutable juce::CriticalSection displayLock;
    std::vector<float> displaySpectrum;

    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<float>  strength { 0.6f };
    std::atomic<bool>   enabled { true }, autoRelease { true }, clearRequested { false };
    double lastTickMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeedbackDetector)
};
} // namespace ringstop
