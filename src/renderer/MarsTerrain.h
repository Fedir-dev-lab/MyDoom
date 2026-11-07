#pragma once

#include <cstdint>

class MarsTerrain {
public:
    // Builds height and colour maps once. It is safe to call repeatedly.
    static void Initialize();

    // lod: 0 = one-metre samples, 1 = two-metre samples, 2 = four-metre
    // samples. The renderer selects it by view distance; gameplay uses lod 0.
    static float HeightAt(float x, float z, int lod = 0);
    static uint32_t ColorAt(float x, float z);
};
