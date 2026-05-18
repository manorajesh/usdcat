#include "screen.h"
#include <clocale>
#include <cstdio>

// ── Platform-specific: constructor, destructor, get_dims, wgetch ──────────────

#ifdef _WIN32

#include <conio.h>

Screen::Screen(bool blocking_input) {
  setlocale(LC_ALL, "");
  blocking_input_ = blocking_input;

  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);

  GetConsoleMode(hOut, &orig_console_mode_out_);
  GetConsoleMode(hIn,  &orig_console_mode_in_);

  // Enable ANSI/VT100 escape sequence processing on output
  SetConsoleMode(hOut, orig_console_mode_out_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

  // Raw input: no echo, no line buffering; Ctrl+C arrives as byte 3
  SetConsoleMode(hIn, orig_console_mode_in_
                      & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT));

  std::printf("\033[?1049h\033[?25l");
  std::fflush(stdout);
}

Screen::~Screen() {
  std::printf("\033[?25h\033[?1049l");
  std::fflush(stdout);
  SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), orig_console_mode_out_);
  SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),  orig_console_mode_in_);
}

void Screen::get_dims(int &h, int &w) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    w = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
    h = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
  } else {
    h = 24; w = 80;
  }
}

int Screen::wgetch() {
  if (!blocking_input_ && !_kbhit()) return -1;
  int c = _getch();
  // Extended key prefix: 0x00 or 0xE0 followed by the scan code
  if (c == 0 || c == 0xE0) {
    switch (_getch()) {
    case 72: return KEY_UP;
    case 80: return KEY_DOWN;
    case 77: return KEY_RIGHT;
    case 75: return KEY_LEFT;
    default: return -1;
    }
  }
  return c;
}

#else // POSIX ──────────────────────────────────────────────────────────────────

#include <sys/ioctl.h>
#include <unistd.h>

Screen::Screen(bool blocking_input) {
  setlocale(LC_ALL, "");

  std::printf("\033[?1049h\033[?25l");

  tcgetattr(STDIN_FILENO, &orig_termios);
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_cflag |= (CS8);

  // VMIN=0, VTIME=0 → non-blocking; VMIN=1, VTIME=0 → blocking
  if (!blocking_input) {
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
  } else {
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
  }

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  is_raw = true;
}

Screen::~Screen() {
  if (is_raw)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  std::printf("\033[?25h\033[?1049l");
  std::fflush(stdout);
}

void Screen::get_dims(int &h, int &w) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    h = 24; w = 80;
  } else {
    h = ws.ws_row;
    w = ws.ws_col;
  }
}

int Screen::wgetch() {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) == 1) {
    if (c == '\033') {
      char seq[2];
      if (read(STDIN_FILENO, &seq[0], 1) == 0) return '\033';
      if (read(STDIN_FILENO, &seq[1], 1) == 0) return '\033';
      if (seq[0] == '[') {
        switch (seq[1]) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        }
      }
      return '\033';
    }
    return c;
  }
  return -1;
}

#endif // _WIN32

// ── Platform-independent ──────────────────────────────────────────────────────

void Screen::add_string(int y, int x, const char *str) {
  std::printf("\033[%d;%dH%s", y + 1, x + 1, str);
}

void Screen::add_string(int y, int x, const char *str, int n) {
  std::printf("\033[%d;%dH%.*s", y + 1, x + 1, n, str);
}

void Screen::erase() {
  std::printf("\033[H");
}

void Screen::refresh() {
  std::fflush(stdout);
}

void Screen::display_frame(const std::vector<std::string> &framebuffer,
                           int width, int height) {
  std::string output_buffer;
  output_buffer.reserve(width * height * 64);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x)
      output_buffer.append(framebuffer[y * width + x]);
    if (y < height - 1)
      output_buffer.append("\r\n", 2);
  }

  std::fwrite(output_buffer.data(), 1, output_buffer.size(), stdout);
}

void Screen::display_buffer(const char *data, size_t len) {
  std::fwrite(data, 1, len, stdout);
}

void Screen::clear() {
  std::printf("\033[2J\033[H");
}
