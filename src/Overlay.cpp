#include "Overlay.h"
#include "SimulationConfig.h"
#include "simulation/ElementDefs.h"
#include "simulation/Simulation.h"
#include <algorithm>
#include "OverlayWindows.h"
#include "OverlayWalls.h"
#include "SimulationConfig.h"
#include "prefs/GlobalPrefs.h"
#include "gui/interface/Engine.h"
#include "gui/interface/Window.h"
#include "gui/game/GameController.h"
#include "gui/game/GameModel.h"
#include <vector>
#include <array>
#include <numeric>
#include <cstring>
#include <execution>
#include <cstdio>
#include <utility>
#include <mutex>
#include <thread>
#include <algorithm>
#ifdef _WIN32
# include <SDL_syswm.h>
# define WIN32_LEAN_AND_MEAN
# undef _WIN32_WINNT
# define _WIN32_WINNT 0x0602 // SHQueryUserNotificationState, DirectComposition (the build targets XP)
# include <windows.h>
# include <shellapi.h>
# include <d3d11.h>
# include <dxgi1_2.h>
# include <dcomp.h>
# include <d3dcompiler.h>
# include "FpsLimit.h"
# pragma comment(lib, "d3d11.lib")
# ifndef WS_EX_NOREDIRECTIONBITMAP
#  define WS_EX_NOREDIRECTIONBITMAP 0x00200000L // SDL_syswm.h already pulled in windows.h for the XP target
# endif
#elif defined(__APPLE__)
# include "OverlayMacWin32.h"
# include "FpsLimit.h"
namespace
{
	OverlayInput input = OverlayInput::Auto;
	OverlayLayer layer = OverlayLayer::OnTop;
	bool fullScreenApp = false; // an app filling the screen is in front: clicks pass through whatever the mode
	std::vector<uint32_t> argb;
	// Behind the icons takes no clicks, so Locked there is drawing mode, as on Windows
	OverlayLayer Shown()
	{
		return layer == OverlayLayer::BehindIcons && input == OverlayInput::Locked ? OverlayLayer::UnderWindows : layer;
	}
	void ApplyLayer()
	{
		OverlayMac::SetLayer(int(Shown()));
	}
	double Now()
	{
		return OverlayMac::Now();
	}
	// As the Windows side's: what moved between the last two sim frames, carried on by the drawing thread
	struct Moving
	{
		float x, y, vx, vy;
	};
	std::vector<Moving> moving;
	enum : uint8_t { OnParticle = 1, OnWall = 2, Moves = 4 };
	std::vector<uint8_t> frameOccupied;
	double framePeriod = 1 / 60.0, frameAt = 0;

	void TickFullScreen()
	{
		static int frames = 0;
		if (++frames % 30) return;
		static ByteString app;
		auto was = app;
		app = OverlayWindows::PassApp(nullptr);
		bool now = app.size() || (overlayAutoPass && OverlayMac::FrontFillsScreen());
		if (now == fullScreenApp) return;
		fullScreenApp = now;
		if (app.size()) OverlayWindows::Notify((app + " in front: clicks pass through").c_str());
		else OverlayWindows::Notify(now ? "Full-screen app: clicks pass through" : was.size() ? (was + " left: clicks back to normal").c_str() : "Full-screen app closed: clicks back to normal");
	}
}
#endif

bool overlayMode = false;
bool overlayWindowed = false;
bool overlayIdle = false;
bool overlayAutoPass = true;

#ifdef _WIN32
namespace
{
	HWND hwnd = nullptr;
	SDL_Window *sdlWindow = nullptr;
	constexpr uint32_t layerKey = 0x010001; // the see-through colour behind the icons (same as RGB() and 0xRRGGBB)
	OverlayInput input = OverlayInput::Auto;
	bool fullScreenApp = false; // a full-screen app or game is in front: clicks pass through whatever the mode
	OverlayLayer layer = OverlayLayer::OnTop;
	// Behind the icons takes no clicks (the icons' view covers it), so Locked there is drawing mode: the layer comes up
	// over the icons, under windows, until the input goes back to Auto or Pass-through.
	OverlayLayer Shown()
	{
		return layer == OverlayLayer::BehindIcons && input == OverlayInput::Locked ? OverlayLayer::UnderWindows : layer;
	}
	void ApplyLayer();

	// A global hotkey ("Ctrl+Alt+O"), polled each frame so it works while the window is unfocused.
	struct Hotkey
	{
		bool ctrl = false, alt = false, shift = false, win = false;
		int vk = 0;
		bool wasDown = false;

		static Hotkey Parse(ByteString text)
		{
			Hotkey k;
			for (auto &part : text.PartitionBy('+'))
			{
				auto p = part.ToUpper();
				if (p == "CTRL") k.ctrl = true;
				else if (p == "ALT") k.alt = true;
				else if (p == "SHIFT") k.shift = true;
				else if (p == "WIN") k.win = true;
				else if (p.size() == 1 && isalnum((unsigned char)p[0])) k.vk = p[0];
				else if (p.size() >= 2 && p[0] == 'F') k.vk = VK_F1 - 1 + atoi(p.c_str() + 1);
			}
			return k;
		}

		static bool Down(int vk)
		{
			return GetAsyncKeyState(vk) & 0x8000;
		}

		// True once per press.
		bool Pressed()
		{
			bool down = vk && Down(vk) && ctrl == Down(VK_CONTROL) && alt == Down(VK_MENU) && shift == Down(VK_SHIFT) &&
			            win == (Down(VK_LWIN) || Down(VK_RWIN));
			bool pressed = down && !wasDown;
			wasDown = down;
			return pressed;
		}
	};
	Hotkey cycleInput, passThroughKey, lockedKey, paletteKey, clearKey;

	bool cursorMiss = false; // GPU frames: the mouse is over a see-through pixel (our window can't tell by itself)

	void ApplyHitTest()
	{
		bool pass = input == OverlayInput::PassThrough || fullScreenApp || Shown() == OverlayLayer::BehindIcons || cursorMiss;
		auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		ex = pass ? ex | WS_EX_TRANSPARENT : ex & ~LONG_PTR(WS_EX_TRANSPARENT);
		SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
	}

	void ApplyPassThrough()
	{
		OverlayWindows::SetPassThrough(fullScreenApp);
		ApplyHitTest();
	}

	double Now()
	{
		LARGE_INTEGER f, c;
		QueryPerformanceFrequency(&f);
		QueryPerformanceCounter(&c);
		return double(c.QuadPart) / f.QuadPart;
	}

	// The particles that moved between the last two sim frames (OverlayFrameParts): the pixel each is at in the newest,
	// and how far (sim pixels) it went since the one before. Frames drawn between sim ticks carry them on at that speed,
	// so the newest frame is shown right away (no tick of latency) and still moves at the screen's rate.
	struct Moving
	{
		float x, y, vx, vy;
	};
	std::vector<Moving> moving;
	// XRES x YRES, of the shown frame (sized at the first sim frame: the sim size is set at start-up)
	enum : uint8_t { OnParticle = 1, OnWall = 2, Moves = 4 };
	std::vector<uint8_t> frameOccupied;
	double framePeriod = 1 / 60.0, frameAt = 0; // seconds between sim frames, and when the newest came
	DrawLimit busyLimit = DrawLimitExplicit{ 60 }; // the draw rate while something moves (GPU frames: the screen's)

	// Frames on the GPU: a click-through window just over ours shows a screen-sized swap chain (DirectComposition):
	// the frame goes up sim-sized and the GPU scales it (nearest pixel), with the moving particles taken out and drawn
	// as squares where they're heading, and sand on a window that moved since the sim read it moved along. That replaces UpdateLayeredWindow's full-screen copy
	// (~4.5 ms at 5120x1440). Ours stays, at alpha 1 of 255, to take the clicks: see-through pixels pass them on by
	// turning it click-through while the mouse is over one. Null without DirectComposition (Windows 7), a D3D11
	// device or the shader compiler: UpdateLayeredWindow then.
	// A particle the drawing thread carries on: from where it is in the frame at its speed (sim pixels a frame), or
	// riding the window `ride` (sand on top of a rising one)
	struct Mover
	{
		float x, y, vx, vy;
		uint32_t colour;
		void *ride;
	};
	struct Gpu
	{
		HWND view = nullptr;
		ID3D11Device *device = nullptr;
		ID3D11DeviceContext *context = nullptr;
		IDXGIFactory2 *factory = nullptr;
		IDXGISwapChain1 *chain = nullptr;
		ID3D11RenderTargetView *target = nullptr;
		IDCompositionDevice *comp = nullptr;
		IDCompositionTarget *compTarget = nullptr;
		IDCompositionVisual *visual = nullptr;
		ID3D11VertexShader *bgVs = nullptr, *partVs = nullptr;
		ID3D11PixelShader *bgPs = nullptr, *partPs = nullptr;
		ID3D11InputLayout *partLayout = nullptr;
		ID3D11BlendState *over = nullptr;
		ID3D11RasterizerState *noCull = nullptr;
		ID3D11Buffer *constants = nullptr, *parts = nullptr;
		ID3D11Texture2D *frame = nullptr;
		ID3D11ShaderResourceView *frameView = nullptr;
		int w = 0, h = 0, ww = 0, wh = 0, partsCap = 0;
		// handed to the drawing thread (GpuLoop) under `lock`, held just long enough to pass them; the D3D objects above
		// are under `draw`, which the main thread only takes to remake them (a resize)
		std::mutex lock, draw;
		std::vector<uint32_t> pending; // the newest frame
		bool fresh = false, drawing = false, shifted = false;
		std::vector<Mover> movers;
		double frameAt = 0, period = 1 / 60.0, lastT = 1;
		RECT at{};
		bool alphaSet = false; // our window's own picture is at alpha 1 (reset whenever WS_EX_LAYERED is toggled)
	};
	Gpu *gpu = nullptr;
	struct Part
	{
		float x, y; // sim pixels
		uint32_t colour;
	};

	constexpr char shaders[] = R"(
cbuffer C : register(b0) { float2 scale; float2 screen; int moved; int3 pad; int4 rects[16]; float4 offs[16]; };
Texture2D<float4> frame : register(t0);
struct V { float4 pos : SV_Position; float4 col : COLOR; };
V Bg(uint id : SV_VertexID)
{
	V o;
	float2 uv = float2((id << 1) & 2, id & 2);
	o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
	o.col = 0;
	return o;
}
// windows that moved since the frame (rects: where it had them, offs: how far they've gone, sim pixels): what is drawn on
// one goes with it, and where it was is left clear
float4 BgPs(V i) : SV_Target
{
	float2 f = i.pos.xy / scale;
	float4 c = frame.Load(int3(f, 0)), patch = 0;
	bool inNew = false, inOld = false;
	for (int k = 0; k < moved; k++)
	{
		int4 r = rects[k];
		int2 q = int2(floor(f - offs[k].xy));
		if (!inNew && q.x >= r.x && q.y >= r.y && q.x < r.z && q.y < r.w) patch = frame.Load(int3(q, 0)), inNew = true;
		if (f.x >= r.x && f.y >= r.y && f.x < r.z && f.y < r.w) inOld = true;
	}
	if (inNew && patch.a > 0) return patch;
	return inOld ? 0 : c;
}
V Part(uint id : SV_VertexID, float2 at : AT, float4 col : COLOR)
{
	float2 p = round(at * scale) + float2(id & 1, id >> 1) * scale;
	V o;
	o.pos = float4(p / screen * float2(2, -2) + float2(-1, 1), 0, 1);
	o.col = col.bgra;
	return o;
}
float4 PartPs(V i) : SV_Target { return i.col; }
)";

	struct Constants // the shaders' cbuffer C
	{
		float scale[2], screen[2];
		int moved, pad[3];
		int rects[16][4];
		float offs[16][4];
	};

	Gpu *MakeGpu()
	{
		auto create = (decltype(&DCompositionCreateDevice))GetProcAddress(LoadLibraryW(L"dcomp.dll"), "DCompositionCreateDevice");
		auto compile = (pD3DCompile)GetProcAddress(LoadLibraryW(L"d3dcompiler_47.dll"), "D3DCompile");
		if (!create || !compile) return nullptr;
		auto *g = new Gpu;
		WNDCLASSW wc{};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"TPTOverlayView";
		RegisterClassW(&wc);
		// owned by ours: always just above it in the z-order, whichever layer
		g->view = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		                          wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, hwnd, nullptr, wc.hInstance, nullptr);
		IDXGIDevice *dxgi = nullptr;
		IDXGIAdapter *adapter = nullptr;
		bool ok = g->view && SetLayeredWindowAttributes(g->view, 0, 255, LWA_ALPHA) &&
		          SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
		                                      D3D11_SDK_VERSION, &g->device, nullptr, &g->context)) &&
		          SUCCEEDED(g->device->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
		          SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&g->factory))) && SUCCEEDED(create(dxgi, IID_PPV_ARGS(&g->comp))) &&
		          SUCCEEDED(g->comp->CreateTargetForHwnd(g->view, TRUE, &g->compTarget)) && SUCCEEDED(g->comp->CreateVisual(&g->visual)) &&
		          SUCCEEDED(g->compTarget->SetRoot(g->visual));
		IDXGIDevice1 *dxgi1 = nullptr; // one frame queued, not three: each would show the sim a frame later
		if (ok && SUCCEEDED(dxgi->QueryInterface(IID_PPV_ARGS(&dxgi1)))) dxgi1->SetMaximumFrameLatency(1), dxgi1->Release();
		if (adapter) adapter->Release();
		if (dxgi) dxgi->Release();
		auto shader = [&](const char *entry, const char *target) {
			ID3DBlob *code = nullptr, *errors = nullptr;
			if (FAILED(compile(shaders, sizeof(shaders) - 1, nullptr, nullptr, nullptr, entry, target, 0, 0, &code, &errors)))
			{
				fprintf(stderr, "overlay: shader %s: %s\n", entry, errors ? (const char *)errors->GetBufferPointer() : "?");
				code = nullptr;
			}
			if (errors) errors->Release();
			return code;
		};
		ID3DBlob *bgVs = nullptr, *bgPs = nullptr, *partVs = nullptr, *partPs = nullptr;
		if (ok)
		{
			bgVs = shader("Bg", "vs_4_0"), bgPs = shader("BgPs", "ps_4_0"), partVs = shader("Part", "vs_4_0"), partPs = shader("PartPs", "ps_4_0");
			D3D11_INPUT_ELEMENT_DESC layout[] = {
				{ "AT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			};
			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
			                          D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
			D3D11_RASTERIZER_DESC raster{};
			raster.FillMode = D3D11_FILL_SOLID;
			raster.CullMode = D3D11_CULL_NONE;
			raster.DepthClipEnable = TRUE;
			D3D11_BUFFER_DESC cb{ sizeof(Constants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
			ok = bgVs && bgPs && partVs && partPs &&
			     SUCCEEDED(g->device->CreateVertexShader(bgVs->GetBufferPointer(), bgVs->GetBufferSize(), nullptr, &g->bgVs)) &&
			     SUCCEEDED(g->device->CreatePixelShader(bgPs->GetBufferPointer(), bgPs->GetBufferSize(), nullptr, &g->bgPs)) &&
			     SUCCEEDED(g->device->CreateVertexShader(partVs->GetBufferPointer(), partVs->GetBufferSize(), nullptr, &g->partVs)) &&
			     SUCCEEDED(g->device->CreatePixelShader(partPs->GetBufferPointer(), partPs->GetBufferSize(), nullptr, &g->partPs)) &&
			     SUCCEEDED(g->device->CreateInputLayout(layout, 2, partVs->GetBufferPointer(), partVs->GetBufferSize(), &g->partLayout)) &&
			     SUCCEEDED(g->device->CreateBlendState(&blend, &g->over)) &&
			     SUCCEEDED(g->device->CreateRasterizerState(&raster, &g->noCull)) &&
			     SUCCEEDED(g->device->CreateBuffer(&cb, nullptr, &g->constants));
		}
		for (auto *b : { bgVs, bgPs, partVs, partPs })
			if (b) b->Release();
		if (!ok)
		{
			fprintf(stderr, "overlay: no GPU frames, UpdateLayeredWindow instead\n");
			// ponytail: the device and its objects leak here, once
			if (g->view) DestroyWindow(g->view);
			return nullptr;
		}
		return g;
	}

	// The drawing thread: at each screen refresh, the newest frame the main thread handed over, its moving particles
	// carried on, and every window that moved since slid along with what is drawn on it (holes, marks, the sand on
	// top). A dragged window keeps up at the screen's rate however slow the sim and the frames behind it are. Only
	// draws when something changed.
	void GpuLoop(Gpu *gp)
	{
		auto &g = *gp;
		IDXGIOutput *out = nullptr;
		std::vector<Part> parts;
		std::vector<uint32_t> frame; // the newest frame, swapped out of pending
		for (;;)
		{
			if (out) out->WaitForVBlank();
			else Sleep(4);
			auto shifts = OverlayWindows::WindowShifts();
			if (shifts.size() > 16) shifts.resize(16); // ponytail: 16 moving at once; more just don't slide till the next frame
			std::unique_lock lock(g.lock);
			bool fresh = g.fresh;
			if (fresh) std::swap(g.pending, frame), g.fresh = false;
			float t = float(std::clamp((Now() - g.frameAt) / g.period, 0.0, 1.0));
			bool sliding = !g.movers.empty() && g.lastT < 1;
			if (!fresh && shifts.empty() && !g.shifted && !sliding) continue;
			g.shifted = !shifts.empty(), g.lastT = t;
			parts.clear();
			for (auto &m : g.movers)
			{
				float y = m.y + m.vy * t;
				if (m.ride)
					for (auto &s : shifts)
						if (s.id == m.ride) y += s.dy;
				parts.push_back({ m.x + m.vx * t, y, m.colour });
			}
			lock.unlock(); // the main thread can hand over the next frame while this one is drawn
			std::lock_guard draw(g.draw);
			if (!out && g.chain) g.chain->GetContainingOutput(&out);
			if (!g.chain || !g.target || !g.frame) continue;
			if (fresh && frame.size() == size_t(g.w) * g.h) g.context->UpdateSubresource(g.frame, 0, nullptr, frame.data(), g.w * 4, 0);
			if (int(parts.size()) > g.partsCap)
			{
				if (g.parts) g.parts->Release(), g.parts = nullptr;
				g.partsCap = std::max(int(parts.size()) * 2, 4096);
				D3D11_BUFFER_DESC d{ UINT(g.partsCap * sizeof(Part)), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
				if (FAILED(g.device->CreateBuffer(&d, nullptr, &g.parts))) g.partsCap = 0, parts.clear();
			}
			D3D11_MAPPED_SUBRESOURCE map;
			if (!parts.empty() && SUCCEEDED(g.context->Map(g.parts, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
			{
				std::memcpy(map.pData, parts.data(), parts.size() * sizeof(Part));
				g.context->Unmap(g.parts, 0);
			}
			if (SUCCEEDED(g.context->Map(g.constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
			{
				Constants c{ { float(g.ww) / g.w, float(g.wh) / g.h }, { float(g.ww), float(g.wh) }, int(shifts.size()) };
				for (size_t k = 0; k < shifts.size(); k++)
				{
					auto &s = shifts[k];
					c.rects[k][0] = s.x0, c.rects[k][1] = s.y0, c.rects[k][2] = s.x1, c.rects[k][3] = s.y1;
					c.offs[k][0] = s.dx, c.offs[k][1] = s.dy;
				}
				std::memcpy(map.pData, &c, sizeof(c));
				g.context->Unmap(g.constants, 0);
			}

			D3D11_VIEWPORT vp{ 0, 0, float(g.ww), float(g.wh), 0, 1 };
			g.context->RSSetViewports(1, &vp);
			g.context->RSSetState(g.noCull);
			g.context->OMSetRenderTargets(1, &g.target, nullptr);
			g.context->VSSetConstantBuffers(0, 1, &g.constants);
			g.context->PSSetConstantBuffers(0, 1, &g.constants);
			// the frame, scaled up, the moved windows slid
			g.context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
			g.context->IASetInputLayout(nullptr);
			g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			g.context->VSSetShader(g.bgVs, nullptr, 0);
			g.context->PSSetShader(g.bgPs, nullptr, 0);
			g.context->PSSetShaderResources(0, 1, &g.frameView);
			g.context->Draw(3, 0);
			// the moving particles, over it
			if (!parts.empty())
			{
				UINT stride = sizeof(Part), offset = 0;
				g.context->OMSetBlendState(g.over, nullptr, 0xFFFFFFFF);
				g.context->IASetInputLayout(g.partLayout);
				g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				g.context->IASetVertexBuffers(0, 1, &g.parts, &stride, &offset);
				g.context->VSSetShader(g.partVs, nullptr, 0);
				g.context->PSSetShader(g.partPs, nullptr, 0);
				g.context->DrawInstanced(4, UINT(parts.size()), 0, 0);
			}
			g.chain->Present(0, 0);
		}
	}

	// One frame (premultiplied, w x h; the moving particles' pixels get `empty`) for a ww x wh window. False: not
	// shown, use UpdateLayeredWindow.
	bool GpuPresent(uint32_t *argb, int w, int h, int ww, int wh, uint32_t empty)
	{
		static bool tried = false;
		if (!tried)
		{
			tried = true;
			gpu = MakeGpu();
			// draw at the screen's rate; the sim keeps its own (FpsLimit) and the frames between slide the particles
			if (gpu) busyLimit = DrawLimitDisplay{};
		}
		if (!gpu) return false;
		auto &g = *gpu;
		// Behind the icons ours is a child of Explorer's desktop window (ApplyLayer): the view goes in with it, just
		// above it, and ours turns invisible (it takes no clicks there).
		HWND desk = GetParent(hwnd);
		if (GetAncestor(g.view, GA_PARENT) != (desk ? desk : GetDesktopWindow()))
		{
			auto style = GetWindowLongPtrW(g.view, GWL_STYLE);
			SetWindowLongPtrW(g.view, GWL_STYLE, desk ? (style | WS_CHILD) & ~LONG_PTR(WS_POPUP) : (style & ~LONG_PTR(WS_CHILD)) | WS_POPUP);
			SetParent(g.view, desk);
			if (!desk) SetWindowLongPtrW(g.view, GWLP_HWNDPARENT, LONG_PTR(hwnd)); // owned by ours again: just above it
			g.at = {};
			g.alphaSet = false;
		}
		if (!g.alphaSet) g.alphaSet = SetLayeredWindowAttributes(hwnd, 0, desk ? 0 : 1, LWA_ALPHA);
		RECT wr;
		GetWindowRect(hwnd, &wr);
		if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
		{
			ShowWindow(g.view, SW_HIDE);
			return true;
		}
		HWND above = desk ? GetWindow(hwnd, GW_HWNDPREV) : nullptr; // what the view goes under, to sit on ours
		if (!EqualRect(&wr, &g.at) || !IsWindowVisible(g.view) || (desk && above != g.view))
		{
			g.at = wr;
			RECT r = wr;
			if (desk) MapWindowPoints(HWND_DESKTOP, desk, (POINT *)&r, 2);
			SetWindowPos(g.view, desk ? (above ? above : HWND_TOP) : nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
			             (desk ? 0 : SWP_NOZORDER) | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		}
		// clicks: ours takes them only over a drawn pixel (or alpha 1: empty space that catches clicks)
		POINT cur;
		GetCursorPos(&cur);
		int cx = int((cur.x - wr.left) * int64_t(w) / std::max(1L, wr.right - wr.left));
		int cy = int((cur.y - wr.top) * int64_t(h) / std::max(1L, wr.bottom - wr.top));
		bool miss = desk || cx < 0 || cy < 0 || cx >= w || cy >= h || !(argb[size_t(cy) * w + cx] >> 24);
		if (miss != cursorMiss)
		{
			cursorMiss = miss;
			ApplyHitTest();
		}

		std::unique_lock draw(g.draw, std::defer_lock); // remade below: not while the drawing thread draws with them
		if (!g.chain || ww != g.ww || wh != g.wh || !g.frame || w != g.w || h != g.h) draw.lock();
		if (!g.chain || ww != g.ww || wh != g.wh)
		{
			if (g.target) g.target->Release(), g.target = nullptr;
			if (g.chain)
			{
				g.context->ClearState(); // let go of the old buffers
				g.chain->ResizeBuffers(2, ww, wh, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
			}
			else
			{
				DXGI_SWAP_CHAIN_DESC1 d{};
				d.Width = ww;
				d.Height = wh;
				d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				d.SampleDesc.Count = 1;
				d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				d.BufferCount = 2;
				d.Scaling = DXGI_SCALING_STRETCH;
				d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
				d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
				if (FAILED(g.factory->CreateSwapChainForComposition(g.device, &d, nullptr, &g.chain))) return false;
				g.visual->SetContent(g.chain);
				g.comp->Commit();
			}
			ID3D11Texture2D *buffer = nullptr;
			if (FAILED(g.chain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
			g.device->CreateRenderTargetView(buffer, nullptr, &g.target); // with flip model, buffer 0 is always the back one
			buffer->Release();
			g.ww = ww, g.wh = wh;
		}
		if (!g.frame || w != g.w || h != g.h)
		{
			if (g.frameView) g.frameView->Release(), g.frameView = nullptr;
			if (g.frame) g.frame->Release(), g.frame = nullptr;
			D3D11_TEXTURE2D_DESC d{};
			d.Width = w;
			d.Height = h;
			d.MipLevels = d.ArraySize = 1;
			d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			d.SampleDesc.Count = 1;
			d.Usage = D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(g.device->CreateTexture2D(&d, nullptr, &g.frame)) ||
			    FAILED(g.device->CreateShaderResourceView(g.frame, nullptr, &g.frameView))) return false;
			g.w = w, g.h = h;
		}

		// out of the frame, into squares: first the sand on top of a window that rose since the sim last read it (it
		// would sink into the window by up to a tick): it rides the window, the drawing thread lifting it as the window
		// goes. Only rising: the sim doesn't carry a pile sideways (the window slides out from under it) and a falling
		// window leaves it to gravity.
		std::vector<Mover> movers;
		for (auto &s : OverlayWindows::WindowShifts())
			for (int x = std::max(s.x0, 0); s.dy < 0 && x < std::min(s.x1, w); x++)
				for (int y = std::min(s.y0, h) - 1; y >= 0; y--)
				{
					auto &px = argb[size_t(y) * w + x];
					if (px >> 24 < 2) break; // the top of the pile
					movers.push_back({ float(x), float(y), 0, 0, px, s.id });
					px = empty;
				}
		// then the moving particles, carried on at their speed (one tick at most); not into a still particle or a
		// wall, where the sim will stop them: overshooting and snapping back would flicker
		for (auto &m : moving)
		{
			int x = int(m.x), y = int(m.y);
			if (x < 0 || y < 0 || x >= w || y >= h) continue;
			auto &px = argb[size_t(y) * w + x];
			if (px >> 24 < 2) continue; // under a window or a panel, or already taken
			int nx = int(std::lround(m.x + m.vx)), ny = int(std::lround(m.y + m.vy));
			bool open = nx >= 0 && ny >= 0 && nx < XRES && ny < YRES && size_t(ny) * XRES + nx < frameOccupied.size();
			if (open)
			{
				auto o = frameOccupied[size_t(ny) * XRES + nx];
				open = !(o & OnWall) && (!(o & OnParticle) || (o & Moves));
			}
			movers.push_back({ m.x, m.y, open ? m.vx : 0, open ? m.vy : 0, px, nullptr });
			px = empty;
		}
		// to the drawing thread, which puts it on the screen at its next refresh
		if (draw.owns_lock()) draw.unlock();
		static std::vector<uint32_t> spare;
		spare.assign(argb, argb + size_t(w) * h);
		std::lock_guard lock(g.lock);
		std::swap(g.pending, spare);
		g.fresh = true;
		g.movers = std::move(movers);
		g.frameAt = frameAt, g.period = framePeriod;
		if (!g.drawing) g.drawing = true, std::thread(GpuLoop, &g).detach();
		return true;
	}

	void SetInput(OverlayInput newInput, bool announce = true)
	{
		input = newInput;
		if (layer == OverlayLayer::BehindIcons) ApplyLayer(); // in or out of drawing mode
		ApplyPassThrough();
		if (announce)
		{
			static const char *names[] = { "Input: Auto", "Input: Pass-through", "Input: Locked to TPT" };
			OverlayWindows::Notify(names[int(input)]);
		}
		GlobalPrefs::Ref().Set("Overlay.Input", int(input));
	}

	// The desktop window the wallpaper is drawn in, under the icons; parenting to it puts us behind the icons (the
	// trick live-wallpaper apps use). Returns the parent and the window to sit just under, if any.
	std::pair<HWND, HWND> WallpaperLayer()
	{
		HWND progman = FindWindowW(L"Progman", nullptr);
		SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, nullptr); // makes Explorer split off a WorkerW
		HWND worker = nullptr;
		EnumWindows([](HWND top, LPARAM out) -> BOOL {
			if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr))
			{
				*(HWND *)out = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
			}
			return TRUE;
		}, LPARAM(&worker));
		if (worker) return { worker, nullptr };                                       // up to Windows 11 23H2
		return { progman, FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr) }; // 24H2: under the icons' view
	}

	void ApplyLayer()
	{
		static RECT screen; // where the layer sits as a top-level window
		auto [desk, under] = Shown() == OverlayLayer::BehindIcons ? WallpaperLayer() : std::pair<HWND, HWND>{};
		// SDL re-applies its own always-on-top whenever it moves or raises the window, which would lift the lower
		// layers (the wallpaper one over the icons)
		SDL_SetWindowAlwaysOnTop(sdlWindow, Shown() == OverlayLayer::OnTop ? SDL_TRUE : SDL_FALSE);
		auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
		if (desk && !GetParent(hwnd))
		{
			// the corner of all the monitors, where the overlay sits (at startup the window is still centred, not there yet)
			screen.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
			screen.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
			SetWindowLongPtrW(hwnd, GWL_STYLE, (style | WS_CHILD) & ~LONG_PTR(WS_POPUP));
			SetParent(hwnd, desk);
			// Colour-keyed instead of per-pixel alpha (OverlayPresent); dropping WS_EX_LAYERED resets the mode
			// (and not topmost, from ApplyOverlay, or it stays over the icons whatever the order)
			auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~LONG_PTR(WS_EX_TOPMOST);
			SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~LONG_PTR(WS_EX_LAYERED));
			SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
			SetLayeredWindowAttributes(hwnd, layerKey, 0, LWA_COLORKEY);
			if (gpu) gpu->alphaSet = false; // GpuPresent moves the view in with it
			// see-through until the first frame (an unpainted window is black over the whole desktop)
			RECT rc;
			GetClientRect(hwnd, &rc);
			HDC dc = GetDC(hwnd);
			HBRUSH key = CreateSolidBrush(layerKey);
			FillRect(dc, &rc, key);
			DeleteObject(key);
			ReleaseDC(hwnd, dc);
		}
		else if (!desk && GetParent(hwnd))
		{
			SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~LONG_PTR(WS_CHILD)) | WS_POPUP); // before SetParent(nullptr)
			SetParent(hwnd, nullptr);
			auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE); // back to per-pixel alpha
			SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~LONG_PTR(WS_EX_LAYERED));
			SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
			if (gpu) gpu->alphaSet = false;
			SetWindowPos(hwnd, nullptr, screen.left, screen.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		if (desk)
		{
			// Child coordinates are the desktop window's: put the layer back over this monitor.
			POINT origin{ screen.left, screen.top };
			ScreenToClient(desk, &origin);
			SetWindowPos(hwnd, under ? under : HWND_TOP, origin.x, origin.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
		else if (Shown() == OverlayLayer::OnTop)
		{
			SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
		else
		{
			SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		ApplyPassThrough();
	}

	// Under windows: clicking the layer brings it up; send it back down. Behind the icons (24H2 and later): TPT
	// raising its window puts it over the icons' view; put it back under.
	void TickLayer()
	{
		static int frames = 0;
		if (Shown() == OverlayLayer::BehindIcons && GetParent(hwnd) && !(++frames % 10))
		{
			HWND icons = FindWindowExW(GetParent(hwnd), nullptr, L"SHELLDLL_DefView", nullptr);
			if (icons && GetWindow(hwnd, GW_HWNDPREV) != icons)
				SetWindowPos(hwnd, icons, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		if (Shown() == OverlayLayer::UnderWindows && !(++frames % 10) && GetWindow(hwnd, GW_HWNDNEXT))
		{
			// ponytail: anything after us in the z-order counts; fine as long as nothing else pins itself to the bottom
			HWND next = GetWindow(hwnd, GW_HWNDNEXT);
			bool onlyDesktop = true;
			for (; next; next = GetWindow(next, GW_HWNDNEXT))
				if (IsWindowVisible(next) && !IsIconic(next))
				{
					wchar_t cls[32];
					GetClassNameW(next, cls, 32);
					if (wcscmp(cls, L"Progman") && wcscmp(cls, L"WorkerW")) { onlyDesktop = false; break; }
				}
			if (!onlyDesktop) SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}

	// Twice a second: is a full-screen app (a game, a video, a presentation) in front of the desktop?
	void TickFullScreen()
	{
		static int frames = 0;
		if (++frames % 30) return;
		bool now = false;
		// an app on the Overlay tab's list in front: pass-through whether it's full screen or not
		static ByteString app;
		auto was = app;
		app = OverlayWindows::PassApp(GetForegroundWindow());
		QUERY_USER_NOTIFICATION_STATE state;
		if (app.size()) now = true;
		else if (overlayAutoPass && SHQueryUserNotificationState(&state) == S_OK)
		{
			DWORD pid = 0;
			HWND fg = GetForegroundWindow();
			GetWindowThreadProcessId(fg, &pid);
			// "Busy" is any full-screen window, TPT's own sim included: it only counts when the window in front fills
			// its screen itself (the desktop doesn't).
			auto fills = [fg] {
				wchar_t cls[16]{};
				GetClassNameW(fg, cls, 16);
				MONITORINFO mi{ sizeof(mi) };
				RECT r;
				return wcscmp(cls, L"Progman") && wcscmp(cls, L"WorkerW") && GetWindowRect(fg, &r) &&
				       GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi) && r.left <= mi.rcMonitor.left &&
				       r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
			};
			now = fg && pid != GetCurrentProcessId() &&
			      (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE || (state == QUNS_BUSY && fills()));
		}
		if (now == fullScreenApp) return;
		fullScreenApp = now;
		ApplyPassThrough();
		if (app.size()) OverlayWindows::Notify((app + " in front: clicks pass through").c_str());
		else OverlayWindows::Notify(now ? "Full-screen app: clicks pass through" : was.size() ? (was + " left: clicks back to normal").c_str() : "Full-screen app closed: clicks back to normal");
	}

	// A top-down 32-bit DIB the frame is scaled into, then handed to UpdateLayeredWindow.
	std::vector<uint32_t> argb;
	HDC memDC = nullptr;
	HBITMAP dib = nullptr;
	uint32_t *bits = nullptr;
	int dibW = 0, dibH = 0;
}
#endif

void OverlaySetLayer(OverlayLayer newLayer)
{
#ifdef _WIN32
	layer = newLayer;
	GlobalPrefs::Ref().Set("Overlay.Layer", int(layer));
	ApplyLayer();
#elif defined(__APPLE__)
	layer = newLayer;
	GlobalPrefs::Ref().Set("Overlay.Layer", int(layer));
	ApplyLayer();
#endif
}

OverlayLayer OverlayGetLayer()
{
#if defined(_WIN32) || defined(__APPLE__)
	return layer;
#else
	return OverlayLayer::OnTop;
#endif
}

void OverlaySetInput(OverlayInput newInput)
{
#ifdef _WIN32
	SetInput(newInput);
#elif defined(__APPLE__)
	input = newInput;
	GlobalPrefs::Ref().Set("Overlay.Input", int(input));
	if (layer == OverlayLayer::BehindIcons) ApplyLayer(); // in or out of drawing mode
	static const char *names[] = { "Input: Auto", "Input: Pass-through", "Input: Locked to TPT" };
	OverlayWindows::Notify(names[int(input)]);
#else
	(void)newInput;
#endif
}

void OverlayLoadKeys()
{
#ifdef _WIN32
	auto &prefs = GlobalPrefs::Ref();
	cycleInput = Hotkey::Parse(prefs.Get("Overlay.Keys.CycleInput", ByteString("Ctrl+Alt+O")));
	passThroughKey = Hotkey::Parse(prefs.Get("Overlay.Keys.PassThrough", ByteString("Ctrl+Alt+P")));
	lockedKey = Hotkey::Parse(prefs.Get("Overlay.Keys.Locked", ByteString("Ctrl+Alt+L")));
	paletteKey = Hotkey::Parse(prefs.Get("Overlay.Keys.Palette", ByteString("Ctrl+Alt+E")));
	clearKey = Hotkey::Parse(prefs.Get("Overlay.Keys.Clear", ByteString("Ctrl+Alt+C")));
#elif defined(__APPLE__)
	auto &prefs = GlobalPrefs::Ref();
	OverlayMac::SetHotkeys({ prefs.Get("Overlay.Keys.CycleInput", ByteString("Ctrl+Alt+O")), prefs.Get("Overlay.Keys.PassThrough", ByteString("Ctrl+Alt+P")),
	                         prefs.Get("Overlay.Keys.Locked", ByteString("Ctrl+Alt+L")), prefs.Get("Overlay.Keys.Palette", ByteString("Ctrl+Alt+E")),
	                         prefs.Get("Overlay.Keys.Clear", ByteString("Ctrl+Alt+C")) });
#endif
}

OverlayInput OverlayGetInput()
{
#if defined(_WIN32) || defined(__APPLE__)
	return input;
#else
	return OverlayInput::Auto;
#endif
}

int OverlayDefaultScale()
{
#ifdef _WIN32
	HDC screen = GetDC(nullptr);
	float dpiScale = GetDeviceCaps(screen, LOGPIXELSY) / 96.f;
	ReleaseDC(nullptr, screen);
	return std::max(1, int(std::lround(2 * std::max(dpiScale, GetSystemMetrics(SM_CYSCREEN) / 1080.f))));
#elif defined(__APPLE__)
	return 1; // a sim pixel is a point: two pixels on Retina, the size Windows picks for a 1080p screen
#else
	return 1;
#endif
}

std::pair<Vec2<int>, int> OverlaySimSize(int scale)
{
#ifdef _WIN32
	Vec2<int> screen(GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)); // every monitor
	// any size: past MAX_NPART pixels the particle count is what's capped (SetSimSize)
	scale = std::max(scale, 1);
	bool menus = GlobalPrefs::Ref().Get("Overlay.ShowTPTMenus", false);
	return { (screen / scale - (menus ? Vec2(BARSIZE, MENUSIZE) : Vec2(0, 0))) / CELL, scale }; // hidden menus: off screen
#elif defined(__APPLE__)
	auto &prefs = GlobalPrefs::Ref();
	OverlayMac::SetScreen(prefs.Get("Overlay.Screen", 0));
	bool menus = prefs.Get("Overlay.ShowTPTMenus", false);
	int sw, sh;
	OverlayMac::ScreenSize(sw, sh, !menus);
	scale = std::max(scale, 1);
	return { (Vec2(sw, sh) / scale - (menus ? Vec2(BARSIZE, MENUSIZE) : Vec2(0, 0))) / CELL, scale }; // hidden menus: off screen
#else
	return { CELLS, scale };
#endif
}

void ApplyOverlay(SDL_Window *window)
{
#ifdef _WIN32
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info))
	{
		return;
	}
	hwnd = info.info.win.window;
	sdlWindow = window;
	if (overlayWindowed)
	{
		OverlayLoadKeys();
		OverlayWindows::Init(hwnd);
		return;
	}
	SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
	// SDL_WINDOW_ALWAYS_ON_TOP alone doesn't stick once the style changes above; set it on the window itself.
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	auto &prefs = GlobalPrefs::Ref();
	OverlayLoadKeys();
	OverlayWindows::Init(hwnd);
	overlayAutoPass = prefs.Get("Overlay.AutoPassThrough", true);
	SetInput(prefs.Get("Overlay.Input", OverlayInput::Count, OverlayInput::Auto), false);
	layer = prefs.Get("Overlay.Layer", OverlayLayer::Count, OverlayLayer::OnTop);
	if (layer != OverlayLayer::OnTop) ApplyLayer();
#elif defined(__APPLE__)
	if (overlayWindowed) OverlayMac::InitMenu();
	else OverlayMac::Init(window);
	auto &prefs = GlobalPrefs::Ref();
	OverlayLoadKeys();
	OverlayWindows::Init(window);
	overlayAutoPass = prefs.Get("Overlay.AutoPassThrough", true);
	input = prefs.Get("Overlay.Input", OverlayInput::Count, OverlayInput::Auto);
	layer = prefs.Get("Overlay.Layer", OverlayLayer::Count, OverlayLayer::OnTop);
	if (Shown() != OverlayLayer::OnTop) ApplyLayer();
#else
	(void)window; // ponytail: Windows and macOS only; X11/Wayland each need their own shape/input-region code
#endif
}

void OverlayTickUI()
{
#ifdef _WIN32
	if (OverlayWindows::RecordingKey())
	{
		// the keys being recorded must not also fire
	}
	else if (cycleInput.Pressed())
	{
		SetInput(OverlayInput((int(input) + 1) % int(OverlayInput::Count)));
	}
	else if (passThroughKey.Pressed())
	{
		SetInput(input == OverlayInput::PassThrough ? OverlayInput::Auto : OverlayInput::PassThrough);
	}
	else if (lockedKey.Pressed())
	{
		SetInput(input == OverlayInput::Locked ? OverlayInput::Auto : OverlayInput::Locked);
	}
	else if (paletteKey.Pressed())
	{
		OverlayWindows::TogglePalette();
	}
	else if (clearKey.Pressed())
	{
		OverlayWindows::ClearSim();
	}
	OverlayWindows::Tick();
#elif defined(__APPLE__)
	for (int key; (key = OverlayMac::TakeHotkey()) >= 0;)
	{
		if (OverlayWindows::RecordingKey()) continue; // the keys being recorded must not also fire
		if (key == 0) OverlaySetInput(OverlayInput((int(input) + 1) % int(OverlayInput::Count)));
		if (key == 1) OverlaySetInput(input == OverlayInput::PassThrough ? OverlayInput::Auto : OverlayInput::PassThrough);
		if (key == 2) OverlaySetInput(input == OverlayInput::Locked ? OverlayInput::Auto : OverlayInput::Locked);
		if (key == 3) OverlayWindows::TogglePalette();
		if (key == 4) OverlayWindows::ClearSim();
	}
	OverlayWindows::Tick();
#endif
}

void OverlayFrameParts(const RenderableSimulation &sim, bool zoom)
{
#if defined(_WIN32) || defined(__APPLE__)
	static std::vector<float> lastX, lastY;
	static std::vector<int> lastType;
	double now = Now();
	if (frameAt) framePeriod = std::clamp(framePeriod + (now - frameAt - framePeriod) * 0.1, 1 / 240.0, 1 / 20.0);
	frameAt = now;
	moving.clear();
	frameOccupied.assign(size_t(XRES) * YRES, 0);
	// the zoom window is a magnified copy of the unmoved frame: what's under it isn't slid
	auto magnifier = RectSized(Vec2{ 0, 0 }, Vec2{ 0, 0 });
	if (zoom)
	{
		auto *model = GameController::Ref().GetModel();
		magnifier = RectSized(model->GetZoomWindowPosition() - Vec2{ 2, 2 }, Vec2{ 1, 1 } * (model->GetZoomSize() * model->GetZoomFactor() + 4));
	}
	int n = sim.parts.active;
	if (int(lastType.size()) < n) lastX.resize(n), lastY.resize(n), lastType.resize(n, 0);
	for (int i = 0; i < n; i++)
	{
		auto &p = sim.parts[i];
		// the same slot and type a frame ago, and close: the same particle (a reused slot is a new one)
		int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
		bool moves = false;
		if (p.type && p.type == lastType[i])
		{
			float vx = p.x - lastX[i], vy = p.y - lastY[i];
			if ((vx != 0 || vy != 0) && std::abs(vx) < 8 && std::abs(vy) < 8 && !magnifier.Contains({ x, y }))
				moving.push_back({ float(x), float(y), vx, vy }), moves = true;
		}
		lastType[i] = p.type, lastX[i] = p.x, lastY[i] = p.y;
		if (p.type && x >= 0 && y >= 0 && x < XRES && y < YRES) frameOccupied[y * XRES + x] = OnParticle | (moves ? Moves : 0);
	}
	std::fill(lastType.begin() + n, lastType.end(), 0);
	OverlayWindows::FrameWalls(frameOccupied, OnWall);
#else
	(void)sim, (void)zoom;
#endif
}

void OverlayPresent(SDL_Window *window, const uint32_t *vid, int w, int h)
{
#ifdef _WIN32
	if (!hwnd)
	{
		return;
	}
	TickFullScreen();
	TickLayer();
	OverlayTickUI();

	int ww, wh;
	SDL_GetWindowSize(window, &ww, &wh);
	if (ww != dibW || wh != dibH)
	{
		if (dib) DeleteObject(dib);
		if (!memDC) memDC = CreateCompatibleDC(nullptr);
		BITMAPINFO bi{};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = ww;
		bi.bmiHeader.biHeight = -wh;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
		SelectObject(memDC, dib);
		dibW = ww;
		dibH = wh;
	}
	// Black is alpha 0: see-through, and clicks on it reach the desktop. Catching clicks gives it alpha 1 instead:
	// still invisible, but it catches clicks. Black particles stay solid, and hidden menu bars are always clear.
	// Auto: empty space catches clicks while TPT is the app in front, and passes them once another app is.
	DWORD fg = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &fg);
	bool catchEmpty = input == OverlayInput::Locked || (input == OverlayInput::Auto && fg == GetCurrentProcessId());
	uint32_t empty = catchEmpty ? 0x01000000 : 0;
	argb.resize(size_t(w) * h);
	float fade = 1; // TPT's menus
	OverlayBands(h, [&](int y0, int y1) {
		for (int i = y0 * w; i < y1 * w; i++)
		{
			auto c = vid[i] & 0xFFFFFF;
			argb[i] = c ? c | 0xFF000000 : empty;
		}
	});
	if (w == WINDOWW && h == WINDOWH)
	{
		if (frameOccupied.size() < size_t(XRES) * YRES) frameOccupied.assign(size_t(XRES) * YRES, 0);
		OverlayWindows::ApplySimMasks(argb.data(), w, frameOccupied.data());
		auto *top = ui::Engine::Ref().GetWindow();
		// TPT's menus: the side bar (x >= XRES), the tool strip under the sim and the bottom button row (which would
		// sit on the taskbar).
		static uint32_t lastUse = 0;
		static bool wasShown = false;
		bool shown = !(OverlayWindows::HideTPTMenus() && !(top && top->InPlace));
		if (shown && !wasShown) lastUse = SDL_GetTicks(); // just turned on: start visible
		wasShown = shown;
		if (OverlayWindows::HideTPTMenus() && !(top && top->InPlace))
		{
			fade = 0; // all of them are in the side window
		}
		else if (!(top && top->InPlace))
		{
			// Shown: they fade out after a few seconds without the mouse over them, and come back on hover.
			POINT cur;
			RECT wr;
			GetCursorPos(&cur);
			GetWindowRect(hwnd, &wr);
			int cx = wr.right > wr.left ? (cur.x - wr.left) * w / (wr.right - wr.left) : 0;
			int cy = wr.bottom > wr.top ? (cur.y - wr.top) * h / (wr.bottom - wr.top) : 0;
			if (cx >= 0 && cy >= 0 && cx < w && cy < h && (cx >= XRES || cy >= YRES)) lastUse = SDL_GetTicks();
			fade = std::clamp(1 - (int(SDL_GetTicks() - lastUse) - 3000) / 1000.f, 0.f, 1.f);
		}
		if (fade < 1)
		{
			uint32_t k = uint32_t(fade * 256);
			for (int y = 0; y < WINDOWH; y++)
				for (int x = y < YRES ? XRES : 0; x < w; x++)
				{
					auto &p = argb[y * w + x];
					p = k ? ((((p & 0xFF00FF) * k) >> 8) & 0xFF00FF) | ((((p & 0xFF00) * k) >> 8) & 0xFF00) | ((((p >> 24) * k) >> 8) << 24) : 0;
				}
		}
		OverlayWindows::DrawExtras(argb.data(), w, h);
	}
	OverlayWindows::ClearUnderWindows(argb.data(), w, h, ww, wh);
	// Drawing at the screen's rate only pays while something moves: particles, a window, the menus fading, or TPT in
	// use. Idle, a few frames a second keep up with slow changes and spot the next move, for a fraction of the CPU.
	static double busyUntil = 0;
	if (!moving.empty() || catchEmpty || !cursorMiss || (fade > 0 && fade < 1) || !OverlayWindows::WindowShifts().empty() || OverlayDebrisFlying())
		busyUntil = Now() + 1;
	overlayIdle = Now() >= busyUntil;
	ui::Engine::Ref().SetDrawingFrequencyLimit(overlayIdle ? DrawLimit(DrawLimitExplicit{ 15 }) : busyLimit);
	if (GpuPresent(argb.data(), w, h, ww, wh, empty)) return;
	bool keyed = GetParent(hwnd);
	if (keyed)
	{
		// Behind the icons, inside Explorer's desktop window: Windows 11 24H2 and later refuse per-pixel alpha
		// (UpdateLayeredWindow) there, so this is a colour-keyed layered window painted normally. See-through pixels
		// get the key; the rest are drawn as if over black (soft edges darken a little).
		for (auto &p : argb)
		{
			if (p >> 24 < 2) p = layerKey; // alpha 1 is "catch clicks" (never here: behind icons takes none)
			else if ((p & 0xFFFFFF) == layerKey) p ^= 1;
		}
	}
	// Scale up to the window (nearest pixel) in bands across every core: a column table, and rows that repeat the
	// one above are copied whole.
	static std::vector<int> columns;
	if (int(columns.size()) != ww || (ww && columns.back() != (ww - 1) * w / ww))
	{
		columns.resize(ww);
		for (int x = 0; x < ww; x++) columns[x] = x * w / ww;
	}
	constexpr int bands = 32;
	OverlayParallel(bands, [&](int b) {
		for (int y = wh * b / bands; y < wh * (b + 1) / bands; y++)
		{
			auto *dst = bits + size_t(y) * ww;
			if (y > wh * b / bands && y * h / wh == (y - 1) * h / wh)
			{
				std::memcpy(dst, dst - ww, size_t(ww) * 4);
				continue;
			}
			auto *src = argb.data() + size_t(y * h / wh) * w;
			for (int x = 0; x < ww; x++) dst[x] = src[columns[x]];
		}
	});
	if (keyed)
	{
		HDC dc = GetDC(hwnd);
		BitBlt(dc, 0, 0, ww, wh, memDC, 0, 0, SRCCOPY);
		ReleaseDC(hwnd, dc);
		return;
	}
	POINT origin{ 0, 0 };
	SIZE size{ ww, wh };
	BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	if (!UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, memDC, &origin, 0, &blend, ULW_ALPHA))
	{
		static bool told = false;
		if (!told) fprintf(stderr, "overlay: UpdateLayeredWindow failed (%lu)\n", GetLastError());
		told = true;
	}
#elif defined(__APPLE__)
	(void)window;
	MacDockFloor(XRES, YRES, CELL, WINDOWW, WINDOWH);
	TickFullScreen();
	OverlayTickUI();
	// Black is alpha 0: see-through, and clicks on it reach the desktop. Catching clicks gives it alpha 1 instead: still
	// invisible, but it catches them. Auto here: bare desktop is always TPT's, and windows and icons always keep theirs
	// (ApplySimMasks clears them to alpha 0), so nothing has to be clicked to get back to drawing, and a click on bare
	// wallpaper doesn't set off macOS's "click wallpaper to reveal desktop".
	bool catchEmpty = input == OverlayInput::Locked || input == OverlayInput::Auto;
	uint32_t empty = catchEmpty ? 0x01000000 : 0;
	argb.resize(size_t(w) * h);
	OverlayBands(h, [&](int y0, int y1) {
		for (int i = y0 * w; i < y1 * w; i++)
		{
			auto c = vid[i] & 0xFFFFFF;
			argb[i] = c ? c | 0xFF000000 : empty;
		}
	});
	float fade = 1; // TPT's menus
	if (w == WINDOWW && h == WINDOWH)
	{
		if (frameOccupied.size() < size_t(XRES) * YRES) frameOccupied.assign(size_t(XRES) * YRES, 0);
		OverlayWindows::ApplySimMasks(argb.data(), w, frameOccupied.data());
		auto *top = ui::Engine::Ref().GetWindow();
		static uint32_t lastUse = 0;
		static bool wasShown = false;
		bool shown = !(OverlayWindows::HideTPTMenus() && !(top && top->InPlace));
		if (shown && !wasShown) lastUse = SDL_GetTicks(); // just turned on: start visible
		wasShown = shown;
		if (OverlayWindows::HideTPTMenus() && !(top && top->InPlace))
		{
			fade = 0; // all of them are in the side window
		}
		else if (!(top && top->InPlace))
		{
			// Shown: they fade out after a few seconds without the mouse over them, and come back on hover.
			auto me = OverlayMac::Window();
			double mx, my;
			OverlayMac::MouseFrom(mx, my);
			int cx = me.r > me.l ? int((mx - me.l) * w / (me.r - me.l)) : 0, cy = me.b > me.t ? int((my - me.t) * h / (me.b - me.t)) : 0;
			if (cx >= 0 && cy >= 0 && cx < w && cy < h && (cx >= XRES || cy >= YRES)) lastUse = SDL_GetTicks();
			fade = std::clamp(1 - (int(SDL_GetTicks() - lastUse) - 3000) / 1000.f, 0.f, 1.f);
		}
		if (fade < 1)
		{
			uint32_t k = uint32_t(fade * 256);
			for (int y = 0; y < WINDOWH; y++)
				for (int x = y < YRES ? XRES : 0; x < w; x++)
				{
					auto &p = argb[y * w + x];
					p = k ? ((((p & 0xFF00FF) * k) >> 8) & 0xFF00FF) | ((((p & 0xFF00) * k) >> 8) & 0xFF00) | ((((p >> 24) * k) >> 8) << 24) : 0;
				}
		}
		OverlayWindows::DrawExtras(argb.data(), w, h);
	}
	auto me = OverlayMac::Window();
	double scale = OverlayMac::BackingScale();
	OverlayWindows::ClearUnderWindows(argb.data(), w, h, int((me.r - me.l) * scale), int((me.b - me.t) * scale));
	// which is which is all in the pixels: a wall or a window is alpha 0, so its clicks go to it, wherever the input is
	bool overDrawn = OverlayMac::UpdateClicks(argb.data(), w, h, false, input == OverlayInput::PassThrough || fullScreenApp || Shown() == OverlayLayer::BehindIcons);
	// Drawing at the screen's rate only pays while something moves: particles, a window, the menus fading, or TPT in
	// use. Idle, a few frames a second keep up with slow changes and spot the next move, for a fraction of the CPU.
	static double busyUntil = 0;
	auto shifts = OverlayWindows::WindowShifts();
	if (!moving.empty() || (catchEmpty && OverlayMac::Active()) || overDrawn || (fade > 0 && fade < 1) || !shifts.empty() || OverlayDebrisFlying())
		busyUntil = Now() + 1;
	overlayIdle = Now() >= busyUntil;
	ui::Engine::Ref().SetDrawingFrequencyLimit(overlayIdle ? DrawLimit(DrawLimitExplicit{ 15 }) : DrawLimit(DrawLimitDisplay{}));

	// out of the frame, into squares: first the sand on top of a window that rose since the sim last read it (it would
	// sink into the window by up to a tick), then the moving particles, carried on at their speed (one tick at most);
	// not into a still particle or a wall, where the sim will stop them. As GpuPresent on Windows.
	std::vector<OverlayMac::Mover> movers;
	for (auto &s : shifts)
		for (int x = std::max(s.x0, 0); s.dy < 0 && x < std::min(s.x1, w); x++)
			for (int y = std::min(s.y0, h) - 1; y >= 0; y--)
			{
				auto &px = argb[size_t(y) * w + x];
				if (px >> 24 < 2) break; // the top of the pile
				movers.push_back({ float(x), float(y), 0, 0, px, uint32_t(uintptr_t(s.id)) });
				px = empty;
			}
	for (auto &m : moving)
	{
		int x = int(m.x), y = int(m.y);
		if (x < 0 || y < 0 || x >= w || y >= h) continue;
		auto &px = argb[size_t(y) * w + x];
		if (px >> 24 < 2) continue; // under a window or a panel, or already taken
		int nx = int(std::lround(m.x + m.vx)), ny = int(std::lround(m.y + m.vy));
		bool open = nx >= 0 && ny >= 0 && nx < XRES && ny < YRES && size_t(ny) * XRES + nx < frameOccupied.size();
		if (open)
		{
			auto o = frameOccupied[size_t(ny) * XRES + nx];
			open = !(o & OnWall) && (!(o & OnParticle) || (o & Moves));
		}
		movers.push_back({ m.x, m.y, open ? m.vx : 0, open ? m.vy : 0, px, 0 });
		px = empty;
	}
	OverlayMac::Present(argb, w, h, std::move(movers), frameAt, framePeriod);
#else
	(void)window, (void)vid, (void)w, (void)h;
#endif
}
