#include "screen.h"
#include <curses.h>

Screen::Screen() {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
}

Screen::~Screen() { endwin(); }

void Screen::add_string(int y, int x, const char *str) { mvaddstr(y, x, str); }
void Screen::add_string(int y, int x, const char *str, int n) {
  mvaddnstr(y, x, str, n);
}
void Screen::get_dims(int &h, int &w) { getmaxyx(scr, h, w); }
void Screen::erase() { ::erase(); }
void Screen::refresh() { ::refresh(); }
int Screen::wgetch() { return ::getch(); }
