#include "PluginEditor.h"
#include <cstring>
#include <BinaryData.h>

namespace
{
    // every parameter the page mirrors
    constexpr const char* stateParams[] = {
        "attack", "decay", "sustain", "release", "curveA", "curveD", "curveR", "adsrOn",
        "mono", "legato", "glideOn", "glide", "fine", "rootShift", "volume", "autoTrim",
        "revOn", "revRoom", "revAmt", "dlyOn", "dlySync", "dlyDiv", "dlyMs", "dlyPing", "dlyFb", "dlyAmt",
        "sampleStart", "sampleEnd", "fadeIn", "fadeOut", "dlySpread", "foldRoot", "stdNames",
        "satOn", "satDrive", "satMatch", "satMode", "fxOrder", "revLo", "revHi",
        "fltOn", "fltHp", "fltLp", "fltHpS", "fltLpS", "fltMF", "fltMG", "fltMQ", "autoTune"
    };

    constexpr int peakBins = 480;

    bool isAudioName(const juce::String& name)
    {
        return name.endsWithIgnoreCase(".wav") || name.endsWithIgnoreCase(".aif") || name.endsWithIgnoreCase(".aiff")
            || name.endsWithIgnoreCase(".mp3") || name.endsWithIgnoreCase(".flac") || name.endsWithIgnoreCase(".ogg")
            || name.endsWithIgnoreCase(".m4a");
    }
}

ChromaticEditor::ChromaticEditor(ChromaticProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    // fixed 3:2 window; the page scales itself to fit, so resizing never breaks the layout
    setResizable(true, true);
    setResizeLimits(600, 400, 1800, 1200);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(900.0 / 600.0);
    setSize(900, 600);

    // The window appears at once with a painted splash; the WebView2 environment is slow to create,
    // so it is built a moment later instead of blocking the host while it opens.
    news = std::make_unique<seleven::NewsFeed>([this](const seleven::NewsItem*) { sendNews(); });
    juce::Component::SafePointer<ChromaticEditor> safe(this);
    juce::Timer::callAfterDelay(15, [safe] { if (safe != nullptr) safe->createWeb(); });
}

void ChromaticEditor::sendNews()
{
    const auto* it = news ? news->current() : nullptr;
    if (web) web->emitEventIfBrowserIsVisible("news", it ? it->toVar() : juce::var());
}

void ChromaticEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff060708));
    g.setColour(juce::Colour(0xffeceef2));
    g.setFont(juce::FontOptions((float) getHeight() * 0.06f, juce::Font::bold));
    g.drawText("Chromatic Sampler", getLocalBounds(), juce::Justification::centred, false);
    g.setColour(juce::Colour(0xff5b616b));
    g.setFont(juce::FontOptions((float) getHeight() * 0.022f));
    g.drawText("SELEVEN AUDIO", getLocalBounds().withTrimmedTop(getHeight() / 8), juce::Justification::centred, false);
}

void ChromaticEditor::createWeb()
{
    auto options = juce::WebBrowserComponent::Options {}
#if JUCE_WINDOWS
        // WebView2 has to be asked for explicitly: the default Windows backend is the old IE control,
        // which cannot serve the page. Its cache goes to a writable folder, since a host like FL Studio
        // lives under Program Files. (macOS uses the system WKWebView, which needs no setup.)
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2 {}
                                    .withUserDataFolder(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                                            .getChildFile("Seleven Audio").getChildFile("Chromatic Sampler").getChildFile("WebView2"))
                                    .withBackgroundColour(juce::Colours::black)
                                    .withStatusBarDisabled())
#endif
        .withNativeIntegrationEnabled()
        .withResourceProvider([](const juce::String& url) -> std::optional<juce::WebBrowserComponent::Resource>
        {
            // JUCE passes the path after https://juce.backend, e.g. "/" or "/index.html"
            const auto path = url.upToFirstOccurrenceOf("?", false, false);
            if (path.isEmpty() || path == "/" || path == "/index.html")
            {
                std::vector<std::byte> data((size_t) BinaryData::index_htmlSize);
                std::memcpy(data.data(), BinaryData::index_html, (size_t) BinaryData::index_htmlSize);
                return juce::WebBrowserComponent::Resource { std::move(data), "text/html" };
            }
            return std::nullopt;
        })
        .withEventListener("param", [this](const juce::var& v) { handleParam(v); })
        .withEventListener("noteOn", [this](const juce::var& v)
        {
            proc.keyboardState.noteOn(1, (int) v.getProperty("note", 60), (float) (double) v.getProperty("vel", 0.8));
        })
        .withEventListener("noteOff", [this](const juce::var& v)
        {
            proc.keyboardState.noteOff(1, (int) v.getProperty("note", 60), 0.f);
        })
        .withEventListener("openFile", [this](const juce::var&) { openFileChooser(); })
        .withEventListener("dropFile", [this](const juce::var& v) { handleDrop(v); })
        .withEventListener("dropPath", [this](const juce::var& v)
        {
            // drags from DAW browsers sometimes arrive as a path instead of file data
            const juce::File f(v.getProperty("path", "").toString());
            if (f.existsAsFile() && isAudioName(f.getFileName())) proc.loadSample(f);
        })
        .withEventListener("demo", [this](const juce::var&) { proc.loadDemo(); })
        .withEventListener("resizeTo", [this](const juce::var& v)
        {
            const double s = juce::jlimit(0.67, 2.0, (double) v.getProperty("scale", 1.0));
            setSize((int) std::lround(900.0 * s), (int) std::lround(600.0 * s));
        })
        .withEventListener("resetAll", [this](const juce::var&)
        {
            for (auto* prm : proc.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
                {
                    const juce::String id = rp->paramID;
                    // the two display settings are not part of the sound
                    if (id == "stdNames" || id == "foldRoot") continue;
                    rp->setValueNotifyingHost(rp->getDefaultValue());
                }
        })
        // the page is new each time the window opens: send the waveform again once it is listening
        .withEventListener("ready", [this](const juce::var&) { lastSeq = -1; sendNews(); })
        .withEventListener("licActivate", [this](const juce::var& v)
        {
            juce::Component::SafePointer<ChromaticEditor> safe(this);
            proc.license->activate(v.getProperty("key", "").toString().trim(), [safe](bool ok, juce::String msg)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty("ok", ok);
                o->setProperty("msg", msg);
                if (safe != nullptr && safe->web) safe->web->emitEventIfBrowserIsVisible("licResult", juce::var(o));
                else delete o;
            });
        })
        .withEventListener("licLink", [](const juce::var& v)
        {
            juce::URL(v.getProperty("which", "").toString() == "buy" ? juce::String(SELEVEN_BUY_URL)
                                                                       : juce::String(SELEVEN_SERVER "/account.html")).launchInDefaultBrowser();
        })
        .withEventListener("newsDismiss", [this](const juce::var& v) { if (news) news->dismiss(v.getProperty("id", "").toString()); })
        .withEventListener("newsOpen", [this](const juce::var&)
        {
            // only the URL the feed vetted (https), never one from the page
            if (const auto* it = news ? news->current() : nullptr)
                if (it->url.isNotEmpty()) juce::URL(it->url).launchInDefaultBrowser();
        })
        .withEventListener("openUrl", [](const juce::var&) { juce::URL("https://selevenaudio.com").launchInDefaultBrowser(); })
        .withEventListener("clear", [this](const juce::var&) { proc.clearSample(); })
        .withEventListener("logout", [this](const juce::var&)
        {
            auto lic = proc.license; // keep the manager alive while the dialog is open
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Log out",
                "Deactivate this computer for key " + lic->maskedKey() + "?\nThe slot is freed so the key can be used on another computer.",
                "Log out", "Cancel", this,
                juce::ModalCallbackFunction::create([lic](int r)
                {
                    if (r == 0) return;
                    lic->deactivate([](bool ok, juce::String msg)
                    {
                        if (! ok) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Log out",
                                                                         "Logged out on this computer, but the server could not free the slot: " + msg);
                    });
                }));
        });

    web = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*web);
    resized();   // setSize() above ran before the browser existed, so size it now
    web->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    lastSeq = -1;
    startTimerHz(30);
}

ChromaticEditor::~ChromaticEditor()
{
    stopTimer();
}

void ChromaticEditor::resized()
{
    if (web) web->setBounds(getLocalBounds());
}

void ChromaticEditor::timerCallback()
{
    if (web) web->emitEventIfBrowserIsVisible("state", buildState());
}

void ChromaticEditor::handleParam(const juce::var& msg)
{
    const auto id = msg.getProperty("id", "").toString();
    const float value = (float) (double) msg.getProperty("value", 0.0);
    if (auto* p = proc.apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

void ChromaticEditor::handleDrop(const juce::var& msg)
{
    const auto name = msg.getProperty("name", "").toString();
    if (!isAudioName(name)) return;
    // standard base64 from the page; MemoryBlock::fromBase64Encoding expects JUCE's own format
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, msg.getProperty("data", "").toString())) return;
    proc.loadSampleBytes(decoded.getMemoryBlock(), name);
}

void ChromaticEditor::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser>("Choose a sample", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc)
                         {
                             const auto f = fc.getResult();
                             if (f.existsAsFile()) proc.loadSample(f);
                         });
}

juce::var ChromaticEditor::buildState()
{
    auto* root = new juce::DynamicObject();

    for (auto* id : stateParams)
        root->setProperty(id, (double) proc.apvts.getRawParameterValue(id)->load());

    auto* sample = new juce::DynamicObject();
    auto s = proc.getSampler().getSample();
    const int seq = proc.sampleSeq.load();
    sample->setProperty("loaded", s != nullptr);
    sample->setProperty("seq", seq);
    sample->setProperty("name", proc.getSampleName());

    if (s != nullptr)
    {
        sample->setProperty("duration", s->audio.getNumSamples() / s->sampleRate);
        // with auto tune off the page shows the sample as unpitched
        sample->setProperty("hasPitch", s->hasPitch && proc.apvts.getRawParameterValue("autoTune")->load() > 0.5f);
        const bool fold = proc.apvts.getRawParameterValue("foldRoot")->load() > 0.5f;
        sample->setProperty("midi", fold ? SampleLoader::foldRoot(s->midi) : s->midi);
        sample->setProperty("confidence", (double) s->confidence);
        sample->setProperty("removedMs", (double) (s->removedSamples / s->sampleRate * 1000.0));
        sample->setProperty("pitchHz", s->hasPitch ? 440.0 * std::pow(2.0, (s->midi - 69.0) / 12.0) : 0.0);
        sample->setProperty("channels", s->audio.getNumChannels());

        const double len = (double) juce::jmax(1, s->audio.getNumSamples());
        sample->setProperty("viewStart", (double) s->viewStart / len);
        sample->setProperty("viewEnd", (double) juce::jmax(s->viewEnd, s->viewStart + 1) / len);
    }

    if (seq != lastSeq)
    {
        lastSeq = seq;
        peaks.clear();
        if (s != nullptr && s->audio.getNumSamples() > 1)
        {
            const float* ch0 = s->audio.getReadPointer(0);
            const int len = s->audio.getNumSamples();
            // the whole (auto-trimmed) sample, so the start/end handles map straight onto it
            const int v0 = 0;
            const int v1 = len;
            const int span = v1 - v0;
            for (int b = 0; b < peakBins; ++b)
            {
                const int a = v0 + (int) ((double) b * span / peakBins);
                const int e = juce::jmin(v1, v0 + (int) ((double) (b + 1) * span / peakBins) + 1);
                float lo = 0.f, hi = 0.f;
                for (int i = a; i < e; ++i) { lo = juce::jmin(lo, ch0[i]); hi = juce::jmax(hi, ch0[i]); }
                peaks.add(juce::var((double) lo));
                peaks.add(juce::var((double) hi));
            }
        }
        sample->setProperty("peaks", juce::var(peaks));
    }
    root->setProperty("sample", juce::var(sample));
    root->setProperty("hostBpm", (double) proc.getHostBpm());

    const auto snap = proc.getSampler().getSnapshot();
    auto* env = new juce::DynamicObject();
    env->setProperty("stage", snap.stage);
    env->setProperty("frac", (double) snap.frac);
    env->setProperty("level", (double) snap.level);
    env->setProperty("playPos", (double) snap.playPos);
    root->setProperty("snap", juce::var(env));
    root->setProperty("licensed", proc.license->isActive());
    root->setProperty("licenseKey", proc.license->isActive() ? proc.license->maskedKey() : juce::String());

    return juce::var(root);
}
