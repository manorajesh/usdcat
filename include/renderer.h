#pragma once
#include "screen.h"
#include <Eigen/Dense>
#include <optional>
#include <vector>

class Renderer {
public:
  Renderer(std::vector<Eigen::Vector3f> verts,
           std::vector<Eigen::Vector3i> indices)
      : verts(std::move(verts)), indices(std::move(indices)) {}

  Renderer() {}
  ~Renderer() {}

  void update_framebuffer(Eigen::Vector2i dims, float yaw, float pitch,
                          float radius);
  void display_framebuffer();
  void add_mesh(const std::vector<Eigen::Vector3f> &vertices,
                const std::vector<Eigen::Vector3i> &indices);

  // curses Screen
  Screen screen{};

private:
  // character set
  static constexpr const char RAMP[] =
      " .`^\",:;Il!i~+_-?][}{1)(|\\/"
      "*tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
  static constexpr int RAMP_SIZE = sizeof(RAMP) - 1;

  // geometry
  std::vector<Eigen::Vector3f> verts;
  std::vector<Eigen::Vector3i> indices;

  // display variables
  std::vector<char> framebuffer;
  std::vector<char> previous_framebuffer;
  std::vector<float> zbuffer;
  std::vector<float> intensity_buffer;

  // render calculation variables
  Eigen::Vector3f eye;
  Eigen::Vector3f target = Eigen::Vector3f(0, 0, 0);
  Eigen::Vector2i dims;

  float yaw;
  float pitch;
  float radius;
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
