#include "renderer.h"

// 8 vertices of a cube
std::vector<Eigen::Vector3f> V = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
};

// 12 triangles (indices into V)
std::vector<Eigen::Vector3i> TRIS = {
    {0, 1, 2}, {0, 2, 3}, // back
    {4, 6, 5}, {4, 7, 6}, // front
    {0, 4, 5}, {0, 5, 1}, // bottom
    {3, 2, 6}, {3, 6, 7}, // top
    {0, 3, 7}, {0, 7, 4}, // left
    {1, 5, 6}, {1, 6, 2}, // right
};

void Renderer::orbit_camera(float yaw, float pitch, float radius,
                            Eigen::Vector3f target) {
  pitch = std::max(-1.5f, std::min(1.5f, pitch));
  float cy = cosf(yaw);
  float sy = sinf(yaw);
  float cp = cosf(pitch);
  float sp = sinf(pitch);

  float x = radius * cp * cy;
  float y = radius * sp;
  float z = radius * cp * sy;

  this->eye = target + Eigen::Vector3f(x, y, z);
}