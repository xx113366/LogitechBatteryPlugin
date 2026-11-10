// mouse_icon_data.h - 由 probe/make_mouse_icon.cpp 从用户提供的鼠标 PNG 生成
// 请勿手工编辑；重新生成：make_mouse_icon.exe <png路径>
#pragma once
#include <cstddef>
namespace icon {
constexpr int kMouseW = 42;
constexpr int kMouseH = 64;
constexpr unsigned char kMouseAccentR = 235;
constexpr unsigned char kMouseAccentG = 75;
constexpr unsigned char kMouseAccentB = 65;
constexpr bool kMouseHasAccent = false;
extern const unsigned char kMouseInk[kMouseW * kMouseH];
extern const unsigned char kMouseAccent[kMouseW * kMouseH];
} // namespace icon
