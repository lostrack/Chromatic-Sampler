#pragma once

#include "PluginProcessor.h"
#include "LicenseGate.h"

// The plugin window is the prototype UI (Resources/ui/index.html) running in a WebView.
// The page talks to the audio engine through JUCE's native event bridge:
//   JS -> C++ : "param", "noteOn", "noteOff", "openFile", "dropFile", "demo", "clear"
//   C++ -> JS : "state" (about 30 times a second)
class ChromaticEditor : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private seleven::LicenseManager::Listener
{
public:
    explicit ChromaticEditor(ChromaticProcessor&);
    ~ChromaticEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void createWeb();
    void showGate();
    void openPlugin();
    void licenseChanged() override;
    void timerCallback() override;
    void handleParam(const juce::var& msg);
    void handleDrop(const juce::var& msg);
    void openFileChooser();
    juce::var buildState();

    ChromaticProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<seleven::LicenseGate> gate;
    std::unique_ptr<seleven::DemoBar> demoBar;

    int lastSeq = -1;
    juce::Array<juce::var> peaks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromaticEditor)
};
