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
  if (std::string_view(commandLine).find("--wild-west-smoke") != std::string_view::npos) {
    fiada::Game game(true);game.resize(1280,720);fiada::Input confirm{};confirm.confirm=true;game.update(1.0F/120.0F,confirm);game.update(1.0F/120.0F,{});game.update(1.0F/120.0F,confirm);game.update(1.0F/120.0F,{});for(int i=0;i<5;++i){fiada::Input next{};next.next=true;game.update(1.0F/120.0F,next);game.update(1.0F/120.0F,{});}game.enableAi();
    for(int step=0;step<360*120&&game.laps()==0;++step)game.update(1.0F/120.0F,{});
    if(game.selectedTrack()!=5)return 80;
    if(game.laps()!=1)return 100+game.checkpoint();
    return game.bestLap()>120.0F?0:79;
  }
  if (std::string_view(commandLine).find("--audio-smoke") != std::string_view::npos) {
    std::array<wchar_t,32768> path{};GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    const auto wav=(std::filesystem::path(path.data()).parent_path()/L"assets/audio/engine_loop.wav").wstring();
    return PlaySoundW(wav.c_str(),nullptr,SND_FILENAME|SND_SYNC|SND_NODEFAULT)?0:77;
  }
  if (std::string_view(commandLine).find("--item-smoke") != std::string_view::npos) {
    fiada::Game horn(false);horn.beginItemTrainingEpisode(1,12345);fiada::Input use{};use.useItem=true;horn.update(1.0F/120.0F,use);if(horn.itemUses()!=1)return 75;
    fiada::Game rod(false);rod.beginItemTrainingEpisode(2,54321);rod.update(1.0F/120.0F,use);return rod.itemUses()==1&&rod.shortcutsTaken(2)==1?0:76;
  }
  if (std::string_view(commandLine).find("--lab-menu-smoke") != std::string_view::npos) {
    fiada::Game game(true);fiada::Input in{};
    in.confirm=true;game.update(1.0F/120.0F,in);in.confirm=false;game.update(1.0F/120.0F,in);
    for(int i=0;i<2;++i){in.brake=1;game.update(1.0F/120.0F,in);in.brake=0;game.update(1.0F/120.0F,in);}
    in.confirm=true;game.update(1.0F/120.0F,in);in.confirm=false;game.update(1.0F/120.0F,in);if(!game.labTrackSelection())return 72;
    for(int i=0;i<2;++i){in.brake=1;game.update(1.0F/120.0F,in);in.brake=0;game.update(1.0F/120.0F,in);}
    if(game.selectedTrack()!=2)return 73;
    in.confirm=true;game.update(1.0F/120.0F,in);
    return !game.inFrontEnd()&&game.labMode()&&game.selectedTrack()==2?0:74;
  }
  if (std::string_view(commandLine).find("--menu-smoke") != std::string_view::npos) {
    fiada::Game game(true); if(!game.inFrontEnd())return 70;
    fiada::Input input{};input.confirm=true;game.update(1.0F/120.0F,input);input.confirm=false;game.update(1.0F/120.0F,input);
    input.brake=1.0F;game.update(1.0F/120.0F,input);input.brake=0.0F;game.update(1.0F/120.0F,input);
    input.confirm=true;game.update(1.0F/120.0F,input);
    return !game.inFrontEnd()&&game.championshipMode()&&!game.labMode()?0:71;
  }
  if (std::string_view(commandLine).find("--grand-prix-smoke") != std::string_view::npos) {
    fiada::Game a(false), b(false); a.enableAi(); b.enableAi();
    for(int track=0;track<6;++track){
      if(track){fiada::Input next{};next.next=true;a.update(1.0F/120.0F,next);b.update(1.0F/120.0F,next);}
      bool moved=false;
      for(int step=0;step<1800;++step){a.update(1.0F/120.0F,{});b.update(1.0F/120.0F,{});if(step==480)moved=a.countdownComplete()&&a.playerSpeed()>1.0F;}
      if(a.deterministicHash()!=b.deterministicHash())return 30+track;
      if(a.placement()<1||a.placement()>8)return 40+track;
      if(!moved)return a.countdownComplete()?50+track:60+track;
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
  const BOOL audioStarted=PlaySoundW(engineSound.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_LOOP|SND_NODEFAULT);
  if(!audioStarted) MessageBeep(MB_ICONWARNING);
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
      .throttle = hasPad ? std::max(pad.Gamepad.bRightTrigger / 255.0F, (pad.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) ? 1.0F : 0.0F) : ((down('W') || down(VK_UP)) ? 1.0F : 0.0F),
      .brake = hasPad ? std::max(pad.Gamepad.bLeftTrigger / 255.0F, (pad.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1.0F : 0.0F) : ((down('S') || down(VK_DOWN)) ? 1.0F : 0.0F),
      .steer = hasPad ? filteredSteer : ((down('D') || down(VK_RIGHT) ? 1.0F : 0.0F) -
               (down('A') || down(VK_LEFT) ? 1.0F : 0.0F)),
      .reset = down('R'),
      .drift = down(VK_SPACE) || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_A)),
      .toggleAi = down('P'),
      .useItem = down(VK_LSHIFT) || down(VK_RSHIFT) || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_X)),
      .menu = down(VK_ESCAPE) || down(VK_BACK) || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_B)),
      .confirm = down(VK_RETURN) || (hasPad && (pad.Gamepad.wButtons & XINPUT_GAMEPAD_A)),
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

