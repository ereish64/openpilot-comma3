#include "tools/cabana/imgui/icons.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui_impl_opengl3_loader.h"

#include "common/util.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wshadow=compatible-local"
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#pragma GCC diagnostic pop

// Map from enum to bootstrap icon ID string
static const char *kIconNames[static_cast<int>(BootstrapIcon::_Count)] = {
  "play-fill",
  "pause-fill",
  "rewind-fill",
  "fast-forward-fill",
  "skip-end-fill",
  "skip-end-fill",
  "repeat",
  "x",
  "x-lg",
  "pencil-fill",
  "plus",
  "dash",
  "gear-fill",
  "graph-up",
  "arrow-counterclockwise",
  "arrow-clockwise",
  "zoom-out",
  "info-circle-fill",
  "chevron-left",
  "chevron-right",
  "file-earmark-ruled",
  "stopwatch",
  "grip-horizontal",
  "three-dots",
  "window-stack",
  "dash-square",
  "file-plus",
  "list",
};

static GLuint s_icon_textures[static_cast<int>(BootstrapIcon::_Count)] = {};
static int s_icon_px = 0;

// Extract a <symbol id="name" ...>...</symbol> block and wrap it as standalone SVG
static std::string extractSymbol(const std::string &svg_content, const char *id) {
  std::string id_attr = std::string("id=\"") + id + "\"";
  size_t id_pos = svg_content.find(id_attr);
  if (id_pos == std::string::npos) return {};

  size_t sym_start = svg_content.rfind("<symbol", id_pos);
  if (sym_start == std::string::npos) return {};

  size_t sym_end = svg_content.find("</symbol>", sym_start);
  if (sym_end == std::string::npos) return {};
  sym_end += strlen("</symbol>");

  std::string symbol = svg_content.substr(sym_start, sym_end - sym_start);

  // Extract viewBox
  std::string viewBox = "0 0 16 16";
  size_t vb_pos = symbol.find("viewBox=\"");
  if (vb_pos != std::string::npos) {
    size_t vb_start = vb_pos + 9;
    size_t vb_end = symbol.find("\"", vb_start);
    if (vb_end != std::string::npos) {
      viewBox = symbol.substr(vb_start, vb_end - vb_start);
    }
  }

  // Extract inner content (between <symbol ...> and </symbol>)
  size_t inner_start = symbol.find(">", 0);
  if (inner_start == std::string::npos) return {};
  inner_start += 1;
  size_t inner_end = symbol.rfind("</symbol>");
  if (inner_end == std::string::npos) return {};

  std::string inner = symbol.substr(inner_start, inner_end - inner_start);

  return "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" +
         viewBox + "\" width=\"16\" height=\"16\">" + inner + "</svg>";
}

static GLuint rasterizeToTexture(NSVGrasterizer *rast, const std::string &svg_str, int px) {
  if (svg_str.empty()) return 0;

  // nsvgParse modifies the input string, so we need a mutable copy
  std::vector<char> buf(svg_str.begin(), svg_str.end());
  buf.push_back('\0');

  NSVGimage *image = nsvgParse(buf.data(), "px", 96.0f);
  if (!image) return 0;

  float scale = static_cast<float>(px) / 16.0f;

  std::vector<unsigned char> pixels(px * px * 4, 0);
  nsvgRasterize(rast, image, 0, 0, scale, pixels.data(), px, px, px * 4);
  nsvgDelete(image);

  // Convert black-on-transparent to white-on-transparent for theme-aware tinting
  for (int i = 0; i < px * px; ++i) {
    pixels[i * 4 + 0] = 255;
    pixels[i * 4 + 1] = 255;
    pixels[i * 4 + 2] = 255;
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  // These icons are pre-rasterized UI assets. Mipmaps/trilinear filtering soften
  // thin strokes more than they help, since we mostly draw them at fixed sizes.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, px, px, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  return tex;
}

void initBootstrapIcons(const char *svg_path, float icon_size) {
  if (icon_size <= 0) {
    icon_size = ImGui::GetFontSize();
  }
  s_icon_px = static_cast<int>(icon_size * 4);  // 4x for crisp rendering on HiDPI
  if (s_icon_px < 16) s_icon_px = 16;

  std::string content = util::read_file(svg_path);
  if (content.empty()) {
    fprintf(stderr, "Failed to open bootstrap icons: %s\n", svg_path);
    return;
  }

  NSVGrasterizer *rast = nsvgCreateRasterizer();
  if (!rast) return;

  for (int i = 0; i < static_cast<int>(BootstrapIcon::_Count); ++i) {
    std::string svg = extractSymbol(content, kIconNames[i]);
    s_icon_textures[i] = rasterizeToTexture(rast, svg, s_icon_px);
    if (s_icon_textures[i] == 0) {
      fprintf(stderr, "Warning: failed to load bootstrap icon '%s'\n", kIconNames[i]);
    }
  }

  nsvgDeleteRasterizer(rast);
}

void destroyBootstrapIcons() {
  for (int i = 0; i < static_cast<int>(BootstrapIcon::_Count); ++i) {
    if (s_icon_textures[i]) {
      glDeleteTextures(1, &s_icon_textures[i]);
      s_icon_textures[i] = 0;
    }
  }
}

ImTextureID getBootstrapIcon(BootstrapIcon icon) {
  int idx = static_cast<int>(icon);
  if (idx < 0 || idx >= static_cast<int>(BootstrapIcon::_Count)) return 0;
  return (ImTextureID)(uintptr_t)s_icon_textures[idx];
}

static void overlayIcon(ImTextureID tex, float pad_frac) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 mn = ImGui::GetItemRectMin();
  ImVec2 mx = ImGui::GetItemRectMax();
  const float w = mx.x - mn.x;
  const float h = mx.y - mn.y;
  const float side = std::min(w, h);
  const float pad = std::round(side * pad_frac);
  const float icon_side = std::max(1.0f, side - pad * 2.0f);
  const float x = std::round(mn.x + (w - icon_side) * 0.5f);
  const float y = std::round(mn.y + (h - icon_side) * 0.5f);
  ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  dl->AddImage(tex, ImVec2(x, y), ImVec2(x + icon_side, y + icon_side), ImVec2(0, 0), ImVec2(1, 1), col);
}

bool iconButton(const char *str_id, BootstrapIcon icon, const char *tooltip) {
  float h = ImGui::GetFrameHeight();
  ImTextureID tex = getBootstrapIcon(icon);
  if (!tex) {
    bool pressed = ImGui::Button(str_id, ImVec2(h, h));
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
  }
  bool pressed = ImGui::Button(str_id, ImVec2(h, h));
  overlayIcon(tex, 0.15f);
  if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
  return pressed;
}

bool smallIconButton(const char *str_id, BootstrapIcon icon, const char *tooltip) {
  const float h = std::round(ImGui::GetTextLineHeight());
  ImTextureID tex = getBootstrapIcon(icon);
  if (!tex) {
    bool pressed = ImGui::Button(str_id, ImVec2(h, h));
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
  }
  bool pressed = ImGui::Button(str_id, ImVec2(h, h));
  overlayIcon(tex, 0.10f);
  if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
  return pressed;
}

void drawIcon(BootstrapIcon icon, float size) {
  if (size <= 0) size = ImGui::GetTextLineHeight();
  size = std::round(size);
  ImTextureID tex = getBootstrapIcon(icon);
  if (tex) {
    ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    pos.x = std::round(pos.x);
    pos.y = std::round(pos.y);
    ImGui::Dummy(ImVec2(size, size));
    ImGui::GetWindowDrawList()->AddImage(tex, pos, ImVec2(pos.x + size, pos.y + size), ImVec2(0, 0), ImVec2(1, 1), col);
  } else {
    ImGui::Dummy(ImVec2(size, size));
  }
}
