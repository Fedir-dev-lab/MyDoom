#include "Texture.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>

#include "Framebuffer.h"

namespace {

bool ReadPpmToken(std::istream& input, std::string& token) {
    token.clear();
    char character = '\0';
    while (input.get(character)) {
        if (character == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else if (!std::isspace(static_cast<unsigned char>(character))) {
            token += character;
            break;
        }
    }
    if (token.empty()) { return false; }

    while (input.get(character)) {
        if (std::isspace(static_cast<unsigned char>(character))) { break; }
        token += character;
    }
    return true;
}

bool ParseInt(const std::string& token, int& value) {
    try {
        size_t parsed = 0;
        value = std::stoi(token, &parsed);
        return parsed == token.size();
    } catch (...) {
        return false;
    }
}

uint8_t ScalePpmChannel(int channel, int maximum) {
    return static_cast<uint8_t>(channel * 255 / maximum);
}

} // namespace
Texture::Texture(int width, int height, std::vector<uint32_t> pixels)
    : m_width(width),
      m_height(height),
      m_fWidth(static_cast<float>(width)),
      m_fHeight(static_cast<float>(height)),
      m_maskX(width - 1),
      m_maskY(height - 1),
      m_isPowerOfTwo(width > 0 && (width & (width - 1)) == 0 &&
                     height > 0 && (height & (height - 1)) == 0),
      m_pixels(std::move(pixels)) {
}

Texture Texture::CreateCheckerboard(int width, int height, int cellSize,
                                    uint32_t firstColor, uint32_t secondColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool isFirstColor = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            pixels[static_cast<size_t>(y) * width + x] =
                isFirstColor ? firstColor : secondColor;
        }
    }

    return Texture(width, height, std::move(pixels));
}

std::optional<Texture> Texture::LoadPpm(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { return std::nullopt; }

    std::string token;
    if (!ReadPpmToken(input, token) || (token != "P3" && token != "P6")) {
        return std::nullopt;
    }
    const bool binary = token == "P6";

    int width = 0;
    int height = 0;
    int maximum = 0;
    if (!ReadPpmToken(input, token) || !ParseInt(token, width) || width <= 0 ||
        !ReadPpmToken(input, token) || !ParseInt(token, height) || height <= 0 ||
        !ReadPpmToken(input, token) || !ParseInt(token, maximum) ||
        maximum <= 0 || maximum > 255) {
        return std::nullopt;
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixelCount > std::numeric_limits<size_t>::max() / 3) {
        return std::nullopt;
    }

    std::vector<uint32_t> pixels;
    pixels.reserve(pixelCount);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        int red = 0;
        int green = 0;
        int blue = 0;
        if (binary) {
            red = input.get();
            green = input.get();
            blue = input.get();
            if (red == EOF || green == EOF || blue == EOF) { return std::nullopt; }
        } else {
            if (!ReadPpmToken(input, token) || !ParseInt(token, red) ||
                !ReadPpmToken(input, token) || !ParseInt(token, green) ||
                !ReadPpmToken(input, token) || !ParseInt(token, blue)) {
                return std::nullopt;
            }
        }
        if (red < 0 || red > maximum || green < 0 || green > maximum ||
            blue < 0 || blue > maximum) {
            return std::nullopt;
        }
        pixels.push_back(Framebuffer::MakeColor(
            ScalePpmChannel(red, maximum),
            ScalePpmChannel(green, maximum),
            ScalePpmChannel(blue, maximum)));
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateLamp(int width, int height, uint32_t poleColor, uint32_t lightColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int centerX = width / 2;
    const int lanternTop = height / 8;
    const int lanternBottom = height / 3;
    const int lanternHalfWidth = width / 3;
    const int poleHalfWidth = std::max(1, width / 10);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool isLantern = y >= lanternTop && y <= lanternBottom &&
                                   std::abs(x - centerX) <= lanternHalfWidth;
            const bool isPole = y > lanternBottom && std::abs(x - centerX) <= poleHalfWidth;
            if (isLantern) {
                pixels[static_cast<size_t>(y) * width + x] = lightColor;
            } else if (isPole) {
                pixels[static_cast<size_t>(y) * width + x] = poleColor;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}

// Mantis Scout sprite (scenario.md §4.1):
//   - Light-green chitin with yellow vein markings
//   - Elongated triangular head with two large compound eyes
//   - Thin foreleg-scythes flanking the body
//   - Grasshopper-bent hind legs at the bottom
Texture Texture::CreateEnemy(int width, int height, uint32_t bodyColor, uint32_t eyeColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);

    // Derive accent (vein) colour from eyeColor — use it for markings too.
    const uint32_t veinColor  = eyeColor;                 // yellow veins
    const uint32_t darkChitin = bodyColor & 0xC0C0C0FFu;  // shadowed chitin

    const int cx = width / 2;

    // --- Head: tall narrow triangle, top ~1/6 to 2/5 of height --------
    const int headTop    = height / 6;
    const int headBottom = height * 2 / 5;
    const int headMidY   = (headTop + headBottom) / 2;

    // --- Body: slim torso from 2/5 to 4/5 ------------------------------
    const int bodyTop    = headBottom;
    const int bodyBottom = height * 4 / 5;
    const int bodyHW     = std::max(2, width / 6); // half-width of torso

    // --- Scythe foreleg arms: diagonal lines from shoulder level -------
    const int armY  = bodyTop + (bodyBottom - bodyTop) / 4;
    const int armLen = width * 3 / 8;

    // --- Hind legs: bent chevrons at bottom ----------------------------
    const int legY = bodyBottom;

    auto setPixel = [&](int x, int y, uint32_t c) {
        if (x >= 0 && x < width && y >= 0 && y < height)
            pixels[static_cast<size_t>(y) * width + x] = c;
    };

    // Draw a thick line (2-px wide) between two points using DDA
    auto drawLine = [&](int x0, int y0, int x1, int y1, uint32_t c) {
        const int dx = x1 - x0, dy = y1 - y0;
        const int steps = std::max(std::abs(dx), std::abs(dy));
        if (steps == 0) { setPixel(x0, y0, c); return; }
        for (int i = 0; i <= steps; ++i) {
            const int px = x0 + dx * i / steps;
            const int py = y0 + dy * i / steps;
            setPixel(px,     py,     c);
            setPixel(px + 1, py,     c);
            setPixel(px,     py + 1, c);
        }
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int fromCX = x - cx;

            // -- Triangular head: width narrows from bottom to top ------
            if (y >= headTop && y < headBottom) {
                const float t = static_cast<float>(y - headTop) /
                                static_cast<float>(headBottom - headTop);
                // Half-width grows from 1 at top to width/4 at bottom
                const int hw = static_cast<int>(1.0f + t * (width / 4 - 1));
                if (std::abs(fromCX) <= hw) {
                    // Vein stripe down the centre
                    pixels[static_cast<size_t>(y) * width + x] =
                        (std::abs(fromCX) <= 1) ? veinColor : bodyColor;
                }
            }

            // -- Compound eyes: two large oval patches ------------------
            {
                const int eyeOffX = width / 5;
                const int eyeRX   = std::max(2, width / 7);
                const int eyeRY   = std::max(2, (headBottom - headTop) / 5);
                for (int side : {-1, 1}) {
                    const int ex = cx + side * eyeOffX;
                    const int ey = headMidY;
                    const int ddx = x - ex, ddy = y - ey;
                    if ((ddx * ddx * eyeRY * eyeRY + ddy * ddy * eyeRX * eyeRX) <=
                        eyeRX * eyeRX * eyeRY * eyeRY) {
                        pixels[static_cast<size_t>(y) * width + x] = eyeColor;
                    }
                }
            }

            // -- Slim torso ---------------------------------------------
            if (y >= bodyTop && y < bodyBottom && std::abs(fromCX) <= bodyHW) {
                // Alternating vein bands every ~5 pixels
                const bool veinBand = ((y - bodyTop) % 5) < 2 && std::abs(fromCX) <= 1;
                pixels[static_cast<size_t>(y) * width + x] =
                    veinBand ? veinColor : bodyColor;
            }
        }
    }

    // -- Scythe forelegs: two curved lines from shoulder ---------------
    // Left scythe: curves up-left then down
    drawLine(cx - bodyHW, armY,      cx - bodyHW - armLen,     armY - armLen / 2, darkChitin);
    drawLine(cx - bodyHW - armLen,   armY - armLen / 2,
             cx - bodyHW - armLen + armLen / 3, armY + armLen / 2, darkChitin);
    // Right scythe
    drawLine(cx + bodyHW, armY,      cx + bodyHW + armLen,     armY - armLen / 2, darkChitin);
    drawLine(cx + bodyHW + armLen,   armY - armLen / 2,
             cx + bodyHW + armLen - armLen / 3, armY + armLen / 2, darkChitin);

    // -- Bent hind legs (grasshopper-style) ----------------------------
    // Upper segment: outward diagonal
    drawLine(cx - bodyHW, legY,  cx - bodyHW - armLen / 2, legY - armLen / 3, darkChitin);
    drawLine(cx + bodyHW, legY,  cx + bodyHW + armLen / 2, legY - armLen / 3, darkChitin);
    // Lower segment: snaps back down
    drawLine(cx - bodyHW - armLen / 2, legY - armLen / 3,
             cx - bodyHW - armLen / 4, legY + armLen / 3, darkChitin);
    drawLine(cx + bodyHW + armLen / 2, legY - armLen / 3,
             cx + bodyHW + armLen / 4, legY + armLen / 3, darkChitin);

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateEnemyCorpse(int width, int height, uint32_t bodyColor, uint32_t bloodColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int centerY = height * 3 / 4;
    const int poolRadiusX = width * 2 / 5;
    const int poolRadiusY = std::max(2, height / 6);
    const int centerX = width / 2;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int dx = x - centerX;
            const int dy = y - centerY;
            const bool isPool = (dx * dx) * (poolRadiusY * poolRadiusY) +
                                (dy * dy) * (poolRadiusX * poolRadiusX) <=
                                (poolRadiusX * poolRadiusX * poolRadiusY * poolRadiusY);
            // Crumpled body lying down
            const bool isBody = y >= centerY - 4 && y <= centerY + 2 &&
                                x >= centerX - width / 3 && x <= centerX + width / 4;
            const bool isHead = (x - (centerX + width / 4)) * (x - (centerX + width / 4)) +
                                (y - centerY) * (y - centerY) <= 9;

            if (isHead || isBody) {
                pixels[static_cast<size_t>(y) * width + x] = bodyColor;
            } else if (isPool) {
                pixels[static_cast<size_t>(y) * width + x] = bloodColor;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateEnemyShooter(int width, int height, uint32_t robeColor, uint32_t visorColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int centerX = width / 2;
    const int headY = height / 4;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int dx = x - centerX;
            const int dy = y - headY;

            const bool isHood = (y < headY) && (y >= headY - height / 6) &&
                                (std::abs(dx) <= (y - (headY - height / 6)) * 2);
            const bool isHead = (dx * dx + dy * dy <= (width / 4) * (width / 4));
            const int robeWidth = (width / 4) + (y > headY ? (y - headY) * (width / 5) / (height - headY) : 0);
            const bool isRobe = (y >= headY && y < height - 2 && std::abs(dx) <= robeWidth);
            const bool isVisor = (y >= headY - 1 && y <= headY + 1 && std::abs(dx) <= width / 5);
            const bool isCrystal = (y >= height / 2 - 2 && y <= height / 2 + 3) &&
                                   (std::abs(x - (centerX + width / 3)) <= 2);

            if (isVisor || isCrystal) {
                pixels[static_cast<size_t>(y) * width + x] = visorColor;
            } else if (isHood || isHead || isRobe) {
                pixels[static_cast<size_t>(y) * width + x] = robeColor;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateEnemyTank(int width, int height, uint32_t armorColor, uint32_t eyeColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int centerX = width / 2;
    const int headY = height / 5;
    const int headRadius = width / 5;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int dx = x - centerX;
            const int dy = y - headY;

            const bool isHorn = (y < headY && y >= headY - height / 6) &&
                                (std::abs(std::abs(dx) - width / 4) <= 2);
            const bool isHead = (dx * dx + dy * dy <= headRadius * headRadius);
            const bool isShoulders = (y >= headY && y <= headY + height / 5 && std::abs(dx) <= width * 2 / 5);
            const bool isBody = (y > headY + height / 5 && y < height - 2 && std::abs(dx) <= width / 3);
            const bool isEye = (y >= headY - 1 && y <= headY + 1) &&
                               (std::abs(dx - width / 8) <= 1 || std::abs(dx + width / 8) <= 1);
            const bool isPlateTrim = (isBody || isShoulders) && (y % 6 == 0 || std::abs(dx) == width / 3);

            if (isEye) {
                pixels[static_cast<size_t>(y) * width + x] = eyeColor;
            } else if (isPlateTrim) {
                pixels[static_cast<size_t>(y) * width + x] = Framebuffer::MakeColor(38, 40, 44);
            } else if (isHead || isShoulders || isBody || isHorn) {
                pixels[static_cast<size_t>(y) * width + x] = armorColor;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateProjectile(int width, int height, uint32_t coreColor, uint32_t glowColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int centerX = width / 2;
    const int centerY = height / 2;
    const int coreRadius = std::max(1, width / 5);
    const int glowRadius = std::max(2, width * 2 / 5);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int dx = x - centerX;
            const int dy = y - centerY;
            const int distSq = dx * dx + dy * dy;

            if (distSq <= coreRadius * coreRadius) {
                pixels[static_cast<size_t>(y) * width + x] = coreColor;
            } else if (distSq <= glowRadius * glowRadius) {
                pixels[static_cast<size_t>(y) * width + x] = glowColor;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateMedkit(int width, int height, uint32_t caseColor, uint32_t crossColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int border = width / 8;
    const int centerX = width / 2;
    const int centerY = height / 2;
    const int crossHalfWidth = std::max(1, width / 10);

    for (int y = border; y < height - border; ++y) {
        for (int x = border; x < width - border; ++x) {
            const bool isCross = std::abs(x - centerX) <= crossHalfWidth ||
                                 std::abs(y - centerY) <= crossHalfWidth;
            pixels[static_cast<size_t>(y) * width + x] = isCross ? crossColor : caseColor;
        }
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateAmmoBox(int width, int height, uint32_t caseColor, uint32_t stripeColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int border = width / 8;
    const int stripeWidth = std::max(1, width / 8);

    for (int y = border; y < height - border; ++y) {
        for (int x = border; x < width - border; ++x) {
            const bool isStripe = (x / stripeWidth + y / stripeWidth) % 2 == 0;
            pixels[static_cast<size_t>(y) * width + x] = isStripe ? stripeColor : caseColor;
        }
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateExitGate(int width, int height, uint32_t frameColor,
                                uint32_t glowColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int border = std::max(1, width / 10);
    const int top = height / 8;
    const int bottom = height - height / 12;
    const int left = width / 8;
    const int right = width - left - 1;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const bool isFrame = x < left + border || x > right - border ||
                                 y < top + border || y > bottom - border;
            const bool isBeacon = x > width / 2 - border && x < width / 2 + border &&
                                  y > top + border * 2 && y < bottom - border * 2;
            if (isFrame) {
                pixels[static_cast<size_t>(y) * width + x] = frameColor;
            } else if (isBeacon) {
                pixels[static_cast<size_t>(y) * width + x] = glowColor;
            }
        }
    }
    return Texture(width, height, std::move(pixels));
}

// ---------------------------------------------------------------------------
// CeilingLamp — a hanging lamp viewed as a billboard.
// Top half: a flat disc body (shade). Bottom: a bright glowing bulb circle.
// Transparent background so it composites correctly over walls/ceiling.
// ---------------------------------------------------------------------------
Texture Texture::CreateCeilingLamp(int width, int height,
                                   uint32_t bodyColor, uint32_t glowColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int cx = width / 2;

    // Mounting cord — thin vertical line from top to shade top
    const int cordHalfW = std::max(1, width / 14);
    const int shadeTop  = height / 6;
    const int shadeBot  = height / 2;
    const int shadeHalfW = width * 2 / 5;

    for (int y = 0; y < shadeTop; ++y) {
        for (int x = cx - cordHalfW; x <= cx + cordHalfW; ++x) {
            if (x >= 0 && x < width)
                pixels[static_cast<size_t>(y) * width + x] = bodyColor;
        }
    }

    // Shade: trapezoid — wider at bottom than top
    for (int y = shadeTop; y <= shadeBot; ++y) {
        const float t = static_cast<float>(y - shadeTop) /
                        static_cast<float>(shadeBot - shadeTop + 1);
        const int hw = static_cast<int>(width / 6 + t * (shadeHalfW - width / 6));
        for (int x = cx - hw; x <= cx + hw; ++x) {
            if (x >= 0 && x < width)
                pixels[static_cast<size_t>(y) * width + x] = bodyColor;
        }
    }

    // Bulb: filled circle below shade, bright glow colour
    const int bulbCY = shadeBot + height / 7;
    const int bulbR  = height / 9;
    for (int y = bulbCY - bulbR; y <= bulbCY + bulbR; ++y) {
        for (int x = cx - bulbR; x <= cx + bulbR; ++x) {
            if (x < 0 || x >= width || y < 0 || y >= height) continue;
            const int dx = x - cx, dy = y - bulbCY;
            if (dx * dx + dy * dy <= bulbR * bulbR)
                pixels[static_cast<size_t>(y) * width + x] = glowColor;
        }
    }

    return Texture(width, height, std::move(pixels));
}

// ---------------------------------------------------------------------------
// Barrel — a classic wooden barrel sprite.
// Vertical body with darker horizontal bands (hoops).
// ---------------------------------------------------------------------------
Texture Texture::CreateBarrel(int width, int height,
                               uint32_t bodyColor, uint32_t bandColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int cx     = width  / 2;
    // Barrel outline is an ellipse-ish shape: narrower at top/bottom
    const int bodyHW = width  * 2 / 5;  // half-width at widest point (equator)
    const int topY   = height / 8;
    const int botY   = height - height / 8 - 1;

    for (int y = topY; y <= botY; ++y) {
        // Width of barrel at this row: full at middle, narrower near top/bottom
        const float fy = static_cast<float>(y - topY) /
                         static_cast<float>(botY - topY);
        // Sine curve gives barrel bulge
        const float bulge = std::sin(fy * 3.14159265f);
        const int hw = static_cast<int>((width / 5) + bulge * (bodyHW - width / 5));

        for (int x = cx - hw; x <= cx + hw; ++x) {
            if (x < 0 || x >= width) continue;
            pixels[static_cast<size_t>(y) * width + x] = bodyColor;
        }
    }

    // Top and bottom caps (flat ellipses)
    const int capR = width / 5;
    for (int dy = -capR / 2; dy <= capR / 2; ++dy) {
        for (int dx = -capR; dx <= capR; ++dx) {
            // Top cap
            const int ty = topY + dy, tx = cx + dx;
            if (tx >= 0 && tx < width && ty >= 0 && ty < height)
                pixels[static_cast<size_t>(ty) * width + tx] = bodyColor;
            // Bottom cap
            const int by2 = botY + dy, bx = cx + dx;
            if (bx >= 0 && bx < width && by2 >= 0 && by2 < height)
                pixels[static_cast<size_t>(by2) * width + bx] = bodyColor;
        }
    }

    // Horizontal bands (iron hoops) at 1/4, 1/2, 3/4 height
    const int bandH = std::max(1, height / 18);
    for (int band = 1; band <= 3; ++band) {
        const int bandY = topY + (botY - topY) * band / 4 - bandH / 2;
        for (int y = bandY; y < bandY + bandH && y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (pixels[static_cast<size_t>(y) * width + x] != 0)
                    pixels[static_cast<size_t>(y) * width + x] = bandColor;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}

// ---------------------------------------------------------------------------
// Column — a stone pillar. Rectangular body with bevel edges and mortar lines.
// ---------------------------------------------------------------------------
Texture Texture::CreateColumn(int width, int height,
                               uint32_t stoneColor, uint32_t mortarColor) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
    const int marginX = width / 6;   // side margins (bevelled edges)
    const int blockH  = height / 5;  // height of each stone block

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Outside column body → transparent
            if (x < marginX || x >= width - marginX) continue;

            // Mortar horizontal lines between blocks
            const bool isMortarH = (y % blockH == 0) || (y % blockH == blockH - 1);
            // Mortar vertical lines — staggered per row like real brickwork
            const int blockRow   = y / blockH;
            const int offset     = (blockRow % 2 == 0) ? 0 : width / 4;
            const int localX     = (x - marginX + offset) % (width / 2);
            const bool isMortarV = (localX == 0) || (localX == 1);

            // Edge shading: left edge slightly darker
            const bool isLeftEdge = (x == marginX || x == marginX + 1);

            if (isMortarH || isMortarV) {
                pixels[static_cast<size_t>(y) * width + x] = mortarColor;
            } else if (isLeftEdge) {
                // Darken stone by mixing with mortarColor
                const uint32_t s = stoneColor;
                const uint32_t m = mortarColor;
                pixels[static_cast<size_t>(y) * width + x] = (
                    (((s >> 24) / 2 + (m >> 24) / 2) << 24) |
                    ((((s >> 16) & 0xFF) / 2 + ((m >> 16) & 0xFF) / 2) << 16) |
                    ((((s >>  8) & 0xFF) / 2 + ((m >>  8) & 0xFF) / 2) <<  8) |
                    0xFF
                );
            } else {
                pixels[static_cast<size_t>(y) * width + x] = stoneColor;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}

// ---------------------------------------------------------------------------
// Bookshelf — rows of coloured book spines on dark shelves.
// ---------------------------------------------------------------------------
Texture Texture::CreateBookshelf(int width, int height) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);

    // Shelf colours — 4 distinct rows of books with varied spine colours
    // RGBA8888 format (alpha = 0xFF)
    const uint32_t shelfColor  = (0x28u << 24) | (0x18u << 16) | (0x10u << 8) | 0xFFu;
    const uint32_t bookColors[8] = {
        (0xC0u << 24) | (0x30u << 16) | (0x30u << 8) | 0xFFu, // red
        (0xD0u << 24) | (0x90u << 16) | (0x20u << 8) | 0xFFu, // orange
        (0x30u << 24) | (0x80u << 16) | (0xC0u << 8) | 0xFFu, // blue
        (0x30u << 24) | (0xA0u << 16) | (0x50u << 8) | 0xFFu, // green
        (0x90u << 24) | (0x40u << 16) | (0xB0u << 8) | 0xFFu, // purple
        (0xD0u << 24) | (0xC0u << 16) | (0x40u << 8) | 0xFFu, // yellow
        (0x50u << 24) | (0x50u << 16) | (0x50u << 8) | 0xFFu, // grey
        (0xB0u << 24) | (0x60u << 16) | (0x30u << 8) | 0xFFu, // brown
    };

    const int shelfThickness = std::max(2, height / 16);
    const int numShelves     = 4;
    const int shelfSpacing   = height / numShelves;
    const int bookW          = std::max(3, width / 7);   // width of each book spine
    const int bookMarginTop  = 2;

    for (int shelf = 0; shelf < numShelves; ++shelf) {
        const int shelfY    = shelf * shelfSpacing;
        const int booksTop  = shelfY + shelfThickness + bookMarginTop;
        const int booksBot  = shelfY + shelfSpacing - 1;

        // Shelf plank
        for (int y = shelfY; y < shelfY + shelfThickness && y < height; ++y) {
            for (int x = 0; x < width; ++x)
                pixels[static_cast<size_t>(y) * width + x] = shelfColor;
        }

        // Books
        int x = 0;
        int bookIndex = shelf * 3; // offset so each shelf looks different
        while (x < width) {
            const int thisBookW = bookW - 1 + (x % 3); // slight width variation
            const uint32_t color = bookColors[bookIndex % 8];
            ++bookIndex;

            for (int y = booksTop; y <= booksBot && y < height; ++y) {
                for (int bx = x; bx < x + thisBookW && bx < width; ++bx) {
                    // Thin dark line between books
                    if (bx == x)
                        pixels[static_cast<size_t>(y) * width + bx] = shelfColor;
                    else
                        pixels[static_cast<size_t>(y) * width + bx] = color;
                }
            }
            x += thisBookW + 1;
        }
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateKey(int width, int height, uint32_t colour) {
    // colour is RGBA8888: R<<24 | G<<16 | B<<8 | A
    // Transparent pixel = same RGB but alpha 0.
    const uint32_t transparent = colour & 0xFFFFFF00u;
    const uint32_t dark        = Framebuffer::MakeColor(
        static_cast<uint8_t>((colour >> 24) / 2),
        static_cast<uint8_t>(((colour >> 16) & 0xFF) / 2),
        static_cast<uint8_t>(((colour >>  8) & 0xFF) / 2));

    std::vector<uint32_t> pixels(static_cast<size_t>(width * height), transparent);

    // --- Bow: annulus in the top 55% of the sprite ---
    const float bowCX = width * 0.5f;
    const float bowCY = height * 0.30f;
    const float bowRO = width * 0.40f;  // outer radius
    const float bowRI = width * 0.22f;  // inner radius (hole)

    for (int y = 0; y < static_cast<int>(height * 0.60f); ++y) {
        for (int x = 0; x < width; ++x) {
            const float dx = x - bowCX + 0.5f;
            const float dy = y - bowCY + 0.5f;
            const float r  = std::sqrt(dx * dx + dy * dy);
            if (r <= bowRO && r >= bowRI) {
                pixels[static_cast<size_t>(y) * width + x] = colour;
            }
        }
    }

    // --- Blade: thin rectangle in the bottom 55% ---
    const int bladeL = static_cast<int>(width * 0.44f);
    const int bladeR = static_cast<int>(width * 0.56f);
    const int bladeTop = static_cast<int>(height * 0.48f);
    const int bladeBot = static_cast<int>(height * 0.95f);

    for (int y = bladeTop; y < bladeBot; ++y) {
        for (int x = bladeL; x < bladeR; ++x) {
            pixels[static_cast<size_t>(y) * width + x] = colour;
        }
    }

    // --- Notches: two small indentations cut from the right side of the blade ---
    const int notchW  = bladeR - bladeL;          // full blade width
    const int notch1Y = bladeTop + (bladeBot - bladeTop) * 2 / 5;
    const int notch2Y = bladeTop + (bladeBot - bladeTop) * 3 / 5;
    for (int notchY : {notch1Y, notch2Y}) {
        for (int y = notchY; y < notchY + height / 9; ++y) {
            for (int x = bladeL + notchW / 2; x < bladeR; ++x) {
                pixels[static_cast<size_t>(y) * width + x] = dark;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}

Texture Texture::CreateCubeImpostor(int width, int height,
                                    uint32_t topColor, uint32_t frontColor, uint32_t sideColor) {
    // 0 = transparent in RGBA8888 (alpha == 0)
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);

    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    // 7 key vertices of an axonometric 3D cube projection
    const float cx    = fw * 0.5f;
    const float topY  = fh * 0.08f;
    const float midY  = fh * 0.32f;
    const float leftX = fw * 0.12f;
    const float rightX= fw * 0.88f;
    const float centY = midY + (midY - topY);
    const float botY  = fh * 0.70f;
    const float botMidY = centY + (botY - midY);

    struct Pt { float x, y; };
    const Pt pTop{cx, topY};
    const Pt pRight{rightX, midY};
    const Pt pCenter{cx, centY};
    const Pt pLeft{leftX, midY};
    const Pt pBotLeft{leftX, botY};
    const Pt pBotCenter{cx, botMidY};
    const Pt pBotRight{rightX, botY};

    auto InsideQuadCCW = [](float px, float py, Pt a, Pt b, Pt c, Pt d) -> bool {
        auto cross = [](Pt p1, Pt p2, float x, float y) {
            return (p2.x - p1.x) * (y - p1.y) - (p2.y - p1.y) * (x - p1.x);
        };
        return cross(a, b, px, py) >= 0.0f &&
               cross(b, c, px, py) >= 0.0f &&
               cross(c, d, px, py) >= 0.0f &&
               cross(d, a, px, py) >= 0.0f;
    };

    const uint32_t borderColor = Framebuffer::MakeColor(45, 30, 20);

    for (int y = 0; y < height; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        for (int x = 0; x < width; ++x) {
            const float px = static_cast<float>(x) + 0.5f;

            // 1. Top face (diamond): pLeft -> pTop -> pRight -> pCenter (CCW)
            if (InsideQuadCCW(px, py, pLeft, pTop, pRight, pCenter)) {
                const bool edge = (px - leftX < 1.5f) || (rightX - px < 1.5f) ||
                                  (py - topY < 1.5f) || (centY - py < 1.5f);
                pixels[static_cast<size_t>(y) * width + x] = edge ? borderColor : topColor;
                continue;
            }

            // 2. Left face (front-left): pBotLeft -> pLeft -> pCenter -> pBotCenter (CCW)
            if (InsideQuadCCW(px, py, pBotLeft, pLeft, pCenter, pBotCenter)) {
                const bool edge = (px - leftX < 1.5f) || (std::abs(px - cx) < 1.0f) ||
                                  (py - botY > -1.5f);
                pixels[static_cast<size_t>(y) * width + x] = edge ? borderColor : frontColor;
                continue;
            }

            // 3. Right face (front-right): pBotCenter -> pCenter -> pRight -> pBotRight (CCW)
            if (InsideQuadCCW(px, py, pBotCenter, pCenter, pRight, pBotRight)) {
                const bool edge = (rightX - px < 1.5f) || (std::abs(px - cx) < 1.0f) ||
                                  (py - botY > -1.5f);
                pixels[static_cast<size_t>(y) * width + x] = edge ? borderColor : sideColor;
                continue;
            }
        }
    }

    return Texture(width, height, std::move(pixels));
}
