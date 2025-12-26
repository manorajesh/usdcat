#include "mesh_loader.h"
#include "renderer.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <numeric>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>

// 8 vertices of a cube
const std::vector<Eigen::Vector3f> VERTS = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
};

// 12 triangles (indices into V)
const std::vector<Eigen::Vector3i> INDICES = {
    {0, 1, 2}, {0, 2, 3}, // back
    {4, 6, 5}, {4, 7, 6}, // front
    {0, 4, 5}, {0, 5, 1}, // bottom
    {3, 2, 6}, {3, 6, 7}, // top
    {0, 3, 7}, {0, 7, 4}, // left
    {1, 5, 6}, {1, 6, 2}, // right
};

int main() {
  Renderer renderer;

  auto stage = pxr::UsdStage::Open("simple_primitives.usda");
  for (const auto &prim : stage->Traverse()) {
    if (prim.IsA<pxr::UsdGeomMesh>()) {
      pxr::UsdGeomMesh usdMesh(prim);
      MeshData data = MeshLoader::LoadUsdMesh(usdMesh);

      // Now send to your renderer
      // Assuming your renderer has a method to add mesh data
      renderer.add_mesh(data.vertices, data.indices);
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
