#pragma once
#include <Eigen/Dense>
#include <pxr/usd/usdGeom/mesh.h>
#include <vector>

struct MeshData {
  std::vector<Eigen::Vector3f> vertices;
  std::vector<Eigen::Vector3i> indices; // x, y, z as vertex IDs
  Eigen::Matrix4f worldTransform = Eigen::Matrix4f::Identity();
};

class MeshLoader {
public:
  static MeshData LoadUsdMesh(const pxr::UsdGeomMesh &usdMesh);
};