#include <Eigen/Dense>
#include <vector>

// void look_at(Eigen::Vector3f eye, Eigen::Vector3f target, Eigen::Vector3f
// up); void project(Eigen::Vector3f p_view, float fov, float aspect);

class Renderer {
public:
  Renderer()
      : framebuffer(80 * 24, ' '),
        zbuffer(80 * 24, std::numeric_limits<float>::infinity()), eye(0, 0, 5) {
  }

  ~Renderer() {}

private:
  std::vector<char> framebuffer;
  std::vector<float> zbuffer;
  Eigen::Vector3f eye;

  void orbit_camera(float yaw, float pitch, float radius,
                    Eigen::Vector3f target = Eigen::Vector3f(0, 0, 0));
  void look_at(Eigen::Vector3f eye, Eigen::Vector3f target,
               Eigen::Vector3f up = Eigen::Vector3f(0, 1, 0));
};