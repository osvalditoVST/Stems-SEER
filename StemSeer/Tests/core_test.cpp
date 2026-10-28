// Prueba del nucleo (sin JUCE): genera una cancion sintetica y comprueba BPM, escala y reconstruccion.
#include "../Source/Core/Separator.h"
#include "../Source/Core/Fft.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <random>
#include <cstdlib>

using namespace stemseer;

int main (int argc, char** argv)
{
    const double sr = 44100.0, bpm = argc > 1 ? std::atof (argv[1]) : 128.0, dur = 30.0;
    const size_t n = (size_t) (sr * dur);
    std::vector<float> L (n, 0.f), R (n, 0.f);
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> U (-1.f, 1.f);
    const double beat = 60.0 / bpm;

    auto add = [&] (size_t i, float l, float r) { if (i < n) { L[i] += l; R[i] += r; } };

    // Kick en cada pulso
    for (double tb = 0; tb < dur; tb += beat)
        for (int i = 0; i < (int) (0.15 * sr); ++i)
        {
            const double t = i / sr;
            const float v = (float) (0.8 * std::exp (-t * 25.0) * std::sin (2 * kPi * (50.0 * t + 1.5 * (1.0 - std::exp (-t * 30.0)))));
            add ((size_t) (tb * sr) + i, v, v);
        }
    // Hi-hat a corcheas (contratiempo)
    for (double tb = beat / 2; tb < dur; tb += beat)
        for (int i = 0; i < (int) (0.03 * sr); ++i)
        {
            const float v = 0.15f * U (rng) * (float) std::exp (-i / (0.008 * sr));
            add ((size_t) (tb * sr) + i, v, v);
        }
    // Bajo C2 sostenido por compas y acordes Cm - Ab - Eb - Bb
    const double chords[4][3] = { { 261.63, 311.13, 392.00 }, { 207.65, 261.63, 311.13 }, { 311.13, 392.00, 466.16 }, { 233.08, 293.66, 349.23 } };
    const double bass[4] = { 65.41, 51.91, 77.78, 58.27 };
    for (size_t i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const int bar = (int) (t / (beat * 4)) % 4;
        float pad = 0.f;
        for (int c = 0; c < 3; ++c) pad += (float) (0.06 * std::sin (2 * kPi * chords[bar][c] * t) + 0.02 * std::sin (4 * kPi * chords[bar][c] * t));
        const float b = (float) (0.3 * std::sin (2 * kPi * bass[bar] * t));
        L[i] += pad * 1.0f + b; R[i] += pad * 0.7f + b;
    }

    // ---- BPM / escala
    std::vector<float> mono (n);
    for (size_t i = 0; i < n; ++i) mono[i] = 0.5f * (L[i] + R[i]);
    auto t0 = std::chrono::steady_clock::now();
    const double detected = detectBpm (mono, sr);
    KeyResult k = detectKey (mono, sr);
    auto t1 = std::chrono::steady_clock::now();
    std::printf ("BPM detectado: %.1f (real %.1f)\n", detected, bpm);
    std::printf ("Escala: %s | modo: %s %s | notas: %s | corr %.2f\n", keyTitle (k).c_str(), modeName (k).c_str(),
                modeSubtitle (k).c_str(), scaleNotes (k).c_str(), k.confidence);
    std::printf ("Tiempo analisis: %.2f s\n", std::chrono::duration<double> (t1 - t0).count());

    // ---- Separacion
    std::vector<std::vector<float>> oL ((size_t) kNumStems), oR ((size_t) kNumStems);
    t0 = std::chrono::steady_clock::now();
    float lastP = 0;
    bool ok = separateStems (L.data(), R.data(), n, sr,
        [&] (int s, const float* l, const float* r, int cnt) { oL[(size_t) s].insert (oL[(size_t) s].end(), l, l + cnt); oR[(size_t) s].insert (oR[(size_t) s].end(), r, r + cnt); },
        [&] (float p) { lastP = p; return true; });
    t1 = std::chrono::steady_clock::now();
    std::printf ("Separacion: %s en %.2f s (progreso final %.2f) para %.0f s de audio\n", ok ? "OK" : "FALLO", std::chrono::duration<double> (t1 - t0).count(), lastP, dur);

    static const char* names[] = { "Kick", "Bass", "Hi-hat", "Percusion", "Guitarras", "Pianos", "Strings", "Flautas", "Pads", "Voces", "FX/Otros" };
    double err = 0, ref = 0;
    for (int s = 0; s < kNumStems; ++s)
    {
        double e = 0;
        if (oL[(size_t) s].size() != n) std::printf ("Longitud incorrecta en stem %d: %zu\n", s, oL[(size_t) s].size());
        for (size_t i = 0; i < std::min (n, oL[(size_t) s].size()); ++i) e += (double) oL[(size_t) s][i] * oL[(size_t) s][i];
        std::printf ("  %-10s RMS %.4f\n", names[s], std::sqrt (e / n));
    }
    for (size_t i = 0; i < n; ++i)
    {
        double sl = 0;
        for (int s = 0; s < kNumStems; ++s) sl += oL[(size_t) s][i];
        err += (sl - L[i]) * (sl - L[i]); ref += (double) L[i] * L[i];
    }
    std::printf ("Error de reconstruccion (suma de stems vs original): %.2f dB\n", 10 * std::log10 (err / ref + 1e-30));
    return 0;
}
