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
  void orbit_delta(float yaw_delta, float pitch_delta,
                   const pxr::UsdGeomCamera &camera);
  void zoom_delta(float steps, const pxr::UsdGeomCamera &camera);

private:
  float _yaw = 0.0f;
  float _pitch = 0.2f;
  float _radius = 1.0f;
  float _scene_radius = 1.0f;
  float _min_radius = 0.05f;

  float _yaw_step = 0.1f;
  float _pitch_step = 0.08f;
  float _zoom_factor = 1.12f;

  pxr::GfVec3d _target = pxr::GfVec3d(0.0, 0.0, 0.0);

  float _compute_camera_fov_y(const pxr::UsdGeomCamera &camera) const;
  void _normalize_angles();
};
