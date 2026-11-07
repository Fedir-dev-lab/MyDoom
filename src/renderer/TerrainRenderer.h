#pragma once
class Framebuffer;
class TerrainRenderer {
public:
    TerrainRenderer(int width, int height, float fovRadians);
    void Draw(Framebuffer& framebuffer, float cameraX, float cameraY, float cameraZ,
              float yaw, float pitch) const;
private:
    int m_width, m_height;
    int m_terrainWidth, m_terrainHeight;
    float m_focalLength;
};
