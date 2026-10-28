#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/Separator.h"

// Texto UTF-8 -> juce::String
#define T8(x) juce::String::fromUTF8 (x)

struct StemDef
{
    const char*  name;        // nombre mostrado
    const char*  fileLabel;   // nombre para archivos exportados
    int          group;       // 0 Drums, 1 Melodias, 2 Extras
    juce::uint32 colour;
};

inline const StemDef kStemDefs[stemseer::kNumStems] =
{
    { "Kick",       "Kick",       0, 0xffff3b57 },
    { "Bass",       "Bass",       0, 0xfff59e25 },
    { "Hi-hat",     "Hi-hat",     0, 0xfff5e03a },
    { "Percusión",  "Percusion",  0, 0xffa855f7 },
    { "Guitarras",  "Guitarras",  1, 0xff14b8a6 },
    { "Pianos",     "Pianos",     1, 0xff3b9cff },
    { "Strings",    "Strings",    1, 0xff8b7bff },
    { "Flautas",    "Flautas",    1, 0xffe84fa3 },
    { "Pads",       "Pads",       1, 0xffee4bd8 },
    { "Voces",      "Voces",      2, 0xff5b8cd6 },
    { "FX / Otros", "FX-Otros",   2, 0xff8a94a6 }
};
