#pragma once
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>

namespace pxr {

class HdTerminalRenderTask final : public HdTask {
public:
  HdTerminalRenderTask(HdRenderPassSharedPtr const &renderPass,
                       SdfPath const &id, HdRenderIndex *renderIndex,
                       SdfPath const &cameraPath)
      : HdTask(id), _renderPass(renderPass), _renderIndex(renderIndex),
        _cameraPath(cameraPath) {
    _renderPassState = std::make_shared<HdRenderPassState>();
  }

  void Sync([[maybe_unused]] HdSceneDelegate *delegate,
            [[maybe_unused]] HdTaskContext *ctx, // Correct type
            [[maybe_unused]] HdDirtyBits *dirtyBits) override {
    _renderPass->Sync();
    if (!_renderIndex) {
      return;
    }

    HdCamera *camera = nullptr;
    if (!_cameraPath.IsEmpty()) {
      HdSprim *sprim =
          _renderIndex->GetSprim(HdPrimTypeTokens->camera, _cameraPath);
      camera = dynamic_cast<HdCamera *>(sprim);
    }

    if (!camera) {
      camera = dynamic_cast<HdCamera *>(
          _renderIndex->GetFallbackSprim(HdPrimTypeTokens->camera));
    }

    _renderPassState->SetCamera(camera);
  }

  void Prepare([[maybe_unused]] HdTaskContext *ctx,
               [[maybe_unused]] HdRenderIndex *renderIndex) override {}
  void Execute([[maybe_unused]] HdTaskContext *ctx) override {
    _renderPass->Execute(_renderPassState, GetRenderTags());
  }

  const TfTokenVector &GetRenderTags() const override {
    static const TfTokenVector tags = {HdTokens->geometry};
    return tags;
  }

private:
  HdRenderPassSharedPtr _renderPass;
  HdRenderPassStateSharedPtr _renderPassState;
  HdRenderIndex *_renderIndex = nullptr;
  SdfPath _cameraPath;
};

} // namespace pxr
