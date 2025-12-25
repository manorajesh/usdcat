#include "renderer.h"
#include <algorithm>

constexpr const char RAMP[] =
    " .`^\",:;Il!i~+_-?][}{1)(|\\/*tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";

// Get length at compile time
constexpr int RAMP_SIZE = sizeof(RAMP) - 1;

// 8 vertices of a cube
const std::vector<Eigen::Vector3f> V = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
};

// 12 triangles (indices into V)
const std::vector<Eigen::Vector3i> TRIS = {
    {0, 1, 2}, {0, 2, 3}, // back
    {4, 6, 5}, {4, 7, 6}, // front
    {0, 4, 5}, {0, 5, 1}, // bottom
    {3, 2, 6}, {3, 6, 7}, // top
    {0, 3, 7}, {0, 7, 4}, // left
    {1, 5, 6}, {1, 6, 2}, // right
};

const float FOV = 1.0472f; // 60 degrees in radians

// public functions ---------------------------------------------

void Renderer::update_framebuffer(Eigen::Vector2i dims, float yaw, float pitch,
                                  float radius) {
  framebuffer.assign(dims.x() * dims.y(), ' ');
  zbuffer.assign(dims.x() * dims.y(), std::numeric_limits<float>::infinity());
  this->dims = dims;
  this->yaw = yaw;
  this->pitch = pitch;
  this->radius = radius;

  // update target and eye
  orbit_camera();
  look_at();
  float aspect = (float)dims.x() / std::max(1.0f, (float)dims.y());

  Eigen::Vector3f light_dir = Eigen::Vector3f(0.4, 0.6, 0.2).normalized();

  for (auto tri : TRIS) {
    Eigen::Vector3f p0 = V[tri(0)];
    Eigen::Vector3f p1 = V[tri(1)];
    Eigen::Vector3f p2 = V[tri(2)];

    Eigen::Vector3f n = (p1 - p0).cross(p2 - p0).normalized();
    float lambert = std::max(0.0f, n.dot(light_dir));

    // backface culling
    Eigen::Vector3f v0 = world_to_view(p0);
    Eigen::Vector3f v1 = world_to_view(p1);
    Eigen::Vector3f v2 = world_to_view(p2);
    // compute signed area in screen-ish space later; cheap view-space cull:
    // if normal points away from camera, skip
    // camera looks down -Z in view space, so facing camera means normal has
    // negative z in view
    Eigen::Vector3f n_view = (v1 - v0).cross(v2 - v0);
    if (n_view.z() >= 0)
      continue;

    auto q0 = project(v0, FOV, aspect);
    auto q1 = project(v1, FOV, aspect);
    auto q2 = project(v2, FOV, aspect);
    if (!q0 || !q1 || !q2)
      continue;

    Eigen::Vector3f proj0 = *q0;
    Eigen::Vector3f proj1 = *q1;
    Eigen::Vector3f proj2 = *q2;

    Eigen::Vector2f s0 = to_screen(proj0.head<2>());
    Eigen::Vector2f s1 = to_screen(proj1.head<2>());
    Eigen::Vector2f s2 = to_screen(proj2.head<2>());

    int minx = std::max(0, std::min({(int)s0.x(), (int)s1.x(), (int)s2.x()}));
    int maxx = std::min(dims.x() - 1,
                        std::max({(int)s0.x(), (int)s1.x(), (int)s2.x()}));
    int miny = std::max(0, std::min({(int)s0.y(), (int)s1.y(), (int)s2.y()}));
    int maxy = std::min(dims.y() - 1,
                        std::max({(int)s0.y(), (int)s1.y(), (int)s2.y()}));

    int idx = (int)(lambert * (RAMP_SIZE - 1) + 0.5);
    char ch = RAMP[std::max(0, std::min(RAMP_SIZE - 1, idx))];
    for (int py = miny; py <= maxy; ++py) {
      for (int px = minx; px <= maxx; ++px) {
        auto bc_optional = barycentric({px + 0.5f, py + 0.5f}, s0, s1, s2);
        if (!bc_optional)
          continue;

        Eigen::Vector3f bc = *bc_optional;

        if (bc.x() < 0 || bc.y() < 0 || bc.z() < 0)
          continue;

        float depth =
            bc.x() * proj0.z() + bc.y() * proj1.z() + bc.z() * proj2.z();
        int k = py * dims.x() + px;
        if (depth < zbuffer[k]) {
          zbuffer[k] = depth;
          framebuffer[k] = ch;
        }
      }
    }
  }
}

void Renderer::display_framebuffer() {
  for (int row = 0; row < dims.y(); ++row) {
    int row_offset = row * dims.x();
    for (int col = 0; col < dims.x();) {
      if (framebuffer[row_offset + col] == ' ') {
        col++;
        continue;
      }
      int start = col;
      while (col < dims.x() && framebuffer[row_offset + col] != ' ') {
        col++;
      }
      screen.add_string(row, start, &framebuffer[row_offset + start],
                        col - start);
    }
  }
}

// private functions ---------------------------------------------

void Renderer::orbit_camera() {
  pitch = std::max(-1.5f, std::min(1.5f, pitch));
  float cy = cosf(yaw);
  float sy = sinf(yaw);
  float cp = cosf(pitch);
  float sp = sinf(pitch);

  float x = radius * cp * cy;
  float y = radius * sp;
  float z = radius * cp * sy;

  eye = target + Eigen::Vector3f(x, y, z);
}

void Renderer::look_at(Eigen::Vector3f up) {
  Eigen::Vector3f f = (target - eye).normalized();
  r = f.cross(up).normalized();
  u = r.cross(f);
  fneg = -f;
}

inline std::optional<Eigen::Vector3f>
Renderer::project(Eigen::Vector3f p_view, float fov_y, float aspect) {
  if (p_view.z() > -1e-6) {
    return {};
  }

  float f = 1.0 / std::tan(fov_y * 0.6);
  float ndc_x = (p_view.x() * f / aspect) / (-p_view.z());
  float ndc_y = (p_view.y() * f) / (-p_view.z());
  return Eigen::Vector3f(ndc_x, ndc_y, -p_view.z()); // positive depth
}

inline Eigen::Vector2f Renderer::to_screen(Eigen::Vector2f ndc) {
  float sx = (ndc.x() * 0.5 + 0.5) * (dims.x() - 1);
  float sy = (-ndc.y() * 0.5 + 0.5) * (dims.y() - 1);
  return Eigen::Vector2f(sx, sy);
}

inline std::optional<Eigen::Vector3f>
Renderer::barycentric(const Eigen::Vector2f &p, const Eigen::Vector2f &a,
                      const Eigen::Vector2f &b, const Eigen::Vector2f &c) {
  Eigen::Vector2f v0 = b - a;
  Eigen::Vector2f v1 = c - a;
  Eigen::Vector2f v2 = p - a;

  // Use cross products (for 2D, we only care about z-component)
  float den = v0.x() * v1.y() - v0.y() * v1.x();

  if (std::abs(den) < 1e-9f) {
    return {};
  }

  float inv = 1.0f / den;
  float v = (v2.x() * v1.y() - v2.y() * v1.x()) * inv;
  float w = (v0.x() * v2.y() - v0.y() * v2.x()) * inv;
  float u = 1.0f - v - w;

  return Eigen::Vector3f(u, v, w);
}

inline Eigen::Vector3f Renderer::world_to_view(Eigen::Vector3f p) {
  Eigen::Vector3f pe = p - eye;
  return Eigen::Vector3f(pe.dot(r), pe.dot(u), pe.dot(fneg));
}
