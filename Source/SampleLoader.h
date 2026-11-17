#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// A loaded one-shot. Audio is already trimmed (when auto trim is on).
struct SampleData
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    int viewStart = 0;      // first audible sample (onset)
    int viewEnd = 0;        // last audible sample (tail cut for display)
    int removedSamples = 0; // silence removed from the start/end
    bool hasPitch = false;
    double midi = 60.0;     // detected root note, fractional MIDI number
    float confidence = 0.f;
    juce::String name;
};

namespace SampleLoader
{
    // Moves a detected note into the 4th octave (MIDI 48..59, FL naming C4..B4) without touching its
    // pitch class or its cents, so the key you press is close to the sound as recorded.
    inline double foldRoot(double midi)
    {
        const int k = (int) std::lround(midi);
        const int shift = 12 * (int) std::floor((k - 48) / 12.0);
        return midi - shift;
    }

    // YIN pitch estimate on one window. Returns false when the window is not periodic.
    inline bool yinWindow(const float* x, int W, double sr, int tauMin, int tauMax, double& freq, float& conf)
    {
        std::vector<float> d((size_t) tauMax + 1, 0.f), cmnd((size_t) tauMax + 1, 1.f);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            for (int i = 0; i < W; ++i)
            {
                const double df = (double) x[i] - (double) x[i + tau];
                s += df * df;
            }
            d[(size_t) tau] = (float) s;
        }

        double run = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += d[(size_t) tau];
            cmnd[(size_t) tau] = run > 0.0 ? (float) (d[(size_t) tau] * tau / run) : 1.f;
        }

        int tau = -1;
        for (int t = tauMin; t <= tauMax; ++t)
        {
            if (cmnd[(size_t) t] < 0.15f)
            {
                while (t + 1 <= tauMax && cmnd[(size_t) (t + 1)] < cmnd[(size_t) t]) ++t;
                tau = t;
                break;
            }
        }

        if (tau < 0)
        {
            float best = 1e9f;
            for (int t = tauMin; t <= tauMax; ++t)
                if (cmnd[(size_t) t] < best) { best = cmnd[(size_t) t]; tau = t; }
            if (best > 0.35f) return false;
        }

        double bt = tau;
        if (tau > 1 && tau < tauMax)
        {
            const double a = cmnd[(size_t) (tau - 1)], b = cmnd[(size_t) tau], c = cmnd[(size_t) (tau + 1)];
            const double den = a - 2.0 * b + c;
            if (den > 0.0) bt = tau + 0.5 * (a - c) / den;
        }

        freq = sr / bt;
        conf = 1.f - cmnd[(size_t) tau];
        return true;
    }

    // Root note detection over a region of mono audio. Median of several YIN windows.
    inline bool detectPitch(const float* x, int n, double sr, double& midi, float& conf)
    {
        const int W = std::min(4096, n / 3);
        if (W < 512) return false;

        const int tauMin = std::max(2, (int) (sr / 2200.0));
        const int tauMax = std::min((int) (sr / 30.0), W / 2);
        const int span = n - W - tauMax;
        if (span <= 0) return false;

        // rank frames by energy, keep the loud ones
        const int frames = 16;
        std::vector<int> starts;
        std::vector<float> rms;
        for (int f = 0; f < frames; ++f)
        {
            const int s = (int) ((double) span * f / (frames - 1));
            double e = 0.0;
            for (int i = 0; i < W; ++i) e += (double) x[s + i] * x[s + i];
            starts.push_back(s);
            rms.push_back((float) std::sqrt(e / W));
        }
        const float maxR = *std::max_element(rms.begin(), rms.end());
        if (maxR < 1e-5f) return false;

        std::vector<double> mids;
        std::vector<float> confs;
        for (int f = 0; f < frames; ++f)
        {
            if (rms[(size_t) f] < 0.15f * maxR) continue;
            double freq = 0.0;
            float c = 0.f;
            if (yinWindow(x + starts[(size_t) f], W, sr, tauMin, tauMax, freq, c) && freq > 20.0)
            {
                mids.push_back(69.0 + 12.0 * std::log2(freq / 440.0));
                confs.push_back(c);
            }
        }
        if (mids.empty()) return false;

        std::vector<double> sorted = mids;
        std::sort(sorted.begin(), sorted.end());
        const double med = sorted[sorted.size() / 2];

        int cluster = 0;
        float meanConf = 0.f;
        for (size_t i = 0; i < mids.size(); ++i)
        {
            if (std::abs(mids[i] - med) < 0.5) { ++cluster; meanConf += confs[i]; }
        }
        if (cluster == 0) return false;

        midi = med;
        conf = (meanConf / (float) cluster) * ((float) cluster / (float) frames) * 1.5f;
        conf = std::min(1.f, conf);
        return conf > 0.2f;
    }

    // Turns raw audio into a SampleData: optional silence trim, view range, pitch.
    inline std::shared_ptr<SampleData> finish(const juce::AudioBuffer<float>& raw, double sr, bool autoTrim)
    {
        auto sd = std::make_shared<SampleData>();
        sd->sampleRate = sr;

        const int n = raw.getNumSamples(), ch = raw.getNumChannels();
        if (n == 0 || ch == 0) return sd;

        std::vector<float> mono((size_t) n, 0.f);
        for (int c = 0; c < ch; ++c)
        {
            const float* d = raw.getReadPointer(c);
            for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i] / (float) ch;
        }

        float peak = 0.f;
        for (float v : mono) peak = std::max(peak, std::abs(v));
        if (peak < 1e-5f)
        {
            sd->audio.makeCopyOf(raw);
            return sd;
        }

        int onset = 0;
        while (onset < n && std::abs(mono[(size_t) onset]) < 0.05f * peak) ++onset;

        int lastAudible = n - 1;
        while (lastAudible > onset && std::abs(mono[(size_t) lastAudible]) < 0.01f * peak) --lastAudible;

        int lastTrim = n - 1;
        while (lastTrim > onset && std::abs(mono[(size_t) lastTrim]) < 0.003f * peak) --lastTrim;

        int start = 0, end = n;
        if (autoTrim)
        {
            start = std::max(0, onset - (int) (sr * 0.002));
            end = std::min(n, lastTrim + (int) (sr * 0.02));
        }

        const int len = std::max(1, end - start);
        sd->audio.setSize(ch, len);
        for (int c = 0; c < ch; ++c) sd->audio.copyFrom(c, 0, raw, c, start, len);
        sd->removedSamples = n - len;
        sd->viewStart = std::max(0, onset - start);
        sd->viewEnd = std::min(len, lastAudible - start + (int) (sr * 0.01));

        // pitch from the attack-free body of the sound
        const int pStart = std::min(len - 1, sd->viewStart + (int) (sr * 0.02));
        const int pLen = std::min(len - pStart, (int) (sr * 1.6));
        if (pLen > 0)
        {
            double midi = 60.0;
            float conf = 0.f;
            const float* m = mono.data() + start + pStart;
            if (detectPitch(m, pLen, sr, midi, conf))
            {
                sd->hasPitch = true;
                sd->midi = midi;
                sd->confidence = conf;
            }
        }
        return sd;
    }

    inline std::shared_ptr<SampleData> fromReader(juce::AudioFormatReader& reader, bool autoTrim, const juce::String& name)
    {
        const int ch = juce::jmin(2, (int) reader.numChannels);
        const int len = (int) reader.lengthInSamples;
        juce::AudioBuffer<float> raw(ch, len);
        reader.read(raw.getArrayOfWritePointers(), ch, 0, len);

        auto sd = finish(raw, reader.sampleRate, autoTrim);
        sd->name = name;
        return sd;
    }

    inline std::shared_ptr<SampleData> loadFile(const juce::File& file, juce::AudioFormatManager& afm, bool autoTrim)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(afm.createReaderFor(file));
        if (!reader) return nullptr;
        return fromReader(*reader, autoTrim, file.getFileName());
    }

    // The built-in demo: a pluck at B4 that sits 23 cents sharp, with 60 ms of silence in front.
    inline std::shared_ptr<SampleData> demo(bool autoTrim)
    {
        const double sr = 48000.0;
        const int n = (int) (sr * 1.8), lead = (int) (sr * 0.06);
        juce::AudioBuffer<float> raw(1, n);
        raw.clear();
        float* d = raw.getWritePointer(0);
        const double f = 440.0 * std::pow(2.0, (59.23 - 69.0) / 12.0);
        juce::Random rnd(7);
        for (int i = lead; i < n; ++i)
        {
            const double t = (double) (i - lead) / sr;
            double s = 0.0;
            for (int h = 1; h <= 8; ++h)
                s += std::sin(2.0 * juce::MathConstants<double>::pi * f * h * t + h) * std::exp(-t * (1.6 + h * 0.9)) / h;
            const double noise = (rnd.nextFloat() * 2.0 - 1.0) * std::exp(-t * 60.0) * 0.25;
            d[i] = (float) ((s * 0.55 + noise) * juce::jmin(1.0, t / 0.004));
        }
        auto sd = finish(raw, sr, autoTrim);
        sd->name = "demo-pluck";
        return sd;
    }
}
