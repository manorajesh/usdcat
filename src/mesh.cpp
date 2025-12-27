#include "mesh.h"
#include "delegate.h"
#include "mesh.h"
#include "renderer.h"
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/usdGeom/xformable.h>

namespace pxr {

void HdTerminalMesh::_InitRepr(TfToken const &reprToken,
                               HdDirtyBits *dirtyBits) {
  // This tells Hydra: "If we are drawing smoothHull, we need Points and
  // Topology"
  if (reprToken == HdReprTokens->smoothHull) {
    *dirtyBits |= HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology;
  }
}

void HdTerminalMesh::Sync(HdSceneDelegate *sceneDelegate,
                          HdRenderParam *renderParam, HdDirtyBits *dirtyBits,
                          TfToken const &reprToken) {
  auto id = GetId();
  HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
  HdTerminalDelegate *delegate =
      static_cast<HdTerminalDelegate *>(renderIndex.GetRenderDelegate());
  Renderer *renderer = delegate->GetRenderer();

  // Check if we already have this mesh in the renderer to handle initial load
  bool isNew = renderer->get_meshes().find(id) == renderer->get_meshes().end();

  // Use a local MeshData so we don't overwrite partial updates
  MeshData &data = renderer->get_meshes()[id];

  // 1. Update Points (or initial load)
  if (isNew || (*dirtyBits & HdChangeTracker::DirtyPoints)) {
    VtValue value = sceneDelegate->Get(id, HdTokens->points);
    if (value.IsHolding<VtArray<GfVec3f>>()) {
      const auto &points = value.UncheckedGet<VtArray<GfVec3f>>();
      data.vertices.clear();
      for (const auto &p : points) {
        data.vertices.emplace_back(p[0], p[1], p[2]);
      }
    }
  }

  // 2. Update Topology
  if (isNew || (*dirtyBits & HdChangeTracker::DirtyTopology)) {
    HdMeshTopology topology = GetMeshTopology(sceneDelegate);
    VtIntArray faceVertexCounts = topology.GetFaceVertexCounts();
    VtIntArray faceVertexIndices = topology.GetFaceVertexIndices();

    data.indices.clear();
    int entryOffset = 0;

    for (int numVerts : faceVertexCounts) {
      if (numVerts == 3) {
        // Standard Triangle
        data.indices.emplace_back(faceVertexIndices[entryOffset],
                                  faceVertexIndices[entryOffset + 1],
                                  faceVertexIndices[entryOffset + 2]);
      } else if (numVerts == 4) {
        // Quad: Split into two triangles (0,1,2) and (0,2,3)
        data.indices.emplace_back(faceVertexIndices[entryOffset],
                                  faceVertexIndices[entryOffset + 1],
                                  faceVertexIndices[entryOffset + 2]);
        data.indices.emplace_back(faceVertexIndices[entryOffset],
                                  faceVertexIndices[entryOffset + 2],
                                  faceVertexIndices[entryOffset + 3]);
      }
      // Advance offset by the number of vertices in this face
      entryOffset += numVerts;
    }
  }

  // 3. Update Transform (or initial load)
  if (isNew || (*dirtyBits & HdChangeTracker::DirtyTransform)) {
    GfMatrix4d m = sceneDelegate->GetTransform(id);
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        data.worldTransform(i, j) = (float)m[j][i];
  }

  *dirtyBits = HdChangeTracker::Clean;
}

} // namespace pxr