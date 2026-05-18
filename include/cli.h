#pragma once
#include <string>

struct CliArgs {
    std::string usd_file;
    std::string renderer   = "halfblock"; // "halfblock" or "braille"
    std::string select_path;              // --select PRIMPATH
    std::string camera;                   // --camera CAMERA
    bool simple_mode = false;
    bool help        = false;
    bool valid       = true;
};

CliArgs parse_args(int argc, char **argv);
void    print_help(const char *prog);
