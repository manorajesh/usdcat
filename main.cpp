#include <Eigen/Dense>
#include <curses.h>
#include <stdio.h>
#include <vector>

// 8 vertices of a cube
std::vector<Eigen::Vector3f> V = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
};

// 12 triangles (indices into V)
std::vector<Eigen::Vector3i> TRIS = {
    {0, 1, 2}, {0, 2, 3}, // back
    {4, 6, 5}, {4, 7, 6}, // front
    {0, 4, 5}, {0, 5, 1}, // bottom
    {3, 2, 6}, {3, 6, 7}, // top
    {0, 3, 7}, {0, 7, 4}, // left
    {1, 5, 6}, {1, 6, 2}, // right
};

class Renderer {
public:
  void orbit_camera(float &eye, float &at, float yaw, float pitch,
                    float radius) {
    // Implementation of camera orbiting
  }
};

class Screen {
public:
  Screen() {
    initscr();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    noecho();
  }
  ~Screen() { endwin(); }

  void addString(int y, int x, const char *str) { mvaddstr(y, x, str); }
  void clear() { ::clear(); }
  void refresh() { ::refresh(); }
  int wgetch() { return ::getch(); }
};

int main() {
  Screen screen;

  char c;
  while ((c = screen.wgetch()) != 'q') {
    screen.clear();

    screen.refresh();
  }

  return 0;
}