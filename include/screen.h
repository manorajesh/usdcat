#pragma once
#include <string>
#include <termios.h>
#include <vector>

enum KeyCode { KEY_UP = 259, KEY_DOWN = 258, KEY_RIGHT = 261, KEY_LEFT = 260 };

class Screen {
public:
  Screen(bool blocking_input = true);
  ~Screen();

  void add_string(int y, int x, const char *str);
  void add_string(int y, int x, const char *str, int n);
  void add_string(int y, int x, std::string str) {
    add_string(y, x, str.c_str());
  }
  void get_dims(int &h, int &w);
  void erase();
  void refresh();
  int wgetch();

  void display_frame(const std::vector<std::string> &framebuffer, int width,
                     int height);

private:
  struct termios orig_termios;
  bool is_raw = false;
};