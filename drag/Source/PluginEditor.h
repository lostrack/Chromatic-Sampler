#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "LicenseGate.h"
#include "SelevenNews.h"

// A single strip showing the whole loop. The waveform stays still; the record
// line sweeps left to right and wraps, overwriting the oldest audio.
//   drag            : select a region
//   drag selection  : drag it out as a WAV (DAW / Explorer)
//   Ctrl+click      : pause / resume capture
//   Alt+hold        : preview from that point while the mouse is held
//   Ctrl+Shift+click: zoom in (right-click with Ctrl+Shift: zoom out)
//   middle drag     : scroll when zoomed
//   right click     : Sampler menu
class DragSamplerEditor : public juce::AudioProcessorEditor, private juce::Timer,
                          private seleven::LicenseManager::Listener
{
public:
    explicit DragSamplerEditor (DragSamplerProcessor&);
    ~DragSamplerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void stateRestored();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void licenseChanged() override;
    void updateGate();
    std::unique_ptr<seleven::LicenseGate> gate;
    std::unique_ptr<seleven::DemoBar> demoBar;
    std::unique_ptr<seleven::NewsDot> news;
    void showGate();
    void layoutLicense();
    bool holdingPreview = false;

    // view in ring-position samples
    double viewLen() const;
    double clampStart (double s) const;
    int xToPos (float x) const;
    float posToX (double p) const;

    void zoom (double factor, float aroundX);
    void dragOut();
    void showMenu();
    void applyOpenGL();
    void applyFps();
    void openManual();
    void getRange (int a, int b, float& lo0, float& hi0, float& lo1, float& hi1) const;
    void drawLane (juce::Graphics&, const std::vector<float>& hi, const std::vector<float>& lo,
                   float top, float h, int count, int x0);

    DragSamplerProcessor& proc;
    juce::OpenGLContext gl;

    double viewStart = 0.0;          // ring samples
    int selStart = -1, selEnd = -1;  // ring positions

    enum class Mode { none, selecting, scrolling, maybeDragOut };
    Mode mode = Mode::none;
    int anchor = 0;
    float scrollStartX = 0;
    double scrollStartView = 0;
    bool dragStarted = false;

    std::vector<float> hiL, loL, hiR, loR; // per-pixel scratch, reused
    std::unique_ptr<juce::FileChooser> chooser;
};
