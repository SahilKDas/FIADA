#include "game.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

#include <windows.h>
#include <xinput.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
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

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR commandLine, int showCommand) {
  if (std::string_view(commandLine).find("--grand-prix-smoke") != std::string_view::npos) {
    fiada::Game a(false), b(false); a.enableAi(); b.enableAi();
    for(int track=0;track<5;++track){
      if(track){fiada::Input next{};next.next=true;a.update(1.0F/120.0F,next);b.update(1.0F/120.0F,next);}
      for(int step=0;step<1800;++step){a.update(1.0F/120.0F,{});b.update(1.0F/120.0F,{});}
      if(a.deterministicHash()!=b.deterministicHash()||a.placement()<1||a.placement()>8)return 8;
    }
    return 0;
  }
  if (std::string_view(commandLine).find("--ai-smoke") != std::string_view::npos) {
    fiada::Game simulation;
    simulation.resize(1280, 720);
    simulation.enableAi();
    for (int step = 0; step < 120 * 120 && simulation.laps() == 0; ++step)
      simulation.update(1.0F / 120.0F, {});
    return simulation.laps() > 0 ? 0 : 3;
  }
  SetProcessDPIAware();
  std::array<wchar_t,32768> modulePath{};
  GetModuleFileNameW(nullptr,modulePath.data(),static_cast<DWORD>(modulePath.size()));
  const auto engineSound=(std::filesystem::path(modulePath.data()).parent_path()/L"assets/audio/engine_loop.wav").wstring();
  PlaySoundW(engineSound.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_LOOP|SND_NODEFAULT);
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
    XINPUT_STATE pad{};
    const bool hasPad = XInputGetState(0, &pad) == ERROR_SUCCESS;
    const float padSteer = hasPad ? std::clamp(static_cast<float>(pad.Gamepad.sThumbLX) / 32767.0F, -1.0F, 1.0F) : 0.0F;
    const float filteredSteer = std::abs(padSteer) > 0.16F ? padSteer : 0.0F;
    fiada::Input input{
      .throttle = hasPad ? pad.Gamepad.bRightTrigger / 255.0F : ((down('W') || down(VK_UP)) ? 1.0F : 0.0F),
      .brake = hasPad ? pad.Gamepad.bLeftTrigger / 255.0F : ((down('S') || down(VK_DOWN)) ? 1.0F : 0.0F),
      .steer = hasPad ? filteredSteer : ((down('D') || down(VK_RIGHT) ? 1.0F : 0.0F) -
               (down('A') || down(VK_LEFT) ? 1.0F : 0.0F)),
      .reset = down('R'),
      .drift = down(VK_SPACE) || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_A)),
      .toggleAi = down('P'),
      .useItem = down(VK_LSHIFT) || down(VK_RSHIFT) || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_X)),
      .menu = down('C') || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_START)),
      .next = down(VK_OEM_6) || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER)),
      .previous = down(VK_OEM_4) || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)),
      .labOverlay = down('L') || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_BACK)),
    };
    while (accumulator >= fixedStep) {
      app.game.update(fixedStep, input);
      if (down(VK_TAB) && app.game.spectatorFastForwardAllowed()) app.game.update(fixedStep, input);
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

