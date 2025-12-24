#include <curses.h>

class Screen {
public:
  Screen();
  ~Screen();

  void add_string(int y, int x, const char *str, int n);
  void add_string(int y, int x, const char *str);
  void get_dims(int &h, int &w);
  void erase();
  void refresh();
  int wgetch();

private:
  WINDOW *scr = stdscr;
};
