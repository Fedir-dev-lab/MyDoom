#include "Framebuffer.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

Framebuffer::Framebuffer(int width, int height)
    : m_width(width), m_height(height),
      m_pixels(static_cast<size_t>(width) * height, 0),
      m_depthBuffer(static_cast<size_t>(width) * height, std::numeric_limits<float>::infinity()) {
}

uint32_t Framebuffer::MakeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (static_cast<uint32_t>(r) << 24) |
           (static_cast<uint32_t>(g) << 16) |
           (static_cast<uint32_t>(b) << 8) |
           static_cast<uint32_t>(a);
}

void Framebuffer::Clear(uint32_t color) {
    std::fill(m_pixels.begin(), m_pixels.end(), color);
}

void Framebuffer::ClearDepth() {
    std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), std::numeric_limits<float>::infinity());
}

void Framebuffer::CopyPixels(const uint32_t* pixels, size_t count) {
    const size_t pixelCount = std::min(count, m_pixels.size());
    std::copy_n(pixels, pixelCount, m_pixels.begin());
}

void Framebuffer::SetPixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) {
        return;
    }
    m_pixels[static_cast<size_t>(y) * m_width + x] = color;
}

void Framebuffer::DrawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        SetPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Framebuffer::DrawRect(int x, int y, int w, int h, uint32_t color, bool filled) {
    if (filled) {
        for (int row = y; row < y + h; ++row) {
            for (int col = x; col < x + w; ++col) {
                SetPixel(col, row, color);
            }
        }
        return;
    }

    DrawLine(x, y, x + w - 1, y, color);
    DrawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    DrawLine(x, y, x, y + h - 1, color);
    DrawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void Framebuffer::BlendPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height || alpha <= 0.0f) { return; }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const size_t index = static_cast<size_t>(y) * m_width + x;
    const uint32_t pixel = m_pixels[index];
    const int pr = (pixel >> 24) & 0xFF;
    const int pg = (pixel >> 16) & 0xFF;
    const int pb = (pixel >> 8)  & 0xFF;
    const int a = static_cast<int>(alpha * 256.0f);
    const int invA = 256 - a;
    const int nr = (pr * invA + r * a) >> 8;
    const int ng = (pg * invA + g * a) >> 8;
    const int nb = (pb * invA + b * a) >> 8;
    m_pixels[index] = MakeColor(static_cast<uint8_t>(nr),
                                static_cast<uint8_t>(ng),
                                static_cast<uint8_t>(nb));
}

void Framebuffer::DrawCircle(int cx, int cy, int radius, uint32_t color, bool filled) {
    if (radius <= 0) { return; }
    if (filled) {
        const int r2 = radius * radius;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy <= r2) {
                    SetPixel(cx + dx, cy + dy, color);
                }
            }
        }
    } else {
        int x = radius, y = 0;
        int err = 0;
        while (x >= y) {
            SetPixel(cx + x, cy + y, color);
            SetPixel(cx + y, cy + x, color);
            SetPixel(cx - y, cy + x, color);
            SetPixel(cx - x, cy + y, color);
            SetPixel(cx - x, cy - y, color);
            SetPixel(cx - y, cy - x, color);
            SetPixel(cx + y, cy - x, color);
            SetPixel(cx + x, cy - y, color);
            y += 1;
            err += 1 + 2 * y;
            if (2 * (err - x) + 1 > 0) {
                x -= 1;
                err += 1 - 2 * x;
            }
        }
    }
}

void Framebuffer::DrawBlendCircle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, float alpha, bool filled) {
    if (radius <= 0 || alpha <= 0.0f) { return; }
    if (filled) {
        const int r2 = radius * radius;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy <= r2) {
                    BlendPixel(cx + dx, cy + dy, r, g, b, alpha);
                }
            }
        }
    } else {
        const int rOuter2 = radius * radius;
        const int rInner2 = std::max(0, (radius - 3) * (radius - 3));
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int dist2 = dx * dx + dy * dy;
                if (dist2 <= rOuter2 && dist2 >= rInner2) {
                    BlendPixel(cx + dx, cy + dy, r, g, b, alpha);
                }
            }
        }
    }
}

void Framebuffer::ApplyTint(uint8_t tr, uint8_t tg, uint8_t tb, float alpha) {
    if (alpha <= 0.0f) { return; }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const int a = static_cast<int>(alpha * 256.0f);
    const int invA = 256 - a;
    for (uint32_t& pixel : m_pixels) {
        const int r = (pixel >> 24) & 0xFF;
        const int g = (pixel >> 16) & 0xFF;
        const int b = (pixel >> 8)  & 0xFF;
        const int nr = (r * invA + tr * a) >> 8;
        const int ng = (g * invA + tg * a) >> 8;
        const int nb = (b * invA + tb * a) >> 8;
        pixel = MakeColor(static_cast<uint8_t>(nr),
                          static_cast<uint8_t>(ng),
                          static_cast<uint8_t>(nb));
    }
}
