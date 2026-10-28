#include "Separator.h"
#include "Fft.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace stemseer {

namespace {

constexpr int N   = 2048;          // tamano FFT
constexpr int HOP = 512;           // salto (75 % de solape)
constexpr int K   = N / 2 + 1;     // bins utiles

inline float sample (const float* x, size_t len, long long i) { return (i >= 0 && (size_t) i < len) ? x[i] : 0.0f; }
inline double sigmoid (double x) { return 1.0 / (1.0 + std::exp (-x)); }

// Banda suave en escala logaritmica de frecuencia
inline double bandWeight (double f, double lo, double hi)
{
    if (f < 1e-3) f = 1e-3;
    const double l = std::log2 (f);
    return sigmoid (4.0 * (l - std::log2 (lo))) * sigmoid (-4.0 * (l - std::log2 (hi)));
}

} // namespace

bool separateStems (const float* L, const float* R, size_t len, double sr,
                    const StemSink& sink, const ProgressFn& progress)
{
    if (len == 0 || sr <= 0.0) return false;

    const int T = (int) ((len + (size_t) (N - HOP) + (size_t) HOP - 1) / (size_t) HOP);
    const size_t TK = (size_t) T * (size_t) K;
    Fft fft (N);

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) std::sin (kPi * i / N);   // raiz de Hann (periodica)

    auto report = [&] (float p) { return ! progress || progress (p); };
    auto frameStart = [] (int t) -> long long { return (long long) t * HOP - (N - HOP); };

    std::vector<float> z ((size_t) (2 * N));

    // ---------------------------------------------------------------- Paso 1: espectrograma de magnitud (mono)
    std::vector<float> mag (TK);
    for (int t = 0; t < T; ++t)
    {
        const long long s0 = frameStart (t);
        for (int i = 0; i < N; ++i)
        {
            const float m = 0.5f * (sample (L, len, s0 + i) + sample (R, len, s0 + i));
            z[(size_t) (2 * i)] = m * win[(size_t) i];
            z[(size_t) (2 * i + 1)] = 0.0f;
        }
        fft.forward (z.data());
        float* row = &mag[(size_t) t * K];
        for (int k = 0; k < K; ++k)
        {
            const float re = z[(size_t) (2 * k)], im = z[(size_t) (2 * k + 1)];
            row[k] = std::sqrt (re * re + im * im);
        }
        if ((t & 127) == 0 && ! report (0.20f * (float) t / (float) T)) return false;
    }

    // ---------------------------------------------------------------- Paso 2: armonico = mediana en el tiempo
    constexpr int MW = 17, MH = MW / 2;
    std::vector<float> Hm (TK);
    {
        float tmp[MW];
        for (int t = 0; t < T; ++t)
        {
            for (int k = 0; k < K; ++k)
            {
                for (int j = 0; j < MW; ++j)
                {
                    const int tt = std::min (T - 1, std::max (0, t - MH + j));
                    tmp[j] = mag[(size_t) tt * K + (size_t) k];
                }
                std::nth_element (tmp, tmp + MH, tmp + MW);
                Hm[(size_t) t * K + (size_t) k] = tmp[MH];
            }
            if ((t & 127) == 0 && ! report (0.20f + 0.25f * (float) t / (float) T)) return false;
        }
    }

    // ---------------------------------------------------------------- Paso 3: mascaras armonico/percusivo y sostenido
    std::vector<uint8_t> mH8 (TK), mS8 (TK);
    {
        constexpr int LW = 20;                       // ventana de sostenido: 41 frames (~0.5 s)
        std::vector<float> sum ((size_t) K, 0.0f);
        for (int j = -LW; j <= LW; ++j)
        {
            const int tt = std::min (T - 1, std::max (0, j));
            for (int k = 0; k < K; ++k) sum[(size_t) k] += Hm[(size_t) tt * K + (size_t) k];
        }
        float tmp[MW];
        for (int t = 0; t < T; ++t)
        {
            const float* mrow = &mag[(size_t) t * K];
            for (int k = 0; k < K; ++k)
            {
                for (int j = 0; j < MW; ++j)
                {
                    const int kk = std::min (K - 1, std::max (0, k - MH + j));
                    tmp[j] = mrow[kk];
                }
                std::nth_element (tmp, tmp + MH, tmp + MW);
                const float P = tmp[MH];
                const float H = Hm[(size_t) t * K + (size_t) k];
                const float mh = (H * H) / (H * H + P * P + 1e-12f);

                const float Hl = sum[(size_t) k] / (float) (2 * LW + 1);
                const float d = std::fabs (H - Hl);
                const float ms = (Hl * Hl) / (Hl * Hl + d * d + 1e-12f);

                mH8[(size_t) t * K + (size_t) k] = (uint8_t) std::lround (mh * 255.0f);
                mS8[(size_t) t * K + (size_t) k] = (uint8_t) std::lround (ms * 255.0f);
            }
            // deslizar ventana
            const int add = std::min (T - 1, t + LW + 1), rem = std::min (T - 1, std::max (0, t - LW));
            for (int k = 0; k < K; ++k)
                sum[(size_t) k] += Hm[(size_t) add * K + (size_t) k] - Hm[(size_t) rem * K + (size_t) k];

            if ((t & 127) == 0 && ! report (0.45f + 0.15f * (float) t / (float) T)) return false;
        }
    }
    std::vector<float>().swap (mag);
    std::vector<float>().swap (Hm);

    // ---------------------------------------------------------------- Pesos por frecuencia
    std::vector<float> wL ((size_t) K), wHi ((size_t) K), wMid ((size_t) K);
    std::vector<float> bVoz ((size_t) K), bGui ((size_t) K), bPia ((size_t) K), bFlu ((size_t) K), bFx ((size_t) K);
    std::vector<float> bStr ((size_t) K), bPad ((size_t) K);
    for (int k = 0; k < K; ++k)
    {
        const double f = (double) k * sr / N;
        const double lo = 1.0 / (1.0 + std::pow (f / 220.0, 4.0));
        const double hi = std::pow (f / 6000.0, 4.0) / (1.0 + std::pow (f / 6000.0, 4.0));
        wL[(size_t) k] = (float) lo;
        wHi[(size_t) k] = (float) hi;
        wMid[(size_t) k] = (float) (1.0 - lo - hi);
        bVoz[(size_t) k] = (float) bandWeight (f, 200.0, 4500.0);
        bGui[(size_t) k] = (float) bandWeight (f, 150.0, 4000.0);
        bPia[(size_t) k] = (float) bandWeight (f, 60.0, 5000.0);
        bFlu[(size_t) k] = (float) bandWeight (f, 1200.0, 8000.0);
        bFx[(size_t) k]  = (float) bandWeight (f, 8000.0, 24000.0);
        bStr[(size_t) k] = (float) bandWeight (f, 300.0, 5000.0);
        bPad[(size_t) k] = (float) bandWeight (f, 80.0, 2500.0);
    }

    // ---------------------------------------------------------------- Paso 4: resintesis por stem (streaming)
    std::vector<float> accL ((size_t) kNumStems * N, 0.0f), accR ((size_t) kNumStems * N, 0.0f);
    std::vector<float> masks ((size_t) kNumStems * K);
    std::vector<float> y ((size_t) (2 * N));

    for (int t = 0; t < T; ++t)
    {
        const long long s0 = frameStart (t);
        for (int i = 0; i < N; ++i)
        {
            z[(size_t) (2 * i)]     = sample (L, len, s0 + i) * win[(size_t) i];
            z[(size_t) (2 * i + 1)] = sample (R, len, s0 + i) * win[(size_t) i];
        }
        fft.forward (z.data());          // z = FFT(L + iR)

        const uint8_t* rowH = &mH8[(size_t) t * K];
        const uint8_t* rowS = &mS8[(size_t) t * K];

        for (int k = 0; k < K; ++k)
        {
            const int m = (N - k) % N;
            const float zr = z[(size_t) (2 * k)], zi = z[(size_t) (2 * k + 1)];
            const float mr = z[(size_t) (2 * m)], mi = z[(size_t) (2 * m + 1)];
            const float lr = 0.5f * (zr + mr), li = 0.5f * (zi - mi);      // espectro L
            const float rr = 0.5f * (zi + mi), ri = -0.5f * (zr - mr);     // espectro R
            const float magL = std::sqrt (lr * lr + li * li), magR = std::sqrt (rr * rr + ri * ri);
            const float mx = std::max (magL, magR), mn = std::min (magL, magR);
            const float c = mx > 1e-9f ? mn / mx : 1.0f;                   // 1 = centrado, 0 = lateral

            const float mh = rowH[k] / 255.0f, mp = 1.0f - mh, ms = rowS[k] / 255.0f;
            const float R_ = mh * (1.0f - wL[(size_t) k]);                 // armonico sin graves
            const float S_ = R_ * ms, Rn = R_ * (1.0f - ms);               // sostenido / no sostenido

            // sostenido -> strings / pads
            const float sS = bStr[(size_t) k] * (0.4f + c), sP = bPad[(size_t) k] * (0.4f + (1.0f - c));
            const float sN = sS + sP + 1e-9f;
            // no sostenido -> voces / guitarras / pianos / flautas / fx
            const float c3 = c * c * c;
            const float vV = bVoz[(size_t) k] * c3;
            const float vG = bGui[(size_t) k] * (0.25f + (1.0f - c));
            const float vP = bPia[(size_t) k] * (0.25f + c) * 0.7f;
            const float vF = bFlu[(size_t) k] * 0.5f;
            const float vX = 0.03f + bFx[(size_t) k];
            const float vN = vV + vG + vP + vF + vX;

            masks[0 * (size_t) K + (size_t) k]  = mp * wL[(size_t) k];             // Kick
            masks[1 * (size_t) K + (size_t) k]  = mh * wL[(size_t) k];             // Bass
            masks[2 * (size_t) K + (size_t) k]  = mp * wHi[(size_t) k];            // Hi-hat
            masks[3 * (size_t) K + (size_t) k]  = mp * wMid[(size_t) k];           // Percusion
            masks[4 * (size_t) K + (size_t) k]  = Rn * vG / vN;                    // Guitarras
            masks[5 * (size_t) K + (size_t) k]  = Rn * vP / vN;                    // Pianos
            masks[6 * (size_t) K + (size_t) k]  = S_ * sS / sN;                    // Strings
            masks[7 * (size_t) K + (size_t) k]  = Rn * vF / vN;                    // Flautas
            masks[8 * (size_t) K + (size_t) k]  = S_ * sP / sN;                    // Pads
            masks[9 * (size_t) K + (size_t) k]  = Rn * vV / vN;                    // Voces
            masks[10 * (size_t) K + (size_t) k] = Rn * vX / vN;                    // FX / Otros
        }

        for (int s = 0; s < kNumStems; ++s)
        {
            const float* ms = &masks[(size_t) s * K];
            for (int k = 0; k < N; ++k)
            {
                const float g = ms[k < K ? k : N - k];
                y[(size_t) (2 * k)]     = g * z[(size_t) (2 * k)];
                y[(size_t) (2 * k + 1)] = g * z[(size_t) (2 * k + 1)];
            }
            fft.inverse (y.data());
            float* aL = &accL[(size_t) s * N];
            float* aR = &accR[(size_t) s * N];
            for (int i = 0; i < N; ++i)
            {
                const float w = win[(size_t) i] * 0.5f;       // suma de Hann^2 con 75 % de solape = 2
                aL[i] += y[(size_t) (2 * i)] * w;
                aR[i] += y[(size_t) (2 * i + 1)] * w;
            }
        }

        // Emitir los HOP primeros muestras (ya completas) y desplazar
        const int iStart = s0 < 0 ? (int) std::min<long long> (-s0, HOP) : 0;
        const long long remaining = (long long) len - s0;
        const int iEnd = remaining <= 0 ? 0 : (int) std::min<long long> (HOP, remaining);
        for (int s = 0; s < kNumStems; ++s)
        {
            float* aL = &accL[(size_t) s * N];
            float* aR = &accR[(size_t) s * N];
            if (iEnd > iStart) sink (s, aL + iStart, aR + iStart, iEnd - iStart);
            std::memmove (aL, aL + HOP, sizeof (float) * (size_t) (N - HOP));
            std::memmove (aR, aR + HOP, sizeof (float) * (size_t) (N - HOP));
            std::memset (aL + (N - HOP), 0, sizeof (float) * HOP);
            std::memset (aR + (N - HOP), 0, sizeof (float) * HOP);
        }

        if ((t & 63) == 0 && ! report (0.60f + 0.40f * (float) t / (float) T)) return false;
    }

    report (1.0f);
    return true;
}

} // namespace stemseer
