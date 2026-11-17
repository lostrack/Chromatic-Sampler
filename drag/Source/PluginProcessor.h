#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DiskRecorder.h"
#include "SelevenLicense.h"

// Transparent pass-through recorder. Audio passes unchanged; the last N seconds
// are kept in a ring buffer. Preview audio is mixed in AFTER capture (unless
// "Capture Playback" is on), so it is never recorded back into the buffer.
class DragSamplerProcessor : public juce::AudioProcessor
{
public:
    static constexpr int peakBlock = 256; // samples per waveform peak bucket

    enum InputMode  { inStereo = 0, inMono, inLeft, inRight };
    enum VisualMode { visStereo = 0, visMono };

    DragSamplerProcessor();
    ~DragSamplerProcessor() override = default;

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Drag Sampler"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- ring buffer access (GUI thread) ----
    juce::int64 getWritten() const noexcept     { return written.load(); }
    int getCapacity() const noexcept            { return ring.getNumSamples(); }
    double getSampleRate2() const noexcept      { return sr.load(); }
    const juce::AudioBuffer<float>& getRing() const noexcept { return ring; }
    juce::int64 getOldest() const noexcept      { return juce::jmax ((juce::int64) 0, getWritten() - getCapacity()); }

    // Peak buckets indexed by RING position (bucket = ringPos / peakBlock), 4 floats each
    // (minL, maxL, minR, maxR). Overwritten every lap, like the ring itself.
    int getPeakCapacity() const noexcept        { return peakCap; }
    const float* getPeaks() const noexcept      { return peaks.data(); }

    // Ring position of the write head (the moving line).
    int getHead() const noexcept { const int c = getCapacity(); return c > 0 ? (int) (getWritten() % c) : 0; }
    // Absolute sample index currently stored at ring position p (may be < oldest if never written).
    juce::int64 ringToAbs (int p) const noexcept
    {
        const auto w = getWritten();
        const int c = getCapacity();
        auto a = (w / c) * c + p;
        return a >= w ? a - c : a;
    }

    // Copies absolute range [start, end) into a new buffer. Returns false if out of range.
    bool copyRange (juce::int64 start, juce::int64 end, juce::AudioBuffer<float>& out) const;

    void startPreview (juce::int64 from, juce::int64 to) { previewEnd.store (to); previewPos.store (from); }
    void stopPreview() { previewPos.store (-1); }
    juce::int64 getPreviewPos() const { return previewPos.load(); }

    // Message thread: changes the ring length (clears history).
    void setDuration (double seconds);
    double getDuration() const { return durationSec.load(); }

    DiskRecorder disk;

    std::atomic<bool> paused { false };
    std::atomic<bool> muteOutput { false };
    std::atomic<bool> capturePlayback { false };
    std::atomic<bool> autoTrim { true };
    std::atomic<int>  inputMode { inStereo };

    // persisted UI state
    std::atomic<double> viewSeconds { 0.0 }; // 0 = whole loop visible
    std::atomic<int> uiWidth { 1600 }, uiHeight { 120 };
    std::atomic<int> fps { 30 };
    std::atomic<bool> useOpenGL { false };
    std::atomic<int> theme { 0 };
    std::atomic<int> visualMode { visMono };
    std::atomic<float> displayGainDb { 0.0f };

    juce::SharedResourcePointer<seleven::LicenseManager> license;
    seleven::DemoNag demoNag; // mixed in while not activated, so demo drag-outs carry it too

private:
    void reallocate();
    void capture (const float* l, const float* r, int n) noexcept;
    void mixPreview (juce::AudioBuffer<float>&) noexcept;

    juce::AudioBuffer<float> ring;
    std::vector<float> peaks;
    int peakCap = 0;
    int curBucket = 0;
    float acc[4] { 0, 0, 0, 0 };

    std::atomic<juce::int64> written { 0 };
    std::atomic<double> sr { 44100.0 }, durationSec { 30.0 };
    std::atomic<juce::int64> previewPos { -1 }, previewEnd { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragSamplerProcessor)
};
