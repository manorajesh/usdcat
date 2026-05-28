#include "cli.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

// ── ANSI color helpers ────────────────────────────────────────────────────────

static bool use_color() {
    return isatty(STDOUT_FILENO);
}

#define C(code) (color ? "\033[" code "m" : "")

// ── Help layout constants ─────────────────────────────────────────────────────

static constexpr int DESC_COL = 26; // column where descriptions start

// Print one option row. `flag_vis` is the visual (non-ANSI) width of the flag
// string so we can decide whether the description fits on the same line.
static void opt(const char *flags_colored,
                int          flag_vis_len,
                const char *meta_colored,  // pass "" for none
                int          meta_vis_len,
                const char *desc) {
    // +1 for the space before meta
    int used = 2 + flag_vis_len + (meta_vis_len ? 1 + meta_vis_len : 0);
    const char *sp = meta_vis_len ? " " : "";
    if (used < DESC_COL) {
        int pad = DESC_COL - used;
        printf("  %s%s%s%*s%s\n",
               flags_colored, sp, meta_colored,
               pad, "", desc);
    } else {
        // Flag+meta too long: description on next line (or empty if desc=="")
        if (desc[0])
            printf("  %s%s%s\n%*s%s\n",
                   flags_colored, sp, meta_colored,
                   DESC_COL, "", desc);
        else
            printf("  %s%s%s\n",
                   flags_colored, sp, meta_colored);
    }
}

// Wrap and print a continuation description line at DESC_COL indent.
static void cont(const char *text) {
    printf("%*s%s\n", DESC_COL, "", text);
}

// ── Public API ────────────────────────────────────────────────────────────────

void print_help(const char *prog) {
    bool color = use_color();

    // Colors
    const char *R    = C("0");
    const char *BD   = C("1");
    const char *DM   = C("2");
    const char *BCY  = C("1;36");  // bold cyan
    const char *BYL  = C("1;33");  // bold yellow
    const char *BGN  = C("1;32");  // bold green
    const char *CY   = C("36");    // cyan
    const char *BLD  = C("1");     // bold white

    // ── Header ───────────────────────────────────────────────────────────────
    printf("\n%susdless%s  Terminal USD Viewer\n", BCY, R);
    printf("%s%s%s\n\n",
           DM,
           "────────────────────────────────────────────────────────────────────────────",
           R);

    // ── Usage ─────────────────────────────────────────────────────────────────
    printf("%susage:%s %s%s%s [%susdFile%s] [%soptions%s]\n",
           BD, R,
           BLD, prog, R,
           BD, R,
           DM, R);
    printf("       %scat scene.usd | %s%s%s [%soptions%s]\n\n",
           DM,
           BLD, prog, R,
           DM, R);

    printf("View a USD file in the terminal. Reads from stdin when no file is given.\n\n");

    // ── Positional ───────────────────────────────────────────────────────────
    printf("%spositional arguments:%s\n", BYL, R);

    {   // usdFile
        char flag[64];
        snprintf(flag, sizeof(flag), "%susdFile%s", BD, R);
        opt(flag, 7, "", 0, "USD file to view (.usd, .usda, .usdz)");
    }
    printf("\n");

    // ── Options ───────────────────────────────────────────────────────────────
    printf("%soptions:%s\n", BYL, R);

    // -h / --help
    {
        char flag[128];
        snprintf(flag, sizeof(flag), "%s-h%s, %s--help%s", BGN, R, BGN, R);
        opt(flag, 10, "", 0, "show this help message and exit");
    }

    // --renderer / -r
    {
        char flag[128], meta[64];
        snprintf(flag, sizeof(flag), "%s-r%s, %s--renderer%s", BGN, R, BGN, R);
        snprintf(meta, sizeof(meta), "%s{halfblock,braille}%s", CY, R);
        opt(flag, 14, meta, 19, "render mode for the 3D viewport.");
        printf("%*s  %shalfblock%s  Unicode half-block characters, fast, full 24-bit color.\n",
               DESC_COL, "", BD, R);
        printf("%*s  %sbraille%s    Braille dot patterns, finer pixel grid at reduced color depth.\n",
               DESC_COL, "", BD, R);
        printf("%*s  %s(default: halfblock)%s\n", DESC_COL, "", DM, R);
    }

    // --simple / -s
    {
        char flag[128];
        snprintf(flag, sizeof(flag), "%s-s%s, %s--simple%s", BGN, R, BGN, R);
        opt(flag, 12, "", 0, "bypass the TUI; fullscreen 3D view only. recommended for");
        cont("slow SSH connections or weaker terminals.");
    }

    // --select
    {
        char flag[128], meta[64];
        snprintf(flag, sizeof(flag), "    %s--select%s", BGN, R);
        snprintf(meta, sizeof(meta), "%sPRIMPATH%s", CY, R);
        opt(flag, 12, meta, 8, "a prim path to initially select in the scene tree");
        cont("(e.g. /World/Geom/Cube).");
    }

    // --camera
    {
        char flag[128], meta[64];
        snprintf(flag, sizeof(flag), "    %s--camera%s", BGN, R);
        snprintf(meta, sizeof(meta), "%sCAMERA%s", CY, R);
        opt(flag, 12, meta, 6, "which camera to set the view to on open. may be given");
        cont("as a prim name or full prim path. (default: first camera found)");
    }

    // ── Key bindings (TUI mode) ───────────────────────────────────────────────
    printf("\n%skey bindings (TUI mode):%s\n", BYL, R);

    struct { const char *key; const char *desc; } keys[] = {
        { "Tab",              "switch focus between scene tree and 3D view" },
        { "`",               "toggle fullscreen 3D / split TUI layout"      },
        { "Arrow keys",       "orbit camera (3D focus) / navigate tree (tree focus)" },
        { "Enter",            "select prim in scene tree"                   },
        { "Left / Right",     "collapse / expand node in scene tree"        },
        { "w / s",            "zoom in / out"                               },
        { "f",                "frame all meshes"                            },
        { "q / Ctrl+C",       "quit"                                        },
    };
    for (const auto &k : keys) {
        char kfmt[64];
        snprintf(kfmt, sizeof(kfmt), "%s%s%s", BD, k.key, R);
        // Visual length of key name (no ANSI codes)
        int vis = (int)strlen(k.key);
        opt(kfmt, vis, "", 0, k.desc);
    }
    printf("\n");
}

CliArgs parse_args(int argc, char **argv) {
    CliArgs args;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            args.help = true;

        } else if (strcmp(a, "-r") == 0 || strcmp(a, "--renderer") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires an argument\n", a);
                args.valid = false;
            } else {
                args.renderer = argv[++i];
                if (args.renderer != "halfblock" && args.renderer != "braille") {
                    fprintf(stderr, "error: unknown renderer '%s' (choose halfblock or braille)\n",
                            args.renderer.c_str());
                    args.valid = false;
                }
            }

        // Legacy short flags kept for backward compatibility
        } else if (strcmp(a, "-b")  == 0) { args.renderer = "braille";
        } else if (strcmp(a, "-hb") == 0) { args.renderer = "halfblock";

        } else if (strcmp(a, "-s") == 0 || strcmp(a, "--simple") == 0) {
            args.simple_mode = true;

        } else if (strcmp(a, "--select") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --select requires a prim path argument\n");
                args.valid = false;
            } else {
                args.select_path = argv[++i];
            }

        } else if (strcmp(a, "--camera") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --camera requires an argument\n");
                args.valid = false;
            } else {
                args.camera = argv[++i];
            }

        } else if (a[0] == '-') {
            fprintf(stderr, "error: unrecognized argument: %s\n", a);
            args.valid = false;

        } else {
            // Positional: first non-flag arg is the USD file
            if (args.usd_file.empty()) {
                args.usd_file = a;
            } else {
                fprintf(stderr, "error: unexpected positional argument: %s\n", a);
                args.valid = false;
            }
        }
    }

    return args;
}
