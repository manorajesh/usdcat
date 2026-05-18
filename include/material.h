#pragma once
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hio/image.h>

namespace pxr {

class HdTerminalMaterial : public HdMaterial {
public:
    HdTerminalMaterial(SdfPath const &id);
    ~HdTerminalMaterial() override = default;

    // The core sync function
    void Sync(HdSceneDelegate *sceneDelegate,
              HdRenderParam *renderParam,
              HdDirtyBits *dirtyBits) override;

    // Required pure virtuals
    HdDirtyBits GetInitialDirtyBitsMask() const override { 
        return HdChangeTracker::AllDirty; 
    }
    
    void Finalize(HdRenderParam * /*renderParam*/) override {}
};

} // namespace pxr
