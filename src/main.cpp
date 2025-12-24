#include "renderer.h"
#include <algorithm>

int main() {
  Renderer renderer;

  int w{0}, h{0};
  float yaw = 0.0;
  float pitch = 0.2;
  float radius = 4.0;

  bool running = true;
  while (running) {
    renderer.screen.erase();
    renderer.screen.get_dims(h, w);

    char c = renderer.screen.wgetch();

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

    renderer.update_framebuffer({w, h}, yaw, pitch, radius);
    renderer.display_framebuffer();
    renderer.screen.refresh();
  }

  return 0;
}
