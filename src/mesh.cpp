#include "mesh.h"
#include <pxr/usd/usdGeom/xformable.h>

MeshData MeshLoader::LoadUsdMesh(const pxr::UsdGeomMesh &usdMesh) {
  MeshData data;

  // 1. Get the Transform
  pxr::GfMatrix4d usdWorldMat =
      usdMesh.ComputeLocalToWorldTransform(pxr::UsdTimeCode::Default());
  Eigen::Matrix4f worldTransform =
      Eigen::Map<Eigen::Matrix<double, 4, 4, Eigen::RowMajor>>(
          usdWorldMat.GetArray())
          .cast<float>();

  // 2. Get USD Points
  pxr::VtArray<pxr::GfVec3f> points;
  usdMesh.GetPointsAttr().Get(&points);
  size_t numPoints = points.size();

  // 3. Map the USD memory to an Eigen Matrix
  // We treat the flat array of GfVec3f as a 3xN matrix
  Eigen::Map<Eigen::Matrix<float, 3, Eigen::Dynamic>> lPos(
      (float *)points.data(), 3, numPoints);

  // 4. Transform everything at once
  // We need 4xN for the math (to include the 'w' component for translation)
  Eigen::Matrix<float, 4, Eigen::Dynamic> worldPos =
      worldTransform * lPos.colwise().homogeneous();

  // 5. Store back into your vector
  data.vertices.resize(numPoints);
  for (size_t i = 0; i < numPoints; ++i) {
    data.vertices[i] = worldPos.block<3, 1>(0, i);
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