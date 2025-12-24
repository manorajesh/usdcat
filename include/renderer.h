#pragma once
#include "screen.h"
#include <Eigen/Dense>
#include <optional>
#include <vector>

class Renderer {
public:
  Renderer() {}
  ~Renderer() {}

  void update_framebuffer(Eigen::Vector2i dims, float yaw, float pitch,
                          float radius);
  void display_framebuffer();

  // curses Screen
  Screen screen{};

private:
  // display variables
  std::vector<char> framebuffer;
  std::vector<float> zbuffer;

  // render calculation variables
  Eigen::Vector3f eye;
  Eigen::Vector3f target = Eigen::Vector3f(0, 0, 0);
  Eigen::Vector2i dims;

  float yaw;
  float pitch;
  float radius;

  Eigen::Vector3f r;
  Eigen::Vector3f u;
  Eigen::Vector3f fneg;

  // render functions
  void orbit_camera();

  void look_at(Eigen::Vector3f up = Eigen::Vector3f(0, 1, 0));

  std::optional<Eigen::Vector3f> project(Eigen::Vector3f p_view, float fov_y,
                                         float aspect);

  Eigen::Vector2i to_screen(Eigen::Vector2f ndc);

  std::optional<Eigen::Vector3f> barycentric(Eigen::Vector2f p,
                                             Eigen::Vector2f a,
                                             Eigen::Vector2f b,
                                             Eigen::Vector2f c);

  Eigen::Vector3f world_to_view(Eigen::Vector3f p);
};
