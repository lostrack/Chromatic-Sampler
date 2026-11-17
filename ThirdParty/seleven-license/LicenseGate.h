#pragma once
// Activation screen shown over a plugin editor until this computer is licensed.
// Two layouts: a full panel for normal windows, a single row for thin strips (Drag Sampler).
//
//   gate = std::make_unique<seleven::LicenseGate>([this] { onLicensed(); });
//   addAndMakeVisible(*gate);   // and gate->setBounds(getLocalBounds()) in resized()

#include "SelevenLicense.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace seleven
{

namespace colours
{
    const juce::Colour bg     { 0xff0a0820 }, bg2 { 0xff151036 }, violet { 0xff8b5cff }, violet2 { 0xffc4a8ff },
                       ink    { 0xfff1edff }, dim { 0xff9a90c4 }, faint  { 0xff5d5485 }, ok { 0xff38d6ff }, bad { 0xffff6a7a };
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
        keyBox.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain));
        keyBox.setJustification(juce::Justification::centredLeft);
        keyBox.setIndents(12, 0);
        keyBox.setColour(juce::TextEditor::backgroundColourId, bg2);
        keyBox.setColour(juce::TextEditor::textColourId, ink);
        keyBox.setColour(juce::TextEditor::outlineColourId, violet.withAlpha(0.35f));
        keyBox.setColour(juce::TextEditor::focusedOutlineColourId, violet);
        keyBox.setColour(juce::TextEditor::highlightColourId, violet.withAlpha(0.4f));
        keyBox.setColour(juce::CaretComponent::caretColourId, violet2);
        keyBox.onReturnKey = [this] { submit(); };
        keyBox.onTextChange = [this] { setStatus({}, dim); };
        addAndMakeVisible(keyBox);

        activateBtn.setColour(juce::TextButton::buttonColourId, violet);
        activateBtn.setColour(juce::TextButton::buttonOnColourId, violet);
        activateBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        activateBtn.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        activateBtn.onClick = [this] { submit(); };
        addAndMakeVisible(activateBtn);

        demoBtn.setColour(juce::TextButton::buttonColourId, bg2);
        demoBtn.setColour(juce::TextButton::textColourOffId, dim);
        demoBtn.setColour(juce::ComboBox::outlineColourId, violet.withAlpha(0.3f));
        demoBtn.onClick = [this] { if (demo) { auto cb = demo; cb(); } };
        addAndMakeVisible(demoBtn);

        for (auto* l : { &buyLink, &lostLink })
        {
            l->setFont(juce::FontOptions(13.0f), false, juce::Justification::centred);
            l->setColour(juce::HyperlinkButton::textColourId, violet2);
            addAndMakeVisible(*l);
        }
    }

    void paint(juce::Graphics& g) override
    {
        using namespace colours;
        const auto r = getLocalBounds().toFloat();
        g.fillAll(bg);
        g.setGradientFill(juce::ColourGradient(violet.withAlpha(0.28f), r.getCentreX(), r.getY() - r.getHeight() * 0.2f,
                                               bg.withAlpha(0.0f), r.getCentreX(), r.getBottom(), true));
        g.fillRect(r);

        if (compact())
        {
            g.setColour(ink);
            g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
            g.drawText(SELEVEN_PRODUCT, titleArea, juce::Justification::centredLeft);
            g.setColour(status.isNotEmpty() ? statusColour : dim);
            g.setFont(juce::FontOptions(12.0f));
            g.drawFittedText(status.isNotEmpty() ? status : juce::String("Activate, or try it in demo."),
                             statusArea, juce::Justification::centredLeft, 1);
            return;
        }

        // stacked wordmark
        auto mark = markArea;
        g.setColour(ink);
        g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        g.drawText("Seleven", mark.removeFromTop(mark.getHeight() / 2), juce::Justification::centred);
        g.setColour(violet2);
        g.setFont(juce::FontOptions(20.0f));
        g.drawText("Audio", mark, juce::Justification::centred);

        g.setColour(ink);
        g.setFont(juce::FontOptions(30.0f, juce::Font::bold));
        g.drawText("Activate " SELEVEN_PRODUCT, titleArea, juce::Justification::centred);
        g.setColour(dim);
        g.setFont(juce::FontOptions(14.0f));
        g.drawFittedText("Your key is in the order email from Seleven Audio. One key works on 2 computers.",
                         subArea, juce::Justification::centred, 2);

        g.setColour(statusColour);
        g.setFont(juce::FontOptions(13.5f));
        g.drawFittedText(status, statusArea, juce::Justification::centred, 2);

        g.setColour(faint);
        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        g.drawText("LICENSE / " + juce::SystemStats::getComputerName().toUpperCase(), footArea, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (compact())
        {
            r.reduce(12, juce::jmax(4, (r.getHeight() - 30) / 2));
            titleArea = r.removeFromLeft(120);
            lostLink.setBounds(r.removeFromRight(90));
            buyLink.setBounds(r.removeFromRight(56));
            demoBtn.setBounds(r.removeFromRight(96).reduced(4, 0));
            activateBtn.setBounds(r.removeFromRight(86).reduced(4, 0));
            keyBox.setBounds(r.removeFromLeft(juce::jmin(340, r.getWidth() / 2)));
            statusArea = r.withTrimmedLeft(10);
            return;
        }

        auto col = r.withSizeKeepingCentre(juce::jmin(460, r.getWidth() - 40), 384);
        markArea   = col.removeFromTop(52);
        col.removeFromTop(18);
        titleArea  = col.removeFromTop(40);
        subArea    = col.removeFromTop(44);
        col.removeFromTop(16);
        auto row   = col.removeFromTop(44);
        activateBtn.setBounds(row.removeFromRight(120));
        row.removeFromRight(8);
        keyBox.setBounds(row);
        col.removeFromTop(10);
        demoBtn.setBounds(col.removeFromTop(40));
        statusArea = col.removeFromTop(40);
        auto links = col.removeFromTop(24);
        buyLink.setBounds(links.removeFromLeft(links.getWidth() / 2));
        lostLink.setBounds(links);
        footArea = getLocalBounds().removeFromBottom(34);
    }

private:
    bool compact() const { return getHeight() < 220; }

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

    void setStatus(const juce::String& s, juce::Colour c) { status = s; statusColour = c; repaint(); }

    juce::SharedResourcePointer<LicenseManager> manager;
    std::function<void()> licensed, demo;
    juce::TextEditor keyBox;
    juce::TextButton activateBtn { "Activate" }, demoBtn { "Continue in demo" };
    juce::HyperlinkButton buyLink  { "Buy a license", juce::URL(SELEVEN_BUY_URL) };
    juce::HyperlinkButton lostLink { "Lost your key?", juce::URL("https://app.lemonsqueezy.com/my-orders") };
    juce::Rectangle<int> markArea, titleArea, subArea, statusArea, footArea;
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
        g.setGradientFill(juce::ColourGradient(violet.withAlpha(hover ? 0.95f : 0.8f), r.getX(), 0, juce::Colour(0xff5b3bd6), r.getRight(), 0, false));
        g.fillRoundedRectangle(r, juce::jmin(r.getHeight() / 2, 10.0f));
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(juce::jlimit(10.0f, 13.0f, r.getHeight() * 0.55f), juce::Font::bold));
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
