#pragma once
#include <curses.h>
#include <string>

class Screen {
public:
  Screen();
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
