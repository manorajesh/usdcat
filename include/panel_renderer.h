#pragma once
#include "renderer.h"
#include "screen.h"
#include "tui_state.h"
#include <string>

class PanelRenderer {
public:
    void draw_borders(const TuiState &tui, Screen &screen);
    void draw_tree(const TuiState &tui, Screen &screen);
    void draw_attrs(const TuiState &tui, Screen &screen, const Renderer &renderer);

private:
    void draw_row(Screen &screen, int row, int content_w,
                  const std::string &text, bool highlighted, bool focused);
    static std::string truncate(const std::string &s, int max_len);
};
