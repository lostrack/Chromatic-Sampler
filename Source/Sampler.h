#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
#include "SampleLoader.h"

struct SamplerParams
{
    float attack = 0.002f, decay = 0.35f, sustain = 0.f, release = 0.22f;
    float curveA = 0.f, curveD = 0.f, curveR = 0.f;   // -1..1
    bool adsrOn = false;
    bool mono = false;
    bool legato = false;
    bool glideOn = false;
    float glideSeconds = 0.09f;
    float fineCents = 0.f;
    float rootShift = 0.f;    // semitones, set by the stepper
    float startFrac = 0.f;    // playable range inside the sample, 0..1
    float endFrac = 1.f;
    float fadeInMs = 0.f;     // fades applied to the playable range
    float fadeOutMs = 0.f;
    bool foldRoot = true;     // keep the detected root in octave 4
    bool autoTune = true;     // off: ignore the detected root and play the sample as recorded on C5
};

// Per-voice state exposed to the UI (the envelope dot).
struct EnvSnapshot
{
    int stage = -1;   // 0 attack, 1 decay, 2 sustain, 3 release, -1 idle
    float frac = 0.f; // progress through the stage, 0..1
    float level = 0.f;
    float playPos = -1.f;
};

class Sampler
{
public:
    static constexpr int numVoices = 24;

    void prepare(double sr)
    {
        sampleRate = sr;
        for (auto& v : voices) v = Voice {};
    }

    void setSample(std::shared_ptr<const SampleData> s)
    {
        juce::SpinLock::ScopedLockType lock(sampleLock);
        sample = std::move(s);
        for (auto& v : voices) v.active = false;
        held.clear();
    }

    std::shared_ptr<const SampleData> getSample() const
    {
        juce::SpinLock::ScopedLockType lock(sampleLock);
        return sample;
    }

    void setParams(const SamplerParams& p) { params = p; }

    EnvSnapshot getSnapshot() const
    {
        juce::SpinLock::ScopedLockType lock(snapLock);
        return snapshot;
    }

    void noteOn(int note, float velocity)
    {
        auto s = getSample();
        if (!s) return;
        currentSample = s;
        const double target = rateFor(note, *s);

        if (params.mono)
        {
            Voice& v = voices[0];
            const bool legato = !held.empty() && v.active && !v.releasing;
            held.erase(std::remove(held.begin(), held.end(), note), held.end());
            held.push_back(note);
            if (legato)
            {
                v.note = note;
                v.target = target;
                return;
            }
            const double from = (params.glideOn && v.hasPlayed) ? v.rate : -1.0;
            startVoice(0, note, velocity, target, from);
            return;
        }

        int idx = -1;
        for (int i = 0; i < numVoices; ++i)
            if (voices[(size_t) i].active && voices[(size_t) i].note == note && !voices[(size_t) i].releasing) { idx = i; break; }
        if (idx < 0) idx = pickVoice();

        Voice& v = voices[(size_t) idx];
        double from = -1.0;
        if (params.glideOn && v.hasPlayed)
        {
            if (params.legato)
            {
                if (v.active && !v.releasing) from = v.rate;
            }
            else from = v.rate;
        }
        startVoice(idx, note, velocity, target, from);
    }

    void noteOff(int note)
    {
        if (!params.mono)
        {
            if (!params.adsrOn) return;
            for (auto& v : voices)
                if (v.active && v.note == note && !v.releasing) beginRelease(v);
            return;
        }

        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        Voice& v = voices[0];
        if (v.active && v.note == note && !v.releasing)
        {
            if (!held.empty())
            {
                v.note = held.back();
                v.target = currentSample ? rateFor(v.note, *currentSample) : v.target;
                if (!params.glideOn) v.rate = v.target;
            }
            else if (params.adsrOn) beginRelease(v);
        }
    }

    void allNotesOff()
    {
        held.clear();
        for (auto& v : voices)
            if (v.active && !v.fading) beginFade(v);
    }

    // Adds the sampler output to L and R (the caller clears them first).
    void render(float* L, float* R, int n)
    {
        {
            juce::SpinLock::ScopedLockType lock(sampleLock);
            currentSample = sample;
        }
        auto s = currentSample;
        if (!s || s->audio.getNumSamples() < 2)
        {
            publishSnapshot(-1, 0.f, 0.f, -1.f);
            return;
        }

        const float* ch0 = s->audio.getReadPointer(0);
        const float* ch1 = s->audio.getNumChannels() > 1 ? s->audio.getReadPointer(1) : ch0;
        const double len = (double) s->audio.getNumSamples();
        const double dt = 1.0 / sampleRate;
        const double rateRatio = s->sampleRate / sampleRate;
        const double glideCoef = params.glideSeconds > 0.f ? 1.0 - std::exp(-4.6 / (params.glideSeconds * sampleRate)) : 1.0;

        // playable range with short fades at both ends so a cut never clicks
        const double startPos = juce::jlimit(0.0, len - 2.0, (double) params.startFrac * len);
        const double endPos = juce::jlimit(startPos + 64.0, len - 2.0, (double) params.endFrac * len);
        const double fadeIn = s->sampleRate * juce::jmax(0.002, params.fadeInMs / 1000.0);
        const double fadeOut = s->sampleRate * juce::jmax(0.004, params.fadeOutMs / 1000.0);

        for (int vi = 0; vi < numVoices; ++vi)
        {
            Voice& v = voices[(size_t) vi];
            if (!v.active) continue;

            for (int i = 0; i < n && v.active; ++i)
            {
                if (params.glideOn && params.glideSeconds > 0.f) v.rate += (v.target - v.rate) * glideCoef;
                else v.rate = v.target;

                v.pos += v.rate * rateRatio;
                if (v.pos >= endPos) { v.active = false; break; }

                const float lev = stepEnvelope(v, dt);
                if (!v.active) break;

                const int p0 = (int) v.pos;
                const float fr = (float) (v.pos - p0);
                const float l = ch0[p0] * (1.f - fr) + ch0[p0 + 1] * fr;
                const float r = ch1[p0] * (1.f - fr) + ch1[p0 + 1] * fr;
                const double edge = juce::jmin(1.0, juce::jmin((v.pos - startPos) / fadeIn, (endPos - v.pos) / fadeOut));
                const float g = v.velGain * lev * (float) juce::jmax(0.0, edge);
                L[i] += l * g;
                R[i] += r * g;
            }
        }

        // the most recently started voice drives the UI dot
        const Voice* shown = nullptr;
        for (const auto& v : voices)
            if (v.active && (shown == nullptr || v.serial > shown->serial)) shown = &v;
        if (shown != nullptr)
        {
            const float frac = (float) juce::jlimit(0.0, 1.0, shown->stageT / std::max(1e-4, stageLength(*shown)));
            publishSnapshot(shown->stage, frac, shown->level, (float) (shown->pos / len));
        }
        else publishSnapshot(-1, 0.f, 0.f, -1.f);
    }

private:
    struct Voice
    {
        bool active = false;
        bool releasing = false;
        bool fading = false;
        bool hasPlayed = false;
        int note = -1;
        float velocity = 0.8f;
        float velGain = 0.83f;
        double pos = 0.0;
        double rate = 1.0;
        double target = 1.0;
        int stage = 0;        // 0 attack, 1 decay, 2 sustain, 3 release, 4 fade
        double stageT = 0.0;
        float level = 0.f;
        float relFrom = 1.f;
        unsigned long long serial = 0;
    };

    double rateFor(int note, const SampleData& s) const
    {
        const double detected = params.foldRoot ? SampleLoader::foldRoot(s.midi) : s.midi;
        const double root = (s.hasPitch && params.autoTune ? detected : 60.0) + params.rootShift;
        return std::pow(2.0, (note - root + params.fineCents / 100.0) / 12.0);
    }

    double stageLength(const Voice& v) const
    {
        switch (v.stage)
        {
            case 0: return std::max(0.001, (double) params.attack);
            case 1: return std::max(0.002, (double) params.decay);
            case 3: return std::max(0.005, (double) params.release);
            case 4: return 0.012;
            default: return 1.2;
        }
    }

    static float curveExp(float c) { return std::pow(4.f, c); }

    int pickVoice()
    {
        for (int i = 0; i < numVoices; ++i)
            if (!voices[(size_t) i].active) return i;
        int oldest = 0;
        for (int i = 1; i < numVoices; ++i)
            if (voices[(size_t) i].serial < voices[(size_t) oldest].serial) oldest = i;
        return oldest;
    }

    void startVoice(int idx, int note, float velocity, double target, double fromRate)
    {
        Voice& v = voices[(size_t) idx];
        v.active = true;
        v.releasing = false;
        v.fading = false;
        v.hasPlayed = true;
        v.note = note;
        v.velocity = velocity;
        // FL's default note velocity (100/127) plays at the old level; lower values fall away clearly
        v.velGain = juce::jmin(1.1f, 0.83f * std::pow(juce::jlimit(0.f, 1.f, velocity) / 0.787f, 1.4f));
        v.pos = currentSample ? juce::jlimit(0.f, 1.f, params.startFrac) * (double) currentSample->audio.getNumSamples() : 0.0;
        v.target = target;
        v.rate = fromRate > 0.0 ? fromRate : target;
        v.stage = params.adsrOn ? 0 : 2;
        v.stageT = 0.0;
        v.level = params.adsrOn ? 0.f : 1.f;
        v.serial = ++serialCounter;
    }

    void beginRelease(Voice& v)
    {
        v.relFrom = v.level;
        v.stage = 3;
        v.stageT = 0.0;
        v.releasing = true;
    }

    void beginFade(Voice& v)
    {
        v.relFrom = v.level;
        v.stage = 4;
        v.stageT = 0.0;
        v.fading = true;
        v.releasing = true;
    }

    // Advances the envelope one sample and returns its level (0..1).
    float stepEnvelope(Voice& v, double dt)
    {
        v.stageT += dt;

        if (v.stage == 4)
        {
            const double f = std::min(1.0, v.stageT / 0.012);
            if (f >= 1.0) v.active = false;
            v.level = (float) (v.relFrom * (1.0 - f));
            return v.level;
        }

        if (!params.adsrOn)
        {
            v.level = 1.f;
            return 1.f;
        }

        const float sus = params.sustain;
        switch (v.stage)
        {
            case 0:
            {
                const double a = std::max(0.001, (double) params.attack);
                const double f = std::min(1.0, v.stageT / a);
                v.level = (float) std::pow(f, curveExp(params.curveA));
                if (v.stageT >= a) { v.stage = 1; v.stageT = 0.0; }
                break;
            }
            case 1:
            {
                const double d = std::max(0.002, (double) params.decay);
                const double f = std::min(1.0, v.stageT / d);
                v.level = sus + (1.f - sus) * (float) std::pow(1.0 - f, curveExp(params.curveD));
                if (v.stageT >= d) { v.stage = 2; v.stageT = 0.0; v.level = sus; }
                break;
            }
            case 2:
                v.level = sus;
                break;
            case 3:
            {
                const double r = std::max(0.005, (double) params.release);
                const double f = std::min(1.0, v.stageT / r);
                v.level = v.relFrom * (float) std::pow(1.0 - f, curveExp(params.curveR));
                if (f >= 1.0) v.active = false;
                break;
            }
            default:
                break;
        }
        return v.level;
    }

    void publishSnapshot(int stage, float frac, float level, float pos)
    {
        juce::SpinLock::ScopedLockType lock(snapLock);
        snapshot.stage = stage;
        snapshot.frac = frac;
        snapshot.level = level;
        snapshot.playPos = pos;
    }

    double sampleRate = 44100.0;
    std::array<Voice, numVoices> voices {};
    std::vector<int> held;
    SamplerParams params;
    unsigned long long serialCounter = 0;

    mutable juce::SpinLock sampleLock;
    std::shared_ptr<const SampleData> sample;
    std::shared_ptr<const SampleData> currentSample;

    mutable juce::SpinLock snapLock;
    EnvSnapshot snapshot;
};
