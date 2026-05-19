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
  int minx, maxx, miny, maxy;            // Bounding box
  float u0, v0_uv, u1, v1_uv, u2, v2_uv; // UV coordinates
  Eigen::Vector3f p0, p1, p2;            // World positions
  Eigen::Vector3f n0, n1, n2;            // World normals
  Eigen::Vector3f tangent, bitangent;     // Tangent frame for normal maps
  const MaterialData *material = nullptr; // Resolved material, or nullptr fallback
};

static inline float saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }

static inline Eigen::Vector3f saturate(const Eigen::Vector3f &v) {
  return v.cwiseMax(0.0f).cwiseMin(1.0f);
}

static inline Eigen::Vector3f fresnel_schlick(float cosTheta,
                                              const Eigen::Vector3f &f0) {
  return f0 + (Eigen::Vector3f::Ones() - f0) *
                  std::pow(1.0f - saturate(cosTheta), 5.0f);
}

static inline float distribution_ggx(float nDotH, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float denom = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
  return a2 / std::max(1e-4f, (float)M_PI * denom * denom);
}

static inline float geometry_schlick_ggx(float nDotV, float roughness) {
  float r = roughness + 1.0f;
  float k = (r * r) / 8.0f;
  return nDotV / std::max(1e-4f, nDotV * (1.0f - k) + k);
}

static inline Eigen::Vector3f shade_preview_surface(
    const MaterialData *material,
    float texU,
    float texV,
    const Eigen::Vector3f &position,
    const Eigen::Vector3f &normal,
    const Eigen::Vector3f &tangent,
    const Eigen::Vector3f &bitangent,
    const Eigen::Vector3f &eye,
    const Eigen::Vector3f &viewRight,
    const Eigen::Vector3f &viewUp,
    const Eigen::Vector3f &viewForward) {
  static const MaterialData fallbackMaterial;
  const MaterialData *mat = material ? material : &fallbackMaterial;

  Eigen::Vector3f baseColor =
      mat->baseColorTexture.sample(texU, texV, mat->baseColor);
  float occlusion = mat->occlusion;
  if (mat->occlusionTexture.valid) {
    occlusion *= mat->occlusionTexture.sample(texU, texV,
                                              Eigen::Vector3f::Ones()).x();
  }

  Eigen::Vector3f n = normal.normalized();
  if (mat->normalTexture.valid) {
    Eigen::Vector3f normalSample =
        mat->normalTexture.sample(texU, texV, Eigen::Vector3f(0.5f, 0.5f, 1.0f));
    Eigen::Vector3f tangentNormal = normalSample * 2.0f - Eigen::Vector3f::Ones();
    Eigen::Vector3f t = (tangent - n * n.dot(tangent)).normalized();
    Eigen::Vector3f b = bitangent.normalized();
    n = (t * tangentNormal.x() + b * tangentNormal.y() +
         n * tangentNormal.z()).normalized();
  }
  Eigen::Vector3f v = (eye - position).normalized();
  Eigen::Vector3f reflectedView = (2.0f * n.dot(v) * n - v).normalized();

  float nDotV = std::max(0.04f, saturate(n.dot(v)));
  float roughness = std::clamp(mat->roughness, 0.04f, 1.0f);
  float metallic = saturate(mat->metallic);

  Eigen::Vector3f f0 = Eigen::Vector3f::Constant(0.04f);
  f0 = f0 * (1.0f - metallic) + baseColor * metallic;

  auto evaluateLight = [&](const Eigen::Vector3f &lightDir,
                           const Eigen::Vector3f &radiance) -> Eigen::Vector3f {
    Eigen::Vector3f l = lightDir.normalized();
    Eigen::Vector3f h = (v + l).normalized();
    float nDotL = saturate(n.dot(l));
    if (nDotL <= 0.0f) {
      return Eigen::Vector3f::Zero();
    }
    float nDotH = saturate(n.dot(h));
    float hDotV = saturate(h.dot(v));
    Eigen::Vector3f f = fresnel_schlick(hDotV, f0);
    float d = distribution_ggx(nDotH, roughness);
    float g = geometry_schlick_ggx(nDotV, roughness) *
              geometry_schlick_ggx(nDotL, roughness);
    Eigen::Vector3f specular =
        (d * g / std::max(1e-4f, 4.0f * nDotV * nDotL)) * f;
    Eigen::Vector3f kd =
        (Eigen::Vector3f::Ones() - f) * (1.0f - metallic);
    Eigen::Vector3f diffuse =
        kd.cwiseProduct(baseColor) * (1.0f / (float)M_PI);
    Eigen::Vector3f result = (diffuse + specular).cwiseProduct(radiance) * nDotL;
    return result;
  };

  Eigen::Vector3f keyDir =
      (viewForward * 0.45f + viewUp * 0.75f - viewRight * 0.35f).normalized();
  Eigen::Vector3f fillDir =
      (viewForward * 0.35f + viewUp * 0.25f + viewRight * 0.65f).normalized();
  Eigen::Vector3f rimDir =
      (-viewForward * 0.45f + viewUp * 0.55f + viewRight * 0.10f).normalized();

  Eigen::Vector3f direct =
      evaluateLight(keyDir, Eigen::Vector3f(3.0f, 2.95f, 2.85f)) +
      evaluateLight(fillDir, Eigen::Vector3f(0.85f, 0.95f, 1.08f)) +
      evaluateLight(rimDir, Eigen::Vector3f(1.25f, 1.30f, 1.35f));

  float hemi = saturate(0.5f + 0.5f * n.dot(viewUp));
  Eigen::Vector3f skyDiffuse(0.46f, 0.50f, 0.54f);
  Eigen::Vector3f groundDiffuse(0.10f, 0.095f, 0.085f);
  Eigen::Vector3f hemiDiffuse =
      (groundDiffuse * (1.0f - hemi) + skyDiffuse * hemi)
          .cwiseProduct(baseColor) *
      (1.0f - 0.75f * metallic) * occlusion;

  float horizon = saturate(0.5f + 0.5f * reflectedView.dot(viewUp));
  float keyGlint = std::pow(saturate(reflectedView.dot(keyDir)), 18.0f);
  float rimGlint = std::pow(saturate(reflectedView.dot(rimDir)), 10.0f);
  Eigen::Vector3f envColor =
      Eigen::Vector3f(0.10f, 0.11f, 0.12f) * (1.0f - horizon) +
      Eigen::Vector3f(0.72f, 0.74f, 0.76f) * horizon;
  Eigen::Vector3f envSpec =
      envColor.cwiseProduct(f0) * (0.35f + 0.85f * metallic) *
          (1.0f - 0.55f * roughness) +
      Eigen::Vector3f::Ones() * (keyGlint + rimGlint * 0.6f) * metallic *
          (1.0f - roughness);
  Eigen::Vector3f color =
      hemiDiffuse + direct + envSpec + mat->emissiveColor;
  return saturate(color);
}

// Rasterize triangles within a horizontal band [band_miny, band_maxy]
static void rasterize_band(const std::vector<RasterTri>& tris,
                           int band_miny, int band_maxy,
                           int hr_width,
                           float* hi_res_zbuffer,
                           uint8_t* dot_buffer,
                           Eigen::Vector3f* hi_res_color,
                           const Eigen::Vector3f eye,
                           const Eigen::Vector3f viewRight,
                           const Eigen::Vector3f viewUp,
                           const Eigen::Vector3f viewForward) {
  for (const auto& tri : tris) {
    // Skip if triangle doesn't overlap this band
    if (tri.maxy < band_miny || tri.miny > band_maxy)
      continue;

    // Clamp to band
    const int miny = std::max(tri.miny, band_miny);
    const int maxy = std::min(tri.maxy, band_maxy);
    const int minx = tri.minx;
    const int maxx = tri.maxx;

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
            
            float tex_u = u * tri.u0 + v * tri.u1 + w * tri.u2;
            float tex_v = u * tri.v0_uv + v * tri.v1_uv + w * tri.v2_uv;
            Eigen::Vector3f position = u * tri.p0 + v * tri.p1 + w * tri.p2;
            Eigen::Vector3f normal =
                (u * tri.n0 + v * tri.n1 + w * tri.n2).normalized();

            hi_res_color[k_dot] =
                shade_preview_surface(tri.material, tex_u, tex_v,
                                      position, normal, tri.tangent,
                                      tri.bitangent, eye, viewRight, viewUp,
                                      viewForward);
          }
        }
      }
    }
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

  if (zbuffer.size() != fb_size) zbuffer.resize(fb_size);
  if (intensity_buffer.size() != fb_size) intensity_buffer.resize(fb_size);
  if (dot_buffer.size() != hr_size) dot_buffer.resize(hr_size);
  if (hi_res_zbuffer.size() != hr_size) hi_res_zbuffer.resize(hr_size);
  if (hi_res_color.size() != hr_size) hi_res_color.resize(hr_size);

  // Fast clear with memset
  std::fill(zbuffer.begin(), zbuffer.end(),
            std::numeric_limits<float>::infinity());
  std::memset(intensity_buffer.data(), 0, fb_size * sizeof(float));
  std::memset(dot_buffer.data(), 0, hr_size);
  std::fill(hi_res_zbuffer.begin(), hi_res_zbuffer.end(),
            std::numeric_limits<float>::infinity());
  std::fill(hi_res_color.begin(), hi_res_color.end(), Eigen::Vector3f::Zero());

  // Reserve output buffer (max ~50 bytes per cell + newlines)
  output_buffer.clear();
  output_buffer.reserve(dims.x() * dims.y() * 52 + dims.y() * 2);

  std::lock_guard<std::mutex> sceneLock(scene_mutex);

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
  Eigen::Vector3f viewForward = -fneg.normalized();
  Eigen::Vector3f viewRight = r.normalized();
  Eigen::Vector3f viewUp = u.normalized();

  // Phase 1: Pre-transform all triangles (single-threaded vertex processing)
  std::vector<RasterTri> raster_tris;
  size_t total_triangles = 0;
  for (auto const &[path, m] : meshes) {
    (void)path;
    total_triangles += m.indices.size();
  }
  raster_tris.reserve(total_triangles);

  for (auto const &[path, m] : meshes) {
    (void)path;
    scratch.world_vertices.resize(m.vertices.size());
    scratch.view_vertices.resize(m.vertices.size());
    for (size_t i = 0; i < m.vertices.size(); ++i) {
      scratch.world_vertices[i] =
          (m.worldTransform * m.vertices[i].homogeneous()).head<3>();
      scratch.view_vertices[i] = world_to_view(scratch.world_vertices[i]);
    }

    Eigen::Matrix3f normalMatrix =
        m.worldTransform.block<3, 3>(0, 0).inverse().transpose();
    scratch.smooth_world_normals.clear();
    if (m.smoothSubdivision && !m.smoothNormals.empty()) {
      scratch.smooth_world_normals.resize(m.smoothNormals.size());
      for (size_t i = 0; i < m.smoothNormals.size(); ++i) {
        scratch.smooth_world_normals[i] =
            (normalMatrix * m.smoothNormals[i]).normalized();
      }
    }

    scratch.world_normals.clear();
    if (!m.normals.empty()) {
      scratch.world_normals.resize(m.normals.size());
      for (size_t i = 0; i < m.normals.size(); ++i) {
        scratch.world_normals[i] = (normalMatrix * m.normals[i]).normalized();
      }
    }

    const MaterialData *meshMaterial = nullptr;
    auto materialIt = materials.find(m.materialId);
    if (materialIt != materials.end()) {
      meshMaterial = &materialIt->second;
    }

    for (size_t triIndex = 0; triIndex < m.indices.size(); ++triIndex) {
      const auto &tri = m.indices[triIndex];
      if (tri(0) < 0 || tri(1) < 0 || tri(2) < 0 ||
          static_cast<size_t>(std::max({tri(0), tri(1), tri(2)})) >=
              scratch.world_vertices.size()) {
        continue;
      }

      const Eigen::Vector3f &p0 = scratch.world_vertices[tri(0)];
      const Eigen::Vector3f &p1 = scratch.world_vertices[tri(1)];
      const Eigen::Vector3f &p2 = scratch.world_vertices[tri(2)];

      Eigen::Vector3f faceNormal = (p1 - p0).cross(p2 - p0).normalized();
      Eigen::Vector3f n0 = faceNormal;
      Eigen::Vector3f n1 = faceNormal;
      Eigen::Vector3f n2 = faceNormal;
      const bool hasSmoothSubdivisionNormals =
          m.smoothSubdivision &&
          scratch.smooth_world_normals.size() >
              static_cast<size_t>(std::max({tri(0), tri(1), tri(2)}));
      if (hasSmoothSubdivisionNormals) {
        n0 = scratch.smooth_world_normals[tri(0)];
        n1 = scratch.smooth_world_normals[tri(1)];
        n2 = scratch.smooth_world_normals[tri(2)];
      } else {
        const bool hasNormalIndices = triIndex < m.normalIndices.size();
        const Eigen::Vector3i normalTri =
            hasNormalIndices ? m.normalIndices[triIndex] : tri;
        if (!scratch.world_normals.empty() &&
          scratch.world_normals.size() >
              (size_t)std::max({normalTri(0), normalTri(1), normalTri(2)})) {
          n0 = scratch.world_normals[normalTri(0)];
          n1 = scratch.world_normals[normalTri(1)];
          n2 = scratch.world_normals[normalTri(2)];
        }
      }

      const Eigen::Vector3f &v0 = scratch.view_vertices[tri(0)];
      const Eigen::Vector3f &v1 = scratch.view_vertices[tri(1)];
      const Eigen::Vector3f &v2 = scratch.view_vertices[tri(2)];

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
      rt.minx = minx; rt.maxx = maxx;
      rt.miny = miny; rt.maxy = maxy;
      rt.p0 = p0; rt.p1 = p1; rt.p2 = p2;
      rt.n0 = n0; rt.n1 = n1; rt.n2 = n2;
      
      // Extract UV coordinates if available
      const bool hasUvIndices = triIndex < m.uvIndices.size();
      const Eigen::Vector3i uvTri = hasUvIndices ? m.uvIndices[triIndex] : tri;
      if (!m.uvs.empty() &&
          m.uvs.size() > (size_t)std::max({uvTri(0), uvTri(1), uvTri(2)})) {
        rt.u0 = m.uvs[uvTri(0)].x();
        rt.v0_uv = m.uvs[uvTri(0)].y();
        rt.u1 = m.uvs[uvTri(1)].x();
        rt.v1_uv = m.uvs[uvTri(1)].y();
        rt.u2 = m.uvs[uvTri(2)].x();
        rt.v2_uv = m.uvs[uvTri(2)].y();
      } else {
        // Default UVs
        rt.u0 = rt.v0_uv = 0.0f;
        rt.u1 = rt.v1_uv = 0.0f;
        rt.u2 = rt.v2_uv = 0.0f;
      }

      Eigen::Vector3f edge1 = p1 - p0;
      Eigen::Vector3f edge2 = p2 - p0;
      float du1 = rt.u1 - rt.u0;
      float dv1 = rt.v1_uv - rt.v0_uv;
      float du2 = rt.u2 - rt.u0;
      float dv2 = rt.v2_uv - rt.v0_uv;
      float tangentDen = du1 * dv2 - du2 * dv1;
      if (std::abs(tangentDen) > 1e-8f) {
        float inv = 1.0f / tangentDen;
        rt.tangent = (edge1 * dv2 - edge2 * dv1) * inv;
        rt.bitangent = (edge2 * du1 - edge1 * du2) * inv;
      } else {
        rt.tangent = edge1.normalized();
        rt.bitangent = faceNormal.cross(rt.tangent).normalized();
      }
      
      rt.material = meshMaterial;
      
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
                           hi_res_color.data(),
                           eye,
                           viewRight,
                           viewUp,
                           viewForward);
    }

    for (auto& th : threads) {
      th.join();
    }
  } else {
    // Single-threaded fallback
    rasterize_band(raster_tris, 0, hr_height - 1, hr_width,
                   hi_res_zbuffer.data(), dot_buffer.data(), hi_res_color.data(),
                   eye, viewRight, viewUp, viewForward);
  }

  // Write directly to output_buffer with absolute cursor positioning per row
  char cell_buf[64];
  char pos_buf[24];
  for (int y = 0; y < dims.y(); ++y) {
    int pos_len = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;%dH",
                           viewport_row_offset_ + y + 1,
                           viewport_col_offset_ + 1);
    output_buffer.append(pos_buf, pos_len);
    for (int x = 0; x < dims.x(); ++x) {
      int len;
      if (mode == RenderMode::Braille) {
        len = write_colored_braille_char(cell_buf, x, y);
      } else {
        len = write_colored_block_char(cell_buf, x, y);
      }
      output_buffer.append(cell_buf, len);
    }
  }
}

void Renderer::display_framebuffer() {
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
  constexpr float near_clip = 1e-2f;
  if (p_view.z() > -near_clip) {
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
  Eigen::Vector3f total_color = Eigen::Vector3f::Zero();
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
        total_color += hi_res_color[idx];
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

  float coverage = (float)active_dots * 0.125f; // /8
  Eigen::Vector3f color = (total_color / active_dots) * coverage;
  int r8 = std::clamp((int)(color.x() * 255.0f), 0, 255);
  int g8 = std::clamp((int)(color.y() * 255.0f), 0, 255);
  int b8 = std::clamp((int)(color.z() * 255.0f), 0, 255);

  char *p = out;

  *p++ = '\x1b';
  *p++ = '[';
  *p++ = '3';
  *p++ = '8';
  *p++ = ';';
  *p++ = '2';
  *p++ = ';';
  p = write_uint8(p, r8);
  *p++ = ';';
  p = write_uint8(p, g8);
  *p++ = ';';
  p = write_uint8(p, b8);
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
  Eigen::Vector3f sum_top = Eigen::Vector3f::Zero();
  int count_top = 0;
  for (int dy = 0; dy < 2; ++dy) {
    int row_offset = (base_y_top + dy) * hr_width;
    for (int dx = 0; dx < 2; ++dx) {
      int idx = row_offset + base_x + dx;
      if (dot_buffer[idx]) {
        sum_top += hi_res_color[idx];
        count_top++;
      }
    }
  }
  Eigen::Vector3f avg_top = Eigen::Vector3f::Zero();
  if (count_top > 0) {
    avg_top = sum_top * 0.25f;
  }

  // Inline sampling for bottom 2x2 block
  Eigen::Vector3f sum_bot = Eigen::Vector3f::Zero();
  int count_bot = 0;
  for (int dy = 0; dy < 2; ++dy) {
    int row_offset = (base_y_bot + dy) * hr_width;
    for (int dx = 0; dx < 2; ++dx) {
      int idx = row_offset + base_x + dx;
      if (dot_buffer[idx]) {
        sum_bot += hi_res_color[idx];
        count_bot++;
      }
    }
  }
  Eigen::Vector3f avg_bot = Eigen::Vector3f::Zero();
  if (count_bot > 0) {
    avg_bot = sum_bot * 0.25f;
  }

  if (avg_top.maxCoeff() <= 0.0f && avg_bot.maxCoeff() <= 0.0f) {
    out[0] = ' ';
    return 1;
  }

  int top_r = std::clamp((int)(avg_top.x() * 255.0f), 0, 255);
  int top_g = std::clamp((int)(avg_top.y() * 255.0f), 0, 255);
  int top_b = std::clamp((int)(avg_top.z() * 255.0f), 0, 255);
  int bot_r = std::clamp((int)(avg_bot.x() * 255.0f), 0, 255);
  int bot_g = std::clamp((int)(avg_bot.y() * 255.0f), 0, 255);
  int bot_b = std::clamp((int)(avg_bot.z() * 255.0f), 0, 255);

  char *p = out;

  // Foreground: \x1b[38;2;R;G;Bm
  *p++ = '\x1b';
  *p++ = '[';
  *p++ = '3';
  *p++ = '8';
  *p++ = ';';
  *p++ = '2';
  *p++ = ';';
  p = write_uint8(p, top_r);
  *p++ = ';';
  p = write_uint8(p, top_g);
  *p++ = ';';
  p = write_uint8(p, top_b);
  *p++ = 'm';

  // Background: \x1b[48;2;R;G;Bm
  *p++ = '\x1b';
  *p++ = '[';
  *p++ = '4';
  *p++ = '8';
  *p++ = ';';
  *p++ = '2';
  *p++ = ';';
  p = write_uint8(p, bot_r);
  *p++ = ';';
  p = write_uint8(p, bot_g);
  *p++ = ';';
  p = write_uint8(p, bot_b);
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
