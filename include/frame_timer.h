#pragma once
#include <chrono>
#include <cstddef>
#include <deque>

class FrameTimer {
public:
  // window: number of frames to average (default 100)
  explicit FrameTimer(std::size_t window = 100);

  // Call at beginning of a frame
  void start();

  // Call at end of a frame (will update the moving average)
  void end();

  // Return the moving average frame time in microseconds
  float frame_time_micros() const;

  // Return integer FPS computed from the moving average frame time
  int fps() const;

private:
  using clock = std::chrono::high_resolution_clock;
  clock::time_point startTime_;
  std::deque<float> times_; // microseconds
  std::size_t window_;
  float avgTime_{0.0f};

  void push_time(float t);
};
