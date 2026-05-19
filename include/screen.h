#pragma once
#include <string>
#include <termios.h>
#include <vector>

enum KeyCode {
  KEY_UP = 259,
  KEY_DOWN = 258,
  KEY_RIGHT = 261,
  KEY_LEFT = 260,
  KEY_MOUSE = 409
};

struct MouseEvent {
  int x = 0;
  int y = 0;
  int button = -1;
  bool pressed = false;
  bool motion = false;
  bool wheel_up = false;
  bool wheel_down = false;
};

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
  int wgetch_for(int timeout_ms);
  const MouseEvent &last_mouse_event() const { return last_mouse_; }

  void display_frame(const std::vector<std::string> &framebuffer, int width,
                     int height);

  void display_buffer(const char *data, size_t len);
  void clear();

private:
  struct termios orig_termios;
  bool is_raw = false;
  MouseEvent last_mouse_;

  int read_byte();
  int parse_escape_sequence();
};
