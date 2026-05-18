#include "panel_renderer.h"
#include "mesh.h"
#include <algorithm>
#include <cstdio>
#include <string>

// Content width = PANEL_W minus 1 for left margin and 1 for the right border
static constexpr int CONTENT_W = TuiState::PANEL_W - 2;

std::string PanelRenderer::truncate(const std::string &s, int max_len) {
    if (max_len <= 0) return {};
    if ((int)s.size() <= max_len) return s;
    return s.substr(0, (size_t)max_len);
}

void PanelRenderer::draw_row(Screen &screen, int row, int content_w,
                              const std::string &text, bool highlighted, bool focused) {
    std::string padded = truncate(text, content_w);
    while ((int)padded.size() < content_w) padded += ' ';

    std::string out;
    out.reserve(padded.size() + 32);
    if (highlighted) {
        out += focused ? "\033[48;2;40;60;100m" : "\033[48;2;50;50;55m";
    }
    out += padded;
    out += "\033[0m";
    screen.add_string(row, 1, out.c_str());
}

void PanelRenderer::draw_borders(const TuiState &tui, Screen &screen) {
    const char *dim = "\033[38;2;80;80;90m";
    const char *rst = "\033[0m";

    // Vertical divider at column PANEL_W-1 for every row
    std::string vert = std::string(dim) + "│" + rst;
    for (int row = 0; row < tui.term_h; ++row)
        screen.add_string(row, TuiState::PANEL_W - 1, vert.c_str());

    // Horizontal divider between tree and attrs
    std::string hline = std::string(dim);
    for (int i = 0; i < TuiState::PANEL_W - 2; ++i) hline += "\xe2\x94\x80"; // ─
    hline += rst;
    screen.add_string(tui.tree_h, 0, hline.c_str());

    // Left-tee connector at (tree_h, PANEL_W-1)
    std::string connector = std::string(dim) + "\xe2\x94\x9c" + rst; // ├
    screen.add_string(tui.tree_h, TuiState::PANEL_W - 1, connector.c_str());
}

void PanelRenderer::draw_tree(const TuiState &tui, Screen &screen) {
    bool focused = (tui.focus == TuiPanel::Tree);

    // Title row (row 0)
    {
        const char *col = focused ? "\033[1;38;2;120;180;255m" : "\033[1;38;2;100;100;130m";
        std::string title = std::string(col) + " Scene Tree " + "\033[0m";
        // Clear row background first
        std::string blank(TuiState::PANEL_W - 1, ' ');
        screen.add_string(0, 0, blank.c_str());
        screen.add_string(0, 0, title.c_str());
    }

    const int visible_rows = tui.tree_h - 1;
    const int n = (int)tui.flat_nodes.size();

    for (int i = 0; i < visible_rows; ++i) {
        int row = i + 1;
        int node_idx = tui.scroll + i;
        bool highlighted = (node_idx == tui.cursor);

        if (node_idx >= n) {
            std::string blank(CONTENT_W, ' ');
            screen.add_string(row, 1, blank.c_str());
            continue;
        }

        const PrimNode &pn = tui.flat_nodes[node_idx];
        std::string text;

        // Indent (cap at 12 to preserve name space)
        int indent = std::min(pn.depth * 2, 12);
        text.append((size_t)indent, ' ');

        // Expand/collapse indicator
        if (pn.hasChildren) {
            // ▾ = \xe2\x96\xbe  ▸ = \xe2\x96\xb8
            text += pn.expanded ? "\xe2\x96\xbe " : "\xe2\x96\xb8 ";
        } else {
            text += "  ";
        }

        // Name + type tag (byte-counting truncation; accepts minor ±1 col imprecision for multi-byte arrows)
        std::string type_tag = " [" + (pn.typeName.empty() ? "?" : pn.typeName) + "]";
        int arrow_bytes = 3 + 1; // 3-byte UTF-8 arrow + space
        int used = indent + arrow_bytes;
        int name_budget = CONTENT_W - used - (int)type_tag.size();
        if (name_budget > 0) {
            text += truncate(pn.name, name_budget) + type_tag;
        } else {
            text += truncate(pn.name, std::max(1, CONTENT_W - used));
        }

        draw_row(screen, row, CONTENT_W, text, highlighted, focused);
    }
}

void PanelRenderer::draw_attrs(const TuiState &tui, Screen &screen, const Renderer &renderer) {
    // Title row
    {
        std::string title = "\033[1;38;2;100;100;130m Attributes \033[0m";
        std::string blank(TuiState::PANEL_W - 1, ' ');
        screen.add_string(tui.tree_h + 1, 0, blank.c_str());
        screen.add_string(tui.tree_h + 1, 0, title.c_str());
    }

    int row = tui.tree_h + 2;
    const int max_row = tui.term_h - 1;

    auto add_row = [&](const std::string &text) {
        if (row >= max_row) return;
        std::string padded = truncate(text, CONTENT_W);
        while ((int)padded.size() < CONTENT_W) padded += ' ';
        screen.add_string(row++, 1, padded.c_str());
    };

    if (tui.selected_path.IsEmpty()) {
        add_row("(no selection)");
        while (row < max_row) add_row("");
        return;
    }

    add_row("Path: " + tui.selected_path.GetString());

    const auto &meshes    = renderer.get_meshes();
    const auto &materials = renderer.get_materials();

    auto mesh_it = meshes.find(tui.selected_path);
    if (mesh_it != meshes.end()) {
        const MeshData &m = mesh_it->second;
        add_row("Type: Mesh");
        add_row("Verts: " + std::to_string(m.vertices.size()));
        add_row("Tris:  " + std::to_string(m.indices.size()));

        if (!m.materialId.IsEmpty()) {
            add_row("Mat:  " + m.materialId.GetName());
            auto mat_it = materials.find(m.materialId);
            if (mat_it != materials.end()) {
                const MaterialData &mat = mat_it->second;
                char buf[48];
                snprintf(buf, sizeof(buf), "Color: %.2f %.2f %.2f",
                         mat.baseColor.x(), mat.baseColor.y(), mat.baseColor.z());
                add_row(buf);
                snprintf(buf, sizeof(buf), "Metal: %.2f", mat.metallic);
                add_row(buf);
                snprintf(buf, sizeof(buf), "Rough: %.2f", mat.roughness);
                add_row(buf);
            }
        }
    } else {
        std::string type = tui.selected_path.GetName();
        add_row("Type: " + (type.empty() ? "Prim" : type));
    }

    while (row < max_row) add_row("");
}
