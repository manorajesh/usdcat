#include "renderer.h"

#include "delegate.h"
#include "render_task.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <numeric>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usdImaging/usdImaging/delegate.h>

void handle_input(int c, Renderer &renderer, bool &running) {
  switch (c) {
  case 'q':
    running = false;
    break;
  case KEY_LEFT:
    renderer.set_yaw(renderer.get_yaw() - 0.10f);
    break;
  case KEY_RIGHT:
    renderer.set_yaw(renderer.get_yaw() + 0.10f);
    break;
  case KEY_UP:
    renderer.set_pitch(renderer.get_pitch() + 0.08f);
    break;
  case KEY_DOWN:
    renderer.set_pitch(renderer.get_pitch() - 0.08f);
    break;
  case 'w':
    renderer.set_radius(std::max(1.0f, renderer.get_radius() - 0.3f));
    break;
  case 's':
    renderer.set_radius(renderer.get_radius() + 0.3f);
    break;
  default:
    break;
  }
}

int main() {
  Renderer renderer;

  auto stage = pxr::UsdStage::Open("simple_primitives.usda");
  if (!stage) {
    return -1;
  }

  pxr::HdTerminalDelegate renderDelegate(&renderer);
  pxr::HdRenderIndex *renderIndex =
      pxr::HdRenderIndex::New(&renderDelegate, {});

  pxr::UsdImagingDelegate sceneDelegate(renderIndex,
                                        pxr::SdfPath::AbsoluteRootPath());
  sceneDelegate.Populate(stage->GetPseudoRoot());

  pxr::HdRprimCollection collection(
      pxr::HdTokens->geometry,
      pxr::HdReprSelector(pxr::HdReprTokens->smoothHull));

  collection.SetRootPath(pxr::SdfPath::AbsoluteRootPath());

  pxr::HdRenderPassSharedPtr renderPass =
      renderDelegate.CreateRenderPass(renderIndex, collection);

  pxr::SdfPath taskPath("/renderTask");
  pxr::HdTaskSharedPtr renderTask =
      std::make_shared<pxr::HdTerminalRenderTask>(renderPass, taskPath);

  pxr::HdTaskSharedPtrVector tasks = {renderTask};

  pxr::HdEngine engine;

  int w{0}, h{0};
  float frame_time_micos = 0.0f;
  std::deque<float> frame_times;

  bool running = true;
  while (running) {
    auto frame_start = std::chrono::high_resolution_clock::now();
    renderer.screen.get_dims(h, w);

    // This will internally call renderPass->Sync() and renderPass->Execute()
    engine.Execute(renderIndex, &tasks);

    renderer.display_framebuffer();

    // --------

    std::string fps_text =
        "Frame Time: " + std::to_string(frame_time_micos) + "μs (" +
        std::to_string(static_cast<int>(1e6f / frame_time_micos)) + " FPS)";
    fps_text.resize(50, ' ');
    renderer.screen.add_string(h - 2, 0, fps_text);
    renderer.screen.add_string(h - 1, 0, "Arrows: orbit | w/s: zoom | q: quit");
    renderer.screen.refresh();

    int c = renderer.screen.wgetch();
    handle_input(c, renderer, running);

    auto frame_end = std::chrono::high_resolution_clock::now();
    float current_frame_time =
        std::chrono::duration<float, std::micro>(frame_end - frame_start)
            .count();
    frame_times.push_back(current_frame_time);
    if (frame_times.size() > 100) {
      frame_times.pop_front();
    }
    frame_time_micos =
        std::accumulate(frame_times.begin(), frame_times.end(), 0.0f) /
        frame_times.size();
  }

  return 0;
}
