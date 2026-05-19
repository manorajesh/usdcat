#pragma once
#include "mesh.h"
#include "screen.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/sdf/path.h>
#include <string>
#include <vector>

enum class RenderMode { Braille, HalfBlock };

struct ImageTexture {
    int width = 0;
    int height = 0;
    std::vector<float> pixels; // RGB float
    bool valid = false;

    Eigen::Vector3f sample(float u, float v,
                           const Eigen::Vector3f &fallback) const {
        if (!valid || pixels.empty()) return fallback;

        // Wrap (Repeat) logic
        u = u - floor(u);
        v = v - floor(v);

        int x = static_cast<int>(u * width);
        int y = static_cast<int>(v * height);

        // Clamp just in case
        x = std::max(0, std::min(x, width - 1));
        y = std::max(0, std::min(y, height - 1));

        int idx = (y * width + x) * 3;
        return {pixels[idx], pixels[idx+1], pixels[idx+2]};
    }
};

struct MaterialData {
  Eigen::Vector3f baseColor = Eigen::Vector3f(0.8f, 0.8f, 0.8f);
  Eigen::Vector3f emissiveColor = Eigen::Vector3f::Zero();
  float metallic = 0.0f;
  float roughness = 1.0f;
  float occlusion = 1.0f;
  float opacity = 1.0f;
  ImageTexture baseColorTexture;
  ImageTexture normalTexture;
  ImageTexture occlusionTexture;
};

struct RenderScratch {
  std::vector<Eigen::Vector3f> world_vertices;
  std::vector<Eigen::Vector3f> view_vertices;
  std::vector<Eigen::Vector3f> world_normals;
  std::vector<Eigen::Vector3f> smooth_world_normals;
};

class Renderer {
public:
  Renderer(RenderMode mode, bool blocking_input = true)
      : screen(blocking_input), mode(mode) {}
  ~Renderer() {}

  void update_framebuffer(Eigen::Vector2i dims);
  void display_framebuffer();

  // mesh management
  void update_mesh(const pxr::SdfPath &path, const MeshData &data) {
    if (meshes.find(path) != meshes.end()) {
      meshes[path] = data;
    } else {
      meshes.insert({path, data});
    }
  }
  void clear_meshes() { meshes.clear(); }

  // setters/getters
  void set_yaw(float y) { yaw = y; }
  void set_pitch(float p) { pitch = p; }
  void set_radius(float r) { radius = r; }

  float get_yaw() const { return yaw; }
  float get_pitch() const { return pitch; }
  float get_radius() const { return radius; }

  std::map<pxr::SdfPath, MeshData> &get_meshes() { return meshes; }
  const std::map<pxr::SdfPath, MeshData> &get_meshes() const { return meshes; }

  std::map<pxr::SdfPath, MaterialData> &get_materials() { return materials; }
  const std::map<pxr::SdfPath, MaterialData> &get_materials() const { return materials; }
  std::mutex &get_scene_mutex() { return scene_mutex; }

  void frame_scene_to_view(Eigen::Vector2i dims);

  void set_target(const Eigen::Vector3f &t) { target = t; }
  Eigen::Vector3f get_target() const { return target; }

  void set_hydra_camera(const pxr::GfMatrix4d &world_to_view, float fov_y);
  void clear_hydra_camera();

  void set_render_mode(RenderMode m) { mode = m; }
  RenderMode get_render_mode() const { return mode; }

  void set_viewport(int col_offset, int row_offset, int width, int height) {
    viewport_col_offset_ = col_offset;
    viewport_row_offset_ = row_offset;
    viewport_w_ = width;
    viewport_h_ = height;
  }
  int get_viewport_w() const { return viewport_w_; }
  int get_viewport_h() const { return viewport_h_; }

  // curses Screen
  Screen screen{};

private:
  // character set
  const std::string ramp[4] = {" ", "≥", "•", "…"};

  // geometry keyed by Usd path
  std::map<pxr::SdfPath, MeshData> meshes;

  // materials keyed by material path
  std::map<pxr::SdfPath, MaterialData> materials;
  std::mutex scene_mutex;

  // strings
  std::string output_buffer;
  std::vector<float> zbuffer;
  std::vector<float> intensity_buffer;

  // This buffer is 2x wider and 4x taller than the terminal
  std::vector<uint8_t> dot_buffer;
  std::vector<float> hi_res_zbuffer;
  Eigen::Vector2i hi_res_dims;
  std::vector<Eigen::Vector3f> hi_res_color;
  RenderScratch scratch;

  // render calculation variables
  Eigen::Vector3f eye;
  Eigen::Vector3f target = Eigen::Vector3f(0, 0, 0);
  Eigen::Vector2i dims;

  float yaw = 0.0f;
  float pitch = 0.2f;
  float radius = 4.0f;
  static constexpr float FOV = 1.0472f; // 60 degrees in radians
  float hydra_fov_y = FOV;
  bool has_hydra_camera = false;

  Eigen::Vector3f r;
  Eigen::Vector3f u;
  Eigen::Vector3f fneg;
  Eigen::Matrix4f view_matrix = Eigen::Matrix4f::Identity();

  RenderMode mode = RenderMode::HalfBlock;

  int viewport_col_offset_ = 0;
  int viewport_row_offset_ = 0;
  int viewport_w_ = 0;
  int viewport_h_ = 0;

  // render functions
  void orbit_camera();

  void look_at(Eigen::Vector3f up = Eigen::Vector3f(0, 1, 0));

  std::optional<Eigen::Vector3f> project(Eigen::Vector3f p_view, float fov_y,
                                         float aspect);

  Eigen::Vector2f to_screen(Eigen::Vector2f ndc);
  Eigen::Vector2f to_hi_res_screen(Eigen::Vector2f ndc);

  // Write character directly to output buffer
  int write_colored_braille_char(char *out, int char_x, int char_y);
  int write_colored_block_char(char *out, int char_x, int char_y);

  std::optional<Eigen::Vector3f> barycentric(const Eigen::Vector2f &p,
                                             const Eigen::Vector2f &a,
                                             const Eigen::Vector2f &b,
                                             const Eigen::Vector2f &c);

  Eigen::Vector3f world_to_view(Eigen::Vector3f p);
};
