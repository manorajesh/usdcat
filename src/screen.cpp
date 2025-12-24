#include "screen.h"
#include <curses.h>

Screen::Screen() {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  timeout(0);

  start_color();
  init_pair(1, COLOR_WHITE, COLOR_BLACK);
  bkgd(COLOR_PAIR(1));
}

Screen::~Screen() { endwin(); }

void Screen::add_string(int y, int x, const char *str) { mvaddstr(y, x, str); }
void Screen::add_string(int y, int x, const char *str, int n) {
  mvaddnstr(y, x, str, n);
}
void Screen::get_dims(int &h, int &w) { getmaxyx(stdscr, h, w); }
void Screen::erase() { ::erase(); }
void Screen::refresh() {
  wnoutrefresh(stdscr); // Copy to virtual screen without refresh
  doupdate();           // Single efficient update
}
int Screen::wgetch() { return ::getch(); }
