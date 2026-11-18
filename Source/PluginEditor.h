#pragma once

#include "PluginProcessor.h"
#include "LicenseGate.h"
#include "SelevenNews.h"

// The plugin window is the prototype UI (Resources/ui/index.html) running in a WebView.
// The page talks to the audio engine through JUCE's native event bridge:
//   JS -> C++ : "param", "noteOn", "noteOff", "openFile", "dropFile", "demo", "clear",
//               "licActivate" {key}, "licLink" {which: buy|lost}, "newsDismiss" {id}, "newsOpen"
//   C++ -> JS : "state" (about 30 times a second, includes "licensed"), "licResult" {ok, msg}, "news" (item or null)
// Activation and news are drawn by the page itself (a modal over the blurred UI), since the WebView is a
// native window that JUCE components cannot sit on top of.
class ChromaticEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit ChromaticEditor(ChromaticProcessor&);
    ~ChromaticEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void createWeb();
    void sendNews();
    void timerCallback() override;
    void handleParam(const juce::var& msg);
    void handleDrop(const juce::var& msg);
    void openFileChooser();
    juce::var buildState();

    ChromaticProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<seleven::NewsFeed> news;

    int lastSeq = -1;
    juce::Array<juce::var> peaks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChromaticEditor)
};
