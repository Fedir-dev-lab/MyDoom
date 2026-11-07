#include "SpriteRenderer.h"

#include <algorithm>

#include "Framebuffer.h"
#include "Lighting.h"
#include "Texture.h"

namespace {
constexpr float kNearPlane = 0.05f;
} // namespace

SpriteRenderer::SpriteRenderer(int screenWidth, int screenHeight, float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight),
      m_focalLength((static_cast<float>(screenWidth) * 0.5f) /
                    std::tan(fieldOfViewRadians * 0.5f)),
      m_halfWidth(static_cast<float>(screenWidth) * 0.5f) {
}

void SpriteRenderer::DrawSprite(Framebuffer& framebuffer, const Vector3& cameraPosition,
                                float cosAngle, float sinAngle,
                                const Vector3& position,
                                float width, float height,
                                float horizonY,
                                const std::vector<PointLight>& lights, float ambientLight,
                                const DirectionalLight& sunLight,
                                const Texture& texture,
                                bool highlight) const {
    const float relX = position.x - cameraPosition.x;
    const float relZ = position.z - cameraPosition.z;
    // No need to compute viewY — sprites are always upright billboards.
    const float viewX = relX * cosAngle - relZ * sinAngle;
    const float viewZ = relX * sinAngle + relZ * cosAngle;

    if (viewZ < kNearPlane) {
        return;
    }

    const float invViewZ     = 1.0f / viewZ;
    const float focalOverZ   = m_focalLength * invViewZ;
    const float centerX      = m_halfWidth + viewX * focalOverZ;
    const float halfScrWidth = width * focalOverZ * 0.5f;
    const float left         = centerX - halfScrWidth;
    const float right        = centerX + halfScrWidth;
    const float top          = horizonY - (position.y + height - cameraPosition.y) * focalOverZ;
    const float bottom       = horizonY - (position.y          - cameraPosition.y) * focalOverZ;

    const int firstColumn = std::max(0,               static_cast<int>(std::ceil(left)));
    const int lastColumn  = std::min(m_screenWidth  - 1, static_cast<int>(std::floor(right)));
    const int firstRow    = std::max(0,               static_cast<int>(std::ceil(top)));
    const int lastRow     = std::min(m_screenHeight - 1, static_cast<int>(std::floor(bottom)));

    if (firstColumn > lastColumn || firstRow > lastRow) { return; }

    // Lighting is sampled once at sprite centre — cheap and good enough for
    // small billboards.
    const float lit = ComputeLighting(position.x, position.y + height * 0.5f, position.z,
                                      lights, ambientLight, sunLight);

    // Reciprocals precomputed so the inner loops use multiply instead of divide.
    const float invSpriteWidth  = 1.0f / (right  - left);
    const float invSpriteHeight = 1.0f / (bottom - top);
    const float u0 = (static_cast<float>(firstColumn) - left) * invSpriteWidth;
    const float dU = invSpriteWidth;

    for (int y = firstRow; y <= lastRow; ++y) {
        const float v        = (static_cast<float>(y) - top) * invSpriteHeight;
        const size_t rowBase = static_cast<size_t>(y) * m_screenWidth;
        float u = u0;

        for (int x = firstColumn; x <= lastColumn; ++x, u += dU) {
            const uint32_t color = texture.Sample(u, v);
            if ((color & 0xFFu) == 0) { continue; }
            const uint32_t finalColor = highlight ? Framebuffer::MakeColor(255, 220, 220)
                                                  : ApplyLight(color, lit);
            framebuffer.SetPixelDepthFast(rowBase + x, finalColor, viewZ);
        }
    }
}
