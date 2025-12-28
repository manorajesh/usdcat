#pragma once
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>

namespace pxr {

class HdTerminalRenderTask final : public HdTask {
public:
  HdTerminalRenderTask(HdRenderPassSharedPtr const &renderPass,
                       SdfPath const &id)
      : HdTask(id), _renderPass(renderPass) {
    _renderPassState = std::make_shared<HdRenderPassState>();
  }

  void Sync([[maybe_unused]] HdSceneDelegate *delegate,
            [[maybe_unused]] HdTaskContext *ctx, // Correct type
            [[maybe_unused]] HdDirtyBits *dirtyBits) override {
    _renderPass->Sync();
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
};

} // namespace pxr