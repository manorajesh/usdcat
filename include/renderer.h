#pragma once
#include "mesh.h"
#include "screen.h"
#include <Eigen/Dense>
#include <map>
#include <optional>
#include <pxr/usd/sdf/path.h>
#include <vector>

class Renderer {
public:
  Renderer() {}
  ~Renderer() {}

  void update_framebuffer(Eigen::Vector2i dims, float yaw, float pitch,
                          float radius);
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

  // curses Screen
  Screen screen{};

private:
  // character set
  static constexpr const char RAMP[] =
      " .`^\",:;Il!i~+_-?][}{1)(|\\/"
      "*tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
  static constexpr int RAMP_SIZE = sizeof(RAMP) - 1;

  // geometry keyed by Usd path
  std::map<pxr::SdfPath, MeshData> meshes;

  // display variables
  std::vector<char> framebuffer;
  std::vector<char> previous_framebuffer;
  std::vector<float> zbuffer;
  std::vector<float> intensity_buffer;

  // render calculation variables
  Eigen::Vector3f eye;
  Eigen::Vector3f target = Eigen::Vector3f(0, 0, 0);
  Eigen::Vector2i dims;

  float yaw = 0.0f;
  float pitch = 0.2f;
  float radius = 4.0f;
  static constexpr float FOV = 1.0472f; // 60 degrees in radians

  Eigen::Vector3f r;
  Eigen::Vector3f u;
  Eigen::Vector3f fneg;

  // render functions
  void orbit_camera();

  void look_at(Eigen::Vector3f up = Eigen::Vector3f(0, 1, 0));

  std::optional<Eigen::Vector3f> project(Eigen::Vector3f p_view, float fov_y,
                                         float aspect);

  Eigen::Vector2f to_screen(Eigen::Vector2f ndc);

  std::optional<Eigen::Vector3f> barycentric(const Eigen::Vector2f &p,
                                             const Eigen::Vector2f &a,
                                             const Eigen::Vector2f &b,
                                             const Eigen::Vector2f &c);

  Eigen::Vector3f world_to_view(Eigen::Vector3f p);
};
