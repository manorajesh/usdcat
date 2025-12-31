#pragma once
#include "mesh.h"
#include "screen.h"
#include <Eigen/Dense>
#include <map>
#include <optional>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/sdf/path.h>
#include <string>
#include <vector>

class Renderer {
public:
  Renderer() {}
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

  void frame_scene_to_view(Eigen::Vector2i dims);

  void set_target(const Eigen::Vector3f &t) { target = t; }
  Eigen::Vector3f get_target() const { return target; }

  void set_hydra_camera(const pxr::GfMatrix4d &world_to_view, float fov_y);
  void clear_hydra_camera();

  // curses Screen
  Screen screen{};

private:
  // character set
  const std::string ramp[4] = {" ", "≥", "•", "…"};
  const int ramp_size = 4;

  // geometry keyed by Usd path
  std::map<pxr::SdfPath, MeshData> meshes;

  // display variables
  std::vector<std::string> framebuffer;
  // std::vector<std::string> previous_framebuffer;
  std::vector<float> zbuffer;
  std::vector<float> intensity_buffer;

  // This buffer is 2x wider and 4x taller than the terminal
  std::vector<uint8_t> dot_buffer;
  std::vector<float> hi_res_zbuffer;
  Eigen::Vector2i hi_res_dims;
  std::vector<float> hi_res_intensity;

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

  // render functions
  void orbit_camera();

  void look_at(Eigen::Vector3f up = Eigen::Vector3f(0, 1, 0));

  std::optional<Eigen::Vector3f> project(Eigen::Vector3f p_view, float fov_y,
                                         float aspect);

  Eigen::Vector2f to_screen(Eigen::Vector2f ndc);
  Eigen::Vector2f to_hi_res_screen(Eigen::Vector2f ndc);
  std::string get_colored_braille_char(int char_x, int char_y);
  std::string get_colored_block_char(int char_x, int char_y);

  std::optional<Eigen::Vector3f> barycentric(const Eigen::Vector2f &p,
                                             const Eigen::Vector2f &a,
                                             const Eigen::Vector2f &b,
                                             const Eigen::Vector2f &c);

  Eigen::Vector3f world_to_view(Eigen::Vector3f p);
};
