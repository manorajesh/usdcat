#include "frame_timer.h"

FrameTimer::FrameTimer(std::size_t window) : window_(window) {}

void FrameTimer::start() { startTime_ = clock::now(); }

void FrameTimer::end() {
  auto end = clock::now();
  float micros =
      std::chrono::duration<float, std::micro>(end - startTime_).count();
  push_time(micros);
  // reset start time for convenience so consecutive end() calls behave
  // deterministically (start() should still be called at frame begin)
  startTime_ = end;
}

void FrameTimer::push_time(float t) {
  times_.push_back(t);
  if (times_.size() > window_) {
    times_.pop_front();
  }
  float sum = 0.0f;
  for (float v : times_)
    sum += v;
  if (!times_.empty()) {
    avgTime_ = sum / static_cast<float>(times_.size());
  } else {
    avgTime_ = 0.0f;
  }
}

float FrameTimer::frame_time_micros() const { return avgTime_; }

int FrameTimer::fps() const {
  if (avgTime_ <= 0.0f)
    return 0;
  return static_cast<int>(1e6f / avgTime_);
}
