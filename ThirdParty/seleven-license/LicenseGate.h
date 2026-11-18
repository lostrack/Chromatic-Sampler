#pragma once
// Activation screen shown over a plugin editor until this computer is licensed.
// Two layouts: a full panel for normal windows, a single row for thin strips (Drag Sampler).
// Look follows the site's "Signal" system: black room, white signal, grey for everything quiet.
//
//   gate = std::make_unique<seleven::LicenseGate>([this] { onLicensed(); }, [this] { onDemo(); });
//   addAndMakeVisible(*gate);   // and gate->setBounds(getLocalBounds()) in resized()
//   gate->setBackdrop(seleven::blurredSnapshot(*this));  // optional: full layout becomes a card over the blurred editor

#include "SelevenLicense.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace seleven
{

namespace colours
{
    const juce::Colour bg   { 0xff000000 }, bg2 { 0xff0e0e0f }, line { 0x24ffffff }, line2 { 0x3dffffff },
                       ink  { 0xfff4f4f2 }, dim { 0xa3f4f4f2 }, faint { 0x6bf4f4f2 }, star { 0xff8fa9d4 },
                       ok   { 0xff9fd8b0 }, bad { 0xffff6b5e };
}

/** The Seleven "S" mark with its star, from seleven-audio-site/brand/logo-mark.svg (24x24 box). */
inline void drawSelevenMark(juce::Graphics& g, juce::Rectangle<float> box)
{
    static const juce::Path s     = juce::Drawable::parseSVGPath("M16.4 7.6C15.5 6 13.9 5.1 11.8 5.1 9.1 5.1 7.3 6.6 7.3 8.7c0 2 1.5 3 4.2 3.6l1 .2c2.8.6 4.2 1.6 4.2 3.7 0 2.2-2 3.7-4.8 3.7-2.4 0-4.2-1-5-2.8");
    static const juce::Path spark = juce::Drawable::parseSVGPath("M19.4 .4Q19.4 4.6 23.6 4.6 19.4 4.6 19.4 8.8 19.4 4.6 15.2 4.6 19.4 4.6 19.4 .4Z");
    static const juce::Path stroked = []
    {
        juce::Path p;
        juce::PathStrokeType(2.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(p, s);
        return p;
    }();
    const float k = juce::jmin(box.getWidth(), box.getHeight()) / 24.0f;
    const auto t = juce::AffineTransform::scale(0.85f).translated(1.8f, 1.8f).scaled(k).translated(box.getX(), box.getY());
    g.setColour(colours::ink);  g.fillPath(stroked, t);
    g.setColour(colours::star); g.fillPath(spark, t);
}

/** Blurred picture of a component, for drawing behind a popup. Taken small (cheap) and blurred; draw it stretched. */
inline juce::Image blurredSnapshot(juce::Component& c)
{
    if (c.getWidth() <= 0 || c.getHeight() <= 0) return {};
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 0.125f).convertedToFormat(juce::Image::ARGB);
    juce::ImageConvolutionKernel k(7);
    k.createGaussianBlur(2.5f);
    juce::Image out(juce::Image::ARGB, img.getWidth(), img.getHeight(), true);
    k.applyToImage(out, img, img.getBounds());
    return out;
}

class LicenseGate : public juce::Component
{
public:
    LicenseGate(std::function<void()> onLicensed, std::function<void()> onDemo)
        : licensed(std::move(onLicensed)), demo(std::move(onDemo))
    {
        using namespace colours;
        setOpaque(true);
        setWantsKeyboardFocus(false);

        keyBox.setTextToShowWhenEmpty("Paste your license key", faint);
        keyBox.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
        keyBox.setJustification(juce::Justification::centredLeft);
        keyBox.setIndents(14, 0);
        keyBox.setColour(juce::TextEditor::backgroundColourId, bg2);
        keyBox.setColour(juce::TextEditor::textColourId, ink);
        keyBox.setColour(juce::TextEditor::outlineColourId, line);
        keyBox.setColour(juce::TextEditor::focusedOutlineColourId, line2);
        keyBox.setColour(juce::TextEditor::highlightColourId, ink.withAlpha(0.25f));
        keyBox.setColour(juce::CaretComponent::caretColourId, ink);
        keyBox.onReturnKey = [this] { submit(); };
        keyBox.onTextChange = [this] { setStatus({}, dim); };
        addAndMakeVisible(keyBox);

        activateBtn.setColour(juce::TextButton::buttonColourId, ink);
        activateBtn.setColour(juce::TextButton::buttonOnColourId, ink);
        activateBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        activateBtn.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        activateBtn.onClick = [this] { submit(); };
        addAndMakeVisible(activateBtn);

        for (auto* l : { &demoLink, &buyLink, &lostLink })
        {
            l->setFont(juce::FontOptions(12.5f), false, juce::Justification::centred);
            l->setColour(juce::HyperlinkButton::textColourId, dim);
            addAndMakeVisible(*l);
        }
        demoLink.onClick = [this] { if (demo) { auto cb = demo; cb(); } };
    }

    /** Full layout only: draw this (stretched, dimmed) behind a centred card instead of a plain black page. */
    void setBackdrop(juce::Image img) { backdrop = std::move(img); repaint(); }

    void paint(juce::Graphics& g) override
    {
        using namespace colours;
        g.fillAll(bg);
        if (! compact() && backdrop.isValid())
        {
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImage(backdrop, getLocalBounds().toFloat());
            g.fillAll(juce::Colours::black.withAlpha(0.45f));
            const auto c = cardArea.toFloat();
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRoundedRectangle(c.translated(0, 6).expanded(4), 16.0f);
            g.setColour(bg2.withAlpha(0.96f));
            g.fillRoundedRectangle(c, 14.0f);
            g.setColour(line2);
            g.drawRoundedRectangle(c.reduced(0.5f), 14.0f, 1.0f);
        }
        drawSelevenMark(g, markArea.toFloat());

        if (compact())
        {
            g.setColour(ink);
            g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
            g.drawText(SELEVEN_PRODUCT, titleArea, juce::Justification::centredLeft);
            // narrow rows have no status column: the status takes the links' place until the key is edited
            const bool overLinks = status.isNotEmpty() && statusArea.getWidth() < 120;
            g.setColour(status.isNotEmpty() ? statusColour : faint);
            g.setFont(juce::FontOptions(12.0f));
            g.drawFittedText(status.isNotEmpty() ? status : juce::String("Paste your key, or try the demo."),
                             overLinks ? linksArea : statusArea, juce::Justification::centredLeft, 1);
            return;
        }

        g.setColour(ink);
        g.setFont(juce::FontOptions(26.0f, juce::Font::bold));
        g.drawText(SELEVEN_PRODUCT, titleArea, juce::Justification::centredLeft);
        g.setColour(dim);
        g.setFont(juce::FontOptions(14.0f));
        g.drawFittedText("Paste the key from your order email. One key covers two computers.",
                         subArea, juce::Justification::topLeft, 2);

        g.setColour(statusColour);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(status, statusArea, juce::Justification::centredLeft, 2);

        g.setColour(line); // hairline over the links, like the site footer rule
        g.fillRect(linksArea.getX(), linksArea.getY(), linksArea.getWidth(), 1);

        g.setColour(faint);
        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
        g.drawText("SELEVEN AUDIO  /  " + juce::SystemStats::getComputerName().toUpperCase(), footArea, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (compact())
        {
            // fits 400..wide: short link labels and no title below 760 px
            r.reduce(10, juce::jmax(4, (r.getHeight() - 30) / 2));
            const bool wide = r.getWidth() >= 740;
            markArea = r.removeFromLeft(r.getHeight()).reduced(2);
            r.removeFromLeft(8);
            titleArea = wide ? r.removeFromLeft(112) : juce::Rectangle<int>();
            demoLink.setButtonText(wide ? "Try the demo" : "Demo");
            lostLink.setButtonText(wide ? "Lost your key?" : "Lost key?");
            linksArea = r.removeFromRight(wide ? 216 : 160);
            auto links = linksArea;
            lostLink.setBounds(links.removeFromRight(wide ? 90 : 66));
            buyLink.setBounds(links.removeFromRight(wide ? 40 : 34));
            demoLink.setBounds(links);
            activateBtn.setBounds(r.removeFromRight(80).reduced(4, 0));
            keyBox.setBounds(r.removeFromLeft(juce::jmin(300, r.getWidth() * 2 / 3)));
            statusArea = r.withTrimmedLeft(10);
            for (auto* l : { &demoLink, &buyLink, &lostLink }) l->setVisible(status.isEmpty() || statusArea.getWidth() >= 120);
            return;
        }

        const bool card = backdrop.isValid();
        const bool tight = getHeight() < 400; // no logo row on short windows
        auto col = r.withSizeKeepingCentre(juce::jmin(card ? 340 : 400, r.getWidth() - 48), tight ? 272 : 334);
        cardArea = col.expanded(26, 14);
        markArea = {};
        if (! tight) { markArea = col.removeFromTop(40).removeFromLeft(40); col.removeFromTop(22); }
        titleArea  = col.removeFromTop(34);
        subArea    = col.removeFromTop(42);
        col.removeFromTop(14);
        keyBox.setBounds(col.removeFromTop(44));
        col.removeFromTop(10);
        activateBtn.setBounds(col.removeFromTop(44));
        statusArea = col.removeFromTop(40);
        col.removeFromTop(8);
        linksArea  = col.removeFromTop(36);
        auto links = linksArea.withTrimmedTop(10);
        const int w = links.getWidth() / 3;
        demoLink.setBounds(links.removeFromLeft(w));
        buyLink.setBounds(links.removeFromLeft(w));
        lostLink.setBounds(links);
        footArea = card ? juce::Rectangle<int>() : getLocalBounds().removeFromBottom(34);
    }

private:
    bool compact() const { return getHeight() < 300; } // the card needs ~290 px; below that the single row is used

    void submit()
    {
        if (busy) return;
        const auto k = keyBox.getText().trim();
        if (k.isEmpty()) { setStatus("Paste the key from your order email.", colours::bad); return; }
        busy = true;
        activateBtn.setEnabled(false);
        keyBox.setReadOnly(true);
        setStatus("Checking your key...", colours::dim);

        juce::Component::SafePointer<LicenseGate> safe(this);
        manager->activate(k, [safe](bool ok, juce::String msg)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            safe->activateBtn.setEnabled(true);
            safe->keyBox.setReadOnly(false);
            safe->setStatus(msg, ok ? colours::ok : colours::bad);
            if (ok && safe->licensed)
            {
                auto cb = safe->licensed;
                juce::Timer::callAfterDelay(900, cb); // let the success line be read
            }
        });
    }

    void setStatus(const juce::String& s, juce::Colour c)
    {
        const bool relayout = s.isEmpty() != status.isEmpty();
        status = s; statusColour = c;
        if (relayout && compact()) resized();
        repaint();
    }

    juce::SharedResourcePointer<LicenseManager> manager;
    std::function<void()> licensed, demo;
    juce::TextEditor keyBox;
    juce::TextButton activateBtn { "Activate" };
    juce::HyperlinkButton demoLink { "Try the demo", {} };
    juce::HyperlinkButton buyLink  { "Buy", juce::URL(SELEVEN_BUY_URL) };
    juce::HyperlinkButton lostLink { "Lost your key?", juce::URL(SELEVEN_SERVER "/account.html") };
    juce::Rectangle<int> markArea, titleArea, subArea, statusArea, footArea, linksArea, cardArea;
    juce::Image backdrop;
    juce::String status;
    juce::Colour statusColour { colours::dim };
    bool busy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseGate)
};

/** Thin reminder shown while running in demo. Click = reopen the activation screen. */
class DemoBar : public juce::Component
{
public:
    explicit DemoBar(std::function<void()> onClick) : clicked(std::move(onClick)) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void paint(juce::Graphics& g) override
    {
        using namespace colours;
        const auto r = getLocalBounds().toFloat();
        g.setColour(hover ? ink : bg2);
        g.fillRoundedRectangle(r, r.getHeight() / 2);
        g.setColour(line2);
        g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() / 2, 1.0f);
        g.setColour(hover ? juce::Colours::black : ink);
        g.setFont(juce::FontOptions(juce::jlimit(10.0f, 12.5f, r.getHeight() * 0.52f), juce::Font::bold));
        g.drawText(juce::String(juce::CharPointer_UTF8(getWidth() > 260 ? "DEMO  \xc2\xb7  A noise plays every 10 s. Activate to remove it  \xe2\x86\x92"
                                                                       : "DEMO  \xc2\xb7  Activate  \xe2\x86\x92")),
                   getLocalBounds().reduced(10, 0), juce::Justification::centred, true);
    }

    void mouseUp(const juce::MouseEvent&) override { if (clicked) { auto cb = clicked; cb(); } }
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override  { hover = false; repaint(); }

private:
    std::function<void()> clicked;
    bool hover = false;
};

} // namespace seleven
