#include "camera_controller.h"
#include "delegate.h"
#include "frame_timer.h"
#include "render_task.h"
#include "renderer.h"
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

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);

// Signal handler for Ctrl+C
void signal_handler(int signal) {
  if (signal == SIGINT) {
    g_running = false;
  }
}

int main(int argc, char **argv) {
  std::signal(SIGINT, signal_handler);

  if (argc < 2) {
    printf("Usage: %s <path to usdz>\n", argv[0]);
    return -1;
  }

  RenderMode mode = RenderMode::HalfBlock;
  if (argc >= 3) {
    std::string mode_arg = argv[2];
    if (mode_arg == "-b") {
      mode = RenderMode::Braille;
    } else if (mode_arg == "-hb") {
      mode = RenderMode::HalfBlock;
    } else {
      printf("Unknown render mode: %s. Using default HalfBlock mode.\n",
             mode_arg.c_str());
    }
  }

  try {
    Renderer renderer(mode);

    auto stage = pxr::UsdStage::Open(argv[1]);
    if (!stage) {
      fprintf(stderr, "Error: Failed to open stage from '%s'\n", argv[1]);
      return -1;
    }

    {
      pxr::UsdEditTarget previousEditTarget = stage->GetEditTarget();
      stage->SetEditTarget(stage->GetSessionLayer());
      for (const auto &prim : stage->Traverse()) {
        if (prim.HasRelationship(pxr::TfToken("material:binding")) &&
            !prim.HasAPI<pxr::UsdShadeMaterialBindingAPI>()) {
          pxr::UsdShadeMaterialBindingAPI::Apply(prim);
        }
      }
      stage->SetEditTarget(previousEditTarget);
    }

    // camera setup
    pxr::SdfPath cameraPath;
    for (const auto &prim : stage->Traverse()) {
      if (prim.IsA<pxr::UsdGeomCamera>()) {
        cameraPath = prim.GetPath();
        break;
      }
    }

    if (cameraPath.IsEmpty()) {
      pxr::UsdGeomCamera camera =
          pxr::UsdGeomCamera::Define(stage, pxr::SdfPath("/Camera"));
      cameraPath = camera.GetPath();
    }

    pxr::UsdGeomCamera camera(stage->GetPrimAtPath(cameraPath));
    CameraController controller;
    controller.apply_orbit(camera);

    // delegate and render index setup
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

    int w{0}, h{0};
    FrameTimer frametimer; // moving average over 100 frames by default

    // initial frame to setup framebuffer size and camera
    renderer.screen.get_dims(h, w);
    engine.Execute(renderIndex, &tasks);
    if (controller.frame_to_meshes(renderer, camera, w, h)) {
      controller.apply_orbit(camera);
      sceneDelegate.ApplyPendingUpdates();
      engine.Execute(renderIndex, &tasks);
    }

    bool running = true;
    while (running && g_running) {
      frametimer.start();
      renderer.screen.get_dims(h, w);

      engine.Execute(renderIndex, &tasks);

      renderer.display_framebuffer();

      // --------

      std::string fps_text =
          "Frame Time: " + std::to_string(frametimer.frame_time_micros()) +
          "μs (" + std::to_string(frametimer.fps()) + " FPS)";
      fps_text.resize(50, ' ');
      renderer.screen.add_string(h - 2, 0, fps_text);
      renderer.screen.add_string(
          h - 1, 0, "Arrows: orbit | w/s: zoom | f: frame | q/Ctrl+C: quit");
      renderer.screen.refresh();
      frametimer.end();

      int c = renderer.screen.wgetch();
      if (controller.handle_input(c, renderer, camera, w, h, running)) {
        sceneDelegate.ApplyPendingUpdates();
      }
    }

    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "\nError: %s\n", e.what());
    return -1;
  } catch (...) {
    fprintf(stderr, "\nUnknown error occurred\n");
    return -1;
  }
}
