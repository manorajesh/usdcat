#include "camera_controller.h"
#include "cli.h"
#include "delegate.h"
#include "frame_timer.h"
#include "panel_renderer.h"
#include "render_task.h"
#include "renderer.h"
#include "tui_state.h"
#include <atomic>
#include <csignal>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usdImaging/usdImaging/delegate.h>

std::atomic<bool> g_running(true);

void signal_handler(int signal) {
  if (signal == SIGINT) {
    g_running = false;
  }
}

int main(int argc, char **argv) {
  std::signal(SIGINT, signal_handler);

  CliArgs args = parse_args(argc, argv);

  if (args.help) {
    print_help(argv[0]);
    return 0;
  }

  if (!args.valid) {
    fprintf(stderr, "Run '%s --help' for usage.\n", argv[0]);
    return 1;
  }

  if (args.usd_file.empty()) {
    print_help(argv[0]);
    return 1;
  }

  RenderMode mode = (args.renderer == "braille") ? RenderMode::Braille
                                                  : RenderMode::HalfBlock;

  try {
    Renderer renderer(mode);

    auto stage = pxr::UsdStage::Open(args.usd_file);
    if (!stage) {
      fprintf(stderr, "error: failed to open '%s'\n", args.usd_file.c_str());
      return 1;
    }

    // Apply material bindings
    {
      pxr::UsdEditTarget prev = stage->GetEditTarget();
      stage->SetEditTarget(stage->GetSessionLayer());
      for (const auto &prim : stage->Traverse()) {
        if (prim.HasRelationship(pxr::TfToken("material:binding")) &&
            !prim.HasAPI<pxr::UsdShadeMaterialBindingAPI>()) {
          pxr::UsdShadeMaterialBindingAPI::Apply(prim);
        }
      }
      stage->SetEditTarget(prev);
    }

    // Camera setup: honour --camera arg, else use first camera found
    pxr::SdfPath cameraPath;
    for (const auto &prim : stage->Traverse()) {
      if (!prim.IsA<pxr::UsdGeomCamera>()) continue;
      if (args.camera.empty()) {
        cameraPath = prim.GetPath();
        break;
      }
      // Match by full path or by prim name (last element)
      if (prim.GetPath().GetString() == args.camera ||
          prim.GetName()              == args.camera) {
        cameraPath = prim.GetPath();
        break;
      }
    }
    if (cameraPath.IsEmpty()) {
      pxr::UsdGeomCamera cam =
          pxr::UsdGeomCamera::Define(stage, pxr::SdfPath("/Camera"));
      cameraPath = cam.GetPath();
    }

    pxr::UsdGeomCamera camera(stage->GetPrimAtPath(cameraPath));
    CameraController controller;
    controller.apply_orbit(camera);

    // Hydra pipeline
    pxr::HdTerminalDelegate renderDelegate(&renderer);
    pxr::HdRenderIndex *renderIndex =
        pxr::HdRenderIndex::New(&renderDelegate, {});

    pxr::UsdImagingDelegate sceneDelegate(renderIndex,
                                          pxr::SdfPath::AbsoluteRootPath());
    sceneDelegate.Populate(stage->GetPseudoRoot());
    sceneDelegate.SetCameraForSampling(cameraPath);

    pxr::HdRprimCollection collection(
        pxr::HdTokens->geometry,
        pxr::HdReprSelector(pxr::HdReprTokens->refined));
    collection.SetRootPath(pxr::SdfPath::AbsoluteRootPath());

    pxr::HdRenderPassSharedPtr renderPass =
        renderDelegate.CreateRenderPass(renderIndex, collection);

    pxr::SdfPath taskPath("/renderTask");
    pxr::HdTaskSharedPtr renderTask =
        std::make_shared<pxr::HdTerminalRenderTask>(renderPass, taskPath,
                                                    renderIndex, cameraPath);
    pxr::HdTaskSharedPtrVector tasks = {renderTask};
    pxr::HdEngine engine;

    // Initial frame: get dims, set viewport, populate meshes, frame camera
    int w{0}, h{0};
    renderer.screen.get_dims(h, w);

    TuiState tui;
    if (args.simple_mode) {
      renderer.set_viewport(0, 0, w, h);
    } else {
      tui.compute_layout(w, h);
      renderer.set_viewport(tui.render_x, 0, tui.render_w, h);
    }

    engine.Execute(renderIndex, &tasks);
    int frame_w = args.simple_mode ? w : tui.render_w;
    if (controller.frame_to_meshes(renderer, camera, frame_w, h)) {
      controller.apply_orbit(camera);
      sceneDelegate.ApplyPendingUpdates();
      engine.Execute(renderIndex, &tasks);
    }

    // Build scene tree and apply --select if requested
    if (!args.simple_mode) {
      tui.build_tree(stage);

      if (!args.select_path.empty()) {
        pxr::SdfPath sel(args.select_path);
        for (int i = 0; i < (int)tui.flat_nodes.size(); ++i) {
          if (tui.flat_nodes[i].path == sel) {
            tui.cursor        = i;
            tui.selected_path = sel;
            tui.clamp_scroll(tui.tree_h - 1);
            break;
          }
        }
      }
    }

    // Main render loop
    bool running = true;
    FrameTimer frametimer;
    PanelRenderer panel_renderer;
    int prev_w = w, prev_h = h;

    while (running && g_running) {
      frametimer.start();
      renderer.screen.get_dims(h, w);

      // Detect terminal resize
      if (w != prev_w || h != prev_h) {
        prev_w = w; prev_h = h;
        renderer.screen.clear();
        tui.render_dirty = true;
        tui.panels_dirty = true;
      }

      if (args.simple_mode) {
        renderer.set_viewport(0, 0, w, h);
        engine.Execute(renderIndex, &tasks);
        renderer.display_framebuffer();
      } else {
        tui.compute_layout(w, h);
        if (tui.fullscreen) {
          renderer.set_viewport(0, 0, w, h);
        } else {
          renderer.set_viewport(tui.render_x, 0, tui.render_w, h);
        }

        if (tui.render_dirty) {
          engine.Execute(renderIndex, &tasks);
          renderer.display_framebuffer();
          tui.render_dirty = false;
        }

        if (tui.panels_dirty && !tui.fullscreen) {
          panel_renderer.draw_borders(tui, renderer.screen);
          panel_renderer.draw_tree(tui, renderer.screen);
          panel_renderer.draw_attrs(tui, renderer.screen, renderer);
          tui.panels_dirty = false;
        }
      }

      // Status bar
      std::string fps_text =
          "Frame: " + std::to_string(frametimer.frame_time_micros()) +
          "us (" + std::to_string(frametimer.fps()) + " FPS)";
      fps_text.resize(40, ' ');
      int hud_col = (args.simple_mode || tui.fullscreen) ? 0 : tui.render_x;
      renderer.screen.add_string(h - 2, hud_col, fps_text.c_str());

      const char *controls =
          args.simple_mode
          ? "Arrows: orbit | w/s: zoom | f: frame | q: quit"
          : "Tab: panel | `: fullscreen | Arrows/Enter: nav | q: quit";
      renderer.screen.add_string(h - 1, hud_col, controls);
      renderer.screen.refresh();
      frametimer.end();

      // Input
      int c = renderer.screen.wgetch();

      if (c == 'q' || c == 3) { running = false; continue; }

      if (args.simple_mode) {
        if (controller.handle_input(c, renderer, camera, w, h, running))
          sceneDelegate.ApplyPendingUpdates();
        continue;
      }

      // TUI key dispatch
      if (c == '\t') {
        tui.focus = (tui.focus == TuiPanel::Tree) ? TuiPanel::View : TuiPanel::Tree;
        tui.panels_dirty = true;
      } else if (c == '`') {
        tui.fullscreen = !tui.fullscreen;
        renderer.screen.clear();
        tui.render_dirty = true;
        tui.panels_dirty = true;
      } else if (tui.focus == TuiPanel::Tree) {
        bool changed = false;
        switch (c) {
        case KEY_UP:
          if (tui.cursor > 0) { --tui.cursor; changed = true; }
          break;
        case KEY_DOWN:
          if (tui.cursor < (int)tui.flat_nodes.size() - 1) {
            ++tui.cursor; changed = true;
          }
          break;
        case KEY_LEFT:
          if (!tui.flat_nodes.empty()) {
            const PrimNode &pn = tui.flat_nodes[tui.cursor];
            if (pn.hasChildren && pn.expanded) {
              tui.set_expanded(pn.path, false);
              changed = true;
            }
          }
          break;
        case KEY_RIGHT:
          if (!tui.flat_nodes.empty()) {
            const PrimNode &pn = tui.flat_nodes[tui.cursor];
            if (pn.hasChildren && !pn.expanded) {
              tui.set_expanded(pn.path, true);
              changed = true;
            }
          }
          break;
        case '\r':
        case '\n':
          if (!tui.flat_nodes.empty()) {
            tui.selected_path = tui.flat_nodes[tui.cursor].path;
            changed = true;
          }
          break;
        default:
          break;
        }
        if (changed) {
          tui.clamp_scroll(tui.tree_h - 1);
          tui.panels_dirty = true;
        }
      } else {
        int view_w = tui.fullscreen ? w : tui.render_w;
        if (controller.handle_input(c, renderer, camera, view_w, h, running)) {
          sceneDelegate.ApplyPendingUpdates();
          tui.render_dirty = true;
        }
      }
    }

    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "\nerror: %s\n", e.what());
    return 1;
  } catch (...) {
    fprintf(stderr, "\nunknown error\n");
    return 1;
  }
}
