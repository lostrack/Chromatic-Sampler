#include "PluginEditor.h"

namespace
{
    struct Theme { const char* name; juce::uint32 bg, wave, sel, play; };
    const Theme themes[] = {
        { "Graphite",      0xff1c1d20, 0xffb4bcc8, 0xffb4bcc8, 0xffffffff },
        { "Cyan Night",    0xff0f1418, 0xff35d6e8, 0xff35d6e8, 0xffffffff },
        { "Midnight Blue", 0xff0d1222, 0xff5b9dff, 0xff5b9dff, 0xffffd166 },
        { "Teal",          0xff0e1a19, 0xff2ee6b0, 0xff2ee6b0, 0xffffffff },
        { "Amber",         0xff17130c, 0xffffb547, 0xffffb547, 0xffffffff },
        { "Crimson",       0xff170e10, 0xffff5470, 0xffff5470, 0xffffffff },
        { "Violet",        0xff130f1c, 0xffb07cff, 0xffb07cff, 0xff7cf3ff },
        { "Lime",          0xff111509, 0xffb6f23a, 0xffb6f23a, 0xffffffff },
        { "Mono",          0xff000000, 0xffe6e6e6, 0xffffffff, 0xffff4040 },
        { "Light Studio",  0xffeceef1, 0xff2a5bd7, 0xff2a5bd7, 0xffe0303a },
    };
    constexpr int numThemes = (int) (sizeof (themes) / sizeof (themes[0]));
    const Theme& currentTheme (int i) { return themes[juce::jlimit (0, numThemes - 1, i)]; }

    // Removes silence (below -60 dBFS) at both ends.
    void trimSilence (juce::AudioBuffer<float>& b)
    {
        constexpr float thr = 0.001f;
        const int n = b.getNumSamples();
        auto loud = [&] (int i) { return std::abs (b.getSample (0, i)) > thr || std::abs (b.getSample (1, i)) > thr; };
        int s = 0, e = n;
        while (s < n && ! loud (s)) ++s;
        while (e > s && ! loud (e - 1)) --e;
        if (s >= e || (s == 0 && e == n))
            return;
        juce::AudioBuffer<float> t (2, e - s);
        t.copyFrom (0, 0, b, 0, s, e - s);
        t.copyFrom (1, 0, b, 1, s, e - s);
        b = std::move (t);
    }
}

DragSamplerEditor::DragSamplerEditor (DragSamplerProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setOpaque (true);
    setResizable (false, true); // host window fixed; only the corner grip resizes
    setResizeLimits (400, 40, 6000, 800);
    setSize (proc.uiWidth.load(), proc.uiHeight.load());
    applyOpenGL();
    applyFps();
    proc.license->addListener (this);
    updateGate();
}

DragSamplerEditor::~DragSamplerEditor()
{
    proc.license->removeListener (this);
    gl.detach();
}

void DragSamplerEditor::licenseChanged() { updateGate(); }

// Unlicensed: activation row on open, then a small DEMO pill that reopens it.
void DragSamplerEditor::updateGate()
{
    if (proc.license->isActive()) { gate.reset(); demoBar.reset(); repaint(); return; }
    if (gate == nullptr && demoBar == nullptr) showGate();
}

void DragSamplerEditor::showGate()
{
    demoBar.reset();
    auto toDemo = [this]
    {
        gate.reset();
        if (proc.license->isActive()) { repaint(); return; }
        demoBar = std::make_unique<seleven::DemoBar> ([this] { showGate(); });
        addAndMakeVisible (*demoBar);
        layoutLicense();
    };
    gate = std::make_unique<seleven::LicenseGate> (toDemo, toDemo);
    addAndMakeVisible (*gate);
    layoutLicense();
}

void DragSamplerEditor::layoutLicense()
{
    if (gate != nullptr) gate->setBounds (getLocalBounds());
    if (demoBar != nullptr)
    {
        const int h = juce::jlimit (12, 20, getHeight() - 6);
        demoBar->setBounds (getWidth() - (getWidth() > 700 ? 380 : 150) - 6, 4, getWidth() > 700 ? 380 : 150, h);
    }
}

void DragSamplerEditor::applyOpenGL()
{
    if (proc.useOpenGL.load()) { if (! gl.isAttached()) gl.attachTo (*this); }
    else if (gl.isAttached())  gl.detach();
}

void DragSamplerEditor::applyFps() { startTimerHz (juce::jlimit (1, 60, proc.fps.load())); }

void DragSamplerEditor::stateRestored()
{
    setSize (proc.uiWidth.load(), proc.uiHeight.load());
    applyOpenGL();
    applyFps();
    repaint();
}

void DragSamplerEditor::timerCallback()
{
    // preview only lasts while Alt + mouse are held
    if (holdingPreview && ! juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown())
    {
        holdingPreview = false;
        proc.stopPreview();
    }
    repaint();
}

void DragSamplerEditor::resized()
{
    proc.uiWidth.store (getWidth());
    proc.uiHeight.store (getHeight());
    layoutLicense();
}

// ---------------------------------------------------------------- view mapping
double DragSamplerEditor::viewLen() const
{
    const double cap = proc.getCapacity();
    const double v = proc.viewSeconds.load() * proc.getSampleRate2();
    return (v <= 0.0 || v >= cap) ? cap : v;
}

double DragSamplerEditor::clampStart (double s) const
{
    return juce::jlimit (0.0, juce::jmax (0.0, proc.getCapacity() - viewLen()), s);
}

int DragSamplerEditor::xToPos (float x) const
{
    const int cap = proc.getCapacity();
    return juce::jlimit (0, juce::jmax (0, cap), (int) (viewStart + viewLen() * x / getWidth()));
}

float DragSamplerEditor::posToX (double p) const
{
    return (float) ((p - viewStart) / viewLen() * getWidth());
}

// Min/max over ring positions [a, b). Uses peak buckets when a pixel spans
// many samples, raw samples only when zoomed far in.
void DragSamplerEditor::getRange (int a, int b, float& lo0, float& hi0, float& lo1, float& hi1) const
{
    lo0 = hi0 = lo1 = hi1 = 0.0f;
    constexpr int B = DragSamplerProcessor::peakBlock;
    if (b - a >= B)
    {
        const float* pk = proc.getPeaks();
        const int last = juce::jmin ((b - 1) / B, proc.getPeakCapacity() - 1);
        for (int k = a / B; k <= last; ++k)
        {
            const float* p = pk + (size_t) k * 4;
            lo0 = juce::jmin (lo0, p[0]); hi0 = juce::jmax (hi0, p[1]);
            lo1 = juce::jmin (lo1, p[2]); hi1 = juce::jmax (hi1, p[3]);
        }
        return;
    }
    const float* L = proc.getRing().getReadPointer (0);
    const float* R = proc.getRing().getReadPointer (1);
    for (int i = a; i < b; ++i)
    {
        lo0 = juce::jmin (lo0, L[i]); hi0 = juce::jmax (hi0, L[i]);
        lo1 = juce::jmin (lo1, R[i]); hi1 = juce::jmax (hi1, R[i]);
    }
}

// Flat filled body + thin contour. Every 4 s a soft light band sweeps left to right
// across the waveform, fading in and out.
void DragSamplerEditor::drawLane (juce::Graphics& g, const std::vector<float>& hi, const std::vector<float>& lo,
                                  float top, float h, int count, int x0)
{
    if (count < 2)
        return;
    const auto& th = currentTheme (proc.theme.load());
    const juce::Colour wc (th.wave);
    const float mid = top + h * 0.5f;
    const float amp = h * 0.44f * juce::Decibels::decibelsToGain (proc.displayGainDb.load());

    juce::Path body, upper, lower;
    for (int i = 0; i < count; ++i)
    {
        const float x = (float) (x0 + i) + 0.5f;
        const float yh = juce::jmax (top, mid - hi[(size_t) i] * amp);
        const float yl = juce::jmin (top + h, mid - lo[(size_t) i] * amp);
        if (i == 0) { body.startNewSubPath (x, yh); upper.startNewSubPath (x, yh); lower.startNewSubPath (x, yl); }
        else        { body.lineTo (x, yh);          upper.lineTo (x, yh);          lower.lineTo (x, yl); }
    }
    for (int i = count - 1; i >= 0; --i)
        body.lineTo ((float) (x0 + i) + 0.5f, juce::jmin (top + h, mid - lo[(size_t) i] * amp));
    body.closeSubPath();

    g.setColour (wc.withAlpha (0.35f));
    g.fillPath (body);

    // shimmer: 4 s period, band travels across in the first 2.4 s, sin() fade in/out
    constexpr double period = 4000.0, travel = 2400.0;
    const double t = std::fmod (juce::Time::getMillisecondCounterHiRes(), period);
    if (t < travel)
    {
        const float u = (float) (t / travel);
        const float fade = std::sin (u * juce::MathConstants<float>::pi);
        const float W = (float) getWidth(), band = W * 0.18f;
        const float cx = -band + u * (W + 2.0f * band);
        juce::ColourGradient sweep (wc.withAlpha (0.0f), cx - band, 0, wc.withAlpha (0.0f), cx + band, 0, false);
        sweep.addColour (0.5, wc.withAlpha (0.22f * fade));
        g.setGradientFill (sweep);
        g.fillPath (body);
    }

    g.setColour (wc.withAlpha (0.9f));
    g.strokePath (upper, juce::PathStrokeType (1.0f));
    g.strokePath (lower, juce::PathStrokeType (1.0f));
}

void DragSamplerEditor::paint (juce::Graphics& g)
{
    const auto& th = currentTheme (proc.theme.load());
    const juce::Colour bg (th.bg), wc (th.wave);
    const float W = (float) getWidth(), H = (float) getHeight();

    g.fillAll (bg);

    const int cap = proc.getCapacity();
    if (cap == 0)
        return;
    viewStart = clampStart (viewStart);

    const bool stereo = proc.visualMode.load() == DragSamplerProcessor::visStereo;
    const float laneH = stereo ? H * 0.5f : H;

    // centre line(s)
    g.setColour (wc.withAlpha (0.12f));
    g.fillRect (0.0f, laneH * 0.5f, W, 1.0f);
    if (stereo) g.fillRect (0.0f, laneH * 1.5f, W, 1.0f);

    if (selStart >= 0 && selEnd > selStart)
    {
        const float x0 = posToX (selStart), x1 = posToX (selEnd);
        g.setColour (juce::Colour (th.sel).withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, 0, x1, H));
        g.setColour (juce::Colour (th.sel).withAlpha (0.5f));
        g.fillRect (x0, 0.0f, 1.0f, H);
        g.fillRect (x1 - 1.0f, 0.0f, 1.0f, H);
    }

    // gather per-pixel peaks (only the part of the loop that has been written)
    const int filled = (int) juce::jmin ((juce::int64) cap, proc.getWritten());
    const int w = getWidth();
    hiL.resize ((size_t) w); loL.resize ((size_t) w); hiR.resize ((size_t) w); loR.resize ((size_t) w);
    int count = 0;
    for (int x = 0; x < w; ++x)
    {
        const int a = xToPos ((float) x);
        const int b = juce::jmin (juce::jmax (a + 1, xToPos ((float) x + 1)), filled);
        if (a >= filled)
            break;
        float l0, h0, l1, h1;
        getRange (a, b, l0, h0, l1, h1);
        if (stereo) { hiL[(size_t) x] = h0; loL[(size_t) x] = l0; hiR[(size_t) x] = h1; loR[(size_t) x] = l1; }
        else        { hiL[(size_t) x] = juce::jmax (h0, h1); loL[(size_t) x] = juce::jmin (l0, l1); }
        count = x + 1;
    }
    drawLane (g, hiL, loL, 0.0f, laneH, count, 0);
    if (stereo)
        drawLane (g, hiR, loR, laneH, laneH, count, 0);

    // record line: sweeps left -> right, wraps at the end of the loop
    const float hx = posToX (proc.getHead());
    const juce::Colour pc = proc.paused.load() ? wc.withAlpha (0.5f) : juce::Colour (th.play);
    // soft shadow behind the moving line so it stands out from the waveform
    {
        const float sw = 10.0f;
        const juce::Colour sh = juce::Colours::black.withAlpha (bg.getBrightness() > 0.5f ? 0.126f : 0.385f);
        juce::ColourGradient shadow (sh.withAlpha (0.0f), hx - sw, 0, sh.withAlpha (0.0f), hx + sw, 0, false);
        shadow.addColour (0.5, sh);
        g.setGradientFill (shadow);
        g.fillRect (hx - sw, 0.0f, 2.0f * sw, H);
    }
    g.setColour (pc);
    g.fillRect (hx - 0.5f, 0.0f, 1.5f, H);

    // preview playhead
    const auto pp = proc.getPreviewPos();
    if (pp >= 0)
    {
        g.setColour (juce::Colour (th.sel).withAlpha (0.9f));
        g.fillRect (posToX ((double) (pp % cap)), 0.0f, 1.0f, H);
    }

    // thin frame
    g.setColour (wc.withAlpha (0.10f));
    g.drawRect (getLocalBounds().toFloat(), 1.0f);

    if (proc.muteOutput.load() || proc.paused.load())
    {
        g.setColour (wc.withAlpha (0.6f));
        g.setFont (10.0f);
        g.drawText (proc.paused.load() ? (proc.muteOutput.load() ? "PAUSED  MUTE" : "PAUSED") : "MUTE",
                    getLocalBounds().reduced (6, 3), juce::Justification::topRight);
    }
}

// ---------------------------------------------------------------- mouse
void DragSamplerEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto& m = e.mods;
    dragStarted = false;
    mode = Mode::none;

    if (m.isCtrlDown() && m.isShiftDown())
        return zoom (m.isRightButtonDown() ? 2.0 : 0.5, e.position.x);
    if (m.isPopupMenu())
        return showMenu();
    if (m.isCtrlDown())
        return proc.paused.store (! proc.paused.load());
    if (m.isAltDown())
    {
        const int p = xToPos (e.position.x);
        const auto from = proc.ringToAbs (p);
        const auto to = (selStart >= 0 && p >= selStart && p < selEnd) ? proc.ringToAbs (selEnd - 1) + 1
                                                                       : proc.getWritten();
        if (from >= proc.getOldest() && to > from)
        {
            proc.startPreview (from, to);
            holdingPreview = true;
        }
        return;
    }
    if (m.isMiddleButtonDown())
    {
        mode = Mode::scrolling;
        scrollStartX = e.position.x;
        scrollStartView = viewStart;
        return;
    }

    const int p = xToPos (e.position.x);
    if (selStart >= 0 && p >= selStart && p < selEnd)
    {
        mode = Mode::maybeDragOut;
        return;
    }
    mode = Mode::selecting;
    anchor = p;
    selStart = selEnd = -1;
}

void DragSamplerEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (mode == Mode::scrolling)
    {
        viewStart = clampStart (scrollStartView - (e.position.x - scrollStartX) * viewLen() / getWidth());
        repaint();
    }
    else if (mode == Mode::selecting)
    {
        const int p = xToPos (e.position.x);
        selStart = juce::jmin (anchor, p);
        selEnd = juce::jmax (anchor, p);
        repaint();
    }
    else if (mode == Mode::maybeDragOut && ! dragStarted && e.getDistanceFromDragStart() > 4)
    {
        dragStarted = true;
        dragOut();
    }
}

void DragSamplerEditor::mouseUp (const juce::MouseEvent&)
{
    if (holdingPreview)
    {
        holdingPreview = false;
        proc.stopPreview();
    }
    if (mode == Mode::selecting && selEnd - selStart < 64)
        selStart = selEnd = -1;
    mode = Mode::none;
    repaint();
}

void DragSamplerEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown())
        zoom (w.deltaY > 0 ? 0.8 : 1.25, e.position.x);
}

void DragSamplerEditor::zoom (double factor, float aroundX)
{
    const double cap = proc.getCapacity(), sr = proc.getSampleRate2();
    if (cap <= 0) return;
    const double anchorPos = viewStart + viewLen() * aroundX / getWidth();
    const double len = juce::jlimit (sr * 0.25, cap, viewLen() * factor);
    proc.viewSeconds.store (len >= cap ? 0.0 : len / sr);
    viewStart = clampStart (anchorPos - len * aroundX / getWidth());
    repaint();
}

// ---------------------------------------------------------------- export
void DragSamplerEditor::dragOut()
{
    // A selection that crosses the record line holds new audio before it and old
    // audio after it; keep only the continuous part up to the line.
    const int head = proc.getHead();
    int s = selStart, e = selEnd;
    if (s < head && e > head)
        e = head;
    const auto a = proc.ringToAbs (s);

    juce::AudioBuffer<float> clip;
    if (! proc.copyRange (a, a + (e - s), clip))
        return;
    if (proc.autoTrim.load())
        trimSilence (clip);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Drag_Sampler");
    dir.createDirectory();
    auto file = dir.getChildFile ("Drag_Sampler_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".wav")
                   .getNonexistentSibling();
    {
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
        if (out == nullptr)
            return;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), proc.getSampleRate2(), 2,
                                                                         proc.disk.bitDepth.load(), {}, 0));
        if (w == nullptr)
            return;
        out.release();
        w->writeFromAudioSampleBuffer (clip, 0, clip.getNumSamples());
    } // writer closed -> header finalised before the OS gets the file

    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void DragSamplerEditor::openManual()
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Drag_Sampler_Manual.html");
    f.replaceWithText (R"(<!doctype html><meta charset="utf-8"><title>Drag Sampler Manual</title>
<style>body{font:15px system-ui;background:#1c1d20;color:#ddd;max-width:720px;margin:40px auto;padding:0 16px}
code{background:#333;padding:1px 5px;border-radius:3px}</style>
<h1>Drag Sampler</h1><p>Seleven Audio. Invisible recorder: audio passes unchanged. The strip shows the whole loop;
the record line sweeps left to right and wraps, overwriting the oldest audio.</p>
<h2>Mouse</h2><ul>
<li>Drag: select a region</li><li>Drag the selection: drop a WAV into your DAW or Explorer</li>
<li><code>Ctrl</code>+click: pause / resume capture</li><li><code>Alt</code>+hold: preview from that point while held</li>
<li><code>Ctrl+Shift</code>+click: zoom in (right-click: zoom out), <code>Ctrl</code>+wheel: zoom</li>
<li>Middle drag: scroll when zoomed</li><li>Right-click: menu</li></ul>
<h2>Auto-save folder</h2><p>30-second WAV files are written in the background. Files older than "Keep files"
are deleted. Only files starting with <code>Drag_Sampler</code> are touched.</p>
<h2>Auto Trim</h2><p>Removes silence (below -60 dBFS) at both ends of a dragged clip.</p>)");
    juce::URL (f).launchInDefaultBrowser();
}

// ---------------------------------------------------------------- menu
void DragSamplerEditor::showMenu()
{
    auto refresh = [this] { repaint(); };

    juce::PopupMenu format;
    for (int b : { 16, 24, 32 })
        format.addItem (b == 32 ? "WAV 32-bit float" : "WAV " + juce::String (b) + "-bit", true,
                        proc.disk.bitDepth.load() == b, [this, b] { proc.disk.bitDepth.store (b); });

    juce::PopupMenu keep;
    const double cur = proc.disk.getKeepMinutes();
    for (double m : { 5.0, 10.0, 30.0, 60.0, 180.0, 720.0, 1440.0 })
        keep.addItem (m < 60 ? juce::String ((int) m) + " min" : juce::String ((int) (m / 60)) + " h",
                      true, cur == m, [this, m] { proc.disk.setKeepMinutes (m); });

    const auto folder = proc.disk.getFolder();
    juce::PopupMenu various;
    various.addItem ("Auto Trim silence", true, proc.autoTrim.load(), [this] { proc.autoTrim.store (! proc.autoTrim.load()); });
    various.addItem ("Auto-save to folder", true, folder != juce::File(), [this, folder]
    {
        proc.disk.setFolder (folder != juce::File() ? juce::File()
                             : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Drag_Sampler"));
    });
    various.addSubMenu ("Keep files", keep);
    various.addItem ("Open recording folder", folder.isDirectory(), false, [folder] { folder.startAsProcess(); });

    juce::PopupMenu perf;
    perf.addSectionHeader ("Waveform FPS:");
    for (int f : { 60, 30, 20, 15, 10, 5 })
        perf.addItem (juce::String (f), true, proc.fps.load() == f, [this, f] { proc.fps.store (f); applyFps(); });
    perf.addSeparator();
    perf.addItem ("Use OpenGL", true, proc.useOpenGL.load(), [this]
    {
        proc.useOpenGL.store (! proc.useOpenGL.load());
        applyOpenGL();
    });

    juce::PopupMenu dgain;
    for (float db : { -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
        dgain.addItem ((db > 0 ? "+" : "") + juce::String ((int) db) + " dB", true, proc.displayGainDb.load() == db,
                       [this, db, refresh] { proc.displayGainDb.store (db); refresh(); });

    juce::PopupMenu visual;
    visual.addItem ("Mono (single wave)", true, proc.visualMode.load() == DragSamplerProcessor::visMono,
                    [this, refresh] { proc.visualMode.store (DragSamplerProcessor::visMono); refresh(); });
    visual.addItem ("Stereo (L / R)", true, proc.visualMode.load() == DragSamplerProcessor::visStereo,
                    [this, refresh] { proc.visualMode.store (DragSamplerProcessor::visStereo); refresh(); });

    juce::PopupMenu input;
    const char* inNames[] = { "Stereo", "Mono (L+R)", "Left only", "Right only" };
    for (int i = 0; i < 4; ++i)
        input.addItem (inNames[i], true, proc.inputMode.load() == i, [this, i] { proc.inputMode.store (i); });

    juce::PopupMenu settings;
    settings.addSectionHeader ("File");
    settings.addItem ("Set Recording Path...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Recording path", proc.disk.getFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                      proc.disk.setFolder (fc.getResult());
                              });
    });
    settings.addSubMenu ("Format", format);
    settings.addSubMenu ("Various", various);
    settings.addSectionHeader ("Display");
    settings.addSubMenu ("Performance", perf);
    settings.addSubMenu ("Display Gain", dgain);
    settings.addSubMenu ("Visual", visual);
    settings.addSubMenu ("Input", input);
    settings.addSectionHeader ("Recording");
    settings.addItem ("Capture Playback", true, proc.capturePlayback.load(),
                      [this] { proc.capturePlayback.store (! proc.capturePlayback.load()); });
    settings.addItem ("Mute Output", true, proc.muteOutput.load(),
                      [this, refresh] { proc.muteOutput.store (! proc.muteOutput.load()); refresh(); });
    settings.addSeparator();
    settings.addItem ("Open Online Manual", [this] { openManual(); });
    settings.addItem ("Deactivate This Computer", proc.license->isActive(), false, [this]
    {
        proc.license->deactivate ([] (bool ok, juce::String msg)
        {
            if (! ok) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Deactivate", msg);
        });
    });

    juce::PopupMenu duration;
    for (double s : { 30.0, 60.0, 120.0, 300.0, 600.0, 900.0 })
        duration.addItem (s < 60 ? juce::String ((int) s) + " sec" : juce::String ((int) (s / 60)) + " min",
                          true, proc.getDuration() == s, [this, s]
                          {
                              proc.setDuration (s);
                              proc.viewSeconds.store (0.0);
                              viewStart = 0;
                              selStart = selEnd = -1;
                              repaint();
                          });

    juce::PopupMenu themeMenu;
    for (int i = 0; i < numThemes; ++i)
        themeMenu.addItem (themes[i].name, true, proc.theme.load() == i,
                           [this, i, refresh] { proc.theme.store (i); refresh(); });

    juce::PopupMenu menu;
    menu.addSectionHeader ("Sampler");
    menu.addSubMenu ("Settings", settings);
    menu.addSubMenu ("Duration", duration);
    menu.addItem ("Mute Output", true, proc.muteOutput.load(),
                  [this, refresh] { proc.muteOutput.store (! proc.muteOutput.load()); refresh(); });
    menu.addSeparator();
    menu.addSubMenu ("Themes", themeMenu);
    menu.addSeparator();
    menu.addItem ("About - Seleven Audio", []
    {
        juce::URL ("https://www.instagram.com/seleven.audio/").launchInDefaultBrowser();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
