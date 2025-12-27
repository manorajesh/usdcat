#pragma once
#include <pxr/imaging/hd/renderPass.h>

namespace pxr {
class HdTerminalRenderPass final : public HdRenderPass {
public:
  HdTerminalRenderPass(HdRenderIndex *index,
                       HdRprimCollection const &collection)
      : HdRenderPass(index, collection) {}

  void _Execute(HdRenderPassStateSharedPtr const &renderPassState,
                TfTokenVector const &renderTags);
};
} // namespace pxr