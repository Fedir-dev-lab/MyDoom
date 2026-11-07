#include "SpriteRenderer.h"

#include <algorithm>

#include "Framebuffer.h"
#include "Lighting.h"
#include "Texture.h"

namespace {
constexpr float kNearPlane = 0.05f;
}  // namespace

SpriteRenderer::SpriteRenderer(int screenWidth, int screenHeight,
                               float fieldOfViewRadians)
    : m_screenWidth(screenWidth),
      m_screenHeight(screenHeight) {
    (void)fieldOfViewRadians;  // projection now comes from the Camera
}

void SpriteRenderer::DrawSprite(Framebuffer& framebuffer, const Camera& camera,
                                const Vector3& position, float width, float height,
                                const std::vector<PointLight>& lights, float ambientLight,
                                const DirectionalLight& sunLight,
                                const Texture& texture, bool highlight) const {
    // Billboard centre (mid-height) projected through the true 3D camera.
    const Vector3 centre{position.x, position.y + height * 0.5f, position.z};
    const Camera::View v = camera.ToView(centre);
    if (v.z < kNearPlane) { return; }

    const float invZ = 1.0f / v.z;
    const float focalOverZ = camera.focalLength * invZ;
    const float cx = camera.halfWidth + v.x * focalOverZ;
    const float cy = camera.halfHeight - v.y * focalOverZ;

    // Screen-aligned rect sized by the true-pitch depth.
    const float halfW = width * focalOverZ * 0.5f;
    const float halfH = height * focalOverZ * 0.5f;
    const float left   = cx - halfW;
    const float right  = cx + halfW;
    const float top    = cy - halfH;
    const float bottom = cy + halfH;

    const int firstCol = std::max(0,                 static_cast<int>(std::ceil(left)));
    const int lastCol  = std::min(m_screenWidth - 1, static_cast<int>(std::floor(right)));
    const int firstRow = std::max(0,                 static_cast<int>(std::ceil(top)));
    const int lastRow  = std::min(m_screenHeight - 1, static_cast<int>(std::floor(bottom)));
    if (firstCol > lastCol || firstRow > lastRow) { return; }

    const float lit = ComputeLighting(position.x, position.y + height * 0.5f, position.z,
                                      lights, ambientLight, sunLight);

    const float invW = 1.0f / (right - left);
    const float invH = 1.0f / (bottom - top);
    const float u0 = (static_cast<float>(firstCol) - left) * invW;
    const float dU = invW;

    for (int y = firstRow; y <= lastRow; ++y) {
        const float vCoord = (static_cast<float>(y) - top) * invH;
        const size_t rowBase = static_cast<size_t>(y) * m_screenWidth;
        float u = u0;
        for (int x = firstCol; x <= lastCol; ++x, u += dU) {
            const uint32_t color = texture.Sample(u, vCoord);
            if ((color & 0xFFu) == 0) { continue; }  // transparent
            const uint32_t finalColor = highlight ? Framebuffer::MakeColor(255, 220, 220)
                                                  : ApplyLight(color, lit);
            framebuffer.SetPixelDepthFast(rowBase + x, finalColor, v.z);
        }
    }
}
