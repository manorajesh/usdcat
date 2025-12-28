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

  HdResourceRegistrySharedPtr GetResourceRegistry() const override {
    return nullptr;
  }

  HdSprim *CreateSprim(TfToken const &typeId, SdfPath const &primId) override;

  HdSprim *CreateFallbackSprim(TfToken const &typeId) override;

  void DestroySprim(HdSprim *sprim) override;

  HdBprim *CreateBprim(TfToken const &typeId, SdfPath const &primId) override;

  HdBprim *CreateFallbackBprim(TfToken const &typeId) override;

  void DestroyBprim(HdBprim *bprim) override;

  HdInstancer *CreateInstancer([[maybe_unused]] HdSceneDelegate *delegate,
                               [[maybe_unused]] SdfPath const &id) override {
    return nullptr;
  }

  void DestroyInstancer([[maybe_unused]] HdInstancer *instancer) override {}

private:
  Renderer *_renderer;
};

} // namespace pxr
