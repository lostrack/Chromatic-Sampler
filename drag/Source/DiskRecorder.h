#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

// Background disk recorder. The audio thread only calls push() (lock-free FIFO).
// A worker thread writes 30-second WAV chunks and deletes files older than "keep last".
// Only files whose name starts with "Drag_Sampler" are ever touched.
class DiskRecorder : private juce::Thread
{
public:
    DiskRecorder();
    ~DiskRecorder() override;

    void prepare (double sampleRate);

    // Audio thread. Never blocks, never allocates. Drops audio if FIFO is full.
    void push (const float* left, const float* right, int numSamples) noexcept;

    void setFolder (const juce::File& f);
    juce::File getFolder() const;
    void setKeepMinutes (double m) { keepMinutes.store (m); }
    double getKeepMinutes() const   { return keepMinutes.load(); }
    std::atomic<int> bitDepth { 24 }; // 16, 24 or 32 (float)

    static constexpr const char* filePrefix = "Drag_Sampler";

private:
    void run() override;
    void closeWriter();
    void openWriter();
    void prune();

    static constexpr int fifoSize = 1 << 20; // ~10 s at 96 kHz stereo frames
    juce::AbstractFifo fifo { fifoSize };
    juce::AudioBuffer<float> fifoBuffer { 2, fifoSize };

    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> keepMinutes { 10.0 };

    mutable juce::CriticalSection folderLock;
    juce::File folder;
    std::atomic<bool> folderChanged { false };

    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::File activeFolder;
    juce::int64 samplesInFile = 0;
    juce::uint32 lastPrune = 0;
};
