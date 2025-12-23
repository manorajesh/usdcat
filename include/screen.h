#include <curses.h>

class Screen {
public:
  Screen();
  ~Screen();

  void addString(int y, int x, const char *str);
  void clear();
  void refresh();
  int wgetch();
};