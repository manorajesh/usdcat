#include "render_pass.h"
#include "delegate.h"

void pxr::HdTerminalRenderPass::_Execute(
    HdRenderPassStateSharedPtr const &renderPassState,
    TfTokenVector const &renderTags) {

  // 1. Get the Delegate from the Render Index
  auto *delegate =
      static_cast<HdTerminalDelegate *>(GetRenderIndex()->GetRenderDelegate());

  // 2. Access your Renderer backend
  Renderer *renderer = delegate->GetRenderer();

  // 3. Trigger the draw logic
  // We pass the parameters you were previously handling manually in main.cpp
  // For now, these are likely still stored or passed into your renderer
  int w, h;
  renderer->screen.get_dims(h, w);

  // Note: yaw, pitch, and radius are likely managed in your main.cpp loop
  // and updated in the renderer. You can call your core update function here:
  renderer->update_framebuffer({w, h}, renderer->get_yaw(),
                               renderer->get_pitch(), renderer->get_radius());
}