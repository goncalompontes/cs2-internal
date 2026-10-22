#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include "cs2/features/dxgi_interposer.hpp"
#include "cs2/render/imgui_backend.hpp"

namespace {
void require(bool value, const char* message) {
  if (!value) { std::fprintf(stderr, "FAIL: %s (Win32=%lu)\n", message, GetLastError()); std::exit(1); }
}
struct Callbacks { unsigned presents = 0; unsigned resizes = 0; };
void present_callback(void* user, void*) { ++static_cast<Callbacks*>(user)->presents; }
void resize_callback(void* user) { ++static_cast<Callbacks*>(user)->resizes; }
}

int main(int argc, char** argv) {
  const bool debug_preview=argc>=2&&std::strcmp(argv[1],"--debug-preview")==0;
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"cs2-native-overlay-test";
  require(RegisterClassW(&wc) != 0, "register hidden test window");
  HWND window = CreateWindowW(wc.lpszClassName, L"Native overlay regression test", WS_OVERLAPPEDWINDOW,
                              0, 0, 160, 160, nullptr, nullptr, wc.hInstance, nullptr);
  require(window != nullptr, "hidden test window");
  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferDesc.Width = debug_preview?1280:128; desc.BufferDesc.Height = debug_preview?720:128;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 1; desc.OutputWindow = window; desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  IDXGISwapChain* chain = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  const HRESULT created = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
      0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
  if (FAILED(created)) std::fprintf(stderr, "WARP HRESULT: 0x%08lx\n", created);
  require(SUCCEEDED(created), "software D3D11 device/swapchain");

  if (argc == 2 && std::strcmp(argv[1], "--host") == 0) {
    // Disposable graphics host for loading the actual payload. No game modules
    // or game state are involved, and the window remains hidden.
    std::puts("READY");
    std::fflush(stdout);
    for (unsigned i = 0; i < 1000; ++i) {
      MSG message{};
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
      }
      chain->Present(0, 0);
      Sleep(10);
    }
    return 0;
  }

  cs2::render::ImGuiBackend backend;
  cs2::render::RenderContext rc{device, context, window, chain};
  require(backend.initialize(rc), "native renderer and window input initialization");
  if(debug_preview) {
    cs2::game::RageDebugSnapshot snapshot;snapshot.enabled=true;snapshot.auto_fire=true;snapshot.hook=true;
    snapshot.command=3372;snapshot.client_tick=3522;snapshot.tick_base=3521;snapshot.next_primary=3551;snapshot.clip=9;snapshot.snapshot_clip=9;
    snapshot.observer_ms=GetTickCount64();snapshot.captured_ms=snapshot.observer_ms;
    snapshot.calls5=3372;snapshot.callbacks=3372;snapshot.unique_commands=3360;snapshot.crc_writes=3372;
    snapshot.source.type=5;snapshot.source.item=40;snapshot.source.pellets=1;
    snapshot.source.local_eye={-1298,-2263,-148};snapshot.source.returned_eye={0,0,0};snapshot.source.eye_distance=2610;
    for(int i=0;i<static_cast<int>(cs2::game::RageDebugStage::eye);++i)snapshot.gates[i]=cs2::game::RageDebugGate::pass;
    snapshot.gates[static_cast<std::size_t>(cs2::game::RageDebugStage::eye)]=cs2::game::RageDebugGate::fail;
    snapshot.gates[static_cast<std::size_t>(cs2::game::RageDebugStage::serialization)]=cs2::game::RageDebugGate::pass;
    std::snprintf(snapshot.weapon_name,sizeof(snapshot.weapon_name),"weapon_ssg08");
    std::snprintf(snapshot.detail,sizeof(snapshot.detail),"SIMULATED: shoot-eye result invalid (0.0 0.0 0.0), local delta 2610 (limit 128)");
    snapshot.minimum_damage=20;snapshot.required_probability=.6F;
    backend.configure_rage_debug([](void* p,cs2::game::RageDebugSnapshot& out){out=*static_cast<cs2::game::RageDebugSnapshot*>(p);return true;},&snapshot);
    cs2::render::OverlayModel preview_model;
    for(int i=0;i<3;++i)backend.render(rc,preview_model);
    ID3D11Texture2D* preview_buffer=nullptr;ID3D11Texture2D* staging=nullptr;
    require(SUCCEEDED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&preview_buffer))),"preview buffer");
    D3D11_TEXTURE2D_DESC copy_desc{};preview_buffer->GetDesc(&copy_desc);
    copy_desc.BindFlags=0;copy_desc.MiscFlags=0;copy_desc.Usage=D3D11_USAGE_STAGING;copy_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    require(SUCCEEDED(device->CreateTexture2D(&copy_desc,nullptr,&staging)),"preview staging");
    context->CopyResource(staging,preview_buffer);D3D11_MAPPED_SUBRESOURCE mapped{};
    require(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped)),"preview map");
    const char* path=argc>=3?argv[2]:"build-tools/rage-debug-preview.bmp";
    FILE* file=std::fopen(path,"wb");require(file!=nullptr,"preview output");
    BITMAPFILEHEADER fh{};BITMAPINFOHEADER ih{};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih);
    ih.biSize=sizeof(ih);ih.biWidth=1280;ih.biHeight=-720;ih.biPlanes=1;ih.biBitCount=32;ih.biSizeImage=1280*720*4;fh.bfSize=fh.bfOffBits+ih.biSizeImage;
    std::fwrite(&fh,sizeof(fh),1,file);std::fwrite(&ih,sizeof(ih),1,file);
    for(unsigned y=0;y<720;++y)for(unsigned x=0;x<1280;++x){const auto* c=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;const unsigned char bgra[]={c[2],c[1],c[0],255};std::fwrite(bgra,4,1,file);}
    std::fclose(file);context->Unmap(staging,0);staging->Release();preview_buffer->Release();
    backend.shutdown();chain->Release();context->Release();device->Release();DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::printf("PASS: simulated debug panel rendered to %s\n",path);return 0;
  }

  cs2::render::OverlayModel model;
  model.draw.fill(32, 32, 96, 96, {0, 255, 0, 255});
  backend.render(rc, model);
  require(backend.frames() == 1, "native frame submitted");

  ID3D11Texture2D* buffer = nullptr;
  require(SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&buffer))), "read back buffer");
  D3D11_TEXTURE2D_DESC staging_desc{};
  buffer->GetDesc(&staging_desc);
  staging_desc.BindFlags = 0; staging_desc.MiscFlags = 0;
  staging_desc.Usage = D3D11_USAGE_STAGING; staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D* staging = nullptr;
  require(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)), "staging texture");
  context->CopyResource(staging, buffer);
  D3D11_MAPPED_SUBRESOURCE pixels{};
  require(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &pixels)), "pixel readback");
  const auto* pixel = static_cast<const std::uint8_t*>(pixels.pData) + 64 * pixels.RowPitch + 64 * 4;
  require(pixel[1] > 200 && pixel[0] < 30 && pixel[2] < 30, "overlay actually painted green pixels");
  context->Unmap(staging, 0); staging->Release(); buffer->Release();

  Callbacks callbacks;
  cs2::features::DxgiInterposer hook;
  require(hook.install(&present_callback, &resize_callback, &callbacks), "native function hook install");
  chain->Present(0, DXGI_PRESENT_TEST);
  require(callbacks.presents == 0, "skip DXGI_PRESENT_TEST");
  chain->Present(0, 0);
  require(callbacks.presents == 1, "intercept actual existing swapchain Present");
  backend.on_before_resize();
  require(SUCCEEDED(chain->ResizeBuffers(1, 96, 96, DXGI_FORMAT_UNKNOWN, 0)),
          "release back-buffer view before ResizeBuffers");
  require(callbacks.resizes == 1, "resize function intercepted");
  backend.render(rc, model);
  require(backend.frames() == 2, "render target recreated after resize");
  hook.uninstall();
  chain->Present(0, 0);
  require(callbacks.presents == 1, "Present restored after uninstall");
  backend.shutdown();
  require(GetWindowLongPtrW(window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(DefWindowProcW),
          "window procedure restored");
  context->ClearState(); context->Release(); device->Release(); chain->Release();
  DestroyWindow(window); UnregisterClassW(wc.lpszClassName, wc.hInstance);
  std::puts("PASS: native DX11 pixels, function hooks, test-present filtering, resize, target recreation, input cleanup");
}
