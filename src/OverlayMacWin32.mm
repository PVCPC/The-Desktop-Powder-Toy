#include "OverlayMacWin32.h"
#include <SDL.h>
#include <algorithm>
#include <map>
#include <mutex>
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <QuartzCore/QuartzCore.h>
#include <mach-o/dyld.h>

// See OverlayMacWin32.h. Not ARC. Screen coordinates are pixels from the primary screen's top-left; a window's content is
// a layer showing the bitmap UpdateLayeredWindow was given.

namespace
{
	NSString *NS(const wchar_t *s, size_t n)
	{
		return [[[NSString alloc] initWithBytes:s length:n * sizeof(wchar_t) encoding:NSUTF32LittleEndianStringEncoding] autorelease];
	}
	NSString *NS(const wchar_t *s) { return NS(s, wcslen(s)); }
	std::wstring Wide(NSString *s)
	{
		NSData *d = [s dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
		return std::wstring((const wchar_t *)d.bytes, d.length / sizeof(wchar_t));
	}
	double PrimaryHeight() { return NSScreen.screens.firstObject.frame.size.height; }
	double Px() { return OverlayMac::BackingScale(); }

	struct MacWnd
	{
		WNDPROC proc = nullptr;
		NSWindow *win = nil;
		NSView *view = nil;
		LONG_PTR user = 0;
		DWORD ex = 0, style = 0;
		HWND owner = nullptr;
		RECT rect{};
		RECT restore{};
		bool shown = false, zoomed = false, iconic = false, tracking = false;
		int wheel = 0;
	};
	// never destroyed: windows held by statics close themselves at exit, after anything static here would be gone
	auto &classes = *new std::map<std::wstring, WNDPROC>();
	auto &windows = *new std::map<HWND, bool>();
	HWND capture = nullptr;

	MacWnd *W(HWND h) { return windows.count(h) ? (MacWnd *)h : nullptr; }

	NSRect ToFrame(const RECT &r)
	{
		double s = Px();
		return NSMakeRect(r.left / s, PrimaryHeight() - r.bottom / s, (r.right - r.left) / s, (r.bottom - r.top) / s);
	}
	LPARAM ScreenParam(POINT p) { return MAKELPARAM(int(p.x), int(p.y)); }
	POINT MouseOnScreen()
	{
		POINT p;
		GetCursorPos(&p);
		return p;
	}
}

// macOS 14 made asking to come to the front cooperative; a click in our window is reason enough
static void ActivateApp()
{
	if (@available(macOS 14.0, *)) [NSApp activate];
	else
	{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		[NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
	}
}

@interface TDPTWindow : NSWindow // not NSPanel: a click on one of those needn't bring the app forward
@end
@implementation TDPTWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@interface TDPTHost : NSView <NSWindowDelegate>
@property(nonatomic) HWND hwnd;
@end
@implementation TDPTHost
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (void)updateTrackingAreas
{
	for (NSTrackingArea *a in self.trackingAreas) [self removeTrackingArea:a];
	[self addTrackingArea:[[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil] autorelease]];
	[super updateTrackingAreas];
}
- (LPARAM)client:(WPARAM *)keys
{
	POINT p = MouseOnScreen();
	ScreenToClient(self.hwnd, &p);
	*keys = (NSEvent.pressedMouseButtons & 1) ? MK_LBUTTON : 0;
	return MAKELPARAM(int(p.x), int(p.y));
}
- (void)press:(UINT)msg
{
	WPARAM keys;
	LPARAM at = [self client:&keys];
	if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
	{
		// a TPT window in front is what puts clicks on the desktop back in TPT's hands (Auto), and its keys go to the game
		if (!NSApp.isActive)
		{
			ActivateApp();
			OverlayMac::FocusOverlay();
		}
	}
	if (msg == WM_LBUTTONDOWN)
	{
		LRESULT hit = SendMessageW(self.hwnd, WM_NCHITTEST, 0, ScreenParam(MouseOnScreen()));
		if (hit != HTCLIENT && W(self.hwnd))
		{
			SendMessageW(self.hwnd, WM_NCLBUTTONDOWN, WPARAM(hit), ScreenParam(MouseOnScreen()));
			return;
		}
	}
	SendMessageW(self.hwnd, msg, keys, at);
}
- (void)mouseDown:(NSEvent *)e { [self press:WM_LBUTTONDOWN]; }
- (void)mouseUp:(NSEvent *)e { [self press:WM_LBUTTONUP]; }
- (void)rightMouseDown:(NSEvent *)e { [self press:WM_RBUTTONDOWN]; }
- (void)rightMouseUp:(NSEvent *)e { [self press:WM_RBUTTONUP]; }
- (void)otherMouseDown:(NSEvent *)e { [self press:WM_MBUTTONDOWN]; }
- (void)otherMouseUp:(NSEvent *)e { [self press:WM_MBUTTONUP]; }
- (void)mouseMoved:(NSEvent *)e { [self press:WM_MOUSEMOVE]; }
- (void)mouseDragged:(NSEvent *)e { [self press:WM_MOUSEMOVE]; }
- (void)rightMouseDragged:(NSEvent *)e { [self press:WM_MOUSEMOVE]; }
- (void)mouseExited:(NSEvent *)e
{
	auto *w = W(self.hwnd);
	if (w && w->tracking) w->tracking = false, SendMessageW(self.hwnd, WM_MOUSELEAVE, 0, 0);
}
- (void)scrollWheel:(NSEvent *)e
{
	auto *w = W(self.hwnd);
	if (!w) return;
	// whole notches of WHEEL_DELTA, as a mouse wheel sends; a trackpad's small steps add up to them
	w->wheel += int(e.scrollingDeltaY * (e.hasPreciseScrollingDeltas ? 4 : 40));
	int notches = w->wheel / WHEEL_DELTA;
	if (!notches) return;
	w->wheel -= notches * WHEEL_DELTA;
	SendMessageW(self.hwnd, WM_MOUSEWHEEL, WPARAM(uint32_t(uint16_t(short(notches * WHEEL_DELTA))) << 16), ScreenParam(MouseOnScreen()));
}
- (void)keyDown:(NSEvent *)e
{
	static const std::map<int, int> keys = { { 51, VK_BACK }, { 36, VK_RETURN }, { 76, VK_RETURN }, { 53, VK_ESCAPE }, { 123, VK_LEFT }, { 124, VK_RIGHT }, { 125, VK_DOWN }, { 126, VK_UP }, { 117, VK_DELETE } };
	auto it = keys.find(e.keyCode);
	if (it != keys.end()) SendMessageW(self.hwnd, WM_KEYDOWN, WPARAM(it->second), 0);
	if (it == keys.end() || it->second == VK_BACK || it->second == VK_RETURN || it->second == VK_ESCAPE)
	{
		if (it != keys.end())
		{
			SendMessageW(self.hwnd, WM_CHAR, WPARAM(it->second), 0);
			return;
		}
		for (wchar_t c : Wide(e.characters))
			if (c >= 32 && c != 127) SendMessageW(self.hwnd, WM_CHAR, WPARAM(c), 0);
	}
}
- (void)windowDidResignKey:(NSNotification *)n { SendMessageW(self.hwnd, WM_KILLFOCUS, 0, 0); }
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info { return NSDragOperationCopy; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info
{
	NSURL *url = [NSURL URLFromPasteboard:info.draggingPasteboard];
	if (!url) return NO;
	auto path = Wide(url.path);
	SendMessageW(self.hwnd, WM_DROPFILES, WPARAM(&path), 0);
	return YES;
}
@end

HINSTANCE GetModuleHandleW(LPCWSTR) { return nullptr; }
HCURSOR LoadCursorW(HINSTANCE, const void *) { return nullptr; }

WORD RegisterClassW(const WNDCLASSW *c)
{
	classes[c->lpszClassName] = c->lpfnWndProc;
	return 1;
}

HWND CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND owner, HMENU, HINSTANCE, void *)
{
	auto *m = new MacWnd;
	HWND hwnd = HWND(m);
	windows[hwnd] = true;
	auto it = classes.find(cls);
	m->proc = it == classes.end() ? DefWindowProcW : it->second;
	m->ex = ex, m->style = style, m->owner = owner;
	m->rect = { x, y, x + w, y + h };
	if (!(ex & WS_EX_LAYERED)) return hwnd; // the tray's and menus' invisible windows just take messages
	// Titled with the title bar drawn away, not borderless: only a titled window brings its app forward when clicked from
	// another app. The UI draws its own frame over all of it.
	bool appWindow = ex & WS_EX_APPWINDOW;
	NSWindowStyleMask mask = appWindow || owner ? NSWindowStyleMaskTitled | NSWindowStyleMaskFullSizeContentView : NSWindowStyleMaskBorderless;
	m->win = [[TDPTWindow alloc] initWithContentRect:ToFrame(m->rect) styleMask:mask backing:NSBackingStoreBuffered defer:NO];
	if (mask != NSWindowStyleMaskBorderless)
	{
		m->win.titlebarAppearsTransparent = YES;
		m->win.titleVisibility = NSWindowTitleHidden;
		for (auto button : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton }) [m->win standardWindowButton:button].hidden = YES;
		m->win.movable = NO; // the UI moves it, from its own title bar
	}
	m->win.opaque = NO;
	m->win.backgroundColor = NSColor.clearColor;
	m->win.hasShadow = NO;
	m->win.releasedWhenClosed = NO;
	m->win.hidesOnDeactivate = NO;
	m->win.title = NS(title);
	m->win.acceptsMouseMovedEvents = YES;
	// normal windows: other apps can cover them; the sim layer above is cleared over them and lets their clicks through
	m->win.level = (ex & WS_EX_TOPMOST) ? NSStatusWindowLevel : NSNormalWindowLevel;
	m->win.collectionBehavior = NSWindowCollectionBehaviorManaged | NSWindowCollectionBehaviorFullScreenAuxiliary;
	TDPTHost *view = [[TDPTHost alloc] initWithFrame:NSMakeRect(0, 0, m->win.frame.size.width, m->win.frame.size.height)];
	view.hwnd = hwnd;
	view.wantsLayer = YES;
	view.layer.contentsGravity = kCAGravityResize;
	view.layer.magnificationFilter = kCAFilterNearest;
	view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	m->win.contentView = view;
	m->win.delegate = view;
	m->view = view;
	return hwnd;
}

bool DestroyWindow(HWND h)
{
	auto *m = W(h);
	if (!m) return false;
	if (capture == h) capture = nullptr;
	windows.erase(h);
	if (m->win)
	{
		m->win.delegate = nil;
		[m->win orderOut:nil];
		[m->win release];
		[m->view release];
	}
	delete m;
	return true;
}

LRESULT DefWindowProcW(HWND, UINT msg, WPARAM, LPARAM)
{
	return msg == WM_NCHITTEST ? HTCLIENT : 0;
}

LRESULT SendMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	auto *m = W(h);
	if (!m) return 0;
	if (msg == WM_DROPFILES)
	{
		lp = 0; // wp: the path; DragQueryFileW reads it
	}
	return m->proc(h, msg, wp, lp);
}

bool PostMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	dispatch_async(dispatch_get_main_queue(), ^{ SendMessageW(h, msg, wp, lp); });
	return true;
}

UINT DragQueryFileW(HDROP drop, UINT, wchar_t *out, UINT n)
{
	wcsncpy(out, ((std::wstring *)drop)->c_str(), n - 1);
	out[n - 1] = 0;
	return 1;
}
void DragFinish(HDROP) {}
bool DragAcceptFiles(HWND h, WBOOL)
{
	if (auto *m = W(h); m && m->view) [m->view registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
	return true;
}

LONG_PTR GetWindowLongPtrW(HWND h, int what)
{
	auto *m = W(h);
	return !m ? 0 : what == GWLP_USERDATA ? m->user : LONG_PTR(m->ex);
}

LONG_PTR SetWindowLongPtrW(HWND h, int what, LONG_PTR v)
{
	auto *m = W(h);
	if (!m) return 0;
	if (what == GWLP_USERDATA) std::swap(m->user, v);
	else
	{
		m->ex = DWORD(v);
		if (m->win) m->win.ignoresMouseEvents = (m->ex & WS_EX_TRANSPARENT) != 0;
	}
	return v;
}

static int floorX, floorY, floorW, floorH; // the sim's last row and the window's size (MacDockFloor)
void MacDockFloor(int simW, int simH, int cell, int windowW, int windowH)
{
	floorX = simW - cell, floorY = simH - cell, floorW = windowW, floorH = windowH;
}

uint32_t MacWindowNumber(HWND h)
{
	auto *m = W(h);
	return m && m->win ? uint32_t(m->win.windowNumber) : 0;
}

bool GetWindowRect(HWND h, RECT *out)
{
	if (h == macOverlay)
	{
		*out = MacRect(OverlayMac::Window());
		return true;
	}
	if (h == macDock)
	{
		// Where the taskbar's toys stand: on the Dock itself; without Accessibility, on a floor where it starts
		double l, t, r, b, s = MacScale();
		if (OverlayMac::DockFrame(l, t, r, b)) *out = { long(l * s), long(t * s), long(r * s), long(b * s) };
		else
		{
			auto me = MacRect(OverlayMac::Window());
			double sw = me.right - me.left;
			*out = { me.left, long(OverlayMac::VisibleBottom() * s), long(me.left + double(floorX) * sw / std::max(floorW, 1)), me.bottom };
		}
		return true;
	}
	if (auto *m = W(h))
	{
		*out = m->rect;
		return true;
	}
	for (auto &r : OverlayMac::OtherWindows())
		if (r.id == uint32_t(uintptr_t(h)))
		{
			*out = MacRect(r);
			return true;
		}
	return false;
}

bool SetWindowPos(HWND h, HWND after, long x, long y, long w, long h2, int flags)
{
	auto *m = W(h);
	if (!m) return OverlayMac::MoveWindow(uint32_t(uintptr_t(h)), x / MacScale(), y / MacScale());
	RECT r = m->rect;
	long cw = r.right - r.left, ch = r.bottom - r.top;
	if (!(flags & SWP_NOMOVE)) r.left = x, r.top = y;
	if (!(flags & SWP_NOSIZE)) cw = w, ch = h2;
	r.right = r.left + cw, r.bottom = r.top + ch;
	bool resized = cw != m->rect.right - m->rect.left || ch != m->rect.bottom - m->rect.top;
	m->rect = r;
	if (m->win)
	{
		[m->win setFrame:ToFrame(r) display:NO];
		if (after == HWND_TOPMOST) m->win.level = NSStatusWindowLevel;
		if (!(flags & SWP_NOZORDER) && m->shown) [m->win orderFrontRegardless];
		if ((flags & SWP_SHOWWINDOW) && !m->shown) ShowWindow(h, SW_SHOWNOACTIVATE);
	}
	if (resized) SendMessageW(h, WM_SIZE, 0, MAKELPARAM(int(cw), int(ch)));
	return true;
}

bool ShowWindow(HWND h, int how)
{
	auto *m = W(h);
	if (!m) return false;
	bool was = m->shown;
	if (how == SW_HIDE || how == SW_MINIMIZE)
	{
		m->shown = false;
		m->iconic = how == SW_MINIMIZE;
		if (m->win) [m->win orderOut:nil];
		return was;
	}
	if (how == SW_MAXIMIZE && !m->zoomed)
	{
		m->restore = m->rect, m->zoomed = true;
		MONITORINFO mi{ sizeof(mi) };
		GetMonitorInfoW(MonitorFromRect(&m->rect, MONITOR_DEFAULTTONEAREST), &mi);
		auto &k = mi.rcWork;
		SetWindowPos(h, nullptr, k.left, k.top, k.right - k.left, k.bottom - k.top, SWP_NOZORDER);
	}
	if (how == SW_RESTORE)
	{
		if (m->zoomed && !m->iconic)
		{
			m->zoomed = false;
			auto &k = m->restore;
			SetWindowPos(h, nullptr, k.left, k.top, k.right - k.left, k.bottom - k.top, SWP_NOZORDER);
		}
		m->iconic = false;
	}
	m->shown = true;
	if (m->win) [m->win orderFrontRegardless];
	return was;
}

bool IsWindowVisible(HWND h) { auto *m = W(h); return m && m->shown; }
bool IsIconic(HWND h) { auto *m = W(h); return m && m->iconic; }
bool IsZoomed(HWND h) { auto *m = W(h); return m && m->zoomed; }
bool IsWindow(HWND h)
{
	if (W(h)) return true;
	for (auto &r : OverlayMac::OtherWindows())
		if (r.id == uint32_t(uintptr_t(h))) return true;
	return false;
}

bool ScreenToClient(HWND h, POINT *p)
{
	auto *m = W(h);
	if (!m) return false;
	p->x -= m->rect.left, p->y -= m->rect.top;
	return true;
}
bool ClientToScreen(HWND h, POINT *p)
{
	auto *m = W(h);
	if (!m) return false;
	p->x += m->rect.left, p->y += m->rect.top;
	return true;
}

HWND SetCapture(HWND h)
{
	auto was = capture;
	capture = h;
	return was;
}
bool ReleaseCapture()
{
	auto was = capture;
	capture = nullptr;
	if (was) SendMessageW(was, WM_CAPTURECHANGED, 0, 0);
	return true;
}
bool TrackMouseEvent(TRACKMOUSEEVENT *t)
{
	if (auto *m = W(t->hwndTrack)) m->tracking = true;
	return true;
}
bool SetForegroundWindow(HWND h)
{
	auto *m = W(h);
	if (!m || !m->win) return true;
	ActivateApp();
	[m->win makeKeyWindow];
	[m->win makeFirstResponder:m->view];
	return true;
}
HWND SetFocus(HWND h)
{
	if (h) SetForegroundWindow(h);
	else OverlayMac::FocusOverlay();
	return nullptr;
}

bool UpdateLayeredWindow(HWND h, HDC, POINT *, SIZE *size, HDC src, POINT *, COLORREF, BLENDFUNCTION *blend, DWORD);

HRESULT DwmSetWindowAttribute(HWND, DWORD, const void *, DWORD) { return 0; }
HRESULT DwmFlush() { usleep(16000); return 0; }
void Sleep(DWORD ms) { usleep(ms * 1000); }

// ------------------------------------------------------------------------------------------------------------- GDI

namespace
{
	struct Bitmap { int w, h; uint32_t *bits; };
	struct Font { CTFontRef font; bool underline; };
	struct DC
	{
		Bitmap *bitmap = nullptr;
		Font *font = nullptr;
		COLORREF colour = 0xFFFFFF;
	};
	auto &kinds = *new std::map<void *, char>(); // what an HGDIOBJ is: 'b' or 'f'

	CFAttributedStringRef Attributed(const wchar_t *s, int n, Font *f, COLORREF c)
	{
		CGFloat rgba[] = { GetRValue(c) / 255.0, GetGValue(c) / 255.0, GetBValue(c) / 255.0, 1 };
		CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
		CGColorRef colour = CGColorCreate(space, rgba);
		CGColorSpaceRelease(space);
		NSMutableDictionary *a = [NSMutableDictionary dictionaryWithObjectsAndKeys:(id)f->font, (id)kCTFontAttributeName, (id)colour, (id)kCTForegroundColorAttributeName, nil];
		if (f->underline) a[(id)kCTUnderlineStyleAttributeName] = @(kCTUnderlineStyleSingle);
		CGColorRelease(colour);
		return (CFAttributedStringRef)[[[NSAttributedString alloc] initWithString:NS(s, n) attributes:a] autorelease];
	}
}

HDC GetDC(HWND) { return new DC; }
int ReleaseDC(HWND, HDC dc) { delete (DC *)dc; return 1; }
HDC CreateCompatibleDC(HDC) { return new DC; }
bool DeleteDC(HDC dc) { delete (DC *)dc; return true; }

HBITMAP CreateDIBSection(HDC, const BITMAPINFO *bi, UINT, void **bits, void *, DWORD)
{
	auto *b = new Bitmap{ int(bi->bmiHeader.biWidth), std::abs(int(bi->bmiHeader.biHeight)), nullptr };
	b->bits = (uint32_t *)calloc(size_t(b->w) * b->h, 4);
	*bits = b->bits;
	kinds[b] = 'b';
	return b;
}

HGDIOBJ SelectObject(HDC dc, HGDIOBJ o)
{
	auto *d = (DC *)dc;
	if (auto it = kinds.find(o); it != kinds.end() && it->second == 'f')
	{
		auto *was = d->font;
		d->font = (Font *)o;
		return was;
	}
	auto *was = d->bitmap;
	d->bitmap = (Bitmap *)o;
	return was;
}

bool DeleteObject(HGDIOBJ o)
{
	auto it = kinds.find(o);
	if (it == kinds.end()) return false;
	if (it->second == 'b')
	{
		free(((Bitmap *)o)->bits);
		delete (Bitmap *)o;
	}
	else
	{
		CFRelease(((Font *)o)->font);
		delete (Font *)o;
	}
	kinds.erase(it);
	return true;
}

HFONT CreateFontW(int height, int, int, int, int weight, DWORD italic, DWORD underline, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR face)
{
	double size = std::abs(height);
	NSFontWeight w = weight >= 700 ? NSFontWeightBold : weight >= 600 ? NSFontWeightSemibold : weight <= 300 ? NSFontWeightLight : NSFontWeightRegular;
	// Segoe UI isn't on a Mac: the system font is the nearest in shape and size; a theme's other faces (Consolas, Chicago...)
	// where the Mac has them
	NSFont *font = face && wcscmp(face, L"Segoe UI") ? [NSFont fontWithName:NS(face) size:size] : nil;
	if (!font) font = [NSFont systemFontOfSize:size weight:w];
	if (italic) font = [NSFontManager.sharedFontManager convertFont:font toHaveTrait:NSFontItalicTrait];
	auto *f = new Font{ (CTFontRef)CFRetain((CTFontRef)font), underline != 0 };
	kinds[f] = 'f';
	return f;
}

bool GetTextExtentPoint32W(HDC dc, const wchar_t *s, int n, SIZE *out)
{
	auto *d = (DC *)dc;
	@autoreleasepool
	{
		CTLineRef line = CTLineCreateWithAttributedString(Attributed(s, n, d->font, 0));
		CGFloat ascent, descent, leading;
		double w = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
		CFRelease(line);
		*out = { long(std::ceil(w)), long(std::ceil(ascent + descent + leading)) };
	}
	return true;
}

int SetBkMode(HDC, int) { return 1; }
COLORREF SetTextColor(HDC dc, COLORREF c)
{
	auto was = ((DC *)dc)->colour;
	((DC *)dc)->colour = c;
	return was;
}

int DrawTextW(HDC dc, const wchar_t *s, int n, RECT *r, UINT flags)
{
	auto *d = (DC *)dc;
	if (!d->bitmap || !d->font) return 0;
	@autoreleasepool
	{
		CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
		CGContextRef cg = CGBitmapContextCreate(d->bitmap->bits, d->bitmap->w, d->bitmap->h, 8, size_t(d->bitmap->w) * 4, space, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
		CGColorSpaceRelease(space);
		if (!cg) return 0;
		CGContextSetShouldSmoothFonts(cg, false); // the UI reads coverage from this and blends the text itself
		CTLineRef line = CTLineCreateWithAttributedString(Attributed(s, n, d->font, d->colour));
		double width = r->right - r->left;
		if ((flags & DT_END_ELLIPSIS) && CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr) > width)
		{
			CTLineRef dots = CTLineCreateWithAttributedString(Attributed(L"…", 1, d->font, d->colour));
			CTLineRef cut = CTLineCreateTruncatedLine(line, width, kCTLineTruncationEnd, dots);
			CFRelease(dots);
			if (cut) CFRelease(line), line = cut;
		}
		CGFloat ascent, descent, leading;
		CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
		CGContextClipToRect(cg, CGRectMake(r->left, d->bitmap->h - r->bottom, width, r->bottom - r->top));
		CGContextSetTextPosition(cg, r->left, d->bitmap->h - r->top - ascent);
		CTLineDraw(line, cg);
		CFRelease(line);
		CGContextRelease(cg);
		return int(ascent + descent);
	}
}

bool GdiFlush() { return true; }
int GetDeviceCaps(HDC, int) { return int(96 * Px()); }

int GetSystemMetrics(int what)
{
	double s = Px();
	NSRect all = NSZeroRect;
	for (NSScreen *screen in NSScreen.screens) all = NSUnionRect(all, screen.frame);
	double top = PrimaryHeight() - (all.origin.y + all.size.height);
	switch (what)
	{
	case SM_CXVIRTUALSCREEN: return int(all.size.width * s);
	case SM_CYVIRTUALSCREEN: return int(all.size.height * s);
	case SM_XVIRTUALSCREEN: return int(all.origin.x * s);
	case SM_YVIRTUALSCREEN: return int(top * s);
	case SM_CYSCREEN: return int(PrimaryHeight() * s);
	}
	return 0;
}

bool UpdateLayeredWindow(HWND h, HDC, POINT *, SIZE *size, HDC src, POINT *, COLORREF, BLENDFUNCTION *blend, DWORD)
{
	auto *m = W(h);
	auto *d = (DC *)src;
	if (!m || !m->win || !d->bitmap) return false;
	CFDataRef data = CFDataCreate(nullptr, (const UInt8 *)d->bitmap->bits, CFIndex(size_t(size->cx) * size->cy * 4));
	CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
	CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGImageRef image = CGImageCreate(size->cx, size->cy, 8, 32, size_t(size->cx) * 4, srgb, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, provider, nullptr, false, kCGRenderingIntentDefault);
	CGColorSpaceRelease(srgb);
	CGDataProviderRelease(provider);
	CFRelease(data);
	double alpha = blend ? blend->SourceConstantAlpha / 255.0 : 1;
	dispatch_block_t show = ^{ // a live theme's thread calls this too
		if (W(h) == m)
		{
			[CATransaction begin];
			[CATransaction setDisableActions:YES];
			m->view.layer.contents = (id)image;
			[CATransaction commit];
			m->win.alphaValue = alpha;
			if (!m->shown) ShowWindow(h, SW_SHOWNOACTIVATE);
		}
		CGImageRelease(image);
	};
	if ([NSThread isMainThread]) show();
	else dispatch_async(dispatch_get_main_queue(), show);
	return true;
}

// -------------------------------------------------------------------------------------------------------- monitors

namespace
{
	std::vector<RECT> Monitors(bool work)
	{
		std::vector<RECT> out;
		double s = Px(), ph = PrimaryHeight();
		for (NSScreen *screen in NSScreen.screens)
		{
			auto f = work ? screen.visibleFrame : screen.frame;
			out.push_back({ long(f.origin.x * s), long((ph - f.origin.y - f.size.height) * s), long((f.origin.x + f.size.width) * s), long((ph - f.origin.y) * s) });
		}
		return out;
	}
	HMONITOR Nearest(long x, long y)
	{
		auto all = Monitors(false);
		size_t best = 0;
		double bestD = 1e18;
		for (size_t i = 0; i < all.size(); i++)
		{
			double dx = x - std::clamp(x, all[i].left, all[i].right), dy = y - std::clamp(y, all[i].top, all[i].bottom), d = dx * dx + dy * dy;
			if (d < bestD) bestD = d, best = i;
		}
		return HMONITOR(best + 1);
	}
}

HMONITOR MonitorFromPoint(POINT p, DWORD) { return Nearest(p.x, p.y); }
HMONITOR MonitorFromRect(const RECT *r, DWORD) { return Nearest((r->left + r->right) / 2, (r->top + r->bottom) / 2); }
HMONITOR MonitorFromWindow(HWND h, DWORD)
{
	RECT r{};
	if (auto *m = W(h)) r = m->rect;
	return MonitorFromRect(&r, 0);
}
bool GetMonitorInfoW(HMONITOR mon, MONITORINFO *mi)
{
	auto i = size_t(uintptr_t(mon)) - 1;
	auto frames = Monitors(false), work = Monitors(true);
	if (i >= frames.size()) return false;
	mi->rcMonitor = frames[i], mi->rcWork = work[i];
	return true;
}

// ----------------------------------------------------------------------------------------------------------- menus

@interface TDPTPicker : NSObject
@property(nonatomic) int picked;
- (void)pick:(NSMenuItem *)item;
@end
@implementation TDPTPicker
- (void)pick:(NSMenuItem *)item { self.picked = int(item.tag); }
@end

HMENU CreatePopupMenu()
{
	NSMenu *m = [[NSMenu alloc] init];
	m.autoenablesItems = NO;
	return m;
}

bool AppendMenuW(HMENU menu, UINT flags, uintptr_t id, LPCWSTR text)
{
	NSMenu *m = (NSMenu *)menu;
	if (flags & MF_SEPARATOR)
	{
		[m addItem:NSMenuItem.separatorItem];
		return true;
	}
	// "Clear the sim\tCtrl+Alt+C": the shortcut after a tab is shown dimmed
	auto whole = std::wstring(text);
	auto tab = whole.find(L'\t');
	NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:NS(whole.substr(0, tab).c_str()) action:@selector(pick:) keyEquivalent:@""] autorelease];
	if (tab != whole.npos) item.toolTip = NS(whole.substr(tab + 1).c_str());
	item.tag = NSInteger(id);
	item.enabled = !(flags & MF_GRAYED);
	item.state = (flags & MF_CHECKED) ? NSControlStateValueOn : NSControlStateValueOff;
	[m addItem:item];
	return true;
}

bool DestroyMenu(HMENU menu)
{
	[(NSMenu *)menu release];
	return true;
}
bool SetMenuDefaultItem(HMENU, UINT, UINT) { return true; }

int TrackPopupMenu(HMENU menu, UINT, int x, int y, int, HWND, const RECT *)
{
	NSMenu *m = (NSMenu *)menu;
	TDPTPicker *target = [[[TDPTPicker alloc] init] autorelease];
	for (NSMenuItem *item in m.itemArray)
		if (!item.isSeparatorItem) item.target = target;
	double s = Px();
	[m popUpMenuPositioningItem:nil atLocation:NSMakePoint(x / s, PrimaryHeight() - y / s) inView:nil];
	return target.picked;
}

// ----------------------------------------------------------------------------------------------------------- shell

int MessageBoxW(HWND, LPCWSTR text, LPCWSTR title, UINT flags)
{
	NSAlert *a = [[[NSAlert alloc] init] autorelease];
	a.messageText = NS(title);
	a.informativeText = NS(text);
	a.alertStyle = (flags & MB_ICONWARNING) ? NSAlertStyleWarning : NSAlertStyleInformational;
	[a addButtonWithTitle:(flags & MB_YESNO) ? @"No" : @"OK"];
	if (flags & MB_YESNO) [a addButtonWithTitle:@"Yes"];
	return [a runModal] == NSAlertSecondButtonReturn ? IDYES : 0;
}

void *ShellExecuteW(HWND, LPCWSTR, LPCWSTR file, LPCWSTR params, LPCWSTR, int)
{
	NSString *f = NS(file);
	if (params && wcsstr(params, L"/select,")) // explorer.exe /select,"path"
	{
		NSString *path = [[NS(params) substringFromIndex:8] stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"\""]];
		[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:path] ]];
	}
	else if ([f containsString:@"://"]) [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:f]];
	else [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:f]];
	return (void *)33;
}

bool GetFullPathNameW_(const wchar_t *path, wchar_t *out)
{
	wcsncpy(out, Wide([[NSURL fileURLWithPath:NS(path)] path]).c_str(), MAX_PATH - 1);
	out[MAX_PATH - 1] = 0;
	return true;
}

DWORD GetModuleFileNameW(HMODULE, wchar_t *out, DWORD n)
{
	char path[MAX_PATH];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size)) return 0;
	wcsncpy(out, Wide(@(path)).c_str(), n - 1);
	out[n - 1] = 0;
	return DWORD(wcslen(out));
}

int DeleteFileW(const wchar_t *path)
{
	return unlink(NS(path).fileSystemRepresentation) == 0;
}

// The Windows tray icon: left click and right click reach the tray window's message as they do there.
@interface TDPTStatus : NSObject
@property(nonatomic) HWND hwnd;
@property(nonatomic) UINT message;
@end
@implementation TDPTStatus
- (void)clicked:(id)sender
{
	NSEvent *e = NSApp.currentEvent;
	bool right = e.type == NSEventTypeRightMouseUp || (e.modifierFlags & NSEventModifierFlagControl);
	SendMessageW(self.hwnd, self.message, 0, right ? WM_RBUTTONUP : WM_LBUTTONUP);
}
@end

bool Shell_NotifyIconW(DWORD what, NOTIFYICONDATAW *d)
{
	static NSStatusItem *item;
	static TDPTStatus *target;
	if (what == NIM_DELETE)
	{
		if (item) [NSStatusBar.systemStatusBar removeStatusItem:item];
		[item release];
		item = nil;
		return true;
	}
	if (what == NIM_ADD && !item)
	{
		target = target ?: [[TDPTStatus alloc] init];
		target.hwnd = d->hWnd, target.message = d->uCallbackMessage;
		item = [[NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength] retain];
		NSImage *icon = [[NSApp.applicationIconImage copy] autorelease];
		icon.size = NSMakeSize(18, 18);
		item.button.image = icon;
		item.button.toolTip = NS(d->szTip);
		item.button.target = target;
		item.button.action = @selector(clicked:);
		[item.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
	}
	if (what == NIM_MODIFY && (d->uFlags & NIF_INFO))
	{
		std::wstring both = std::wstring(d->szInfoTitle) + L". " + d->szInfo;
		OverlayMac::Toast(NS(both.c_str()).UTF8String);
	}
	return true;
}

// The colour dialog, modal like Windows'.
@interface TDPTColour : NSObject <NSWindowDelegate>
@end
@implementation TDPTColour
- (void)windowWillClose:(NSNotification *)n { [NSApp stopModal]; }
@end

bool ChooseColorW(CHOOSECOLORW *c)
{
	NSColorPanel *panel = NSColorPanel.sharedColorPanel;
	panel.showsAlpha = NO;
	panel.color = [NSColor colorWithSRGBRed:GetRValue(c->rgbResult) / 255.0 green:GetGValue(c->rgbResult) / 255.0 blue:GetBValue(c->rgbResult) / 255.0 alpha:1];
	TDPTColour *delegate = [[[TDPTColour alloc] init] autorelease];
	id<NSWindowDelegate> was = panel.delegate;
	panel.delegate = delegate;
	panel.level = NSStatusWindowLevel;
	[NSApp runModalForWindow:panel];
	panel.delegate = was;
	NSColor *picked = [panel.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
	c->rgbResult = RGB(int(std::lround(picked.redComponent * 255)), int(std::lround(picked.greenComponent * 255)), int(std::lround(picked.blueComponent * 255)));
	return true;
}

// ------------------------------------------------------------------------------------------- Form, console, the rest

@interface TDPTFormChecker : NSObject <NSTextFieldDelegate>
@property(nonatomic, assign) NSButton *ok;
@property(nonatomic, assign) NSPopUpButton *popup;
@property(nonatomic, assign) NSTextField *field;
@end
static const std::function<bool(int, const std::wstring &)> *formValid;
@implementation TDPTFormChecker
- (void)check
{
	if (*formValid) self.ok.enabled = (*formValid)(self.popup ? int(self.popup.indexOfSelectedItem) : 0, Wide(self.field.stringValue));
}
- (void)controlTextDidChange:(NSNotification *)n { [self check]; }
@end

int OverlayMac::Form(const std::wstring &title, const std::wstring &prompt, const std::vector<std::wstring> &options, int &choice, std::wstring &text,
                     const std::vector<std::wstring> &extras, const std::function<bool(int, const std::wstring &)> &valid)
{
	@autoreleasepool
	{
		NSAlert *a = [[[NSAlert alloc] init] autorelease];
		a.messageText = NS(title.c_str());
		a.informativeText = NS(prompt.c_str());
		NSView *box = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, options.empty() ? 26 : 58)] autorelease];
		TDPTFormChecker *checker = [[[TDPTFormChecker alloc] init] autorelease];
		NSTextField *field = [NSTextField textFieldWithString:NS(text.c_str())];
		field.frame = NSMakeRect(0, 0, 300, 24);
		[box addSubview:field];
		checker.field = field;
		if (!options.empty())
		{
			NSPopUpButton *popup = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 32, 300, 26) pullsDown:NO] autorelease];
			for (auto &o : options) [popup addItemWithTitle:NS(o.c_str())];
			[popup selectItemAtIndex:choice];
			popup.target = checker;
			popup.action = @selector(check);
			[box addSubview:popup];
			checker.popup = popup;
		}
		a.accessoryView = box;
		checker.ok = [a addButtonWithTitle:@"OK"];
		[a addButtonWithTitle:@"Cancel"];
		for (auto &e : extras) [a addButtonWithTitle:NS(e.c_str())];
		field.delegate = checker;
		formValid = &valid;
		[checker check];
		a.window.initialFirstResponder = field;
		auto answer = [a runModal];
		choice = checker.popup ? int(checker.popup.indexOfSelectedItem) : choice;
		text = Wide(field.stringValue);
		return answer == NSAlertFirstButtonReturn ? 1 : answer == NSAlertSecondButtonReturn ? 0 : 2 + int(answer - NSAlertThirdButtonReturn);
	}
}

@interface TDPTConsole : NSObject <NSWindowDelegate, NSTextFieldDelegate>
@end
namespace
{
	NSWindow *consoleWindow;
	NSTextView *consoleLog;
	NSTextField *consoleInput;
	TDPTConsole *consoleTarget;
	OverlayMac::ConsoleHooks consoleHooks;
	int historyAt = 0;
}
@implementation TDPTConsole
- (BOOL)control:(NSControl *)c textView:(NSTextView *)t doCommandBySelector:(SEL)command
{
	if (command == @selector(insertNewline:))
	{
		auto line = Wide(consoleInput.stringValue);
		consoleInput.stringValue = @"";
		historyAt = 0;
		if (consoleHooks.run) consoleHooks.run(line);
		return YES;
	}
	if (command == @selector(moveUp:) || command == @selector(moveDown:))
	{
		historyAt = std::max(0, historyAt + (command == @selector(moveUp:) ? 1 : -1));
		auto line = historyAt && consoleHooks.history ? consoleHooks.history(historyAt) : std::wstring();
		if (historyAt && line.empty()) historyAt--;
		else consoleInput.stringValue = NS(line.c_str());
		return YES;
	}
	if (command == @selector(cancelOperation:))
	{
		[consoleWindow orderOut:nil];
		if (consoleHooks.closed) consoleHooks.closed();
		return YES;
	}
	return NO;
}
- (BOOL)windowShouldClose:(NSWindow *)w
{
	[w orderOut:nil];
	if (consoleHooks.closed) consoleHooks.closed();
	return NO;
}
@end

void OverlayMac::ConsoleOpen(const ConsoleHooks &hooks)
{
	consoleHooks = hooks;
	if (!consoleWindow)
	{
		consoleTarget = [[TDPTConsole alloc] init];
		consoleWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 420) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
		consoleWindow.title = @"Lua console - The Powder Toy";
		consoleWindow.delegate = consoleTarget;
		consoleWindow.releasedWhenClosed = NO;
		[consoleWindow center];
		NSView *v = consoleWindow.contentView;
		consoleInput = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 640, 26)];
		consoleInput.font = [NSFont fontWithName:@"Menlo" size:12];
		consoleInput.autoresizingMask = NSViewWidthSizable;
		consoleInput.delegate = consoleTarget;
		[v addSubview:consoleInput];
		NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 26, 640, 394)];
		scroll.hasVerticalScroller = YES;
		scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		consoleLog = [[NSTextView alloc] initWithFrame:scroll.bounds];
		consoleLog.editable = NO;
		consoleLog.font = [NSFont fontWithName:@"Menlo" size:12];
		consoleLog.autoresizingMask = NSViewWidthSizable;
		scroll.documentView = consoleLog;
		[v addSubview:scroll];
	}
	ActivateApp();
	[consoleWindow makeKeyAndOrderFront:nil];
	[consoleWindow makeFirstResponder:consoleInput];
}

void OverlayMac::ConsoleClose()
{
	[consoleWindow orderOut:nil];
}

bool OverlayMac::ConsoleVisible()
{
	return consoleWindow.visible;
}

void OverlayMac::ConsoleAppend(const std::wstring &text, bool error)
{
	if (!consoleLog) return;
	NSDictionary *a = @{ NSFontAttributeName: [NSFont fontWithName:@"Menlo" size:12], NSForegroundColorAttributeName: error ? NSColor.systemRedColor : NSColor.textColor };
	[consoleLog.textStorage appendAttributedString:[[[NSAttributedString alloc] initWithString:NS((text + L"\n").c_str()) attributes:a] autorelease]];
	[consoleLog scrollToEndOfDocument:nil];
}

static NSString *LoginAgent()
{
	return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents/io.github.pvcpc.desktop-powder-toy.plist"];
}

bool OverlayMac::StartsAtLogin()
{
	return [NSFileManager.defaultManager fileExistsAtPath:LoginAgent()];
}

void OverlayMac::SetStartsAtLogin(bool on)
{
	if (!on)
	{
		[NSFileManager.defaultManager removeItemAtPath:LoginAgent() error:nil];
		return;
	}
	NSDictionary *agent = @{ @"Label": @"io.github.pvcpc.desktop-powder-toy", @"ProgramArguments": @[ @"/usr/bin/open", NSBundle.mainBundle.bundlePath ], @"RunAtLoad": @YES };
	[agent writeToFile:LoginAgent() atomically:YES];
}

void OverlayMac::OnMain(std::function<void()> f)
{
	auto *copy = new std::function<void()>(std::move(f));
	dispatch_async(dispatch_get_main_queue(), ^{
		(*copy)();
		delete copy;
	});
}

@interface TDPTPopup : NSObject
- (void)pick:(NSButton *)sender;
@end
namespace
{
	NSWindow *popup;
	id popupGlobal, popupLocal;
	std::function<void(int)> popupDone;
	TDPTPopup *popupTarget;

	void ClosePopup(int picked)
	{
		if (!popup) return;
		[popup orderOut:nil];
		[popup release];
		popup = nil;
		[NSEvent removeMonitor:popupGlobal];
		[NSEvent removeMonitor:popupLocal];
		auto done = std::move(popupDone);
		popupDone = nullptr;
		if (done) done(picked);
	}
}
@implementation TDPTPopup
- (void)pick:(NSButton *)sender { ClosePopup(int(sender.tag)); }
@end

void OverlayMac::PopupList(const std::vector<std::tuple<std::wstring, bool, bool>> &items, double x, double y, std::function<void(int)> done)
{
	@autoreleasepool
	{
		ClosePopup(0);
		popupTarget = popupTarget ?: [[TDPTPopup alloc] init];
		constexpr double rowHeight = 24, width = 220;
		double total = rowHeight * items.size() + 8, height = std::min(total, 520.0), ph = PrimaryHeight();
		popup = [[NSWindow alloc] initWithContentRect:NSMakeRect(x, ph - y - height, width, height) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
		popup.releasedWhenClosed = NO;
		popup.level = NSPopUpMenuWindowLevel;
		popup.opaque = NO;
		popup.backgroundColor = NSColor.clearColor;
		popup.hasShadow = YES;
		NSVisualEffectView *glass = [[[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, width, height)] autorelease];
		glass.material = NSVisualEffectMaterialMenu;
		glass.state = NSVisualEffectStateActive;
		glass.wantsLayer = YES;
		glass.layer.cornerRadius = 10;
		glass.layer.masksToBounds = YES;
		popup.contentView = glass;
		NSView *list = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, total)] autorelease];
		for (size_t i = 0; i < items.size(); i++)
		{
			auto &[text, enabled, checked] = items[i];
			NSButton *row = [NSButton buttonWithTitle:[(checked ? @"\u2713 " : @"    ") stringByAppendingString:NS(text.c_str())] target:popupTarget action:@selector(pick:)];
			row.frame = NSMakeRect(6, total - 4 - rowHeight * (i + 1), width - 12, rowHeight);
			row.bordered = NO;
			row.alignment = NSTextAlignmentLeft;
			row.tag = NSInteger(i + 1);
			row.enabled = enabled;
			[list addSubview:row];
		}
		NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:glass.bounds] autorelease];
		scroll.drawsBackground = NO;
		scroll.hasVerticalScroller = total > height;
		scroll.documentView = list;
		[list scrollPoint:NSMakePoint(0, total - height)];
		[glass addSubview:scroll];
		popupDone = std::move(done);
		// a click anywhere else closes it, as a menu's does; not by the key window, so nothing has to give up the keyboard
		NSEventMask mask = NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown;
		popupGlobal = [NSEvent addGlobalMonitorForEventsMatchingMask:mask handler:^(NSEvent *) { ClosePopup(0); }];
		popupLocal = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent *(NSEvent *e) {
			if (e.window != popup) dispatch_async(dispatch_get_main_queue(), ^{ ClosePopup(0); });
			return e;
		}];
		[popup orderFrontRegardless];
	}
}
