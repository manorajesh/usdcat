#include "render_pass.h"
#include "delegate.h"
#include <cmath>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/renderPassState.h>

void pxr::HdTerminalRenderPass::_Execute(
    [[maybe_unused]] HdRenderPassStateSharedPtr const &renderPassState,
    [[maybe_unused]] TfTokenVector const &renderTags) {

  // 1. Get the Delegate from the Render Index
  auto *delegate =
      static_cast<HdTerminalDelegate *>(GetRenderIndex()->GetRenderDelegate());

  // 2. Access your Renderer backend
  Renderer *renderer = delegate->GetRenderer();

  if (renderPassState) {
    const HdCamera *camera = renderPassState->GetCamera();
    if (camera) {
      float fov_y = 1.0472f;
      if (camera->GetProjection() == HdCamera::Projection::Perspective) {
        float focal = camera->GetFocalLength();
        float vertical_aperture = camera->GetVerticalAperture();
        if (focal > 1e-6f) {
          fov_y = 2.0f * std::atan(0.5f * vertical_aperture / focal);
        }
      }
      renderer->set_hydra_camera(renderPassState->GetWorldToViewMatrix(),
                                 fov_y);
    } else {
      renderer->clear_hydra_camera();
    }
  }

  // 3. Trigger the draw logic using the viewport set by main.cpp
  renderer->update_framebuffer({renderer->get_viewport_w(), renderer->get_viewport_h()});
}
