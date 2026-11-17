#include "DiskRecorder.h"

DiskRecorder::DiskRecorder() : juce::Thread ("Drag_Sampler disk")
{
    startThread (juce::Thread::Priority::low);
}

DiskRecorder::~DiskRecorder()
{
    stopThread (3000);
    closeWriter();
}

void DiskRecorder::prepare (double sr)
{
    if (sr > 0 && sr != sampleRate.load())
    {
        sampleRate.store (sr);
        folderChanged.store (true); // forces a new file at the new rate
    }
}

void DiskRecorder::push (const float* l, const float* r, int n) noexcept
{
    if (fifo.getFreeSpace() < n)
        return; // drop rather than block the audio thread

    const auto scope = fifo.write (n);
    if (scope.blockSize1 > 0)
    {
        fifoBuffer.copyFrom (0, scope.startIndex1, l, scope.blockSize1);
        fifoBuffer.copyFrom (1, scope.startIndex1, r, scope.blockSize1);
    }
    if (scope.blockSize2 > 0)
    {
        fifoBuffer.copyFrom (0, scope.startIndex2, l + scope.blockSize1, scope.blockSize2);
        fifoBuffer.copyFrom (1, scope.startIndex2, r + scope.blockSize1, scope.blockSize2);
    }
}

void DiskRecorder::setFolder (const juce::File& f)
{
    const juce::ScopedLock sl (folderLock);
    folder = f;
    folderChanged.store (true);
}

juce::File DiskRecorder::getFolder() const
{
    const juce::ScopedLock sl (folderLock);
    return folder;
}

void DiskRecorder::closeWriter()
{
    writer.reset(); // flushes and finalises the header
    samplesInFile = 0;
}

void DiskRecorder::openWriter()
{
    closeWriter();
    if (activeFolder == juce::File() || ! activeFolder.createDirectory())
        return;

    const auto name = juce::String (filePrefix) + "_"
                    + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".wav";
    auto file = activeFolder.getChildFile (name).getNonexistentSibling();

    std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
    if (out == nullptr)
        return;

    juce::WavAudioFormat wav;
    writer.reset (wav.createWriterFor (out.get(), sampleRate.load(), 2, bitDepth.load(), {}, 0));
    if (writer != nullptr)
        out.release();
}

void DiskRecorder::prune()
{
    if (activeFolder == juce::File())
        return;

    const auto cutoff = juce::Time::getCurrentTime()
                      - juce::RelativeTime::minutes (keepMinutes.load());

    for (const auto& entry : juce::RangedDirectoryIterator (activeFolder, false,
                                                             juce::String (filePrefix) + "*.wav"))
    {
        const auto& f = entry.getFile();
        if (! f.getFileName().startsWith (filePrefix))
            continue;
        if (f.getLastModificationTime() < cutoff)
            f.deleteFile();
    }
}

void DiskRecorder::run()
{
    juce::AudioBuffer<float> chunk (2, 8192);

    while (! threadShouldExit())
    {
        if (folderChanged.exchange (false))
        {
            closeWriter();
            const juce::ScopedLock sl (folderLock);
            activeFolder = folder;
        }

        const int ready = fifo.getNumReady();
        if (ready == 0)
        {
            wait (50);
        }
        else
        {
            const int n = juce::jmin (ready, chunk.getNumSamples());
            const auto scope = fifo.read (n);
            if (scope.blockSize1 > 0)
                for (int c = 0; c < 2; ++c)
                    chunk.copyFrom (c, 0, fifoBuffer, c, scope.startIndex1, scope.blockSize1);
            if (scope.blockSize2 > 0)
                for (int c = 0; c < 2; ++c)
                    chunk.copyFrom (c, scope.blockSize1, fifoBuffer, c, scope.startIndex2, scope.blockSize2);

            if (activeFolder != juce::File())
            {
                const auto chunkLen = (juce::int64) (sampleRate.load() * 30.0);
                int done = 0;
                while (done < n)
                {
                    if (writer == nullptr || samplesInFile >= chunkLen)
                        openWriter();
                    if (writer == nullptr)
                        break;

                    const int todo = (int) juce::jmin ((juce::int64) (n - done), chunkLen - samplesInFile);
                    const float* ptrs[2] = { chunk.getReadPointer (0, done), chunk.getReadPointer (1, done) };
                    writer->writeFromFloatArrays (ptrs, 2, todo);
                    samplesInFile += todo;
                    done += todo;
                }
            }
        }

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastPrune > 5000)
        {
            lastPrune = now;
            prune();
        }
    }
}
