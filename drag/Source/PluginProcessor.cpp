#include "PluginProcessor.h"
#include "PluginEditor.h"

DragSamplerProcessor::DragSamplerProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    disk.setFolder (juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                        .getChildFile ("Drag_Sampler"));
}

bool DragSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void DragSamplerProcessor::reallocate()
{
    const int cap = juce::jmax (1, (int) (sr.load() * durationSec.load()));
    ring.setSize (2, cap);
    ring.clear();
    peakCap = cap / peakBlock + 2;
    peaks.assign ((size_t) peakCap * 4, 0.0f);
    curBucket = 0;
    std::fill (std::begin (acc), std::end (acc), 0.0f);
    written.store (0);
    previewPos.store (-1);
}

void DragSamplerProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate != sr.load() || ring.getNumSamples() == 0)
    {
        sr.store (sampleRate);
        demoNag.prepare (sampleRate);
        demoNag.prepare (sampleRate);
        reallocate();
    }
    disk.prepare (sampleRate);
}

void DragSamplerProcessor::setDuration (double seconds)
{
    if (seconds == durationSec.load())
        return;
    const juce::ScopedLock sl (getCallbackLock()); // audio thread is not inside processBlock
    durationSec.store (seconds);
    reallocate();
}

void DragSamplerProcessor::capture (const float* inL, const float* inR, int n) noexcept
{
    const int cap = ring.getNumSamples();
    const auto w = written.load();
    const int mode = inputMode.load();
    float* L = ring.getWritePointer (0);
    float* R = ring.getWritePointer (1);
    int pos = (int) (w % cap);
    const int startPos = pos;

    for (int i = 0; i < n; ++i)
    {
        float l = inL[i], r = inR[i];
        switch (mode)
        {
            case inMono:  l = r = 0.5f * (l + r); break;
            case inLeft:  r = l; break;
            case inRight: l = r; break;
            default: break;
        }
        L[pos] = l;
        R[pos] = r;

        const int bi = pos / peakBlock;
        if (bi != curBucket)
        {
            float* p = peaks.data() + (size_t) curBucket * 4;
            for (int k = 0; k < 4; ++k) { p[k] = acc[k]; acc[k] = 0.0f; }
            curBucket = bi;
        }
        acc[0] = juce::jmin (acc[0], l); acc[1] = juce::jmax (acc[1], l);
        acc[2] = juce::jmin (acc[2], r); acc[3] = juce::jmax (acc[3], r);
        if (++pos == cap) pos = 0;
    }
    {   // publish the partially filled bucket so the head looks live
        float* p = peaks.data() + (size_t) curBucket * 4;
        for (int k = 0; k < 4; ++k) p[k] = acc[k];
    }
    written.store (w + n);

    // disk FIFO gets exactly what was captured (two parts if the ring wrapped)
    const int first = juce::jmin (n, cap - startPos);
    disk.push (L + startPos, R + startPos, first);
    if (first < n)
        disk.push (L, R, n - first);
}

void DragSamplerProcessor::mixPreview (juce::AudioBuffer<float>& buffer) noexcept
{
    auto p = previewPos.load();
    if (p < 0)
        return;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    const auto end = juce::jmin (previewEnd.load(), written.load());
    const auto oldest = getOldest();
    const int cap = ring.getNumSamples();
    for (int i = 0; i < n; ++i, ++p)
    {
        if (p >= end || p < oldest) { p = -1; break; }
        const int idx = (int) (p % cap);
        buffer.addSample (0, i, ring.getSample (0, idx));
        if (nch > 1)
            buffer.addSample (1, i, ring.getSample (1, idx));
    }
    auto expected = previewPos.load();
    if (expected >= 0) // GUI may have stopped/restarted it meanwhile
        previewPos.compare_exchange_strong (expected, p);
}

void DragSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n == 0 || nch == 0 || ring.getNumSamples() == 0)
        return;

    const bool capPlay = capturePlayback.load();

    // Mute silences the pass-through only; capture still sees the input.
    // With Capture Playback the preview is mixed first and recorded too.
    if (capPlay)
        mixPreview (buffer);

    if (! license->isActive())
        demoNag.process (buffer); // demo watermark, recorded too

    if (! paused.load())
        capture (buffer.getReadPointer (0), buffer.getReadPointer (nch > 1 ? 1 : 0), n);

    if (muteOutput.load())
        buffer.clear();

    if (! capPlay || muteOutput.load())
        mixPreview (buffer);
}

bool DragSamplerProcessor::copyRange (juce::int64 start, juce::int64 end, juce::AudioBuffer<float>& out) const
{
    start = juce::jmax (start, getOldest());
    end = juce::jmin (end, getWritten());
    if (end <= start)
        return false;

    const int cap = ring.getNumSamples();
    const int n = (int) (end - start);
    out.setSize (2, n);
    int pos = (int) (start % cap);
    int done = 0;
    while (done < n)
    {
        const int todo = juce::jmin (n - done, cap - pos);
        out.copyFrom (0, done, ring, 0, pos, todo);
        out.copyFrom (1, done, ring, 1, pos, todo);
        done += todo;
        pos = 0;
    }
    return true;
}

void DragSamplerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree t ("DragSampler");
    t.setProperty ("folder", disk.getFolder().getFullPathName(), nullptr);
    t.setProperty ("keepMinutes", disk.getKeepMinutes(), nullptr);
    t.setProperty ("bitDepth", disk.bitDepth.load(), nullptr);
    t.setProperty ("viewSeconds", viewSeconds.load(), nullptr);
    t.setProperty ("uiW", uiWidth.load(), nullptr);
    t.setProperty ("uiH", uiHeight.load(), nullptr);
    t.setProperty ("duration", durationSec.load(), nullptr);
    t.setProperty ("muteOutput", muteOutput.load(), nullptr);
    t.setProperty ("capturePlayback", capturePlayback.load(), nullptr);
    t.setProperty ("autoTrim", autoTrim.load(), nullptr);
    t.setProperty ("inputMode", inputMode.load(), nullptr);
    t.setProperty ("fps", fps.load(), nullptr);
    t.setProperty ("openGL", useOpenGL.load(), nullptr);
    t.setProperty ("theme", theme.load(), nullptr);
    t.setProperty ("visualMode", visualMode.load(), nullptr);
    t.setProperty ("displayGain", displayGainDb.load(), nullptr);
    juce::MemoryOutputStream mos (dest, false);
    t.writeToStream (mos);
}

void DragSamplerProcessor::setStateInformation (const void* data, int size)
{
    auto t = juce::ValueTree::readFromData (data, (size_t) size);
    if (! t.hasType ("DragSampler"))
        return;
    const juce::String folder = t.getProperty ("folder", "");
    disk.setFolder (folder.isNotEmpty() ? juce::File (folder) : juce::File());
    disk.setKeepMinutes (t.getProperty ("keepMinutes", 10.0));
    disk.bitDepth.store (t.getProperty ("bitDepth", 24));
    viewSeconds.store (t.getProperty ("viewSeconds", 0.0));
    uiWidth.store (t.getProperty ("uiW", 1600));
    uiHeight.store (t.getProperty ("uiH", 120));
    muteOutput.store (t.getProperty ("muteOutput", false));
    capturePlayback.store (t.getProperty ("capturePlayback", false));
    autoTrim.store (t.getProperty ("autoTrim", true));
    inputMode.store (t.getProperty ("inputMode", 0));
    fps.store (t.getProperty ("fps", 30));
    useOpenGL.store (t.getProperty ("openGL", false));
    theme.store (t.getProperty ("theme", 0));
    visualMode.store (t.getProperty ("visualMode", (int) visMono));
    displayGainDb.store ((float) (double) t.getProperty ("displayGain", 0.0));

    const double dur = t.getProperty ("duration", 30.0);
    juce::MessageManager::callAsync ([this, dur]
    {
        setDuration (dur);
        if (auto* ed = dynamic_cast<DragSamplerEditor*> (getActiveEditor()))
            ed->stateRestored();
    });
}

juce::AudioProcessorEditor* DragSamplerProcessor::createEditor() { return new DragSamplerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DragSamplerProcessor(); }
