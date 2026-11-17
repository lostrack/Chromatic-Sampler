#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Effects.h"
#include "Sampler.h"
#include "SelevenLicense.h"

class ChromaticProcessor : public juce::AudioProcessor
{
public:
    ChromaticProcessor();
    ~ChromaticProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chromatic Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // sample handling (called from the UI thread)
    bool loadSample(const juce::File& file);
    bool loadSampleBytes(const juce::MemoryBlock& data, const juce::String& name);
    void loadDemo();
    void clearSample();
    juce::String getSampleName() const { return sampleName; }
    float getHostBpm() const { return hostBpmShared.load(); }
    void resetRange();
    std::atomic<int> sampleSeq { 0 };   // bumps on every sample change, the UI redraws the waveform when it moves
    bool isAutoTrim() const { return apvts.getRawParameterValue("autoTrim")->load() > 0.5f; }
    juce::String getSamplePath() const { return samplePath; }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    juce::SharedResourcePointer<seleven::LicenseManager> license;
    seleven::DemoNag demoNag; // mixed in while not activated
    Sampler& getSampler() { return sampler; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    SamplerParams readSamplerParams() const;
    FXParams readFXParams() const;
    void renderChunk(juce::AudioBuffer<float>& buffer, int start, int count, const FXParams& fxp);

    Sampler sampler;
    ChromaticFX fx;
    juce::AudioFormatManager formats;
    juce::String samplePath;
    juce::String sampleName;
    double sampleRate = 44100.0;
    double hostBpm = 140.0;
    std::atomic<float> hostBpmShared { 0.f };   // 0 until a host reports a tempo

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromaticProcessor)
};
