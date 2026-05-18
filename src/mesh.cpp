#include "mesh.h"
#include "delegate.h"
#include "renderer.h"
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/pxOsd/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>

namespace pxr {

static bool _IsUvPrimvarName(const TfToken &name) {
  return name == TfToken("st") || name == TfToken("st0") ||
         name == TfToken("primvars:st") || name == TfToken("primvars:st0");
}

static std::vector<Eigen::Vector3f>
_ComputeWeldedSmoothNormals(const std::vector<Eigen::Vector3f> &vertices,
                            const std::vector<Eigen::Vector3i> &indices) {
  std::vector<Eigen::Vector3f> accum(vertices.size(),
                                     Eigen::Vector3f::Zero());
  for (const Eigen::Vector3i &tri : indices) {
    if (tri(0) < 0 || tri(1) < 0 || tri(2) < 0 ||
        static_cast<size_t>(std::max({tri(0), tri(1), tri(2)})) >=
            vertices.size()) {
      continue;
    }

    const Eigen::Vector3f &p0 = vertices[tri(0)];
    const Eigen::Vector3f &p1 = vertices[tri(1)];
    const Eigen::Vector3f &p2 = vertices[tri(2)];
    Eigen::Vector3f areaNormal = (p1 - p0).cross(p2 - p0);
    if (areaNormal.squaredNorm() < 1e-12f) {
      continue;
    }
    accum[tri(0)] += areaNormal;
    accum[tri(1)] += areaNormal;
    accum[tri(2)] += areaNormal;
  }

  using PositionKey = std::tuple<int, int, int>;
  std::map<PositionKey, Eigen::Vector3f> welded;
  auto keyFor = [](const Eigen::Vector3f &p) {
    constexpr float scale = 100000.0f;
    return PositionKey(static_cast<int>(std::lround(p.x() * scale)),
                       static_cast<int>(std::lround(p.y() * scale)),
                       static_cast<int>(std::lround(p.z() * scale)));
  };

  for (size_t i = 0; i < vertices.size(); ++i) {
    welded[keyFor(vertices[i])] += accum[i];
  }

  std::vector<Eigen::Vector3f> smooth(vertices.size(), Eigen::Vector3f::UnitZ());
  for (size_t i = 0; i < vertices.size(); ++i) {
    Eigen::Vector3f n = welded[keyFor(vertices[i])];
    smooth[i] = n.squaredNorm() > 1e-12f ? n.normalized() : Eigen::Vector3f::UnitZ();
  }
  return smooth;
}

void HdTerminalMesh::_InitRepr(TfToken const &reprToken,
                               HdDirtyBits *dirtyBits) {
  // This tells Hydra: "If we are drawing smoothHull, we need Points and
  // Topology"
  if (reprToken == HdReprTokens->smoothHull ||
      reprToken == HdReprTokens->refined) {
    *dirtyBits |= HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology;
  }
}

void HdTerminalMesh::Sync([[maybe_unused]] HdSceneDelegate *sceneDelegate,
                          [[maybe_unused]] HdRenderParam *renderParam,
                          [[maybe_unused]] HdDirtyBits *dirtyBits,
                          [[maybe_unused]] TfToken const &reprToken) {
  auto id = GetId();
  HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
  HdTerminalDelegate *delegate =
      static_cast<HdTerminalDelegate *>(renderIndex.GetRenderDelegate());
  Renderer *renderer = delegate->GetRenderer();
  std::lock_guard<std::mutex> lock(renderer->get_scene_mutex());

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
    data.smoothSubdivision =
        topology.GetScheme() == PxOsdOpenSubdivTokens->catmullClark ||
        topology.GetScheme() == PxOsdOpenSubdivTokens->loop;
    HdMeshUtil meshUtil(&topology, GetId());

    VtVec3iArray triangulation;
    VtIntArray primitiveParams; // We don't need this, but the API requires it

    // This performs the triangulation for you based on the face counts
    meshUtil.ComputeTriangleIndices(&triangulation, &primitiveParams);

    data.indices.clear();
    for (const auto &tri : triangulation) {
      // We emplace as Vector3i for your Renderer's MeshData struct
      data.indices.emplace_back(tri[0], tri[1], tri[2]);
    }
  }

  // 3. Update Transform (or initial load)
  if (isNew || (*dirtyBits & HdChangeTracker::DirtyTransform)) {
    GfMatrix4d m = sceneDelegate->GetTransform(id);
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        data.worldTransform(i, j) = (float)m[j][i];
  }

  // 4. Update UVs (texture coordinates)
  if (isNew || (*dirtyBits & HdChangeTracker::DirtyPrimvar) ||
      (*dirtyBits & HdChangeTracker::DirtyNormals)) {
    VtValue uvValue;
    HdInterpolation uvInterpolation = HdInterpolationConstant;
    for (HdInterpolation interpolation :
         {HdInterpolationFaceVarying, HdInterpolationVertex,
          HdInterpolationVarying}) {
      for (const HdPrimvarDescriptor &desc :
           GetPrimvarDescriptors(sceneDelegate, interpolation)) {
        if (_IsUvPrimvarName(desc.name)) {
          uvValue = sceneDelegate->Get(id, desc.name);
          uvInterpolation = interpolation;
          break;
        }
      }
      if (!uvValue.IsEmpty()) {
        break;
      }
    }

    if (uvValue.IsEmpty()) {
      uvValue = sceneDelegate->Get(id, TfToken("st"));
    }
    if (uvValue.IsEmpty()) {
      uvValue = sceneDelegate->Get(id, TfToken("st0"));
    }
    if (uvValue.IsEmpty()) {
      uvValue = sceneDelegate->Get(id, TfToken("primvars:st"));
    }
    if (uvValue.IsEmpty()) {
      uvValue = sceneDelegate->Get(id, TfToken("primvars:st0"));
    }

    if (uvValue.IsHolding<VtArray<GfVec2f>>()) {
      const auto &uvs = uvValue.UncheckedGet<VtArray<GfVec2f>>();
      data.uvs.clear();
      data.uvIndices.clear();

      if (uvInterpolation == HdInterpolationFaceVarying) {
        HdMeshTopology topology = GetMeshTopology(sceneDelegate);
        HdMeshUtil meshUtil(&topology, GetId());
        VtValue triangulated;
        HdMeshComputationResult result =
            meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                uvs.cdata(), static_cast<int>(uvs.size()), HdTypeFloatVec2,
                &triangulated);
        const VtArray<GfVec2f> *triUvs = nullptr;
        if (result == HdMeshComputationResult::Success &&
            triangulated.IsHolding<VtArray<GfVec2f>>()) {
          triUvs = &triangulated.UncheckedGet<VtArray<GfVec2f>>();
        } else if (result == HdMeshComputationResult::Unchanged ||
                   uvs.size() == data.indices.size() * 3) {
          triUvs = &uvs;
        }
        if (triUvs) {
          for (const auto &uv : *triUvs) {
            data.uvs.emplace_back(uv[0], uv[1]);
          }
          for (size_t i = 0; i + 2 < data.uvs.size(); i += 3) {
            data.uvIndices.emplace_back(static_cast<int>(i),
                                        static_cast<int>(i + 1),
                                        static_cast<int>(i + 2));
          }
        }
      } else {
        for (const auto &uv : uvs) {
          data.uvs.emplace_back(uv[0], uv[1]);
        }
        data.uvIndices = data.indices;
      }
    }

    VtValue normalValue;
    HdInterpolation normalInterpolation = HdInterpolationConstant;
    for (HdInterpolation interpolation :
         {HdInterpolationFaceVarying, HdInterpolationVertex,
          HdInterpolationVarying}) {
      for (const HdPrimvarDescriptor &desc :
           GetPrimvarDescriptors(sceneDelegate, interpolation)) {
        if (desc.name == HdTokens->normals || desc.name == TfToken("normals")) {
          normalValue = sceneDelegate->Get(id, desc.name);
          normalInterpolation = interpolation;
          break;
        }
      }
      if (!normalValue.IsEmpty()) {
        break;
      }
    }

    if (normalValue.IsEmpty()) {
      normalValue = sceneDelegate->Get(id, HdTokens->normals);
    }
    if (normalValue.IsHolding<VtArray<GfVec3f>>()) {
      const auto &normals = normalValue.UncheckedGet<VtArray<GfVec3f>>();
      data.normals.clear();
      data.normalIndices.clear();

      if (normalInterpolation == HdInterpolationFaceVarying) {
        HdMeshTopology topology = GetMeshTopology(sceneDelegate);
        HdMeshUtil meshUtil(&topology, GetId());
        VtValue triangulated;
        HdMeshComputationResult result =
            meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                normals.cdata(), static_cast<int>(normals.size()),
                HdTypeFloatVec3, &triangulated);
        const VtArray<GfVec3f> *triNormals = nullptr;
        if (result == HdMeshComputationResult::Success &&
            triangulated.IsHolding<VtArray<GfVec3f>>()) {
          triNormals = &triangulated.UncheckedGet<VtArray<GfVec3f>>();
        } else if (result == HdMeshComputationResult::Unchanged ||
                   normals.size() == data.indices.size() * 3) {
          triNormals = &normals;
        }
        if (triNormals) {
          for (const auto &n : *triNormals) {
            data.normals.emplace_back(n[0], n[1], n[2]);
          }
          for (size_t i = 0; i + 2 < data.normals.size(); i += 3) {
            data.normalIndices.emplace_back(static_cast<int>(i),
                                            static_cast<int>(i + 1),
                                            static_cast<int>(i + 2));
          }
        }
      } else {
        for (const auto &n : normals) {
          data.normals.emplace_back(n[0], n[1], n[2]);
        }
        data.normalIndices = data.indices;
      }
    }
  }

  // 5. Get the Bound Material ID
  // This tells the mesh WHICH texture to use
  if (isNew || (*dirtyBits & HdChangeTracker::DirtyMaterialId)) {
    data.materialId = sceneDelegate->GetMaterialId(id);
  }

  if ((isNew || (*dirtyBits & HdChangeTracker::DirtyPoints) ||
       (*dirtyBits & HdChangeTracker::DirtyTopology)) &&
      data.smoothSubdivision && !data.vertices.empty() && !data.indices.empty()) {
    data.smoothNormals =
        _ComputeWeldedSmoothNormals(data.vertices, data.indices);
  } else if (!data.smoothSubdivision) {
    data.smoothNormals.clear();
  }

  *dirtyBits = HdChangeTracker::Clean;
}

} // namespace pxr
