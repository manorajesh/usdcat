#include "renderer.h"
#include <pxr/imaging/hd/renderDelegate.h>

namespace pxr {
class HdTerminalDelegate final : public HdRenderDelegate {
public:
  HdTerminalDelegate(Renderer *renderer);
  ~HdTerminalDelegate() override = default;

  Renderer *GetRenderer() const { return _renderer; }

  const TfTokenVector &GetSupportedRprimTypes() const override;
  const TfTokenVector &GetSupportedSprimTypes() const override;
  const TfTokenVector &GetSupportedBprimTypes() const override;

  HdRenderPassSharedPtr
  CreateRenderPass(HdRenderIndex *index,
                   HdRprimCollection const &collection) override;
  HdRprim *CreateRprim(TfToken const &typeId, SdfPath const &primId) override;
  void DestroyRprim(HdRprim *rPrim) override;

  HdRenderParam *GetRenderParam() const override { return nullptr; }
  void CommitResources([[maybe_unused]] HdChangeTracker *tracker) override {}

  // mandatory overrides with empty implementations
  HdResourceRegistrySharedPtr GetResourceRegistry() const override {
    return nullptr;
  }
  HdSprim *CreateSprim([[maybe_unused]] TfToken const &typeId,
                       [[maybe_unused]] SdfPath const &primId) override {
    return nullptr;
  }
  HdSprim *
  CreateFallbackSprim([[maybe_unused]] TfToken const &typeId) override {
    return nullptr;
  }
  void DestroySprim([[maybe_unused]] HdSprim *sprim) override {}
  HdBprim *CreateBprim([[maybe_unused]] TfToken const &typeId,
                       [[maybe_unused]] SdfPath const &primId) override {
    return nullptr;
  }
  HdBprim *
  CreateFallbackBprim([[maybe_unused]] TfToken const &typeId) override {
    return nullptr;
  }
  void DestroyBprim([[maybe_unused]] HdBprim *bprim) override {}
  HdInstancer *CreateInstancer([[maybe_unused]] HdSceneDelegate *delegate,
                               [[maybe_unused]] SdfPath const &id) override {
    return nullptr;
  }
  void DestroyInstancer([[maybe_unused]] HdInstancer *instancer) override {}

private:
  Renderer *_renderer;
};

} // namespace pxr