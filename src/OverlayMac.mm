#include "OverlayMac.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unistd.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread/qos.h>
#include <spawn.h>
#include <sys/resource.h>
#include <crt_externs.h>
#include <mach-o/dyld.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140200
# import <CoreAudio/AudioHardwareTapping.h>
# import <CoreAudio/CATapDescription.h>
#endif

// This file is built without ARC: what is kept past the autorelease pool is retained by hand.

static NSWindow *window;
static NSInteger windowLevel = NSFloatingWindowLevel; // SDL sets its always-on-top level back as it shows the window
static int screenIndex = 0;
// The one the overlay lives on: never NSScreen.mainScreen, which is whichever has the keyboard and so changes under us
static NSScreen *Screen()
{
	NSArray<NSScreen *> *screens = NSScreen.screens;
	return screens[NSUInteger(std::clamp(screenIndex, 0, int(screens.count) - 1))];
}

static double PrimaryHeight()
{
	return NSScreen.screens.firstObject.frame.size.height;
}

// ---------------------------------------------------------------------------------------------- the menu bar item

@interface TDPTMenu : NSObject <NSMenuDelegate>
@end
@implementation TDPTMenu
- (void)pick:(NSMenuItem *)item
{
	OverlayMacMenu(int(item.tag));
}
- (void)menuNeedsUpdate:(NSMenu *)menu
{
	for (NSMenuItem *item in menu.itemArray)
		if (!item.isSeparatorItem) item.state = OverlayMacChecked(int(item.tag)) ? NSControlStateValueOn : NSControlStateValueOff;
}
@end

static TDPTMenu *menuTarget;
static NSStatusItem *statusItem;

// ------------------------------------------------------------------------------------------------- the Metal frame

namespace
{
	// The drawing thread: at each screen refresh, the newest frame the main thread handed over, its moving particles
	// carried on, and every window that moved since slid along with what is drawn on it. The same job as GpuLoop in
	// Overlay.cpp, on Metal. Only draws when something changed.
	struct Part
	{
		float x, y;
		uint32_t colour;
	};
	struct Constants // the shaders' C
	{
		float scale[2], screen[2];
		int moved, pad[3];
		int rects[16][4];
		float offs[16][4];
	};
	constexpr int framesInFlight = 3;

	NSString *const shaders = @R"(
#include <metal_stdlib>
using namespace metal;
struct C { float2 scale; float2 screen; int moved; int pad[3]; int4 rects[16]; float4 offs[16]; };
struct V { float4 pos [[position]]; float4 col; };
struct Part { packed_float2 at; uint colour; };
vertex V bgV(uint id [[vertex_id]])
{
	float2 uv = float2((id << 1) & 2, id & 2);
	V o;
	o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
	o.col = 0;
	return o;
}
// windows that moved since the frame (rects: where it had them, offs: how far they've gone, sim pixels): what is drawn on
// one goes with it, and where it was is left clear
fragment float4 bgF(V i [[stage_in]], constant C &c [[buffer(0)]], texture2d<float> frame [[texture(0)]])
{
	float2 f = i.pos.xy / c.scale;
	int2 size = int2(frame.get_width(), frame.get_height());
	float4 here = frame.read(uint2(clamp(int2(f), int2(0), size - 1))), patch = 0;
	bool inNew = false, inOld = false;
	for (int k = 0; k < c.moved; k++)
	{
		int4 r = c.rects[k];
		int2 q = int2(floor(f - c.offs[k].xy));
		if (!inNew && q.x >= r.x && q.y >= r.y && q.x < r.z && q.y < r.w && q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y)
			patch = frame.read(uint2(q)), inNew = true;
		if (f.x >= r.x && f.y >= r.y && f.x < r.z && f.y < r.w) inOld = true;
	}
	if (inNew && patch.a > 0) return patch;
	return inOld ? float4(0) : here;
}
vertex V partV(uint id [[vertex_id]], uint inst [[instance_id]], constant C &c [[buffer(0)]], constant Part *parts [[buffer(1)]])
{
	float2 p = round(float2(parts[inst].at) * c.scale) + float2(id & 1, id >> 1) * c.scale;
	uint u = parts[inst].colour;
	V o;
	o.pos = float4(p / c.screen * float2(2, -2) + float2(-1, 1), 0, 1);
	o.col = float4((u >> 16) & 255, (u >> 8) & 255, u & 255, u >> 24) / 255.0;
	return o;
}
fragment float4 partF(V i [[stage_in]]) { return i.col; }
)";

	id<MTLDevice> device;
	id<MTLCommandQueue> queue;
	id<MTLRenderPipelineState> bgPipe, partPipe;
	CAMetalLayer *metal;
	id<MTLTexture> textures[framesInFlight];
	id<MTLBuffer> partBuffers[framesInFlight];
	int partCaps[framesInFlight] = {};
	int textureW = 0, textureH = 0, nextSlot = 0;
	dispatch_semaphore_t inFlight, vsync;
	CVDisplayLinkRef displayLink = nullptr;

	std::mutex handOver; // held just long enough to swap: the main thread goes straight back to the sim
	std::vector<uint32_t> pending;
	int pendingW = 0, pendingH = 0;
	bool fresh = false, threadStarted = false;
	std::vector<OverlayMac::Mover> movers;
	double frameAt = 0, framePeriod = 1 / 60.0;
	double lastT = 1;
	bool shifted = false;
	std::vector<OverlayMac::Shift> (*shiftSource)() = nullptr;

	CVReturn OnVsync(CVDisplayLinkRef, const CVTimeStamp *, const CVTimeStamp *, CVOptionFlags, CVOptionFlags *, void *)
	{
		dispatch_semaphore_signal(vsync);
		return kCVReturnSuccess;
	}

	bool MakeMetal(NSView *view)
	{
		device = MTLCreateSystemDefaultDevice();
		if (!device) return false;
		queue = [device newCommandQueue];
		NSError *error = nil;
		id<MTLLibrary> library = [[device newLibraryWithSource:shaders options:nil error:&error] retain];
		if (!library)
		{
			fprintf(stderr, "overlay: shaders: %s\n", error.localizedDescription.UTF8String);
			return false;
		}
		auto pipe = [&](NSString *vertex, NSString *fragment, bool blend) -> id<MTLRenderPipelineState> {
			MTLRenderPipelineDescriptor *d = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
			d.vertexFunction = [[library newFunctionWithName:vertex] autorelease];
			d.fragmentFunction = [[library newFunctionWithName:fragment] autorelease];
			d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
			if (blend)
			{
				auto *c = d.colorAttachments[0];
				c.blendingEnabled = YES;
				c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = MTLBlendFactorOne;
				c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
			}
			return [device newRenderPipelineStateWithDescriptor:d error:&error];
		};
		bgPipe = pipe(@"bgV", @"bgF", false);
		partPipe = pipe(@"partV", @"partF", true);
		if (!bgPipe || !partPipe)
		{
			fprintf(stderr, "overlay: pipelines: %s\n", error.localizedDescription.UTF8String);
			return false;
		}
		metal = [[CAMetalLayer alloc] init];
		metal.device = device;
		metal.pixelFormat = MTLPixelFormatBGRA8Unorm;
		metal.framebufferOnly = YES;
		metal.opaque = NO;
		metal.maximumDrawableCount = 2; // one frame queued, not three: each would show the sim a frame later
		metal.displaySyncEnabled = YES;
		CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		metal.colorspace = srgb; // TPT's colours are sRGB; left alone they go oversaturated on a P3 screen
		CGColorSpaceRelease(srgb);
		metal.frame = view.bounds;
		metal.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
		[view.layer addSublayer:metal];
		inFlight = dispatch_semaphore_create(framesInFlight);
		vsync = dispatch_semaphore_create(0);
		CVDisplayLinkCreateWithActiveCGDisplays(&displayLink);
		if (displayLink)
		{
			CVDisplayLinkSetOutputCallback(displayLink, OnVsync, nullptr);
			CVDisplayLinkStart(displayLink);
		}
		return true;
	}

	void DrawLoop()
	{
		pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
		std::vector<Part> parts;
		std::vector<uint32_t> frame;
		int frameW = 0, frameH = 0, slot = 0;
		for (;;)
		{
			@autoreleasepool
			{
				dispatch_semaphore_wait(vsync, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC));
				auto shifts = shiftSource ? shiftSource() : std::vector<OverlayMac::Shift>{};
				if (shifts.size() > 16) shifts.resize(16); // ponytail: 16 moving at once; more just don't slide till the next frame
				std::unique_lock lock(handOver);
				bool isFresh = fresh;
				if (isFresh) std::swap(pending, frame), frameW = pendingW, frameH = pendingH, fresh = false;
				float t = float(std::clamp((OverlayMac::Now() - frameAt) / framePeriod, 0.0, 1.0));
				bool sliding = !movers.empty() && lastT < 1;
				if (!isFresh && shifts.empty() && !shifted && !sliding) continue;
				shifted = !shifts.empty(), lastT = t;
				parts.clear();
				for (auto &m : movers)
				{
					float y = m.y + m.vy * t;
					if (m.ride)
						for (auto &s : shifts)
							if (s.id == m.ride) y += s.dy;
					parts.push_back({ m.x + m.vx * t, y, m.colour });
				}
				lock.unlock();

				auto size = metal.drawableSize;
				if (size.width < 1 || !frameW) continue;
				if (textureW != frameW || textureH != frameH)
				{
					MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:frameW height:frameH mipmapped:NO];
					d.usage = MTLTextureUsageShaderRead;
					d.storageMode = MTLStorageModeShared; // one memory on Apple Silicon: the upload is a plain copy
					for (int i = 0; i < framesInFlight; i++)
					{
						dispatch_semaphore_wait(inFlight, DISPATCH_TIME_FOREVER);
						[textures[i] release];
						textures[i] = [device newTextureWithDescriptor:d];
						dispatch_semaphore_signal(inFlight);
					}
					textureW = frameW, textureH = frameH;
					isFresh = true;
				}
				dispatch_semaphore_wait(inFlight, DISPATCH_TIME_FOREVER);
				// the newest upload goes to a texture the GPU isn't reading; slide-only frames redraw the last one
				static int shownSlot = 0;
				int use = shownSlot;
				if (isFresh && frame.size() == size_t(frameW) * frameH)
				{
					use = (shownSlot + 1) % framesInFlight;
					[textures[use] replaceRegion:MTLRegionMake2D(0, 0, frameW, frameH) mipmapLevel:0 withBytes:frame.data() bytesPerRow:frameW * 4];
					shownSlot = use;
				}
				id<CAMetalDrawable> drawable = [metal nextDrawable];
				if (!drawable)
				{
					dispatch_semaphore_signal(inFlight);
					continue;
				}
				slot = (slot + 1) % framesInFlight;
				if (int(parts.size()) > partCaps[slot])
				{
					partCaps[slot] = std::max(int(parts.size()) * 2, 4096);
					[partBuffers[slot] release];
					partBuffers[slot] = [device newBufferWithLength:partCaps[slot] * sizeof(Part) options:MTLResourceStorageModeShared];
				}
				if (!parts.empty()) std::copy(parts.begin(), parts.end(), (Part *)partBuffers[slot].contents);
				Constants c{ { float(size.width / frameW), float(size.height / frameH) }, { float(size.width), float(size.height) }, int(shifts.size()), {}, {}, {} };
				for (size_t k = 0; k < shifts.size(); k++)
				{
					auto &s = shifts[k];
					c.rects[k][0] = s.x0, c.rects[k][1] = s.y0, c.rects[k][2] = s.x1, c.rects[k][3] = s.y1;
					c.offs[k][0] = s.dx, c.offs[k][1] = s.dy;
				}
				MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
				pass.colorAttachments[0].texture = drawable.texture;
				pass.colorAttachments[0].loadAction = MTLLoadActionDontCare; // the first draw covers every pixel
				pass.colorAttachments[0].storeAction = MTLStoreActionStore;
				id<MTLCommandBuffer> commands = [queue commandBuffer];
				id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
				[encoder setRenderPipelineState:bgPipe];
				[encoder setFragmentBytes:&c length:sizeof(c) atIndex:0];
				[encoder setFragmentTexture:textures[use] atIndex:0];
				[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
				if (!parts.empty())
				{
					[encoder setRenderPipelineState:partPipe];
					[encoder setVertexBytes:&c length:sizeof(c) atIndex:0];
					[encoder setVertexBuffer:partBuffers[slot] offset:0 atIndex:1];
					[encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:parts.size()];
				}
				[encoder endEncoding];
				[commands presentDrawable:drawable];
				dispatch_semaphore_t done = inFlight;
				[commands addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_semaphore_signal(done); }];
				[commands commit];
			}
		}
	}

	void KeepPlaced()
	{
		// centred on the usable area, its bottom (TPT's menus) just above the Dock; SDL moves it after Init
		auto v = Screen().visibleFrame;
		auto f = window.frame;
		NSPoint at = NSMakePoint(v.origin.x + std::floor((v.size.width - f.size.width) / 2), v.origin.y);
		if (f.origin.x != at.x || f.origin.y != at.y) [window setFrameOrigin:at];
		if (window.level != windowLevel) window.level = windowLevel;
		CGFloat scale = window.backingScaleFactor;
		CGSize want = CGSizeMake(f.size.width * scale, f.size.height * scale);
		if (!CGSizeEqualToSize(metal.drawableSize, want))
		{
			[CATransaction begin];
			[CATransaction setDisableActions:YES];
			metal.contentsScale = scale;
			metal.drawableSize = want;
			[CATransaction commit];
		}
	}
}

void OverlayMac::SetShiftSource(std::vector<Shift> (*source)())
{
	shiftSource = source;
}

double OverlayMac::Now()
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) * 1e-9;
}

void OverlayMac::SetScreen(int index)
{
	screenIndex = index;
}

std::vector<std::string> OverlayMac::ScreenNames()
{
	std::vector<std::string> names;
	for (NSScreen *s in NSScreen.screens) names.push_back(s.localizedName.UTF8String);
	return names;
}

void OverlayMac::ScreenSize(int &w, int &h)
{
	auto v = Screen().visibleFrame;
	w = int(v.size.width);
	h = int(v.size.height);
}

void OverlayMac::Init(SDL_Window *sdl)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(sdl, &info) || info.subsystem != SDL_SYSWM_COCOA) return;
	window = [info.info.cocoa.window retain];
	window.alphaValue = 0; // until the first frame: SDL's view is black, and anything holding up the start would show it
	window.opaque = NO;
	window.backgroundColor = NSColor.clearColor;
	window.hasShadow = NO;
	window.level = windowLevel; // over app windows, under the menu bar and the Dock
	window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
	                            NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenAuxiliary;

	NSView *view = window.contentView;
	view.wantsLayer = YES;
	if (!MakeMetal(view)) fprintf(stderr, "overlay: no Metal, nothing will show\n");
	InitMenu();
}

void OverlayMac::InitMenu()
{
	// The sim stays on the performance cores, and macOS doesn't nap or throttle an app that is seldom the front one but
	// still has sand falling on screen.
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
	[[[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
	                                                reason:@"Particles on the desktop"] retain];

	menuTarget = [[TDPTMenu alloc] init];
	NSMenu *menu = [[NSMenu alloc] init];
	menu.delegate = menuTarget;
	auto add = [&](NSString *title, int tag) {
		NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:title action:@selector(pick:) keyEquivalent:@""] autorelease];
		item.target = menuTarget;
		item.tag = tag;
		[menu addItem:item];
	};
	add(@"Clicks: Auto (TPT while it's in front)", 0);
	add(@"Clicks: Pass-through", 1);
	add(@"Clicks: Locked to TPT", 2);
	[menu addItem:NSMenuItem.separatorItem];
	add(@"Settings…", 12);
	add(@"Paused", 10);
	add(@"Clear the sim", 11);
	[menu addItem:NSMenuItem.separatorItem];
	add(@"Quit The Desktop Powder Toy", 99);
	statusItem = [[NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength] retain];
	NSImage *icon = [NSImage imageWithSystemSymbolName:@"hourglass.bottomhalf.filled" accessibilityDescription:@"The Desktop Powder Toy"];
	if (icon) statusItem.button.image = icon;
	else statusItem.button.title = @"TPT";
	statusItem.menu = menu;
}

void OverlayMac::SetLayer(int layer)
{
	// 0 on top; 1 over the icons, under app windows; 2 under the icons, over the wallpaper
	windowLevel = layer == 0 ? NSFloatingWindowLevel : layer == 1 ? kCGDesktopIconWindowLevel + 1 : kCGDesktopIconWindowLevel - 1;
}

void OverlayMac::Present(std::vector<uint32_t> &frame, int w, int h, std::vector<Mover> newMovers, double at, double period)
{
	if (!window || !metal) return;
	@autoreleasepool
	{
		// SDL's renderer adds a view over ours (after Init), and SDL's view paints its layer black whenever it redraws;
		// nothing is presented through either here
		for (NSView *sub in window.contentView.subviews)
			if (!sub.hidden) sub.hidden = YES;
		if (window.contentView.layer.backgroundColor) window.contentView.layer.backgroundColor = nil;
		KeepPlaced();
		if (window.alphaValue < 1) window.alphaValue = 1;
	}
	std::lock_guard lock(handOver);
	std::swap(pending, frame);
	pendingW = w, pendingH = h, fresh = true;
	movers = std::move(newMovers);
	frameAt = at, framePeriod = period;
	if (!threadStarted) threadStarted = true, std::thread(DrawLoop).detach();
}

bool OverlayMac::UpdateClicks(const uint32_t *argb, int w, int h, bool catchAll, bool catchNone)
{
	if (!window) return false;
	// decided by the pixel under the mouse (a window ignoring the mouse gets no events to decide with)
	bool catches = catchAll;
	auto m = NSEvent.mouseLocation;
	auto f = window.frame;
	if (!catchAll && NSPointInRect(m, f))
	{
		int x = int((m.x - f.origin.x) * w / f.size.width), y = int((f.origin.y + f.size.height - m.y) * h / f.size.height);
		catches = x >= 0 && y >= 0 && x < w && y < h && argb[size_t(y) * w + x] >> 24;
	}
	catches = catches && !catchNone;
	if (window.ignoresMouseEvents == catches) window.ignoresMouseEvents = !catches;
	return catches;
}

bool OverlayMac::CatchesMouse()
{
	return window && !window.ignoresMouseEvents;
}

OverlayMac::Rect OverlayMac::Window()
{
	if (!window) return {};
	static std::mutex lock; // the drawing thread asks too
	static CGRect at = CGRectZero;
	{
		std::lock_guard guard(lock);
		if ([NSThread isMainThread]) at = window.frame;
		double top = PrimaryHeight() - (at.origin.y + at.size.height);
		return { 0, at.origin.x, top, at.origin.x + at.size.width, top + at.size.height, 0, {} };
	}
}

void OverlayMac::MouseFrom(double &x, double &y)
{
	auto m = NSEvent.mouseLocation;
	x = m.x;
	y = PrimaryHeight() - m.y;
}

// ----------------------------------------------------------------------------------------- windows, apps, the Mac

std::vector<OverlayMac::Rect> OverlayMac::OtherWindows()
{
	std::vector<Rect> out;
	@autoreleasepool
	{
		CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
		if (!list) return out;
		pid_t me = getpid();
		for (NSDictionary *d in (NSArray *)list)
		{
			// layer 0: normal app windows (the menu bar, Dock and status items sit higher, and outside the sim anyway)
			if ([d[(id)kCGWindowLayer] intValue] != 0 || [d[(id)kCGWindowOwnerPID] intValue] == me || [d[(id)kCGWindowAlpha] doubleValue] <= 0)
				continue;
			CGRect b;
			if (!CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)d[(id)kCGWindowBounds], &b)) continue;
			if (b.size.width < 40 || b.size.height < 24) continue; // helpers' invisible slivers
			out.push_back({ [d[(id)kCGWindowNumber] unsignedIntValue], b.origin.x, b.origin.y, b.origin.x + b.size.width, b.origin.y + b.size.height, [d[(id)kCGWindowOwnerPID] intValue], [d[(id)kCGWindowOwnerName] UTF8String] ?: "" });
		}
		CFRelease(list);
	}
	return out;
}

int OverlayMac::FrontPid()
{
	return NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
}

std::string OverlayMac::FrontName()
{
	NSString *name = NSWorkspace.sharedWorkspace.frontmostApplication.localizedName.lowercaseString;
	return name ? name.UTF8String : "";
}

bool OverlayMac::FrontFillsScreen()
{
	int front = FrontPid();
	if (front == getpid()) return false;
	auto s = Screen().frame;
	double top = PrimaryHeight() - (s.origin.y + s.size.height);
	for (auto &r : OtherWindows())
		if (r.pid == front && r.l <= s.origin.x && r.t <= top && r.r >= s.origin.x + s.size.width && r.b >= top + s.size.height) return true;
	return false;
}

bool OverlayMac::Active()
{
	return NSApp.isActive && !(NSApp.keyWindow && NSApp.keyWindow != window);
}

double OverlayMac::IdleSeconds(bool keysOnly)
{
	return CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState, keysOnly ? kCGEventKeyDown : kCGAnyInputEventType);
}

bool OverlayMac::ButtonDown(int button)
{
	return NSEvent.pressedMouseButtons & (NSUInteger(1) << button);
}

bool OverlayMac::ControlDown()
{
	return NSEvent.modifierFlags & NSEventModifierFlagControl;
}

float OverlayMac::CpuLoad()
{
	static uint64_t lastIdle = 0, lastTotal = 0;
	static float load = 0;
	host_cpu_load_info_data_t info;
	mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
	if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&info, &count) != KERN_SUCCESS) return load;
	uint64_t idle = info.cpu_ticks[CPU_STATE_IDLE], total = 0;
	for (int i = 0; i < CPU_STATE_MAX; i++) total += info.cpu_ticks[i];
	if (lastTotal && total > lastTotal) load = 1 - float(idle - lastIdle) / float(total - lastTotal);
	lastIdle = idle, lastTotal = total;
	return load;
}

double OverlayMac::ProcessCpuSeconds(int pid)
{
	rusage_info_v0 usage;
	if (proc_pid_rusage(pid, RUSAGE_INFO_V0, (rusage_info_t *)&usage)) return -1;
	static mach_timebase_info_data_t base = [] { mach_timebase_info_data_t b; mach_timebase_info(&b); return b; }();
	// mach ticks, not nanoseconds, on Apple Silicon
	return double(usage.ri_user_time + usage.ri_system_time) * base.numer / base.denom * 1e-9;
}

// ------------------------------------------------------------------------------------------- what's playing, level only

namespace
{
	std::atomic<float> audioPeak{ 0 };
}

float OverlayMac::AudioPeak()
{
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140200
	static bool started = false;
	if (!started)
	{
		started = true;
		// not here on the main thread: the first time it waits for the answer to macOS's permission prompt, and the sim with it
		dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
			if (@available(macOS 14.2, *))
			{
				// A private tap on everything playing; the samples go no further than the peak below.
				CATapDescription *tap = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
				tap.privateTap = YES;
				tap.muteBehavior = CATapUnmuted;
				AudioObjectID tapId = kAudioObjectUnknown, deviceId = kAudioObjectUnknown;
				if (AudioHardwareCreateProcessTap(tap, &tapId) == noErr)
				{
					NSDictionary *aggregate = @{
						@kAudioAggregateDeviceNameKey: @"The Desktop Powder Toy level",
						@kAudioAggregateDeviceUIDKey: NSUUID.UUID.UUIDString,
						@kAudioAggregateDeviceIsPrivateKey: @YES,
						@kAudioAggregateDeviceTapAutoStartKey: @YES,
						@kAudioAggregateDeviceTapListKey: @[ @{ @kAudioSubTapUIDKey: tap.UUID.UUIDString } ],
					};
					AudioDeviceIOProcID proc = nullptr;
					dispatch_queue_t audio = dispatch_queue_create("overlay.audio", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
					if (AudioHardwareCreateAggregateDevice((CFDictionaryRef)aggregate, &deviceId) == noErr &&
					    AudioDeviceCreateIOProcIDWithBlock(&proc, deviceId, audio, ^(const AudioTimeStamp *, const AudioBufferList *in, const AudioTimeStamp *, AudioBufferList *, const AudioTimeStamp *) {
						    float peak = 0;
						    for (UInt32 b = 0; b < in->mNumberBuffers; b++)
						    {
							    auto *s = (const float *)in->mBuffers[b].mData;
							    for (UInt32 i = 0, n = in->mBuffers[b].mDataByteSize / sizeof(float); i < n; i++) peak = std::max(peak, std::abs(s[i]));
						    }
						    audioPeak.store(std::max(peak, audioPeak.load() * 0.8f));
					    }) == noErr)
						AudioDeviceStart(deviceId, proc);
				}
			}
		});
	}
#endif
	return audioPeak.exchange(0);
}

// ---------------------------------------------------------------------------------- Accessibility: icons, Dock, caret

namespace
{
	std::mutex axLock;
	std::vector<OverlayMac::Icon> axIcons;
	std::vector<OverlayMac::DockItem> axDock;
	bool axHasCaret = false;
	double axCaretX = 0, axCaretY = 0;
	std::atomic<bool> wantIcons{ false }, wantDock{ false }, wantCaret{ false };
	std::atomic<bool> axStarted{ false };

	CFTypeRef Attr(AXUIElementRef e, CFStringRef name)
	{
		CFTypeRef v = nullptr;
		return AXUIElementCopyAttributeValue(e, name, &v) == kAXErrorSuccess ? v : nullptr;
	}

	bool Bounds(AXUIElementRef e, CGRect &r)
	{
		CFTypeRef p = Attr(e, kAXPositionAttribute), s = Attr(e, kAXSizeAttribute);
		bool ok = p && s && AXValueGetValue((AXValueRef)p, (AXValueType)kAXValueCGPointType, &r.origin) && AXValueGetValue((AXValueRef)s, (AXValueType)kAXValueCGSizeType, &r.size);
		if (p) CFRelease(p);
		if (s) CFRelease(s);
		return ok;
	}

	NSString *Text(AXUIElementRef e, CFStringRef name)
	{
		CFTypeRef v = Attr(e, name);
		if (!v) return nil;
		if (CFGetTypeID(v) != CFStringGetTypeID())
		{
			CFRelease(v);
			return nil;
		}
		return [(NSString *)v autorelease];
	}

	// The picture a file shows on the desktop, opaque pixels solid.
	std::shared_ptr<const std::vector<uint8_t>> IconShape(NSString *path, int size, int &budget)
	{
		static std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> cache;
		std::string key = std::to_string(size) + "|" + path.UTF8String;
		if (auto it = cache.find(key); it != cache.end()) return it->second;
		if (budget-- <= 0) return nullptr;
		auto shape = std::make_shared<std::vector<uint8_t>>(size_t(size) * size, 1);
		NSImage *icon = [NSWorkspace.sharedWorkspace iconForFile:path];
		std::vector<uint32_t> px(size_t(size) * size);
		CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
		CGContextRef cg = CGBitmapContextCreate(px.data(), size, size, 8, size * 4, rgb, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
		CGColorSpaceRelease(rgb);
		if (cg)
		{
			NSGraphicsContext *ns = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
			[NSGraphicsContext saveGraphicsState];
			NSGraphicsContext.currentContext = ns;
			[icon drawInRect:NSMakeRect(0, 0, size, size)];
			[NSGraphicsContext restoreGraphicsState];
			CGContextRelease(cg);
			// rows of a bitmap context run from the top
			for (size_t i = 0; i < px.size(); i++) (*shape)[i] = px[i] >> 24 >= 128;
		}
		return cache[key] = shape;
	}

	void ReadIcons(int finderPid)
	{
		std::vector<OverlayMac::Icon> out;
		AXUIElementRef app = AXUIElementCreateApplication(finderPid);
		AXUIElementSetMessagingTimeout(app, 0.5f);
		CFTypeRef kids = Attr(app, kAXChildrenAttribute);
		int budget = 16;
		if (kids)
			for (id child in (NSArray *)kids)
			{
				NSString *role = Text((AXUIElementRef)child, kAXRoleAttribute);
				if (![role isEqualToString:@"AXScrollArea"]) continue; // the desktop
				CFTypeRef groups = Attr((AXUIElementRef)child, kAXChildrenAttribute);
				if (!groups) continue;
				for (id group in (NSArray *)groups)
				{
					CFTypeRef items = Attr((AXUIElementRef)group, kAXChildrenAttribute);
					if (!items) continue;
					for (id item in (NSArray *)items)
					{
						CGRect r;
						if (!Bounds((AXUIElementRef)item, r) || r.size.width < 8) continue;
						NSString *file = Text((AXUIElementRef)item, kAXFilenameAttribute);
						NSString *path = file;
						CFTypeRef url = Attr((AXUIElementRef)item, kAXURLAttribute);
						if (url)
						{
							if (CFGetTypeID(url) == CFURLGetTypeID()) path = [(NSURL *)url path];
						}
						int size = int(r.size.width);
						out.push_back({ r.origin.x, r.origin.y, r.origin.x + r.size.width, r.origin.y + r.size.height,
						                std::hash<std::string>{}(path ? path.UTF8String : ""), path ? IconShape(path, size, budget) : nullptr, size });
						if (url) CFRelease(url);
					}
					CFRelease(items);
				}
				CFRelease(groups);
			}
		if (kids) CFRelease(kids);
		CFRelease(app);
		std::lock_guard lock(axLock);
		axIcons = std::move(out);
	}

	void ReadDock(int dockPid)
	{
		std::vector<OverlayMac::DockItem> out;
		AXUIElementRef app = AXUIElementCreateApplication(dockPid);
		AXUIElementSetMessagingTimeout(app, 0.5f);
		CFTypeRef kids = Attr(app, kAXChildrenAttribute);
		if (kids)
			for (id list in (NSArray *)kids)
			{
				CFTypeRef items = Attr((AXUIElementRef)list, kAXChildrenAttribute);
				if (!items) continue;
				for (id item in (NSArray *)items)
				{
					CGRect r;
					NSString *title = Text((AXUIElementRef)item, kAXTitleAttribute);
					if (!title || !Bounds((AXUIElementRef)item, r)) continue;
					NSString *badge = Text((AXUIElementRef)item, CFSTR("AXStatusLabel"));
					NSString *subrole = Text((AXUIElementRef)item, kAXSubroleAttribute);
					// "2,759" and "99+" are still a count; anything else is a mark ("!" and so on): 1
					NSString *digits = [[badge componentsSeparatedByCharactersInSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet] componentsJoinedByString:@""];
					int count = !badge.length ? 0 : digits.length ? digits.intValue : 1;
					CFTypeRef isRunning = Attr((AXUIElementRef)item, CFSTR("AXIsApplicationRunning"));
					bool running = isRunning && CFGetTypeID(isRunning) == CFBooleanGetTypeID() && CFBooleanGetValue((CFBooleanRef)isRunning);
					if (isRunning) CFRelease(isRunning);
					out.push_back({ title.UTF8String, r.origin.x, r.origin.y, r.origin.x + r.size.width, r.origin.y + r.size.height, count, [subrole isEqualToString:@"AXTrashDockItem"], running });
				}
				CFRelease(items);
			}
		if (kids) CFRelease(kids);
		CFRelease(app);
		std::lock_guard lock(axLock);
		axDock = std::move(out);
	}

	void ReadCaret()
	{
		AXUIElementRef wide = AXUIElementCreateSystemWide();
		AXUIElementSetMessagingTimeout(wide, 0.2f);
		bool ok = false;
		CGRect caret = CGRectZero;
		if (CFTypeRef focus = Attr(wide, kAXFocusedUIElementAttribute))
		{
			if (CFTypeRef range = Attr((AXUIElementRef)focus, kAXSelectedTextRangeAttribute))
			{
				CFTypeRef bounds = nullptr;
				if (AXUIElementCopyParameterizedAttributeValue((AXUIElementRef)focus, kAXBoundsForRangeParameterizedAttribute, range, &bounds) == kAXErrorSuccess && bounds)
				{
					ok = AXValueGetValue((AXValueRef)bounds, (AXValueType)kAXValueCGRectType, &caret) && caret.origin.x + caret.origin.y > 0;
					CFRelease(bounds);
				}
				CFRelease(range);
			}
			CFRelease(focus);
		}
		CFRelease(wide);
		std::lock_guard lock(axLock);
		axHasCaret = ok, axCaretX = caret.origin.x, axCaretY = caret.origin.y;
	}

	void AxLoop()
	{
		pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
		for (int pass = 0;; pass++)
		{
			@autoreleasepool
			{
				if (AXIsProcessTrusted())
				{
					auto pidOf = [](NSString *bundle) {
						NSRunningApplication *a = [NSRunningApplication runningApplicationsWithBundleIdentifier:bundle].firstObject;
						return a ? int(a.processIdentifier) : 0;
					};
					// four times a second: the caret, as it's only wanted when it moves; the rest a couple of times a second
					if (wantCaret) ReadCaret();
					if (!(pass % 2))
					{
						if (int pid = wantIcons ? pidOf(@"com.apple.finder") : 0) ReadIcons(pid);
						if (int pid = wantDock ? pidOf(@"com.apple.dock") : 0) ReadDock(pid);
					}
				}
				else
				{
					std::lock_guard lock(axLock);
					axIcons.clear(), axDock.clear(), axHasCaret = false;
				}
			}
			usleep(250000);
		}
	}

	// The AX window that shows a CGWindow: same app, same place and size. Kept, as looking one up is slow and a shake
	// asks every tick.
	std::map<uint32_t, AXUIElementRef> axWindows;
	AXUIElementRef WindowElement(uint32_t number)
	{
		if (auto it = axWindows.find(number); it != axWindows.end()) return it->second;
		for (auto &w : OverlayMac::OtherWindows())
		{
			if (w.id != number) continue;
			AXUIElementRef app = AXUIElementCreateApplication(w.pid);
			AXUIElementSetMessagingTimeout(app, 0.2f);
			AXUIElementRef found = nullptr;
			if (CFTypeRef list = Attr(app, kAXWindowsAttribute))
			{
				for (id item in (NSArray *)list)
				{
					CGRect r;
					if (!found && Bounds((AXUIElementRef)item, r) && std::abs(r.origin.x - w.l) < 2 && std::abs(r.origin.y - w.t) < 2 &&
					    std::abs(r.size.width - (w.r - w.l)) < 2 && std::abs(r.size.height - (w.b - w.t)) < 2)
						found = AXUIElementRef(CFRetain((AXUIElementRef)item));
				}
				CFRelease(list);
			}
			CFRelease(app);
			if (found) axWindows[number] = found;
			return found;
		}
		return nullptr;
	}
}

void OverlayMac::WantDesktop(bool icons, bool dock, bool caret)
{
	wantIcons = icons, wantDock = dock, wantCaret = caret;
	if ((icons || dock || caret) && !axStarted.exchange(true)) std::thread(AxLoop).detach();
}

std::vector<OverlayMac::Icon> OverlayMac::Icons()
{
	std::lock_guard lock(axLock);
	return axIcons;
}

std::vector<OverlayMac::DockItem> OverlayMac::DockItems()
{
	std::lock_guard lock(axLock);
	return axDock;
}

bool OverlayMac::Caret(double &x, double &y)
{
	std::lock_guard lock(axLock);
	x = axCaretX, y = axCaretY;
	return axHasCaret;
}

bool OverlayMac::MoveWindow(uint32_t number, double x, double y)
{
	if (!AXIsProcessTrusted()) return false;
	AXUIElementRef e = WindowElement(number);
	if (!e) return false;
	CGPoint p = CGPointMake(x, y);
	AXValueRef v = AXValueCreate((AXValueType)kAXValueCGPointType, &p);
	bool ok = AXUIElementSetAttributeValue(e, kAXPositionAttribute, v) == kAXErrorSuccess;
	CFRelease(v);
	return ok;
}

bool OverlayMac::AccessibilityGranted()
{
	return AXIsProcessTrusted();
}

void OverlayMac::AskAccessibility()
{
	NSDictionary *options = @{ (id)kAXTrustedCheckOptionPrompt: @YES };
	AXIsProcessTrustedWithOptions((CFDictionaryRef)options);
}

// ----------------------------------------------------------------------------------------------------- the screen

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
namespace
{
	bool ScaledInto(CGImageRef image, int w, int h, uint32_t *out)
	{
		if (!image) return false;
		CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
		CGContextRef cg = CGBitmapContextCreate(out, w, h, 8, size_t(w) * 4, rgb, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
		CGColorSpaceRelease(rgb);
		if (!cg) return false;
		CGContextSetInterpolationQuality(cg, kCGInterpolationLow);
		CGContextDrawImage(cg, CGRectMake(0, 0, w, h), image);
		CGContextRelease(cg);
		for (int i = 0; i < w * h; i++) out[i] &= 0xFFFFFF;
		return true;
	}

	CGImageRef Capture(CGRect r, CGWindowID under)
	{
		// ponytail: CGWindowListCreateImage is deprecated for ScreenCaptureKit, which is asynchronous and session-based; this
		// reads one rectangle now, which is all shards and holes want.
		return CGWindowListCreateImage(r, kCGWindowListOptionOnScreenBelowWindow, under, kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution);
	}

	std::mutex shotLock;
	std::vector<uint32_t> shotsWanted;
	double shotScale = 1;
	std::map<uint32_t, std::shared_ptr<const OverlayMac::Shot>> shots;
	std::atomic<bool> shotsStarted{ false };

	void ShotLoop()
	{
		pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
		for (;;)
		{
			@autoreleasepool
			{
				std::vector<uint32_t> ids;
				double scale;
				{
					std::lock_guard lock(shotLock);
					ids = shotsWanted;
					scale = shotScale;
				}
				if (!ids.empty())
				{
					std::map<uint32_t, std::shared_ptr<const OverlayMac::Shot>> made;
					auto windows = OverlayMac::OtherWindows();
					for (uint32_t id : ids)
						for (auto &w : windows)
						{
							if (w.id != id) continue;
							auto shot = std::make_shared<OverlayMac::Shot>();
							shot->w = int(std::ceil((w.r - w.l) * scale)), shot->h = int(std::ceil((w.b - w.t) * scale));
							if (shot->w < 1 || shot->h < 1) continue;
							shot->px.resize(size_t(shot->w) * shot->h);
							CGImageRef image = Capture(CGRectMake(w.l, w.t, w.r - w.l, w.b - w.t), id);
							if (ScaledInto(image, shot->w, shot->h, shot->px.data())) made[id] = shot;
							if (image) CGImageRelease(image);
						}
					std::lock_guard lock(shotLock);
					shots = std::move(made);
				}
			}
			usleep(500000);
		}
	}
}

bool OverlayMac::ScreenCaptureGranted()
{
	return CGPreflightScreenCaptureAccess();
}

void OverlayMac::AskScreenCapture()
{
	CGRequestScreenCaptureAccess();
}

bool OverlayMac::Grab(double l, double t, double w, double h, int pw, int ph, uint32_t *out)
{
	if (!window || !CGPreflightScreenCaptureAccess()) return false;
	CGImageRef image = Capture(CGRectMake(l, t, w, h), CGWindowID(window.windowNumber));
	bool ok = ScaledInto(image, pw, ph, out);
	if (image) CGImageRelease(image);
	return ok;
}

void OverlayMac::WantShots(const std::vector<uint32_t> &ids, double simPerPoint)
{
	if (!CGPreflightScreenCaptureAccess()) return; // holes show the wallpaper instead
	std::lock_guard lock(shotLock);
	shotsWanted = ids, shotScale = simPerPoint;
	if (!ids.empty() && !shotsStarted.exchange(true)) std::thread(ShotLoop).detach();
}

std::shared_ptr<const OverlayMac::Shot> OverlayMac::TakeShot(uint32_t id)
{
	std::lock_guard lock(shotLock);
	auto it = shots.find(id);
	return it == shots.end() ? nullptr : it->second;
}

void OverlayMac::Wallpaper(int w, int h, uint32_t *out)
{
	std::fill(out, out + size_t(w) * h, 0x1A1A1Au);
	if (!window) return;
	@autoreleasepool
	{
		NSScreen *screen = Screen();
		NSURL *url = [NSWorkspace.sharedWorkspace desktopImageURLForScreen:screen];
		NSImage *image = url ? [[[NSImage alloc] initWithContentsOfURL:url] autorelease] : nil;
		CGImageRef cgImage = [image CGImageForProposedRect:nullptr context:nil hints:nil];
		if (!cgImage) return;
		// filling the screen, centred, as the default "Fill Screen" does; the buffer is the window on that screen
		auto sf = screen.frame, wf = window.frame;
		double iw = CGImageGetWidth(cgImage), ih = CGImageGetHeight(cgImage), scale = std::max(sf.size.width / iw, sf.size.height / ih);
		CGRect fill = CGRectMake((sf.size.width - iw * scale) / 2, (sf.size.height - ih * scale) / 2, iw * scale, ih * scale);
		double sx = w / wf.size.width, sy = h / wf.size.height;
		CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
		CGContextRef cg = CGBitmapContextCreate(out, w, h, 8, size_t(w) * 4, rgb, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
		CGColorSpaceRelease(rgb);
		if (!cg) return;
		CGContextScaleCTM(cg, sx, sy);
		CGContextTranslateCTM(cg, sf.origin.x - wf.origin.x, sf.origin.y - wf.origin.y);
		CGContextDrawImage(cg, fill, cgImage);
		CGContextRelease(cg);
		for (size_t i = 0; i < size_t(w) * h; i++) out[i] &= 0xFFFFFF;
	}
}
#pragma clang diagnostic pop

// ---------------------------------------------------------------------------------------------- hotkeys, toasts, restart

namespace
{
	std::mutex hotkeyLock;
	std::deque<int> hotkeysPressed;
	std::vector<EventHotKeyRef> hotkeyRefs;

	OSStatus OnHotkey(EventHandlerCallRef, EventRef event, void *)
	{
		EventHotKeyID id;
		if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) == noErr)
		{
			std::lock_guard lock(hotkeyLock);
			hotkeysPressed.push_back(int(id.id));
		}
		return noErr;
	}

	// The virtual key of a letter, digit or F1-F20 on a US layout (Carbon has no table for them); -1 if none.
	int KeyCode(const std::string &key)
	{
		static const std::map<std::string, int> keys = {
			{ "A", kVK_ANSI_A }, { "B", kVK_ANSI_B }, { "C", kVK_ANSI_C }, { "D", kVK_ANSI_D }, { "E", kVK_ANSI_E }, { "F", kVK_ANSI_F }, { "G", kVK_ANSI_G },
			{ "H", kVK_ANSI_H }, { "I", kVK_ANSI_I }, { "J", kVK_ANSI_J }, { "K", kVK_ANSI_K }, { "L", kVK_ANSI_L }, { "M", kVK_ANSI_M }, { "N", kVK_ANSI_N },
			{ "O", kVK_ANSI_O }, { "P", kVK_ANSI_P }, { "Q", kVK_ANSI_Q }, { "R", kVK_ANSI_R }, { "S", kVK_ANSI_S }, { "T", kVK_ANSI_T }, { "U", kVK_ANSI_U },
			{ "V", kVK_ANSI_V }, { "W", kVK_ANSI_W }, { "X", kVK_ANSI_X }, { "Y", kVK_ANSI_Y }, { "Z", kVK_ANSI_Z }, { "0", kVK_ANSI_0 }, { "1", kVK_ANSI_1 },
			{ "2", kVK_ANSI_2 }, { "3", kVK_ANSI_3 }, { "4", kVK_ANSI_4 }, { "5", kVK_ANSI_5 }, { "6", kVK_ANSI_6 }, { "7", kVK_ANSI_7 }, { "8", kVK_ANSI_8 },
			{ "9", kVK_ANSI_9 }, { "F1", kVK_F1 }, { "F2", kVK_F2 }, { "F3", kVK_F3 }, { "F4", kVK_F4 }, { "F5", kVK_F5 }, { "F6", kVK_F6 }, { "F7", kVK_F7 },
			{ "F8", kVK_F8 }, { "F9", kVK_F9 }, { "F10", kVK_F10 }, { "F11", kVK_F11 }, { "F12", kVK_F12 }, { "F13", kVK_F13 }, { "F14", kVK_F14 },
			{ "F15", kVK_F15 }, { "F16", kVK_F16 }, { "F17", kVK_F17 }, { "F18", kVK_F18 }, { "F19", kVK_F19 }, { "F20", kVK_F20 },
		};
		auto it = keys.find(key);
		return it == keys.end() ? -1 : it->second;
	}

	NSPanel *toastPanel;
	NSTextField *toastText;
	int toastGeneration = 0;
}

void OverlayMac::SetHotkeys(const std::vector<std::string> &list)
{
	static bool installed = false;
	if (!installed)
	{
		installed = true;
		EventTypeSpec spec{ kEventClassKeyboard, kEventHotKeyPressed };
		InstallApplicationEventHandler(OnHotkey, 1, &spec, nullptr, nullptr);
	}
	for (auto ref : hotkeyRefs) UnregisterEventHotKey(ref);
	hotkeyRefs.clear();
	for (size_t i = 0; i < list.size(); i++)
	{
		UInt32 mods = 0;
		int code = -1;
		NSArray<NSString *> *parts = [@(list[i].c_str()).uppercaseString componentsSeparatedByString:@"+"];
		for (NSString *p in parts)
		{
			if ([p isEqualToString:@"CTRL"]) mods |= controlKey;
			else if ([p isEqualToString:@"ALT"] || [p isEqualToString:@"OPT"] || [p isEqualToString:@"OPTION"]) mods |= optionKey;
			else if ([p isEqualToString:@"SHIFT"]) mods |= shiftKey;
			else if ([p isEqualToString:@"WIN"] || [p isEqualToString:@"CMD"]) mods |= cmdKey;
			else code = KeyCode(p.UTF8String);
		}
		EventHotKeyRef ref = nullptr;
		if (code >= 0 && RegisterEventHotKey(UInt32(code), mods, EventHotKeyID{ 'TDPT', UInt32(i) }, GetApplicationEventTarget(), 0, &ref) == noErr)
			hotkeyRefs.push_back(ref);
	}
}

int OverlayMac::TakeHotkey()
{
	std::lock_guard lock(hotkeyLock);
	if (hotkeysPressed.empty()) return -1;
	int id = hotkeysPressed.front();
	hotkeysPressed.pop_front();
	return id;
}

void OverlayMac::Toast(const std::string &message)
{
	@autoreleasepool
	{
		if (!toastPanel)
		{
			toastPanel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 320, 44) styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
			toastPanel.level = NSStatusWindowLevel;
			toastPanel.opaque = NO;
			toastPanel.backgroundColor = NSColor.clearColor;
			toastPanel.ignoresMouseEvents = YES;
			toastPanel.hasShadow = YES;
			toastPanel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
			NSVisualEffectView *glass = [[[NSVisualEffectView alloc] initWithFrame:toastPanel.contentView.bounds] autorelease];
			glass.material = NSVisualEffectMaterialHUDWindow;
			glass.blendingMode = NSVisualEffectBlendingModeBehindWindow;
			glass.state = NSVisualEffectStateActive;
			glass.wantsLayer = YES;
			glass.layer.cornerRadius = 12;
			glass.layer.masksToBounds = YES;
			glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
			[toastPanel.contentView addSubview:glass];
			toastText = [[NSTextField labelWithString:@""] retain];
			toastText.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
			toastText.lineBreakMode = NSLineBreakByTruncatingTail;
			toastText.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin | NSViewMaxYMargin;
			[glass addSubview:toastText];
		}
		toastText.stringValue = @(message.c_str());
		[toastText sizeToFit];
		double width = std::min(toastText.frame.size.width + 32, 520.0);
		auto v = Screen().visibleFrame;
		[toastPanel setFrame:NSMakeRect(v.origin.x + v.size.width - width - 16, v.origin.y + v.size.height - 60, width, 44) display:NO];
		toastText.frame = NSMakeRect(16, 13, width - 32, 18);
		toastPanel.alphaValue = 1;
		[toastPanel orderFrontRegardless];
		int generation = ++toastGeneration;
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2600 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
			if (generation != toastGeneration) return;
			[NSAnimationContext runAnimationGroup:^(NSAnimationContext *c) {
				c.duration = 0.4;
				toastPanel.animator.alphaValue = 0;
			} completionHandler:^{
				if (generation == toastGeneration) [toastPanel orderOut:nil];
			}];
		});
	}
}

bool OverlayMac::Relaunch(const char *extraArg)
{
	char path[4096];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size)) return false;
	std::vector<char *> argv{ path };
	for (int i = 1, n = *_NSGetArgc(); i < n; i++)
		if (strncmp((*_NSGetArgv())[i], "overlay:", 8)) argv.push_back((*_NSGetArgv())[i]);
	if (extraArg) argv.push_back(const_cast<char *>(extraArg));
	argv.push_back(nullptr);
	pid_t pid;
	return posix_spawn(&pid, path, nullptr, nullptr, argv.data(), *_NSGetEnviron()) == 0;
}

// --------------------------------------------------------------------------------------------- the settings window

@interface TDPTFlipped : NSView
@end
@implementation TDPTFlipped
- (BOOL)isFlipped
{
	return YES;
}
@end

@interface TDPTSettings : NSObject <NSWindowDelegate, NSTextFieldDelegate>
@end

namespace
{
	NSPanel *settings;
	TDPTSettings *settingsTarget;
	std::deque<OverlayMac::Row> settingsRows; // a deque: the controls keep pointers (as tags) into it
	struct Live
	{
		NSControl *control;
		NSTextField *value;
		size_t row;
	};
	std::vector<Live> settingsLive;
	NSTimer *settingsTimer;
}

@implementation TDPTSettings
- (void)changed:(NSControl *)sender
{
	auto &row = settingsRows[size_t(sender.tag)];
	float value = 0;
	if (row.kind == OverlayMac::Row::Toggle) value = ((NSButton *)sender).state == NSControlStateValueOn;
	else if (row.kind == OverlayMac::Row::Slider) value = sender.floatValue;
	else if (row.kind == OverlayMac::Row::Choice) value = float(((NSPopUpButton *)sender).indexOfSelectedItem);
	if (row.set) row.set(value);
	[self refresh];
}
- (void)pressed:(NSControl *)sender
{
	auto &row = settingsRows[size_t(sender.tag)];
	if (row.press) row.press();
	[self refresh];
}
- (void)controlTextDidEndEditing:(NSNotification *)note
{
	NSTextField *field = note.object;
	auto &row = settingsRows[size_t(field.tag)];
	if (row.setText) row.setText(field.stringValue.UTF8String);
}
- (void)refresh
{
	NSEvent *event = NSApp.currentEvent;
	bool dragging = event && (event.type == NSEventTypeLeftMouseDragged || event.type == NSEventTypeLeftMouseDown);
	for (auto &l : settingsLive)
	{
		auto &row = settingsRows[l.row];
		if (row.kind == OverlayMac::Row::Note)
		{
			if (row.getText) ((NSTextField *)l.control).stringValue = @(row.getText().c_str());
			continue;
		}
		if (row.kind == OverlayMac::Row::Text)
		{
			if (row.getText && l.control.currentEditor == nil) l.control.stringValue = @(row.getText().c_str());
			continue;
		}
		float v = row.get ? row.get() : 0;
		if (row.kind == OverlayMac::Row::Toggle) ((NSButton *)l.control).state = v != 0 ? NSControlStateValueOn : NSControlStateValueOff;
		else if (row.kind == OverlayMac::Row::Choice) [(NSPopUpButton *)l.control selectItemAtIndex:NSInteger(v)];
		else if (row.kind == OverlayMac::Row::Slider)
		{
			if (!dragging) l.control.floatValue = v;
			if (l.value && row.format) l.value.stringValue = @(row.format(v).c_str());
		}
	}
}
- (BOOL)windowShouldClose:(NSWindow *)sender
{
	[sender orderOut:nil];
	return NO;
}
@end

namespace
{
	NSView *BuildTab(const OverlayMac::Tab &tab)
	{
		constexpr double width = 560, side = 20, valueWidth = 70, labelWidth = 190, rowHeight = 28;
		TDPTFlipped *page = [[[TDPTFlipped alloc] initWithFrame:NSMakeRect(0, 0, width, 100)] autorelease];
		double y = 12;
		auto label = [&](NSString *text, NSRect at, bool bold) {
			NSTextField *t = [NSTextField labelWithString:text];
			t.frame = at;
			t.font = bold ? [NSFont boldSystemFontOfSize:13] : [NSFont systemFontOfSize:12];
			[page addSubview:t];
			return t;
		};
		for (auto &source : tab.rows)
		{
			settingsRows.push_back(source);
			size_t index = settingsRows.size() - 1;
			auto &row = settingsRows.back();
			NSString *text = @(row.label.c_str()), *tip = row.tip.empty() ? nil : @(row.tip.c_str());
			using Row = OverlayMac::Row;
			switch (row.kind)
			{
			case Row::Header:
				y += 10;
				label(text, NSMakeRect(side, y, width - 2 * side, 18), true);
				y += 24;
				break;
			case Row::Note:
			{
				NSTextField *t = [NSTextField wrappingLabelWithString:row.getText ? @(row.getText().c_str()) : text];
				t.font = [NSFont systemFontOfSize:12];
				t.textColor = NSColor.secondaryLabelColor;
				t.preferredMaxLayoutWidth = width - 2 * side;
				auto size = [t.cell cellSizeForBounds:NSMakeRect(0, 0, width - 2 * side, 1000)];
				t.frame = NSMakeRect(side, y, width - 2 * side, size.height);
				[page addSubview:t];
				if (row.getText) settingsLive.push_back({ t, nil, index });
				y += size.height + 8;
				break;
			}
			case Row::Toggle:
			{
				NSButton *b = [NSButton checkboxWithTitle:text target:settingsTarget action:@selector(changed:)];
				b.frame = NSMakeRect(side, y, width - 2 * side, 22);
				b.tag = NSInteger(index);
				b.toolTip = tip;
				[page addSubview:b];
				settingsLive.push_back({ b, nil, index });
				y += rowHeight - 4;
				break;
			}
			case Row::Slider:
			{
				label(text, NSMakeRect(side, y + 3, labelWidth, 18), false).toolTip = tip;
				NSSlider *s = [NSSlider sliderWithValue:row.lo minValue:row.lo maxValue:row.hi target:settingsTarget action:@selector(changed:)];
				s.frame = NSMakeRect(side + labelWidth + 8, y, width - 2 * side - labelWidth - valueWidth - 16, 24);
				s.tag = NSInteger(index);
				s.toolTip = tip;
				s.continuous = YES;
				[page addSubview:s];
				NSTextField *v = label(@"", NSMakeRect(width - side - valueWidth, y + 3, valueWidth, 18), false);
				v.alignment = NSTextAlignmentRight;
				v.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
				settingsLive.push_back({ s, v, index });
				y += rowHeight;
				break;
			}
			case Row::Choice:
			{
				label(text, NSMakeRect(side, y + 4, labelWidth, 18), false).toolTip = tip;
				NSPopUpButton *p = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(side + labelWidth + 8, y, 220, 26) pullsDown:NO] autorelease];
				for (auto &o : row.options) [p addItemWithTitle:@(o.c_str())];
				p.target = settingsTarget;
				p.action = @selector(changed:);
				p.tag = NSInteger(index);
				p.toolTip = tip;
				[page addSubview:p];
				settingsLive.push_back({ p, nil, index });
				y += rowHeight + 2;
				break;
			}
			case Row::Text:
			{
				label(text, NSMakeRect(side, y + 4, labelWidth, 18), false).toolTip = tip;
				NSTextField *f = [NSTextField textFieldWithString:@""];
				f.frame = NSMakeRect(side + labelWidth + 8, y, width - 2 * side - labelWidth - 8, 24);
				f.tag = NSInteger(index);
				f.delegate = settingsTarget;
				f.toolTip = tip;
				[page addSubview:f];
				settingsLive.push_back({ f, nil, index });
				y += rowHeight + 2;
				break;
			}
			case Row::Button:
			{
				NSButton *b = [NSButton buttonWithTitle:text target:settingsTarget action:@selector(pressed:)];
				[b sizeToFit];
				b.frame = NSMakeRect(side, y, std::max(b.frame.size.width + 12, 120.0), 26);
				b.tag = NSInteger(index);
				b.toolTip = tip;
				[page addSubview:b];
				y += rowHeight + 2;
				break;
			}
			}
		}
		page.frame = NSMakeRect(0, 0, width, y + 12);
		NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, width, 620)] autorelease];
		scroll.hasVerticalScroller = YES;
		scroll.drawsBackground = NO;
		scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		scroll.documentView = page;
		return scroll;
	}
}

void OverlayMac::ToggleSettings(const std::vector<Tab> &tabs)
{
	@autoreleasepool
	{
		if (settings && settings.visible)
		{
			[settings orderOut:nil];
			return;
		}
		if (!settings)
		{
			settingsTarget = [[TDPTSettings alloc] init];
			settings = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 600, 680)
			                                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
			                                        backing:NSBackingStoreBuffered
			                                          defer:NO];
			settings.title = @"The Desktop Powder Toy";
			settings.delegate = settingsTarget;
			settings.floatingPanel = NO;
			settings.becomesKeyOnlyIfNeeded = NO;
			settings.hidesOnDeactivate = YES;      // it sits over the sim layer, so it mustn't stay over other apps
			settings.level = NSFloatingWindowLevel + 1;
			settings.minSize = NSMakeSize(600, 360);
			settings.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
			settings.frameAutosaveName = @"TDPTSettings";
			NSTabView *tabView = [[[NSTabView alloc] initWithFrame:settings.contentView.bounds] autorelease];
			tabView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
			for (auto &tab : tabs)
			{
				NSTabViewItem *item = [[[NSTabViewItem alloc] initWithIdentifier:@(tab.name.c_str())] autorelease];
				item.label = @(tab.name.c_str());
				item.view = BuildTab(tab);
				[tabView addTabViewItem:item];
			}
			[settings.contentView addSubview:tabView];
			if (![settings setFrameUsingName:@"TDPTSettings"]) [settings center];
			// hotkeys and the menu bar change the same things
			settingsTimer = [[NSTimer timerWithTimeInterval:0.5 target:settingsTarget selector:@selector(refresh) userInfo:nil repeats:YES] retain];
			[NSRunLoop.mainRunLoop addTimer:settingsTimer forMode:NSRunLoopCommonModes];
		}
		[settingsTarget refresh];
		[NSApp activateIgnoringOtherApps:YES];
		[settings makeKeyAndOrderFront:nil];
	}
}

bool OverlayMac::SettingsOpen()
{
	return settings && settings.visible;
}
