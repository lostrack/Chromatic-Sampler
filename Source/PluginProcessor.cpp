#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Note-synced delay lengths in beats. The first six keep their old indices so saved projects still load;
    // the rest were added later. The page lists them in musical order.
    constexpr double divisionBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.0 / 3.0, 2.0 / 3.0,
                                         0.125, 1.0 / 12.0, 1.0 / 6.0, 0.375, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0, 8.0 / 3.0, 6.0 };
    constexpr int numDivisions = (int) (sizeof(divisionBeats) / sizeof(divisionBeats[0]));

    juce::NormalisableRange<float> range(float lo, float hi, float skew = 1.f)
    {
        return juce::NormalisableRange<float>(lo, hi, 0.f, skew);
    }
}

ChromaticProcessor::ChromaticProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createLayout())
{
    formats.registerBasicFormats();
}

juce::AudioProcessorValueTreeState::ParameterLayout ChromaticProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "attack", 1 }, "Attack", range(0.001f, 2.f, 0.3f), 0.002f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "decay", 1 }, "Decay", range(0.005f, 4.f, 0.3f), 0.35f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "sustain", 1 }, "Sustain", range(0.f, 1.f), 0.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "release", 1 }, "Release", range(0.005f, 6.f, 0.3f), 0.22f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "curveA", 1 }, "Attack Curve", range(-1.f, 1.f), 0.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "curveD", 1 }, "Decay Curve", range(-1.f, 1.f), 0.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "curveR", 1 }, "Release Curve", range(-1.f, 1.f), 0.f));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "adsrOn", 1 }, "Envelope On", false));

    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "mono", 1 }, "Mono", false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "legato", 1 }, "Legato", false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "glideOn", 1 }, "Portamento On", false));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "glide", 1 }, "Glide", range(0.f, 1.5f, 0.45f), 0.09f));

    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "fine", 1 }, "Fine Tune", range(-100.f, 100.f), 0.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "rootShift", 1 }, "Root Shift", range(-24.f, 24.f), 0.f));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "autoTrim", 1 }, "Auto Trim", true));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "sampleStart", 1 }, "Sample Start", range(0.f, 1.f), 0.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "sampleEnd", 1 }, "Sample End", range(0.f, 1.f), 1.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "fadeIn", 1 }, "Fade In", range(0.f, 5000.f, 0.4f), 0.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "fadeOut", 1 }, "Fade Out", range(0.f, 5000.f, 0.4f), 0.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "volume", 1 }, "Output", range(0.f, 1.f), 0.8f));

    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "revOn", 1 }, "Reverb On", false));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "revRoom", 1 }, "Reverb Size", range(0.f, 1.f), 0.4f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "revAmt", 1 }, "Reverb Amount", range(0.f, 1.f), 0.35f));

    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "dlyOn", 1 }, "Delay On", false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "dlySync", 1 }, "Delay Sync", true));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { "dlyDiv", 1 }, "Delay Division",
                                                       StringArray { "1/16", "1/8", "1/8.", "1/4", "1/8T", "1/4T",
                                                                     "1/32", "1/32T", "1/16T", "1/16.", "1/4.", "1/2T", "1/2", "1/2.", "1/1", "1/1T", "1/1." }, 1));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "dlyMs", 1 }, "Delay Time", range(10.f, 2000.f, 0.6f), 250.f));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "dlyPing", 1 }, "Ping-Pong", false));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "dlyFb", 1 }, "Delay Feedback", range(0.f, 0.9f), 0.35f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "dlyAmt", 1 }, "Delay Amount", range(0.f, 1.f), 0.3f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "dlySpread", 1 }, "Ping-Pong Spread", range(0.f, 1.f), 1.f));

    // saturation + effect order
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "satOn", 1 }, "Saturation On", false));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "satDrive", 1 }, "Saturation Drive", range(0.f, 1.f), 0.35f));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "satMatch", 1 }, "Saturation Match", true));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { "satMode", 1 }, "Saturation Mode",
                                                       StringArray { "Soft", "Tube", "Hard", "Fold", "Bits", "Rate" }, 0));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID { "fxOrder", 1 }, "Effect Order", 0, 23, 12));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "revLo", 1 }, "Reverb Low Cut", range(20.f, 1000.f, 0.35f), 20.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "revHi", 1 }, "Reverb High Cut", range(1000.f, 20000.f, 0.35f), 20000.f));

    // filter
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "fltOn", 1 }, "Filter On", false));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "fltHp", 1 }, "Filter Low Cut", range(20.f, 2000.f, 0.3f), 20.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "fltLp", 1 }, "Filter High Cut", range(1000.f, 20000.f, 0.3f), 20000.f));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID { "fltHpS", 1 }, "Filter Low Cut Slope", 0, 2, 1));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID { "fltLpS", 1 }, "Filter High Cut Slope", 0, 2, 1));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "fltMF", 1 }, "Filter Mid Freq", range(150.f, 8000.f, 0.4f), 1000.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "fltMG", 1 }, "Filter Mid Gain", range(-12.f, 12.f), 0.f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "fltMQ", 1 }, "Filter Mid Width", range(0.3f, 4.f, 0.6f), 0.9f));

    // auto root detection on or off
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "autoTune", 1 }, "Auto Tune", true));

    // settings menu
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "foldRoot", 1 }, "Root In Octave 4", true));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID { "stdNames", 1 }, "Standard Octave Names", false));

    return { p.begin(), p.end() };
}

void ChromaticProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;
    sampler.prepare(sr);
    fx.prepare(sr, samplesPerBlock);
    demoNag.prepare(sr);
}

bool ChromaticProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

SamplerParams ChromaticProcessor::readSamplerParams() const
{
    auto get = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    SamplerParams sp;
    sp.attack = get("attack");
    sp.decay = get("decay");
    sp.sustain = get("sustain");
    sp.release = get("release");
    sp.curveA = get("curveA");
    sp.curveD = get("curveD");
    sp.curveR = get("curveR");
    sp.adsrOn = get("adsrOn") > 0.5f;
    sp.mono = get("mono") > 0.5f;
    sp.legato = get("legato") > 0.5f;
    sp.glideOn = get("glideOn") > 0.5f;
    sp.glideSeconds = get("glide");
    sp.fineCents = get("fine");
    sp.rootShift = get("rootShift");
    sp.startFrac = get("sampleStart");
    sp.endFrac = juce::jmax(sp.startFrac + 0.005f, get("sampleEnd"));
    sp.fadeInMs = get("fadeIn");
    sp.fadeOutMs = get("fadeOut");
    sp.foldRoot = get("foldRoot") > 0.5f;
    sp.autoTune = get("autoTune") > 0.5f;
    return sp;
}

FXParams ChromaticProcessor::readFXParams() const
{
    auto get = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    FXParams fp;
    fp.dlyOn = get("dlyOn") > 0.5f;
    fp.dlyPing = get("dlyPing") > 0.5f;
    fp.dlyFeedback = get("dlyFb");
    fp.dlyAmount = get("dlyAmt");
    fp.dlySpread = get("dlySpread");
    fp.satOn = get("satOn") > 0.5f;
    fp.satDrive = get("satDrive");
    fp.satMatch = get("satMatch") > 0.5f;
    fp.satMode = (int) get("satMode");
    fp.order = (int) get("fxOrder");
    fp.revLowCut = get("revLo");
    fp.revHighCut = get("revHi");
    fp.fltOn = get("fltOn") > 0.5f;
    fp.fltHp = get("fltHp");   fp.fltLp = get("fltLp");
    fp.fltHpSlope = (int) get("fltHpS"); fp.fltLpSlope = (int) get("fltLpS");
    fp.fltMF = get("fltMF");   fp.fltMG = get("fltMG");   fp.fltMQ = get("fltMQ");
    if (get("dlySync") > 0.5f)
    {
        const int div = juce::jlimit(0, numDivisions - 1, (int) get("dlyDiv"));
        fp.dlySeconds = (float) (60.0 / hostBpm * divisionBeats[div]);
    }
    else
    {
        fp.dlySeconds = get("dlyMs") / 1000.f;
    }
    fp.revOn = get("revOn") > 0.5f;
    fp.revRoom = get("revRoom");
    fp.revAmount = get("revAmt");
    return fp;
}

void ChromaticProcessor::renderChunk(juce::AudioBuffer<float>& buffer, int start, int count, const FXParams& fxp)
{
    if (count <= 0) return;
    float* L = buffer.getWritePointer(0) + start;
    float* R = buffer.getWritePointer(1) + start;
    sampler.render(L, R, count);
    fx.process(L, R, count, fxp);
}

void ChromaticProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // notes from the on-screen keyboard go through the same path as MIDI
    keyboardState.processNextMidiBuffer(midi, 0, numSamples, true);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm > 20.0 && *bpm < 400.0) { hostBpm = *bpm; hostBpmShared = (float) *bpm; }

    sampler.setParams(readSamplerParams());
    const FXParams fxp = readFXParams();

    int done = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(0, numSamples, meta.samplePosition);
        if (at > done)
        {
            renderChunk(buffer, done, at - done, fxp);
            done = at;
        }
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) sampler.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) sampler.noteOff(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) sampler.allNotesOff();
    }
    if (done < numSamples) renderChunk(buffer, done, numSamples - done, fxp);

    const float v = apvts.getRawParameterValue("volume")->load();
    buffer.applyGain(v * v * 1.25f);
    if (! license->isActive()) demoNag.process(buffer); // demo watermark

    // speaker safety: never pass NaN/inf or more than 0 dBFS to the host
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (auto* s = buffer.getWritePointer(c), *e = s + numSamples; s != e; ++s)
            *s = std::isfinite(*s) ? juce::jlimit(-1.f, 1.f, *s) : 0.f;
}

void ChromaticProcessor::resetRange()
{
    for (auto [id, v] : { std::pair { "sampleStart", 0.f }, std::pair { "sampleEnd", 1.f },
                          std::pair { "fadeIn", 0.f }, std::pair { "fadeOut", 0.f } })
        if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
}

bool ChromaticProcessor::loadSample(const juce::File& file)
{
    auto sd = SampleLoader::loadFile(file, formats, isAutoTrim());
    if (sd == nullptr) return false;
    resetRange();
    sampler.setSample(sd);
    samplePath = file.getFullPathName();
    sampleName = file.getFileName();
    ++sampleSeq;
    return true;
}

bool ChromaticProcessor::loadSampleBytes(const juce::MemoryBlock& data, const juce::String& name)
{
    auto stream = std::make_unique<juce::MemoryInputStream>(data, true);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
    if (reader == nullptr) return false;
    auto sd = SampleLoader::fromReader(*reader, isAutoTrim(), name);
    resetRange();
    sampler.setSample(sd);
    samplePath.clear();   // dropped audio has no file path, so it is not restored with the project
    sampleName = name;
    ++sampleSeq;
    return true;
}

void ChromaticProcessor::loadDemo()
{
    resetRange();
    sampler.setSample(SampleLoader::demo(isAutoTrim()));
    samplePath.clear();
    sampleName = "demo-pluck.wav";
    ++sampleSeq;
}

void ChromaticProcessor::clearSample()
{
    sampler.setSample(nullptr);
    samplePath.clear();
    sampleName.clear();
    ++sampleSeq;
}

void ChromaticProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("samplePath", samplePath, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void ChromaticProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(tree);

    const auto path = tree.getProperty("samplePath").toString();
    if (path.isNotEmpty())
    {
        juce::File f(path);
        if (f.existsAsFile()) loadSample(f);
    }
}

juce::AudioProcessorEditor* ChromaticProcessor::createEditor()
{
    return new ChromaticEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChromaticProcessor();
}
