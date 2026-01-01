#include "screen.h"
#include <clocale>
#include <cstdio>
#include <sys/ioctl.h>
#include <unistd.h>

Screen::Screen(bool blocking_input) {
  setlocale(LC_ALL, "");

  // 1. Enter Alternate Screen Buffer
  // and hide the cursor
  std::printf("\033[?1049h\033[?25l");

  // 2. Set terminal to raw mode for non-blocking/direct input
  tcgetattr(STDIN_FILENO, &orig_termios);
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_cflag |= (CS8);

  // VMIN = 0, VTIME = 0 makes read() non-blocking
  if (!blocking_input) {
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
  } else {
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
  }

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  is_raw = true;
}

Screen::~Screen() {
  // Restore terminal state, show cursor, leave alternate buffer
  if (is_raw) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  }
  std::printf("\033[?25h\033[?1049l");
  std::fflush(stdout);
}

void Screen::add_string(int y, int x, const char *str) {
  // ANSI Move Cursor: \033[Line;ColumnH (1-indexed)
  std::printf("\033[%d;%dH%s", y + 1, x + 1, str);
}

void Screen::add_string(int y, int x, const char *str, int n) {
  std::printf("\033[%d;%dH%.*s", y + 1, x + 1, n, str);
}

void Screen::get_dims(int &h, int &w) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    h = 24;
    w = 80; // Fallback
  } else {
    h = ws.ws_row;
    w = ws.ws_col;
  }
}

void Screen::refresh() { std::fflush(stdout); }

int Screen::wgetch() {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) == 1) {
    // Basic arrow key handling (ANSI escape sequences start with \033[)
    if (c == '\033') {
      char seq[2];
      if (read(STDIN_FILENO, &seq[0], 1) == 0)
        return '\033';
      if (read(STDIN_FILENO, &seq[1], 1) == 0)
        return '\033';

      if (seq[0] == '[') {
        switch (seq[1]) {
        case 'A':
          return KEY_UP;
        case 'B':
          return KEY_DOWN;
        case 'C':
          return KEY_RIGHT;
        case 'D':
          return KEY_LEFT;
        }
      }
      return '\033';
    }
    return c;
  }
  return -1;
}

void Screen::erase() {
  // move the cursor back to the top-left
  std::printf("\033[H");
}

void Screen::display_frame(const std::vector<std::string> &framebuffer,
                           int width, int height) {
  // Each cell can be up to ~60 bytes (ANSI codes + UTF-8 char)
  // Plus newlines. Reserve generously to avoid reallocations.
  std::string output_buffer;
  output_buffer.reserve(width * height * 64);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::string &cell = framebuffer[y * width + x];
      output_buffer.append(cell);
    }

    if (y < height - 1) {
      output_buffer.append("\r\n", 2);
    }
  }

  std::fwrite(output_buffer.data(), 1, output_buffer.size(), stdout);
}

void Screen::display_buffer(const char *data, size_t len) {
  std::fwrite(data, 1, len, stdout);
}