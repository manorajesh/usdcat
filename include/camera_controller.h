#pragma once
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usdGeom/camera.h>

class Renderer;

class CameraController {
public:
  CameraController() = default;

  bool frame_to_meshes(const Renderer &renderer,
                       const pxr::UsdGeomCamera &camera, int w, int h);
  void apply_orbit(const pxr::UsdGeomCamera &camera) const;
  bool handle_input(int c, Renderer &renderer, const pxr::UsdGeomCamera &camera,
                    int w, int h, bool &running);

private:
  float _yaw = 0.0f;
  float _pitch = 0.2f;
  float _radius = 1.0f;

  float _yaw_step = 0.1f;
  float _pitch_step = 0.08f;
  float _radius_step = 10.0f;

  pxr::GfVec3d _target = pxr::GfVec3d(0.0, 0.0, 0.0);

  float _compute_camera_fov_y(const pxr::UsdGeomCamera &camera) const;
};
