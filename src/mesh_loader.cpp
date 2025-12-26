#include "mesh_loader.h"
#include <pxr/usd/usdGeom/xformable.h>

MeshData MeshLoader::LoadUsdMesh(const pxr::UsdGeomMesh &usdMesh) {
  MeshData data;

  // Get Transform
  pxr::GfMatrix4d worldMatrix =
      usdMesh.ComputeLocalToWorldTransform(pxr::UsdTimeCode::Default());

  // Convert to Eigen (transpose for column-vector convention)
  Eigen::Matrix4f transform;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      transform(i, j) = static_cast<float>(worldMatrix[j][i]);
    }
  }

  // 1. Get Points (Vertices)
  pxr::VtArray<pxr::GfVec3f> points;
  usdMesh.GetPointsAttr().Get(&points);

  for (const auto &p : points) {
    Eigen::Vector4f v(p[0], p[1], p[2], 1.0f);
    Eigen::Vector4f v_transformed = transform * v;
    data.vertices.emplace_back(v_transformed.x(), v_transformed.y(),
                               v_transformed.z());
  }

  // 2. Get Topology (Face Counts and Indices)
  pxr::VtIntArray counts, vertexIndices;
  usdMesh.GetFaceVertexCountsAttr().Get(&counts);
  usdMesh.GetFaceVertexIndicesAttr().Get(&vertexIndices);

  // 3. Triangulate (Triangle Fan Method)
  int indexOffset = 0;
  for (int faceCount : counts) {
    // A triangle needs at least 3 vertices
    for (int i = 1; i < faceCount - 1; ++i) {
      data.indices.emplace_back(vertexIndices[indexOffset],
                                vertexIndices[indexOffset + i],
                                vertexIndices[indexOffset + i + 1]);
    }
    indexOffset += faceCount;
  }

  return data;
}