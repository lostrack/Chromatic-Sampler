#pragma once
// Seleven Audio licensing (shared by Chromatic Sampler and Drag Sampler).
//
// Triangle: Lemon Squeezy sells and emails the key -> webhook -> our server (seleven-server)
// stores every key -> the plugin asks only our server, in the Lemon Squeezy License API format:
//   POST SELEVEN_SERVER/v1/licenses/validate    (check key + product before using a slot)
//   POST SELEVEN_SERVER/v1/licenses/activate    (uses one activation slot)
//   POST SELEVEN_SERVER/v1/licenses/deactivate  (frees this computer's slot)
// Works for Lemon Squeezy keys and for SLV- keys made in the admin panel.
// No secret ships in the plugin; these endpoints only need the customer's license key.
//
// Per-target compile definitions (set in CMakeLists.txt, see seleven-license/README.md):
//   SELEVEN_PRODUCT  "Chromatic Sampler"
//   SELEVEN_SLUG     "chromatic"   (a "bundle" key unlocks every plugin)
//   SELEVEN_SERVER   "https://selevenaudio.com"
//   SELEVEN_BUY_URL  "https://selevenaudio.com/product.html?id=chromatic"
//
// The activation is stored encrypted with a key derived from this machine's device id, so a copied
// licence file does not unlock another computer. Online re-check every 3 days, 21 days offline grace.

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>

#ifndef SELEVEN_PRODUCT
 #error "Define SELEVEN_PRODUCT for this target"
#endif
#ifndef SELEVEN_LS_STORE_ID
 #define SELEVEN_LS_STORE_ID 0
#endif
#ifndef SELEVEN_LS_PRODUCT_ID
 #define SELEVEN_LS_PRODUCT_ID 0
#endif
#ifndef SELEVEN_BUY_URL
 #define SELEVEN_BUY_URL "https://selevenaudio.com"
#endif
#ifndef SELEVEN_SERVER
 #define SELEVEN_SERVER "https://selevenaudio.com"
#endif
#ifndef SELEVEN_SLUG
 #error "Define SELEVEN_SLUG (chromatic | drag) for this target"
#endif

namespace seleven
{

class LicenseManager
{
public:
    static constexpr juce::int64 recheckMs = 3LL * 24 * 3600 * 1000;
    static constexpr juce::int64 graceMs   = 21LL * 24 * 3600 * 1000;

    struct Listener { virtual ~Listener() = default; virtual void licenseChanged() = 0; };

    LicenseManager()
    {
        load();
        if (key.isNotEmpty() && juce::Time::currentTimeMillis() - lastOk > recheckMs)
            revalidate();
    }

    ~LicenseManager() { pool.removeAllJobs(true, 15000); }

    /** Safe to read from any thread, including the audio thread. */
    bool isActive() const noexcept { return active.load(std::memory_order_relaxed); }

    juce::String maskedKey() const
    {
        const juce::ScopedLock sl(lock);
        return key.length() > 8 ? key.substring(0, 4) + "......" + key.getLastCharacters(4) : key;
    }

    void addListener(Listener* l)    { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

    using Done = std::function<void(bool ok, juce::String message)>;

    /** Validates the key for this product, then activates this computer. done() runs on the message thread. */
    void activate(juce::String licenseKey, Done done)
    {
        licenseKey = licenseKey.trim().removeCharacters(" \t\r\n");
        juce::WeakReference<LicenseManager> self(this);
        pool.addJob([self, licenseKey, done]
        {
            juce::String msg, inst;
            const bool ok = doActivate(licenseKey, inst, msg);
            juce::MessageManager::callAsync([self, ok, msg, inst, licenseKey, done]
            {
                if (self == nullptr) return;
                if (ok) self->store(licenseKey, inst, juce::Time::currentTimeMillis());
                if (done) done(ok, msg);
            });
        });
    }

    /** Frees this computer's activation slot so the key can be used on another computer. */
    void deactivate(Done done)
    {
        juce::String k, inst;
        { const juce::ScopedLock sl(lock); k = key; inst = instanceId; }
        juce::WeakReference<LicenseManager> self(this);
        pool.addJob([self, k, inst, done]
        {
            int status = 0; juce::String err;
            auto r = post("deactivate", { { "license_key", k }, { "instance_id", inst } }, status, err);
            // 404 = instance already gone on the server: treat it as freed
            const bool ok = (bool) r["deactivated"] || status == 404;
            const auto msg = ok ? juce::String("This computer is deactivated.")
                                : err.isNotEmpty() ? err : r["error"].toString();
            juce::MessageManager::callAsync([self, ok, msg, done]
            {
                if (self == nullptr) return;
                if (ok) self->clear();
                if (done) done(ok, msg);
            });
        });
    }

private:
    // ------------------------------------------------------------------ network
    static juce::var post(const juce::String& endpoint,
                          std::initializer_list<std::pair<const char*, juce::String>> params,
                          int& status, juce::String& netError)
    {
        // every key (admin-made SLV- keys and Lemon Squeezy keys) is checked by our server,
        // which imports Lemon Squeezy keys and enforces devices, expiry and refunds in one place
        juce::URL url(juce::String(SELEVEN_SERVER) + "/v1/licenses/" + endpoint);
        for (auto& p : params) url = url.withParameter(p.first, p.second);
        status = 0;
        auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                                .withExtraHeaders("Accept: application/json")
                                                .withConnectionTimeoutMs(12000)
                                                .withStatusCode(&status));
        if (stream == nullptr) { netError = "No connection. Check your internet and try again."; return {}; }
        return juce::JSON::parse(stream->readEntireStreamAsString());
    }

    static bool productMatches(const juce::var& meta, juce::String& msg)
    {
        // our own server: product slug, a bundle key unlocks both plugins
        if (meta.hasProperty("product_slug"))
        {
            const auto slug = meta["product_slug"].toString();
            if (slug == SELEVEN_SLUG || slug == "bundle") return true;
            msg = "This key is for " + meta["product_name"].toString() + ", not " SELEVEN_PRODUCT ".";
            return false;
        }
        const auto store = (juce::int64) meta["store_id"], product = (juce::int64) meta["product_id"];
        if ((SELEVEN_LS_STORE_ID != 0 && store != (juce::int64) SELEVEN_LS_STORE_ID)
            || (SELEVEN_LS_PRODUCT_ID != 0 && product != (juce::int64) SELEVEN_LS_PRODUCT_ID))
        {
            const auto other = meta["product_name"].toString();
            msg = other.isNotEmpty() ? "This key is for " + other + ", not " SELEVEN_PRODUCT "."
                                     : juce::String("This key is not for " SELEVEN_PRODUCT ".");
            return false;
        }
        return true;
    }

    static bool doActivate(const juce::String& k, juce::String& instanceOut, juce::String& msg)
    {
        if (k.length() < 16) { msg = "That does not look like a license key."; return false; }
        int status = 0; juce::String err;

        // 1) check the key and product without using an activation slot
        auto v = post("validate", { { "license_key", k } }, status, err);
        if (v.isVoid()) { msg = err; return false; }
        const auto lk = v["license_key"];
        if (! lk.isObject()) { msg = "License key not found. Copy it again from your order email."; return false; }
        if (! productMatches(v["meta"], msg)) return false;
        const auto st = lk["status"].toString();
        if (st == "disabled") { msg = "This key has been disabled. Contact support@selevenaudio.com."; return false; }
        if (st == "expired")  { msg = "This key has expired."; return false; }
        const int limit = (int) lk["activation_limit"], used = (int) lk["activation_usage"];
        if (limit > 0 && used >= limit)
        {
            msg = "Already active on " + juce::String(used) + " computers. Deactivate one from its plugin menu first.";
            return false;
        }

        // 2) take a slot for this computer
        const auto name = juce::SystemStats::getComputerName() + " / " SELEVEN_PRODUCT;
        auto a = post("activate", { { "license_key", k }, { "instance_name", name } }, status, err);
        if (a.isVoid()) { msg = err; return false; }
        if (! (bool) a["activated"])
        {
            msg = a["error"].toString().isNotEmpty() ? a["error"].toString() : juce::String("Activation failed.");
            return false;
        }
        instanceOut = a["instance"]["id"].toString();
        const int left = limit > 0 ? limit - used - 1 : -1;
        msg = left >= 0 ? "Activated. " + juce::String(left) + (left == 1 ? " activation" : " activations") + " left on this key."
                        : juce::String("Activated.");
        return instanceOut.isNotEmpty();
    }

    void revalidate()
    {
        juce::String k, inst;
        { const juce::ScopedLock sl(lock); k = key; inst = instanceId; }
        juce::WeakReference<LicenseManager> self(this);
        pool.addJob([self, k, inst]
        {
            int status = 0; juce::String err, msg;
            auto r = post("validate", { { "license_key", k }, { "instance_id", inst } }, status, err);
            if (! r.isObject()) return; // offline: keep running inside the grace period
            const bool valid = (bool) r["valid"] && productMatches(r["meta"], msg);
            juce::MessageManager::callAsync([self, valid, k, inst]
            {
                if (self == nullptr) return;
                if (valid) self->store(k, inst, juce::Time::currentTimeMillis());
                else self->clear(); // refunded, disabled or deactivated elsewhere
            });
        });
    }

    // ------------------------------------------------------------------ storage
    static juce::File file()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Seleven Audio").getChildFile(SELEVEN_PRODUCT).getChildFile("license.dat");
    }

    static juce::BlowFish cipher()
    {
        const auto seed = juce::SystemStats::getUniqueDeviceID() + "|" SELEVEN_PRODUCT "|s11-audio";
        const auto h = juce::SHA256(seed.toUTF8()).getRawData();
        return juce::BlowFish(h.getData(), (int) h.getSize());
    }

    void load()
    {
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding(file().loadFileAsString().trim()) || mb.getSize() == 0) return;
        const int n = cipher().decrypt(mb.getData(), mb.getSize());
        if (n <= 0) return;
        const auto lines = juce::StringArray::fromLines(juce::String::fromUTF8((const char*) mb.getData(), n));
        if (lines.size() < 4 || lines[0] != "SLV1") return; // other machine or tampered
        const juce::ScopedLock sl(lock);
        key = lines[1]; instanceId = lines[2]; lastOk = lines[3].getLargeIntValue();
        active = key.isNotEmpty() && juce::Time::currentTimeMillis() - lastOk < graceMs;
    }

    void store(const juce::String& k, const juce::String& inst, juce::int64 when)
    {
        {
            const juce::ScopedLock sl(lock);
            key = k; instanceId = inst; lastOk = when;
            const auto text = "SLV1\n" + k + "\n" + inst + "\n" + juce::String(when);
            const auto len = text.getNumBytesAsUTF8();
            juce::MemoryBlock mb(len + 16, true);
            mb.copyFrom(text.toRawUTF8(), 0, len);
            const int n = cipher().encrypt(mb.getData(), len, mb.getSize());
            if (n > 0)
            {
                mb.setSize((size_t) n);
                file().getParentDirectory().createDirectory();
                file().replaceWithText(mb.toBase64Encoding());
            }
        }
        active = true;
        listeners.call([](Listener& l) { l.licenseChanged(); });
    }

    void clear()
    {
        { const juce::ScopedLock sl(lock); key = {}; instanceId = {}; lastOk = 0; }
        file().deleteFile();
        active = false;
        listeners.call([](Listener& l) { l.licenseChanged(); });
    }

    juce::CriticalSection lock;
    juce::String key, instanceId;
    juce::int64 lastOk = 0;
    std::atomic<bool> active { false };
    juce::ListenerList<Listener> listeners;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads(1).withThreadName("Seleven license") };

    JUCE_DECLARE_WEAK_REFERENCEABLE(LicenseManager)
    JUCE_DECLARE_NON_COPYABLE(LicenseManager)
};

/** Demo watermark: while unlicensed, a soft 0.45 s noise swell is mixed in every 10 seconds.
    The plugin stays fully usable; the swell makes demo renders unusable. Audio-thread safe. */
class DemoNag
{
public:
    void prepare(double sampleRate) noexcept
    {
        period = juce::jmax<juce::int64>(1, (juce::int64) (sampleRate * 10.0));
        len    = juce::jmax(1, (int) (sampleRate * 0.45));
        pos    = period - (juce::int64) (sampleRate * 4.0); // first swell 4 s after load
    }

    void process(juce::AudioBuffer<float>& b) noexcept
    {
        const int n = b.getNumSamples(), nch = b.getNumChannels();
        for (int i = 0; i < n; ++i, ++pos)
        {
            const auto phase = pos % period;
            if (phase >= len) continue;
            const float env = std::sin(juce::MathConstants<float>::pi * (float) phase / (float) len);
            const float v = (rng.nextFloat() * 2.0f - 1.0f) * 0.14f * env * env;
            for (int c = 0; c < nch; ++c) b.getWritePointer(c)[i] += v;
        }
    }

private:
    juce::int64 period = 441000, pos = 0;
    int len = 19845;
    juce::Random rng;
};

} // namespace seleven
