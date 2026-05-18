#include "tui_state.h"
#include <algorithm>
#include <pxr/usd/usd/primRange.h>

void TuiState::build_tree(const pxr::UsdStageRefPtr &stage) {
    all_nodes.clear();
    for (const auto &prim : stage->Traverse()) {
        const pxr::SdfPath &path = prim.GetPath();
        int depth = (int)path.GetPathElementCount() - 1;
        FullNode fn;
        fn.path        = path;
        fn.name        = path.GetName();
        fn.typeName    = prim.GetTypeName().GetString();
        fn.depth       = depth;
        fn.expanded    = true;
        fn.hasChildren = !prim.GetAllChildren().empty();
        all_nodes.push_back(fn);
    }
    rebuild_flat();
}

void TuiState::rebuild_flat() {
    flat_nodes.clear();
    std::vector<bool> collapsed(64, false);

    for (const auto &n : all_nodes) {
        bool hidden = false;
        for (int d = 0; d < n.depth && d < 64; ++d) {
            if (collapsed[d]) { hidden = true; break; }
        }
        if (hidden) continue;

        PrimNode pn;
        pn.path        = n.path;
        pn.name        = n.name;
        pn.typeName    = n.typeName;
        pn.depth       = n.depth;
        pn.expanded    = n.expanded;
        pn.hasChildren = n.hasChildren;
        flat_nodes.push_back(pn);

        if (n.depth < 64) {
            collapsed[n.depth] = !n.expanded;
            // Reset deeper levels to avoid bleed-through between siblings
            for (int d = n.depth + 1; d < 64; ++d)
                collapsed[d] = false;
        }
    }

    if (cursor >= (int)flat_nodes.size())
        cursor = std::max(0, (int)flat_nodes.size() - 1);
}

void TuiState::compute_layout(int w, int h) {
    term_w  = w;
    term_h  = h;
    tree_h  = std::max(2, (int)((float)h * TREE_FRAC));
    attr_h  = h - tree_h;
    render_x = PANEL_W;
    render_w = std::max(1, w - PANEL_W);
}

void TuiState::clamp_scroll(int visible_rows) {
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + visible_rows) scroll = cursor - visible_rows + 1;
    scroll = std::max(0, scroll);
}

void TuiState::set_expanded(const pxr::SdfPath &path, bool expanded) {
    for (auto &n : all_nodes) {
        if (n.path == path) {
            n.expanded = expanded;
            break;
        }
    }
    rebuild_flat();
}
