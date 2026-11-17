#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

struct FXParams
{
    bool dlyOn = false;
    bool dlyPing = false;
    float dlySeconds = 0.25f;
    float dlyFeedback = 0.35f;
    float dlyAmount = 0.3f;
    float dlySpread = 1.0f;   // 0 = both echoes centred, 1 = full left/right ping-pong
    bool revOn = false;
    float revRoom = 0.4f;     // 0..1
    float revAmount = 0.35f;
    bool satOn = false;
    float satDrive = 0.35f;   // 0..1
    bool satMatch = true;     // trim the saturated signal to the loudness it came in with
    int satMode = 0;          // 0 soft, 1 tube, 2 hard, 3 fold, 4 crush, 5 rectify
    float revLowCut = 20.f, revHighCut = 20000.f;
    bool fltOn = false;
    float fltHp = 20.f, fltLp = 20000.f;
    int fltHpSlope = 1, fltLpSlope = 1;
    float fltMF = 1000.f, fltMG = 0.f, fltMQ = 0.9f;
    int order = 12;           // index into the 24 chain permutations below
};

// Order of the four effects (0 delay, 1 reverb, 2 saturation, 3 filter). The 24 permutations are in
// lexicographic order, which the page rebuilds the same way. Index 12 is saturation, delay, reverb, filter.
inline const std::array<std::array<int, 4>, 24>& fxChains()
{
    static const auto table = []
    {
        std::array<std::array<int, 4>, 24> t {};
        std::array<int, 4> p { 0, 1, 2, 3 };
        for (auto& row : t) { row = p; std::next_permutation(p.begin(), p.end()); }
        return t;
    }();
    return table;
}

// One-knob saturator with four voicings. Drive raises the level into the curve, the output is
// scaled back so the knob changes character more than loudness.
class Saturator
{
public:
    void prepare(double sr) { sampleRate = (float) sr; reset(); }
    void reset() { dcL = dcR = 0.f; mixSm = 0.f; driveSm = 0.f; envIn = envOut = 0.f; gainSm = 1.f; }

    void process(float* L, float* R, int n, bool on, float drive, int mode, bool match)
    {
        const float mixTarget = on ? 1.f : 0.f;
        const float envCoef = 1.f - std::exp(-1.f / (0.06f * sampleRate));
        const float dcCoef = 1.f - std::exp(-2.f * juce::MathConstants<float>::pi * 12.f / sampleRate);
        for (int i = 0; i < n; ++i)
        {
            mixSm += (mixTarget - mixSm) * 0.01f;
            driveSm += (juce::jlimit(0.f, 1.f, drive) - driveSm) * 0.01f;
            if (mixSm < 0.0005f && !on) continue;

            const float pre = 1.f + 17.f * driveSm * driveSm;
            float xl = L[i], xr = R[i];
            if (mode == 5)
            {
                // sample-rate reduction: hold every Nth sample, N from 1 to 40
                holdN = 1.f + 39.f * driveSm * driveSm;
                if ((holdCount += 1.f) >= holdN) { holdCount -= holdN; heldL = xl; heldR = xr; }
                xl = heldL; xr = heldR;
            }
            float yl = shape(xl, pre, mode, dcL, dcCoef);
            float yr = shape(xr, pre, mode, dcR, dcCoef);
            // volume match: follow the loudness going in and coming out, and trim the wet signal to equal it
            const float pIn = 0.5f * (L[i] * L[i] + R[i] * R[i]), pOut = 0.5f * (yl * yl + yr * yr);
            envIn += (pIn - envIn) * envCoef;
            envOut += (pOut - envOut) * envCoef;
            const float gT = match ? juce::jlimit(0.25f, 4.f, std::sqrt((envIn + 1e-9f) / (envOut + 1e-9f))) : 1.f;
            gainSm += (gT - gainSm) * 0.002f;
            yl *= gainSm; yr *= gainSm;
            L[i] += (yl - L[i]) * mixSm;
            R[i] += (yr - R[i]) * mixSm;
        }
    }

private:
    static float shape(float x, float pre, int mode, float& dc, float dcCoef)
    {
        float y;
        switch (mode)
        {
            case 1:   // tube: asymmetric curve, adds even harmonics
            {
                constexpr float bias = 0.28f;
                y = (std::tanh(pre * x + bias) - std::tanh(bias)) / std::tanh(pre * 0.9f + 0.2f);
                break;
            }
            case 2:   // hard: flat-topped clip
                y = juce::jlimit(-1.f, 1.f, pre * x) * 0.85f;
                break;
            case 3:   // fold: the peaks bend back instead of clipping
                y = std::sin(juce::jlimit(-3.f, 3.f, pre * x * 1.1f)) * 0.9f;
                break;
            case 4:   // bits: fewer and fewer amplitude steps, 12 bits down to about 3
            {
                const float d = std::sqrt(juce::jlimit(0.f, 1.f, (pre - 1.f) / 17.f));
                const float steps = std::pow(2.f, 11.f - 8.f * d);
                y = std::round(juce::jlimit(-1.f, 1.f, x) * steps) / steps * 0.9f;
                break;
            }
            case 5:   // rate: the sample-and-hold stage runs in process(); only a little level trim here
                y = x * 0.9f;
                break;
            default:  // soft: smooth tanh
                y = std::tanh(pre * x) / std::tanh(pre);
                break;
        }
        // the asymmetric curve leaves a little DC behind
        dc += dcCoef * (y - dc);
        return y - dc;
    }

    float sampleRate = 44100.f;
    float dcL = 0.f, dcR = 0.f, mixSm = 0.f, driveSm = 0.f;
    float envIn = 0.f, envOut = 0.f, gainSm = 1.f;
    float heldL = 0.f, heldR = 0.f, holdCount = 0.f, holdN = 1.f;
};

// Small circular delay with a fractional (linear) read, used by the reverb.
struct ModDelay
{
    void init(int maxSamples)
    {
        buf.assign((size_t) maxSamples + 4, 0.f);
        w = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); w = 0; }
    float read(float d) const
    {
        const int n = (int) buf.size();
        float rp = (float) w - d;
        while (rp < 0.f) rp += (float) n;
        const int i0 = (int) rp;
        const float f = rp - (float) i0;
        const float a = buf[(size_t) (i0 % n)], b = buf[(size_t) ((i0 + 1) % n)];
        return a + (b - a) * f;
    }
    void write(float v)
    {
        buf[(size_t) w] = v;
        if (++w >= (int) buf.size()) w = 0;
    }
    std::vector<float> buf;
    int w = 0;
};

// Schroeder allpass used to smear the input before the feedback network.
struct Allpass
{
    void init(int len) { d.assign((size_t) len, 0.f); i = 0; }
    void clear() { std::fill(d.begin(), d.end(), 0.f); i = 0; }
    float process(float x, float g)
    {
        const float z = d[(size_t) i];
        const float y = z - g * x;
        d[(size_t) i] = x + g * y;
        if (++i >= (int) d.size()) i = 0;
        return y;
    }
    std::vector<float> d;
    int i = 0;
};

// 8-line feedback delay network with a Hadamard mix, input diffusion, pre-delay,
// frequency-dependent damping and slow delay modulation. Mixed with a constant-power
// crossfade so switching it on never makes the sound louder.
class FDNReverb
{
public:
    static constexpr int N = 8;

    void prepare(double sr)
    {
        sampleRate = (float) sr;
        const float scale = (float) (sr / 44100.0);
        static constexpr int base[N] = { 1031, 1327, 1523, 1871, 2153, 2447, 2749, 3079 };
        for (int i = 0; i < N; ++i)
        {
            baseLen[(size_t) i] = (float) base[i] * scale;
            line[(size_t) i].init((int) (baseLen[(size_t) i] * maxScale) + 64);
            curLen[(size_t) i] = baseLen[(size_t) i];
        }
        static constexpr int apLen[4] = { 142, 107, 379, 277 };
        for (int c = 0; c < 2; ++c)
            for (int a = 0; a < 4; ++a)
                ap[c][a].init((int) ((float) apLen[a] * scale * (c == 0 ? 1.f : 1.13f)) + 1);
        pre.init((int) (sr * 0.08) + 4);
        reset();
    }

    void reset()
    {
        for (auto& l : line) l.clear();
        for (auto& c : ap) for (auto& a : c) a.clear();
        pre.clear();
        lp.fill(0.f);
        hpL = hpR = hp2L = hp2R = lpL1 = lpR1 = lpL2 = lpR2 = 0.f;
        phase = 0.f;
        wetSm = 0.f;
    }

    // room 0..1 sets size and decay, amount 0..1 is the wet level, on fades the whole effect
    void process(float* L, float* R, int n, float room, float amount, bool on, float lowCut = 20.f, float highCut = 20000.f)
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float hpC = 1.f - std::exp(-twoPi * juce::jlimit(20.f, 1000.f, lowCut) / sampleRate);
        const float lpC = 1.f - std::exp(-twoPi * juce::jlimit(1000.f, 0.45f * sampleRate, highCut) / sampleRate);
        room = juce::jlimit(0.f, 1.f, room);
        const float size = 0.7f + 1.5f * room;                          // delay length scale
        const float rt60 = 0.5f + 3.3f * room * room + 1.2f * room;     // seconds
        const float wetTarget = on ? juce::jlimit(0.f, 1.f, amount) : 0.f;
        const float damp = 0.22f + 0.4f * (1.f - room * 0.5f);          // one-pole coefficient, bigger = brighter
        const float preDelay = 0.012f * sampleRate;

        for (int i = 0; i < n; ++i)
        {
            wetSm += (wetTarget - wetSm) * 0.002f;

            phase += 0.35f / sampleRate;
            if (phase > 1.f) phase -= 1.f;

            pre.write(0.5f * (L[i] + R[i]));
            const float x = pre.read(preDelay);

            float dl = x, dr = x;
            for (int a = 0; a < 4; ++a)
            {
                dl = ap[0][a].process(dl, 0.62f);
                dr = ap[1][a].process(dr, 0.62f);
            }
            const float inject = 0.25f * (dl + dr);

            std::array<float, N> out;
            for (size_t k = 0; k < N; ++k)
            {
                // line lengths glide toward the target so a size change never clicks
                curLen[k] += (baseLen[k] * size - curLen[k]) * 0.0004f;
                const float mod = 1.6f * std::sin(juce::MathConstants<float>::twoPi * (phase * (0.7f + 0.13f * (float) k) + 0.125f * (float) k));
                out[k] = line[k].read(curLen[k] + mod);
            }

            // Hadamard feedback matrix (energy preserving), scaled by 1/sqrt(8)
            std::array<float, N> mix = out;
            for (int len = 1; len < N; len <<= 1)
                for (int a = 0; a < N; a += len << 1)
                    for (int b = a; b < a + len; ++b)
                    {
                        const float u = mix[(size_t) b], v = mix[(size_t) (b + len)];
                        mix[(size_t) b] = u + v;
                        mix[(size_t) (b + len)] = u - v;
                    }
            constexpr float norm = 0.35355339f;

            float wl = 0.f, wr = 0.f;
            for (size_t k = 0; k < N; ++k)
            {
                // per-line gain for the requested RT60, then a lowpass so the highs die first
                const float g = std::pow(10.f, -3.f * curLen[k] / (sampleRate * rt60));
                lp[k] += damp * (mix[k] * norm * g - lp[k]);
                line[k].write(inject + lp[k]);
                const float s = (k & 2) ? -1.f : 1.f;
                if (k & 1) wr += out[k] * s;
                else       wl += out[k] * s;
            }
            wl *= 0.5f; wr *= 0.5f;

            // low cut and high cut shape only the tail (two one-poles each, 12 dB per octave)
            hpL += hpC * (wl - hpL); wl -= hpL;
            hpR += hpC * (wr - hpR); wr -= hpR;
            hp2L += hpC * (wl - hp2L); wl -= hp2L;
            hp2R += hpC * (wr - hp2R); wr -= hp2R;
            lpL1 += lpC * (wl - lpL1); wl = lpL1;
            lpR1 += lpC * (wr - lpR1); wr = lpR1;
            lpL2 += lpC * (wl - lpL2); wl = lpL2;
            lpR2 += lpC * (wr - lpR2); wr = lpR2;

            // constant-power crossfade: the dry sound gives way to the tail instead of stacking on top
            const float ang = wetSm * juce::MathConstants<float>::halfPi * 0.55f;
            const float dryG = std::cos(ang), wetG = std::sin(ang) * 1.5f;
            L[i] = L[i] * dryG + wl * wetG;
            R[i] = R[i] * dryG + wr * wetG;
        }
    }

private:
    static constexpr float maxScale = 2.3f;
    float sampleRate = 44100.f;
    std::array<ModDelay, N> line;
    std::array<float, N> baseLen {}, curLen {}, lp {};
    Allpass ap[2][4];
    ModDelay pre;
    float hpL = 0.f, hpR = 0.f, hp2L = 0.f, hp2R = 0.f, lpL1 = 0.f, lpR1 = 0.f, lpL2 = 0.f, lpR2 = 0.f;
    float phase = 0.f, wetSm = 0.f;
};

// Five-band tone filter: low cut, low shelf, mid bell, high shelf, high cut (RBJ biquads).
class ToneFilter
{
public:
    struct Params
    {
        float hp = 20.f, lp = 20000.f;
        int hpSlope = 1, lpSlope = 1;   // 0 = 6 dB/oct, 1 = 12, 2 = 24
        float mF = 1000.f, mG = 0.f, mQ = 0.9f;
    };

    void prepare(double sr) { sampleRate = (float) sr; reset(); }
    void reset() { for (auto& c : ch) for (auto& s : c) s = Stage {}; mixSm = 0.f; }

    void process(float* L, float* R, int n, bool on, const Params& p)
    {
        setup(p);
        float* data[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            mixSm += ((on ? 1.f : 0.f) - mixSm) * 0.01f;
            if (!on && mixSm < 0.0005f) continue;
            for (int c = 0; c < 2; ++c)
            {
                float y = data[c][i];
                for (int s = 0; s < 5; ++s)
                    if (ch[c][s].active) y = ch[c][s].run(y);
                data[c][i] += (y - data[c][i]) * mixSm;
            }
        }
    }

private:
    struct Stage
    {
        bool active = false;
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
        {
            b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0; active = true;
        }
    };

    void setup(const Params& p)
    {
        for (int c = 0; c < 2; ++c)
        {
            const bool hpOn = p.hp > 22.f, lpOn = p.lp < 19000.f;
            // 6 dB: one first-order section, 12 dB: one Butterworth biquad, 24 dB: two with split Q
            cut(ch[c][0], true,  p.hp, p.hpSlope == 0 ? 0 : (p.hpSlope == 1 ? 0.7071f : 0.5412f), p.hpSlope == 0, hpOn);
            cut(ch[c][1], true,  p.hp, 1.3066f, false, hpOn && p.hpSlope == 2);
            tone(ch[c][2], p.mF, p.mG, p.mQ, std::abs(p.mG) > 0.05f);
            cut(ch[c][3], false, p.lp, p.lpSlope == 0 ? 0 : (p.lpSlope == 1 ? 0.7071f : 0.5412f), p.lpSlope == 0, lpOn);
            cut(ch[c][4], false, p.lp, 1.3066f, false, lpOn && p.lpSlope == 2);
        }
    }

    void cut(Stage& s, bool highPass, float f, float q, bool firstOrder, bool wanted)
    {
        if (!wanted) { s.active = false; s.z1 = s.z2 = 0.f; return; }
        const float w0 = juce::MathConstants<float>::twoPi * juce::jlimit(20.f, 0.45f * sampleRate, f) / sampleRate;
        if (firstOrder)
        {
            const float k = std::tan(w0 * 0.5f);
            if (highPass) s.set(1.f, -1.f, 0.f, 1.f + k, k - 1.f, 0.f);
            else          s.set(k, k, 0.f, 1.f + k, k - 1.f, 0.f);
            return;
        }
        const float cw = std::cos(w0), al = std::sin(w0) / (2.f * q);
        if (highPass) s.set((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
        else          s.set((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
    }

    void tone(Stage& s, float f, float gainDb, float q, bool wanted)
    {
        if (!wanted) { s.active = false; s.z1 = s.z2 = 0.f; return; }
        const float w0 = juce::MathConstants<float>::twoPi * juce::jlimit(20.f, 0.45f * sampleRate, f) / sampleRate;
        const float cw = std::cos(w0), al = std::sin(w0) / (2.f * juce::jmax(0.2f, q));
        const float A = std::pow(10.f, gainDb / 40.f);
        s.set(1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
    }

    float sampleRate = 44100.f, mixSm = 0.f;
    Stage ch[2][5];
};

// Ping-pong (or straight) delay with a dark loop, then the stereo reverb.
class ChromaticFX
{
public:
    void prepare(double sr, int maxBlock)
    {
        sampleRate = sr;
        const int maxDelay = (int) (sr * 6.2);
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 1 };
        dLine.setMaximumDelayInSamples(maxDelay);
        rLine.setMaximumDelayInSamples(maxDelay);
        dLine.prepare(spec);
        rLine.prepare(spec);
        reverb.prepare(sr);
        sat.prepare(sr);
        filter.prepare(sr);
        lpCoef = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 6000.0f / (float) sr);
        reset();
    }

    void reset()
    {
        dLine.reset();
        rLine.reset();
        lpL = lpR = 0.f;
        reverb.reset();
        sat.reset();
        filter.reset();
    }

    // L and R are the dry sum; the effects are applied in place.
    void process(float* L, float* R, int n, const FXParams& p)
    {
        const auto& chain = fxChains()[(size_t) juce::jlimit(0, 23, p.order)];
        ToneFilter::Params tp;
        tp.hp = p.fltHp; tp.lp = p.fltLp; tp.hpSlope = p.fltHpSlope; tp.lpSlope = p.fltLpSlope;
        tp.mF = p.fltMF; tp.mG = p.fltMG; tp.mQ = p.fltMQ;
        for (int slot = 0; slot < 4; ++slot)
        {
            switch (chain[(size_t) slot])
            {
                case 0: delayStage(L, R, n, p); break;
                case 1: reverb.process(L, R, n, p.revRoom, p.revAmount, p.revOn, p.revLowCut, p.revHighCut); break;
                case 2: sat.process(L, R, n, p.satOn, p.satDrive, p.satMode, p.satMatch); break;
                default: filter.process(L, R, n, p.fltOn, tp); break;
            }
        }
    }

private:
    void delayStage(float* L, float* R, int n, const FXParams& p)
    {
        const float fb = juce::jlimit(0.f, 0.9f, p.dlyFeedback);
        const float delaySamples = juce::jlimit(1.f, (float) (sampleRate * 6.0), p.dlySeconds * (float) sampleRate);
        const float spread = juce::jlimit(0.f, 1.f, p.dlySpread);

        for (int i = 0; i < n; ++i)
        {
            const float in = 0.5f * (L[i] + R[i]);
            const float dlo = dLine.popSample(0, delaySamples);
            const float dro = rLine.popSample(0, delaySamples);

            lpL += lpCoef * (dlo - lpL);
            lpR += lpCoef * (dro - lpR);

            float inL = in, inR = 0.f;
            if (p.dlyPing)
            {
                inL += lpR * fb;
                inR = lpL * fb;
            }
            else
            {
                inL += lpL * fb;
            }
            dLine.pushSample(0, inL);
            rLine.pushSample(0, inR);

            if (p.dlyOn)
            {
                if (p.dlyPing)
                {
                    // spread blends from a centred echo (0) to hard left/right bounces (1)
                    const float mid = 0.5f * (dlo + dro), side = 0.5f * (dlo - dro) * spread;
                    L[i] += (mid + side) * p.dlyAmount;
                    R[i] += (mid - side) * p.dlyAmount;
                }
                else
                {
                    L[i] += dlo * p.dlyAmount;
                    R[i] += dlo * p.dlyAmount;
                }
            }
        }
    }

    double sampleRate = 44100.0;
    Saturator sat;
    ToneFilter filter;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dLine { 1 }, rLine { 1 };
    FDNReverb reverb;
    float lpCoef = 0.5f, lpL = 0.f, lpR = 0.f;
};
