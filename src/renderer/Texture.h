#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Texture {
public:
    Texture(int width, int height, std::vector<uint32_t> pixels);

    int Width() const { return m_width; }
    int Height() const { return m_height; }

    // Samples repeating UV coordinates: (0, 0) and (1, 1) are the same texel.
    uint32_t Sample(float u, float v) const {
        if (m_isPowerOfTwo) {
            const int x = static_cast<int>((u + 4096.0f) * m_fWidth) & m_maskX;
            const int y = static_cast<int>((v + 4096.0f) * m_fHeight) & m_maskY;
            return m_pixels[static_cast<size_t>(y) * m_width + x];
        }
        const float wrappedU = u - std::floor(u);
        const float wrappedV = v - std::floor(v);
        const int x = static_cast<int>(wrappedU * m_fWidth);
        const int y = static_cast<int>(wrappedV * m_fHeight);
        return m_pixels[static_cast<size_t>(y) * m_width + x];
    }

    // Resolves horizontal texture coordinate once per vertical column.
    int GetTexX(float u) const {
        if (m_isPowerOfTwo) {
            return static_cast<int>((u + 4096.0f) * m_fWidth) & m_maskX;
        }
        const float wrappedU = u - std::floor(u);
        return static_cast<int>(wrappedU * m_fWidth);
    }

    // Samples a pre-resolved column at vertical coordinate v.
    uint32_t SampleColumn(int texX, float v) const {
        int y = 0;
        if (m_isPowerOfTwo) {
            y = static_cast<int>((v + 4096.0f) * m_fHeight) & m_maskY;
        } else {
            const float wrappedV = v - std::floor(v);
            y = static_cast<int>(wrappedV * m_fHeight);
        }
        return m_pixels[static_cast<size_t>(y) * m_width + texX];
    }

    const uint32_t* Pixels() const { return m_pixels.data(); }

    static Texture CreateCheckerboard(int width, int height, int cellSize,
                                      uint32_t firstColor, uint32_t secondColor);

    // Loads a portable pixmap (PPM P3 or P6) and converts it to RGBA8888.
    // Returns std::nullopt when the file cannot be read or is malformed.
    static std::optional<Texture> LoadPpm(const std::string& path);
    static Texture CreateLamp(int width, int height, uint32_t poleColor,
                              uint32_t lightColor);
    static Texture CreateCeilingLamp(int width, int height, uint32_t bodyColor,
                                     uint32_t glowColor);
    static Texture CreateBarrel(int width, int height, uint32_t bodyColor,
                                uint32_t bandColor);
    static Texture CreateColumn(int width, int height, uint32_t stoneColor,
                                uint32_t mortarColor);
    static Texture CreateBookshelf(int width, int height);
    static Texture CreateEnemy(int width, int height, uint32_t bodyColor,
                               uint32_t eyeColor);
    static Texture CreateEnemyShooter(int width, int height, uint32_t robeColor,
                                      uint32_t visorColor);
    static Texture CreateEnemyTank(int width, int height, uint32_t armorColor,
                                   uint32_t eyeColor);
    static Texture CreateEnemyCorpse(int width, int height, uint32_t bodyColor,
                                     uint32_t bloodColor);
    static Texture CreateProjectile(int width, int height, uint32_t coreColor,
                                    uint32_t glowColor);
    static Texture CreateMedkit(int width, int height, uint32_t caseColor,
                                uint32_t crossColor);
    static Texture CreateAmmoBox(int width, int height, uint32_t caseColor,
                                 uint32_t stripeColor);
    static Texture CreateExitGate(int width, int height, uint32_t frameColor,
                                  uint32_t glowColor);
    // Key sprite: bow (circle) at top, blade with notches at bottom.
    // colour is an RGBA8888 packed value that tints the whole sprite.
    static Texture CreateKey(int width, int height, uint32_t colour);

private:
    int m_width;
    int m_height;
    float m_fWidth = 0.0f;
    float m_fHeight = 0.0f;
    int m_maskX = 0;
    int m_maskY = 0;
    bool m_isPowerOfTwo = false;
    std::vector<uint32_t> m_pixels;
};
