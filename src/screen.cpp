#include "screen.h"

Screen::Screen(WINDOW *win) : window(win), owns_stdscr(false) {
  if (window == nullptr) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(0);

    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    bkgd(COLOR_PAIR(1));

    window = stdscr;
    owns_stdscr = true;
  } else {
    keypad(window, TRUE);
  }
}

Screen::~Screen() {
  if (owns_stdscr) {
    endwin();
  }
}

void Screen::add_string(int y, int x, const char *str) {
  mvwaddstr(window, y, x, str);
}
void Screen::add_string(int y, int x, const char *str, int n) {
  mvwaddnstr(window, y, x, str, n);
}
void Screen::get_dims(int &h, int &w) { getmaxyx(window, h, w); }
void Screen::erase() { werase(window); }
void Screen::refresh() {
  wnoutrefresh(window); // Copy to virtual screen without refresh
  doupdate();           // Single efficient update
}
int Screen::wgetch() { return ::wgetch(window); }
