#include "renderer.h"
#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define USE_SSE 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define USE_NEON 1
#endif

// Pre-transformed triangle for rasterization
struct RasterTri {
  float s0x, s0y, s1x, s1y, s2x, s2y;  // Screen coords
  float z0, z1, z2;                      // Depths
  float v0x, v0y, v1x, v1y, inv_den;     // Barycentric precomputed
  float lambert;                         // Lighting
  int minx, maxx, miny, maxy;            // Bounding box
  float u0, v0_uv, u1, v1_uv, u2, v2_uv; // UV coordinates
  pxr::SdfPath materialId;               // Material/texture ID
};

// Rasterize triangles within a horizontal band [band_miny, band_maxy]
static void rasterize_band(const std::vector<RasterTri>& tris,
                           int band_miny, int band_maxy,
                           int hr_width,
                           float* hi_res_zbuffer,
                           uint8_t* dot_buffer,
                           float* hi_res_intensity,
                           const std::map<pxr::SdfPath, Texture>* textures) {
  for (const auto& tri : tris) {
    // Skip if triangle doesn't overlap this band
    if (tri.maxy < band_miny || tri.miny > band_maxy)
      continue;

    // Clamp to band
    const int miny = std::max(tri.miny, band_miny);
    const int maxy = std::min(tri.maxy, band_maxy);
    const int minx = tri.minx;
    const int maxx = tri.maxx;

#if defined(USE_NEON)
    const float32x4_t v0x_vec = vdupq_n_f32(tri.v0x);
    const float32x4_t v0y_vec = vdupq_n_f32(tri.v0y);
    const float32x4_t v1x_vec = vdupq_n_f32(tri.v1x);
    const float32x4_t v1y_vec = vdupq_n_f32(tri.v1y);
    const float32x4_t inv_den_vec = vdupq_n_f32(tri.inv_den);
    const float32x4_t s0x_vec = vdupq_n_f32(tri.s0x);
    const float32x4_t s0y_vec = vdupq_n_f32(tri.s0y);
    const float32x4_t z0_vec = vdupq_n_f32(tri.z0);
    const float32x4_t z1_vec = vdupq_n_f32(tri.z1);
    const float32x4_t z2_vec = vdupq_n_f32(tri.z2);
    const float32x4_t one_vec = vdupq_n_f32(1.0f);
    const float32x4_t zero_vec = vdupq_n_f32(0.0f);
    const float32x4_t offset = {0.5f, 1.5f, 2.5f, 3.5f};

    for (int py = miny; py <= maxy; ++py) {
      const float32x4_t v2y_vec = vsubq_f32(vdupq_n_f32(py + 0.5f), s0y_vec);
      const int row_offset = py * hr_width;

      int px = minx;
      for (; px <= maxx - 3; px += 4) {
        float32x4_t px_vec = vaddq_f32(vdupq_n_f32((float)px), offset);
        float32x4_t v2x_vec = vsubq_f32(px_vec, s0x_vec);

        float32x4_t v = vmulq_f32(
            vsubq_f32(vmulq_f32(v2x_vec, v1y_vec), vmulq_f32(v2y_vec, v1x_vec)),
            inv_den_vec);
        float32x4_t w = vmulq_f32(
            vsubq_f32(vmulq_f32(v0x_vec, v2y_vec), vmulq_f32(v0y_vec, v2x_vec)),
            inv_den_vec);
        float32x4_t u = vsubq_f32(vsubq_f32(one_vec, v), w);

        uint32x4_t mask = vandq_u32(vandq_u32(vcgeq_f32(u, zero_vec),
                                               vcgeq_f32(v, zero_vec)),
                                     vcgeq_f32(w, zero_vec));
        if (vmaxvq_u32(mask) == 0)
          continue;

        float32x4_t depth = vaddq_f32(
            vaddq_f32(vmulq_f32(u, z0_vec), vmulq_f32(v, z1_vec)),
            vmulq_f32(w, z2_vec));

        alignas(16) float depths[4];
        alignas(16) uint32_t masks[4];
        vst1q_f32(depths, depth);
        vst1q_u32(masks, mask);

        alignas(16) float us[4], vs[4], ws[4];
        vst1q_f32(us, u);
        vst1q_f32(vs, v);
        vst1q_f32(ws, w);

        for (int i = 0; i < 4; ++i) {
          if (masks[i]) {
            int k_dot = row_offset + px + i;
            if (depths[i] < hi_res_zbuffer[k_dot]) {
              hi_res_zbuffer[k_dot] = depths[i];
              dot_buffer[k_dot] = 1;
              
              // Interpolate UVs and sample texture
              float tex_u = us[i] * tri.u0 + vs[i] * tri.u1 + ws[i] * tri.u2;
              float tex_v = us[i] * tri.v0_uv + vs[i] * tri.v1_uv + ws[i] * tri.v2_uv;
              
              Eigen::Vector3f textureColor(1,1,1);
              if (textures && textures->count(tri.materialId)) {
                const Texture &tex = textures->at(tri.materialId);
                textureColor = tex.sample(tex_u, tex_v);
              }
              
              float intensity = tri.lambert * textureColor.mean();
              hi_res_intensity[k_dot] = intensity;
            }
          }
        }
      }

      // Remainder
      for (; px <= maxx; ++px) {
        float v2x = px + 0.5f - tri.s0x;
        float v2y_s = py + 0.5f - tri.s0y;
        float v = (v2x * tri.v1y - v2y_s * tri.v1x) * tri.inv_den;
        float w = (tri.v0x * v2y_s - tri.v0y * v2x) * tri.inv_den;
        float u = 1.0f - v - w;
        if (u >= 0.0f && v >= 0.0f && w >= 0.0f) {
          float depth = u * tri.z0 + v * tri.z1 + w * tri.z2;
          int k_dot = row_offset + px;
          if (depth < hi_res_zbuffer[k_dot]) {
            hi_res_zbuffer[k_dot] = depth;
            dot_buffer[k_dot] = 1;
            hi_res_intensity[k_dot] = tri.lambert;
          }
        }
      }
    }

#elif defined(USE_SSE)
    const __m128 v0x_vec = _mm_set1_ps(tri.v0x);
    const __m128 v0y_vec = _mm_set1_ps(tri.v0y);
    const __m128 v1x_vec = _mm_set1_ps(tri.v1x);
    const __m128 v1y_vec = _mm_set1_ps(tri.v1y);
    const __m128 inv_den_vec = _mm_set1_ps(tri.inv_den);
    const __m128 s0x_vec = _mm_set1_ps(tri.s0x);
    const __m128 s0y_vec = _mm_set1_ps(tri.s0y);
    const __m128 z0_vec = _mm_set1_ps(tri.z0);
    const __m128 z1_vec = _mm_set1_ps(tri.z1);
    const __m128 z2_vec = _mm_set1_ps(tri.z2);
    const __m128 one_vec = _mm_set1_ps(1.0f);
    const __m128 zero_vec = _mm_setzero_ps();
    const __m128 offset = _mm_set_ps(3.5f, 2.5f, 1.5f, 0.5f);

    for (int py = miny; py <= maxy; ++py) {
      const __m128 v2y_vec = _mm_sub_ps(_mm_set1_ps(py + 0.5f), s0y_vec);
      const int row_offset = py * hr_width;

      int px = minx;
      for (; px <= maxx - 3; px += 4) {
        __m128 px_vec = _mm_add_ps(_mm_set1_ps((float)px), offset);
        __m128 v2x_vec = _mm_sub_ps(px_vec, s0x_vec);

        __m128 v = _mm_mul_ps(
            _mm_sub_ps(_mm_mul_ps(v2x_vec, v1y_vec), _mm_mul_ps(v2y_vec, v1x_vec)),
            inv_den_vec);
        __m128 w = _mm_mul_ps(
            _mm_sub_ps(_mm_mul_ps(v0x_vec, v2y_vec), _mm_mul_ps(v0y_vec, v2x_vec)),
            inv_den_vec);
        __m128 u = _mm_sub_ps(_mm_sub_ps(one_vec, v), w);

        __m128 mask = _mm_and_ps(
            _mm_and_ps(_mm_cmpge_ps(u, zero_vec), _mm_cmpge_ps(v, zero_vec)),
            _mm_cmpge_ps(w, zero_vec));
        if (_mm_movemask_ps(mask) == 0)
          continue;

        __m128 depth = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(u, z0_vec), _mm_mul_ps(v, z1_vec)),
            _mm_mul_ps(w, z2_vec));

        alignas(16) float depths[4];
        alignas(16) float masks_f[4];
        _mm_store_ps(depths, depth);
        _mm_store_ps(masks_f, mask);

        alignas(16) float us[4], vs[4], ws[4];
        _mm_store_ps(us, u);
        _mm_store_ps(vs, v);
        _mm_store_ps(ws, w);

        for (int i = 0; i < 4; ++i) {
          if (masks_f[i] != 0.0f) {
            int k_dot = row_offset + px + i;
            if (depths[i] < hi_res_zbuffer[k_dot]) {
              hi_res_zbuffer[k_dot] = depths[i];
              dot_buffer[k_dot] = 1;
              
              // Interpolate UVs and sample texture
              float tex_u = us[i] * tri.u0 + vs[i] * tri.u1 + ws[i] * tri.u2;
              float tex_v = us[i] * tri.v0_uv + vs[i] * tri.v1_uv + ws[i] * tri.v2_uv;
              
              Eigen::Vector3f textureColor(1,1,1);
              if (textures && textures->count(tri.materialId)) {
                const Texture &tex = textures->at(tri.materialId);
                textureColor = tex.sample(tex_u, tex_v);
              }
              
              float intensity = tri.lambert * textureColor.mean();
              hi_res_intensity[k_dot] = intensity;
            }
          }
        }
      }

      // Remainder
      for (; px <= maxx; ++px) {
        float v2x = px + 0.5f - tri.s0x;
        float v2y_s = py + 0.5f - tri.s0y;
        float v = (v2x * tri.v1y - v2y_s * tri.v1x) * tri.inv_den;
        float w = (tri.v0x * v2y_s - tri.v0y * v2x) * tri.inv_den;
        float u = 1.0f - v - w;
        if (u >= 0.0f && v >= 0.0f && w >= 0.0f) {
          float depth = u * tri.z0 + v * tri.z1 + w * tri.z2;
          int k_dot = row_offset + px;
          if (depth < hi_res_zbuffer[k_dot]) {
            hi_res_zbuffer[k_dot] = depth;
            dot_buffer[k_dot] = 1;
            
            // Interpolate UVs and sample texture
            float tex_u = u * tri.u0 + v * tri.u1 + w * tri.u2;
            float tex_v = u * tri.v0_uv + v * tri.v1_uv + w * tri.v2_uv;
            
            Eigen::Vector3f textureColor(1,1,1);
            if (textures && textures->count(tri.materialId)) {
              const Texture &tex = textures->at(tri.materialId);
              textureColor = tex.sample(tex_u, tex_v);
            }
            
            float intensity = tri.lambert * textureColor.mean();
            hi_res_intensity[k_dot] = intensity;
          }
        }
      }
    }

#else
    // Scalar fallback
    for (int py = miny; py <= maxy; ++py) {
      const int row_offset = py * hr_width;
      const float v2y_s = py + 0.5f - tri.s0y;
      for (int px = minx; px <= maxx; ++px) {
        float v2x = px + 0.5f - tri.s0x;
        float v = (v2x * tri.v1y - v2y_s * tri.v1x) * tri.inv_den;
        float w = (tri.v0x * v2y_s - tri.v0y * v2x) * tri.inv_den;
        float u = 1.0f - v - w;
        if (u >= 0.0f && v >= 0.0f && w >= 0.0f) {
          float depth = u * tri.z0 + v * tri.z1 + w * tri.z2;
          int k_dot = row_offset + px;
          if (depth < hi_res_zbuffer[k_dot]) {
            hi_res_zbuffer[k_dot] = depth;
            dot_buffer[k_dot] = 1;
            
            // Interpolate UVs and sample texture
            float tex_u = u * tri.u0 + v * tri.u1 + w * tri.u2;
            float tex_v = u * tri.v0_uv + v * tri.v1_uv + w * tri.v2_uv;
            
            Eigen::Vector3f textureColor(1,1,1);
            if (textures && textures->count(tri.materialId)) {
              const Texture &tex = textures->at(tri.materialId);
              textureColor = tex.sample(tex_u, tex_v);
            }
            
            float intensity = tri.lambert * textureColor.mean();
            hi_res_intensity[k_dot] = intensity;
          }
          }
        }
      }
    }
#endif
  }
}

static inline char *write_uint8(char *buf, int val) {
  if (val >= 100) {
    *buf++ = '0' + (val / 100);
    val %= 100;
    *buf++ = '0' + (val / 10);
    *buf++ = '0' + (val % 10);
  } else if (val >= 10) {
    *buf++ = '0' + (val / 10);
    *buf++ = '0' + (val % 10);
  } else {
    *buf++ = '0' + val;
  }
  return buf;
}

// public functions ---------------------------------------------

void Renderer::update_framebuffer(Eigen::Vector2i dims) {
  int scale_x = (mode == RenderMode::Braille) ? 2 : 2;
  int scale_y = (mode == RenderMode::Braille) ? 4 : 4;

  this->dims = dims;
  hi_res_dims = Eigen::Vector2i(dims.x() * scale_x, dims.y() * scale_y);

  size_t fb_size = dims.x() * dims.y();
  size_t hr_size = hi_res_dims.x() * hi_res_dims.y();

  zbuffer.resize(fb_size);
  intensity_buffer.resize(fb_size);
  dot_buffer.resize(hr_size);
  hi_res_zbuffer.resize(hr_size);
  hi_res_intensity.resize(hr_size);

  // Fast clear with memset
  std::fill(zbuffer.begin(), zbuffer.end(),
            std::numeric_limits<float>::infinity());
  std::memset(intensity_buffer.data(), 0, fb_size * sizeof(float));
  std::memset(dot_buffer.data(), 0, hr_size);
  std::fill(hi_res_zbuffer.begin(), hi_res_zbuffer.end(),
            std::numeric_limits<float>::infinity());
  std::memset(hi_res_intensity.data(), 0, hr_size * sizeof(float));

  // Reserve output buffer (max ~50 bytes per cell + newlines)
  output_buffer.clear();
  output_buffer.reserve(dims.x() * dims.y() * 52 + dims.y() * 2);

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

  // Phase 1: Pre-transform all triangles (single-threaded vertex processing)
  std::vector<RasterTri> raster_tris;
  raster_tris.reserve(1024);  // Reasonable initial capacity

  for (auto const &[path, m] : meshes) {
    for (auto const &tri : m.indices) {
      Eigen::Vector3f local0 = m.vertices[tri(0)];
      Eigen::Vector3f local1 = m.vertices[tri(1)];
      Eigen::Vector3f local2 = m.vertices[tri(2)];

      Eigen::Vector3f p0 = (m.worldTransform * local0.homogeneous()).head<3>();
      Eigen::Vector3f p1 = (m.worldTransform * local1.homogeneous()).head<3>();
      Eigen::Vector3f p2 = (m.worldTransform * local2.homogeneous()).head<3>();

      Eigen::Vector3f n = (p1 - p0).cross(p2 - p0).normalized();
      float lambert = std::max(0.0f, n.dot(light_dir));

      Eigen::Vector3f v0 = world_to_view(p0);
      Eigen::Vector3f v1 = world_to_view(p1);
      Eigen::Vector3f v2 = world_to_view(p2);

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

      float edge_v0x = s1.x() - s0.x();
      float edge_v0y = s1.y() - s0.y();
      float edge_v1x = s2.x() - s0.x();
      float edge_v1y = s2.y() - s0.y();
      float den = edge_v0x * edge_v1y - edge_v0y * edge_v1x;
      if (std::abs(den) < 1e-9f)
        continue;

      RasterTri rt;
      rt.s0x = s0.x(); rt.s0y = s0.y();
      rt.s1x = s1.x(); rt.s1y = s1.y();
      rt.s2x = s2.x(); rt.s2y = s2.y();
      rt.z0 = proj0.z(); rt.z1 = proj1.z(); rt.z2 = proj2.z();
      rt.v0x = edge_v0x; rt.v0y = edge_v0y;
      rt.v1x = edge_v1x; rt.v1y = edge_v1y;
      rt.inv_den = 1.0f / den;
      rt.lambert = lambert;
      rt.minx = minx; rt.maxx = maxx;
      rt.miny = miny; rt.maxy = maxy;
      
      // Extract UV coordinates if available
      if (!m.uvs.empty() && m.uvs.size() > (size_t)std::max({tri(0), tri(1), tri(2)})) {
        rt.u0 = m.uvs[tri(0)].x();
        rt.v0_uv = m.uvs[tri(0)].y();
        rt.u1 = m.uvs[tri(1)].x();
        rt.v1_uv = m.uvs[tri(1)].y();
        rt.u2 = m.uvs[tri(2)].x();
        rt.v2_uv = m.uvs[tri(2)].y();
      } else {
        // Default UVs
        rt.u0 = rt.v0_uv = 0.0f;
        rt.u1 = rt.v1_uv = 0.0f;
        rt.u2 = rt.v2_uv = 0.0f;
      }
      
      rt.materialId = m.materialId;
      
      raster_tris.push_back(rt);
    }
  }

  // Phase 2: Parallel rasterization by horizontal bands
  const int hr_height = hi_res_dims.y();
  const int hr_width = hi_res_dims.x();
  const unsigned int num_threads = std::max(1u, std::thread::hardware_concurrency());
  const int band_height = std::max(1, (hr_height + (int)num_threads - 1) / (int)num_threads);

  if (raster_tris.size() > 0 && num_threads > 1) {
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (unsigned int t = 0; t < num_threads; ++t) {
      int band_miny = t * band_height;
      int band_maxy = std::min(band_miny + band_height - 1, hr_height - 1);
      if (band_miny > hr_height - 1)
        break;

      threads.emplace_back(rasterize_band,
                           std::cref(raster_tris),
                           band_miny, band_maxy,
                           hr_width,
                           hi_res_zbuffer.data(),
                           dot_buffer.data(),
                           hi_res_intensity.data(),
                           &textures);
    }

    for (auto& th : threads) {
      th.join();
    }
  } else {
    // Single-threaded fallback
    rasterize_band(raster_tris, 0, hr_height - 1, hr_width,
                   hi_res_zbuffer.data(), dot_buffer.data(), hi_res_intensity.data(),
                   &textures);
  }

  // Write directly to output_buffer
  char cell_buf[64];
  for (int y = 0; y < dims.y(); ++y) {
    for (int x = 0; x < dims.x(); ++x) {
      int len;
      if (mode == RenderMode::Braille) {
        len = write_colored_braille_char(cell_buf, x, y);
      } else {
        len = write_colored_block_char(cell_buf, x, y);
      }
      output_buffer.append(cell_buf, len);
    }
    if (y < dims.y() - 1) {
      output_buffer.append("\r\n", 2);
    }
  }
}

void Renderer::display_framebuffer() {
  screen.erase();
  screen.display_buffer(output_buffer.data(), output_buffer.size());
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

int Renderer::write_colored_braille_char(char *out, int char_x, int char_y) {
  int code = 0;
  float total_intensity = 0.0f;
  int active_dots = 0;

  static const int dot_map[4][2] = {{0, 3}, {1, 4}, {2, 5}, {6, 7}};
  const int hr_width = hi_res_dims.x();
  const int base_x = char_x * 2;
  const int base_y = char_y * 4;

  for (int i = 0; i < 4; ++i) {
    int dy = base_y + i;
    if (dy >= hi_res_dims.y())
      continue;
    int row_offset = dy * hr_width;
    for (int j = 0; j < 2; ++j) {
      int dx = base_x + j;
      if (dx >= hr_width)
        continue;
      int idx = row_offset + dx;
      if (dot_buffer[idx]) {
        code |= (1 << dot_map[i][j]);
        total_intensity += hi_res_intensity[idx];
        active_dots++;
      }
    }
  }

  if (active_dots == 0) {
    out[0] = '\xe2';
    out[1] = '\xa0';
    out[2] = '\x80';
    return 3;
  }

  // Map intensity to 0-255 for grayscale
  float coverage = (float)active_dots * 0.125f; // /8
  float avg_intensity = total_intensity / active_dots;

  int color = static_cast<int>(avg_intensity * coverage * 255.0f);
  color = std::clamp(color, 0, 255);

  char *p = out;

  *p++ = '\x1b';
  *p++ = '[';
  *p++ = '3';
  *p++ = '8';
  *p++ = ';';
  *p++ = '2';
  *p++ = ';';
  p = write_uint8(p, color);
  *p++ = ';';
  p = write_uint8(p, color);
  *p++ = ';';
  p = write_uint8(p, color);
  *p++ = 'm';

  // Braille UTF-8 bytes
  *p++ = static_cast<char>(0xE2);
  *p++ = static_cast<char>(0xA0 | (code >> 6));
  *p++ = static_cast<char>(0x80 | (code & 0x3F));

  // Reset: \x1b[0m
  *p++ = '\x1b';
  *p++ = '[';
  *p++ = '0';
  *p++ = 'm';

  return static_cast<int>(p - out);
}

int Renderer::write_colored_block_char(char *out, int char_x, int char_y) {
  // sample a 2x2 area for the TOP and a 2x2 for the BOTTOM
  // This requires hi_res_dims to be (dims.x * 2, dims.y * 4)
  const int hr_width = hi_res_dims.x();
  const int base_x = char_x * 2;
  const int base_y_top = char_y * 4;
  const int base_y_bot = base_y_top + 2;

  // Inline sampling for top 2x2 block
  float sum_top = 0.0f;
  int count_top = 0;
  for (int dy = 0; dy < 2; ++dy) {
    int row_offset = (base_y_top + dy) * hr_width;
    for (int dx = 0; dx < 2; ++dx) {
      int idx = row_offset + base_x + dx;
      if (dot_buffer[idx]) {
        sum_top += hi_res_intensity[idx];
        count_top++;
      }
    }
  }
  float avg_top = (count_top > 0) ? (sum_top * 0.25f) : 0.0f;

  // Inline sampling for bottom 2x2 block
  float sum_bot = 0.0f;
  int count_bot = 0;
  for (int dy = 0; dy < 2; ++dy) {
    int row_offset = (base_y_bot + dy) * hr_width;
    for (int dx = 0; dx < 2; ++dx) {
      int idx = row_offset + base_x + dx;
      if (dot_buffer[idx]) {
        sum_bot += hi_res_intensity[idx];
        count_bot++;
      }
    }
  }
  float avg_bot = (count_bot > 0) ? (sum_bot * 0.25f) : 0.0f;

  if (avg_top <= 0.0f && avg_bot <= 0.0f) {
    out[0] = ' ';
    return 1;
  }

  int c_top = std::clamp((int)(avg_top * 255.0f), 0, 255);
  int c_bot = std::clamp((int)(avg_bot * 255.0f), 0, 255);

  char *p = out;

  // Foreground: \x1b[38;2;R;G;Bm
  *p++ = '\x1b';
  *p++ = '[';
  *p++ = '3';
  *p++ = '8';
  *p++ = ';';
  *p++ = '2';
  *p++ = ';';
  p = write_uint8(p, c_top);
  *p++ = ';';
  p = write_uint8(p, c_top);
  *p++ = ';';
  p = write_uint8(p, c_top);
  *p++ = 'm';

  // Background: \x1b[48;2;R;G;Bm
  *p++ = '\x1b';
  *p++ = '[';
  *p++ = '4';
  *p++ = '8';
  *p++ = ';';
  *p++ = '2';
  *p++ = ';';
  p = write_uint8(p, c_bot);
  *p++ = ';';
  p = write_uint8(p, c_bot);
  *p++ = ';';
  p = write_uint8(p, c_bot);
  *p++ = 'm';

  // Upper half block (▀) UTF-8: E2 96 80
  *p++ = '\xe2';
  *p++ = '\x96';
  *p++ = '\x80';

  // Reset: \x1b[0m
  *p++ = '\x1b';
  *p++ = '[';
  *p++ = '0';
  *p++ = 'm';

  return static_cast<int>(p - out);
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
