#pragma once
#include <Eigen/Dense>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <vector>

struct MeshData {
  std::vector<Eigen::Vector3f> vertices;
  std::vector<Eigen::Vector3i> indices; // x, y, z as vertex IDs
  Eigen::Matrix4f worldTransform = Eigen::Matrix4f::Identity();
};

namespace pxr {

class HdTerminalMesh final : public HdMesh {
public:
  HdTerminalMesh(SdfPath const &id) : HdMesh(id) {}

  // This is the core function where USD data flows into your renderer
  void Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam,
            HdDirtyBits *dirtyBits, TfToken const &reprToken) override;

  HdDirtyBits GetInitialDirtyBitsMask() const override {
    return HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyTransform;
  }

protected:
  void _InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits) override;
  HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override {
    return bits;
  }
};

} // namespace pxr