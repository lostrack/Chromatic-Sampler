#pragma once
// Messages from Seleven Audio shown inside the plugin (news, "update available").
// Written in the admin panel (Plugin messages), served by GET SELEVEN_SERVER/v1/news?product=SLUG&v=VERSION.
// The newest message the user has not closed is offered; closing it (x) is remembered per computer.
//
// Two ways to use it:
//   NewsFeed  - fetch + dismiss model only, for UIs drawn elsewhere (Chromatic's WebView page):
//                 feed = std::make_unique<seleven::NewsFeed>([this](const seleven::NewsItem* it) { ... });
//                 feed->dismiss(id);
//   NewsDot   - JUCE component: a small pulsing dot in the top-left corner; hover/click opens a
//               semi-transparent card with tag, title, body, link and x (Drag Sampler):
//                 news = std::make_unique<seleven::NewsDot>();  addChildComponent(*news);
//                 and in resized(): news->layout();
//
// Testing without the server: set the environment variable SELEVEN_NEWS_TEST=1 before starting the
// host / standalone. One fake item (id "test") is then offered on top of whatever the server returns.
// It is never remembered as closed, so it comes back on every launch while the variable is set.

#include "LicenseGate.h"
#include <juce_data_structures/juce_data_structures.h>

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "0"
#endif

namespace seleven
{

struct NewsItem
{
    juce::String id, tag, title, body, url; // tag is "NEWS" or "UPDATE"; url is empty or https://

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", id);   o->setProperty("tag", tag);   o->setProperty("title", title);
        o->setProperty("body", body); o->setProperty("url", url);
        return juce::var(o);
    }
};

class NewsFeed
{
public:
    /** onResult runs once on the message thread with the item to show, or nullptr when there is none. */
    explicit NewsFeed(std::function<void(const NewsItem*)> onResult) : result(std::move(onResult))
    {
        juce::WeakReference<NewsFeed> self(this);
        pool.addJob([self]
        {
            const auto url = juce::URL(SELEVEN_SERVER "/v1/news")
                                 .withParameter("product", SELEVEN_SLUG)
                                 .withParameter("v", JucePlugin_VersionString);
            juce::var items;
            if (auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                    .withConnectionTimeoutMs(5000)))
                items = juce::JSON::parse(in->readEntireStreamAsString())["items"];

            juce::MessageManager::callAsync([self, items]
            {
                if (auto* f = self.get()) f->pick(items);
            });
        });
    }

    ~NewsFeed() { pool.removeAllJobs(true, 7000); }

    const NewsItem* current() const { return item.id.isNotEmpty() ? &item : nullptr; }

    void dismiss(const juce::String& id)
    {
        if (id.isEmpty() || id != item.id) return;
        if (id != testId)
        {
            auto d = dismissed();
            d.add(id);
            while (d.size() > 50) d.remove(0);
            if (auto* p = settings()) { p->setValue("dismissed", d.joinIntoString(",")); p->saveIfNeeded(); }
        }
        item = {};
    }

private:
    static constexpr const char* testId = "test";

    void pick(const juce::var& items)
    {
        juce::Array<juce::var> list;
        if (juce::SystemStats::getEnvironmentVariable("SELEVEN_NEWS_TEST", {}) == "1")
        {
            auto* t = new juce::DynamicObject();
            t->setProperty("id", testId); t->setProperty("kind", "update");
            t->setProperty("title", "Version 1.1 is out");
            t->setProperty("body", "Test message (SELEVEN_NEWS_TEST=1). Faster loading and two new effects.");
            t->setProperty("url", "https://selevenaudio.com");
            list.add(juce::var(t));
        }
        if (auto* arr = items.getArray()) list.addArray(*arr);

        const auto gone = dismissed();
        for (auto& it : list)
        {
            const auto id = it["id"].toString();
            if (id.isEmpty() || gone.contains(id)) continue;
            item.id    = id;
            item.tag   = it["kind"].toString() == "update" ? "UPDATE" : "NEWS";
            item.title = it["title"].toString();
            item.body  = it["body"].toString();
            item.url   = it["url"].toString();
            if (! item.url.startsWith("https://")) item.url = {};
            break;
        }
        if (result) result(current());
    }

    juce::StringArray dismissed()
    {
        auto* p = settings();
        return p ? juce::StringArray::fromTokens(p->getValue("dismissed"), ",", "") : juce::StringArray();
    }

    juce::PropertiesFile* settings()
    {
        if (props == nullptr)
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "news";
            o.folderName          = "Seleven Audio";
            o.filenameSuffix      = ".settings";
            o.osxLibrarySubFolder = "Application Support";
            props = std::make_unique<juce::PropertiesFile>(o);
        }
        return props.get();
    }

    std::function<void(const NewsItem*)> result;
    juce::ThreadPool pool { 1 };
    std::unique_ptr<juce::PropertiesFile> props;
    NewsItem item;

    JUCE_DECLARE_WEAK_REFERENCEABLE(NewsFeed)
    JUCE_DECLARE_NON_COPYABLE(NewsFeed)
};

/** Small accent dot in the parent's top-left corner; opens a translucent news card. Hidden when there is no news. */
class NewsDot : public juce::Component, private juce::Timer
{
public:
    NewsDot()
    {
        setVisible(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    /** Call from the parent's resized(). */
    void layout()
    {
        auto* p = getParentComponent();
        if (p == nullptr) return;
        if (! open) { setBounds(4, 4, 16, 16); return; }
        const int w = juce::jmin(320, p->getWidth() - 8);
        const int h = juce::jlimit(28, 104, p->getHeight() - 8);
        setBounds(4, 4, w, h);
    }

    void paint(juce::Graphics& g) override
    {
        using namespace colours;
        if (! open)
        {
            const float t = (float) (juce::Time::getMillisecondCounter() % 1800) / 1800.0f; // subtle pulse
            const auto c = getLocalBounds().toFloat().getCentre();
            g.setColour(star.withAlpha(0.35f * (1.0f - t)));
            g.fillEllipse(juce::Rectangle<float>(5.0f + 9.0f * t, 5.0f + 9.0f * t).withCentre(c));
            g.setColour(star);
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(c));
            return;
        }

        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(bg2.withAlpha(0.86f));
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(line2);
        g.drawRoundedRectangle(r, 8.0f, 1.0f);

        auto a = getLocalBounds().reduced(10, 6);
        closeArea = getLocalBounds().removeFromRight(26).removeFromTop(juce::jmin(26, getHeight()));
        const bool oneLine = getHeight() < 60;

        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
        g.setColour(star);
        if (oneLine)
        {
            a.removeFromRight(18);
            g.drawText(item.tag, a.removeFromLeft(item.tag.length() * 7 + 6), juce::Justification::centredLeft);
            g.setColour(ink);
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText(item.title + (item.url.isNotEmpty() ? juce::String(juce::CharPointer_UTF8("  \xe2\x86\x92")) : juce::String()),
                       a, juce::Justification::centredLeft, true);
        }
        else
        {
            g.drawText(item.tag, a.removeFromTop(14), juce::Justification::centredLeft);
            g.setColour(ink);
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText(item.title, a.removeFromTop(18).withTrimmedRight(14), juce::Justification::centredLeft, true);
            if (item.url.isNotEmpty())
            {
                g.setColour(star);
                g.setFont(juce::FontOptions(11.5f));
                g.drawText(juce::String(juce::CharPointer_UTF8("Read more \xe2\x86\x92")), a.removeFromBottom(15), juce::Justification::centredLeft);
            }
            g.setColour(dim);
            g.setFont(juce::FontOptions(11.5f));
            g.drawFittedText(item.body, a, juce::Justification::topLeft, juce::jmax(1, a.getHeight() / 14));
        }

        g.setColour(hoverClose ? ink : faint); // close cross
        const auto c = closeArea.toFloat().withSizeKeepingCentre(7.0f, 7.0f);
        g.drawLine(c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.4f);
        g.drawLine(c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.4f);
    }

    void mouseEnter(const juce::MouseEvent&) override { setOpen(true); }
    void mouseExit(const juce::MouseEvent&) override  { hoverClose = false; setOpen(false); }
    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool hc = open && closeArea.contains(e.getPosition());
        if (hc != hoverClose) { hoverClose = hc; repaint(); }
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (! open) { setOpen(true); return; }
        if (closeArea.contains(e.getPosition()))
        {
            feed.dismiss(item.id);
            open = false;
            stopTimer();
            setVisible(false);
            return;
        }
        if (item.url.isNotEmpty()) juce::URL(item.url).launchInDefaultBrowser();
    }

private:
    void timerCallback() override { repaint(); }

    void setOpen(bool o)
    {
        if (o == open) return;
        open = o;
        if (open) { stopTimer(); toFront(false); } else startTimerHz(24); // card over the DEMO pill while read
        layout();
        repaint();
    }

    NewsFeed feed { [this](const NewsItem* it)
    {
        if (it == nullptr) return;
        item = *it;
        layout();
        setVisible(true);
        startTimerHz(24);
    } };
    NewsItem item;
    juce::Rectangle<int> closeArea;
    bool open = false, hoverClose = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewsDot)
};

} // namespace seleven
