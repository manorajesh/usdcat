#include "delegate.h"
#include "mesh.h"
#include "render_pass.h"
#include "renderer.h"
#include <pxr/imaging/hd/bprim.h>
#include <pxr/imaging/hd/camera.h>

namespace pxr {

HdTerminalDelegate::HdTerminalDelegate(Renderer *renderer)
    : _renderer(renderer) {}

const TfTokenVector &HdTerminalDelegate::GetSupportedRprimTypes() const {
  static const TfTokenVector rprims = {HdPrimTypeTokens->mesh};
  return rprims;
}

HdRprim *HdTerminalDelegate::CreateRprim(TfToken const &typeId,
                                         SdfPath const &primId) {
  if (typeId == HdPrimTypeTokens->mesh) {
    return new HdTerminalMesh(primId);
  }
  return nullptr;
}

HdRenderPassSharedPtr
HdTerminalDelegate::CreateRenderPass(HdRenderIndex *index,
                                     HdRprimCollection const &collection) {
  return HdRenderPassSharedPtr(new HdTerminalRenderPass(index, collection));
}

void HdTerminalDelegate::DestroyRprim(HdRprim *rPrim) { delete rPrim; }

HdSprim *HdTerminalDelegate::CreateSprim(TfToken const &typeId,
                                         SdfPath const &primId) {
  if (typeId == HdPrimTypeTokens->camera) {
    return new HdCamera(primId);
  }
  return nullptr;
}

HdSprim *HdTerminalDelegate::CreateFallbackSprim(TfToken const &typeId) {
  if (typeId == HdPrimTypeTokens->camera) {
    return new HdCamera(SdfPath::EmptyPath());
  }
  return nullptr;
}

void HdTerminalDelegate::DestroySprim(HdSprim *sprim) { delete sprim; }

HdBprim *HdTerminalDelegate::CreateBprim(TfToken const &typeId,
                                         SdfPath const &primId) {
  (void)typeId;
  (void)primId;
  return nullptr;
}

HdBprim *HdTerminalDelegate::CreateFallbackBprim(TfToken const &typeId) {
  (void)typeId;
  return nullptr;
}

void HdTerminalDelegate::DestroyBprim(HdBprim *bprim) { delete bprim; }

const TfTokenVector &HdTerminalDelegate::GetSupportedSprimTypes() const {
  static const TfTokenVector sprims = {HdPrimTypeTokens->camera};
  return sprims;
}
const TfTokenVector &HdTerminalDelegate::GetSupportedBprimTypes() const {
  static TfTokenVector v;
  return v;
}

} // namespace pxr
