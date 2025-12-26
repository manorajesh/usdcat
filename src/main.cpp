#include "renderer.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <numeric>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>

int main() {
  Renderer renderer;

  auto stage = pxr::UsdStage::Open("cube.usda");
  for (const auto &prim : stage->Traverse()) {
    if (prim.IsA<pxr::UsdGeomMesh>()) {
      pxr::UsdGeomMesh usdMesh(prim);
      MeshData data = MeshLoader::LoadUsdMesh(usdMesh);

      renderer.add_mesh(data);
    }
  }

  int w{0}, h{0};
  float yaw = 0.0;
  float pitch = 0.2;
  float radius = 4.0;
  float frame_time_micos = 0.0f;
  std::deque<float> frame_times;

  bool running = true;
  while (running) {
    auto frame_start = std::chrono::high_resolution_clock::now();
    renderer.screen.get_dims(h, w);

    renderer.update_framebuffer({w, h}, yaw, pitch, radius);
    renderer.display_framebuffer();

    std::string fps_text =
        "Frame Time: " + std::to_string(frame_time_micos) + "μs (" +
        std::to_string(static_cast<int>(1e6f / frame_time_micos)) + " FPS)";
    fps_text.resize(50, ' ');
    renderer.screen.add_string(h - 2, 0, fps_text);
    renderer.screen.add_string(h - 1, 0, "Arrows: orbit | w/s: zoom | q: quit");
    renderer.screen.refresh();

    int c = renderer.screen.wgetch();

    switch (c) {
    case 'q':
      running = false;
      break;
    case KEY_LEFT:
      yaw -= 0.10f;
      break;
    case KEY_RIGHT:
      yaw += 0.10f;
      break;
    case KEY_UP:
      pitch += 0.08f;
      break;
    case KEY_DOWN:
      pitch -= 0.08f;
      break;
    case 'w':
      radius = std::max(2.0f, radius - 0.3f);
      break;
    case 's':
      radius = std::min(20.0f, radius + 0.3f);
      break;
    default:
      break;
    }

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
