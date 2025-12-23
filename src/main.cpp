#include "renderer.h"
#include "screen.h"

int main() {
  Screen screen;
  Renderer renderer;

  char c;
  while ((c = screen.wgetch()) != 'q') {
    screen.clear();

    screen.refresh();
  }

  return 0;
}