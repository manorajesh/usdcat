#pragma once
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <algorithm>
#include <string>
#include <vector>

struct TimelineState {
    bool enabled = false;
    bool playing = false;
    double start = 0.0;
    double end = 0.0;
    double current = 0.0;
    double time_codes_per_second = 24.0;
    double playback_rate = 1.0;

    bool has_range() const { return enabled && end > start; }
    double duration() const { return has_range() ? end - start : 0.0; }

    void clamp_current() {
        if (!has_range()) return;
        current = std::clamp(current, start, end);
    }

    void step(double frames) {
        if (!has_range()) return;
        current += frames;
        clamp_current();
    }

    void advance_seconds(double seconds) {
        if (!has_range() || !playing) return;
        current += seconds * time_codes_per_second * playback_rate;
        while (current > end) current = start + (current - end);
        while (current < start) current = end - (start - current);
        clamp_current();
    }
};

struct PrimNode {
    pxr::SdfPath path;
    std::string  name;
    std::string  typeName;
    int          depth;
    bool         expanded;
    bool         hasChildren;
};

enum class TuiPanel { Tree, View };

struct TuiState {
    int panel_w = 26;
    static constexpr float TREE_FRAC = 0.60f;

    int term_w = 0, term_h = 0;
    int tree_h = 0, attr_h = 0;
    int render_x = 0, render_w = 0;

    TuiPanel focus      = TuiPanel::View;
    bool     fullscreen = false;
    bool     help_visible = false;

    std::vector<PrimNode> flat_nodes;
    int cursor = 0, scroll = 0;
    pxr::SdfPath selected_path;

    bool panels_dirty = true;
    bool render_dirty = true;
    TimelineState timeline;

    void build_tree(const pxr::UsdStageRefPtr &stage);
    void rebuild_flat();
    void compute_layout(int w, int h);
    void clamp_scroll(int visible_rows);
    void set_expanded(const pxr::SdfPath &path, bool expanded);

private:
    struct FullNode {
        pxr::SdfPath path;
        std::string  name, typeName;
        int          depth;
        bool         expanded    = true;
        bool         hasChildren = false;
    };
    std::vector<FullNode> all_nodes;
};
