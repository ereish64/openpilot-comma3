#pragma once

#include "imgui_impl_opengl3_loader.h"
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "msgq/visionipc/visionipc_client.h"
#include "tools/cabana/imgui/app.h"
#include "tools/cabana/imgui/app_util.h"

struct CabanaImguiApp::VideoStreamState {
  explicit VideoStreamState(std::string service_name, VisionStreamType stream_type)
      : stream_name(std::move(service_name)), requested_stream(stream_type), active_stream(stream_type) {
    worker = std::thread([this]() { run(); });
  }

  ~VideoStreamState() {
    stop = true;
    if (worker.joinable()) worker.join();
    if (texture_id != 0) glDeleteTextures(1, &texture_id);
  }

  void setRequestedStream(VisionStreamType type) { requested_stream = type; }
  VisionStreamType requestedStream() const { return requested_stream.load(); }
  VisionStreamType activeStream() const { return active_stream.load(); }

  std::set<VisionStreamType> streams() const {
    std::lock_guard lk(lock);
    return available_streams;
  }

  bool hasFrame() const {
    std::lock_guard lk(lock);
    return current_frame != nullptr;
  }

  GLuint texture() {
    std::vector<uint8_t> rgba_copy;
    int width = 0;
    int height = 0;
    uint32_t frame_id = 0;

    {
      std::lock_guard lk(lock);
      if (!current_frame || stream_width <= 0 || stream_height <= 0) {
        return 0;  // No frame available — do not show stale texture
      }
      if (texture_id != 0 && frame_meta.frame_id == uploaded_frame_id) {
        return texture_id;  // Already uploaded this frame
      }
      width = stream_width;
      height = stream_height;
      frame_id = frame_meta.frame_id;
      nv12ToRgba(current_frame->y, current_frame->uv, width, height, stream_stride, rgba_copy);
    }

    if (texture_id == 0) {
      glGenTextures(1, &texture_id);
      glBindTexture(GL_TEXTURE_2D, texture_id);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_copy.data());
    } else {
      glBindTexture(GL_TEXTURE_2D, texture_id);
      if (width != texture_width || height != texture_height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_copy.data());
      } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba_copy.data());
      }
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    texture_width = width;
    texture_height = height;
    uploaded_frame_id = frame_id;
    return texture_id;
  }

  ImVec2 textureSize() const {
    return ImVec2(static_cast<float>(texture_width), static_cast<float>(texture_height));
  }

private:
  void run() {
    VisionStreamType current_type = requested_stream.load();
    std::unique_ptr<VisionIpcClient> client;
    VisionIpcBufExtra extra = {};

    while (!stop) {
      if (!client || current_type != requested_stream.load()) {
        {
          std::lock_guard lk(lock);
          current_frame = nullptr;
          // Signal to the main thread that the texture should be cleared
          // by resetting uploaded_frame_id so the stale texture won't be returned
          uploaded_frame_id = 0;
          stream_width = 0;
          stream_height = 0;
        }
        current_type = requested_stream.load();
        active_stream = current_type;
        client.reset(new VisionIpcClient(stream_name, current_type, false));
      }

      if (!client->connected) {
        auto streams = VisionIpcClient::getAvailableStreams(stream_name, false);
        {
          std::lock_guard lk(lock);
          available_streams = streams;
        }
        if (streams.empty()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        if (!client->connect(false)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        {
          std::lock_guard lk(lock);
          stream_width = client->buffers[0].width;
          stream_height = client->buffers[0].height;
          stream_stride = client->buffers[0].stride;
        }
      }

      if (VisionBuf *buf = client->recv(&extra, 100)) {
        std::lock_guard lk(lock);
        current_frame = buf;
        frame_meta = extra;
      }
    }
  }

  std::string stream_name;
  mutable std::mutex lock;
  std::thread worker;
  std::atomic<bool> stop = false;
  std::atomic<VisionStreamType> requested_stream;
  std::atomic<VisionStreamType> active_stream;
  std::set<VisionStreamType> available_streams;
  VisionBuf *current_frame = nullptr;
  VisionIpcBufExtra frame_meta = {};
  int stream_width = 0;
  int stream_height = 0;
  int stream_stride = 0;
  GLuint texture_id = 0;
  int texture_width = 0;
  int texture_height = 0;
  uint32_t uploaded_frame_id = 0;
};
