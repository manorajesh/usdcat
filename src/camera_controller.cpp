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

void CameraController::_normalize_angles() {
  constexpr float two_pi = 6.28318530718f;
  _yaw = std::fmod(_yaw, two_pi);
  if (_yaw > static_cast<float>(M_PI)) {
    _yaw -= two_pi;
  } else if (_yaw < -static_cast<float>(M_PI)) {
    _yaw += two_pi;
  }
  _pitch = std::fmod(_pitch, two_pi);
  if (_pitch > static_cast<float>(M_PI)) {
    _pitch -= two_pi;
  } else if (_pitch < -static_cast<float>(M_PI)) {
    _pitch += two_pi;
  }
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
  _scene_radius = scene_r;
  _min_radius = std::max(scene_r * 0.05f, 1e-3f);

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

  _radius = std::max(_min_radius, std::max(required, outside));
  return true;
}

void CameraController::apply_orbit(const pxr::UsdGeomCamera &camera) const {
  constexpr double pole_epsilon = 1e-4;
  double pitch = _pitch;
  double cp = std::cos(pitch);
  if (std::abs(cp) < pole_epsilon) {
    pitch += (cp >= 0.0 ? pole_epsilon : -pole_epsilon);
    cp = std::cos(pitch);
  }
  double cy = std::cos(_yaw);
  double sy = std::sin(_yaw);
  double sp = std::sin(pitch);

  pxr::GfVec3d eye(_target[0] + _radius * cp * cy, _target[1] + _radius * sp,
                   _target[2] + _radius * cp * sy);

  pxr::GfMatrix4d view;
  pxr::GfVec3d up = cp >= 0.0 ? pxr::GfVec3d(0.0, 1.0, 0.0)
                              : pxr::GfVec3d(0.0, -1.0, 0.0);
  view.SetLookAt(eye, _target, up);
  pxr::GfMatrix4d world = view.GetInverse();

  pxr::UsdGeomXformable xformable(camera.GetPrim());
  pxr::UsdGeomXformOp op = xformable.MakeMatrixXform();
  op.Set(world);
}

void CameraController::orbit_delta(float yaw_delta, float pitch_delta,
                                   const pxr::UsdGeomCamera &camera) {
  _yaw += yaw_delta;
  _pitch += pitch_delta;
  _normalize_angles();
  apply_orbit(camera);
}

void CameraController::zoom_delta(float steps,
                                  const pxr::UsdGeomCamera &camera) {
  float factor = std::pow(_zoom_factor, std::abs(steps));
  if (steps > 0.0f) {
    _radius = std::max(_min_radius, _radius / factor);
  } else if (steps < 0.0f) {
    _radius = std::min(_scene_radius * 1000.0f, _radius * factor);
  }
  apply_orbit(camera);
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
    _normalize_angles();
    changed = true;
    break;
  case KEY_RIGHT:
    _yaw += _yaw_step;
    _normalize_angles();
    changed = true;
    break;
  case KEY_UP:
    _pitch += _pitch_step;
    _normalize_angles();
    changed = true;
    break;
  case KEY_DOWN:
    _pitch -= _pitch_step;
    _normalize_angles();
    changed = true;
    break;
  case 'w':
    zoom_delta(1.0f, camera);
    changed = true;
    break;
  case 's':
    zoom_delta(-1.0f, camera);
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

  if (changed && c != 'f' && c != 'w' && c != 's') {
    apply_orbit(camera);
  }
  return changed;
}
