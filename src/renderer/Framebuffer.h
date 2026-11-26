// Step 3: our own software framebuffer.
//
// A plain array of packed 32-bit pixels that we fill by hand
// (SetPixel/DrawLine/DrawRect/...), then upload to a GPU texture once per
// frame. Everything the renderer draws later (walls, sectors, sprites)
// goes through this class instead of calling SDL's own draw functions.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

class Framebuffer {
public:
    Framebuffer(int width, int height);

    int Width() const { return m_width; }
    int Height() const { return m_height; }

    // Raw pixel data, ready to hand to SDL_UpdateTexture(). Format matches
    // SDL_PIXELFORMAT_RGBA8888: each pixel is (R<<24)|(G<<16)|(B<<8)|A.
    const uint32_t* Data() const { return m_pixels.data(); }

    static uint32_t MakeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    void Clear(uint32_t color);
    void ClearDepth();
    void CopyPixels(const uint32_t* pixels, size_t count);
    void SetPixel(int x, int y, uint32_t color);
    // Safe version — used by HUD/debug code where coordinates may be arbitrary.
    void SetPixelDepth(int x, int y, uint32_t color, float depth) {
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) {
            return;
        }
        SetPixelDepthFast(static_cast<size_t>(y) * m_width + x, color, depth);
    }

    // Fast version — NO bounds check. Use only inside renderer inner loops
    // where x and y are already clamped to [0, width-1] / [0, height-1].
    // Accepts a precomputed index = y * width + x to eliminate the multiply.
    void SetPixelDepthFast(size_t index, uint32_t color, float depth) {
        if (depth < m_depthBuffer[index]) {
            m_depthBuffer[index] = depth;
            m_pixels[index] = color;
        }
    }

    void SetPixelFast(size_t index, uint32_t color) {
        m_pixels[index] = color;
    }

    uint32_t* Pixels() { return m_pixels.data(); }
    float* DepthBuffer() { return m_depthBuffer.data(); }

    void DrawLine(int x0, int y0, int x1, int y1, uint32_t color);
    void DrawRect(int x, int y, int w, int h, uint32_t color, bool filled = false);
    void DrawCircle(int cx, int cy, int radius, uint32_t color, bool filled = false);
    void DrawBlendCircle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, float alpha, bool filled = false);
    void BlendPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, float alpha);
    // Linearly blends a solid tint colour over every pixel in the color buffer.
    void ApplyTint(uint8_t r, uint8_t g, uint8_t b, float alpha);

private:
    int m_width;
    int m_height;
    std::vector<uint32_t> m_pixels;
    std::vector<float> m_depthBuffer;
};
