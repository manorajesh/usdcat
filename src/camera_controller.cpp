#include "camera_controller.h"
#include "renderer.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/xformable.h>

float CameraController::_compute_camera_fov_y(
    const pxr::UsdGeomCamera &camera) const {
  float fov_y = 1.0472f;
  float focal = 0.0f;
  float vertical_aperture = 0.0f;
  camera.GetFocalLengthAttr().Get(&focal);
  camera.GetVerticalApertureAttr().Get(&vertical_aperture);
  if (focal > 1e-6f) {
    fov_y = 2.0f * std::atan(0.5f * vertical_aperture / focal);
  }
  return fov_y;
}

bool CameraController::frame_to_meshes(const Renderer &renderer,
                                       const pxr::UsdGeomCamera &camera, int w,
                                       int h) {
  Eigen::Vector3f bmin(std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::infinity());
  Eigen::Vector3f bmax(-std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity());
  bool any = false;

  for (auto const &[path, m] : renderer.get_meshes()) {
    (void)path;
    for (auto const &v : m.vertices) {
      Eigen::Vector3f p = (m.worldTransform * v.homogeneous()).head<3>();
      bmin = bmin.cwiseMin(p);
      bmax = bmax.cwiseMax(p);
      any = true;
    }
  }

  if (!any) {
    return false;
  }

  Eigen::Vector3f center = 0.5f * (bmin + bmax);
  Eigen::Vector3f ext = 0.5f * (bmax - bmin);
  float scene_r = std::max(ext.norm(), 1e-4f);

  _target = pxr::GfVec3d(center.x(), center.y(), center.z());

  float fov_y = _compute_camera_fov_y(camera);
  float half_v = fov_y * 0.6f;
  float tan_half_v = std::tan(half_v);
  float aspect =
      0.5f * static_cast<float>(w) / std::max(1.0f, static_cast<float>(h));
  aspect = std::max(1e-4f, aspect);

  float margin = 1.35f;
  float req_v = (scene_r * margin) / tan_half_v;
  float req_h = (scene_r * margin) / (tan_half_v * aspect);
  float required = std::max(req_v, req_h);
  float outside = scene_r * 2.0f;

  _radius = std::max(_radius, std::max(required, outside));
  return true;
}

void CameraController::apply_orbit(const pxr::UsdGeomCamera &camera) const {
  float pitch = std::max(-1.5f, std::min(1.5f, _pitch));
  double cy = std::cos(_yaw);
  double sy = std::sin(_yaw);
  double cp = std::cos(pitch);
  double sp = std::sin(pitch);

  pxr::GfVec3d eye(_target[0] + _radius * cp * cy, _target[1] + _radius * sp,
                   _target[2] + _radius * cp * sy);

  pxr::GfMatrix4d view;
  view.SetLookAt(eye, _target, pxr::GfVec3d(0.0, 1.0, 0.0));
  pxr::GfMatrix4d world = view.GetInverse();

  pxr::UsdGeomXformable xformable(camera.GetPrim());
  pxr::UsdGeomXformOp op = xformable.MakeMatrixXform();
  op.Set(world);
}

bool CameraController::handle_input(int c, Renderer &renderer,
                                    const pxr::UsdGeomCamera &camera, int w,
                                    int h, bool &running) {
  bool changed = false;
  switch (c) {
  case 3: // Ctrl+C
  case 'q':
    running = false;
    return false;
  case KEY_LEFT:
    _yaw -= _yaw_step;
    changed = true;
    break;
  case KEY_RIGHT:
    _yaw += _yaw_step;
    changed = true;
    break;
  case KEY_UP:
    _pitch += _pitch_step;
    changed = true;
    break;
  case KEY_DOWN:
    _pitch -= _pitch_step;
    changed = true;
    break;
  case 'w':
    _radius = std::max(1.0f, _radius - _radius_step);
    changed = true;
    break;
  case 's':
    _radius += _radius_step;
    changed = true;
    break;
  case 'f':
    if (frame_to_meshes(renderer, camera, w, h)) {
      apply_orbit(camera);
      changed = true;
    }
    break;
  default:
    break;
  }

  if (changed && c != 'f') {
    apply_orbit(camera);
  }
  return changed;
}
