#pragma once
#include <ncurses.h>
#include <string>

class Screen {
private:
  WINDOW *window;
  bool owns_stdscr;

public:
  Screen(WINDOW *win = nullptr);
  ~Screen();

  void add_string(int y, int x, const char *str, int n);
  void add_string(int y, int x, const char *str);
  void add_string(int y, int x, std::string str) {
    add_string(y, x, str.c_str());
  }
  void get_dims(int &h, int &w);
  void erase();
  void refresh();
  int wgetch();
};
