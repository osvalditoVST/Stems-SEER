#include "Analysis.h"
#include "Fft.h"
#include <algorithm>
#include <cmath>

namespace stemseer {

// ============================================================================
//  BPM: flujo espectral (onsets) -> autocorrelacion -> peine + prior -> refinado
// ============================================================================
double detectBpm (const std::vector<float>& mono, double sr, const ProgressFn& progress)
{
    const int N = 1024, hop = 256;
    if (sr <= 0.0 || mono.size() < (size_t) N * 16) return 0.0;

    const int frames = (int) ((mono.size() - (size_t) N) / (size_t) hop) + 1;
    Fft fft (N);

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));

    const int kMax = std::min (N / 2, (int) (6000.0 / (sr / N)));
    std::vector<float> buf ((size_t) (2 * N)), prev ((size_t) (N / 2 + 1), 0.0f), env ((size_t) frames, 0.0f);

    for (int t = 0; t < frames; ++t)
    {
        const float* x = &mono[(size_t) t * (size_t) hop];
        for (int i = 0; i < N; ++i) { buf[(size_t) (2 * i)] = x[i] * win[(size_t) i]; buf[(size_t) (2 * i + 1)] = 0.0f; }
        fft.forward (buf.data());

        float flux = 0.0f;
        for (int k = 1; k <= kMax; ++k)
        {
            const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
            const float lm = std::log1p (10.0f * std::sqrt (re * re + im * im));
            const float d = lm - prev[(size_t) k];
            if (d > 0.0f) flux += d;
            prev[(size_t) k] = lm;
        }
        env[(size_t) t] = flux;
        if (progress && (t & 255) == 0 && ! progress (0.8f * (float) t / (float) frames)) return 0.0;
    }

    // Quitar la media local y rectificar
    std::vector<double> cs ((size_t) frames + 1, 0.0);
    for (int t = 0; t < frames; ++t) cs[(size_t) t + 1] = cs[(size_t) t] + env[(size_t) t];
    std::vector<float> e ((size_t) frames);
    for (int t = 0; t < frames; ++t)
    {
        const int lo = std::max (0, t - 21), hi = std::min (frames, t + 22);
        const double mean = (cs[(size_t) hi] - cs[(size_t) lo]) / (hi - lo);
        e[(size_t) t] = (float) std::max (0.0, env[(size_t) t] - mean);
    }

    // Autocorrelacion
    const double fr = sr / hop;                                   // frames por segundo
    const int minLag     = std::max (2, (int) std::floor (fr * 60.0 / 200.0));
    const int maxBaseLag = (int) std::ceil  (fr * 60.0 / 60.0);
    const int maxLag     = std::min (frames / 2, maxBaseLag * 8 + 16);
    if (maxLag <= minLag + 4) return 0.0;

    std::vector<double> acf ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        const int n = frames - lag;
        for (int t = 0; t < n; ++t) s += (double) e[(size_t) t] * (double) e[(size_t) (t + lag)];
        acf[(size_t) lag] = s / n;
        if (progress && (lag & 63) == 0 && ! progress (0.8f + 0.2f * (float) (lag - minLag) / (float) (maxLag - minLag))) return 0.0;
    }
    auto get = [&] (int l) -> double { return (l >= 0 && l <= maxLag) ? acf[(size_t) l] : 0.0; };

    // Mejor candidato con peine armonico y prior de tempo (centrado en 120 BPM)
    double best = -1.0; int bestLag = 0;
    for (int lag = minLag; lag <= std::min (maxBaseLag, maxLag); ++lag)
    {
        const double bpm = 60.0 * fr / lag;
        if (bpm < 70.0 || bpm > 180.0) continue;
        const double o = std::log2 (bpm / 120.0) / 0.9;
        const double prior = std::exp (-0.5 * o * o);
        const double s = (get (lag) + 0.5 * get (2 * lag) + 0.25 * get (4 * lag)) * prior;
        if (s > best) { best = s; bestLag = lag; }
    }
    if (bestLag == 0 || best <= 0.0) return 0.0;

    auto parab = [&] (int l) -> double
    {
        const double a = get (l - 1), b = get (l), c = get (l + 1);
        const double den = a - 2.0 * b + c;
        if (std::fabs (den) < 1e-12) return (double) l;
        double d = 0.5 * (a - c) / den;
        if (d > 1.0 || d < -1.0) d = 0.0;
        return l + d;
    };

    double lag = parab (bestLag);
    for (int m : { 2, 4, 8 })                       // refinar usando multiplos (mas resolucion)
    {
        const int centre = (int) std::lround (lag * m);
        const int lo = centre - m - 1, hi = centre + m + 1;
        if (hi >= maxLag) break;
        int pk = lo;
        for (int l = lo; l <= hi; ++l) if (get (l) > get (pk)) pk = l;
        const double r = parab (pk) / m;
        if (std::fabs (r - lag) / lag > 0.02) break;
        lag = r;
    }

    double bpm = 60.0 * fr / lag;
    const double rb = std::round (bpm);
    if (std::fabs (bpm - rb) < 0.12) bpm = rb;      // muchos temas estan en BPM entero
    return std::round (bpm * 10.0) / 10.0;
}

// ============================================================================
//  Tonalidad: cromagrama + perfiles Krumhansl-Kessler + deteccion de modo
// ============================================================================
KeyResult detectKey (const std::vector<float>& mono, double sr, const ProgressFn& progress)
{
    KeyResult res;
    const int N = 16384, hop = 8192;
    if (sr <= 0.0 || mono.size() < (size_t) N * 2) return res;

    Fft fft (N);
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));

    std::vector<int> pcOf ((size_t) (N / 2 + 1), -1);
    for (int k = 1; k <= N / 2; ++k)
    {
        const double f = k * sr / N;
        if (f < 80.0 || f > 2200.0) continue;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        pcOf[(size_t) k] = (((int) std::lround (midi)) % 12 + 12) % 12;
    }

    const int frames = (int) ((mono.size() - (size_t) N) / (size_t) hop) + 1;
    std::vector<float> buf ((size_t) (2 * N));
    double chroma[12] = {};

    for (int t = 0; t < frames; ++t)
    {
        const float* x = &mono[(size_t) t * (size_t) hop];
        for (int i = 0; i < N; ++i) { buf[(size_t) (2 * i)] = x[i] * win[(size_t) i]; buf[(size_t) (2 * i + 1)] = 0.0f; }
        fft.forward (buf.data());

        double c[12] = {}, tot = 0.0;
        for (int k = 1; k <= N / 2; ++k)
        {
            const int pc = pcOf[(size_t) k];
            if (pc < 0) continue;
            const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
            const double m = std::sqrt (std::sqrt ((double) (re * re + im * im)));   // compresion
            c[pc] += m; tot += m;
        }
        if (tot > 1e-6) for (int i = 0; i < 12; ++i) chroma[i] += c[i] / tot;

        if (progress && (t & 31) == 0 && ! progress ((float) t / (float) frames)) return res;
    }

    static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorP[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    auto corr = [&] (const double* prof, int tonic) -> double
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(tonic + i) % 12]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double dx = chroma[(tonic + i) % 12] - mx, dy = prof[i] - my;
            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }
        return (sxx > 0 && syy > 0) ? sxy / std::sqrt (sxx * syy) : 0.0;
    };

    double best = -2.0; int bt = 0; bool bm = false;
    for (int t = 0; t < 12; ++t)
    {
        const double a = corr (major, t), b = corr (minorP, t);
        if (a > best) { best = a; bt = t; bm = false; }
        if (b > best) { best = b; bt = t; bm = true; }
    }

    auto ch = [&] (int semis) { return chroma[(bt + semis) % 12]; };
    int mode;
    if (bm)
    {
        if      (ch (1) > ch (2) * 1.2) mode = 2;            // Frigio  (2a menor presente)
        else if (ch (9) > ch (8) * 1.5) mode = 1;            // Dorico  (6a mayor)
        else                            mode = 5;            // Eolico
    }
    else
    {
        if      (ch (10) > ch (11) * 1.5) mode = 4;          // Mixolidio (7a menor)
        else if (ch (6)  > ch (5)  * 1.2) mode = 3;          // Lidio (4a aumentada)
        else                              mode = 0;          // Jonico
    }

    res.valid = true; res.tonic = bt; res.minor = bm; res.mode = mode;
    res.confidence = (float) std::max (0.0, std::min (1.0, best));
    return res;
}

// ============================================================================
//  Textos
// ============================================================================
static const int kModeOffset[6]   = { 0, 2, 4, 5, 7, 9 };      // distancia a la mayor "padre"
static const int kModeIntervals[6][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // Jonico
    { 0, 2, 3, 5, 7, 9, 10 },   // Dorico
    { 0, 1, 3, 5, 7, 8, 10 },   // Frigio
    { 0, 2, 4, 6, 7, 9, 11 },   // Lidio
    { 0, 2, 4, 5, 7, 9, 10 },   // Mixolidio
    { 0, 2, 3, 5, 7, 8, 10 }    // Eolico
};

std::string noteName (int pc, bool flats)
{
    static const char* S[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* F[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    pc = ((pc % 12) + 12) % 12;
    return flats ? F[pc] : S[pc];
}

bool usesFlats (const KeyResult& k)
{
    const int parent = (((k.tonic - kModeOffset[k.mode]) % 12) + 12) % 12;
    return parent == 5 || parent == 10 || parent == 3 || parent == 8 || parent == 1;
}

std::string keyTitle (const KeyResult& k)
{
    return noteName (k.tonic, usesFlats (k)) + (k.minor ? " menor" : " mayor");
}

std::string modeName (const KeyResult& k)
{
    static const char* n[] = { "Jónico", "Dórico", "Frigio", "Lidio", "Mixolidio", "Eólico" };
    return n[k.mode];
}

std::string modeSubtitle (const KeyResult& k)
{
    static const char* n[] = { "(mayor natural)", "(menor con 6ª mayor)", "(menor con 2ª menor)",
                               "(mayor con 4ª aumentada)", "(mayor con 7ª menor)", "(menor natural)" };
    return n[k.mode];
}

std::string scaleNotes (const KeyResult& k)
{
    const bool fl = usesFlats (k);
    std::string s;
    for (int i = 0; i < 7; ++i)
    {
        if (i) s += " - ";
        s += noteName (k.tonic + kModeIntervals[k.mode][i], fl);
    }
    return s;
}

bool inScale (const KeyResult& k, int pc)
{
    const int rel = (((pc - k.tonic) % 12) + 12) % 12;
    for (int i = 0; i < 7; ++i) if (kModeIntervals[k.mode][i] == rel) return true;
    return false;
}

} // namespace stemseer
