#include "screen.h"
#include <curses.h>

Screen::Screen() {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
}

Screen::~Screen() { endwin(); }

void Screen::addString(int y, int x, const char *str) { mvaddstr(y, x, str); }
void Screen::clear() { ::clear(); }
void Screen::refresh() { ::refresh(); }
int Screen::wgetch() { return ::getch(); }