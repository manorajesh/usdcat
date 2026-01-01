#include "renderer.h"
#include <algorithm>

// public functions ---------------------------------------------

void Renderer::update_framebuffer(Eigen::Vector2i dims) {
  int scale_x = (mode == RenderMode::Braille) ? 2 : 2;
  int scale_y = (mode == RenderMode::Braille) ? 4 : 4;

  this->dims = dims;
  hi_res_dims = Eigen::Vector2i(dims.x() * scale_x, dims.y() * scale_y);

  framebuffer.assign(dims.x() * dims.y(), "");
  zbuffer.assign(dims.x() * dims.y(), std::numeric_limits<float>::infinity());
  intensity_buffer.assign(dims.x() * dims.y(), -1.0f);
  hi_res_intensity.assign(hi_res_dims.x() * hi_res_dims.y(), 0.0f);

  dot_buffer.assign(hi_res_dims.x() * hi_res_dims.y(), 0);
  hi_res_zbuffer.assign(hi_res_dims.x() * hi_res_dims.y(),
                        std::numeric_limits<float>::infinity());
  hi_res_intensity.assign(hi_res_dims.x() * hi_res_dims.y(), 0.0f);

  // update target and eye
  if (!has_hydra_camera) {
    // Default to internal camera
    // if Hydra camera not set
    frame_scene_to_view(dims);
    orbit_camera();
    look_at();
  }
  // Correct for non-square terminal characters (typically 1x2 ratio)
  float aspect = 0.5f * (float)dims.x() / std::max(1.0f, (float)dims.y());
  float fov_y = has_hydra_camera ? hydra_fov_y : FOV;

  Eigen::Vector3f light_dir = Eigen::Vector3f(0.4, 0.6, 0.2).normalized();

  for (auto const &[path, m] : meshes) {
    for (auto const &tri : m.indices) {
      // 1. Get Local Vertices
      Eigen::Vector3f local0 = m.vertices[tri(0)];
      Eigen::Vector3f local1 = m.vertices[tri(1)];
      Eigen::Vector3f local2 = m.vertices[tri(2)];

      // 2. Transform to WORLD Space using the matrix Hydra gave us
      // We use .homogeneous() to multiply a 3f by a 4f matrix
      Eigen::Vector3f p0 = (m.worldTransform * local0.homogeneous()).head<3>();
      Eigen::Vector3f p1 = (m.worldTransform * local1.homogeneous()).head<3>();
      Eigen::Vector3f p2 = (m.worldTransform * local2.homogeneous()).head<3>();

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
      if (n_view.z() <= 0)
        continue;

      auto q0 = project(v0, fov_y, aspect);
      auto q1 = project(v1, fov_y, aspect);
      auto q2 = project(v2, fov_y, aspect);
      if (!q0 || !q1 || !q2)
        continue;

      Eigen::Vector3f proj0 = *q0;
      Eigen::Vector3f proj1 = *q1;
      Eigen::Vector3f proj2 = *q2;

      Eigen::Vector2f s0 = to_hi_res_screen(proj0.head<2>());
      Eigen::Vector2f s1 = to_hi_res_screen(proj1.head<2>());
      Eigen::Vector2f s2 = to_hi_res_screen(proj2.head<2>());

      int minx = std::max(0, std::min({(int)s0.x(), (int)s1.x(), (int)s2.x()}));
      int maxx = std::min(hi_res_dims.x() - 1,
                          std::max({(int)s0.x(), (int)s1.x(), (int)s2.x()}));
      int miny = std::max(0, std::min({(int)s0.y(), (int)s1.y(), (int)s2.y()}));
      int maxy = std::min(hi_res_dims.y() - 1,
                          std::max({(int)s0.y(), (int)s1.y(), (int)s2.y()}));

      int idx = (int)(lambert * (ramp_size - 1) + 0.5);
      std::string ch = ramp[std::max(0, std::min(ramp_size - 1, idx))];
      for (int py = miny; py <= maxy; ++py) {
        for (int px = minx; px <= maxx; ++px) {
          auto bc_optional = barycentric({px + 0.5f, py + 0.5f}, s0, s1, s2);
          if (!bc_optional)
            continue;

          Eigen::Vector3f bc = *bc_optional;

          if (bc.x() < 0.0 || bc.y() < 0.0 || bc.z() < 0.0)
            continue;

          float depth =
              bc.x() * proj0.z() + bc.y() * proj1.z() + bc.z() * proj2.z();
          int k_dot = py * hi_res_dims.x() + px;
          if (depth < hi_res_zbuffer[k_dot]) {
            hi_res_zbuffer[k_dot] = depth;
            dot_buffer[k_dot] = 1;
            hi_res_intensity[k_dot] = lambert; // Store intensity
          }
        }
      }
    }
  }

  for (int y = 0; y < dims.y(); ++y) {
    for (int x = 0; x < dims.x(); ++x) {
      if (mode == RenderMode::Braille) {
        framebuffer[y * dims.x() + x] = get_colored_braille_char(x, y);
      } else {
        framebuffer[y * dims.x() + x] = get_colored_block_char(x, y);
      }
    }
  }
}

void Renderer::display_framebuffer() {
  screen.erase();
  screen.display_frame(framebuffer, dims.x(), dims.y());
}

void Renderer::frame_scene_to_view(Eigen::Vector2i dims) {
  if (meshes.empty())
    return;

  Eigen::Vector3f bmin(std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity());
  Eigen::Vector3f bmax(-std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity());

  bool any = false;
  for (auto const &[path, m] : meshes) {
    for (auto const &v : m.vertices) {
      Eigen::Vector3f p = (m.worldTransform * v.homogeneous()).head<3>();
      bmin = bmin.cwiseMin(p);
      bmax = bmax.cwiseMax(p);
      any = true;
    }
  }
  if (!any)
    return;

  Eigen::Vector3f center = 0.5f * (bmin + bmax);
  Eigen::Vector3f ext = 0.5f * (bmax - bmin);
  float sceneR = ext.norm();
  sceneR = std::max(sceneR, 1e-4f);

  // Always look at scene center
  target = center;

  // Match your projection's effective half-angle (see project(): fov_y * 0.6)
  float halfV = FOV * 0.6f;
  float tanHalfV = std::tan(halfV);

  float aspect = 0.5f * (float)dims.x() / std::max(1.0f, (float)dims.y());
  aspect = std::max(1e-4f, aspect);

  // Need to fit sphere in both vertical and horizontal
  float margin = 1.10f;
  float reqV = (sceneR * margin) / tanHalfV;
  float reqH = (sceneR * margin) / (tanHalfV * aspect);
  float required = std::max(reqV, reqH);

  // Only zoom out (don't fight user zooming out further)
  radius = std::max(radius, required);
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

void Renderer::set_hydra_camera(const pxr::GfMatrix4d &world_to_view,
                                float fov_y) {
  pxr::GfMatrix4d view = world_to_view;
  pxr::GfMatrix4d inv_view = view.GetInverse();
  pxr::GfVec4d eye4 = inv_view * pxr::GfVec4d(0.0, 0.0, 0.0, 1.0);
  eye = Eigen::Vector3f(eye4[0], eye4[1], eye4[2]);

  pxr::GfVec4d row0 = view.GetRow(0);
  pxr::GfVec4d row1 = view.GetRow(1);
  pxr::GfVec4d row2 = view.GetRow(2);
  r = Eigen::Vector3f(row0[0], row0[1], row0[2]).normalized();
  u = Eigen::Vector3f(row1[0], row1[1], row1[2]).normalized();
  fneg = Eigen::Vector3f(row2[0], row2[1], row2[2]).normalized();

  view_matrix.setIdentity();
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      view_matrix(i, j) = static_cast<float>(view[j][i]);
    }
  }

  target = eye - fneg;
  hydra_fov_y = fov_y;
  has_hydra_camera = true;
}

void Renderer::clear_hydra_camera() { has_hydra_camera = false; }

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

inline Eigen::Vector2f Renderer::to_hi_res_screen(Eigen::Vector2f ndc) {
  float sx = (ndc.x() * 0.5f + 0.5f) * (hi_res_dims.x() - 1);
  float sy = (-ndc.y() * 0.5f + 0.5f) * (hi_res_dims.y() - 1);
  return Eigen::Vector2f(sx, sy);
}

std::string Renderer::get_colored_braille_char(int char_x, int char_y) {
  int code = 0;
  float total_intensity = 0.0f;
  int active_dots = 0;

  static const int dot_map[4][2] = {{0, 3}, {1, 4}, {2, 5}, {6, 7}};

  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 2; ++j) {
      int dx = char_x * 2 + j;
      int dy = char_y * 4 + i;
      if (dx < hi_res_dims.x() && dy < hi_res_dims.y()) {
        int idx = dy * hi_res_dims.x() + dx;
        if (dot_buffer[idx]) {
          code |= (1 << dot_map[i][j]);
          total_intensity += hi_res_intensity[idx];
          active_dots++;
        }
      }
    }
  }

  if (active_dots == 0)
    return "\xe2\xa0\x80";

  // Map intensity to 0-255 for grayscale
  float coverage = (float)active_dots / 8.0f;
  float avg_intensity =
      (active_dots > 0) ? (total_intensity / active_dots) : 0.0f;

  int color = static_cast<int>(avg_intensity * coverage * 255.0f);
  color = std::clamp(color, 0, 255);

  // Construct UTF-8 Braille
  std::string result;
  // ANSI Foreground Color: \x1b[38;2;r;g;bm
  result += "\x1b[38;2;" + std::to_string(color) + ";" + std::to_string(color) +
            ";" + std::to_string(color) + "m";

  // Add Braille bytes
  result.push_back(static_cast<char>(0xE2));
  result.push_back(static_cast<char>(0xA0 | (code >> 6)));
  result.push_back(static_cast<char>(0x80 | (code & 0x3F)));

  // Reset color
  result += "\x1b[0m";

  return result;
}

std::string Renderer::get_colored_block_char(int char_x, int char_y) {
  // sample a 2x2 area for the TOP and a 2x2 for the BOTTOM
  // This requires hi_res_dims to be (dims.x * 2, dims.y * 4)

  auto get_avg_sample = [&](int start_x, int start_y) {
    float sum = 0.0f;
    int count = 0;
    for (int dy = 0; dy < 2; ++dy) {
      for (int dx = 0; dx < 2; ++dx) {
        int idx = (start_y + dy) * hi_res_dims.x() + (start_x + dx);
        if (dot_buffer[idx]) {
          sum += hi_res_intensity[idx];
          count++;
        }
      }
    }
    return (count > 0) ? (sum / 4.0f)
                       : 0.0f; // Divide by total possible samples
  };

  float avg_top = get_avg_sample(char_x * 2, char_y * 4);
  float avg_bot = get_avg_sample(char_x * 2, char_y * 4 + 2);

  if (avg_top <= 0.0f && avg_bot <= 0.0f)
    return " ";

  int c_top = std::clamp((int)(avg_top * 255.0f), 0, 255);
  int c_bot = std::clamp((int)(avg_bot * 255.0f), 0, 255);

  std::string result;
  result += "\x1b[38;2;" + std::to_string(c_top) + ";" + std::to_string(c_top) +
            ";" + std::to_string(c_top) + "m";
  result += "\x1b[48;2;" + std::to_string(c_bot) + ";" + std::to_string(c_bot) +
            ";" + std::to_string(c_bot) + "m";
  result += "▀";
  result += "\x1b[0m";
  return result;
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
  if (has_hydra_camera) {
    Eigen::Vector4f hp(p.x(), p.y(), p.z(), 1.0f);
    Eigen::Vector4f vp = view_matrix * hp;
    return vp.head<3>();
  }
  Eigen::Vector3f pe = p - eye;
  return Eigen::Vector3f(pe.dot(r), pe.dot(u), pe.dot(fneg));
}
