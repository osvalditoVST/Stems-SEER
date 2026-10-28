#pragma once
#include <vector>
#include <string>
#include <functional>

namespace stemseer {

// Callback de progreso (0..1). Devuelve false para cancelar.
using ProgressFn = std::function<bool (float)>;

struct KeyResult
{
    bool  valid      = false;
    int   tonic      = 0;      // 0 = C, 1 = C#, ... 11 = B
    bool  minor      = true;
    int   mode       = 5;      // 0 Jonico, 1 Dorico, 2 Frigio, 3 Lidio, 4 Mixolidio, 5 Eolico
    float confidence = 0.0f;   // correlacion (0..1) con el perfil elegido
};

// BPM (0.0 si no se pudo detectar). Entrada: senal mono.
double    detectBpm (const std::vector<float>& mono, double sampleRate, const ProgressFn& progress = nullptr);
// Tonalidad / escala.
KeyResult detectKey (const std::vector<float>& mono, double sampleRate, const ProgressFn& progress = nullptr);

// Textos (UTF-8)
std::string noteName     (int pitchClass, bool flats);
bool        usesFlats    (const KeyResult& k);
std::string keyTitle     (const KeyResult& k);   // "C menor"
std::string modeName     (const KeyResult& k);   // "Eólico"
std::string modeSubtitle (const KeyResult& k);   // "(menor natural)"
std::string scaleNotes   (const KeyResult& k);   // "C - D - Eb - F - G - Ab - Bb"
bool        inScale      (const KeyResult& k, int pitchClass);

} // namespace stemseer
