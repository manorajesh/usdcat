#include "delegate.h"
#include "mesh.h"
#include "render_pass.h"
#include "renderer.h"

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

// Stubs for types we don't use yet
const TfTokenVector &HdTerminalDelegate::GetSupportedSprimTypes() const {
  static TfTokenVector v;
  return v;
}
const TfTokenVector &HdTerminalDelegate::GetSupportedBprimTypes() const {
  static TfTokenVector v;
  return v;
}

} // namespace pxr