#include "tools/cabana/imgui/app_util.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "implot.h"
#include "third_party/json11/json11.hpp"
#include "common/util.h"
#include "tools/cabana/imgui/settings.h"
#include "tools/cabana/imgui/util.h"

const CabanaColor timeline_colors[] = {
  CabanaColor(111, 143, 175),
  CabanaColor(0, 163, 108),
  CabanaColor(0, 255, 0),
  CabanaColor(255, 195, 0),
  CabanaColor(199, 0, 57),
  CabanaColor(255, 0, 255),
};

ImVec4 imColor(const CabanaColor &color, float alpha_scale) {
  return ImVec4(color.redF(), color.greenF(), color.blueF(), color.alphaF() * alpha_scale);
}

ImU32 packedColor(const CabanaColor &color, float alpha_scale) {
  CabanaColor c = color;
  c.setAlphaF(std::clamp(c.alphaF() * alpha_scale, 0.0f, 1.0f));
  return IM_COL32(c.red(), c.green(), c.blue(), c.alpha());
}

// Close a modal popup only on Escape (for editing/destructive dialogs — matches Qt button-box pattern)
void dismissOnEscape() {
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
}

// Draw a draggable splitter and return true if actively dragging
bool drawSplitter(const char *id, bool vertical, const ImVec2 &size) {
  ImVec2 spos = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, size);
  bool active = ImGui::IsItemActive();
  if (ImGui::IsItemHovered() || active) {
    ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (vertical) {
      float cx = spos.x + size.x * 0.5f;
      dl->AddLine(ImVec2(cx, spos.y), ImVec2(cx, spos.y + size.y), IM_COL32(100, 100, 100, 180), 2.0f);
    } else {
      float cy = spos.y + size.y * 0.5f;
      dl->AddLine(ImVec2(spos.x, cy), ImVec2(spos.x + size.x, cy), IM_COL32(100, 100, 100, 180), 2.0f);
    }
  }
  return active;
}

// Shell-escape a string for safe interpolation into shell commands
static std::string shellEscape(const std::string &s) {
  std::string result = "'";
  for (char c : s) {
    if (c == '\'') result += "'\\''";
    else result += c;
  }
  result += "'";
  return result;
}

// Check if a command-line dialog tool is available
bool hasCommand(const char *cmd) {
  static std::unordered_map<std::string, bool> cache;
  auto it = cache.find(cmd);
  if (it != cache.end()) return it->second;
  std::string check = std::string("command -v ") + cmd + " >/dev/null 2>&1";
  bool found = system(check.c_str()) == 0;
  cache[cmd] = found;
  return found;
}

bool hasNativeFileDialogs() {
#ifdef __APPLE__
  return true;
#else
  // Tiling window managers show native dialogs as tiled windows instead of floating
  if (getenv("I3SOCK") || getenv("SWAYSOCK") || getenv("HYPRLAND_INSTANCE_SIGNATURE"))
    return false;
  return hasCommand("zenity") || hasCommand("kdialog");
#endif
}

// Run a shell command, capture stdout, and return it trimmed.
static std::string popenReadLine(const std::string &cmd) {
  return util::strip(util::check_output(cmd));
}

// Build the platform-specific command for a file dialog, or return empty if no tool is available.
enum class DialogMode { Open, Save, Directory };
static std::string buildDialogCommand(DialogMode mode, const std::string &title, const std::string &default_path,
                                       const std::string &filter_name, const std::string &filter_pattern) {
  std::string cmd;
#ifdef __APPLE__
  {
    // Build the AppleScript expression, then shell-escape the whole thing
    std::string expr;
    if (mode == DialogMode::Directory) {
      expr = "POSIX path of (choose folder";
    } else if (mode == DialogMode::Save) {
      expr = "POSIX path of (choose file name";
    } else {
      expr = "POSIX path of (choose file";
    }
    if (!title.empty()) {
      // Escape backslashes and double quotes for AppleScript string literal
      std::string safe_title;
      for (char c : title) {
        if (c == '\\' || c == '"') safe_title += '\\';
        safe_title += c;
      }
      expr += " with prompt \"" + safe_title + "\"";
    }
    expr += ")";
    cmd = "osascript -e " + shellEscape(expr);
  }
#else
  if (hasCommand("zenity")) {
    cmd = "zenity --file-selection";
    if (mode == DialogMode::Directory) cmd += " --directory";
    if (mode == DialogMode::Save) cmd += " --save --confirm-overwrite";
    if (!title.empty()) cmd += " --title=" + shellEscape(title);
    if (!default_path.empty()) {
      std::string fname = default_path + (mode != DialogMode::Save ? "/" : "");
      cmd += " --filename=" + shellEscape(fname);
    }
    if (!filter_pattern.empty()) cmd += " --file-filter=" + shellEscape(filter_name + " | " + filter_pattern) + " --file-filter=" + shellEscape("All files | *");
  } else if (hasCommand("kdialog")) {
    if (mode == DialogMode::Directory) {
      cmd = "kdialog --getexistingdirectory " + shellEscape(default_path.empty() ? "." : default_path);
    } else if (mode == DialogMode::Save) {
      cmd = "kdialog --getsavefilename " + shellEscape(default_path.empty() ? "." : default_path);
    } else {
      cmd = "kdialog --getopenfilename " + shellEscape(default_path.empty() ? "." : default_path);
    }
    if (!filter_pattern.empty() && mode != DialogMode::Directory) cmd += " " + shellEscape(filter_pattern);
    if (!title.empty()) cmd += " --title " + shellEscape(title);
  } else {
    fprintf(stderr, "No file dialog tool found (install zenity or kdialog)\n");
    return {};
  }
#endif
  cmd += " 2>/dev/null";
  return cmd;
}

// Native file dialog helpers using zenity/kdialog (Linux) or osascript (macOS)
std::string nativeOpenFileDialog(const std::string &title, const std::string &default_path, const std::string &filter_name, const std::string &filter_pattern) {
  std::string cmd = buildDialogCommand(DialogMode::Open, title, default_path, filter_name, filter_pattern);
  return cmd.empty() ? std::string{} : popenReadLine(cmd);
}

std::string nativeSaveFileDialog(const std::string &title, const std::string &default_name, const std::string &filter_name, const std::string &filter_pattern) {
  std::string cmd = buildDialogCommand(DialogMode::Save, title, default_name, filter_name, filter_pattern);
  return cmd.empty() ? std::string{} : popenReadLine(cmd);
}

std::string nativeDirectoryDialog(const std::string &title, const std::string &default_path) {
  std::string cmd = buildDialogCommand(DialogMode::Directory, title, default_path, {}, {});
  return cmd.empty() ? std::string{} : popenReadLine(cmd);
}

// Match Qt's NameValidator: only word characters (alphanumeric + underscore)
bool isValidDbcIdentifier(const std::string &name) {
  if (name.empty()) return false;
  for (char c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

// Normalize DBC identifier: replace spaces with underscores (matching Qt's NameValidator)
void normalizeDbcIdentifier(char *buf) {
  for (char *p = buf; *p; ++p) {
    if (*p == ' ') *p = '_';
  }
}

std::string pathBasename(const std::string &path) {
  return std::filesystem::path(path).filename().string();
}

std::string pathDirname(const std::string &path) {
  auto parent = std::filesystem::path(path).parent_path().string();
  return parent.empty() ? "." : parent;
}

void drawAlertOverlay(ImDrawList *draw, const Timeline::Entry &alert, float x, float y, float w) {
  const CabanaColor &qc = timeline_colors[int(alert.type)];
  const float wrap_width = std::max(80.0f, w - 24.0f);
  ImFont *font = ImGui::GetFont();
  const float font_size = font ? font->LegacySize : ImGui::GetFontSize();
  std::string text = alert.text1;
  if (!alert.text2.empty()) text += "\n" + alert.text2;
  ImVec2 text_size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap_width);
  draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + text_size.y + 12.0f), IM_COL32(qc.red(), qc.green(), qc.blue(), 190), 3.0f);
  draw->AddText(font, font_size, ImVec2(x + 12.0f, y + 6.0f), IM_COL32(255, 255, 255, 255), text.c_str(), nullptr, wrap_width);
}

std::string formatPayload(const std::vector<uint8_t> &bytes) {
  return utils::toHex(bytes, ' ');
}

ImGuiKey mapSdlKey(SDL_Keycode key) {
  switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_INSERT: return ImGuiKey_Insert;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_QUOTE: return ImGuiKey_Apostrophe;
    case SDLK_COMMA: return ImGuiKey_Comma;
    case SDLK_MINUS: return ImGuiKey_Minus;
    case SDLK_PERIOD: return ImGuiKey_Period;
    case SDLK_SLASH: return ImGuiKey_Slash;
    case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
    case SDLK_EQUALS: return ImGuiKey_Equal;
    case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
    case SDLK_BACKSLASH: return ImGuiKey_Backslash;
    case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
    case SDLK_BACKQUOTE: return ImGuiKey_GraveAccent;
    case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
    case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
    case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
    case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
    case SDLK_PAUSE: return ImGuiKey_Pause;
    case SDLK_KP_0: return ImGuiKey_Keypad0;
    case SDLK_KP_1: return ImGuiKey_Keypad1;
    case SDLK_KP_2: return ImGuiKey_Keypad2;
    case SDLK_KP_3: return ImGuiKey_Keypad3;
    case SDLK_KP_4: return ImGuiKey_Keypad4;
    case SDLK_KP_5: return ImGuiKey_Keypad5;
    case SDLK_KP_6: return ImGuiKey_Keypad6;
    case SDLK_KP_7: return ImGuiKey_Keypad7;
    case SDLK_KP_8: return ImGuiKey_Keypad8;
    case SDLK_KP_9: return ImGuiKey_Keypad9;
    case SDLK_KP_PERIOD: return ImGuiKey_KeypadDecimal;
    case SDLK_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case SDLK_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case SDLK_KP_MINUS: return ImGuiKey_KeypadSubtract;
    case SDLK_KP_PLUS: return ImGuiKey_KeypadAdd;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_KP_EQUALS: return ImGuiKey_KeypadEqual;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_LGUI: return ImGuiKey_LeftSuper;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    case SDLK_RGUI: return ImGuiKey_RightSuper;
    case SDLK_APPLICATION: return ImGuiKey_Menu;
    case SDLK_0: return ImGuiKey_0;
    case SDLK_1: return ImGuiKey_1;
    case SDLK_2: return ImGuiKey_2;
    case SDLK_3: return ImGuiKey_3;
    case SDLK_4: return ImGuiKey_4;
    case SDLK_5: return ImGuiKey_5;
    case SDLK_6: return ImGuiKey_6;
    case SDLK_7: return ImGuiKey_7;
    case SDLK_8: return ImGuiKey_8;
    case SDLK_9: return ImGuiKey_9;
    case SDLK_a: return ImGuiKey_A;
    case SDLK_b: return ImGuiKey_B;
    case SDLK_c: return ImGuiKey_C;
    case SDLK_d: return ImGuiKey_D;
    case SDLK_e: return ImGuiKey_E;
    case SDLK_f: return ImGuiKey_F;
    case SDLK_g: return ImGuiKey_G;
    case SDLK_h: return ImGuiKey_H;
    case SDLK_i: return ImGuiKey_I;
    case SDLK_j: return ImGuiKey_J;
    case SDLK_k: return ImGuiKey_K;
    case SDLK_l: return ImGuiKey_L;
    case SDLK_m: return ImGuiKey_M;
    case SDLK_n: return ImGuiKey_N;
    case SDLK_o: return ImGuiKey_O;
    case SDLK_p: return ImGuiKey_P;
    case SDLK_q: return ImGuiKey_Q;
    case SDLK_r: return ImGuiKey_R;
    case SDLK_s: return ImGuiKey_S;
    case SDLK_t: return ImGuiKey_T;
    case SDLK_u: return ImGuiKey_U;
    case SDLK_v: return ImGuiKey_V;
    case SDLK_w: return ImGuiKey_W;
    case SDLK_x: return ImGuiKey_X;
    case SDLK_y: return ImGuiKey_Y;
    case SDLK_z: return ImGuiKey_Z;
    case SDLK_F1: return ImGuiKey_F1;
    case SDLK_F2: return ImGuiKey_F2;
    case SDLK_F3: return ImGuiKey_F3;
    case SDLK_F4: return ImGuiKey_F4;
    case SDLK_F5: return ImGuiKey_F5;
    case SDLK_F6: return ImGuiKey_F6;
    case SDLK_F7: return ImGuiKey_F7;
    case SDLK_F8: return ImGuiKey_F8;
    case SDLK_F9: return ImGuiKey_F9;
    case SDLK_F10: return ImGuiKey_F10;
    case SDLK_F11: return ImGuiKey_F11;
    case SDLK_F12: return ImGuiKey_F12;
    default: return ImGuiKey_None;
  }
}

void updateKeyMods(ImGuiIO &io, SDL_Keymod mods) {
  io.AddKeyEvent(ImGuiMod_Ctrl, (mods & KMOD_CTRL) != 0);
  io.AddKeyEvent(ImGuiMod_Shift, (mods & KMOD_SHIFT) != 0);
  io.AddKeyEvent(ImGuiMod_Alt, (mods & KMOD_ALT) != 0);
  io.AddKeyEvent(ImGuiMod_Super, (mods & KMOD_GUI) != 0);
}

int mapMouseButton(uint8_t button) {
  switch (button) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_RIGHT: return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    case SDL_BUTTON_X1: return 3;
    case SDL_BUTTON_X2: return 4;
    default: return -1;
  }
}

void setClipboardText(void *, const char *text) {
  SDL_SetClipboardText(text);
}

float cabanaUiScale() {
  if (const char *env = std::getenv("CABANA_IMGUI_SCALE")) {
    char *end = nullptr;
    const float value = std::strtof(env, &end);
    if (end != env && value > 0.5f && value < 5.0f) return value;
  }
  // Auto-detect from SDL display DPI vs standard 96 DPI
  float ddpi = 0.0f;
  if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 0.0f) {
    return std::clamp(ddpi / 96.0f, 1.0f, 3.0f);
  }
  return 1.20f;
}

void applyCabanaStyle(float scale) {
  // Match Qt Cabana's Darcula dark theme: neutral grays, not blue-tinted
  // Qt palette reference:
  //   Window:    #353535  Base:      #3c3f41  Text:      #bbbbbb
  //   Button:    #3c3f41  Highlight: #2f65ca  BrightText:#f0f0f0
  //   Disabled:  #777777  Light:     #777777  Dark:      #353535
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style = ImGuiStyle();
  // Qt Cabana mostly relied on palette + native widget chrome, so keep ImGui surfaces flatter.
  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.FrameRounding = 1.0f;
  style.GrabRounding = 1.0f;
  style.ScrollbarRounding = 1.0f;
  style.TabRounding = 0.0f;
  style.PopupRounding = 0.0f;
  style.WindowPadding = ImVec2(10.0f, 8.0f);
  style.FramePadding = ImVec2(5.0f, 3.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
  style.ScrollbarSize = 10.0f;

  // Qt #353535 = (0.208, 0.208, 0.208), #3c3f41 = (0.235, 0.247, 0.255)
  const ImVec4 window_bg(0.208f, 0.208f, 0.208f, 1.0f);      // #353535
  const ImVec4 base_bg(0.235f, 0.247f, 0.255f, 1.0f);         // #3c3f41
  const ImVec4 text_color(0.733f, 0.733f, 0.733f, 1.0f);      // #bbbbbb
  const ImVec4 disabled_text(0.60f, 0.60f, 0.60f, 1.0f);       // #999999
  const ImVec4 highlight(0.184f, 0.396f, 0.792f, 1.0f);       // #2f65ca
  const ImVec4 highlight_hover(0.25f, 0.47f, 0.85f, 1.0f);
  const ImVec4 highlight_active(0.30f, 0.52f, 0.90f, 1.0f);
  const ImVec4 border_color(0.30f, 0.30f, 0.30f, 1.0f);

  style.Colors[ImGuiCol_WindowBg] = window_bg;
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);  // slightly lighter than window
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.25f, 0.25f, 0.27f, 0.98f);
  style.Colors[ImGuiCol_Text] = text_color;
  style.Colors[ImGuiCol_TextDisabled] = disabled_text;
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);          // darker than window/popup bg
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.23f, 0.25f, 1.0f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.27f, 0.30f, 1.0f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
  style.Colors[ImGuiCol_Header] = highlight;
  style.Colors[ImGuiCol_HeaderHovered] = highlight_hover;
  style.Colors[ImGuiCol_HeaderActive] = highlight_active;
  style.Colors[ImGuiCol_Button] = base_bg;
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.32f, 0.35f, 1.0f);
  style.Colors[ImGuiCol_ButtonActive] = highlight;
  style.Colors[ImGuiCol_Tab] = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
  style.Colors[ImGuiCol_TabHovered] = highlight_hover;
  style.Colors[ImGuiCol_TabSelected] = highlight;
  style.Colors[ImGuiCol_Border] = border_color;
  style.Colors[ImGuiCol_Separator] = border_color;
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.6f);
  style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.40f, 0.40f, 0.40f, 0.6f);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.50f, 0.50f, 0.50f, 0.7f);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.55f, 0.55f, 0.8f);
  style.Colors[ImGuiCol_SliderGrab] = highlight;
  style.Colors[ImGuiCol_SliderGrabActive] = highlight_active;
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
  style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.25f, 0.25f, 0.27f, 1.0f);
  style.Colors[ImGuiCol_TableBorderStrong] = border_color;
  style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.27f, 0.27f, 0.27f, 1.0f);
  style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.24f, 0.24f, 0.26f, 0.4f);
  style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(highlight.x, highlight.y, highlight.z, 0.45f);
  style.ScaleAllSizes(scale);

  ImPlot::StyleColorsAuto();
  ImPlotStyle &plot_style = ImPlot::GetStyle();
  plot_style.Colors[ImPlotCol_PlotBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.0f);
  plot_style.Colors[ImPlotCol_FrameBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.0f);
}

void applyCabanaLightStyle(float scale) {
  ImGuiStyle &style = ImGui::GetStyle();
  style = ImGuiStyle();  // Reset to defaults before scaling (matches dark theme path)
  ImGui::StyleColorsLight();  // Apply light palette after reset
  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.FrameRounding = 1.0f;
  style.GrabRounding = 1.0f;
  style.ScrollbarRounding = 1.0f;
  style.TabRounding = 0.0f;
  style.PopupRounding = 0.0f;
  style.WindowPadding = ImVec2(10.0f, 8.0f);
  style.FramePadding = ImVec2(5.0f, 3.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
  style.ScrollbarSize = 10.0f;
  style.ScaleAllSizes(scale);

  ImPlot::StyleColorsLight();
}

bool detectSystemDarkTheme() {
  // Check GTK theme preference
  const char *gtk_theme = std::getenv("GTK_THEME");
  if (gtk_theme) {
    std::string theme_lower(gtk_theme);
    std::transform(theme_lower.begin(), theme_lower.end(), theme_lower.begin(), ::tolower);
    if (theme_lower.find("dark") != std::string::npos) return true;
    return false;
  }
  // Try gsettings for GNOME
  std::string result = popenReadLine("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null");
  if (!result.empty()) return result.find("dark") != std::string::npos;
  return true;  // default to dark
}

void applyCabanaTheme(int theme, float scale) {
  if (theme == LIGHT_THEME) {
    applyCabanaLightStyle(scale);
  } else if (theme == DARK_THEME) {
    applyCabanaStyle(scale);
  } else {
    // Auto: detect system preference
    if (detectSystemDarkTheme()) {
      applyCabanaStyle(scale);
    } else {
      applyCabanaLightStyle(scale);
    }
  }
}

static std::string resolveFontFile(const std::string &family, bool monospace = false) {
  std::string pattern = family;
  if (monospace) pattern += ":spacing=100";
  return popenReadLine("fc-match -f '%{file}' '" + pattern + "' 2>/dev/null");
}

static ImFont *s_bold_font = nullptr;
static ImFont *s_mono_font = nullptr;

ImFont *cabanaBoldFont() { return s_bold_font ? s_bold_font : ImGui::GetFont(); }
ImFont *cabanaMonoFont() { return s_mono_font ? s_mono_font : ImGui::GetFont(); }

void pushQtTabBarStyle() {
  const auto mix = [](const ImVec4 &a, const ImVec4 &b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
  };

  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec4 window_bg = style.Colors[ImGuiCol_WindowBg];
  const ImVec4 frame_bg = style.Colors[ImGuiCol_FrameBg];
  const ImVec4 frame_hovered = style.Colors[ImGuiCol_FrameBgHovered];
  const ImVec4 border = style.Colors[ImGuiCol_Border];

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_Tab, mix(frame_bg, window_bg, 0.18f));
  ImGui::PushStyleColor(ImGuiCol_TabHovered, mix(frame_hovered, window_bg, 0.12f));
  ImGui::PushStyleColor(ImGuiCol_TabSelected, window_bg);
  ImGui::PushStyleColor(ImGuiCol_Border, border);
}

void popQtTabBarStyle() {
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(3);
}

void loadCabanaFonts() {
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  io.Fonts->TexGlyphPadding = 1;

  const float font_px = std::max(15.5f, 13.0f * cabanaUiScale());
  const float paused_font_px = std::max(16.0f, 16.0f * cabanaUiScale());
  ImFontConfig config;
  config.OversampleH = 3;
  config.OversampleV = 3;
  config.RasterizerMultiply = 1.05f;
  config.PixelSnapH = false;

  s_bold_font = nullptr;
  s_mono_font = nullptr;

  const std::string font_path = resolveFontFile("sans-serif");
  if (!font_path.empty()) {
    io.FontDefault = io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_px, &config);
  }
  if (!io.FontDefault) {
    io.FontDefault = io.Fonts->AddFontDefault();
  }

  const std::string bold_path = resolveFontFile("sans-serif:style=Bold");
  if (!bold_path.empty()) {
    s_bold_font = io.Fonts->AddFontFromFileTTF(bold_path.c_str(), paused_font_px, &config);
  }
  if (!s_bold_font) {
    s_bold_font = io.FontDefault;
  }

  // Load monospace font for hex byte display
  const std::string mono_path = resolveFontFile("monospace", true);
  if (!mono_path.empty()) {
    s_mono_font = io.Fonts->AddFontFromFileTTF(mono_path.c_str(), font_px, &config);
  }
  if (!s_mono_font) {
    s_mono_font = io.FontDefault;
  }

  io.FontGlobalScale = 1.0f;
}

bool saveScreenshot(const std::string &path, int width, int height) {
  if (width <= 0 || height <= 0) return false;

  std::vector<unsigned char> pixels(width * height * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  // Write as PPM (portable pixmap) - no Qt dependency
  // Flip vertically since OpenGL reads bottom-up
  std::string out_path = path;
  // Force .ppm extension if original was .png/.jpg
  auto dot = out_path.rfind('.');
  if (dot != std::string::npos) out_path = out_path.substr(0, dot) + ".ppm";

  std::ofstream ofs(out_path, std::ios::binary);
  if (!ofs) return false;
  ofs << "P6\n" << width << " " << height << "\n255\n";
  for (int y = height - 1; y >= 0; --y) {
    for (int x = 0; x < width; ++x) {
      const unsigned char *px = &pixels[(y * width + x) * 4];
      ofs.write(reinterpret_cast<const char *>(px), 3);  // RGB, skip A
    }
  }
  return ofs.good();
}

bool signalFitsInMessage(const cabana::Signal &sig, int msg_size_bytes) {
  if (sig.size < 1 || msg_size_bytes < 1) return false;
  const int bit_limit = msg_size_bytes * 8;
  if (sig.start_bit < 0 || sig.start_bit >= bit_limit) return false;
  if (sig.is_little_endian) {
    return sig.start_bit + sig.size <= bit_limit;
  }
  return flipBitPos(sig.start_bit) + sig.size <= bit_limit;
}

int nextAvailableSignalBit(const cabana::Msg *msg, int msg_size_bytes) {
  const int bit_limit = msg_size_bytes * 8;
  if (!msg || bit_limit <= 0) return 0;

  // Mark used bits in logical space (byte*8 + col), matching the binary panel iteration
  std::vector<bool> used(bit_limit, false);
  for (const auto *sig : msg->getSignals()) {
    for (int j = 0; j < sig->size; ++j) {
      int pos = sig->is_little_endian ? flipBitPos(sig->start_bit + j) : flipBitPos(sig->start_bit) + j;
      if (pos >= 0 && pos < bit_limit) used[pos] = true;
    }
  }
  auto it = std::find(used.begin(), used.end(), false);
  int logical = it == used.end() ? 0 : static_cast<int>(std::distance(used.begin(), it));
  return flipBitPos(logical);
}

std::string autoDbcForFingerprint(const std::string &fingerprint) {
  if (fingerprint.empty()) return {};

  const std::string path = getExeDir() + "/dbc/car_fingerprint_to_dbc.json";
  std::ifstream ifs(path);
  if (!ifs) return {};

  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  std::string err;
  auto json = json11::Json::parse(content, err);
  if (!err.empty()) return {};

  const auto &val = json[fingerprint];
  if (val.is_null()) return {};
  return val.string_value() + ".dbc";
}

std::string streamLabel(VisionStreamType type) {
  switch (type) {
    case VISION_STREAM_ROAD: return "Road";
    case VISION_STREAM_DRIVER: return "Driver";
    case VISION_STREAM_WIDE_ROAD: return "Wide";
    default: return "Unknown";
  }
}

void nv12ToRgba(const uint8_t *y_plane, const uint8_t *uv_plane, int width, int height, int stride, std::vector<uint8_t> &rgba) {
  rgba.resize(static_cast<size_t>(width) * height * 4);
  auto clamp = [](int value) { return static_cast<uint8_t>(std::clamp(value, 0, 255)); };

  for (int y = 0; y < height; ++y) {
    const uint8_t *y_row = y_plane + y * stride;
    const uint8_t *uv_row = uv_plane + (y / 2) * stride;
    uint8_t *dst = rgba.data() + static_cast<size_t>(y) * width * 4;
    for (int x = 0; x < width; ++x) {
      const int yy = y_row[x];
      const int uv_index = (x / 2) * 2;
      const int u = uv_row[uv_index] - 128;
      const int v = uv_row[uv_index + 1] - 128;

      const int r = yy + static_cast<int>(1.402f * v);
      const int g = yy - static_cast<int>(0.344136f * u + 0.714136f * v);
      const int b = yy + static_cast<int>(1.772f * u);
      dst[x * 4 + 0] = clamp(r);
      dst[x * 4 + 1] = clamp(g);
      dst[x * 4 + 2] = clamp(b);
      dst[x * 4 + 3] = 255;
    }
  }
}

CabanaPersistentState readPersistentState() {
  return {
    .last_dir = settings.last_dir,
    .recent_files = settings.recent_files,
    .session = {
      .recent_dbc_file = settings.recent_dbc_file,
      .active_msg_id = settings.active_msg_id,
      .selected_msg_ids = settings.selected_msg_ids,
      .active_charts = settings.active_charts,
    },
  };
}

void writePersistentState(const CabanaPersistentState &state) {
  settings.last_dir = state.last_dir;
  settings.recent_files = state.recent_files;
  settings.recent_dbc_file = state.session.recent_dbc_file;
  settings.active_msg_id = state.session.active_msg_id;
  settings.selected_msg_ids = state.session.selected_msg_ids;
  settings.active_charts = state.session.active_charts;
}
