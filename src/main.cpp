#include "renderer.h"
#include <algorithm>
#include <chrono>

int main() {
  getchar(); // Wait here so you can attach Instruments
  Renderer renderer;

  int w{0}, h{0};
  float yaw = 0.0;
  float pitch = 0.2;
  float radius = 4.0;
  float frame_time_ms = 0.0f;

  bool running = true;
  while (running) {
    auto frame_start = std::chrono::high_resolution_clock::now();
    renderer.screen.erase();
    renderer.screen.get_dims(h, w);

    renderer.update_framebuffer({w, h}, yaw, pitch, radius);
    renderer.display_framebuffer();

    renderer.screen.add_string(
        h - 2, 0,
        "Frame Time: " + std::to_string(frame_time_ms) + "ms (" +
            std::to_string(static_cast<int>(1000.0f / frame_time_ms)) +
            " FPS)");
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
    frame_time_ms =
        std::chrono::duration<float, std::milli>(frame_end - frame_start)
            .count();
  }

  return 0;
}
