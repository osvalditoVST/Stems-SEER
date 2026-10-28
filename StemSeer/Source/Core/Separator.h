#pragma once
#include "Analysis.h"
#include <cstddef>

namespace stemseer {

// Orden de los stems (coincide con la interfaz)
//  Drums:    0 Kick, 1 Bass, 2 Hi-hat, 3 Percusion
//  Melodias: 4 Guitarras, 5 Pianos, 6 Strings, 7 Flautas, 8 Pads
//  Extras:   9 Voces, 10 FX / Otros
constexpr int kNumStems = 11;

// Recibe los stems por trozos, en orden y de forma continua (streaming), para no llenar la RAM.
using StemSink = std::function<void (int stem, const float* left, const float* right, int numSamples)>;

// Separacion espectral con mascaras suaves (HPSS + bandas + estereo).
// Las 11 mascaras suman 1 en cada bin: la suma de todos los stems reconstruye la mezcla original.
// Devuelve false si se cancelo desde 'progress'.
bool separateStems (const float* left, const float* right, size_t numSamples, double sampleRate,
                    const StemSink& sink, const ProgressFn& progress = nullptr);

} // namespace stemseer
