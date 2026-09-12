#include "game.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace {
struct App {
  fiada::Game game;
  int width{1280};
  int height{720};
  std::vector<std::uint32_t> pixels;
  sk_sp<SkSurface> surface;

  void resize(int w, int h) {
    width = std::max(w, 1);
    height = std::max(h, 1);
    pixels.resize(static_cast<std::size_t>(width) * height);
    const auto info = SkImageInfo::MakeN32Premul(width, height);
    surface = SkSurface::MakeRasterDirect(info, pixels.data(), width * sizeof(std::uint32_t));
    game.resize(width, height);
  }
};

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    app = static_cast<App*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
  }
  switch (message) {
    case WM_SIZE:
      if (app && wParam != SIZE_MINIMIZED) app->resize(LOWORD(lParam), HIWORD(lParam));
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(window, &ps);
      if (app && app->surface) {
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = app->width;
        bitmap.bmiHeader.biHeight = -app->height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(dc, 0, 0, app->width, app->height, 0, 0, app->width, app->height,
                      app->pixels.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
      }
      EndPaint(window, &ps);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

bool down(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }
}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
  SetProcessDPIAware();
  WNDCLASSW wc{};
  wc.lpfnWndProc = windowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"FIADAWindow";
  wc.style = CS_HREDRAW | CS_VREDRAW;
  RegisterClassW(&wc);

  App app;
  HWND window = CreateWindowExW(0, wc.lpszClassName,
      L"FIADA — Neural Racing Playground", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, instance, &app);
  if (!window) return 1;
  ShowWindow(window, showCommand);
  RECT client{};
  GetClientRect(window, &client);
  app.resize(client.right - client.left, client.bottom - client.top);

  using clock = std::chrono::steady_clock;
  constexpr float fixedStep = 1.0F / 120.0F;
  constexpr auto frameStep = std::chrono::microseconds(16667);
  auto previous = clock::now();
  auto nextFrame = previous;
  float accumulator = 0.0F;
  MSG message{};
  bool running = true;
  while (running) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) running = false;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    const auto now = clock::now();
    accumulator += std::min(std::chrono::duration<float>(now - previous).count(), 0.1F);
    previous = now;
    fiada::Input input{
      .accelerate = down('W') || down(VK_UP),
      .brake = down('S') || down(VK_DOWN),
      .left = down('A') || down(VK_LEFT),
      .right = down('D') || down(VK_RIGHT),
      .reset = down('R'),
      .drift = down(VK_SPACE),
    };
    while (accumulator >= fixedStep) {
      app.game.update(fixedStep, input);
      accumulator -= fixedStep;
    }
    if (now >= nextFrame) {
      if (app.surface) app.game.render(*app.surface->getCanvas());
      InvalidateRect(window, nullptr, FALSE);
      UpdateWindow(window);
      nextFrame = now + frameStep;
    } else {
      Sleep(1);
    }
  }
  return 0;
}

