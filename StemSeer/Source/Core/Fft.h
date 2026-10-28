#pragma once
// FFT radix-2 minimalista (sin dependencias). Datos complejos intercalados: re, im, re, im...
#include <vector>
#include <cmath>
#include <utility>

namespace stemseer {

constexpr double kPi = 3.14159265358979323846;

class Fft
{
public:
    explicit Fft (int n) : n_ (n)
    {
        int lg = 0;
        while ((1 << lg) < n) ++lg;
        rev_.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < lg; ++b)
                if (i & (1 << b)) r |= 1 << (lg - 1 - b);
            rev_[(size_t) i] = r;
        }
        cos_.resize ((size_t) n / 2);
        sin_.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * kPi * i / n;
            cos_[(size_t) i] = (float) std::cos (a);
            sin_[(size_t) i] = (float) std::sin (a);
        }
    }

    int size() const { return n_; }
    void forward (float* d) const { transform (d, false); }
    void inverse (float* d) const { transform (d, true); }   // incluye escala 1/N

private:
    void transform (float* d, bool inv) const
    {
        for (int i = 0; i < n_; ++i)
        {
            const int j = rev_[(size_t) i];
            if (j > i)
            {
                std::swap (d[2 * i], d[2 * j]);
                std::swap (d[2 * i + 1], d[2 * j + 1]);
            }
        }
        for (int len = 2; len <= n_; len <<= 1)
        {
            const int half = len >> 1, step = n_ / len;
            for (int i = 0; i < n_; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cos_[(size_t) (k * step)];
                    const float wi = inv ? -sin_[(size_t) (k * step)] : sin_[(size_t) (k * step)];
                    const int a = 2 * (i + k), b = 2 * (i + k + half);
                    const float tr = d[b] * wr - d[b + 1] * wi;
                    const float ti = d[b] * wi + d[b + 1] * wr;
                    d[b]     = d[a]     - tr;
                    d[b + 1] = d[a + 1] - ti;
                    d[a]     += tr;
                    d[a + 1] += ti;
                }
            }
        }
        if (inv)
        {
            const float s = 1.0f / (float) n_;
            for (int i = 0; i < 2 * n_; ++i) d[i] *= s;
        }
    }

    int n_;
    std::vector<int> rev_;
    std::vector<float> cos_, sin_;
};

} // namespace stemseer
