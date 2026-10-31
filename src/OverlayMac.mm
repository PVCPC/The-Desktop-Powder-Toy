#include "OverlayMac.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <cmath>
#include <unistd.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

static NSWindow *window;
static CALayer *frameLayer;

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

static double PrimaryHeight()
{
	return NSScreen.screens.firstObject.frame.size.height;
}

void OverlayMac::ScreenSize(int &w, int &h)
{
	auto v = NSScreen.mainScreen.visibleFrame;
	w = int(v.size.width);
	h = int(v.size.height);
}

void OverlayMac::Init(SDL_Window *sdl)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(sdl, &info) || info.subsystem != SDL_SYSWM_COCOA) return;
	window = info.info.cocoa.window;
	window.opaque = NO;
	window.backgroundColor = NSColor.clearColor;
	window.hasShadow = NO;
	window.level = NSFloatingWindowLevel; // over app windows, under the menu bar and the Dock
	window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
	                            NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenAuxiliary;
	// centred on the usable area, its bottom (TPT's menus) just above the Dock
	auto v = NSScreen.mainScreen.visibleFrame;
	auto f = window.frame;
	[window setFrameOrigin:NSMakePoint(v.origin.x + std::floor((v.size.width - f.size.width) / 2), v.origin.y)];

	NSView *view = window.contentView;
	view.wantsLayer = YES;
	frameLayer = [[CALayer alloc] init];
	frameLayer.frame = view.bounds;
	frameLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
	frameLayer.contentsGravity = kCAGravityResize;
	frameLayer.magnificationFilter = kCAFilterNearest;
	frameLayer.opaque = NO;
	[view.layer addSublayer:frameLayer];

	menuTarget = [[TDPTMenu alloc] init];
	NSMenu *menu = [[NSMenu alloc] init];
	menu.delegate = menuTarget;
	auto add = [&](NSString *title, int tag) {
		NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(pick:) keyEquivalent:@""];
		item.target = menuTarget;
		item.tag = tag;
		[menu addItem:item];
	};
	add(@"Clicks: Auto (TPT while it's in front)", 0);
	add(@"Clicks: Pass-through", 1);
	add(@"Clicks: Locked to TPT", 2);
	[menu addItem:NSMenuItem.separatorItem];
	add(@"Paused", 10);
	add(@"Clear the sim", 11);
	[menu addItem:NSMenuItem.separatorItem];
	add(@"Quit The Desktop Powder Toy", 99);
	statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
	NSImage *icon = [NSImage imageWithSystemSymbolName:@"hourglass.bottomhalf.filled" accessibilityDescription:@"The Desktop Powder Toy"];
	if (icon) statusItem.button.image = icon;
	else statusItem.button.title = @"TPT";
	statusItem.menu = menu;
}

void OverlayMac::Present(const uint32_t *argb, int w, int h, bool catchAll, bool catchNone)
{
	if (!window) return;
	@autoreleasepool
	{
		static CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
		// a copy: Core Animation reads the image after this returns, and argb is redrawn next frame
		CFDataRef data = CFDataCreate(nullptr, (const UInt8 *)argb, CFIndex(size_t(w) * h * 4));
		CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
		CGImageRef image = CGImageCreate(w, h, 8, 32, size_t(w) * 4, rgb, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst,
		                                 provider, nullptr, false, kCGRenderingIntentDefault);
		// SDL's renderer adds an opaque Metal view over ours (after Init), and SDL's view paints its layer black whenever
		// it redraws; nothing is presented through either here
		for (NSView *sub in window.contentView.subviews)
			if (!sub.hidden) sub.hidden = YES;
		[CATransaction begin];
		[CATransaction setDisableActions:YES];
		window.contentView.layer.backgroundColor = nil;
		frameLayer.contents = (__bridge id)image;
		[CATransaction commit];
		CGImageRelease(image);
		CGDataProviderRelease(provider);
		CFRelease(data);

		// clicks: decided by the pixel under the mouse (a window ignoring the mouse gets no events to decide with)
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
	}
}

bool OverlayMac::Active()
{
	return NSApp.isActive;
}

OverlayMac::Rect OverlayMac::Window()
{
	if (!window) return {};
	auto f = window.frame;
	double top = PrimaryHeight() - (f.origin.y + f.size.height);
	return { 0, f.origin.x, top, f.origin.x + f.size.width, top + f.size.height };
}

std::vector<OverlayMac::Rect> OverlayMac::OtherWindows()
{
	std::vector<Rect> out;
	CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
	if (!list) return out;
	pid_t me = getpid();
	for (NSDictionary *d in (__bridge NSArray *)list)
	{
		// layer 0: normal app windows (the menu bar, Dock and status items sit higher, and outside the sim anyway)
		if ([d[(id)kCGWindowLayer] intValue] != 0 || [d[(id)kCGWindowOwnerPID] intValue] == me || [d[(id)kCGWindowAlpha] doubleValue] <= 0)
			continue;
		CGRect b;
		if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)d[(id)kCGWindowBounds], &b)) continue;
		out.push_back({ [d[(id)kCGWindowNumber] unsignedIntValue], b.origin.x, b.origin.y, b.origin.x + b.size.width, b.origin.y + b.size.height });
	}
	CFRelease(list);
	return out;
}
