// Overlay mode's own desktop windows (OVERLAY.md): each menu is a real top-level layered window, drawn here in
// software with per-pixel alpha. The look is a Theme (Windows Vista, 7, 8, 10, Classic Mac, macOS).
#include "OverlayWindows.h"
#include "OverlayWalls.h"
#include "OverlayBuddy.h"
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602 // IFolderView2 for the desktop icons, IDesktopWallpaper for holes (the build targets XP)
#include "Overlay.h"
#include "gui/game/GameController.h"
#include "gui/game/GameModel.h"
#include "gui/game/Notification.h"
#include "client/SaveFile.h"
#include "client/SaveInfo.h"
#include "client/http/SearchSavesRequest.h"
#include "client/http/ThumbnailRequest.h"
#include "client/http/Request.h"
#include "gui/SavePreviewType.h"
#include "client/GameSave.h"
#include "gui/game/GameView.h"
#include "graphics/VideoBuffer.h"
#include "simulation/Simulation.h"
#include "simulation/Air.h"
#include "simulation/ElementGraphics.h"
#include "simulation/SimulationData.h"
#include "simulation/ElementClasses.h"
#include "SimulationConfig.h"
#include "gui/game/Menu.h"
#include "gui/game/Brush.h"
#include "gui/game/tool/Tool.h"
#include "prefs/GlobalPrefs.h"
#include "common/platform/Platform.h"
#include "graphics/Renderer.h"
#include "gui/game/RenderPreset.h"
#include "gui/game/IntroText.h"
#include "gui/options/OptionsModel.h"
#include "lua/CommandInterface.h"
#include "Format.h"
#include "luna_png.h"
#include "xp2_png.h"
#include "skin_vista_png.h"
#include "skin_seven_png.h"
#include "skin_eight_png.h"
#include "skin_ten_png.h"
#include "skin_system6_png.h"
#include "skin_system7_png.h"
#include "templeos_font_ttf.h"
#include "skin_mac9_png.h"
#include "skin_aqua_png.h"
#include "skin_lion_png.h"
#include "skin_yosemite_png.h"
#include "skin_macos_png.h"
#include "skin_tendark_png.h"
#include "skin_macosdark_png.h"
#include "OverlaySkins.h"
#include "gui/dialogues/ErrorMessage.h"
#include "client/Client.h"
#include "simulation/SaveRenderer.h"
#include "Config.h"
#include "gui/interface/Engine.h"
#include "graphics/Graphics.h"
#include "gui/interface/Window.h"
#include "gui/interface/Button.h"
#include "gui/interface/Checkbox.h"
#include "gui/interface/DropDown.h"
#include "gui/interface/Label.h"
#include "gui/interface/Panel.h"
#include "gui/interface/ProgressBar.h"
#include "gui/interface/Slider.h"
#include "gui/interface/Textbox.h"
#include <algorithm>
#include <span>
#include <cstring>
#include <cwctype>
#include <atomic>
#include <thread>
#include <mutex>
#include <tuple>
#include <cmath>
#include <functional>
#include <SDL.h>
#include <map>
#include <memory>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <uxtheme.h>
#include <vssym32.h>
#pragma comment(lib, "uxtheme.lib")
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <shlobj.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <exdisp.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <uiautomation.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <ctime>
#include <deque>
// the Overlay tab's pass-through apps (Overlay.PassApps)
static std::vector<ByteString> PassApps();
static void SetPassApps(const std::vector<ByteString> &apps);
static std::vector<ByteString> RunningApps();
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#undef small       // rpcndr.h, via dwmapi.h
#undef GetUserName // SaveInfo::GetUserName
#elif defined(__APPLE__)
#include "Overlay.h"
#include "gui/game/GameController.h"
#include "gui/game/GameModel.h"
#include "gui/game/GameView.h"
#include "gui/game/Notification.h"
#include "gui/interface/Engine.h"
#include "client/GameSave.h"
#include "client/http/Request.h"
#include "simulation/Simulation.h"
#include "simulation/ElementClasses.h"
#include "simulation/SimulationData.h"
#include "SimulationConfig.h"
#include "prefs/GlobalPrefs.h"
#include "common/platform/Platform.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <codecvt>
#include <cstring>
#include <deque>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <SDL.h>
#include <tuple>
#include <vector>
#include "OverlayMacWin32.h"
#include "gui/game/Menu.h"
#include "gui/game/Brush.h"
#include "gui/game/tool/Tool.h"
#include "gui/game/RenderPreset.h"
#include "gui/game/IntroText.h"
#include "gui/SavePreviewType.h"
#include "gui/options/OptionsModel.h"
#include "gui/dialogues/ErrorMessage.h"
#include "client/Client.h"
#include "client/SaveFile.h"
#include "client/SaveInfo.h"
#include "client/http/SearchSavesRequest.h"
#include "client/http/ThumbnailRequest.h"
#include "graphics/VideoBuffer.h"
#include "graphics/Graphics.h"
#include "graphics/Renderer.h"
#include "simulation/Air.h"
#include "simulation/ElementGraphics.h"
#include "simulation/SaveRenderer.h"
#include "lua/CommandInterface.h"
#include "Format.h"
#include "Config.h"
#include "gui/interface/Window.h"
#include "gui/interface/Button.h"
#include "gui/interface/Checkbox.h"
#include "gui/interface/DropDown.h"
#include "gui/interface/Label.h"
#include "gui/interface/Panel.h"
#include "gui/interface/ProgressBar.h"
#include "gui/interface/Slider.h"
#include "gui/interface/Textbox.h"
#include <atomic>
#include <chrono>
#include <cwctype>
#include <functional>
#include <thread>
static std::vector<ByteString> PassApps();
static void SetPassApps(const std::vector<ByteString> &apps);
static std::vector<ByteString> RunningApps();
#endif

#if defined(_WIN32) || defined(__APPLE__)
namespace
{
	float uiScale = 1;
	bool windowWalls = true;            // open windows are walls to particles (Overlay.WindowWalls)
	bool iconWalls = true;              // and so are the desktop icons (Overlay.IconWalls)
	bool areaFrame = false;             // a faint line around a play area smaller than the screen (Overlay.AreaFrame)
	// The Fun tab's settings (Overlay.Fun.*), all off by default
	struct
	{
		float pets = 0, weather = 0, music = 0; // pets: how many; weather and music 0-3
		float weatherIdle = 0, weatherLast = 90; // weather only after this many minutes idle (0: always); seconds a piece stays
		float hot = 0;                           // busy apps' windows warm what sits on them, 0-3
		float hourglass = 0;                     // minutes the taskbar hourglass runs (0: none)
		int weatherKind = 0;                    // 0 by the season, 1 snow, 2 rain, 3 leaves, 4 hail, 5 fog
		float napFrom = 0, napUntil = 7;         // pets nap from/until this hour (napFrom 0: never; 24 = midnight)
		float gather = 0, welcome = 2;           // pets gather after this many minutes away (0: never); hop this many s back
		float snowmen = 0, snowDepth = 20;       // at most this many snowmen, on snow at least this deep (px)
		float lift = 0;                          // how hard a window dragged up throws what sits on it
		float planes = 0, planeSpeed = 1;        // a paper plane every this many minutes (0: none), how fast
		float rainbow = 0, rainbowGlow = 0.6f;   // seconds a rainbow shows after rain (0: never), how strong
		float rainbowDouble = 0.25f;             // the chance it is a double rainbow
		float fetch = 0, kick = 1;               // pets fetch balls this far away (px, 0: no), kick strength
		float volcano = 0, volcanoForce = 1;     // CPU use (0-1) that wakes the taskbar volcano (0: none), lava force
		bool bin = false, broom = false, closeToss = false, openShake = false, holidays = false, notify = false, dayNight = false, trail = false, confetti = false;
		bool castles = false, perch = false;     // pets build sandcastles; morning birds land and rest
		float stars = 0, puddles = 0, wind = 0;  // shooting stars an hour at night; s rain lies as puddles; mouse wind
		bool starsLand = false;                  // now and then a shooting star comes down and hits something
		float birdRest = 10;                     // s a landed bird rests (about)
		bool petHoles = false, showers = false;  // pets use holes in windows; meteor showers on their nights
		bool taskThrow = false, taskPop = false; // Ctrl-drag a taskbar button off to throw a block; apps that open pop one out
		bool tilt = false, weekday = false, typing = false; // gravity leans to the active window; weather by weekday; typing sparks
		float meteors = 0;                       // big meteors an hour at night
		bool buddy = false;                      // Interactive Buddy (OverlayBuddy; his file: Overlay.Fun.BuddySwf)
		bool buddyPets = false;                  // the pets notice him (OverlayBuddy reads Overlay.Fun.BuddyPets)
		bool buddyLooks = false;                 // he looks wet and frosted (Overlay.Fun.BuddyLooks)
		bool buddyTracks = false, buddySplash = false, buddyFireman = false, buddyCarry = false, buddyGlow = false; // footprints, splashes, he puts out fires, carries sand (Overlay.Fun.Buddy*)
		// the Buddy tab (OverlayBuddy reads them from the prefs): his size and speech bubbles' (%), toys at once (0: no
		// limit), standing on the active window, smoothing, speech
		float buddySize = 100, buddySpeechSize = 100, buddyToys = 6, buddyVolume = 50;
		bool buddySmooth = true, buddySpeech = true;
		int buddyWindows = 1; // 0 walks through windows, 1 stands on the active one, 2 all solid (as to sand)
		std::vector<HWND> flashed; // windows that flashed their taskbar button (a new message) since the last tick
	} fun;
	// The Fun tab's rows, by group, and the pref each is saved as. A row with no tip starts a group (name = heading);
	// a row with neither b nor v is the weather kind. Slider units: 0 %, 1 count, 2 minutes, 3 seconds (10 s steps),
	// 4 clock hour, 5 px, 6 seconds (0 off), 7 an hour, 8 a percentage, 9 count (0 no limit)
	// kind: 0 as above, 1 Buddy's windows choice, 2 Buddy's game menus, 3 importing his save
	struct FunRow { const wchar_t *name, *tip = nullptr; bool *b = nullptr; float *v = nullptr; float lo = 0, hi = 0; const char *pref = ""; int unit = 0, kind = 0; };
	FunRow Check(const wchar_t *name, const wchar_t *tip, bool *b, const char *pref) { return { name, tip, b, nullptr, 0, 0, pref }; }
	FunRow Slide(const wchar_t *name, const wchar_t *tip, float *v, float lo, float hi, const char *pref, int unit = 0) { return { name, tip, nullptr, v, lo, hi, pref, unit }; }
	FunRow funRows[] = {
		{ L"Windows and icons" },
		Check(L"Open windows are walls", L"Sand, water and snow land on your windows and pile up on their title bars. Only while the sim is in front of your windows", &windowWalls, "Overlay.WindowWalls"),
		Check(L"Desktop icons are walls", L"Sand piles up on your desktop icons, in the shape of each icon. Not while the sim is behind the icons", &iconWalls, "Overlay.IconWalls"),
		Slide(L"Window grip", L"How well sand rides along on a window you drag. Off: it slides off", &overlayFeel.grip, 0, 1, "Overlay.Feel.Grip"),
		Slide(L"Fling", L"How hard sand flies off a window you drag and then stop short or turn back", &overlayFeel.fling, 0, 3, "Overlay.Feel.Fling"),
		Slide(L"Window push", L"How hard a window you move shoves the sand in its way", &overlayFeel.push, 0, 3, "Overlay.Feel.Push"),
		Slide(L"Window lift", L"Drag a window up fast and whatever sits on it is thrown into the air (how hard)", &fun.lift, 0, 3, "Overlay.Fun.Lift"),
		Check(L"Closing a window throws its sand", L"Close or minimise a window and the pile on it is tossed off", &fun.closeToss, "Overlay.Fun.CloseToss"),
		Check(L"New windows shake sand loose", L"A window that opens bumps the sand above it into the air", &fun.openShake, "Overlay.Fun.OpenShake"),
		Check(L"Recycle Bin eats sand", L"Sand that lands on the Recycle Bin icon disappears into it (needs Desktop icons are walls)", &fun.bin, "Overlay.Fun.Bin"),

		{ L"Breaking windows" },
		Check(L"Destructible windows and icons", L"Acid, explosions and great heat eat holes in windows and icons. Only in the overlay: your real windows and icons are never touched, and clearing the sim mends them. The rest of this group needs it on", &overlayFeel.destructible, "Overlay.Feel.Destructible"),
		Check(L"Windows crack first", L"A hit on whole glass only cracks it; a second hit on the cracks breaks it out. Cracks fade as windows heal", &overlayFeel.crack, "Overlay.Fun.Crack"),
		Slide(L"Windows heal", L"Holes and broken chunks grow back in from their edges; a big hole takes about this long", &overlayFeel.heal, 0, 10, "Overlay.Fun.Heal", 2),
		Slide(L"Acid eats windows", L"How fast acid eats holes in windows and icons", &overlayFeel.acidEat, 0, 3, "Overlay.Feel.AcidEat"),
		Slide(L"Blasts and burning", L"How hard explosions, bombs, DEST, singularities and anything 2000 C or hotter break windows and icons", &overlayFeel.blast, 0, 3, "Overlay.Feel.Blast"),
		Slide(L"Black holes", L"How far and how hard black holes (NBHL) tear at the windows and icons near them", &overlayFeel.blackHole, 0, 3, "Overlay.Feel.BlackHole"),
		Slide(L"Hole opacity", L"Off: holes show straight through to whatever is really behind the window. Up: the broken part fades back in", &overlayFeel.holeOpacity, 0, 1, "Overlay.Feel.HoleSolid"),
		Check(L"Hide window borders at holes", L"Where a hole breaks through a window's edge, the thin dark border Windows draws around it is covered too. Off: the border stays, showing where the real window is", &overlayFeel.holeBorder, "Overlay.Feel.HoleBorder"),
		Slide(L"Shards", L"How many bits chip off and fly when a window breaks. Off: none", &overlayFeel.shards, 0, 3, "Overlay.Feel.Shards"),
		Slide(L"Shard size", L"How big each chip is", &overlayFeel.shardSize, 0, 6, "Overlay.Feel.ShardSize"),
		Check(L"Shards take the window's colour", L"On: chips are the colour of the window they broke off. Off: dark grey", &overlayFeel.shardTint, "Overlay.Feel.ShardTint"),
		Check(L"Shards are sim particles", L"On: broken glass in the sim, which piles up, melts and reacts. Off: solid chunks that spin, fall and fade", &overlayFeel.shardParticles, "Overlay.Feel.ShardParticles"),
		Slide(L"Explosion shake", L"How hard explosions shake your REAL windows. They go back where they were afterwards. Works without Destructible", &overlayFeel.shake, 0, 3, "Overlay.Feel.Shake"),

		{ L"Marks on windows and the wallpaper" },
		Slide(L"Scorch", L"How fast fire and lava blacken the windows they touch (gone when you clear the sim)", &overlayFeel.scorch, 0, 3, "Overlay.Feel.Scorch"),
		Slide(L"Frost", L"How fast ice, LN2 and CRYO frost the windows they touch. The frost melts", &overlayFeel.frost, 0, 3, "Overlay.Feel.Frost"),
		Slide(L"Acid etching", L"How fast acid pits the windows it touches (gone when you clear the sim)", &overlayFeel.acid, 0, 3, "Overlay.Feel.Acid"),
		Slide(L"Water streaks", L"How much water streaks the windows it runs down. The streaks dry", &overlayFeel.water, 0, 3, "Overlay.Feel.Water"),
		Check(L"Fire and acid mark the wallpaper", L"Anything 300 C or hotter blackens the desktop background behind it, and acid pits it, at the Scorch and Acid etching speeds. Only drawn by the overlay: your real wallpaper is never changed, and clearing the sim wipes it", &overlayFeel.wallpaper, "Overlay.Feel.Wallpaper"),

		{ L"Mouse and keyboard" },
		Check(L"Mouse is a broom", L"Where clicks go to the desktop, the mouse pointer is a solid paddle that shoves and scoops sand", &fun.broom, "Overlay.Fun.Broom"),
		Slide(L"Mouse breeze", L"Moving the mouse stirs the air around the pointer: a soft breeze that drifts sand, smoke and gas (where clicks go to the desktop)", &overlayFeel.wind, 0, 3, "Overlay.Feel.Wind"),
		Slide(L"Mouse wind", L"Swipe the mouse fast and it flings the loose sand, liquid and smoke right next to the pointer. Stronger and closer in than Mouse breeze", &fun.wind, 0, 3, "Overlay.Fun.Wind"),
		Check(L"Sparkle trail", L"The mouse pointer leaves a short trail of cold sparks over the desktop", &fun.trail, "Overlay.Fun.Trail"),
		Check(L"Confetti", L"Wiggle the mouse quickly from side to side to throw a burst of confetti", &fun.confetti, "Overlay.Fun.Confetti"),
		Check(L"Typing sparks", L"Typing now and then puffs a few cold sparks up from the text cursor (or from the top of the window you're typing in)", &fun.typing, "Overlay.Fun.Typing"),

		{ L"Taskbar and apps" },
		Check(L"Throw from the taskbar", L"Hold Ctrl and drag an app's taskbar button up off the taskbar: a stone block in its icon's colour drops from the pointer, thrown the way you moved", &fun.taskThrow, "Overlay.Fun.TaskThrow"),
		Check(L"Apps pop out of the taskbar", L"When an app opens a window, a stone block in its icon's colour pops up out of its taskbar button", &fun.taskPop, "Overlay.Fun.TaskPop"),
		Check(L"Notifications drop a ball", L"When an app flashes its taskbar button (a new message in Discord, Teams and the like), a ball drops onto its window", &fun.notify, "Overlay.Fun.Notify"),
		Slide(L"Busy apps run hot", L"A window warms whatever sits on it by how hard its app is working: snow melts on a busy game or browser and stays on an idle one", &fun.hot, 0, 3, "Overlay.Fun.Hot"),
		Slide(L"Music bounce", L"Sand hops and fire flares to the beat of whatever your PC is playing (it only reads how loud it is)", &fun.music, 0, 3, "Overlay.Fun.Music"),
		Check(L"Gravity leans to your window", L"Gravity tilts toward the side of the screen the window you're using is on, so loose sand slides that way (only while Gravity is Vertical, in the Simulation tab)", &fun.tilt, "Overlay.Fun.Tilt"),
		Slide(L"Hourglass", L"A glass hourglass on the taskbar whose sand runs out in this many minutes, counted from when you set it; then it starts again", &fun.hourglass, 0, 120, "Overlay.Fun.Hourglass", 2),
		Slide(L"Volcano wakes at", L"A little volcano by the clock smokes once your PC's total CPU use passes this, and erupts lava at 20% more. Off: no volcano", &fun.volcano, 0, 1, "Overlay.Fun.Volcano"),
		Slide(L"Volcano force", L"How high the volcano throws its lava", &fun.volcanoForce, 0.25f, 3, "Overlay.Fun.VolcanoForce"),

		{ L"Pets" },
		Slide(L"Pets", L"Little stick figures that wander over your windows and taskbar (how many). Fire and long falls can kill them; new ones drop in", &fun.pets, 0, 10, "Overlay.Fun.Pets", 1),
		Check(L"Pets build sandcastles", L"A pet next to a big enough sand pile now and then builds a little castle from it. It crumbles back into sand after 5 minutes", &fun.castles, "Overlay.Fun.Castles"),
		Check(L"Pets explore holes", L"Pets drop through holes broken in windows instead of turning back, and stop to look out of a hole they stand in", &fun.petHoles, "Overlay.Fun.PetHoles"),
		Slide(L"Pets fetch", L"Pets run after a ball up to this far away and kick it back toward the mouse pointer", &fun.fetch, 0, 400, "Overlay.Fun.Fetch", 5),
		Slide(L"Fetch kick", L"How hard the pets kick the ball back", &fun.kick, 0.25f, 3, "Overlay.Fun.Kick"),
		Slide(L"Paper planes", L"Every this many minutes a pet (or the edge of the screen) throws a paper plane that glides until it lands", &fun.planes, 0, 30, "Overlay.Fun.Planes", 2),
		Slide(L"Plane speed", L"How fast the paper planes fly", &fun.planeSpeed, 0.25f, 3, "Overlay.Fun.PlaneSpeed"),
		Slide(L"Pets nap from", L"From this hour the pets stop and doze (z's float up) until you move the mouse; they doze off again after 10 s", &fun.napFrom, 0, 24, "Overlay.Fun.NapFrom", 4),
		Slide(L"Pets wake at", L"The hour the pets stop napping", &fun.napUntil, 1, 12, "Overlay.Fun.NapUntil", 4),
		Slide(L"Pets gather after", L"Leave the PC alone this many minutes and the pets gather in the middle of the screen", &fun.gather, 0, 30, "Overlay.Fun.Gather", 2),
		Slide(L"Welcome back hop", L"How long the pets hop for joy when you come back after a minute or more away", &fun.welcome, 0, 10, "Overlay.Fun.Welcome", 6),

		{ L"Weather" },
		Slide(L"Weather", L"Snow, rain (with lightning), leaves, hail or fog coming down from the top of the screen (how heavy)", &fun.weather, 0, 3, "Overlay.Fun.Weather"),
		{ L"Weather is", L"By season: snow from December to February, leaves from September to November, rain the rest of the year" },
		Check(L"Weekday weather", L"Monday rain, Tuesday fog, Wednesday hail, Thursday snow, Friday leaves; weekends by the season. Only while Weather is By season", &fun.weekday, "Overlay.Fun.Weekday"),
		Slide(L"Weather waits", L"Weather only starts once you have left the PC alone this many minutes, and stops when you come back. Off: always", &fun.weatherIdle, 0, 30, "Overlay.Fun.WeatherIdle", 2),
		Slide(L"Weather lasts", L"How long each flake, leaf or drop stays before it melts or fades; they thin out one by one", &fun.weatherLast, 10, 600, "Overlay.Fun.WeatherLast", 3),
		Slide(L"Puddles last", L"Rain that lands on something lies there as a puddle this many seconds more before it dries up", &fun.puddles, 0, 120, "Overlay.Fun.Puddles", 6),
		Slide(L"Rainbow after rain", L"When rain has fallen half a minute or more and stops, a rainbow shows for this many seconds", &fun.rainbow, 0, 120, "Overlay.Fun.Rainbow", 6),
		Slide(L"Rainbow brightness", L"How strong the rainbow's colours are (it stays see-through, like a real one)", &fun.rainbowGlow, 0.1f, 1, "Overlay.Fun.RainbowGlow"),
		Slide(L"Double rainbow chance", L"How often a second, fainter rainbow, its colours the other way round, shows above the first", &fun.rainbowDouble, 0, 1, "Overlay.Fun.RainbowDouble"),
		Slide(L"Snowmen", L"Where snow lies deep enough, up to this many snowmen appear (made of ice: they melt when it warms up)", &fun.snowmen, 0, 5, "Overlay.Fun.Snowmen", 1),
		Slide(L"Snowman needs", L"How deep the snow must be before a snowman appears on it", &fun.snowDepth, 6, 40, "Overlay.Fun.SnowDepth", 5),

		{ L"Sky and holidays" },
		Check(L"Day and night", L"By the clock: fireflies come out and fallen leaves go dark in the evening, birds fly over in the morning", &fun.dayNight, "Overlay.Fun.DayNight"),
		Check(L"Birds land and rest", L"The morning birds (Day and night) now and then land on a window, icon or pile and rest a while", &fun.perch, "Overlay.Fun.Perch"),
		Slide(L"Birds rest for", L"About how long a bird that landed sits before it flies on", &fun.birdRest, 2, 60, "Overlay.Fun.BirdRest", 6),
		Slide(L"Shooting stars", L"At night (8 pm to 5 am), about this many shooting stars an hour streak across the empty desktop", &fun.stars, 0, 30, "Overlay.Fun.Stars", 7),
		Check(L"Shooting stars land", L"Now and then a shooting star comes all the way down and hits a window or a pile with a burst of sparks; with Destructible on it knocks a chunk out of the window", &fun.starsLand, "Overlay.Fun.StarsLand"),
		Check(L"Meteor showers", L"On the real meteor shower nights (Quadrantids, Lyrids, Eta Aquariids, Perseids, Orionids, Leonids, Geminids) shooting stars come ten times as often, at least 60 an hour", &fun.showers, "Overlay.Fun.Showers"),
		Slide(L"Big meteors", L"At night, about this many big glowing meteors an hour crash down: a crater in the sand, and with Destructible on a big chunk out of a window (3 times as many on meteor shower nights)", &fun.meteors, 0, 10, "Overlay.Fun.Meteors", 7),
		Check(L"Holidays", L"Pumpkins roll about on Halloween, jelly hearts fall on Valentine's Day, fireworks go up after midnight on New Year's", &fun.holidays, "Overlay.Fun.Holidays"),

		{ L"Interactive Buddy" },
		Check(L"Interactive Buddy", L"The buddy from the Flash game Interactive Buddy lives on your desktop in front of the sim: drag him, throw him, give him toys. Bring your own copy of the game (Interactive Buddy v1.01 or v1.02, a .swf file; v1.02 has the pistol and machine gun; it isn't included) and pick it with Choose SWF. Once his file is chosen, his own settings are in the Buddy tab", &fun.buddy, "Overlay.Fun.Buddy"),
	};
	// The Buddy tab (shown once his file is chosen): the desktop Interactive Buddy's own settings, then the sim's
	FunRow buddyRows[] = {
		{ L"Buddy" },
		Check(L"Interactive Buddy", L"Turns him on or off. The game file is your own copy of Interactive Buddy v1.01 or v1.02 (a .swf file; it isn't included)", &fun.buddy, "Overlay.Fun.Buddy"),
		Slide(L"Size", L"How big Buddy is, whatever the sim's pixel size (100% is the game's own size)", &fun.buddySize, 25, 300, "Overlay.Buddy.Size", 8),
		{ L"Windows", L"Walk through: windows don't touch him. Active window: as in the desktop Interactive Buddy, he stands on the top edge of the window you're using (and its bottom edge from inside it) and rides along when you move it. Solid: every window and icon is solid to him like it is to sand (needs Open windows are walls)", nullptr, nullptr, 0, 0, "Overlay.Buddy.Windows", 0, 1 },
		Slide(L"Volume", L"How loud the game's sounds are (far left: silent)", &fun.buddyVolume, 0, 100, "Overlay.Buddy.Volume", 8),
		Check(L"Smooth motion", L"The game's 120 Hz smoothing of his movement", &fun.buddySmooth, "Overlay.Buddy.Smooth"),
		{ L"Game" },
		{ L"Menus", L"The game's own menus, as in the desktop app (also on right-clicking Buddy): load and save, skins and the skin store, items and weapons, modes and the mode store, the game's settings, help", nullptr, nullptr, 0, 0, "", 0, 2 },
		{ L"Save", L"Brings in your save from the desktop Interactive Buddy app (money, skins, items, modes). Buddy's save here is kept beside it as a .bak", nullptr, nullptr, 0, 0, "", 0, 3 },
		{ L"Speech" },
		Check(L"Speech bubbles", L"What Buddy says shows in a bubble over him", &fun.buddySpeech, "Overlay.Buddy.Speech"),
		Slide(L"Speech bubble size", L"How big his speech bubbles are", &fun.buddySpeechSize, 25, 200, "Overlay.Buddy.SpeechSize", 8),
		{ L"Toys" },
		Slide(L"Toys at once", L"How many of the game's toys and objects can be out at the same time (the game's own default is 6). Changing it clears the ones out now. Far left: no limit", &fun.buddyToys, 0, 100, "Overlay.Buddy.Toys", 9),
		{ L"Buddy and the sim" },
		Check(L"Pets meet Buddy", L"The pets walk over to Buddy and hop hello, and run when he is thrown past them or something blows up by him", &fun.buddyPets, "Overlay.Fun.BuddyPets"),
		Check(L"Buddy gets wet and frosty", L"Water and rain leave Buddy darker for a while, and anything below -50 C (cryo, liquid nitrogen) frosts him pale blue", &fun.buddyLooks, "Overlay.Fun.BuddyLooks"),
		Check(L"Buddy glows when shocked", L"A spark, lightning or electrons running through Buddy make him flicker bright blue-white for half a second. (Sparks already run on through him into metal he touches, and into metal powder he carries)", &fun.buddyGlow, "Overlay.Fun.BuddyGlow"),
		Check(L"Footprints", L"Walking through sand and other powders, Buddy's feet push it aside and leave dents; with wet feet he leaves drops of water on dry ground", &fun.buddyTracks, "Overlay.Fun.BuddyTracks"),
		Check(L"Splash", L"Thrown into sand or water, Buddy sends it flying up and out around where he lands", &fun.buddySplash, "Overlay.Fun.BuddySplash"),
		Check(L"Fireman", L"When something burns near Buddy (and he isn't on fire himself), he sprays water at it from his hand for a few seconds, then rests before the next fire", &fun.buddyFireman, "Overlay.Fun.BuddyFireman"),
		Check(L"Buddy carries sand and water", L"When his hand goes into sand, water or another powder or liquid that isn't hot, he scoops up a handful that rides on his hand. Swing or throw him and it flies off; otherwise he drops it after a few seconds", &fun.buddyCarry, "Overlay.Fun.BuddyCarry"),
	};
	bool BuddyTabShown() { return !GlobalPrefs::Ref().Get("Overlay.Fun.BuddySwf", ByteString("")).empty(); }
	// Interactive Buddy's file: the game's .swf, from the user (turns him on)
	void ChooseBuddySwf()
	{
#ifdef _WIN32
		wchar_t file[MAX_PATH] = L"";
		OPENFILENAMEW of{};
		of.lStructSize = sizeof(of);
		of.lpstrFilter = L"Flash games (*.swf) *.swf ";
		of.lpstrFile = file;
		of.nMaxFile = MAX_PATH;
		of.lpstrTitle = L"Your copy of Interactive Buddy";
		of.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		if (!GetOpenFileNameW(&of)) return;
		GlobalPrefs::Ref().Set("Overlay.Fun.BuddySwf", Platform::WinNarrow(file));
		fun.buddy = true;
		GlobalPrefs::Ref().Set("Overlay.Fun.Buddy", true);
#endif
	}

	// paper planes in flight or landed (sim pixels; landed: ticks since), and when the rainbow goes (GetTickCount64)
	struct PaperPlane { float x, y, vx, vy; int landed; };
	std::vector<PaperPlane> paperPlanes;
	ULONGLONG rainbowUntil = 0;
	int funHour = 12; // the local hour, for drawing
	bool rainbowTwice = false; // this one is a double rainbow
	HWND mainWindow = nullptr;    // TPT's own window
	std::string recording;        // Overlay.Keys.<name> being recorded in the Overlay tab, or empty
	std::function<void()> later; // run on the next tick, outside any window's message handling
	bool rebuildPalette = false;  // the UI scale changed: remake the side window (not from inside its own click)
	int reopenTab = -1;
	int S(float v)
	{
		return int(std::lround(v * uiScale));
	}

	// Premultiplied BGRA, the format UpdateLayeredWindow wants.
	struct Canvas
	{
		int w = 0, h = 0;
		std::vector<uint32_t> px;

		void Resize(int nw, int nh)
		{
			w = nw;
			h = nh;
			px.assign(size_t(w) * h, 0);
		}

		// Blends rgb (0xRRGGBB) at alpha a (0-255) over pixel (x, y).
		void Blend(int x, int y, uint32_t rgb, int a)
		{
			if (x < 0 || y < 0 || x >= w || y >= h || a <= 0)
			{
				return;
			}
			auto &d = px[size_t(y) * w + x];
			int inv = 255 - a;
			auto ch = [&](int shift) {
				int s = (rgb >> shift) & 0xFF;
				int dv = (d >> shift) & 0xFF;
				return uint32_t((s * a + dv * inv) / 255) << shift;
			};
			uint32_t da = d >> 24;
			d = ch(16) | ch(8) | ch(0) | (uint32_t(a + da * inv / 255) << 24);
		}

		// A rounded rectangle with a vertical gradient (top -> bottom colour), antialiased corners.
		void RoundRect(int x, int y, int rw, int rh, int r, uint32_t top, uint32_t bottom, int a, bool squareBottom = false)
		{
			for (int j = 0; j < rh; j++)
			{
				float t = rh > 1 ? float(j) / (rh - 1) : 0;
				uint32_t c = Mix(top, bottom, t);
				for (int i = 0; i < rw; i++)
				{
					float cover = 1;
					int cx = i < r ? r - 1 - i : (i >= rw - r ? i - (rw - r) : -1);
					int cy = j < r ? r - 1 - j : (!squareBottom && j >= rh - r ? j - (rh - r) : -1);
					if (cx >= 0 && cy >= 0)
					{
						float d = std::hypot(cx + 0.5f, cy + 0.5f);
						cover = std::clamp(r - d + 0.5f, 0.f, 1.f);
					}
					Blend(x + i, y + j, c, int(a * cover));
				}
			}
		}

		// A 1px rounded outline: an outer rounded rect minus an inner one, approximated by drawing the ring.
		void RoundFrame(int x, int y, int rw, int rh, int r, uint32_t c, int a, bool squareBottom = false)
		{
			for (int j = 0; j < rh; j++)
			{
				for (int i = 0; i < rw; i++)
				{
					int cx = i < r ? r - 1 - i : (i >= rw - r ? i - (rw - r) : -1);
					int cy = j < r ? r - 1 - j : (!squareBottom && j >= rh - r ? j - (rh - r) : -1);
					float cover;
					if (cx >= 0 && cy >= 0)
					{
						float d = std::hypot(cx + 0.5f, cy + 0.5f);
						cover = std::clamp(r - d + 0.5f, 0.f, 1.f) - std::clamp(r - 1 - d + 0.5f, 0.f, 1.f);
					}
					else
					{
						cover = (i == 0 || j == 0 || i == rw - 1 || j == rh - 1) ? 1.f : 0.f;
					}
					Blend(x + i, y + j, c, int(a * cover));
				}
			}
		}

		static uint32_t Mix(uint32_t a, uint32_t b, float t)
		{
			auto ch = [&](int shift) {
				float va = float((a >> shift) & 0xFF), vb = float((b >> shift) & 0xFF);
				return uint32_t(va + (vb - va) * t + 0.5f) << shift;
			};
			return ch(16) | ch(8) | ch(0);
		}
	};

	// Text: GDI draws white-on-black into a scratch DIB, whose brightness is the coverage we blend with.
	struct Text
	{
		HDC dc = CreateCompatibleDC(nullptr);
		HFONT font = nullptr, bold = nullptr;
		std::map<int, HFONT> styled;
		const wchar_t *face = L"Segoe UI"; // the look's (Theme::font, at Theme::fontScale)
		float faceScale = 1;
		bool darkPage = false; // the look's page is dark: black and grey text is drawn light instead

		void SetScale()
		{
			if (font) DeleteObject(font);
			if (bold) DeleteObject(bold);
			for (auto &[key, f] : styled) DeleteObject(f);
			styled.clear();
			font = CreateFontW(-S(12 * faceScale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
			bold = CreateFontW(-S(12 * faceScale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
		}

		// A look's own font (TempleOS's) lacks some of the UI's symbols: plain stand-ins for those.
		std::wstring Plain(const std::wstring &in) const
		{
			if (!wcscmp(face, L"Segoe UI")) return in;
			std::wstring out;
			for (auto c : in)
				out += c == L'…' ? L"..." : c == L'·' ? L"-" : c == L'□' ? L"#" : c == L'—' ? L"--" : std::wstring(1, c);
			return out;
		}

		SIZE Measure(const std::wstring &in, bool isBold = false)
		{
			auto s = Plain(in);
			SelectObject(dc, isBold ? bold : font);
			SIZE size{};
			GetTextExtentPoint32W(dc, s.c_str(), int(s.size()), &size);
			return size;
		}

		// Segoe UI at size (12 = the normal UI text, before UI scale), weight FW_*, italic, underline.
		struct Style
		{
			int size = 12, weight = FW_NORMAL;
			bool italic = false, underline = false;
			uint32_t rgb = 0x000000;
			int px = 0; // a height in pixels instead of size (ignores UI scale)
		};
		HFONT Font(const Style &st)
		{
			auto &f = styled[(st.px ? 1000 + st.px : st.size) * 10000 + st.weight * 4 + st.italic * 2 + st.underline];
			if (!f)
			{
				f = CreateFontW(st.px ? -st.px : -S(st.size * faceScale), 0, 0, 0, st.weight, st.italic, st.underline, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
			}
			return f;
		}
		SIZE Measure(const std::wstring &s, const Style &st)
		{
			auto normal = font;
			font = Font(st);
			auto size = Measure(s);
			font = normal;
			return size;
		}
		void Draw(Canvas &cv, int x, int y, const std::wstring &s, const Style &st)
		{
			auto normal = font;
			font = Font(st);
			Draw(cv, x, y, s, st.rgb);
			font = normal;
		}

		// Draws s with its top-left at (x, y), clipped to maxW. glow > 0 adds Vista's soft white halo behind it.
		void Draw(Canvas &cv, int x, int y, const std::wstring &in, uint32_t rgb, bool isBold = false, int maxW = 1 << 20, int glow = 0)
		{
			auto s = Plain(in);
			if (int r = rgb >> 16 & 0xFF, g = rgb >> 8 & 0xFF, b = rgb & 0xFF; darkPage && std::max({ r, g, b }) < 0x90 && std::max({ r, g, b }) - std::min({ r, g, b }) < 24)
				rgb = 0xFFFFFF - rgb; // ponytail: assumes it sits on the page; text on its own light box would need its own colour
			auto size = Measure(s, isBold);
			int pad = glow;
			int tw = std::min(int(size.cx), maxW) + 2 * pad, th = size.cy + 2 * pad;
			if (tw <= 0 || th <= 0)
			{
				return;
			}
			BITMAPINFO bi{};
			bi.bmiHeader = { sizeof(bi.bmiHeader), tw, -th, 1, 32, BI_RGB };
			uint32_t *bits;
			auto dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
			auto old = SelectObject(dc, dib);
			SelectObject(dc, isBold ? bold : font);
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, RGB(255, 255, 255));
			RECT rc{ pad, pad, tw - pad, th - pad };
			DrawTextW(dc, s.c_str(), int(s.size()), &rc, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			GdiFlush();
			std::vector<int> cover(size_t(tw) * th);
			for (int i = 0; i < tw * th; i++)
			{
				cover[i] = (bits[i] >> 8) & 0xFF;
			}
			if (glow)
			{
				// A box blur of the coverage, brightened: the halo.
				std::vector<int> tmp(cover.size()), halo(cover.size());
				for (int j = 0; j < th; j++)
					for (int i = 0; i < tw; i++)
					{
						int sum = 0;
						for (int k = -glow; k <= glow; k++) sum += cover[j * tw + std::clamp(i + k, 0, tw - 1)];
						tmp[j * tw + i] = sum / (2 * glow + 1);
					}
				for (int j = 0; j < th; j++)
					for (int i = 0; i < tw; i++)
					{
						int sum = 0;
						for (int k = -glow; k <= glow; k++) sum += tmp[std::clamp(j + k, 0, th - 1) * tw + i];
						halo[j * tw + i] = std::min(255, sum / (2 * glow + 1) * 4);
					}
				for (int j = 0; j < th; j++)
					for (int i = 0; i < tw; i++) cv.Blend(x - pad + i, y - pad + j, 0xFFFFFF, halo[j * tw + i] * 3 / 4);
			}
			for (int j = 0; j < th; j++)
				for (int i = 0; i < tw; i++) cv.Blend(x - pad + i, y - pad + j, rgb, cover[j * tw + i]);
			SelectObject(dc, old);
			DeleteObject(dib);
		}
	};
	Text *text;
	bool showTPTMenus = false;

	std::wstring Wide(const String &s)
	{
		return Platform::WinWiden(s.ToUtf8());
	}

	// Walls have no name in TPT (it draws them as icons), so they get their ID's tail: DEFAULT_WL_CNDTW -> CNDTW.
	std::wstring Label(Tool *tool)
	{
		if (tool->Name.size())
		{
			return Wide(tool->Name);
		}
		auto id = tool->Identifier;
		auto cut = id.rfind('_');
		return Platform::WinWiden(cut == id.npos ? id : id.substr(cut + 1));
	}

	enum CaptionKind { CaptionMin, CaptionMax, CaptionClose };

	// A caption glyph centred on (cx, cy): an x (close), a square (maximise), a bar (minimise) or, with plus, a +.
	void Glyph(Canvas &cv, int cx, int cy, CaptionKind kind, uint32_t c, int size, int thick, bool plus = false)
	{
		int k = size / 2;
		if (kind == CaptionClose)
		{
			for (int i = -k; i <= k; i++)
				for (int t = 0; t < thick; t++)
				{
					cv.Blend(cx + i + t, cy + i, c, 255);
					cv.Blend(cx + i + t, cy - i, c, 255);
				}
			return;
		}
		if (kind == CaptionMax && !plus)
		{
			for (int t = 0; t < thick; t++) cv.RoundFrame(cx - k + t, cy - k + t, 2 * k + 1 - 2 * t, 2 * k + 1 - 2 * t, 0, c, 255);
			return;
		}
		cv.RoundRect(cx - k, cy - thick / 2, 2 * k + 1, thick, 0, c, c, 255);
		if (plus) cv.RoundRect(cx - thick / 2, cy - k, thick, 2 * k + 1, 0, c, c, 255);
	}

	// The desktop as the GPU composes it (DXGI desktop duplication, what screen recorders use), minus windows kept out
	// of captures. Grab copies one screen rectangle out at half size; the last frame is kept for when nothing has
	// changed.
#ifdef _WIN32
	struct DesktopGrab
	{
		ID3D11Device *dev = nullptr;
		ID3D11DeviceContext *ctx = nullptr;
		IDXGIOutputDuplication *dup = nullptr;
		// The whole screen, and two read-back copies of the grabbed rectangle: each grab reads the one copied a refresh
		// ago (done by then, so no waiting on the GPU) while the GPU fills the other.
		ID3D11Texture2D *frame = nullptr, *staging[2] = {};
		ID3D11Texture2D *mip = nullptr; // the rectangle at full and half size: GenerateMips does the halving
		ID3D11ShaderResourceView *mipView = nullptr;
		ID3D11RenderTargetView *mipTarget = nullptr; // to clear the part that's off this screen
		POINT stagedAt[2] = {}; // the screen position each copy is of
		bool staged[2] = {};
		int cur = 0;
		RECT desk{};
		int sw = 0, sh = 0;
		bool have = false;

		~DesktopGrab()
		{
			Release();
		}

		void Release()
		{
			for (IUnknown *p : std::initializer_list<IUnknown *>{ mipTarget, mipView, mip, staging[0], staging[1], frame, dup, ctx, dev })
				if (p) p->Release();
			mipTarget = nullptr, mipView = nullptr, mip = nullptr;
			staging[0] = staging[1] = nullptr, frame = nullptr, dup = nullptr, ctx = nullptr, dev = nullptr;
			staged[0] = staged[1] = have = false;
		}

		bool Open(POINT at)
		{
			IDXGIFactory1 *factory;
			if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory))) return false;
			IDXGIAdapter1 *adapter;
			bool ok = false;
			for (UINT i = 0; !ok && factory->EnumAdapters1(i, &adapter) == S_OK; i++)
			{
				IDXGIOutput *output;
				for (UINT j = 0; !ok && adapter->EnumOutputs(j, &output) == S_OK; j++)
				{
					DXGI_OUTPUT_DESC d;
					output->GetDesc(&d);
					IDXGIOutput1 *output1;
					if (PtInRect(&d.DesktopCoordinates, at) && SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput1), (void **)&output1)))
					{
						if (SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
							ok = SUCCEEDED(output1->DuplicateOutput(dev, &dup));
						desk = d.DesktopCoordinates;
						output1->Release();
					}
					output->Release();
				}
				adapter->Release();
			}
			factory->Release();
			if (!ok) Release();
			return ok;
		}

		// The screen rectangle at (x, y), bw x bh, into dst at half size (off this screen: black). False: no
		// duplication here.
		bool Grab(int x, int y, int bw, int bh, uint32_t *dst)
		{
			POINT c{ x + bw / 2, y + bh / 2 };
			if (dup && !PtInRect(&desk, c)) Release(); // moved to another screen
			if (!dup && !Open(c)) return false;
			DXGI_OUTDUPL_FRAME_INFO info;
			IDXGIResource *res = nullptr;
			HRESULT hr = dup->AcquireNextFrame(0, &info, &res);
			if (hr == DXGI_ERROR_ACCESS_LOST)
			{
				Release(); // mode change, secure desktop...: reopened next time
				return false;
			}
			if (SUCCEEDED(hr))
			{
				ID3D11Texture2D *tex;
				if (info.LastPresentTime.QuadPart && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex)))
				{
					if (!frame)
					{
						D3D11_TEXTURE2D_DESC td;
						tex->GetDesc(&td);
						td.Usage = D3D11_USAGE_DEFAULT, td.BindFlags = 0, td.CPUAccessFlags = 0, td.MiscFlags = 0;
						dev->CreateTexture2D(&td, nullptr, &frame);
						sw = int(td.Width), sh = int(td.Height);
					}
					if (frame) ctx->CopyResource(frame, tex), have = true;
					tex->Release();
				}
				res->Release();
				dup->ReleaseFrame();
			}
			if (!have) return false;
			int hw = bw / 2, hh = bh / 2;
			D3D11_TEXTURE2D_DESC md;
			if (mip && (mip->GetDesc(&md), int(md.Width) != bw || int(md.Height) != bh))
			{
				mipTarget->Release(), mipView->Release(), mip->Release();
				mipTarget = nullptr, mipView = nullptr, mip = nullptr;
			}
			if (!mip)
			{
				D3D11_TEXTURE2D_DESC td{};
				td.Width = UINT(bw), td.Height = UINT(bh), td.MipLevels = 2, td.ArraySize = 1, td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				td.SampleDesc.Count = 1, td.Usage = D3D11_USAGE_DEFAULT, td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
				td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
				if (FAILED(dev->CreateTexture2D(&td, nullptr, &mip))) return false;
				dev->CreateShaderResourceView(mip, nullptr, &mipView);
				D3D11_RENDER_TARGET_VIEW_DESC rd{ td.Format, D3D11_RTV_DIMENSION_TEXTURE2D };
				dev->CreateRenderTargetView(mip, &rd, &mipTarget);
				if (!mipView || !mipTarget) return false;
			}
			for (auto &s : staging)
			{
				D3D11_TEXTURE2D_DESC sd;
				if (s && (s->GetDesc(&sd), int(sd.Width) != hw || int(sd.Height) != hh)) s->Release(), s = nullptr, staged[0] = staged[1] = false;
				if (s) continue;
				D3D11_TEXTURE2D_DESC td{};
				td.Width = UINT(hw), td.Height = UINT(hh), td.MipLevels = 1, td.ArraySize = 1, td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				td.SampleDesc.Count = 1, td.Usage = D3D11_USAGE_STAGING, td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(dev->CreateTexture2D(&td, nullptr, &s))) return false;
			}
			// copy this rectangle (the part on this screen) in, halve it, and into one read-back copy...
			int sx = x - desk.left, sy = y - desk.top;
			int l = std::max(0, sx), t = std::max(0, sy), r = std::clamp(sx + bw, 0, sw), b = std::clamp(sy + bh, 0, sh);
			if (l != sx || t != sy || r != sx + bw || b != sy + bh)
			{
				const float black[4] = {};
				ctx->ClearRenderTargetView(mipTarget, black);
			}
			if (r > l && b > t)
			{
				D3D11_BOX box{ UINT(l), UINT(t), 0, UINT(r), UINT(b), 1 };
				ctx->CopySubresourceRegion(mip, 0, UINT(l - sx), UINT(t - sy), 0, frame, 0, &box);
			}
			ctx->GenerateMips(mipView);
			ctx->CopySubresourceRegion(staging[cur], 0, 0, 0, 0, mip, 1, nullptr);
			stagedAt[cur] = { x, y };
			staged[cur] = true;
			// ...and read the other, shifted by how far the window has moved since (the first time: this one)
			int read = staged[cur ^ 1] ? cur ^ 1 : cur;
			cur ^= 1;
			D3D11_MAPPED_SUBRESOURCE m;
			if (FAILED(ctx->Map(staging[read], 0, D3D11_MAP_READ, 0, &m))) return false;
			int dx = (x - stagedAt[read].x) / 2, dy = (y - stagedAt[read].y) / 2; // dst (i, j) is the copy's (i + dx, j + dy)
			int i0 = std::max(0, -dx), i1 = std::min(hw, hw - dx);
			std::fill(dst, dst + size_t(hw) * hh, 0);
			if (i1 > i0)
				for (int j = std::max(0, -dy); j < std::min(hh, hh - dy); j++)
					memcpy(dst + size_t(j) * hw + i0, (uint8_t *)m.pData + size_t(j + dy) * m.RowPitch + size_t(i0 + dx) * 4, size_t(i1 - i0) * 4);
			ctx->Unmap(staging[read], 0);
			return true;
		}
	};

#else
	// The Mac's: what's on screen under one window, at half size, at most 30 times a second (a capture is a read from the
	// window server, no GPU copy to chain). Needs Screen Recording; without it the glass has nothing behind it to bend.
	struct DesktopGrab
	{
		HWND window = nullptr;
		double last = 0;
		bool have = false;

		bool Grab(int x, int y, int bw, int bh, uint32_t *dst)
		{
			double now = OverlayMac::Now(), s = MacScale();
			if (have && now - last < 1 / 30.0) return true;
			last = now;
			have = OverlayMac::GrabBelow(MacWindowNumber(window), x / s, y / s, bw / s, bh / s, bw / 2, bh / 2, dst);
			return have;
		}
	};

#endif
	// An OS look (pref Overlay.Theme). The theme draws the frame, caption buttons, push buttons and popups; tabs,
	// list items, check boxes, group boxes and the status bar are drawn here in its colours. Sizes are unscaled.
	struct Theme
	{
		const wchar_t *name = L"";
		const wchar_t *colour = nullptr; // one of its generation's colour schemes (themeColours), or none
		const wchar_t *font = L"Segoe UI";
		float fontScale = 1;
		uint32_t page = 0xFFFFFF, ink = 0x000000, pressedInk = 0x000000, heading = 0x1E395B, navInk = 0x1E395B, navSelInk = 0xFFFFFFFF /* none: navInk */, edge = 0x898C95, groupEdge = 0xD5DFE5;
		uint32_t strip = 0xF0F0F0, tabOff[2] = { 0xF2F2F2, 0xE3E3E3 }, tabHot[2] = { 0xEAF6FD, 0xA7D9F5 }, tabHotEdge = 0x3C7FB1;
		uint32_t status[3] = { 0xF7F9FC, 0xDCE5F1, 0xA0AFC3 }, nav[3] = { 0xF1F5FB, 0xE5EDF8, 0xA0AFC3 }; // top, bottom, edge
		uint32_t navHot[3] = { 0xF2F8FE, 0xE3EFFD, 0xB8D6FB }, navSel[3] = { 0xDCEBFC, 0xC1DBFC, 0x7DA2CE };
		uint32_t box[3] = { 0xF4F4F4, 0xDEDEDE, 0x8E8F8F }, boxHot[3] = { 0xDEF3FD, 0xB1DFFD, 0x3C7FB1 };
		uint32_t mark = 0x1C3D6E, dot[2] = { 0x8AD0F2, 0x1C64B4 };
		uint32_t boxOn = 0; // non-zero: a ticked box or radio is filled with this (macOS), mark and dot then contrast
		int radius = 3, boxRadius = 2, glow = 0;
		bool segmentTabs = false; // macOS segmented control instead of tabs
		// Caption buttons: widths (minimise, maximise, close), height, top, gap, distance from the edge (-1: the
		// frame border) and the side each sits on.
		int capW[3] = { 27, 27, 43 }, capH = 18, capTop = 1, capGap = 0, capInset = -1;
		bool capLeft[3] = { false, false, false };

		virtual ~Theme() = default;
		virtual void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) = 0;
		virtual void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) = 0;
		virtual void Button(Canvas &cv, int x, int y, int w, int h, int state) = 0; // 0 normal, 1 hover, 2 pressed/selected
		int stripAlpha = 255; // the tab strip's background (0: the frame shows through)
		// Live glass (see Glass): the window is kept out of screen captures. A paint records the glass shapes it wants
		// and Bake turns them into per-pixel samples; every display refresh Prepare grabs what's behind the window and
		// Underlay puts the glass under the cached UI.
		struct Shape { int x, y, w, h, r; float frost; uint32_t tint; float lens; };
		struct Baked { uint32_t idx, src[3], tint; uint8_t cover, frost, dark, white; }; // src: backdrop pixel per channel
		// One window's view of what's behind it: px is the window plus a margin all round; scratch is the glass layer.
		struct Backdrop
		{
			DesktopGrab grab;
			std::vector<uint32_t> px, scratch, small; // small: px at half size, blurred (what the glass samples)
			std::vector<uint32_t> probe; // some of the small pixels seen through the glass
			int w = 0, h = 0;
		};
		std::vector<Shape> shapes;
		virtual bool Live() const { return false; }
		virtual void Bake(Backdrop &bd, const Canvas &ui, std::vector<Baked> &out) { (void)bd, (void)ui, (void)out; }
		// Grabs what's behind; true when the look should change with it (a repaint).
		virtual bool Prepare(Backdrop &bd, HWND hwnd, int w, int h) { (void)bd, (void)hwnd, (void)w, (void)h; return false; }
		virtual uint32_t TabInk(bool on) const { (void)on; return ink; }
		// A check box (round: radio button) b square at x, y; false: drawn here in the theme's colours.
		virtual bool CheckBox(Canvas &cv, int x, int y, int b, bool on, bool round, bool hot) { (void)cv, (void)x, (void)y, (void)b, (void)on, (void)round, (void)hot; return false; }
		// A group box's outline; false: drawn here in groupEdge.
		virtual bool GroupFrame(Canvas &cv, int x, int y, int w, int h) { (void)cv, (void)x, (void)y, (void)w, (void)h; return false; }
		virtual void Underlay(Backdrop &bd, Canvas &cv, const Canvas &ui, const std::vector<Baked> &baked) { (void)bd, (void)cv, (void)ui, (void)baked; }
		virtual void Popup(Canvas &cv)
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(float(radius)), page, page, 250);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(float(radius)), edge, 255);
		}

		RECT CaptionRect(int w, int border, CaptionKind k) const
		{
			// outward in: on the right close, maximise, minimise; on the left close, minimise, maximise
			static const CaptionKind order[2][3] = { { CaptionClose, CaptionMax, CaptionMin }, { CaptionClose, CaptionMin, CaptionMax } };
			int x = capInset < 0 ? border : S(float(capInset)), top = capTop <= 1 ? capTop : S(float(capTop));
			for (auto o : order[capLeft[k]])
			{
				if (capLeft[o] != capLeft[k]) continue;
				int bw = S(float(capW[o]));
				if (o == k) return capLeft[k] ? RECT{ x, top, x + bw, top + S(float(capH)) } : RECT{ w - x - bw, top, w - x, top + S(float(capH)) };
				x += bw + S(float(capGap));
			}
			return {};
		}

		// The navigation pane's background, with its edge on the right.
		virtual void NavPane(Canvas &cv, int x, int y, int w, int h)
		{
			cv.RoundRect(x, y, w, h, 0, nav[0], nav[1], 255);
			for (int j = y; j < y + h; j++) cv.Blend(x + w, j, nav[2], 255);
		}

		// A navigation-pane item: plain, hovered (1) or selected (2).
		virtual void NavItem(Canvas &cv, int x, int y, int w, int h, int state)
		{
			if (state == 0) return;
			auto *c = state == 2 ? navSel : navHot;
			cv.RoundRect(x, y, w, h, S(segmentTabs ? 5.f : 2.f), c[0], c[1], 255);
			cv.RoundFrame(x, y, w, h, S(segmentTabs ? 5.f : 2.f), c[2], 255);
		}

		// A tab on the strip; the selected one's rectangle reaches the page.
		virtual void Tab(Canvas &cv, RECT rc, bool on, bool hot)
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			if (segmentTabs)
			{
				if (on || hot) cv.RoundRect(rc.left, rc.top + S(1), w, h - S(4), S(5), on ? 0xFFFFFF : 0xDDDDDD, on ? 0xFFFFFF : 0xDDDDDD, 255);
				if (on) cv.RoundFrame(rc.left, rc.top + S(1), w, h - S(4), S(5), 0xC4C4C4, 255);
				return;
			}
			cv.RoundRect(rc.left, rc.top, w, h, S(float(radius)), on ? page : (hot ? tabHot[0] : tabOff[0]), on ? page : (hot ? tabHot[1] : tabOff[1]), 255, true);
			cv.RoundFrame(rc.left, rc.top, w, h + S(3), S(float(radius)), hot ? tabHotEdge : edge, 255, true);
		}
	};

	// Windows Vista Aero.
	struct Vista : Theme
	{
		uint32_t glassTop = 0x7FA8D0, glassBottom = 0x4C7BAA; // the default Aero "sky" tint
		int glassAlpha = 205;

		Vista()
		{
			name = L"Windows Vista";
			glow = 5;
		}

		// The glass frame: a translucent tinted body, a glossy upper half on the title bar, dark outer and light
		// inner edges. The client area (below the title bar, inset by the frame) is left for the caller.
		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, glassTop, glassBottom, glassAlpha);
			cv.RoundRect(1, 1, cv.w - 2, titleH / 2, r, 0xFFFFFF, 0xFFFFFF, 60, true); // gloss
			Extra(cv, titleH);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 170);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(80), S(5));
			// The client area's sunken edge.
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x000000, 90);
		}
		virtual void Extra(Canvas &, int) {}

		// Vista's glossy red close pill, or the light glass minimise and maximise buttons. hover brightens them.
		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			bool close = kind == CaptionClose;
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			uint32_t top = close ? (hover ? 0xF3A08A : 0xE0907C) : (hover ? 0xF0F7FF : 0xE3ECF6);
			uint32_t bottom = close ? (hover ? 0xD8341A : 0xC1361D) : (hover ? 0x5FA3E8 : 0x9AB5D3);
			cv.RoundRect(rc.left, rc.top, w, h, S(3), top, bottom, close ? 255 : 235);
			cv.RoundRect(rc.left + 1, rc.top + 1, w - 2, h / 2, S(3), 0xFFFFFF, 0xFFFFFF, 70, true);
			cv.RoundFrame(rc.left, rc.top, w, h, S(3), close ? 0x5A1E12 : 0x1F3B5A, 220);
			int cx = rc.left + w / 2, cy = rc.top + h / 2;
			if (close)
			{
				Glyph(cv, cx, cy, CaptionClose, 0xFFFFFF, S(7), S(2));
			}
			else if (kind == CaptionMax)
			{
				// A white window outline with a thick top, outlined dark.
				int bw = S(9), bh = S(7), bx = cx - bw / 2, by = cy - bh / 2;
				cv.RoundFrame(bx - 1, by - 1, bw + 2, bh + 2, 0, 0x1F3B5A, 200);
				cv.RoundFrame(bx, by, bw, bh, 0, 0xFFFFFF, 255);
				cv.RoundRect(bx, by, bw, S(2), 0, 0xFFFFFF, 0xFFFFFF, 255);
			}
			else
			{
				// The white bar, outlined dark like Vista's.
				int bw = S(9), bh = S(3), bx = cx - bw / 2, by = cy + S(1);
				cv.RoundRect(bx - 1, by - 1, bw + 2, bh + 2, 0, 0x1F3B5A, 0x1F3B5A, 200);
				cv.RoundRect(bx, by, bw, bh, 0, 0xFFFFFF, 0xFFFFFF, 255);
			}
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xF2F2F2, 0xEAF6FD, 0xE5F4FC }, mid1[] = { 0xEBEBEB, 0xD9F0FC, 0xC4E5F6 },
				mid2[] = { 0xDDDDDD, 0xBEE6FD, 0x98D1EF }, bottom[] = { 0xCFCFCF, 0xA7D9F5, 0x68B3DB },
				edge[] = { 0x707070, 0x3C7FB1, 0x2C628B };
			int r = S(3);
			cv.RoundRect(x, y, w, h / 2, r, top[state], mid1[state], 255, true);
			cv.RoundRect(x, y + h / 2, w, h - h / 2, r, mid2[state], bottom[state], 255);
			// Square off the seam between the halves.
			for (int i = 0; i < w; i++) cv.Blend(x + i, y + h / 2 - 1, mid1[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
			cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0xFFFFFF, 120);
		}

		void Popup(Canvas &cv) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, glassTop, glassBottom, glassAlpha);
			cv.RoundRect(1, 1, cv.w - 2, cv.h / 2, r, 0xFFFFFF, 0xFFFFFF, 60, true);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 170);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
		}
	};

	// Windows 7: paler, clearer glass with the diagonal light streaks, and wider caption buttons.
	struct Seven : Vista
	{
		Seven()
		{
			name = L"Windows 7";
			glassTop = 0xA9C8EA, glassBottom = 0x7FA4CF, glassAlpha = 185;
			capW[0] = 26, capW[1] = 26, capW[2] = 45, capH = 19, capTop = 0;
		}

		void Extra(Canvas &cv, int) override
		{
			// three soft bands, only where the frame already is
			for (int band = 0; band < 3; band++)
			{
				int x0 = cv.w / 5 + band * S(70) * (band + 1), bw = S(14) + band * S(8);
				for (int y = 0; y < cv.h; y++)
					for (int x = x0 - y / 2; x < x0 - y / 2 + bw; x++)
						if (x >= 0 && x < cv.w && (cv.px[size_t(y) * cv.w + x] >> 24)) cv.Blend(x, y, 0xFFFFFF, 28);
			}
		}
	};

	// Windows 8: flat, a solid coloured frame, a centred title and flat square caption buttons.
	struct Eight : Theme
	{
		static constexpr uint32_t frame = 0x6A9BD8, frameEdge = 0x4A7CBE;

		Eight()
		{
			name = L"Windows 8";
			radius = 0, boxRadius = 0;
			capW[0] = 26, capW[1] = 27, capW[2] = 45, capH = 20, capTop = 0;
			heading = 0x000000, navInk = 0x000000, edge = 0xACACAC, groupEdge = 0xDCDCDC;
			tabOff[0] = 0xF0F0F0, tabOff[1] = 0xE5E5E5, tabHot[0] = 0xECF4FC, tabHot[1] = 0xDCECFC, tabHotEdge = 0x7EB4EA;
			status[0] = status[1] = 0xF0F0F0, status[2] = 0xD7D7D7;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0xE5E5E5;
			navHot[0] = navHot[1] = 0xE5F3FB, navHot[2] = 0x70C0E7, navSel[0] = navSel[1] = 0xCBE8F6, navSel[2] = 0x26A0DA;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x707070, boxHot[0] = boxHot[1] = 0xF3F9FF, boxHot[2] = 0x3399FF;
			mark = 0x000000, dot[0] = dot[1] = 0x333333;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, frame, frame, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, frameEdge, 255);
			int tw = text->Measure(title).cx;
			text->Draw(cv, std::max(S(10), (cv.w - tw) / 2), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(130));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, frameEdge, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (close) cv.RoundRect(rc.left, rc.top, w, h, 0, hover ? 0xE04343 : 0xC75050, hover ? 0xE04343 : 0xC75050, 255);
			else if (hover) cv.RoundRect(rc.left, rc.top, w, h, 0, 0xFFFFFF, 0xFFFFFF, 70);
			Glyph(cv, rc.left + w / 2, rc.top + h / 2 + (kind == CaptionMin ? S(3) : 0), kind, close ? 0xFFFFFF : 0x000000, S(8), S(1.5f));
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xF0F0F0, 0xECF4FC, 0xDAECFC }, bottom[] = { 0xE5E5E5, 0xDCECFC, 0xC4E0FC }, edge[] = { 0xACACAC, 0x7EB4EA, 0x569DE5 };
			cv.RoundRect(x, y, w, h, 0, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, 0, edge[state], 255);
		}
	};

	// Windows 10: a white frame with a thin border, full-height caption buttons and flat controls.
	struct Ten : Theme
	{
		Ten()
		{
			name = L"Windows 10";
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 46, capH = 28, capTop = 1, capInset = 1;
			heading = 0x000000, navInk = 0x000000, edge = 0xD9D9D9, groupEdge = 0xDCDCDC, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xF0F0F0, tabHot[0] = tabHot[1] = 0xD8EAF9, tabHotEdge = 0x7EB4EA;
			status[0] = status[1] = 0xF0F0F0, status[2] = 0xD7D7D7;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0xF0F0F0;
			navHot[0] = navHot[1] = navHot[2] = 0xE5F3FF, navSel[0] = navSel[1] = 0xCCE8FF, navSel[2] = 0x99D1FF;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x333333, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x0078D7;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFFF, 0xFFFFFF, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x6E6E6E, 200);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(160));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (hover) cv.RoundRect(rc.left, rc.top, w, h, 0, close ? 0xE81123 : 0xE5E5E5, close ? 0xE81123 : 0xE5E5E5, 255);
			Glyph(cv, rc.left + w / 2, rc.top + h / 2, kind, close && hover ? 0xFFFFFF : 0x000000, S(10), 1);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t fill[] = { 0xE1E1E1, 0xE5F1FB, 0xCCE4F7 }, edge[] = { 0xADADAD, 0x0078D7, 0x005499 };
			cv.RoundRect(x, y, w, h, 0, fill[state], fill[state], 255);
			cv.RoundFrame(x, y, w, h, 0, edge[state], 255);
		}
	};

	// Windows 10 in light and dark mode (dark: Windows-10-Dark's colours and controls)
	struct TenLight : Ten { TenLight() { colour = L"Light"; } };
	struct TenDark : Ten
	{
		TenDark()
		{
			colour = L"Dark";
			page = 0x202020, ink = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0xFFFFFF, navInk = 0xFFFFFF, edge = 0x3F3F3F, groupEdge = 0x3F3F3F, strip = 0x191919;
			tabOff[0] = tabOff[1] = 0x2B2B2B, tabHot[0] = tabHot[1] = 0x3A3A3A, tabHotEdge = 0x5A5A5A;
			status[0] = status[1] = 0x191919, status[2] = 0x2B2B2B;
			nav[0] = nav[1] = 0x191919, nav[2] = 0x2B2B2B;
			navHot[0] = navHot[1] = navHot[2] = 0x2D2D2D, navSel[0] = navSel[1] = 0x4D4D4D, navSel[2] = 0x666666;
			box[0] = box[1] = 0x202020, box[2] = 0xCCCCCC, boxHot[0] = boxHot[1] = 0x202020, boxHot[2] = 0xFFFFFF;
			mark = 0xFFFFFF, dot[0] = dot[1] = 0xFFFFFF;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0x202020, 0x202020, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x434343, 255);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0xFFFFFF, false, cv.w - S(160));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (hover) cv.RoundRect(rc.left, rc.top, w, h, 0, close ? 0xE81123 : 0x333333, close ? 0xE81123 : 0x333333, 255);
			Glyph(cv, rc.left + w / 2, rc.top + h / 2, kind, 0xFFFFFF, S(10), 1);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0x2B2B2B, 0x2B2B2B, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x767676, 255);
		}
	};

	// Classic Mac OS (Platinum): grey windows, a ridged title bar with the name centred on a plain patch, a close
	// box on the left, zoom and collapse boxes on the right, black-edged rounded buttons.
	struct ClassicMac : Theme
	{
		ClassicMac()
		{
			name = L"Mac OS 8-9";
			page = 0xDDDDDD, pressedInk = 0xFFFFFF, heading = 0x000000, navInk = 0x000000, edge = 0x666666, groupEdge = 0x888888, strip = 0xDDDDDD;
			tabOff[0] = 0xCCCCCC, tabOff[1] = 0xBBBBBB, tabHot[0] = 0xE6E6E6, tabHot[1] = 0xD0D0D0, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xDDDDDD, status[2] = 0x888888;
			nav[0] = nav[1] = 0xEEEEEE, nav[2] = 0x888888;
			navHot[0] = navHot[1] = 0xE0E0F0, navHot[2] = 0xB0B0D0, navSel[0] = navSel[1] = 0xCCCCFF, navSel[2] = 0x666699;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x000000, boxHot[0] = boxHot[1] = 0xEEEEEE, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 4, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 13, capH = 13, capTop = 8, capGap = 5, capInset = 8;
			capLeft[CaptionClose] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0xFFFFFF, 220);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			// the ridges
			for (int y = S(6); y < titleH - S(6); y += 2)
				for (int x = S(5); x < cv.w - S(5); x++)
				{
					cv.Blend(x, y, 0xFFFFFF, 255);
					cv.Blend(x, y + 1, 0x999999, 255);
				}
			auto size = text->Measure(title, true);
			int tx = std::max(S(30), (cv.w - int(size.cx)) / 2);
			cv.RoundRect(tx - S(8), S(4), std::min(int(size.cx), cv.w - S(110)) + S(16), titleH - S(8), 0, page, page, 255);
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, 0x000000, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x888888, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			cv.RoundRect(rc.left - S(3), rc.top - S(2), w + S(6), h + S(4), 0, page, page, 255); // clear of the ridges
			cv.RoundRect(rc.left, rc.top, w, h, 0, hover ? 0x999999 : 0xEEEEEE, hover ? 0x777777 : 0xBBBBBB, 255);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			if (kind == CaptionMax) cv.RoundFrame(rc.left, rc.top, w / 2 + 1, h / 2 + 1, 0, 0x000000, 255);
			if (kind == CaptionMin)
				for (int i = 0; i < 2; i++) cv.RoundRect(rc.left, rc.top + h / 2 - S(1) + i * S(2), w, 1, 0, 0x000000, 0x000000, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xEEEEEE, 0xF6F6F6, 0x666666 }, bottom[] = { 0xCCCCCC, 0xD8D8D8, 0x444444 };
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 255);
			if (state != 2) cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0xFFFFFF, 150);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0xFFFFFF, 220);
		}
	};

	// Modern macOS: a light grey window with big rounded corners, the three coloured lights on the left, a centred
	// title, white rounded buttons that turn the accent blue when picked, a segmented control for tabs.
	struct ModernMac : Theme
	{
		ModernMac()
		{
			name = L"macOS 11-15";
			page = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0x6E6E73, navInk = 0x000000, edge = 0xD0D0D0, groupEdge = 0xDCDCDC, strip = 0xECECEC;
			segmentTabs = true;
			status[0] = status[1] = 0xECECEC, status[2] = 0xD8D8D8;
			nav[0] = nav[1] = 0xE8E8EC, nav[2] = 0xD8D8D8;
			navHot[0] = navHot[1] = navHot[2] = 0xDCDCE0, navSel[0] = navSel[1] = navSel[2] = 0xCFCFD5;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0xBFBFBF, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x9A9A9A;
			boxOn = 0x007AFF, mark = 0xFFFFFF, dot[0] = dot[1] = 0xFFFFFF;
			radius = 10, boxRadius = 3;
			capW[0] = capW[1] = capW[2] = 12, capH = 12, capTop = 8, capGap = 8, capInset = 12;
			capLeft[0] = capLeft[1] = capLeft[2] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(10);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xEDEDED, 0xE6E6E6, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 60);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, true, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xD0D0D0, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFEBC2E, 0x28C840, 0xFF5F57 }, rim[] = { 0xDEA123, 0x1AAB29, 0xE0443E }, ink[] = { 0x985712, 0x0B650D, 0x4D0000 };
			int d = rc.right - rc.left;
			cv.RoundRect(rc.left, rc.top, d, d, d / 2, fill[kind], fill[kind], 255);
			cv.RoundFrame(rc.left, rc.top, d, d, d / 2, rim[kind], 255);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, ink[kind], S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xFFFFFF, 0xF5F5F5, 0x3B95FF }, bottom[] = { 0xFFFFFF, 0xF0F0F0, 0x0A6CF0 }, edge[] = { 0xC4C4C4, 0xB4B4B4, 0x0A62D8 };
			int r = S(5);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(10), 0xF2F2F2, 0xF2F2F2, 245);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(10), 0x000000, 45);
		}
	};

	// macOS in light and dark mode (dark: macOS-Dark's colours and controls)
	struct MacLight : ModernMac { MacLight() { colour = L"Light"; } };
	struct MacDark : ModernMac
	{
		MacDark()
		{
			colour = L"Dark";
			page = 0x1E1E1E, ink = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0x98989D, navInk = 0xFFFFFF, edge = 0x3A3A3A, groupEdge = 0x3A3A3A, strip = 0x2A2A2A;
			status[0] = status[1] = 0x2A2A2A, status[2] = 0x1A1A1A;
			nav[0] = nav[1] = 0x2B2B2F, nav[2] = 0x1A1A1A;
			navHot[0] = navHot[1] = navHot[2] = 0x38383C, navSel[0] = navSel[1] = navSel[2] = 0x46464B;
			box[0] = box[1] = 0x3A3A3C, box[2] = 0x5A5A5E, boxHot[0] = boxHot[1] = 0x3A3A3C, boxHot[2] = 0x7A7A7E;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(10);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0x323232, 0x2C2C2C, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 200);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 30);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0xDCDCDC, true, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x1A1A1A, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0x6A6A6E, 0x75757A, 0x3B95FF }, bottom[] = { 0x5C5C60, 0x66666A, 0x0A6CF0 };
			int r = S(5);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 90);
		}

		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			if (on || hot) cv.RoundRect(rc.left, rc.top + S(1), w, h - S(4), S(5), on ? 0x636366 : 0x48484A, on ? 0x636366 : 0x48484A, 255);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(10), 0x2C2C2E, 0x2C2C2E, 245);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(10), 0x000000, 160);
		}
	};

	void Line(Canvas &cv, int x, int y, int w, int h, uint32_t c)
	{
		cv.RoundRect(x, y, w, h, 0, c, c, 255);
	}

	// The Windows 9x 3D edge: raised (buttons, windows) or sunken (fields, pressed buttons).
	void Bevel(Canvas &cv, int x, int y, int w, int h, bool sunken)
	{
		uint32_t o1 = sunken ? 0x808080 : 0xFFFFFF, o2 = sunken ? 0xFFFFFF : 0x000000, i1 = sunken ? 0x000000 : 0xDFDFDF, i2 = sunken ? 0xDFDFDF : 0x808080;
		Line(cv, x, y, w, 1, o1), Line(cv, x, y, 1, h, o1), Line(cv, x, y + h - 1, w, 1, o2), Line(cv, x + w - 1, y, 1, h, o2);
		Line(cv, x + 1, y + 1, w - 2, 1, i1), Line(cv, x + 1, y + 1, 1, h - 2, i1), Line(cv, x + 1, y + h - 2, w - 2, 1, i2), Line(cv, x + w - 2, y + 1, 1, h - 2, i2);
	}

	// Windows 98: grey 3D chrome, a navy-to-blue title bar, small bevelled caption buttons.
	struct NinetyEight : Theme
	{
		uint32_t titleRight = 0x1084D0; // the title bar fades from navy to this

		NinetyEight()
		{
			name = L"Windows 98";
			page = 0xC0C0C0, heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x808080, groupEdge = 0x808080, strip = 0xC0C0C0;
			tabOff[0] = tabOff[1] = tabHot[0] = tabHot[1] = 0xC0C0C0, tabHotEdge = 0x808080;
			status[0] = status[1] = 0xC0C0C0, status[2] = 0x808080;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x808080;
			navHot[0] = navHot[1] = navHot[2] = 0xFFFFFF, navSel[0] = navSel[1] = navSel[2] = 0x000080;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x808080, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 16, capH = 14, capTop = 8, capGap = 2, capInset = 8;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			Bevel(cv, 0, 0, cv.w, cv.h, false);
			int bx = S(4), by = S(4), bw = cv.w - 2 * bx, bh = titleH - S(6);
			for (int i = 0; i < bw; i++)
			{
				float t = bw > 1 ? float(i) / (bw - 1) : 0;
				auto ch = [t](int a, int b) { return uint32_t(a + (b - a) * t); };
				Line(cv, bx + i, by, 1, bh, (ch(0x00, int(titleRight >> 16)) << 16) | (ch(0x00, int(titleRight >> 8 & 0xFF)) << 8) | ch(0x80, int(titleRight & 0xFF)));
			}
			text->Draw(cv, S(8), by + (bh - text->Measure(title, true).cy) / 2, title, 0xFFFFFF, true, cv.w - S(90));
			Bevel(cv, border - 2, titleH - 2, cv.w - 2 * border + 4, cv.h - titleH - border + 4, true);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			Line(cv, rc.left, rc.top, w, h, page);
			Bevel(cv, rc.left, rc.top, w, h, false);
			(void)hover; // 98 had no hover
			Glyph(cv, rc.left + w / 2, rc.top + h / 2 + (kind == CaptionMin ? S(2) : 0), kind, 0x000000, S(7), 2);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			Line(cv, x, y, w, h, page);
			Bevel(cv, x, y, w, h, state == 2);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, page);
			Bevel(cv, 0, 0, cv.w, cv.h, false);
		}
	};

	// Windows XP's own Luna images (resources/luna.png, made by resources/luna.py from luna.msstyles, via
	// github.com/listumps/wine_themes): Blue, Olive Green and Silver blocks side by side, each part at the same place
	// in each, its states stacked down it.
	enum LunaPart { LunaCaption, LunaLeft, LunaRight, LunaBottom, LunaCapButton, LunaClose, LunaCloseGlyph, LunaMinGlyph, LunaMaxGlyph, LunaRestoreGlyph, LunaButton, LunaCheck, LunaRadio, LunaGroup, LunaTab, LunaPane };
	constexpr int lunaParts[][4] = { // x, y, w, h in a colour's block
		{ 0, 0, 66, 58 }, { 0, 58, 5, 62 }, { 0, 120, 5, 62 }, { 0, 182, 49, 10 }, { 0, 192, 21, 168 }, { 66, 0, 21, 168 }, { 66, 168, 13, 104 },
		{ 66, 272, 13, 104 }, { 87, 0, 13, 104 }, { 87, 104, 13, 104 }, { 87, 208, 20, 115 }, { 107, 0, 13, 156 }, { 107, 156, 13, 104 },
		{ 107, 260, 22, 20 }, { 107, 280, 18, 90 }, { 129, 0, 48, 48 },
	};
	constexpr int lunaBlock = 177;

	// State f (of n) of part p in column col over x, y, w, h: its l, r, t, b source pixels at the edges kept (scaled
	// as the UI is), the middle stretched, as the msstyles' SizingMargins.
	void Slice(Canvas &cv, const PlaneAdapter<std::vector<pixel_rgba>> *atlas, int sx, int sy, int sw, int sh, int x, int y, int w, int h, int l, int r, int t, int b)
	{
		if (!atlas || w <= 0 || h <= 0) return;
		int aw = atlas->Size().X;
		// a destination pixel's source along one side: margins lo and hi (source px) are S()-sized, shrunk to fit
		auto along = [&](int len, int src, int lo, int hi) {
			int dlo = S(float(lo)), dhi = S(float(hi));
			if (dlo + dhi > len) dlo = len * lo / std::max(1, lo + hi), dhi = len - dlo;
			std::vector<int> m(len);
			for (int d = 0; d < len; d++)
				m[d] = d < dlo ? d * lo / std::max(1, dlo)
				     : d >= len - dhi ? src - hi + (d - (len - dhi)) * hi / std::max(1, dhi)
				     : lo + (d - dlo) * (src - lo - hi) / std::max(1, len - dlo - dhi);
			return m;
		};
		auto mx = along(w, sw, l, r), my = along(h, sh, t, b);
		auto *px = atlas->data();
		for (int j = 0; j < h; j++)
			for (int i = 0; i < w; i++)
			{
				uint32_t c = px[size_t(sy + my[j]) * aw + sx + mx[i]];
				cv.Blend(x + i, y + j, c & 0xFFFFFF, c >> 24);
			}
	}

	void Luna(Canvas &cv, int col, LunaPart p, int f, int n, int x, int y, int w, int h, int l = 0, int r = 0, int t = 0, int b = 0)
	{
		static auto atlas = format::PixelsFromPNG(luna_png.AsCharSpan()), atlas2 = format::PixelsFromPNG(xp2_png.AsCharSpan());
		int sh = lunaParts[p][3] / n;
		if (col >= 3) // xp2.png: Royale, Royale Noir, Zune
			return Slice(cv, atlas2.get(), (col - 3) * lunaBlock + lunaParts[p][0], lunaParts[p][1] + f * sh, lunaParts[p][2], sh, x, y, w, h, l, r, t, b);
		Slice(cv, atlas.get(), col * lunaBlock + lunaParts[p][0], lunaParts[p][1] + f * sh, lunaParts[p][2], sh, x, y, w, h, l, r, t, b);
	}

	// A look's controls in images of a faithful recreation (resources/skins.py, OverlaySkins.h): a part's state f, or
	// false when the look draws that part itself.
	enum SkinPart { SkinButton, SkinCheck, SkinRadio, SkinTab, SkinClose, SkinMin, SkinMax };
	struct Skin
	{
		std::span<const char> png;
		const int (*parts)[4][8];
		bool Draw(Canvas &cv, SkinPart p, int f, int x, int y, int w, int h) const
		{
			static std::map<const char *, std::unique_ptr<PlaneAdapter<std::vector<pixel_rgba>>>> atlases;
			auto &atlas = atlases[png.data()];
			if (!atlas) atlas = format::PixelsFromPNG(png);
			auto &c = parts[p][f];
			if (!c[2]) return false;
			Slice(cv, atlas.get(), c[0], c[1], c[2], c[3], x, y, w, h, c[4], c[5], c[6], c[7]);
			return true;
		}
	};

#ifdef _WIN32
	// Windows' own drawing of a control (the visual style in use, its real Windows 11 parts on Windows 11). False when
	// Windows can't (no visual style).
	// Something GDI draws (draw(dc, rc)), drawn over black and over white: the difference gives its alpha.
	void CaptureGdi(Canvas &cv, int x, int y, int w, int h, const std::function<void(HDC, RECT &)> &draw)
	{
		BITMAPINFO bi{};
		bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32 };
		HDC dc = CreateCompatibleDC(nullptr);
		uint32_t *px[2];
		HBITMAP bm[2];
		RECT rc{ 0, 0, w, h };
		for (int k = 0; k < 2; k++)
		{
			bm[k] = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px[k], nullptr, 0);
			std::fill(px[k], px[k] + size_t(w) * h, k ? 0xFFFFFFFFu : 0xFF000000u);
			SelectObject(dc, bm[k]);
			draw(dc, rc);
		}
		GdiFlush();
		for (int j = 0; j < h; j++)
			for (int i = 0; i < w; i++)
			{
				uint32_t b = px[0][size_t(j) * w + i], wh = px[1][size_t(j) * w + i];
				int a = 255 - (int((wh >> 8) & 0xFF) - int((b >> 8) & 0xFF));
				if (a <= 0) continue;
				auto ch = [&](int sh) { return uint32_t(std::min(255, int((b >> sh) & 0xFF) * 255 / a)) << sh; };
				cv.Blend(x + i, y + j, ch(16) | ch(8) | ch(0), uint8_t(a));
			}
		DeleteDC(dc);
		DeleteObject(bm[0]), DeleteObject(bm[1]);
	}

	bool NativePart(Canvas &cv, const wchar_t *cls, int part, int state, int x, int y, int w, int h)
	{
		static std::map<std::wstring, HTHEME> handles;
		auto &t = handles[cls];
		if (!t) t = OpenThemeData(nullptr, cls);
		if (!t || w <= 0 || h <= 0) return false;
		CaptureGdi(cv, x, y, w, h, [&](HDC dc, RECT &rc) { DrawThemeBackground(t, dc, part, state, &rc, nullptr); });
		return true;
	}

	bool IsWindows11()
	{
		static int build = [] {
			RTL_OSVERSIONINFOW v{ sizeof(v) };
			auto get = (LONG(WINAPI *)(RTL_OSVERSIONINFOW *))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
			return get && !get(&v) ? int(v.dwBuildNumber) : 0;
		}();
		return build >= 22000;
	}
#endif

	// A look drawn by Windows itself where this PC is that Windows (Windows 11 on Windows 11), else by hand.
	template<class Base> struct Native : Base
	{
#ifdef _WIN32
		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (!IsWindows11() || !NativePart(cv, L"BUTTON", BP_PUSHBUTTON, state == 2 ? PBS_PRESSED : state == 1 ? PBS_HOT : PBS_NORMAL, x, y, w, h)) Base::Button(cv, x, y, w, h, state);
		}
		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			if (!IsWindows11() || !NativePart(cv, L"TAB", TABP_TABITEM, on ? TIS_SELECTED : hot ? TIS_HOT : TIS_NORMAL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top)) Base::Tab(cv, rc, on, hot);
		}
		bool CheckBox(Canvas &cv, int x, int y, int b, bool on, bool round, bool hot) override
		{
			return (IsWindows11() && NativePart(cv, L"BUTTON", round ? BP_RADIOBUTTON : BP_CHECKBOX, (on ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL) + hot, x, y, b, b)) || Base::CheckBox(cv, x, y, b, on, round, hot);
		}
#endif
	};

	template<class Base> struct Skinned : Base
	{
		static constexpr SkinPart captionParts[] = { SkinMin, SkinMax, SkinClose }; // by CaptionKind
		Skin skin;
		Skinned(Skin skin) : skin(skin)
		{
			for (int k = 0; k < 3; k++) // caption buttons at their images' size
				if (int w = skin.parts[captionParts[k]][0][2]) this->capW[k] = w, this->capH = skin.parts[captionParts[k]][0][3];
		}
		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (!skin.Draw(cv, SkinButton, state, x, y, w, h)) Base::Button(cv, x, y, w, h, state);
		}
		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			if (!skin.Draw(cv, SkinTab, on ? 2 : hot, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top)) Base::Tab(cv, rc, on, hot);
		}
		bool CheckBox(Canvas &cv, int x, int y, int b, bool on, bool round, bool hot) override
		{
			return skin.Draw(cv, round ? SkinRadio : SkinCheck, (on ? 2 : 0) + hot, x, y, b, b) || Base::CheckBox(cv, x, y, b, on, round, hot);
		}
		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			if (!skin.Draw(cv, captionParts[kind], hover, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top)) Base::CaptionButton(cv, rc, hover, kind);
		}
	};

	// Windows XP Luna, in its own images: Blue (col 0), Olive Green (1) or Silver (2).
	struct XP : Theme
	{
		int col;
		uint32_t captionInk, captionShadow;

		XP(int col = 0) : col(col)
		{
			static const wchar_t *colours[] = { L"Blue", L"Olive Green", L"Silver", L"Royale", L"Royale Noir", L"Zune" };
			static const uint32_t inks[] = { 0xFFFFFF, 0xFFFFFF, 0x0E1010, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF }, shadows[] = { 0x0A1883, 0x41400A, 0xC7C2D1, 0x0A1883, 0x000000, 0x000000 },
				headings[] = { 0x0046D5, 0x99540A, 0x0046D5, 0x0046D5, 0x1C2A4A, 0xC8501C };
			name = L"Windows XP", colour = colours[col], captionInk = inks[col], captionShadow = shadows[col];
			page = 0xFCFCFE, heading = headings[col], navInk = 0x215DC6, navSelInk = 0xFFFFFF, edge = 0x919B9C, groupEdge = 0xD0D0BF, strip = col == 2 ? 0xE0DFE3 : col >= 3 ? 0xEBE9ED : 0xECE9D8;
			tabOff[0] = 0xFEFEFE, tabOff[1] = 0xECEBE6, tabHot[0] = 0xFFFFFF, tabHot[1] = 0xF4F3EE, tabHotEdge = 0xE68B2C;
			status[0] = status[1] = strip, status[2] = 0xACA899;
			nav[0] = 0xD6DFF7, nav[1] = 0xC6D3F7, nav[2] = 0x7BA2E7;
			navHot[0] = navHot[1] = 0xE8EEFB, navHot[2] = 0x7BA2E7, navSel[0] = navSel[1] = navSel[2] = 0x316AC5;
			radius = 3, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 21, capH = 21, capTop = 5, capGap = 3, capInset = 5;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Luna(cv, col, LunaCaption, 0, 2, 0, 0, cv.w, titleH, 28, 35, 9, 17);
			Luna(cv, col, LunaLeft, 0, 2, 0, titleH, border, cv.h - titleH - border, 0, 0, 2, 2);
			Luna(cv, col, LunaRight, 0, 2, cv.w - border, titleH, border, cv.h - titleH - border, 0, 0, 2, 2);
			Luna(cv, col, LunaBottom, 0, 2, 0, cv.h - border, cv.w, border, 5, 5, 2, 2);
			auto size = text->Measure(title, true);
			int ty = (titleH - size.cy) / 2;
			text->Draw(cv, S(9) + 1, ty + 1, title, captionShadow, true, cv.w - S(100));
			text->Draw(cv, S(9), ty, title, captionInk, true, cv.w - S(100));
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top, g = S(13);
			Luna(cv, col, kind == CaptionClose ? LunaClose : LunaCapButton, hover, 8, rc.left, rc.top, w, h, 5, 5, 5, 5);
			Luna(cv, col, kind == CaptionClose ? LunaCloseGlyph : kind == CaptionMin ? LunaMinGlyph : LunaMaxGlyph, hover, 8, rc.left + (w - g) / 2, rc.top + (h - g) / 2, g, g);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			Luna(cv, col, LunaButton, state, 5, x, y, w, h, 8, 8, 9, 9); // normal, hot, pressed (then disabled, default)
		}

		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			Luna(cv, col, LunaTab, on ? 2 : hot ? 1 : 0, 5, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 6, 6, 6, 6);
		}

		bool CheckBox(Canvas &cv, int x, int y, int b, bool on, bool round, bool hot) override
		{
			Luna(cv, col, round ? LunaRadio : LunaCheck, (on ? 4 : 0) + hot, round ? 8 : 12, x, y, b, b);
			return true;
		}

		bool GroupFrame(Canvas &cv, int x, int y, int w, int h) override
		{
			Luna(cv, col, LunaGroup, 0, 1, x, y, w, h, 4, 4, 4, 4);
			return true;
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFE1, 0xFFFFE1, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
		}
	};

	// TempleOS: white windows with a double blue border, blue text, [X]-style caption buttons in text.
	struct Temple : Theme
	{
		static constexpr uint32_t blue = 0x0000AA, cyan = 0x55FFFF;

		Temple()
		{
			name = L"TempleOS";
			font = L"TempleOS", fontScale = 0.65f; // its own, from github.com/PhilipPanda/TempleOS-Theme: wide, so smaller
			ink = blue, pressedInk = 0xFFFFFF, heading = blue, navInk = blue, navSelInk = 0xFFFFFF, edge = blue, groupEdge = blue, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xFFFFFF, tabHot[0] = tabHot[1] = cyan, tabHotEdge = blue;
			status[0] = status[1] = 0xFFFFFF, status[2] = blue;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = blue;
			navHot[0] = navHot[1] = cyan, navHot[2] = blue, navSel[0] = navSel[1] = navSel[2] = blue;
			box[0] = box[1] = 0xFFFFFF, box[2] = blue, boxHot[0] = boxHot[1] = cyan, boxHot[2] = blue;
			mark = blue, dot[0] = dot[1] = blue;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 24, capH = 20, capTop = 4, capGap = 2, capInset = 10;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFFF, 0xFFFFFF, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, blue, 255);
			cv.RoundFrame(S(3), S(3), cv.w - 2 * S(3), cv.h - 2 * S(3), 0, blue, 255);
			auto size = text->Measure(title);
			int tx = std::max(S(12), (cv.w - int(size.cx)) / 2);
			Line(cv, tx - S(6), 0, std::min(int(size.cx), cv.w - S(120)) + S(12), S(4), 0xFFFFFF); // the title sits in the border
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, blue, false, cv.w - S(120));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, blue, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const wchar_t *labels[] = { L"[_]", L"[\u25A1]", L"[X]" };
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			if (hover) Line(cv, rc.left, rc.top, w, h, blue);
			auto size = text->Measure(labels[kind]);
			text->Draw(cv, rc.left + (w - size.cx) / 2, rc.top + (h - size.cy) / 2, labels[kind], hover ? 0xFFFFFF : blue);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			uint32_t fill = state == 2 ? blue : state == 1 ? cyan : 0xFFFFFF;
			Line(cv, x, y, w, h, fill);
			cv.RoundFrame(x, y, w, h, 0, blue, 255);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, blue, 255);
			cv.RoundFrame(S(3), S(3), cv.w - 2 * S(3), cv.h - 2 * S(3), 0, blue, 255);
		}
	};

	// Windows 95: 98 without the title gradient.
	struct NinetyFive : NinetyEight
	{
		NinetyFive()
		{
			name = L"Windows 95";
			titleRight = 0x000080;
		}
	};

	// Windows 3.1: a grey sizing border, a solid navy title bar with the name centred, the control-menu box on the
	// left, arrow buttons on the right, white dialogs.
	struct Three : Theme
	{
		Three()
		{
			name = L"Windows 3.1";
			heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x000000, groupEdge = 0x000000, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xC0C0C0, tabHot[0] = tabHot[1] = 0xC0C0C0, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xFFFFFF, status[2] = 0x000000;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x000000;
			navHot[0] = navHot[1] = navHot[2] = 0xFFFFFF, navSel[0] = navSel[1] = navSel[2] = 0x000080;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x000000, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 20, capH = 20, capTop = 4, capGap = 0, capInset = 4;
			capLeft[CaptionClose] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xC0C0C0);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			int b = S(4);
			Line(cv, b, b, cv.w - 2 * b, titleH - b, 0x000080);
			cv.RoundFrame(b - 1, b - 1, cv.w - 2 * b + 2, titleH - b + 2, 0, 0x000000, 255);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(30), (cv.w - int(size.cx)) / 2), b + (titleH - b - size.cy) / 2, title, 0xFFFFFF, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x000000, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top, cx = rc.left + w / 2, cy = rc.top + h / 2;
			(void)hover;
			Line(cv, rc.left, rc.top, w, h, 0xC0C0C0);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			if (kind == CaptionClose)
			{
				// the control-menu box: a white bar with a dark shadow
				Line(cv, cx - S(6) + 1, cy - 1 + 1, S(12), S(3), 0x808080);
				Line(cv, cx - S(6), cy - 1, S(12), S(3), 0xFFFFFF);
				cv.RoundFrame(cx - S(6), cy - 1, S(12), S(3), 0, 0x000000, 255);
				return;
			}
			Bevel(cv, rc.left + 1, rc.top + 1, w - 2, h - 2, false);
			int k = S(4);
			for (int i = 0; i <= k; i++) // an arrow: down to minimise, up to maximise
				Line(cv, cx - i, kind == CaptionMin ? cy + k / 2 - i : cy - k / 2 + i, 2 * i + 1, 1, 0x000000);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			Line(cv, x, y, w, h, 0xC0C0C0);
			cv.RoundFrame(x, y, w, h, S(2), 0x000000, 255);
			uint32_t lt = state == 2 ? 0x808080 : 0xFFFFFF, rb = state == 2 ? 0xC0C0C0 : 0x808080;
			Line(cv, x + 1, y + 1, w - 2, S(2), lt), Line(cv, x + 1, y + 1, S(2), h - 2, lt);
			Line(cv, x + 1, y + h - 1 - S(2), w - 2, S(2), rb), Line(cv, x + w - 1 - S(2), y + 1, S(2), h - 2, rb);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0x000000, 255);
		}
	};

	// Windows 11: Windows 10's layout with rounded corners, a light Mica grey, rounded controls and the accent blue on
	// picked buttons and ticked boxes.
	struct Eleven : Ten
	{
		Eleven()
		{
			name = L"Windows 11";
			radius = 8, boxRadius = 4, pressedInk = 0xFFFFFF, strip = 0xF3F3F3, edge = 0xE5E5E5;
			tabOff[0] = tabOff[1] = 0xF3F3F3, tabHot[0] = tabHot[1] = 0xEAEAEA, tabHotEdge = 0xE5E5E5;
			status[0] = status[1] = 0xF3F3F3, status[2] = 0xE5E5E5;
			nav[0] = nav[1] = 0xF3F3F3, nav[2] = 0xE5E5E5;
			navHot[0] = navHot[1] = navHot[2] = 0xEAEAEA, navSel[0] = navSel[1] = navSel[2] = 0xE0E0E0;
			box[0] = box[1] = 0xFDFDFD, box[2] = 0x8A8A8A, boxHot[0] = boxHot[1] = 0xF3F3F3, boxHot[2] = 0x8A8A8A;
			boxOn = 0x005FB8, mark = 0xFFFFFF, dot[0] = dot[1] = 0xFFFFFF;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(8);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xF3F3F3, 0xF3F3F3, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 60);
			text->Draw(cv, S(12), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(160));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (hover)
			{
				uint32_t c = close ? 0xC42B1C : 0xE9E9E9;
				cv.RoundRect(rc.left, rc.top, w, h, close ? S(8) : 0, c, c, 255, true); // the close button follows the corner
				if (close) Line(cv, rc.left, rc.top, w - S(8), h, c);
			}
			Glyph(cv, rc.left + w / 2, rc.top + h / 2, kind, close && hover ? 0xFFFFFF : 0x000000, S(10), 1);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			int r = S(4);
			uint32_t fill = state == 2 ? 0x005FB8 : state == 1 ? 0xF6F6F6 : 0xFBFBFB;
			cv.RoundRect(x, y, w, h, r, fill, fill, 255);
			cv.RoundFrame(x, y, w, h, r, state == 2 ? 0x005FB8 : 0xE5E5E5, 255);
			if (state != 2) Line(cv, x + r, y + h - 1, w - 2 * r, 1, 0xCCCCCC); // the darker bottom edge
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(8), 0xF9F9F9, 0xF9F9F9, 250);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(8), 0x000000, 50);
		}
	};

	// System 1-6: black and white, a striped title bar, the close box on the left and the zoom box on the right.
	struct System6 : ClassicMac
	{
		uint32_t stripe = 0x000000, stripeGap = 0xFFFFFF, boxFill = 0xFFFFFF;

		System6()
		{
			name = L"System 1-6";
			page = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x000000, groupEdge = 0x000000, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = tabHot[0] = tabHot[1] = 0xFFFFFF, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xFFFFFF, status[2] = 0x000000;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x000000;
			navHot[0] = navHot[1] = 0xFFFFFF, navHot[2] = 0x000000, navSel[0] = navSel[1] = navSel[2] = 0x000000;
			box[0] = box[1] = boxHot[0] = boxHot[1] = 0xFFFFFF, box[2] = boxHot[2] = 0x000000;
			radius = 6, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 11, capH = 11, capTop = 7, capGap = 4, capInset = 8;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Line(cv, 0, 0, cv.w, cv.h, page);
			cv.RoundFrame(0, 0, cv.w - 1, cv.h - 1, 0, 0x000000, 255);
			Line(cv, cv.w - 1, 1, 1, cv.h - 1, 0x000000), Line(cv, 1, cv.h - 1, cv.w - 1, 1, 0x000000); // the drop shadow
			Line(cv, 1, 1, cv.w - 3, titleH - 2, stripeGap);
			for (int y = S(5); y < titleH - S(5); y += 2) Line(cv, S(3), y, cv.w - S(3) * 2 - 1, 1, stripe);
			auto size = text->Measure(title, true);
			int tx = std::max(S(30), (cv.w - int(size.cx)) / 2);
			Line(cv, tx - S(8), S(3), std::min(int(size.cx), cv.w - S(110)) + S(16), titleH - S(6), stripeGap);
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, 0x000000, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 1, cv.h - titleH - border + 1, 0, 0x000000, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			Line(cv, rc.left - S(2), rc.top - S(2), w + S(4), h + S(4), stripeGap); // clear of the stripes
			Line(cv, rc.left, rc.top, w, h, hover ? 0x000000 : boxFill);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			uint32_t ink = hover ? 0xFFFFFF : 0x000000;
			if (kind == CaptionMax) cv.RoundFrame(rc.left, rc.top, w / 2 + 1, h / 2 + 1, 0, ink, 255);
			if (kind == CaptionMin) Line(cv, rc.left + S(2), rc.top + h / 2, w - S(4), 1, ink);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			int r = S(6);
			cv.RoundRect(x, y, w, h, r, state == 2 ? 0x000000 : boxFill, state == 2 ? 0x000000 : boxFill, 255);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 255);
			if (state == 1) cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0x000000, 255);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w - 1, cv.h - 1, 0, 0x000000, 255);
			Line(cv, cv.w - 1, 1, 1, cv.h - 1, 0x000000), Line(cv, 1, cv.h - 1, cv.w - 1, 1, 0x000000);
		}
	};

	// System 7: the same shapes, with grey stripes, light grey dialogs and a lavender highlight.
	struct System7 : System6
	{
		System7()
		{
			name = L"System 7";
			stripe = 0x999999, stripeGap = 0xDDDDDD, boxFill = 0xEEEEEE;
			page = 0xEEEEEE, strip = 0xDDDDDD, navSelInk = 0x000000;
			tabOff[0] = tabOff[1] = 0xDDDDDD, tabHot[0] = tabHot[1] = 0xEEEEEE;
			status[0] = status[1] = 0xDDDDDD, nav[0] = nav[1] = 0xFFFFFF;
			navHot[0] = navHot[1] = 0xEEEEFF, navSel[0] = navSel[1] = 0xCCCCFF, navSel[2] = 0x6666AA;
		}
	};

	// A glossy round light (Aqua gel), lighter at the top with a highlight.
	void Gel(Canvas &cv, int x, int y, int w, int h, int r, uint32_t c, uint32_t rim, int shine)
	{
		cv.RoundRect(x, y, w, h, r, Canvas::Mix(c, 0xFFFFFF, 0.45f), c, 255);
		cv.RoundFrame(x, y, w, h, r, rim, 255);
		if (shine) cv.RoundRect(x + w / 5, y + 1, w - 2 * (w / 5), h * 2 / 5, std::min(r, h / 5), 0xFFFFFF, 0xFFFFFF, shine);
	}

	// Mac OS X 10.0-10.6 (Aqua): pinstripes, glossy gel lights and pill buttons, blue gel for the picked one.
	struct Aqua : ModernMac
	{
		Aqua()
		{
			name = L"Mac OS X 10.0-10.6";
			page = 0xF2F2F2, strip = 0xE8E8E8, heading = 0x000000;
			status[0] = status[1] = 0xE8E8E8;
			nav[0] = 0xE8EDF5, nav[1] = 0xD6DDE5, navSel[0] = 0x6AA2E8, navSel[1] = navSel[2] = 0x3B7DD8, navSelInk = 0xFFFFFF;
			boxOn = 0x3D8EF0, radius = 6, boxRadius = 3;
			capW[0] = capW[1] = capW[2] = 14, capH = 14, capTop = 6, capGap = 7, capInset = 10;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xECECEC, 0xECECEC, 255, true);
			for (int y = r; y < cv.h - 1; y += 2) Line(cv, 1, y, cv.w - 2, 1, 0xF8F8F8); // the pinstripes
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 110, true);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x000000, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xA0A0A0, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xE8A92B, 0x5DB33B, 0xD9332A }, rim[] = { 0x9A6A10, 0x2F7A1C, 0x8E1A14 };
			int d = rc.right - rc.left;
			Gel(cv, rc.left, rc.top, d, d, d / 2, fill[kind], rim[kind], 200);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2 + 1, kind, 0x4A1A00, S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			bool on = state == 2;
			Gel(cv, x, y, w, h, h / 2, on ? 0x3D8EF0 : state == 1 ? 0xE6E6E6 : 0xDADADA, on ? 0x1A4D9A : 0x8A8A8A, 150);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(4), 0xF8F8F8, 0xF8F8F8, 240);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(4), 0x000000, 80);
		}
	};

	// OS X 10.7-10.9: a grey gradient title bar, softer lights, white gradient buttons, blue gradient when picked.
	struct Lion : ModernMac
	{
		Lion()
		{
			name = L"OS X 10.7-10.9";
			page = 0xEDEDED, strip = 0xE3E3E3, heading = 0x333333;
			nav[0] = 0xE6EBF0, nav[1] = 0xDCE2E8, navSel[0] = 0x6CB3FA, navSel[1] = navSel[2] = 0x2A7BE0, navSelInk = 0xFFFFFF;
			box[0] = 0xFFFFFF, box[1] = 0xE8E8E8, box[2] = 0x8E8E8E, boxOn = 0, mark = 0x000000, dot[0] = 0x6CB3FA, dot[1] = 0x0D7BF2;
			radius = 4, boxRadius = 3;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(5);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xEDEDED, 0xEDEDED, 255, true);
			cv.RoundRect(0, 0, cv.w, titleH, r, 0xEBEBEB, 0xD5D5D5, 255, true);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 100, true);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xAAAAAA, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFDBC40, 0x34C84A, 0xFC605C }, rim[] = { 0xDE9F34, 0x27AA35, 0xDE4A47 };
			int d = rc.right - rc.left;
			Gel(cv, rc.left, rc.top, d, d, d / 2, fill[kind], rim[kind], 70);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, 0x5A2A00, S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xFFFFFF, 0xFFFFFF, 0x6CB3FA }, bottom[] = { 0xF1F1F1, 0xE6E6E6, 0x0D7BF2 }, edge[] = { 0xA6A6A6, 0x8E8E8E, 0x2A68C8 };
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
		}
	};

	// OS X 10.10-10.15: flat, a plain light title bar with a regular-weight title, flat lights, flat blue when picked.
	struct Yosemite : ModernMac
	{
		Yosemite()
		{
			name = L"OS X 10.10-10.15";
			radius = 5, boxRadius = 3;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(5);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xECECEC, 0xECECEC, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 70);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xD0D0D0, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			uint32_t fill = state == 2 ? 0x1E8BFF : state == 1 ? 0xF7F7F7 : 0xFFFFFF;
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, fill, fill, 255);
			cv.RoundFrame(x, y, w, h, r, state == 2 ? 0x0A70F0 : 0xC8C8C8, 255);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(5), 0xF2F2F2, 0xF2F2F2, 245);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(5), 0x000000, 45);
		}
	};

	// macOS 26 (Liquid Glass): real glass. The window is kept out of screen captures; every display refresh the
	// screen behind it is grabbed and lightly blurred, and the glass shapes show it bent like a lens towards their
	// rounded edges with a little colour split, a specular rim lit from the top left (and its bounce bottom right).
	// The frame and the floating sidebar are glass; capsule tabs and buttons catch the light. Where each glass pixel
	// samples from is worked out once per paint (Bake), so a refresh is only the grab and a gather.
	struct Glass : ModernMac
	{

		Glass()
		{
			name = L"macOS 26";
			page = 0xFFFFFF, heading = 0x86868B, navInk = 0x1D1D1F, edge = 0xE3E3E8, groupEdge = 0xE8E8ED, stripAlpha = 0;
			navSel[0] = navSel[1] = 0xFFFFFF, navHot[0] = navHot[1] = 0xFFFFFF;
			box[2] = 0xC7C7CC, boxOn = 0x0088FF, radius = 18, boxRadius = 5;
			capW[0] = capW[1] = capW[2] = 14, capH = 14, capTop = 9, capGap = 9, capInset = 18;
		}

		// Apple's "regular" Liquid Glass (HIG, Materials): the backdrop is blurred and its luminosity pulled into a
		// band labels read over, and the whole look turns dark (white labels) over dark content.
		std::atomic<bool> dark{ false };
		bool Live() const override { return true; }
		uint32_t TabInk(bool on) const override { (void)on; return dark ? 0xF5F5F7 : 0x1D1D1F; }
		bool Prepare(Backdrop &bd, HWND hwnd, int w, int h) override
		{
			RECT wr;
			GetWindowRect(hwnd, &wr);
			int m = S(24); // room for the lens to reach past the edge
			int bw = w + 2 * m, bh = h + 2 * m;
			int hw = bw / 2, hh = bh / 2;
			auto &half = bd.small;
			half.resize(size_t(hw) * hh);
#ifdef __APPLE__
			bd.grab.window = hwnd;
#endif
			if (!bd.grab.Grab(wr.left - m, wr.top - m, bw, bh, half.data()))
			{
				auto &backdrop = bd.px;
				backdrop.resize(size_t(bw) * bh);
				// no desktop duplication (remote desktop, old driver): GDI, much slower
				HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
				BITMAPINFO bi{};
				bi.bmiHeader = { sizeof(bi.bmiHeader), bw, -bh, 1, 32, BI_RGB };
				void *bits;
				auto dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
				auto old = SelectObject(dc, dib);
				BitBlt(dc, 0, 0, bw, bh, screen, wr.left - m, wr.top - m, SRCCOPY);
				memcpy(backdrop.data(), bits, backdrop.size() * 4);
				SelectObject(dc, old);
				DeleteObject(dib);
				DeleteDC(dc);
				ReleaseDC(nullptr, screen);
				for (int j = 0; j < hh; j++)
					for (int i = 0; i < hw; i++)
					{
						int sum[3] = {};
						for (int y = 0; y < 2; y++)
							for (int x = 0; x < 2; x++)
							{
								uint32_t p = backdrop[size_t(2 * j + y) * bw + 2 * i + x];
								sum[0] += p & 0xFF, sum[1] += p >> 8 & 0xFF, sum[2] += p >> 16 & 0xFF;
							}
						half[size_t(j) * hw + i] = uint32_t(sum[0] / 4) | uint32_t(sum[1] / 4) << 8 | uint32_t(sum[2] / 4) << 16;
					}
			}
			// two box blurs each way (about a gaussian): shapes show through, fine detail like text goes soft
			for (int pass = 0; pass < 2; pass++) Blur(half, bd.scratch, hw, hh, std::max(1, S(2)));
			// light or dark: the mean luma of what's seen through the glass, with some hysteresis
			// ponytail: one look per window; macOS decides per bar
			int64_t sum = 0, n = 0;
			for (uint32_t i : bd.probe)
				if (i < half.size()) sum += Luma(half[i]), n++;
			if (!n) return false;
			int mean = int(sum / n);
			bool was = dark;
			if (mean < 100) dark = true;
			else if (mean > 125) dark = false;
			return dark != was;
		}
		static int Luma(uint32_t c) { return int((c >> 16 & 0xFF) * 54 + (c >> 8 & 0xFF) * 183 + (c & 0xFF) * 19) >> 8; }

		// A box blur of radius r, w by h, both ways; tmp is scratch. Running sums, rows only (cache friendly).
		static void Blur(std::vector<uint32_t> &px, std::vector<uint32_t> &tmp, int w, int h, int r)
		{
			tmp.resize(px.size());
			uint32_t inv = 65536 / (2 * r + 1);
			auto pack = [inv](const int *s) { return (uint32_t(s[0]) * inv >> 16) | (uint32_t(s[1]) * inv >> 16) << 8 | (uint32_t(s[2]) * inv >> 16) << 16; };
			auto add = [](int *s, uint32_t c, int k) { s[0] += k * int(c & 0xFF), s[1] += k * int(c >> 8 & 0xFF), s[2] += k * int(c >> 16 & 0xFF); };
			for (int y = 0; y < h; y++) // across: px to tmp
			{
				const uint32_t *row = &px[size_t(y) * w];
				uint32_t *out = &tmp[size_t(y) * w];
				int s[3] = {};
				for (int i = -r; i <= r; i++) add(s, row[std::clamp(i, 0, w - 1)], 1);
				for (int x = 0; x < w; x++)
				{
					out[x] = pack(s);
					add(s, row[std::min(x + r + 1, w - 1)], 1), add(s, row[std::max(x - r, 0)], -1);
				}
			}
			std::vector<int> s(size_t(w) * 3); // down: tmp to px, a running sum per column
			for (int i = -r; i <= r; i++)
				for (int x = 0; x < w; x++) add(&s[3 * x], tmp[size_t(std::clamp(i, 0, h - 1)) * w + x], 1);
			for (int y = 0; y < h; y++)
			{
				const uint32_t *in = &tmp[size_t(std::min(y + r + 1, h - 1)) * w], *outgoing = &tmp[size_t(std::max(y - r, 0)) * w];
				uint32_t *out = &px[size_t(y) * w];
				for (int x = 0; x < w; x++)
				{
					out[x] = pack(&s[3 * x]);
					add(&s[3 * x], in[x], 1), add(&s[3 * x], outgoing[x], -1);
				}
			}
		}

		// Calls f(x, y, cover, nx, ny, depth) for each canvas pixel inside the shape: coverage, the outward normal
		// and the distance in from the rounded edge.
		template<class F> static void Walk(const Shape &s, int cw, int ch, F f)
		{
			float hw = s.w / 2.f, hh = s.h / 2.f;
			for (int j = std::max(0, -s.y); j < s.h && s.y + j < ch; j++)
				for (int i = std::max(0, -s.x); i < s.w && s.x + i < cw; i++)
				{
					float px = i + 0.5f - hw, py = j + 0.5f - hh;
					float qx = std::abs(px) - (hw - s.r), qy = std::abs(py) - (hh - s.r);
					float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f), ol = std::hypot(ox, oy);
					float dist = ol + std::min(std::max(qx, qy), 0.f) - s.r;
					float cover = std::clamp(0.5f - dist, 0.f, 1.f);
					if (cover <= 0) continue;
					float nx, ny;
					if (ol > 0) nx = ox / ol, ny = oy / ol;
					else if (qx > qy) nx = 1, ny = 0;
					else nx = 0, ny = 1;
					f(s.x + i, s.y + j, cover, px < 0 ? -nx : nx, py < 0 ? -ny : ny, -dist);
				}
		}

		// A glass rounded rectangle: frost (0-1) mixes in the tint, lens is how far the edge bends the view. While
		// painting it only cuts a hole in the UI; Underlay fills it every refresh.
		void GlassRect(Canvas &cv, int x, int y, int w, int h, int r, float frost, uint32_t tint, float lens)
		{
			shapes.push_back({ x, y, w, h, r, frost, tint, lens });
			Walk(shapes.back(), cv.w, cv.h, [&](int px, int py, float cover, float, float, float) {
				auto &p = cv.px[size_t(py) * cv.w + px];
				uint32_t keep = uint32_t(256 * (1 - cover));
				p = ((p & 0xFF00FF) * keep >> 8 & 0xFF00FF) | ((p >> 8 & 0xFF00FF) * keep & 0xFF00FF00);
			});
		}

		void Bake(Backdrop &bd, const Canvas &ui, std::vector<Baked> &out) override
		{
			int m = S(24), hw = (ui.w + 2 * m) / 2, hh = (ui.h + 2 * m) / 2;
			float band = float(S(26));
			auto at = [&](float x, float y) { // a pixel of the half-size backdrop
				return uint32_t(std::clamp(int(y + m) / 2, 0, hh - 1) * hw + std::clamp(int(x + m) / 2, 0, hw - 1));
			};
			for (auto &s : shapes)
				Walk(s, ui.w, ui.h, [&](int x, int y, float cover, float nx, float ny, float depth) {
					uint32_t idx = uint32_t(y) * ui.w + x;
					if (ui.px[idx] >> 24 == 255) return; // under solid UI: nothing to see
					float k = depth < band ? 1 - depth / band : 0;
					float bend = s.lens * (1 - std::sqrt(std::max(0.f, 1 - k * k))); // a dome: flat inside, steep at the rim
					// specular: a thin bright rim where the light (top left) or its bounce (bottom right) hits, a soft
					// glow just inside it, and a faint darker band where the lens turns
					float rim = std::clamp(1 - depth / S(1.8f), 0.f, 1.f), light = std::abs(-0.6f * nx - 0.8f * ny);
					float glow = std::clamp(1 - depth / S(7), 0.f, 1.f);
					float dark = 0.10f * k * (1 - k) * 4 * (1 - light), white = std::min(1.f, rim * (0.35f + 0.65f * light) + 0.3f * glow * glow * light);
					out.push_back({ idx,
						{ at(x - nx * bend * 1.18f, y - ny * bend * 1.18f), at(x - nx * bend, y - ny * bend), at(x - nx * bend * 0.82f, y - ny * bend * 0.82f) }, // red bends most, blue least
						s.tint, uint8_t(255 * cover), uint8_t(255 * s.frost), uint8_t(255 * dark), uint8_t(255 * white) });
				});
			bd.probe.clear();
			for (size_t i = 0; i < out.size(); i += 7) bd.probe.push_back(out[i].src[1]);
		}

		void Underlay(Backdrop &bd, Canvas &cv, const Canvas &ui, const std::vector<Baked> &baked) override
		{
			auto &half = bd.small, &scratch = bd.scratch;
			cv.w = ui.w, cv.h = ui.h, cv.px = ui.px;
			scratch.resize(cv.px.size());
			auto mix = [](uint32_t a, uint32_t b, uint32_t t) { // t of 255 toward b
				uint32_t o = 0;
				for (int sh = 0; sh < 24; sh += 8) o |= ((a >> sh & 0xFF) * (255 - t) + (b >> sh & 0xFF) * t) / 255 << sh;
				return o;
			};
			bool isDark = dark;
			for (auto &b : baked) scratch[b.idx] = 0;
			for (auto &b : baked) // the glass, shapes in paint order (premultiplied)
			{
				uint32_t c = (half[b.src[0]] & 0xFF0000) | (half[b.src[1]] & 0xFF00) | (half[b.src[2]] & 0xFF);
				// luminosity into a band: 43-100% light, 8-73% dark; hue and saturation kept
				int y = Luma(c), d = (isDark ? 20 : 110) + y * 165 / 255 - y;
				uint32_t lum = 0;
				for (int sh = 0; sh < 24; sh += 8) lum |= uint32_t(std::clamp(int(c >> sh & 0xFF) + d, 0, 255)) << sh;
				c = mix(mix(mix(lum, isDark ? 0x2C2C2E : b.tint, b.frost), 0, b.dark), 0xFFFFFF, b.white);
				uint32_t &g = scratch[b.idx], keep = 255 - b.cover, o = 0;
				for (int sh = 0; sh < 24; sh += 8) o |= ((c >> sh & 0xFF) * b.cover + (g >> sh & 0xFF) * keep) / 255 << sh;
				g = o | (b.cover + (g >> 24) * keep / 255) << 24;
			}
			for (auto &b : baked) // the UI over it
			{
				uint32_t u = ui.px[b.idx], g = scratch[b.idx], keep = 255 - (u >> 24), o = 0;
				for (int sh = 0; sh < 32; sh += 8) o |= ((u >> sh & 0xFF) + (g >> sh & 0xFF) * keep / 255) << sh;
				cv.px[b.idx] = o;
			}
		}

		// A capsule or panel over the page: a milky fill, a white rim brightest at the top, a faint dark outline.
		static void Pane(Canvas &cv, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int a)
		{
			cv.RoundRect(x, y, w, h, r, top, bottom, a);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 28);
			cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, std::max(0, r - 1), 0xFFFFFF, 200);
			cv.RoundRect(x + r, y + 1, w - 2 * r, 1, 0, 0xFFFFFF, 0xFFFFFF, 255); // the top glint
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			navInk = TabInk(false);
			GlassRect(cv, 0, 0, cv.w, cv.h, S(18), 0.04f, 0xFAFAFC, float(S(24)));
			cv.RoundFrame(0, 0, cv.w, cv.h, S(18), 0x000000, 40);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(100), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, TabInk(false), true, cv.w - S(200));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFFBD2E, 0x28C941, 0xFF5F57 }, ink[] = { 0x985712, 0x0B650D, 0x4D0000 };
			int d = rc.right - rc.left;
			cv.RoundRect(rc.left, rc.top, d, d, d / 2, Canvas::Mix(fill[kind], 0xFFFFFF, 0.15f), fill[kind], 255);
			cv.RoundFrame(rc.left, rc.top, d, d, d / 2, 0x000000, 35);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, ink[kind], S(6), std::max(1, S(1.3f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (state == 2) Pane(cv, x, y, w, h, h / 2, 0x3DA2FF, 0x0077F0, 255);
			else Pane(cv, x, y, w, h, h / 2, state == 1 ? 0xFFFFFF : 0xFBFBFD, state == 1 ? 0xF0F0F5 : 0xF2F2F6, 235);
		}

		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top - S(4);
			if (on && dark) Pane(cv, rc.left, rc.top + S(1), w, h, h / 2, 0x6C6C70, 0x5A5A5E, 230);
			else if (on) Pane(cv, rc.left, rc.top + S(1), w, h, h / 2, 0xFFFFFF, 0xF4F4F8, 230);
			else if (hot) cv.RoundRect(rc.left, rc.top + S(1), w, h, h / 2, 0xFFFFFF, 0xFFFFFF, dark ? 40 : 110);
		}

		void NavPane(Canvas &cv, int x, int y, int w, int h) override
		{
			cv.RoundRect(x, y, w + 1, h, 0, page, page, 255);
			GlassRect(cv, x + S(6), y + S(6), w - S(10), h - S(12), S(14), 0.3f, 0xF4F4F7, float(S(20)));
		}

		void NavItem(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (state == 0) return;
			cv.RoundRect(x + S(4), y, w - S(8), h, S(8), 0xFFFFFF, 0xFFFFFF, dark ? (state == 2 ? 70 : 35) : (state == 2 ? 200 : 110));
		}

		void Popup(Canvas &cv) override
		{
			GlassRect(cv, 0, 0, cv.w, cv.h, S(16), 0.3f, 0xFAFAFC, float(S(20)));
			cv.RoundFrame(0, 0, cv.w, cv.h, S(16), 0x000000, 40);
		}
	};

	Three threeTheme;
	NinetyFive ninetyFiveTheme;
	NinetyEight ninetyEightTheme;
	XP xpTheme, xpOliveTheme{ 1 }, xpSilverTheme{ 2 }, xpRoyaleTheme{ 3 }, xpNoirTheme{ 4 }, xpZuneTheme{ 5 };
	Skinned<Vista> vistaTheme{ { skin_vista_png.AsCharSpan(), skin_vista_parts } };
	Skinned<Seven> sevenTheme{ { skin_seven_png.AsCharSpan(), skin_seven_parts } };
	Skinned<Eight> eightTheme{ { skin_eight_png.AsCharSpan(), skin_eight_parts } };
	Skinned<TenLight> tenTheme{ { skin_ten_png.AsCharSpan(), skin_ten_parts } };
	Native<Eleven> elevenTheme;
	Skinned<ClassicMac> classicMacTheme{ { skin_mac9_png.AsCharSpan(), skin_mac9_parts } };
	Skinned<MacLight> modernMacTheme{ { skin_macos_png.AsCharSpan(), skin_macos_parts } };
	Temple templeTheme;
	Skinned<System6> system6Theme{ { skin_system6_png.AsCharSpan(), skin_system6_parts } };
	Skinned<System7> system7Theme{ { skin_system7_png.AsCharSpan(), skin_system7_parts } };
	Skinned<Aqua> aquaTheme{ { skin_aqua_png.AsCharSpan(), skin_aqua_parts } };
	Skinned<Lion> lionTheme{ { skin_lion_png.AsCharSpan(), skin_lion_parts } };
	Skinned<Yosemite> yosemiteTheme{ { skin_yosemite_png.AsCharSpan(), skin_yosemite_parts } };
	Skinned<TenDark> tenDarkTheme{ { skin_tendark_png.AsCharSpan(), skin_tendark_parts } };
	Skinned<MacDark> macDarkTheme{ { skin_macosdark_png.AsCharSpan(), skin_macosdark_parts } };
	Glass glassTheme;
	Theme *const themes[] = { &threeTheme, &ninetyFiveTheme, &ninetyEightTheme, &xpTheme, &vistaTheme, &sevenTheme, &eightTheme, &tenTheme, &elevenTheme, &classicMacTheme, &modernMacTheme, &templeTheme,
		&system6Theme, &system7Theme, &aquaTheme, &lionTheme, &yosemiteTheme, &glassTheme, &xpOliveTheme, &xpSilverTheme, &xpRoyaleTheme, &xpNoirTheme, &xpZuneTheme, &tenDarkTheme, &macDarkTheme }; // new ones at the end: the pref is the index
	const std::vector<int> windowsThemes = { 0, 1, 2, 3, 4, 5, 6, 7, 8 }, macThemes = { 12, 13, 9, 14, 15, 16, 10, 17 };
	constexpr int templeIndex = 11;
	// A generation's colour schemes, the first the one its picker shows (Windows XP: Blue, Olive Green, Silver, Royale, Royale Noir, Zune)
	const std::vector<std::vector<int>> themeColours = { { 3, 18, 19, 20, 21, 22 }, { 7, 23 }, { 10, 24 } };
	int Generation(int i)
	{
		for (auto &c : themeColours)
			if (std::count(c.begin(), c.end(), i)) return c[0];
		return i;
	}
	constexpr int defaultTheme = 4; // Vista
	Theme *theme = &vistaTheme;

	// A top-level layered window that repaints its Canvas whenever Invalidate() is called.
	struct Panel
	{
		HWND hwnd = nullptr;
		Canvas cv;
		ByteString name;
		std::wstring title;
		std::atomic<bool> dirty{ true }; // the vsync thread sets it too
		bool excluded = false; // a live (glass) theme: kept out of screen captures, recomposed every display refresh
		// Live: the last paint (with holes where the glass goes) and its baked glass, what's behind, and the composed
		// frame. A thread recomposes and shows it every display refresh (DwmFlush), whatever TPT's own loop is doing;
		// liveLock keeps a repaint from swapping ui and baked under it.
		Canvas ui, out;
		std::vector<Theme::Baked> baked;
		Theme::Backdrop backdrop;
		std::mutex liveLock;
		std::thread vsync;
		std::atomic<bool> vsyncRun{ false };

		void StartVsync()
		{
			vsyncRun = true;
			vsync = std::thread([this] {
				SetThreadDescription(GetCurrentThread(), L"overlay side window vsync");
#ifdef _WIN32
				// the vertical blank of the screen the window is on; DwmFlush if there's none
				// ponytail: picked once, so a window dragged to a screen with another refresh rate keeps the first's
				IDXGIOutput *screen = nullptr;
				RECT wr;
				GetWindowRect(hwnd, &wr);
				POINT c{ (wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2 };
				IDXGIFactory1 *factory;
				if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)))
				{
					IDXGIAdapter1 *adapter;
					for (UINT i = 0; !screen && factory->EnumAdapters1(i, &adapter) == S_OK; i++)
					{
						IDXGIOutput *output;
						for (UINT j = 0; !screen && adapter->EnumOutputs(j, &output) == S_OK; j++)
						{
							DXGI_OUTPUT_DESC d;
							output->GetDesc(&d);
							if (PtInRect(&d.DesktopCoordinates, c)) screen = output;
							else output->Release();
						}
						adapter->Release();
					}
					factory->Release();
				}
				while (vsyncRun)
				{
					auto t0 = std::chrono::steady_clock::now();
					if (!screen || FAILED(screen->WaitForVBlank())) DwmFlush();
					if (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1)) Sleep(4); // no vblank to wait for
					Compose();
				}
				if (screen) screen->Release();
#else
				while (vsyncRun)
				{
					usleep(16000);
					Compose();
				}
#endif
			});
		}

		void Compose()
		{
			std::lock_guard lock(liveLock);
			if (!theme->Live() || ui.px.empty()) return; // switching away: the next Present stops this
			if (theme->Prepare(backdrop, hwnd, ui.w, ui.h)) dirty = true;
			theme->Underlay(backdrop, out, ui, baked);
			Show(out);
		}

		void StopVsync()
		{
			vsyncRun = false;
			if (vsync.joinable()) vsync.join();
		}
		int opacity = 255;
		// An app window: taskbar button, normal z-order, resizable, minimise/maximise. Otherwise a small on-top popup
		// (notifications).
		bool appWindow = false;
		int grip = 0; // resize border width
		Vec2<int> minSize{ 0, 0 };
		int drag = 0; // the frame part (HTCAPTION, HTLEFT...) being dragged, 0 = none
		POINT dragFrom{};
		RECT dragRect{};
		HWND owner = nullptr; // an owned window: no taskbar button, normal z-order, minimises with its owner

		virtual ~Panel()
		{
			StopVsync();
			if (hwnd)
			{
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
				DestroyWindow(hwnd);
			}
		}
		virtual void Paint() = 0;
		virtual LRESULT HitTest(int, int) { return HTCLIENT; }
		virtual void Mouse(UINT, int, int, WPARAM) {}
		// WM_KEYDOWN, WM_CHAR, WM_KILLFOCUS; true = handled. Only while a panel has taken the keyboard (see
		// SideWindow::search).
		virtual bool Key(UINT, WPARAM) { return false; }
		virtual void Drop(const std::wstring &path) { (void)path; } // a file dropped on an app window

		void Create(int x, int y, int w, int h)
		{
			static bool registered = false;
			if (!registered)
			{
				WNDCLASSW wc{};
				wc.lpfnWndProc = Proc;
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				wc.lpszClassName = L"TPTOverlayPanel";
				RegisterClassW(&wc);
				registered = true;
			}
			cv.Resize(w, h);
			// NOACTIVATE either way: the keyboard stays with TPT. ponytail: windows with text fields will need focus.
			DWORD ex = WS_EX_LAYERED | WS_EX_NOACTIVATE | (appWindow ? WS_EX_APPWINDOW : owner ? 0 : WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
			DWORD style = WS_POPUP | (appWindow ? WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_THICKFRAME : 0);
			hwnd = CreateWindowExW(ex, L"TPTOverlayPanel", title.empty() ? Platform::WinWiden(name).c_str() : title.c_str(), style, x, y, w, h,
			                       owner, nullptr, GetModuleHandleW(nullptr), nullptr);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));
			if (appWindow) DragAcceptFiles(hwnd, TRUE);
			BOOL noFade = TRUE;
			DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &noFade, sizeof(noFade));
			// Creation measured the client area before Proc could say "no frame"; measure again, or the canvas
			// comes out smaller than the window.
			SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			if (appWindow)
			{
				wchar_t exe[MAX_PATH];
				GetModuleFileNameW(nullptr, exe, MAX_PATH);
				HICON big = nullptr, small = nullptr;
				ExtractIconExW(exe, 0, &big, &small, 1);
				if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, LPARAM(big));
				if (small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, LPARAM(small));
			}
		}

		void Present()
		{
			if (theme->Live() != excluded)
			{
				excluded = theme->Live();
				// WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+) or WDA_NONE; looked up because the SDK target predates it
				static auto setAffinity = reinterpret_cast<BOOL(WINAPI *)(HWND, DWORD)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity"));
				if (setAffinity) setAffinity(hwnd, excluded ? 0x11 : 0);
				if (!excluded) StopVsync(); // started below, once there's a paint to show
				dirty = true;
			}
			if (dirty)
			{
				theme->shapes.clear();
				std::fill(cv.px.begin(), cv.px.end(), 0);
				Paint();
				dirty = false;
				if (excluded)
				{
					std::lock_guard lock(liveLock);
					ui = cv;
					baked.clear();
					theme->Bake(backdrop, ui, baked);
				}
			}
			if (excluded)
			{
				if (!vsync.joinable()) StartVsync();
				return; // the vsync thread shows it
			}
			Show(cv);
		}

		void Show(const Canvas &cv)
		{
			HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
			BITMAPINFO bi{};
			bi.bmiHeader = { sizeof(bi.bmiHeader), cv.w, -cv.h, 1, 32, BI_RGB };
			void *bits;
			auto dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
			memcpy(bits, cv.px.data(), cv.px.size() * 4);
			auto old = SelectObject(dc, dib);
			POINT origin{ 0, 0 };
			SIZE size{ cv.w, cv.h };
			BLENDFUNCTION blend{ AC_SRC_OVER, 0, BYTE(opacity), AC_SRC_ALPHA };
			UpdateLayeredWindow(hwnd, screen, nullptr, &size, dc, &origin, 0, &blend, ULW_ALPHA);
			SelectObject(dc, old);
			DeleteObject(dib);
			DeleteDC(dc);
			ReleaseDC(nullptr, screen);
		}

		void SavePosition()
		{
			if (IsIconic(hwnd) || IsZoomed(hwnd))
			{
				return;
			}
			RECT rc;
			GetWindowRect(hwnd, &rc);
			auto &prefs = GlobalPrefs::Ref();
			prefs.Set("Overlay.Windows." + name + ".X", int(rc.left));
			prefs.Set("Overlay.Windows." + name + ".Y", int(rc.top));
			prefs.Set("Overlay.Windows." + name + ".W", int(rc.right - rc.left));
			prefs.Set("Overlay.Windows." + name + ".H", int(rc.bottom - rc.top));
		}

		// Resize borders and corners, then the window's own areas.
		LRESULT FrameHitTest(int x, int y)
		{
			if (appWindow && !IsZoomed(hwnd))
			{
				bool l = x < grip, r = x >= cv.w - grip, t = y < grip, b = y >= cv.h - grip;
				if (t && l) return HTTOPLEFT;
				if (t && r) return HTTOPRIGHT;
				if (b && l) return HTBOTTOMLEFT;
				if (b && r) return HTBOTTOMRIGHT;
				if (l) return HTLEFT;
				if (r) return HTRIGHT;
				if (t) return HTTOP;
				if (b) return HTBOTTOM;
			}
			return HitTest(x, y);
		}

		static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
		{
			auto *self = (Panel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
			if (self)
			{
				switch (msg)
				{
				case WM_NCHITTEST:
				{
					POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
					ScreenToClient(hwnd, &p);
					return self->FrameHitTest(p.x, p.y);
				}
				case WM_NCCALCSIZE:
					if (self->appWindow && wp)
					{
						return 0; // we draw the whole window, frame included
					}
					break;
				case WM_SIZE:
					if (wp != SIZE_MINIMIZED && (LOWORD(lp) != self->cv.w || HIWORD(lp) != self->cv.h))
					{
						self->cv.Resize(LOWORD(lp), HIWORD(lp));
						self->dirty = true;
						self->Present();
					}
					return 0;
				case WM_GETMINMAXINFO:
				{
					auto *mm = (MINMAXINFO *)lp;
					mm->ptMinTrackSize = { self->minSize.X, self->minSize.Y };
					// Maximised = the monitor's work area, not over the taskbar.
					MONITORINFO mi{ sizeof(mi) };
					if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
					{
						mm->ptMaxPosition = { mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top };
						mm->ptMaxSize = { mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top };
					}
					return 0;
				}
				case WM_MOUSEACTIVATE:
					if (self->appWindow)
					{
						// Come to the front like any window, without taking the keyboard from TPT.
						SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
					}
					return MA_NOACTIVATE;
				case WM_EXITSIZEMOVE:
					self->SavePosition();
					return 0;
				case WM_NCLBUTTONDOWN:
					// Moved and sized by hand: Windows' own move/size loop runs on TPT's thread and holds the sim still
					// until the button comes up. ponytail: no drag-to-top maximise or Snap layouts that way
					if ((wp == HTCAPTION || (wp >= HTLEFT && wp <= HTBOTTOMRIGHT)) && !IsZoomed(hwnd))
					{
						self->drag = int(wp);
						GetCursorPos(&self->dragFrom);
						GetWindowRect(hwnd, &self->dragRect);
						SetCapture(hwnd);
						return 0;
					}
					break;
				case WM_CAPTURECHANGED:
					if (self->drag)
					{
						self->drag = 0;
						self->SavePosition();
					}
					break;
				case WM_MOVING:
					if (self->appWindow)
					{
						// Dock: within a few pixels of a screen edge, the window sticks to it.
						auto *r = (RECT *)lp;
						MONITORINFO mi{ sizeof(mi) };
						GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &mi);
						auto wk = mi.rcWork;
						int snap = S(16), dx = 0, dy = 0;
						if (std::abs(r->left - wk.left) < snap) dx = wk.left - r->left;
						else if (std::abs(r->right - wk.right) < snap) dx = wk.right - r->right;
						if (std::abs(r->top - wk.top) < snap) dy = wk.top - r->top;
						else if (std::abs(r->bottom - wk.bottom) < snap) dy = wk.bottom - r->bottom;
						OffsetRect(r, dx, dy);
						return TRUE;
					}
					break;
				case WM_MOUSEMOVE:
				{
					if (int d = self->drag)
					{
						POINT p;
						GetCursorPos(&p);
						int dx = p.x - self->dragFrom.x, dy = p.y - self->dragFrom.y;
						RECT r = self->dragRect;
						auto min = self->minSize;
						if (d == HTCAPTION) OffsetRect(&r, dx, dy);
						if (d == HTLEFT || d == HTTOPLEFT || d == HTBOTTOMLEFT) r.left = std::min(r.left + dx, r.right - min.X);
						if (d == HTRIGHT || d == HTTOPRIGHT || d == HTBOTTOMRIGHT) r.right = std::max(r.right + dx, r.left + min.X);
						if (d == HTTOP || d == HTTOPLEFT || d == HTTOPRIGHT) r.top = std::min(r.top + dy, r.bottom - min.Y);
						if (d == HTBOTTOM || d == HTBOTTOMLEFT || d == HTBOTTOMRIGHT) r.bottom = std::max(r.bottom + dy, r.top + min.Y);
						if (d == HTCAPTION) SendMessageW(hwnd, WM_MOVING, 0, LPARAM(&r)); // the screen-edge dock
						SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
						return 0;
					}
					TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
					TrackMouseEvent(&tme);
					self->Mouse(msg, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
					return 0;
				}
				case WM_KEYDOWN:
				case WM_CHAR:
				case WM_KILLFOCUS:
					if (self->Key(msg, wp)) return 0;
					break;
				case WM_DROPFILES:
				{
					auto drop = HDROP(wp);
					wchar_t path[MAX_PATH];
					if (DragQueryFileW(drop, 0, path, MAX_PATH)) self->Drop(path); // ponytail: the first file only
					DragFinish(drop);
					return 0;
				}
				case WM_LBUTTONUP:
					if (self->drag)
					{
						ReleaseCapture(); // ends the drag (WM_CAPTURECHANGED)
						return 0;
					}
					[[fallthrough]];
				case WM_MOUSELEAVE:
				case WM_LBUTTONDOWN:
				case WM_RBUTTONDOWN:
				case WM_RBUTTONUP:
				case WM_MBUTTONDOWN:
				case WM_MBUTTONUP:
					self->Mouse(msg, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
					return 0;
				case WM_MOUSEWHEEL:
				{
					POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
					ScreenToClient(hwnd, &p);
					self->Mouse(msg, p.x, p.y, wp);
					return 0;
				}
				}
			}
			return DefWindowProcW(hwnd, msg, wp, lp);
		}
	};

	bool Inside(const RECT &rc, int x, int y)
	{
		return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
	}

	// The side window: every TPT menu as a tab. Elements has TPT's menu sections down the left (Explorer's
	// navigation pane) and the section's tools as Vista buttons; left click picks the primary tool, right click the
	// secondary, as in TPT. The other tabs are immediate-mode Vista controls: each paint rebuilds the list of
	// clickable rectangles (hits) that the mouse handler uses.
	void OpenTorn(int tab);
	void ThemeChanged(); // repaint every window
	void SetTheme(int i);
	void Restart(int scale, bool windowed, bool force = false);
	void LoadSceneTiles();

	struct SideWindow : Panel
	{
		struct Hit
		{
			RECT rc;
			std::function<void()> left, right;
			std::wstring tip;
			bool drag = false; // left also runs while the button is held and the mouse moves (sliders)
		};
		std::vector<Hit> hits;
		int hover = -1;
		int tab = 0, section = 0, scroll = 0;
		int fixedTab = -1;    // a tab torn off into its own window: shows only that one
		bool closing = false; // closed: Tick drops it (not from inside its own click)
		int titleH, border, tabH, navW, statusH, tileW, tileH, gap;
		uint64_t shownState = 0;
		// Saves tab: the saves and stamps on this computer, thumbnails rendered one per tick.
		struct Local
		{
			ByteString path, stamp; // stamp: its ID, for a stamp
			std::wstring name;
			std::unique_ptr<VideoBuffer> thumb;
			bool tried = false;
		};
		std::vector<Local> locals;
		// powdertoy.co.uk's saves, a page at a time (the Saves tab's Online saves)
		struct Online
		{
			int id, date, up, down;
			std::wstring name, author;
			std::unique_ptr<http::ThumbnailRequest> req;
			std::unique_ptr<VideoBuffer> thumb;
			bool tried = false;
		};
		std::vector<Online> online;
		std::unique_ptr<http::SearchSavesRequest> searching;
		std::wstring onlineQuery, onlineStatus = L"Loading\u2026";
		int onlineSort = 0, onlinePeriod = 0, onlinePage = 0, onlinePer = 12, onlineTotal = 0; // sort: popular, new, favourites, mine
		bool onlineSearch = true; // search again at the next tick
		DWORD localsAt = 0; // when the folders were last listed
		static constexpr int savesTab = 4;
		std::wstring query; // Elements tab search: tools from every section whose name or description has it
		bool typing = false; // a search box has the keyboard
		std::wstring *typed = &query; // which one: the elements' or the online saves'
		static constexpr const wchar_t *tabNames[] = { L"Elements", L"Tools", L"Simulation", L"Display", L"Saves", L"Overlay", L"Fun", L"Welcome", L"Buddy" };
		static constexpr int toolsTab = 1, welcomeTab = 7, buddyTab = 8; // Buddy: only once his file is chosen
		static constexpr int tabOrder[] = { 0, 1, 2, 3, 4, 5, 6, buddyTab, welcomeTab }; // as shown (Buddy next to Fun)

		SideWindow()
		{
			name = "Palette"; // keeps the saved position of the old Elements window
			title = L"The Powder Toy";
			appWindow = true;
			tab = reopenTab >= 0 ? reopenTab : welcomeTab; // the overlay's start page, in place of TPT's text over the game
			titleH = S(28), border = S(6), tabH = S(24), navW = S(118), statusH = S(22), tileW = S(92), tileH = S(24), gap = S(4);
		}

		std::vector<Menu *> Sections()
		{
			std::vector<Menu *> out;
			for (auto *menu : GameController::Ref().GetMenuList())
			{
				if (menu->GetVisible())
				{
					out.push_back(menu);
				}
			}
			return out;
		}

		std::vector<Tool *> Tools()
		{
			auto sections = Sections();
			std::vector<Tool *> out;
			if (!query.empty())
			{
				auto lower = [](std::wstring s) {
					for (auto &c : s) c = wchar_t(std::towlower(c));
					return s;
				};
				auto q = lower(query);
				std::vector<Tool *> rest; // names starting with the query first, then any other match
				for (auto *menu : sections)
					for (auto *tool : menu->GetToolList())
					{
						auto name = lower(Label(tool));
						if (!tool->MenuVisible) continue;
						if (name.rfind(q, 0) == 0) out.push_back(tool);
						else if (name.find(q) != name.npos || lower(Wide(tool->Description)).find(q) != std::wstring::npos) rest.push_back(tool);
					}
				out.insert(out.end(), rest.begin(), rest.end());
				return out;
			}
			if (section < int(sections.size()))
			{
				for (auto *tool : sections[section]->GetToolList())
				{
					if (tool->MenuVisible)
					{
						out.push_back(tool);
					}
				}
			}
			return out;
		}

		RECT CloseRect() { return theme->CaptionRect(cv.w, border, CaptionClose); }
		RECT MaxRect() { return theme->CaptionRect(cv.w, border, CaptionMax); }
		RECT MinRect() { return theme->CaptionRect(cv.w, border, CaptionMin); }
		// Below the tabs, above the status bar.
		RECT Content()
		{
			return { border, titleH + tabH, cv.w - border, cv.h - border - statusH };
		}
		RECT SearchRect()
		{
			auto c = Content();
			return { c.left + navW + gap, c.top + gap, c.right - gap, c.top + gap + S(23) };
		}
		RECT ToolArea()
		{
			auto c = Content();
			return { c.left + navW + gap, SearchRect().bottom + gap, c.right - gap, c.bottom };
		}

		// The search box takes the keyboard from TPT until Enter, Esc or a click elsewhere.
		void StartTyping(std::wstring *field = nullptr)
		{
			typed = field ? field : &query;
			typing = true;
			SetForegroundWindow(hwnd);
			SetFocus(hwnd);
		}
		void StopTyping()
		{
			typing = false;
			dirty = true;
			if (mainWindow && GetForegroundWindow() == hwnd) SetForegroundWindow(mainWindow);
		}
		bool Key(UINT msg, WPARAM wp) override
		{
			if (!typing) return false;
			if (msg == WM_KILLFOCUS) typing = false;
			else if (msg == WM_KEYDOWN && wp == VK_ESCAPE) typed->clear(), StopTyping();
			else if (msg == WM_KEYDOWN && wp == VK_RETURN && typed == &onlineQuery)
			{
				onlinePage = 0;
				onlineSearch = true;
				StopTyping();
			}
			else if (msg == WM_KEYDOWN && wp == VK_RETURN)
			{
				auto tools = Tools();
				if (!tools.empty()) GameController::Ref().SetActiveTool(0, tools[0]);
				StopTyping();
			}
			else if (msg == WM_CHAR && wp == VK_BACK && !typed->empty()) typed->pop_back();
			else if (msg == WM_CHAR && wp >= 32) *typed += wchar_t(wp);
			else return msg == WM_CHAR;
			if (typed == &query) scroll = 0;
			dirty = true;
			return true;
		}
		int Columns()
		{
			auto a = ToolArea();
			return std::max(1, int((a.right - a.left + gap) / (tileW + gap)));
		}
		RECT TileRect(int i)
		{
			auto a = ToolArea();
			int c = Columns();
			int x = a.left + (i % c) * (tileW + gap), y = a.top + (i / c) * (tileH + gap) - scroll;
			return { x, y, x + tileW, y + tileH };
		}
		RECT NavRect(int i)
		{
			auto c = Content();
			int h = S(22);
			return { c.left + S(3), c.top + S(4) + i * h, c.left + navW - S(3), c.top + S(4) + (i + 1) * h - S(1) };
		}

		bool Hovered(const RECT &rc)
		{
			return hover >= 0 && hover < int(hits.size()) && EqualRect(&hits[hover].rc, &rc);
		}

		RECT Add(RECT rc, std::function<void()> left, std::wstring tip = L"", std::function<void()> right = nullptr)
		{
			hits.push_back({ rc, std::move(left), std::move(right), std::move(tip) });
			return rc;
		}

		// --- controls in the theme's look; each draws itself and registers its hit rectangle.

		void PushButton(int x, int y, int w, const std::wstring &label, std::function<void()> action, const std::wstring &tip = L"", bool pressed = false)
		{
			RECT rc{ x, y, x + w, y + S(23) };
			theme->Button(cv, x, y, w, S(23), pressed ? 2 : (Hovered(rc) ? 1 : 0));
			auto size = text->Measure(label);
			text->Draw(cv, x + std::max(S(3), int(w - size.cx) / 2), y + (S(23) - size.cy) / 2, label, pressed ? theme->pressedInk : theme->ink, false, w - S(6));
			Add(rc, std::move(action), tip);
		}

		// A check box (round = radio button) with its label; returns the width used.
		int Toggle(int x, int y, const std::wstring &label, bool on, bool round, std::function<void()> action, const std::wstring &tip = L"")
		{
			int b = S(13), w = b + S(6) + text->Measure(label).cx;
			RECT rc{ x, y, x + w, y + S(20) };
			bool hot = Hovered(rc);
			int by = y + (S(20) - b) / 2, r = round ? b / 2 : S(float(theme->boxRadius));
			if (!theme->CheckBox(cv, x, by, b, on, round, hot))
			{
				auto *c = hot ? theme->boxHot : theme->box;
				bool filled = on && theme->boxOn;
				cv.RoundRect(x, by, b, b, r, filled ? theme->boxOn : c[0], filled ? theme->boxOn : c[1], 255);
				cv.RoundFrame(x, by, b, b, r, filled ? theme->boxOn : c[2], 255);
				if (on && round)
				{
					int d = b / 2;
					cv.RoundRect(x + (b - d) / 2, by + (b - d) / 2, d, d, d / 2, theme->dot[0], theme->dot[1], 255);
				}
				if (on && !round)
				{
					int cx = x + b / 2, cy = by + b / 2, k = S(2.5f);
					for (int t = 0; t < S(1.6f); t++)
					{
						for (int i = 0; i <= k; i++) cv.Blend(cx - k - 1 + i, cy + i - 1 + t, theme->mark, 255);
						for (int i = 0; i <= 2 * k; i++) cv.Blend(cx - 1 + i, cy + k - 1 - i + t, theme->mark, 255);
					}
				}
			}
			text->Draw(cv, x + b + S(6), y + (S(20) - text->Measure(label).cy) / 2, label, theme->ink);
			Add(rc, std::move(action), tip);
			return w;
		}

		// A group box: an outline with a heading; returns the top of its inside.
		int Group(int x, int y, int w, int h, const std::wstring &title)
		{
			if (!theme->GroupFrame(cv, x, y + S(8), w, h - S(8))) cv.RoundFrame(x, y + S(8), w, h - S(8), S(float(std::min(theme->radius, 6))), theme->groupEdge, 255);
			auto size = text->Measure(title);
			cv.RoundRect(x + S(8), y, size.cx + S(8), size.cy, 0, theme->page, theme->page, 255);
			text->Draw(cv, x + S(12), y, title, theme->heading);
			return y + size.cy + S(4);
		}

		// A group box whose heading folds it: a small triangle before the title, pointing right when folded
		void FoldGroup(int x, int y, int w, int h, const std::wstring &title, bool folded)
		{
			Group(x, y, w, h, L"    " + title);
			int k = S(4), cx = x + S(16), cy = y + text->Measure(title).cy / 2;
			for (int i = 0; i <= k; i++)
				for (int j = -(k - i); j <= k - i; j++)
					cv.Blend(folded ? cx - k / 2 + i : cx + j, folded ? cy + j : cy - k / 2 + i, theme->heading, 255);
		}

		void Caption(int x, int y, const std::wstring &s, uint32_t rgb = 0x000000)
		{
			text->Draw(cv, x, y + (S(20) - text->Measure(s).cy) / 2, s, rgb);
		}

		// A track with a knob for v in [lo, hi]; a click on the track puts it there.
		void Slider(int x, int y, int w, float v, float lo, float hi, std::function<void(float)> set, const std::wstring &tip = L"")
		{
			RECT rc{ x, y, x + w, y + S(20) };
			bool hot = Hovered(rc);
			int kw = S(9), mid = y + S(10), tx = x + int((w - kw) * std::clamp((v - lo) / (hi - lo), 0.f, 1.f));
			cv.RoundRect(x, mid - S(2), w, S(4), S(2), 0xE7EAEA, 0xE7EAEA, 255);
			cv.RoundFrame(x, mid - S(2), w, S(4), S(2), 0xB0B0B0, 255);
			cv.RoundRect(x, mid - S(2), tx - x + kw / 2, S(4), S(2), theme->boxOn ? theme->boxOn : 0x3C7FB1, theme->boxOn ? theme->boxOn : 0x3C7FB1, 255);
			cv.RoundRect(tx, y + S(2), kw, S(16), S(2), hot ? 0xEAF6FD : 0xF2F2F2, hot ? 0xA7D9F5 : 0xD5D5D5, 255);
			cv.RoundFrame(tx, y + S(2), kw, S(16), S(2), hot ? 0x3C7FB1 : 0x707070, 255);
			Add(rc, [this, x, w, kw, lo, hi, set] {
				POINT p;
				GetCursorPos(&p);
				ScreenToClient(hwnd, &p);
				set(lo + (hi - lo) * std::clamp(float(p.x - x - kw / 2) / (w - kw), 0.f, 1.f));
			}, tip);
			hits.back().drag = true;
		}

		// --- tabs

		void PaintElements()
		{
			auto c = Content();
			theme->NavPane(cv, c.left, c.top, navW, c.bottom - c.top);
			auto sections = Sections();
			for (int i = 0; i < int(sections.size()); i++)
			{
				auto rc = NavRect(i);
				bool on = i == section && query.empty(); // a search spans every section
				theme->NavItem(cv, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, on ? 2 : (Hovered(rc) ? 1 : 0));
				text->Draw(cv, rc.left + S(6), rc.top + S(3), Wide(sections[i]->GetDescription()), on && theme->navSelInk != 0xFFFFFFFF ? theme->navSelInk : theme->navInk, on, rc.right - rc.left - S(10));
				Add(rc, [this, i] { section = i, scroll = 0, query.clear(); });
			}
			auto sr = SearchRect();
			int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
			cv.RoundRect(sr.left, sr.top, sw, sh, S(3), 0xFFFFFF, 0xFFFFFF, 255);
			bool here = typing && typed == &query;
			cv.RoundFrame(sr.left, sr.top, sw, sh, S(3), here ? 0x3C7FB1 : (Hovered(sr) ? 0x7EB4EA : 0xABADB3), 255);
			auto shown = query.empty() && !here ? std::wstring(L"Search elements") : query;
			auto qs = text->Measure(shown.empty() ? std::wstring(L" ") : shown);
			int ty = sr.top + (sh - qs.cy) / 2;
			text->Draw(cv, sr.left + S(6), ty, shown, query.empty() ? 0x6D6D6D : 0x000000, false, sw - S(12));
			if (here) Line(cv, sr.left + S(6) + (query.empty() ? 0 : std::min(int(qs.cx), sw - S(12))), ty, std::max(1, S(1)), qs.cy, 0x000000); // the caret
			Add(sr, [this] { StartTyping(); }, L"Type to find an element by name or description; Enter picks the first, Esc clears");
			auto tools = Tools();
			auto area = ToolArea();
			auto &gc = GameController::Ref();
			auto *primary = gc.GetActiveTool(0), *secondary = gc.GetActiveTool(1);
			for (int i = 0; i < int(tools.size()); i++)
			{
				auto rc = TileRect(i);
				if (rc.bottom <= area.top || rc.top >= area.bottom)
				{
					continue;
				}
				auto *tool = tools[i];
				bool selected = tool == primary || tool == secondary;
				Canvas tile;
				tile.Resize(tileW, tileH);
				theme->Button(tile, 0, 0, tileW, tileH, selected ? 2 : (Hovered(rc) ? 1 : 0));
				if (tool == primary) tile.RoundFrame(0, 0, tileW, tileH, S(3), 0xD02020, 255);
				if (tool == secondary) tile.RoundFrame(1, 1, tileW - 2, tileH - 2, S(3), 0x2050D0, 255);
				auto col = tool->Colour;
				uint32_t rgb = (col.Red << 16) | (col.Green << 8) | col.Blue;
				int sw = tileH - S(10);
				// TPT's own icon (walls, tools, some elements), scaled up; otherwise a colour swatch.
				if (auto icon = tool->GetTexture(Vec2(26, 14)))
				{
					int iw = S(26), ih = S(14), iy = (tileH - ih) / 2;
					auto size = icon->Size();
					for (int y = 0; y < ih; y++)
						for (int x = 0; x < iw; x++) tile.Blend(S(5) + x, iy + y, icon->Data()[(y * size.Y / ih) * size.X + x * size.X / iw] & 0xFFFFFF, 255);
					tile.RoundFrame(S(5) - 1, iy - 1, iw + 2, ih + 2, 0, 0x404040, 200);
					sw = iw;
				}
				else
				{
					tile.RoundRect(S(5), S(5), sw, sw, S(2), rgb, rgb, 255);
					tile.RoundFrame(S(5), S(5), sw, sw, S(2), 0x404040, 200);
				}
				text->Draw(tile, S(9) + sw, (tileH - text->Measure(Label(tool)).cy) / 2, Label(tool), selected ? theme->pressedInk : theme->ink, false, tileW - sw - S(12));
				// Copy the tile in, clipped to the tool area.
				for (int y = 0; y < tileH; y++)
				{
					int dy = rc.top + y;
					if (dy < area.top || dy >= area.bottom) continue;
					for (int x = 0; x < tileW; x++)
					{
						auto p = tile.px[y * tileW + x];
						cv.Blend(rc.left + x, dy, p & 0xFFFFFF, p >> 24);
					}
				}
				RECT visible{ rc.left, std::max(rc.top, area.top), rc.right, std::min(rc.bottom, area.bottom) };
				Add(visible, [tool] { GameController::Ref().SetActiveTool(0, tool); }, Wide(tool->Description),
				    [tool] { GameController::Ref().SetActiveTool(1, tool); });
			}
		}

		void PaintSimulation()
		{
			auto c = Content();
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			auto *sim = model->GetSimulation();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;

			// The playable area, in cells of 4 pixels.
			int top = Group(x, y, w, S(118), L"Play area");
			auto a = sim->playArea;
			Toggle(x + w - S(130), top, L"Show its edge", areaFrame, false, [] {
				areaFrame = !areaFrame;
				GlobalPrefs::Ref().Set("Overlay.AreaFrame", areaFrame);
			}, L"A faint blue line around the play area when it's smaller than the screen (its edge can be dragged either way)");
			Caption(x + S(12), top, L"Size: " + std::to_wstring(a.size.X * CELL) + L" \u00D7 " + std::to_wstring(a.size.Y * CELL) +
			      L" pixels   (largest " + std::to_wstring(XRES) + L" \u00D7 " + std::to_wstring(YRES) + L")");
			int step = 8; // cells
			auto set = [](Rect<int> r) { OverlayWindows::SetPlayArea(r.pos.X, r.pos.Y, r.size.X, r.size.Y); };
			// Grow or shrink about the centre.
			auto resize = [a, set](int dw, int dh) {
				auto r = a;
				r.size.X = std::clamp(r.size.X + dw, 8, CELLS.X);
				r.size.Y = std::clamp(r.size.Y + dh, 8, CELLS.Y);
				r.pos.X = std::clamp(a.pos.X - (r.size.X - a.size.X) / 2, 0, CELLS.X - r.size.X);
				r.pos.Y = std::clamp(a.pos.Y - (r.size.Y - a.size.Y) / 2, 0, CELLS.Y - r.size.Y);
				set(r);
			};
			int by = top + S(24), bw = S(34);
			Caption(x + S(12), by, L"Width");
			PushButton(x + S(60), by, bw, L"\u2212", [=] { resize(-step, 0); }, L"Narrower by 32 pixels");
			PushButton(x + S(60) + bw + S(4), by, bw, L"+", [=] { resize(step, 0); }, L"Wider by 32 pixels");
			Caption(x + S(160), by, L"Height");
			PushButton(x + S(210), by, bw, L"\u2212", [=] { resize(0, -step); }, L"Shorter by 32 pixels");
			PushButton(x + S(210) + bw + S(4), by, bw, L"+", [=] { resize(0, step); }, L"Taller by 32 pixels");
			by += S(30);
			int pw = (w - S(24) - 2 * S(6)) / 3;
			PushButton(x + S(12), by, pw, L"Whole screen", [=] { set(CELLS.OriginRect()); }, L"Use the whole sim");
			PushButton(x + S(12) + pw + S(6), by, pw, L"Stock TPT size", [=] {
				auto stock = Vec2(std::min(153, CELLS.X), std::min(96, CELLS.Y));
				set(RectSized((CELLS - stock) / 2, stock));
			}, L"612 \u00D7 384, centred: saves made at this size open in normal TPT");
			PushButton(x + S(12) + 2 * (pw + S(6)), by, pw, L"Centre", [=] { set(RectSized((CELLS - a.size) / 2, a.size)); }, L"Move the area to the middle");
			y += S(126);

			top = Group(x, y, w, S(88), L"Running");
			bool paused = gc.GetPaused();
			int bw4 = (w - S(24) - 3 * S(6)) / 4;
			PushButton(x + S(12), top, bw4, paused ? L"Resume" : L"Pause", [] { auto &g = GameController::Ref(); g.SetPaused(!g.GetPaused()); }, L"Space", paused);
			PushButton(x + S(12) + bw4 + S(6), top, bw4, L"Step", [] { GameController::Ref().FrameStep(); }, L"One frame (F)");
			PushButton(x + S(12) + 2 * (bw4 + S(6)), top, bw4, L"Reload", [] { GameController::Ref().ReloadSim(); }, L"Back to the last loaded save (F5)");
			PushButton(x + S(12) + 3 * (bw4 + S(6)), top, bw4, L"Clear", [] { GameController::Ref().ClearSim(); }, L"Empty the sim");
			Toggle(x + S(12), top + S(36), L"Multicore physics", model->GetSimulation()->parallelUpdate, false, [] {
				auto *sim = GameController::Ref().GetModel()->GetSimulation();
				sim->parallelUpdate = !sim->parallelUpdate;
				GlobalPrefs::Ref().Set("Simulation.Multicore", sim->parallelUpdate);
			}, L"Most elements update on all CPU cores: several times faster in big scenes. Off: one core, TPT's exact order");
			y += S(96);

			top = Group(x, y, w, S(52), L"Edges");
			const wchar_t *edges[] = { L"Void", L"Solid", L"Loop" };
			const wchar_t *edgeTips[] = { L"Particles and air leave at the edges", L"The edges are walls", L"Particles wrap around (whole screen only for now)" };
			int ex = x + S(12);
			for (int i = 0; i < 3; i++)
			{
				ex += Toggle(ex, top, edges[i], gc.GetEdgeMode() == i, true, [i] { GameController::Ref().SetEdgeMode(i); }, edgeTips[i]) + S(18);
			}
			y += S(60);

			top = Group(x, y, w, S(76), L"Gravity");
			const wchar_t *gravs[] = { L"Vertical", L"Off", L"Radial", L"Custom" };
			int gx = x + S(12);
			for (int i = 0; i < 4; i++)
			{
				gx += Toggle(gx, top, gravs[i], sim->gravityMode == i, true, [sim, i] { sim->gravityMode = i; }) + S(18);
			}
			Toggle(x + S(12), top + S(24), L"Newtonian gravity", model->GetNewtonianGrvity(), false,
			       [model] { model->SetNewtonianGravity(!model->GetNewtonianGrvity()); }, L"Heavy particles pull on each other (N)");
			y += S(84);

			top = Group(x, y, w, S(100), L"Air");
			const wchar_t *airs[] = { L"On", L"Pressure off", L"Velocity off", L"Off", L"No update" };
			int ax = x + S(12), ay = top;
			for (int i = 0; i < 5; i++)
			{
				int tw = text->Measure(airs[i]).cx + S(19);
				if (ax + tw > x + w - S(12))
				{
					ax = x + S(12), ay += S(24);
				}
				ax += Toggle(ax, ay, airs[i], sim->air->airMode == i, true, [sim, i] { sim->air->airMode = i; }) + S(18);
			}
			Toggle(x + S(12), ay + S(24), L"Ambient heat", gc.GetAHeatEnable(), false, [] { GameController::Ref().ToggleAHeat(); }, L"The air carries heat (U)");
			y += S(108);

			auto &opts = Options();
			auto scale = opts.GetTemperatureScale();
			top = Group(x, y, w, S(36) + S(24) + 5 * S(32), L"Heat and air");
			int half = (w - S(24)) / 2, lx = x + S(12), bx = x + S(220), nw = std::min(S(200), x + w - S(12) - bx);
			Toggle(lx, top, L"Heat simulation", opts.GetHeatSimulation(), false, [&opts] { opts.SetHeatSimulation(!opts.GetHeatSimulation()); },
			       L"Off = the old heatless sim some old saves need");
			Toggle(lx + half, top, L"Water equalisation", opts.GetWaterEqualisation(), false, [&opts] { opts.SetWaterEqualisation(!opts.GetWaterEqualisation()); },
			       L"Liquids level out like connected water (slow)");
			int ry = top + S(30);
			auto row = [&](const wchar_t *label) {
				Caption(lx, ry + S(3), label);
				int at = ry;
				ry += S(32);
				return at;
			};
			StringBuilder temp;
			temp << Format::Precision(2);
			format::RenderTemperature(temp, opts.GetAmbientAirTemperature(), scale);
			NumberButton(bx, row(L"Air temperature"), nw, "Ambient air temperature", temp.Build(),
			             FloatSetting(MIN_TEMP, MAX_TEMP, [scale](const String &s) { return format::StringToTemperature(s, scale); }, [&opts](float v) { opts.SetAmbientAirTemperature(v); }),
			             L"The air's temperature at the edges (with ambient heat); a unit like 20C or 300K works");
			NumberButton(bx, row(L"Air pressure"), nw, "Ambient air pressure", String::Build(Format::Precision(2), opts.GetEdgePressure()),
			             FloatSetting(MIN_PRESSURE, MAX_PRESSURE, [](const String &s) { return s.ToNumber<float>(); }, [&opts](float v) { opts.SetEdgePressure(v); }),
			             L"The pressure at the edges");
			NumberButton(bx, row(L"Vorticity"), nw, "Vorticity confinement", String::Build(Format::Precision(2), opts.GetVorticityCoeff()),
			             FloatSetting(0, 1, [](const String &s) { return s.ToNumber<float>(); }, [&opts](float v) { opts.SetVorticityCoeff(v); }),
			             L"How much the air keeps its swirls, 0 to 1");
			ListButton(bx, row(L"Heat convection"), nw, { L"None", L"Legacy", L"Boussinesq" }, opts.GetConvectionMode(),
			           [&opts](int i) { opts.SetConvectionMode(i); }, L"How hot air rises");
			ListButton(bx, row(L"Temperatures in"), nw, { L"Kelvin", L"Celsius", L"Fahrenheit" }, int(scale),
			           [&opts](int i) { opts.SetTemperatureScale(TempScale(i)); }, L"For the HUD and typed temperatures");
		}

		void PaintDisplay()
		{
			auto c = Content();
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
			auto &presets = Renderer::renderModePresets;
			int rows = (int(presets.size()) + 3) / 4;
			int top = Group(x, y, w, S(20) + rows * S(29), L"View mode");
			int bw = (w - S(24) - 3 * S(6)) / 4;
			for (int i = 0; i < int(presets.size()); i++)
			{
				PushButton(x + S(12) + (i % 4) * (bw + S(6)), top + (i / 4) * S(29), bw, Wide(presets[i].Name.EndsWith(" Display") ? presets[i].Name.Substr(0, presets[i].Name.size() - 8) : presets[i].Name),
				           [i] { GameController::Ref().LoadRenderPreset(i); }, L"Number key " + std::to_wstring(i == 9 ? 0 : i + 1));
			}
			y += S(28) + rows * S(29);
			int cw = (w - S(24)) / 3; // three columns
			auto &rs = model->GetRendererSettings();
			top = Group(x, y, w, S(68), L"Show");
			Toggle(x + S(12), top, L"HUD", gc.GetHudEnable(), false, [] { auto &g = GameController::Ref(); g.SetHudEnable(!g.GetHudEnable()); }, L"Frame rate, particle count and cursor info (H)");
			Toggle(x + S(12) + cw, top, L"Decorations", model->GetDecoration(), false, [] { GameController::Ref().SetDecoration(); }, L"Painted colours (Ctrl+B)");
			Toggle(x + S(12) + 2 * cw, top, L"Gravity field", model->GetGravityGrid(), false, [] { GameController::Ref().ShowGravityGrid(); }, L"Newtonian gravity lines (Ctrl+G)");
			struct Extra { const wchar_t *name; uint32_t flag; const wchar_t *tip; } extras[] = {
				{ L"Fancy", DISPLAY_EFFE, L"Moving solids, stickman guns and other extras" },
				{ L"Lensing", DISPLAY_WARP, L"Newtonian gravity bends light" },
				{ L"Persistent", DISPLAY_PERS, L"Paths stay on screen for a while" },
			};
			for (int i = 0; i < 3; i++)
			{
				bool on = rs.displayMode & extras[i].flag;
				Toggle(x + S(12) + i * cw, top + S(24), extras[i].name, on, false, [&rs, flag = extras[i].flag] { rs.displayMode ^= flag; }, extras[i].tip);
			}
			y += S(76);

			// TPT's render options (its whole-screen view sits under the taskbar here), as switches. The view-mode
			// presets above set all three groups at once.
			struct Mode { const wchar_t *name; uint32_t flag; const wchar_t *tip; };
			auto grid = [&](const wchar_t *title, std::initializer_list<Mode> modes, uint32_t &field, int kind) {
				int rows = (int(modes.size()) + 2) / 3;
				int gt = Group(x, y, w, S(20) + rows * S(24), title);
				int i = 0;
				for (auto &m : modes)
				{
					bool on = kind == 2 ? field == m.flag : (field & m.flag) == m.flag;
					Toggle(x + S(12) + (i % 3) * cw, gt + (i / 3) * S(24), m.name, on, kind != 0, [&field, flag = m.flag, kind, on] {
						if (kind == 2) field = on ? 0 : flag;                                   // colours: one or none
						else if (kind == 1) field = (field & ~DISPLAY_AIR & ~flag) | (on ? 0 : flag); // air views: one at a time
						else field = on ? field & ~flag : field | flag;
					}, m.tip);
					i++;
				}
				y += S(28) + rows * S(24);
			};
			grid(L"Effects", {
				{ L"Effects", RENDER_EFFE, L"Special flare effects on some elements" },
				{ L"Fire", RENDER_FIRE, L"Fire effect for gases" },
				{ L"Glow", RENDER_GLOW, L"Glow on some elements" },
				{ L"Blur", RENDER_BLUR, L"Blur for liquids" },
				{ L"Blob", RENDER_BLOB, L"Draws everything as blobs" },
				{ L"Basic", RENDER_BASC, L"Basic drawing; without it most things are invisible" },
				{ L"Spark glow", RENDER_SPRK, L"Glow on sparks" },
			}, rs.renderMode, 0);
			grid(L"Air (one at a time)", {
				{ L"Pressure", DISPLAY_AIRP, L"Red is positive pressure, blue negative" },
				{ L"Velocity", DISPLAY_AIRV, L"Air velocity and positive pressure" },
				{ L"Both", DISPLAY_AIRC, L"Pressure as red and blue, velocity as white" },
				{ L"Air heat", DISPLAY_AIRH, L"Air temperature, like heat display" },
				{ L"Vorticity", DISPLAY_AIRW, L"Red is clockwise, blue anticlockwise" },
			}, rs.displayMode, 1);
			grid(L"Colours (one at a time)", {
				{ L"Heat", COLOUR_HEAT, L"Temperature: dark blue is coldest, pink hottest" },
				{ L"Life", COLOUR_LIFE, L"Life value in greys" },
				{ L"Heat gradient", COLOUR_GRAD, L"Tints colours to show heat spreading" },
				{ L"Plain", COLOUR_BASC, L"No effects at all; overrides everything, deco too" },
			}, rs.colorMode, 2);
		}

		// Brush, replace mode and decoration colour (TPT keeps them in its tool strip and keys).
		// The Fun tab: what the sim does with the desktop, by topic, each setting with its description under it.
		// (and the Buddy tab, the same way)
		void PaintFun(FunRow *rows, size_t n)
		{
			auto c = Content();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
			const Text::Style note{ 11, FW_NORMAL, false, false, 0x6D6D6D };
			for (size_t i = 0; i < n;)
			{
				const wchar_t *title = rows[i++].name;
				// a click on a heading folds its rows away (remembered; all but the tab's first start folded)
				ByteString pref = "Overlay.Folded.";
				for (auto *t = title; *t; t++) pref.push_back(char(*t));
				bool folded = GlobalPrefs::Ref().Get(pref, i > 1);
				int top = y + text->Measure(title).cy + S(4), ry = top;
				Add(RECT{ x, y, x + w, top }, [pref, folded] { GlobalPrefs::Ref().Set(pref, !folded); }, folded ? L"Show this section" : L"Hide this section");
				if (folded)
				{
					while (i < n && rows[i].tip) i++;
					FoldGroup(x, y, w, top - y + S(6), title, true);
					y = top + S(12);
					continue;
				}
				for (; i < n && rows[i].tip; i++)
				{
					auto &f = rows[i];
					int nx = x + S(12);
					if (f.b)
					{
						Toggle(nx, ry, f.name, *f.b, false, [&f] {
							*f.b = !*f.b;
							GlobalPrefs::Ref().Set(f.pref, *f.b);
						}, f.tip);
						if (f.b == &fun.buddy)
						{
							auto file = GlobalPrefs::Ref().Get("Overlay.Fun.BuddySwf", ByteString(""));
							PushButton(x + w - S(132), ry - S(3), S(120), L"Choose SWF...", [] { ChooseBuddySwf(); }, file.empty() ? L"No file chosen yet" : Wide(file.FromUtf8()));
						}
						nx += S(19); // under the label, past the box
					}
					else if (f.v)
					{
						FunSliderRow(f, x, w, ry);
					}
					else if (f.kind == 1)
					{
						Caption(nx, ry, f.name);
						const wchar_t *kinds[] = { L"Walk through", L"Active window", L"Solid" };
						int kx = x + S(160);
						for (int k = 0; k < 3; k++)
							kx += Toggle(kx, ry, kinds[k], fun.buddyWindows == k, true, [k] {
								fun.buddyWindows = k;
								GlobalPrefs::Ref().Set("Overlay.Buddy.Windows", k);
							}) + S(18);
					}
					else if (f.kind == 2)
					{
						Caption(nx, ry, f.name);
						const wchar_t *menus[] = { L"File", L"Skins", L"Items", L"Modes", L"Settings", L"Help" };
						int bw = (w - S(172)) / 6;
						for (int k = 0; k < 6; k++)
							PushButton(x + S(160) + k * bw, ry - S(3), bw - S(4), menus[k], [this, k] {
								if (!OverlayBuddy::GameMenu(k, hwnd)) OverlayWindows::Notify("Turn Interactive Buddy on first");
							}, L"");
					}
					else if (f.kind == 3)
					{
						Caption(nx, ry, f.name);
						PushButton(x + S(160), ry - S(3), S(220), L"Import from the desktop app", [] {
							OverlayWindows::Notify(OverlayBuddy::ImportSave() ? "Buddy has your desktop app's save now" : "No desktop Interactive Buddy save found");
						}, L"");
					}
					else
					{
						Caption(nx, ry, f.name);
						const wchar_t *kinds[] = { L"By season", L"Snow", L"Rain", L"Leaves", L"Hail", L"Fog" };
						int kx = x + S(160);
						for (int k = 0; k < 6; k++)
						{
							if (k == 4) kx = x + S(160), ry += S(26); // second row
							kx += Toggle(kx, ry, kinds[k], fun.weatherKind == k, true, [k] {
								fun.weatherKind = k;
								GlobalPrefs::Ref().Set("Overlay.Fun.WeatherKind", k);
							}) + S(18);
						}
					}
					ry = Para(nx, ry + S(21), x + w - S(12), f.tip, note) + S(8);
				}
				FoldGroup(x, y, w, ry - y, title, false);
				y = ry + S(10);
			}
			Add(RECT{ x, y, x + 1, y + 1 }, nullptr); // lets the page scroll to the last description
		}

		// A Fun slider: name, track and value ("off" at the far left where that is off)
		void FunSliderRow(FunRow &f, int x, int w, int ry)
		{
			Caption(x + S(12), ry, f.name);
			wchar_t val[16];
			if (f.unit == 1) swprintf(val, 16, *f.v >= 1 ? L"%d" : L"off", int(*f.v));
			else if (f.unit == 2) swprintf(val, 16, *f.v >= 1 ? L"%d min" : L"off", int(*f.v));
			else if (f.unit == 3) swprintf(val, 16, L"%d s", int(*f.v));
			else if (f.unit == 4) swprintf(val, 16, *f.v >= 1 ? L"%d:00" : L"off", int(*f.v) % 24);
			else if (f.unit == 5) swprintf(val, 16, *f.v >= 1 ? L"%d px" : L"off", int(*f.v));
			else if (f.unit == 6) swprintf(val, 16, *f.v >= 1 ? L"%d s" : L"off", int(*f.v));
			else if (f.unit == 7) swprintf(val, 16, *f.v >= 1 ? L"%d / h" : L"off", int(*f.v));
			else if (f.unit == 8) swprintf(val, 16, L"%d%%", int(*f.v));
			else if (f.unit == 9) swprintf(val, 16, *f.v >= 1 ? L"%d" : L"no limit", int(*f.v));
			else swprintf(val, 16, *f.v > 0 ? L"%d%%" : L"off", int(std::lround(*f.v * 100)));
			Caption(x + w - S(60), ry, val);
			Slider(x + S(160), ry, w - S(232), *f.v, f.lo, f.hi, [&f](float v) {
				*f.v = f.unit == 3 ? std::round(v / 10) * 10 : f.unit ? std::round(v) : std::round(v * 20) / 20;
				GlobalPrefs::Ref().Set(f.pref, *f.v);
			}, f.tip);
		}

		void PaintTools()
		{
			auto c = Content();
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
			int n = model->BrushListSize(), rows = (n + 2) / 3;
			int top = Group(x, y, w, S(56) + rows * S(29), L"Brush");
			int bw = (w - S(24) - 2 * S(6)) / 3;
			for (int i = 0; i < n; i++)
			{
				std::wstring name = i == 0 ? L"Circle" : i == 1 ? L"Square" : i == 2 ? L"Triangle" : L"Custom " + std::to_wstring(i - 2);
				PushButton(x + S(12) + (i % 3) * (bw + S(6)), top + (i / 3) * S(29), bw, name,
				           [i] { GameController::Ref().GetModel()->SetBrushID(i); }, L"Tab cycles the shapes", model->GetBrushID() == i);
			}
			int sy = top + rows * S(29) + S(4);
			auto r = model->GetBrush().GetRadius();
			Caption(x + S(12), sy, L"Size " + std::to_wstring(r.X) + L" \u00D7 " + std::to_wstring(r.Y));
			PushButton(x + S(12) + bw + S(6), sy, S(40), L"\u2212", [] { GameController::Ref().AdjustBrushSize(-1); }, L"Smaller ([ or the wheel)");
			PushButton(x + S(12) + bw + S(52), sy, S(40), L"+", [] { GameController::Ref().AdjustBrushSize(1); }, L"Bigger (] or the wheel)");
			y += S(64) + rows * S(29);

			top = Group(x, y, w, S(76), L"Drawing over particles");
			int flags = gc.GetReplaceModeFlags();
			Toggle(x + S(12), top, L"Replace mode", flags & REPLACE_MODE, false, [] {
				auto &g = GameController::Ref();
				g.SetReplaceModeFlags((g.GetReplaceModeFlags() ^ REPLACE_MODE) & ~SPECIFIC_DELETE);
			}, L"Draw over particles instead of only empty space (Insert)");
			Toggle(x + S(12), top + S(24), L"Erase only the right-click material", flags & SPECIFIC_DELETE, false, [] {
				auto &g = GameController::Ref();
				g.SetReplaceModeFlags((g.GetReplaceModeFlags() ^ SPECIFIC_DELETE) & ~REPLACE_MODE);
			}, L"Specific delete (Delete)");
			y += S(84);

			top = Group(x, y, w, S(84), L"Decoration");
			Toggle(x + S(12), top, L"Show decorations", model->GetDecoration(), false, [] { GameController::Ref().SetDecoration(); }, L"Ctrl+B");
			auto col = model->GetColourSelectorColour();
			uint32_t rgb = (col.Red << 16) | (col.Green << 8) | col.Blue;
			RECT swatch{ x + S(12), top + S(28), x + S(52), top + S(50) };
			cv.RoundRect(swatch.left, swatch.top, S(40), S(22), S(3), rgb, rgb, 255);
			cv.RoundFrame(swatch.left, swatch.top, S(40), S(22), S(3), Hovered(swatch) ? 0x3C7FB1 : 0x707070, 255);
			Add(swatch, [] { GameController::Ref().OpenColourPicker(); }, L"TPT's colour picker, with transparency");
			PushButton(x + S(60), top + S(26), S(140), L"Pick colour\u2026", [this] { later = [this] { PickDecoColour(); }; },
			           L"The colour the deco tools paint with (click the swatch for transparency)");
		}

		void PaintSaves()
		{
			auto c = Content();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
			auto *model = GameController::Ref().GetModel();
			int top = Group(x, y, w, S(72), L"Open save");
			std::wstring name = L"Untitled simulation", info = L"Not saved yet";
			if (auto *save = model->GetSave())
			{
				name = Wide(save->GetName());
				info = L"by " + Platform::WinWiden(save->GetUserName()) + L"   \u25B2 " + std::to_wstring(save->GetVotesUp()) +
				       L"  \u25BC " + std::to_wstring(save->GetVotesDown());
				std::wstring tags;
				for (auto &tag : save->GetTags()) tags += (tags.empty() ? L"   Tags: " : L", ") + Platform::WinWiden(tag);
				info += tags;
			}
			else if (auto *file = model->GetSaveFile())
			{
				name = file->GetDisplayName().size() ? Wide(file->GetDisplayName()) : Platform::WinWiden(file->GetName());
				info = L"Saved on this computer";
			}
			text->Draw(cv, x + S(12), top, name, Text::Style{ 13, FW_SEMIBOLD, false, false, 0x1E395B });
			text->Draw(cv, x + S(12), top + S(24), info, Text::Style{ 11, FW_NORMAL, false, false, 0x6D6D6D });
			y += S(80);
			top = Group(x, y, w, S(96), L"Saves");
			int bw = (w - S(24) - S(6)) / 2;
			PushButton(x + S(12), top, bw, L"Open\u2026", [] { GameController::Ref().OpenLocalBrowse(); }, L"Your local saves");
			PushButton(x + S(12) + bw + S(6), top, bw, L"Save\u2026", [] { GameController::Ref().OpenLocalSaveWindow(false); },
			           L"Saves the play area; at 612 \u00D7 384 or smaller it opens in normal TPT");
			PushButton(x + S(12), top + S(29), bw, L"Stamps\u2026", [] { GameController::Ref().OpenStamps(); }, L"Saved selections (K)");
			PushButton(x + S(12) + bw + S(6), top + S(29), bw, L"Reload", [] { GameController::Ref().ReloadSim(); }, L"Back to the last loaded save (F5)");
			y += S(104);
			PaintOnline(x, y, w);
			// The rest of what TPT's bottom button row had (it's hidden while the side window is open).
			top = Group(x, y, w, S(125), L"Online");
			PushButton(x + S(12), top, bw, L"Browse\u2026", [] { GameController::Ref().OpenSearch(""); }, L"Saves on powdertoy.co.uk");
			PushButton(x + S(12) + bw + S(6), top, bw, L"Upload\u2026", [] { GameController::Ref().OpenSaveWindow(); }, L"Save to powdertoy.co.uk");
			PushButton(x + S(12), top + S(29), bw, L"\u25B2 Vote up", [] { GameController::Ref().Vote(1); }, L"Vote for the save you opened");
			PushButton(x + S(12) + bw + S(6), top + S(29), bw, L"\u25BC Vote down", [] { GameController::Ref().Vote(-1); }, L"Vote against the save you opened");
			PushButton(x + S(12), top + S(58), bw, L"Tags\u2026", [] { GameController::Ref().OpenTags(); }, L"The open save's tags");
			PushButton(x + S(12) + bw + S(6), top + S(58), bw, L"Sign in\u2026", [] { GameController::Ref().OpenLogin(); }, L"Your powdertoy.co.uk account");
			y += S(133);
			top = Group(x, y, w, S(52), L"TPT");
			PushButton(x + S(12), top, bw, L"Options\u2026", [] { GameController::Ref().OpenOptions(); }, L"TPT's own settings window");
			y += S(60);
			// What's on this computer, with thumbnails: click opens (a stamp: place it), right-click for more.
			int tw = S(124), th = S(78), cellH = th + S(22), pad = S(8);
			int cols = std::max(1, (w - S(24) + pad) / (tw + pad));
			for (int kind = 0; kind < 2; kind++)
			{
				std::vector<Local *> list;
				for (auto &l : locals)
					if (l.stamp.empty() == (kind == 0)) list.push_back(&l);
				int rows = std::max(1, (int(list.size()) + cols - 1) / cols);
				top = Group(x, y, w, S(66) + rows * (cellH + pad), kind ? L"Stamps" : L"Saves on this computer");
				PushButton(x + S(12), top, bw, L"Show folder", [kind] {
					auto dir = ByteString(kind ? STAMPS_DIR : LOCAL_SAVE_DIR);
					Platform::MakeDirectory(dir);
					ShellExecuteW(nullptr, L"open", FullPath(dir).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}, kind ? L"The stamps folder" : L"The saves folder; drop a .cps or .stm file on this window to open it");
				top += S(31);
				if (list.empty()) Caption(x + S(12), top, kind ? L"No stamps yet" : L"No saves yet", 0x6D6D6D);
				for (int i = 0; i < int(list.size()); i++)
				{
					auto &l = *list[i];
					int cx = x + S(12) + (i % cols) * (tw + pad), cy = top + (i / cols) * (cellH + pad);
					RECT rc{ cx, cy, cx + tw, cy + cellH };
					cv.RoundRect(cx, cy, tw, th, 0, 0x000000, 0x000000, 255); // TPT's black, behind a thumbnail that doesn't fill
					if (l.thumb)
					{
						auto size = l.thumb->Size();
						int ox = cx + (tw - size.X) / 2, oy = cy + (th - size.Y) / 2;
						for (int py = 0; py < size.Y; py++)
							for (int px = 0; px < size.X; px++) cv.Blend(ox + px, oy + py, l.thumb->Data()[py * size.X + px] & 0xFFFFFF, 255);
					}
					cv.RoundFrame(cx - 1, cy - 1, tw + 2, th + 2, 0, Hovered(rc) ? 0x3C7FB1 : 0xABADB3, 255);
					auto ns = text->Measure(l.name);
					text->Draw(cv, cx + std::max(0, (tw - int(ns.cx)) / 2), cy + th + S(3), l.name, theme->ink, false, tw);
					Add(rc, [path = l.path, stamp = l.stamp] { OpenLocal(path, stamp); },
					    l.name + (kind ? L": click to place it, right-click for more" : L": click to open, right-click for more"),
					    [this, path = l.path, stamp = l.stamp, name = l.name] { later = [this, path, stamp, name] { LocalMenu(path, stamp, name); }; });
				}
				y += S(74) + rows * (cellH + pad);
			}
		}

		// powdertoy.co.uk's saves: a search box, sort, a page of thumbnails (click: TPT's preview, to open, vote,
		// comment) and page buttons. y moves past it.
		void PaintOnline(int x, int &y, int w)
		{
			int tw = S(124), th = S(78), cellH = th + S(38), pad = S(8);
			int cols = std::max(1, (w - S(24) + pad) / (tw + pad));
			if (onlinePer != cols * 3)
			{
				onlinePer = cols * 3; // a wider window: more a page
				onlineSearch = true;
			}
			int rows = online.empty() ? 1 : (int(online.size()) + cols - 1) / cols;
			int h = S(31) + S(36) + rows * (cellH + pad) + S(31) + S(8);
			int top = Group(x, y, w, h, L"Online saves");
			RECT sr{ x + S(12), top, x + w - S(12), top + S(23) };
			int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
			bool here = typing && typed == &onlineQuery;
			cv.RoundRect(sr.left, sr.top, sw, sh, S(3), 0xFFFFFF, 0xFFFFFF, 255);
			cv.RoundFrame(sr.left, sr.top, sw, sh, S(3), here ? 0x3C7FB1 : (Hovered(sr) ? 0x7EB4EA : 0xABADB3), 255);
			auto shown = onlineQuery.empty() && !here ? std::wstring(L"Search saves") : onlineQuery;
			auto qs = text->Measure(shown.empty() ? std::wstring(L" ") : shown);
			int ty = sr.top + (sh - qs.cy) / 2;
			text->Draw(cv, sr.left + S(6), ty, shown, onlineQuery.empty() ? 0x6D6D6D : 0x000000, false, sw - S(12));
			if (here) Line(cv, sr.left + S(6) + (onlineQuery.empty() ? 0 : std::min(int(qs.cx), sw - S(12))), ty, std::max(1, S(1)), qs.cy, 0x000000);
			Add(sr, [this] { StartTyping(&onlineQuery); }, L"Words, user:name or a save ID; Enter searches, Esc clears");
			int ry = top + S(31), sx = x + S(12);
			const wchar_t *sorts[] = { L"Popular", L"New", L"Favourites", L"Mine" };
			bool signedIn = Client::Ref().GetAuthUser().has_value();
			for (int i = 0; i < (signedIn ? 4 : 2); i++)
			{
				sx += Toggle(sx, ry, sorts[i], onlineSort == i, true, [this, i] { onlineSort = i, onlinePage = 0, onlineSearch = true; }) + S(14);
			}
			if (onlineSort == 0)
			{
				ListButton(x + w - S(12) - S(140), ry, S(140), { L"All time", L"Today", L"This week", L"This month", L"This year" }, onlinePeriod,
				           [this](int i) { onlinePeriod = i, onlinePage = 0, onlineSearch = true; }, L"Popular saves from this time");
			}
			int gy = ry + S(36);
			if (online.empty()) Caption(x + S(12), gy, onlineStatus, 0x6D6D6D);
			for (int i = 0; i < int(online.size()); i++)
			{
				auto &o = online[i];
				int cx = x + S(12) + (i % cols) * (tw + pad), cy = gy + (i / cols) * (cellH + pad);
				RECT rc{ cx, cy, cx + tw, cy + cellH };
				cv.RoundRect(cx, cy, tw, th, 0, 0x000000, 0x000000, 255);
				if (o.thumb)
				{
					auto size = o.thumb->Size();
					int ox = cx + (tw - size.X) / 2, oy = cy + (th - size.Y) / 2;
					for (int py = 0; py < size.Y; py++)
						for (int px = 0; px < size.X; px++) cv.Blend(ox + px, oy + py, o.thumb->Data()[py * size.X + px] & 0xFFFFFF, 255);
				}
				cv.RoundFrame(cx - 1, cy - 1, tw + 2, th + 2, 0, Hovered(rc) ? 0x3C7FB1 : 0xABADB3, 255);
				auto ns = text->Measure(o.name);
				text->Draw(cv, cx + std::max(0, (tw - int(ns.cx)) / 2), cy + th + S(3), o.name, theme->ink, false, tw);
				auto info = L"\u25B2" + std::to_wstring(o.up) + L" \u25BC" + std::to_wstring(o.down) + L"  " + o.author;
				auto is = text->Measure(info, Text::Style{ 10, FW_NORMAL, false, false, 0x6D6D6D });
				text->Draw(cv, cx + std::max(0, (tw - int(is.cx)) / 2), cy + th + S(19), info, Text::Style{ 10, FW_NORMAL, false, false, 0x6D6D6D });
				Add(rc, [id = o.id, date = o.date] { GameController::Ref().OpenSavePreview(id, date, savePreviewNormal); },
				    o.name + L" by " + o.author + L": click to see it, then open");
			}
			int py = gy + rows * (cellH + pad), pages = std::max(1, (onlineTotal + onlinePer - 1) / onlinePer);
			if (onlinePage > 0) PushButton(x + S(12), py, S(110), L"\u25C0 Previous", [this] { onlinePage--, onlineSearch = true; });
			if (onlinePage + 1 < pages) PushButton(x + w - S(12) - S(110), py, S(110), L"Next \u25B6", [this] { onlinePage++, onlineSearch = true; });
			auto page = L"Page " + std::to_wstring(onlinePage + 1) + L" of " + std::to_wstring(pages);
			Caption(x + (w - int(text->Measure(page).cx)) / 2, py + S(3), page, 0x6D6D6D);
			y += h + S(8);
		}

		// The Online saves search and its thumbnails, polled while the Saves tab shows
		void TickOnline()
		{
			if (onlineSearch && !searching)
			{
				onlineSearch = false;
				auto sort = onlineSort == 1 ? http::sortByDate : http::sortByVotes;
				auto category = onlineSort == 2 ? http::categoryFavourites : onlineSort == 3 ? http::categoryMyOwn : http::categoryNone;
				auto period = onlineSort == 0 ? http::Period(onlinePeriod) : http::allSaves;
				searching = std::make_unique<http::SearchSavesRequest>(onlinePage * onlinePer, onlinePer, Platform::WinNarrow(onlineQuery), period, sort, category);
				searching->Start();
				online.clear();
				onlineStatus = L"Loading\u2026";
				dirty = true;
			}
			if (searching && searching->CheckDone())
			{
				try
				{
					auto [total, saves] = searching->Finish();
					onlineTotal = total;
					for (auto &s : saves)
					{
						Online o{ s->GetID(), s->GetVersion(), s->GetVotesUp(), s->GetVotesDown(), Wide(s->GetName()), Platform::WinWiden(s->GetUserName()) };
						online.push_back(std::move(o));
					}
					onlineStatus = online.empty() ? L"No saves found" : L"";
				}
				catch (const http::RequestError &ex)
				{
					onlineStatus = L"Couldn't reach powdertoy.co.uk: " + Platform::WinWiden(ex.what());
				}
				searching.reset();
				dirty = true;
			}
			for (auto &o : online)
			{
				if (!o.tried)
				{
					o.tried = true;
					o.req = std::make_unique<http::ThumbnailRequest>(o.id, o.date, Vec2<int>{ S(124), S(78) });
					o.req->Start();
				}
				if (o.req && o.req->CheckDone())
				{
					try
					{
						o.thumb = o.req->Finish();
					}
					catch (const http::RequestError &)
					{
					}
					o.req.reset();
					dirty = true;
				}
			}
		}

		// The start page, in place of TPT's text over the game at startup: the same help, laid out like a Vista
		// welcome page. Drawn on its own canvas, then the visible part is copied in (it scrolls).
		int welcomeH = 0;
		int formH = 0; // the Tools, Simulation, Display, Saves and Overlay pages' height (they scroll when the window is short)
		// Words wrapped to [x, right); returns the y after the last line.
		int Para(int x, int y, int right, const std::wstring &s, const Text::Style &st)
		{
			int cx = x, lineH = text->Measure(L"Ag", st).cy, space = text->Measure(L" ", st).cx;
			size_t at = 0;
			while (at < s.size())
			{
				auto end = s.find(L' ', at);
				auto word = s.substr(at, end == s.npos ? s.npos : end - at);
				at = end == s.npos ? s.size() : end + 1;
				int ww = text->Measure(word, st).cx;
				if (cx > x && cx + ww > right)
				{
					cx = x;
					y += lineH;
				}
				text->Draw(cv, cx, y, word, st);
				cx += ww + space;
			}
			return y + lineH;
		}

		void PaintWelcome()
		{
			using Style = Text::Style;
			auto c = Content();
			int pw = c.right - c.left, ph = S(1700);
			Canvas page;
			page.Resize(pw, ph);
			page.RoundRect(0, 0, pw, ph, 0, theme->page, theme->page, 255); // in the look's page, heading and ink colours
			std::swap(cv, page);
			struct Link { RECT rc; std::function<void()> go; std::wstring tip; };
			std::vector<Link> links;

			int x0 = S(18), x1 = pw - S(18), y = S(14);
			const Style title{ 22, FW_LIGHT, false, false, theme->heading }, subtitle{ 12, FW_NORMAL, true, false, 0x6D6D6D },
			            link{ 12, FW_NORMAL, false, true, 0x0066CC }, body{ 12, FW_NORMAL, false, false, theme->ink },
			            note{ 11, FW_NORMAL, true, false, 0x6D6D6D }, footer{ 10, FW_NORMAL, false, false, 0x8A8A8A };

			// The top of the page as each family greeted you: 95's Welcome dialog, XP and Vista's Welcome Center, 7 to 11's
			// Getting Started, the old Macs' small plain window, the modern Macs' icon, heading and Continue.
			int gen = Generation(int(std::find(std::begin(themes), std::end(themes), theme) - std::begin(themes)));
			enum { Classic, Center, Started, OldMac, NewMac } family = gen <= 2 ? Classic : gen <= 4 ? Center
			                                                         : gen == 9 || gen == 12 || gen == 13 ? OldMac
			                                                         : gen == 10 || gen >= 14 ? NewMac : Started;
			const std::wstring version = L"Desktop " DESKTOP_VERSION L"  ·  The Powder Toy " + std::to_wstring(DISPLAY_VERSION[0]) + L"." +
			                             std::to_wstring(DISPLAY_VERSION[1]);
			std::vector<std::pair<const wchar_t *, std::function<void()>>> sites;
			for (auto [label, url] : { std::pair{ L"powdertoy.co.uk", L"https://powdertoy.co.uk" },
			                           std::pair{ L"Discord", L"https://tpt.io/discord" },
			                           std::pair{ L"Create an account", L"https://powdertoy.co.uk/Register.html" } })
				sites.push_back({ label, [url = std::wstring(url)] { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); } });
			auto centred = [&](const std::wstring &s, const Style &st) {
				auto size = text->Measure(s, st);
				text->Draw(cv, (pw - int(size.cx)) / 2, y, s, st);
				y += size.cy;
			};
			// the sites as links in a row, from x (or centred: x < 0)
			auto siteLinks = [&](int x) {
				if (x < 0)
				{
					x = -S(18);
					for (auto &site : sites) x += int(text->Measure(site.first, link).cx) + S(18);
					x = (pw - x) / 2;
				}
				for (auto &[label, go] : sites)
				{
					auto size = text->Measure(label, link);
					text->Draw(cv, x, y, label, link);
					links.push_back({ RECT{ x, y, x + int(size.cx), y + int(size.cy) }, go, label });
					x += size.cx + S(18);
				}
				y += S(30);
			};
			// a row of the look's push buttons, from x (or centred: x < 0)
			auto buttons = [&](int x, int bw, int bh, const std::vector<std::pair<const wchar_t *, std::function<void()>>> &list) {
				int gap = S(10);
				bw = std::min(bw, (x1 - x0 - gap * (int(list.size()) - 1)) / int(list.size())); // fit the page
				int total = int(list.size()) * (bw + gap) - gap;
				if (x < 0) x = (pw - total) / 2;
				const Style cap{ 12, FW_NORMAL, false, false, theme->ink };
				for (auto &[label, go] : list)
				{
					theme->Button(cv, x, y, bw, bh, 0);
					auto size = text->Measure(label, cap);
					text->Draw(cv, x + (bw - int(size.cx)) / 2, y + (bh - int(size.cy)) / 2, label, cap);
					links.push_back({ RECT{ x, y, x + bw, y + bh }, go, label });
					x += bw + gap;
				}
				y += bh;
			};
			const wchar_t *tipLabel = L"Tip";
			if (family == Classic)
			{
				// "Welcome to" then the name in bold, an etched rule, and its buttons
				const Style light{ 22, FW_NORMAL, false, false, theme->ink }, bold{ 22, FW_BOLD, false, false, theme->ink };
				text->Draw(cv, x0, y, L"Welcome to ", light);
				text->Draw(cv, x0 + text->Measure(L"Welcome to ", light).cx, y, L"The Powder Toy", bold);
				y += text->Measure(L"Ag", light).cy + S(6);
				for (int i = x0; i < x1; i++) cv.Blend(i, y, 0x808080, 255), cv.Blend(i, y + 1, 0xFFFFFF, 255);
				y += S(8);
				text->Draw(cv, x0, y, version, body);
				y += text->Measure(L"Ag", body).cy + S(10);
				buttons(x0, S(140), S(24), sites);
				y += S(18);
				tipLabel = L"Did you know...";
			}
			else if (family == Center)
			{
				// the Welcome Center's banner, in the look's heading colour fading out
				int bh = S(74);
				auto mix = [](uint32_t a, uint32_t b, int t) {
					auto ch = [&](int sh) { return uint32_t((int((a >> sh) & 0xFF) * (255 - t) + int((b >> sh) & 0xFF) * t) / 255) << sh; };
					return ch(16) | ch(8) | ch(0);
				};
				for (int i = 0; i < pw; i++)
				{
					uint32_t c = mix(theme->heading, theme->page, i * 200 / std::max(1, pw));
					for (int j = 0; j < bh; j++) cv.Blend(i, j, c, 255);
				}
				y = S(12);
				text->Draw(cv, x0, y, L"Welcome to The Powder Toy", Style{ 22, FW_LIGHT, false, false, 0xFFFFFF });
				y += text->Measure(L"Ag", title).cy + S(2);
				text->Draw(cv, x0, y, version, Style{ 12, FW_NORMAL, false, false, 0xFFFFFF });
				y = bh + S(14);
				buttons(x0, S(170), S(40), sites);
				y += S(20);
			}
			else if (family == OldMac || family == NewMac)
			{
				// centred: the app's icon (modern Macs), the name, the version, the sites, and Continue
#ifdef _WIN32
				static HICON icon = HICON(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 256, 256, 0));
				if (family == NewMac && icon)
				{
					int is = S(64);
					CaptureGdi(cv, (pw - is) / 2, y, is, is, [&](HDC dc, RECT &rc) {
						DrawIconEx(dc, 0, 0, icon, rc.right, rc.bottom, 0, nullptr, DI_NORMAL);
					});
					y += is + S(10);
				}
#endif
				centred(L"Welcome to The Powder Toy", Style{ family == NewMac ? 22 : 18, FW_BOLD, false, false, theme->heading });
				y += S(4);
				centred(version, family == NewMac ? subtitle : body);
				y += S(12);
				if (family == OldMac)
					for (int i = x0 + S(40); i < x1 - S(40); i++) cv.Blend(i, y, theme->ink, 255);
				y += S(12);
				siteLinks(-1);
				if (family == NewMac)
				{
					buttons(-1, S(110), S(24), { { L"Continue", [this] { tab = 0, scroll = 0; } } });
					y += S(24);
				}
			}
			else
			{
				text->Draw(cv, x0, y, L"Welcome to The Powder Toy", title);
				y += text->Measure(L"Ag", title).cy + S(2);
				text->Draw(cv, x0, y, version, subtitle);
				y += text->Measure(L"Ag", subtitle).cy + S(8);
				siteLinks(x0);
			}

			// TPT's notifications (update news, server messages): a yellow row each, with Open and Dismiss.
			for (auto *n : GameController::Ref().GetModel()->GetNotifications())
			{
				int rh = S(30);
				cv.RoundRect(x0, y, x1 - x0, rh, S(3), 0xFFFBE5, 0xFFF4C4, 255);
				cv.RoundFrame(x0, y, x1 - x0, rh, S(3), 0xE5C365, 255);
				cv.RoundRect(x0 + S(8), y + S(7), S(16), S(16), S(8), 0xF0B030, 0xC08000, 255);
				text->Draw(cv, x0 + S(14), y + S(7), L"!", Style{ 11, FW_BOLD, false, false, 0xFFFFFF });
				int lx = x1 - S(10);
				for (auto [label, dismiss] : { std::pair{ L"Dismiss", true }, std::pair{ L"Open", false } })
				{
					auto size = text->Measure(label, link);
					lx -= size.cx;
					text->Draw(cv, lx, y + S(6), label, link);
					links.push_back({ RECT{ lx, y + S(6), lx + int(size.cx), y + S(6) + int(size.cy) }, [n, dismiss] {
						if (dismiss) GameController::Ref().RemoveNotification(n);
						else n->Action();
					}, dismiss ? L"Remove this notice" : L"Open it" });
					lx -= S(14);
				}
				text->Draw(cv, x0 + S(32), y + S(6), Wide(n->Message), 0x1A1A1A, false, lx - x0 - S(36));
				y += rh + S(6);
			}
			if (!GameController::Ref().GetModel()->GetNotifications().empty()) y += S(10);

			// Headers: H1 is the title, H2 a part of the page (with a rule), H3 a group of shortcuts.
			auto header = [&](const wchar_t *name) {
				const Style h2{ 17, FW_SEMIBOLD, false, false, theme->heading };
				text->Draw(cv, x0, y, name, h2);
				y += text->Measure(L"Ag", h2).cy + S(3);
				for (int i = x0; i < x1; i++) cv.Blend(i, y, theme->groupEdge, 255);
				y += S(12);
			};

			header(L"Getting started");
			// The tip: in the look's tooltip
			{
				const Style tipStyle{ 12, FW_BOLD, false, false, 0x7A5B00 };
				int top = y, tx = x0 + S(30), px = tx + std::max(S(32), int(text->Measure(tipLabel, tipStyle).cx) + S(8));
				int bottom = Para(px, top + S(8), x1 - S(10),
				                  L"Pick a material in the Elements tab, then draw anywhere on your desktop. Ctrl+Alt+E shows or hides this window.", body) + S(8);
				Canvas tip;
				tip.Resize(x1 - x0, bottom - top);
				theme->Popup(tip);
				for (int j = 0; j < tip.h; j++)
					for (int i = 0; i < tip.w; i++)
						if (uint32_t c = tip.px[size_t(j) * tip.w + i], a = c >> 24; a) // (premultiplied: it was drawn over nothing)
						{
							auto ch = [&](int sh) { return std::min(255u, ((c >> sh) & 0xFF) * 255 / a) << sh; };
							cv.Blend(x0 + i, top + j, ch(16) | ch(8) | ch(0), int(a));
						}
				cv.RoundRect(x0 + S(10), top + S(9), S(16), S(16), S(8), 0x3C8DDE, 0x1E5FAA, 255);
				text->Draw(cv, x0 + S(16), top + S(9), L"i", Style{ 11, FW_BOLD, true, false, 0xFFFFFF });
				text->Draw(cv, tx, top + S(8), tipLabel, tipStyle);
				Para(px, top + S(8), x1 - S(10),
				     L"Pick a material in the Elements tab, then draw anywhere on your desktop. Ctrl+Alt+E shows or hides this window.", body);
				y = bottom + S(18);
			}

			// Shortcut sections: a coloured heading and rule, then keycaps and what they do. "|" separates
			// alternatives, "+" keys pressed together.
			struct Row { const wchar_t *keys, *what, *aside; };
			auto section = [&](const wchar_t *name, uint32_t accent, std::initializer_list<Row> rows) {
				// An accent block, then the name in the accent colour.
				const Style h3{ 14, FW_SEMIBOLD, false, false, accent };
				int hh = text->Measure(L"Ag", h3).cy;
				cv.RoundRect(x0, y + S(3), S(4), hh - S(6), 0, accent, accent, 255);
				text->Draw(cv, x0 + S(10), y, name, h3);
				y += hh + S(6);
				int keyW = S(200);
				for (auto &row : rows)
				{
					int kx = x0 + S(4), ky = y;
					std::wstring keys = row.keys;
					size_t at = 0;
					bool first = true;
					while (at <= keys.size())
					{
						auto end = keys.find(L'|', at);
						auto alt = keys.substr(at, end == keys.npos ? keys.npos : end - at);
						at = end == keys.npos ? keys.size() + 1 : end + 1;
						if (!first)
						{
							text->Draw(cv, kx, ky + S(2), L"or", note);
							kx += text->Measure(L"or", note).cx + S(5);
						}
						first = false;
						size_t k = 0;
						bool firstKey = true;
						while (k <= alt.size())
						{
							auto plus = alt.find(L'+', k);
							auto key = alt.substr(k, plus == alt.npos ? alt.npos : plus - k);
							k = plus == alt.npos ? alt.size() + 1 : plus + 1;
							if (!firstKey)
							{
								text->Draw(cv, kx, ky + S(2), L"+", Style{ 12, FW_NORMAL, false, false, 0x8A8A8A });
								kx += text->Measure(L"+").cx + S(3);
							}
							firstKey = false;
							const Style cap{ 11, FW_SEMIBOLD, false, false, theme->ink };
							int cw = text->Measure(key, cap).cx + S(12), ch = S(20);
							theme->Button(cv, kx, ky, cw, ch, 0); // a key: the look's push button
							text->Draw(cv, kx + S(6), ky + (ch - text->Measure(key, cap).cy) / 2, key, cap);
							kx += cw + S(3);
						}
						kx += S(4);
					}
					int dx = std::max(x0 + keyW, kx + S(8));
					int end = Para(dx, y + S(1), x1, row.what, body);
					if (row.aside)
					{
						end = Para(dx, end, x1, row.aside, note);
					}
					y = std::max(end, y + S(20)) + S(7);
				}
				y += S(14);
			};
			header(L"Keyboard and mouse");
			section(L"Drawing", 0x2E7D32, {
				{ L"Left drag|Right drag", L"Draw with the left or right material", nullptr },
				{ L"Shift+drag", L"A straight line", L"Add Alt for horizontal, vertical or diagonal only" },
				{ L"Ctrl+drag", L"A filled rectangle", L"Add Alt for a square" },
				{ L"Ctrl+Shift+click", L"Flood-fill a closed area", nullptr },
				{ L"Middle click|Alt+click", L"Sample: pick up the material under the mouse", nullptr },
				{ L"Wheel|[|]", L"Brush size", nullptr },
				{ L"Tab", L"Brush shape", nullptr },
			});
			section(L"Editing", 0x6A1B9A, {
				{ L"Ctrl+C|Ctrl+X|Ctrl+V", L"Copy, cut and paste", nullptr },
				{ L"R", L"Rotate while pasting", L"Shift+R and Shift+Ctrl+R mirror it" },
				{ L"Ctrl+Z", L"Undo", nullptr },
				{ L"Ctrl+Y|Ctrl+Shift+Z", L"Redo", nullptr },
				{ L"S", L"Save part of the screen as a stamp", nullptr },
				{ L"L|K", L"Load the last stamp, or open the stamp library", nullptr },
			});
			section(L"Simulation", 0xC25100, {
				{ L"Space", L"Pause and resume", nullptr },
				{ L"F", L"Step one frame", nullptr },
				{ L"F5", L"Reload the simulation", nullptr },
			});
			section(L"View", 0x00796B, {
				{ L"0 \u2013 9", L"View modes", nullptr },
				{ L"H|D", L"Show the HUD, or debug details in it", nullptr },
				{ L"Z", L"Zoom", L"Click to keep the zoom window; the wheel changes its strength" },
				{ L"Ctrl+F", L"Highlight the selected element", nullptr },
			});
			text->Draw(cv, x0, y, Wide(VersionInfo().FromUtf8()), footer);
			y += S(30);

			std::swap(cv, page);
			welcomeH = y;
			int viewH = c.bottom - c.top - 1;
			scroll = std::clamp(scroll, 0, std::max(0, welcomeH - viewH));
			for (int row = 0; row < viewH && row + scroll < ph; row++)
			{
				std::copy_n(page.px.data() + size_t(row + scroll) * pw, pw, cv.px.data() + size_t(c.top + 1 + row) * cv.w + c.left);
			}
			for (auto &l : links)
			{
				RECT on{ l.rc.left + c.left, l.rc.top + c.top + 1 - scroll, l.rc.right + c.left, l.rc.bottom + c.top + 1 - scroll };
				if (on.top >= c.top && on.bottom <= c.bottom)
				{
					Add(on, l.go, l.tip);
				}
			}
		}

		void PaintOverlay()
		{
			auto c = Content();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
			auto &prefs = GlobalPrefs::Ref();
			auto key = [&](const char *name, const char *fallback) {
				return Platform::WinWiden(prefs.Get(ByteString("Overlay.Keys.") + name, ByteString(fallback)));
			};
			int top = Group(x, y, w, S(100), L"Clicks go to");
#ifdef __APPLE__
			const wchar_t *autoLabel = L"Auto: empty desktop is TPT's, windows and icons keep their clicks";
#else
			const wchar_t *autoLabel = L"Auto: TPT while it's in front, the desktop once another app is";
#endif
			const wchar_t *modes[] = { autoLabel, L"Pass-through: the sim takes no clicks (side windows still do)", L"Locked: everything to TPT" };
			for (int i = 0; i < int(OverlayInput::Count); i++)
			{
				Toggle(x + S(12), top + i * S(24), modes[i], int(OverlayGetInput()) == i, true, [i] { OverlaySetInput(OverlayInput(i)); });
			}
			y += S(108);
			top = Group(x, y, w, S(84), L"Sim layer");
			const wchar_t *layers[] = { L"On top of every window", L"Under windows, over the desktop icons",
			                            L"Behind the desktop icons, like a wallpaper (clicks go to the desktop; Locked comes up to draw)" };
			for (int i = 0; i < int(OverlayLayer::Count); i++)
			{
				Toggle(x + S(12), top + i * S(24), layers[i], int(OverlayGetLayer()) == i, true, [i] { OverlaySetLayer(OverlayLayer(i)); });
			}
			y += S(92);
			top = Group(x, y, w, S(92), L"Window");
			Toggle(x + S(12), top, L"Show TPT's own menus too", showTPTMenus, false, [] {
				showTPTMenus = !showTPTMenus;
				GlobalPrefs::Ref().Set("Overlay.ShowTPTMenus", showTPTMenus);
				if (!overlayWindowed) Restart(ui::Engine::Ref().windowFrameOps.scale, false, true); // they need room on screen
			}, L"Restarts TPT: the sim leaves room for the menus");
			Toggle(x + S(12), top + S(24), L"Pass clicks through while a full-screen app or game is in front", overlayAutoPass, false, [] {
				overlayAutoPass = !overlayAutoPass;
				GlobalPrefs::Ref().Set("Overlay.AutoPassThrough", overlayAutoPass);
			});
#ifdef _WIN32
			// Start with Windows: a value under the user's Run key, pointing at this exe
			static const wchar_t *runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", *runName = L"The Desktop Powder Toy";
			bool startup = RegGetValueW(HKEY_CURRENT_USER, runKey, runName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
			Toggle(x + S(12), top + S(48), L"Start with Windows", startup, false, [startup] {
				HKEY k;
				if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
				if (startup) RegDeleteValueW(k, runName);
				else
				{
					wchar_t exe[MAX_PATH];
					GetModuleFileNameW(nullptr, exe, MAX_PATH);
					std::wstring line = L"\"" + std::wstring(exe) + L"\"";
					RegSetValueExW(k, runName, 0, REG_SZ, (const BYTE *)line.c_str(), DWORD((line.size() + 1) * sizeof(wchar_t)));
				}
				RegCloseKey(k);
			}, L"Opens on the desktop when you sign in");
#else
			Toggle(x + S(12), top + S(48), L"Start at login", OverlayMac::StartsAtLogin(), false, [] { OverlayMac::SetStartsAtLogin(!OverlayMac::StartsAtLogin()); },
			       L"Opens on the desktop when you log in");
#endif
			y += S(100);
			// Apps that always get the clicks while they're in front (Overlay.PassApps: exe names, ';'-separated)
			auto apps = PassApps();
			int ah = S(50) + int(apps.size()) * S(26);
			top = Group(x, y, w, ah, L"Pass clicks through while these apps are in front");
			for (int i = 0; i < int(apps.size()); i++)
			{
				int ry = top + i * S(26);
				Caption(x + S(12), ry, Platform::WinWiden(apps[i]));
				PushButton(x + w - S(100), ry, S(88), L"Remove", [apps, i]() mutable {
					apps.erase(apps.begin() + i);
					SetPassApps(apps);
				}, L"Clicks go back to the overlay's input mode while it's in front");
			}
			{
				int by = top + int(apps.size()) * S(26), bx = x + S(12), bw = S(220);
				PushButton(bx, by, bw, L"Add a running app\u2026", [this, bx, by, bw] {
					later = [this, rc = RECT{ bx, by, bx + bw, by + S(23) }] {
						auto running = RunningApps();
						HMENU m = CreatePopupMenu();
						for (int i = 0; i < int(running.size()); i++) AppendMenuW(m, MF_STRING, UINT(i + 1), Platform::WinWiden(running[i]).c_str());
						if (running.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"No other apps have windows open");
						POINT p{ rc.left, rc.bottom };
						ClientToScreen(hwnd, &p);
						int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, hwnd, nullptr);
						DestroyMenu(m);
						if (cmd > 0)
						{
							auto list = PassApps();
							if (std::find(list.begin(), list.end(), running[cmd - 1]) == list.end()) list.push_back(running[cmd - 1]);
							SetPassApps(list);
						}
						dirty = true;
					};
				}, L"An app with a window open now");
				for (int i = 0, k = S(4), ax = bx + bw - S(20), ay = by + S(10); i <= k; i++) Line(cv, ax + i, ay + i, 2 * (k - i) + 1, 1, theme->ink); // the arrow
			}
			y += ah + S(8);
			top = Group(x, y, w, S(166), L"Shortcuts (any app)");
			std::wstring rows[][2] = {
				{ L"Next input mode", key("CycleInput", "Ctrl+Alt+O") },
				{ L"Pass-through on/off", key("PassThrough", "Ctrl+Alt+P") },
				{ L"Locked on/off", key("Locked", "Ctrl+Alt+L") },
				{ L"Show/hide this window", key("Palette", "Ctrl+Alt+E") },
				{ L"Clear the sim", key("Clear", "Ctrl+Alt+C") },
			};
			const char *names[] = { "CycleInput", "PassThrough", "Locked", "Palette", "Clear" };
			for (int i = 0; i < 5; i++)
			{
				int ry = top + i * S(26);
				Caption(x + S(12), ry, rows[i][0]);
				bool rec = recording == names[i];
				Caption(x + S(200), ry, rec ? L"Press the new keys (Esc cancels)" : rows[i][1], rec ? 0xC25100 : theme->heading);
				PushButton(x + w - S(100), ry, S(88), rec ? L"Cancel" : L"Change\u2026", [rec, name = std::string(names[i])] {
					recording = rec ? "" : name;
				}, L"Ctrl, Alt, Shift or Win plus a letter, digit or F key");
			}
			y += S(174);
			top = Group(x, y, w, S(108), L"Size");
			Caption(x + S(12), top + S(56), L"Runs as");
			int mx = x + S(200);
			mx += Toggle(mx, top + S(56), L"Desktop overlay", !overlayWindowed, true, [] { Restart(ui::Engine::Ref().windowFrameOps.scale, false); },
			             L"Over the desktop (restarts TPT; the scene comes along)") + S(18);
			Toggle(mx, top + S(56), L"Normal window", overlayWindowed, true, [] { Restart(ui::Engine::Ref().windowFrameOps.scale, true); },
			       L"TPT's own window with these side windows; resize it for more or less room (restarts TPT; the scene comes along)");
			// The sim's pixel size: a different sim size, so TPT restarts with the scene carried over.
			Caption(x + S(12), top + S(28), L"Sim pixel size");
			int sx = x + S(200);
			for (int sc = 1; sc <= 6; sc++)
			{
				auto cells = OverlaySimSize(sc).first;
				wchar_t label[32];
				swprintf(label, 32, L"%d\u00D7", sc);
				wchar_t tip[160];
				swprintf(tip, 160, L"%dx%d sim pixels (restarts TPT; the scene comes along, clipped if it doesn't fit)%s", cells.X * CELL, cells.Y * CELL,
				         cells.X * cells.Y * CELL * CELL > MAX_NPART ? L"; at most 4.2 million particles" : L"");
				sx += Toggle(sx, top + S(28), label, ui::Engine::Ref().windowFrameOps.scale == sc, true, [sc] { Restart(sc, overlayWindowed); }, tip) + S(18);
			}
			wchar_t scale[32];
			swprintf(scale, 32, L"UI scale %g\u00D7", uiScale);
			Caption(x + S(12), top, scale);
			auto setScale = [this](float v) {
				float old = uiScale;
				uiScale = std::clamp(v, 1.f, 4.f);
				auto &prefs = GlobalPrefs::Ref();
				RECT wr;
				GetWindowRect(hwnd, &wr);
				prefs.Set("Overlay.Windows.Palette.W", int(std::lround((wr.right - wr.left) * uiScale / old)));
				prefs.Set("Overlay.Windows.Palette.H", int(std::lround((wr.bottom - wr.top) * uiScale / old)));
				GlobalPrefs::Ref().Set("Overlay.UIScale", uiScale);
				rebuildPalette = true;
			};
			PushButton(x + S(200), top, S(40), L"\u2212", [setScale] { setScale(uiScale - 0.25f); }, L"Smaller windows and text");
			PushButton(x + S(246), top, S(40), L"+", [setScale] { setScale(uiScale + 0.25f); }, L"Bigger windows and text");
			y += S(116);
			int current = int(std::find(std::begin(themes), std::end(themes), theme) - std::begin(themes)), gen = Generation(current);
			const std::vector<int> *colours = nullptr;
			for (auto &c : themeColours)
				if (c[0] == gen) colours = &c;
			int colourRows = colours ? int(colours->size() + 2) / 3 : 0; // three to a row
			top = Group(x, y, w, S(140) + colourRows * S(32), L"Look");
			auto family = [&](int row, const wchar_t *label, const std::vector<int> &ids) {
				int by = top + row * S(32), bx = x + S(200), bw = S(220);
				bool in = std::count(ids.begin(), ids.end(), gen) > 0;
				Caption(x + S(12), by, label);
				PushButton(bx, by, bw, std::wstring(in ? themes[gen]->name : L"\u2014"), [this, &ids, bx, by, bw] {
					later = [this, &ids, rc = RECT{ bx, by, bx + bw, by + S(23) }] { PickTheme(ids, rc); };
				}, L"Windows and menus look like this system's");
				for (int i = 0, k = S(4), ax = bx + bw - S(20), ay = by + S(10); i <= k; i++) Line(cv, ax + i, ay + i, 2 * (k - i) + 1, 1, theme->ink); // the arrow
			};
			family(0, L"Windows", windowsThemes);
			family(1, L"Mac", macThemes);
			Toggle(x + S(12), top + 2 * S(32), L"TempleOS", current == templeIndex, true, [] { SetTheme(templeIndex); }, L"Windows and menus look like TempleOS");
			if (colours)
			{
				Caption(x + S(12), top + 3 * S(32), L"Colour");
				int cx = 0;
				for (size_t k = 0; k < colours->size(); k++)
				{
					int i = (*colours)[k], cy = top + int(3 + k / 3) * S(32);
					if (k % 3 == 0) cx = x + S(200);
					cx += Toggle(cx, cy, themes[i]->colour, i == current, true, [i] { SetTheme(i); }, std::wstring(themes[i]->name) + L" in " + themes[i]->colour) + S(18);
				}
			}
			{
				// the gradient wallpaper and its two colours
				int gy = top + (3 + colourRows) * S(32), bx = x + S(200);
				Caption(x + S(12), gy, L"Wallpaper");
				bx += Toggle(bx, gy, L"Gradient", overlayFeel.gradient, false, [this] {
					overlayFeel.gradient = !overlayFeel.gradient;
					GlobalPrefs::Ref().Set("Overlay.Gradient", overlayFeel.gradient);
				}, L"The desktop background as a gradient where it shows (needs windows solid, or the sim under windows). Only drawn by the overlay: your real wallpaper is never changed") + S(18);
				for (auto [c, label, pref] : { std::tuple{ &overlayFeel.gradientTop, L"Top", "Overlay.GradientTop" }, std::tuple{ &overlayFeel.gradientBottom, L"Bottom", "Overlay.GradientBottom" } })
				{
					int bw = S(90);
					PushButton(bx, gy - S(3), bw, label, [this, c, pref] {
						later = [this, c, pref] {
							static COLORREF custom[16] = {};
							CHOOSECOLORW cc{ sizeof(cc) };
							cc.hwndOwner = hwnd, cc.rgbResult = RGB(*c >> 16, (*c >> 8) & 0xFF, *c & 0xFF), cc.lpCustColors = custom;
							cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
							if (ChooseColorW(&cc))
							{
								*c = uint32_t(GetRValue(cc.rgbResult)) << 16 | GetGValue(cc.rgbResult) << 8 | GetBValue(cc.rgbResult);
								GlobalPrefs::Ref().Set(pref, int(*c));
							}
							dirty = true;
						};
					}, std::wstring(L"Pick the gradient's ") + (c == &overlayFeel.gradientTop ? L"top" : L"bottom") + L" colour");
					int sw = S(12);
					cv.RoundRect(bx + S(8), gy - S(3) + (S(23) - sw) / 2, sw, sw, S(2), *c, *c, 255); // the colour
					cv.RoundFrame(bx + S(8), gy - S(3) + (S(23) - sw) / 2, sw, sw, S(2), theme->edge, 255);
					bx += bw + S(10);
				}
			}
			y += S(148) + colourRows * S(32);

			auto &opts = Options();
			top = Group(x, y, w, S(36) + 3 * S(24) + 2 * S(32), L"TPT settings");
			int half = (w - S(24)) / 2, lx = x + S(12);
			Toggle(lx, top, L"Perfect circle brush", opts.GetPerfectCircle(), false, [&opts] { opts.SetPerfectCircle(!opts.GetPerfectCircle()); }, L"Rounder small circle brushes");
			Toggle(lx + half, top, L"Pressure in saves", opts.GetIncludePressure(), false, [&opts] { opts.SetIncludePressure(!opts.GetIncludePressure()); },
			       L"Saves and stamps keep the air pressure");
			Toggle(lx, top + S(24), L"Smooth scrolling", opts.GetMomentumScroll(), false, [&opts] { opts.SetMomentumScroll(!opts.GetMomentumScroll()); }, L"Momentum scrolling in TPT's lists");
			Toggle(lx + half, top + S(24), L"Quit without asking", opts.GetFastQuit(), false, [&opts] { opts.SetFastQuit(!opts.GetFastQuit()); }, L"No confirmation when closing TPT");
			Toggle(lx, top + 2 * S(24), L"Show avatars", opts.GetShowAvatars(), false, [&opts] { opts.SetShowAvatars(!opts.GetShowAvatars()); }, L"Profile pictures in the online browser");
			int ry = top + 3 * S(24) + S(4);
			Caption(lx, ry + S(3), L"Deco colour space");
			ListButton(x + S(220), ry, std::min(S(200), x + w - S(232)), { L"sRGB", L"Linear", L"Gamma 2.2", L"Gamma 1.8" }, opts.GetDecoSpace(),
			           [&opts](int i) { opts.SetDecoSpace(i); }, L"How deco colours blend");
			PushButton(lx, ry + S(32), S(200), L"All of TPT's options\u2026", [] { GameController::Ref().OpenOptions(); }, L"TPT's own settings window: everything else");
		}

		// TPT's settings, through its own options model (which saves the prefs).
		static OptionsModel &Options()
		{
			static OptionsModel options(GameController::Ref().GetModel());
			return options;
		}

		// A setting picked from a list: the choice on a button, the list as a native menu under it.
		void ListButton(int bx, int by, int bw, std::vector<std::wstring> names, int current, std::function<void(int)> set, const std::wstring &tip)
		{
			PushButton(bx, by, bw, current >= 0 && current < int(names.size()) ? names[current] : L"\u2014", [this, names, current, set, bx, by, bw] {
				later = [this, names, current, set, rc = RECT{ bx, by, bx + bw, by + S(23) }] {
					HMENU m = CreatePopupMenu();
					for (int i = 0; i < int(names.size()); i++) AppendMenuW(m, MF_STRING | (i == current ? MF_CHECKED : 0), UINT(i + 1), names[i].c_str());
					POINT p{ rc.left, rc.bottom };
					ClientToScreen(hwnd, &p);
					int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, hwnd, nullptr);
					DestroyMenu(m);
					if (cmd > 0) set(cmd - 1);
					dirty = true;
				};
			}, tip);
			for (int i = 0, k = S(4), ax = bx + bw - S(20), ay = by + S(10); i <= k; i++) Line(cv, ax + i, ay + i, 2 * (k - i) + 1, 1, theme->ink); // the arrow
		}

		// A number setting: the value on a button; a click asks for a new one. use(text, apply): is it valid, and
		// when apply, set it.
		void NumberButton(int bx, int by, int bw, const String &title, const String &value, std::function<bool(const String &, bool)> use, const std::wstring &tip)
		{
			PushButton(bx, by, bw, Wide(value), [this, title, value, use] {
				later = [this, title, value, use] {
					int none = 0;
					String text = value;
					if (OverlayWindows::Form(title, title + ":", {}, none, text, {}, [use](int, const String &v) { return use(v, false); }) == 1) use(text, true);
					dirty = true;
				};
			}, tip);
		}

		// A float typed in, within [lo, hi].
		static std::function<bool(const String &, bool)> FloatSetting(float lo, float hi, std::function<float(const String &)> parse, std::function<void(float)> set)
		{
			return [=](const String &s, bool apply) {
				try
				{
					float v = parse(s);
					if (!(v >= lo && v <= hi)) return false;
					if (apply) set(v);
					return true;
				}
				catch (const std::exception &)
				{
					return false;
				}
			};
		}

		// A theme family's list as a menu under its button.
		void ScanLocals()
		{
			std::vector<Local> found;
			auto dir = ByteString::Build(LOCAL_SAVE_DIR, PATH_SEP_CHAR);
			auto files = Platform::DirectorySearch(dir, "", { ".cps" });
			std::sort(files.begin(), files.end(), [](const ByteString &a, const ByteString &b) { return a.ToLower() < b.ToLower(); });
			for (auto &file : files)
			{
				auto name = file.SplitFromEndBy(PATH_SEP_CHAR).After().SplitFromEndBy('.').Before();
				found.push_back({ dir + file, "", Wide(name.FromUtf8()) });
			}
			Client::Ref().RescanStamps();
			for (auto &id : Client::Ref().GetStamps()) // newest first, as TPT lists them
				found.push_back({ ByteString::Build(STAMPS_DIR, PATH_SEP_CHAR, id, ".stm"), id, Platform::WinWiden(id) });
			for (auto &l : found)
				for (auto &old : locals)
					if (old.path == l.path) l.thumb = std::move(old.thumb), l.tried = old.tried;
			if (found.size() != locals.size()) dirty = true;
			locals = std::move(found);
		}

		void TickLocals()
		{
			if (tab != savesTab || !IsWindowVisible(hwnd)) return;
			TickOnline();
			if (GetTickCount() - localsAt > 2000)
			{
				ScanLocals();
				localsAt = GetTickCount();
			}
			for (auto &l : locals)
			{
				if (l.tried) continue;
				// ponytail: rendered on TPT's thread, one per tick; a big save is a short hitch. TPT's own
				// ThumbnailRendererTask threads it if that shows
				l.tried = true;
				SaveFile file(l.path, true);
				if (auto *save = file.LazyGetGameSave())
				{
					RendererSettings settings;
					l.thumb = SaveRenderer::Ref().Render(save, false, settings);
					if (l.thumb) l.thumb->ResizeToFit({ S(124), S(78) }, true);
				}
				dirty = true;
				break;
			}
		}

		static void OpenLocal(const ByteString &path, const ByteString &stamp)
		{
			auto &gc = GameController::Ref();
			if (stamp.size())
			{
				auto file = Client::Ref().GetStamp(stamp);
				if (!file || !file->GetGameSave()) new ErrorMessage("Error loading stamp", file ? file->GetError() : String("Not found"));
				else
				{
					Client::Ref().MoveStampToFront(stamp);
					gc.LoadStamp(file->TakeGameSave());
				}
				return;
			}
			auto file = std::make_unique<SaveFile>(path, true);
			if (!file->LazyGetGameSave())
			{
				new ErrorMessage("Error loading save", file->GetError());
				return;
			}
			file->SetDisplayName(path.SplitFromEndBy(PATH_SEP_CHAR).After().SplitFromEndBy('.').Before().FromUtf8());
			gc.HistorySnapshot();
			gc.LoadSaveFile(std::move(file));
		}

		static std::wstring FullPath(const ByteString &path)
		{
			wchar_t full[MAX_PATH];
			return GetFullPathNameW(Platform::WinWiden(path).c_str(), MAX_PATH, full, nullptr) ? full : Platform::WinWiden(path);
		}

		void LocalMenu(const ByteString &path, const ByteString &stamp, const std::wstring &name)
		{
			HMENU m = CreatePopupMenu();
			AppendMenuW(m, MF_STRING | MF_DEFAULT, 1, stamp.size() ? L"Place" : L"Open");
			AppendMenuW(m, MF_STRING, 2, L"Show in folder");
			AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(m, MF_STRING, 3, L"Delete\u2026");
			POINT p;
			GetCursorPos(&p);
			int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
			DestroyMenu(m);
			if (cmd == 1) OpenLocal(path, stamp);
			if (cmd == 2) ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + FullPath(path) + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
			if (cmd == 3 && MessageBoxW(hwnd, (L"Delete " + name + L"? This can't be undone.").c_str(), L"The Powder Toy", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
			{
				if (stamp.size()) Client::Ref().DeleteStamp(stamp);
				else Platform::RemoveFile(path);
				localsAt = 0; // list again now
			}
		}

		// A .cps dropped on the window opens; a .stm is placed as a stamp.
		void Drop(const std::wstring &path) override
		{
			auto p = Platform::WinNarrow(path);
			auto ext = p.SplitFromEndBy('.').After().ToLower();
			if (ext == "cps") OpenLocal(p, "");
			else if (ext == "stm")
			{
				std::vector<char> data;
				if (Platform::ReadFile(data, p))
				{
					try
					{
						GameController::Ref().LoadStamp(std::make_unique<GameSave>(std::move(data)));
					}
					catch (std::exception &e)
					{
						new ErrorMessage("Error loading stamp", ByteString(e.what()).FromUtf8());
					}
				}
			}
		}

		// Windows' own colour dialog; the alpha stays as it was.
		void PickDecoColour()
		{
			static COLORREF custom[16] = {}; // the dialog's custom colours, for this run
			auto &gc = GameController::Ref();
			auto col = gc.GetModel()->GetColourSelectorColour();
			CHOOSECOLORW cc{ sizeof(cc) };
			cc.hwndOwner = hwnd, cc.rgbResult = RGB(col.Red, col.Green, col.Blue), cc.lpCustColors = custom;
			cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
			if (ChooseColorW(&cc)) gc.SetColour(ui::Colour(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult), col.Alpha));
			dirty = true;
		}

		void PickTheme(const std::vector<int> &ids, RECT rc)
		{
			HMENU m = CreatePopupMenu();
			for (int i : ids) AppendMenuW(m, MF_STRING | (Generation(int(std::find(std::begin(themes), std::end(themes), theme) - std::begin(themes))) == i ? MF_CHECKED : 0), UINT(i + 1), themes[i]->name);
			POINT p{ rc.left, rc.bottom };
			ClientToScreen(hwnd, &p);
			// ponytail: modal here, so the sim waits while the menu is open; NativeMenu's thread if that matters
			int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, hwnd, nullptr);
			DestroyMenu(m);
			if (cmd > 0) SetTheme(cmd - 1);
		}

		void Paint() override
		{
			hits.clear();
			theme->Frame(cv, titleH, border, fixedTab >= 0 ? tabNames[fixedTab] : L"The Powder Toy");
			theme->CaptionButton(cv, MinRect(), Hovered(MinRect()), CaptionMin);
			theme->CaptionButton(cv, MaxRect(), Hovered(MaxRect()), CaptionMax);
			theme->CaptionButton(cv, CloseRect(), Hovered(CloseRect()), CaptionClose);
			Add(MinRect(), [this] { ShowWindow(hwnd, SW_MINIMIZE); }, L"Minimise to the taskbar");
			Add(MaxRect(), [this] { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); }, IsZoomed(hwnd) ? L"Restore" : L"Maximise");
			if (fixedTab >= 0) Add(CloseRect(), [this] { closing = true; }, L"Close (the tab is still in the main window)");
			else Add(CloseRect(), [] { OverlayWindows::TogglePalette(); }, L"Close (the tray icon or Ctrl+Alt+E brings it back)");

			// The tab strip: the selected tab joined to the page.
			int tx = border + S(4);
			cv.RoundRect(border, titleH, cv.w - 2 * border, tabH, 0, theme->strip, theme->strip, theme->stripAlpha);
			// padding around each tab's name, less when they would not all fit
			int pad = S(20);
			if (fixedTab < 0)
			{
				int names = 0;
				for (int i = 0; i < int(std::size(tabNames)); i++)
					if (i != buddyTab || BuddyTabShown()) names += text->Measure(tabNames[i]).cx + S(1);
				pad = std::clamp((cv.w - 2 * tx - names) / int(std::size(tabNames)), S(6), S(20));
			}
			for (int i : tabOrder)
			{
				if (fixedTab >= 0 && i != fixedTab) continue;
				if (i == buddyTab && !BuddyTabShown())
				{
					if (tab == buddyTab) tab = 6; // his file was taken away: back to Fun
					continue;
				}
				int tw = text->Measure(tabNames[i]).cx + pad;
				bool on = i == tab;
				RECT rc{ tx, titleH + (on ? S(2) : S(4)), tx + tw, titleH + tabH + (on ? 1 : 0) };
				bool hot = !on && Hovered(rc);
				theme->Tab(cv, rc, on, hot);
				text->Draw(cv, tx + pad / 2, rc.top + (rc.bottom - rc.top - text->Measure(tabNames[i]).cy) / 2 - (theme->segmentTabs ? S(1) : 0), tabNames[i], theme->TabInk(i == tab));
				if (fixedTab < 0) Add(rc, [this, i] { tab = i, scroll = 0; }, L"Right-click: open this tab in its own window", [i] { OpenTorn(i); });
				tx += tw + S(1);
			}
			// The page.
			auto c = Content();
			cv.RoundRect(c.left, c.top, c.right - c.left, c.bottom - c.top, 0, theme->page, theme->page, 255);
			for (int i = c.left; i < c.right; i++) cv.Blend(i, c.top, theme->edge, 255);
			// The selected tab's bottom edge opens into the page.
			int sx = border + S(4);
			for (int i : tabOrder)
			{
				if (i == tab || fixedTab >= 0) break;
				if (i != buddyTab || BuddyTabShown()) sx += text->Measure(tabNames[i]).cx + pad + S(1);
			}
			if (!theme->segmentTabs)
				for (int i = sx + 1; i < sx + text->Measure(tabNames[tab]).cx + pad - S(1); i++) cv.Blend(i, c.top, theme->page, 255);

			// Form pages scroll: rows outside the page are put back after, and controls there dropped.
			bool form = (tab >= 1 && tab <= 6) || tab == buddyTab;
			size_t firstPageHit = hits.size();
			size_t topEnd = size_t(c.top + 1) * cv.w, bottomStart = size_t(c.bottom) * cv.w;
			std::vector<uint32_t> above(cv.px.begin(), cv.px.begin() + topEnd), below(cv.px.begin() + bottomStart, cv.px.end());
			switch (tab)
			{
			case 0: PaintElements(); break;
			case 2: PaintSimulation(); break;
			case 3: PaintDisplay(); break;
			case 4: PaintSaves(); break;
			case 5: PaintOverlay(); break;
			case 6: PaintFun(funRows, std::size(funRows)); break;
			case buddyTab: PaintFun(buddyRows, std::size(buddyRows)); break;
			case welcomeTab: PaintWelcome(); break;
			case toolsTab: PaintTools(); break;
			}
			if (form)
			{
				std::copy(above.begin(), above.end(), cv.px.begin());
				std::copy(below.begin(), below.end(), cv.px.begin() + bottomStart);
				int bottom = c.top;
				for (size_t i = firstPageHit; i < hits.size(); i++) bottom = std::max(bottom, int(hits[i].rc.bottom));
				formH = bottom + scroll - c.top + S(20);
				hits.erase(std::remove_if(hits.begin() + firstPageHit, hits.end(), [&](const Hit &h) {
					return h.rc.top < c.top + 1 || h.rc.bottom > c.bottom;
				}), hits.end());
			}

			// The status bar: what the hovered control does.
			int sy = cv.h - border - statusH;
			cv.RoundRect(border, sy, cv.w - 2 * border, statusH, 0, theme->status[0], theme->status[1], 255);
			for (int i = border; i < cv.w - border; i++) cv.Blend(i, sy, theme->status[2], 255);
			if (hover >= 0 && hover < int(hits.size()))
			{
				text->Draw(cv, border + S(8), sy + S(3), hits[hover].tip, theme->heading, false, cv.w - 2 * border - S(16));
			}
		}

		LRESULT HitTest(int x, int y) override
		{
			return y < titleH && !Inside(CloseRect(), x, y) && !Inside(MaxRect(), x, y) && !Inside(MinRect(), x, y) ? HTCAPTION : HTCLIENT;
		}

		void Mouse(UINT msg, int x, int y, WPARAM wp) override
		{
			int at = -1;
			if (msg != WM_MOUSELEAVE)
			{
				for (int i = 0; i < int(hits.size()); i++)
					if (Inside(hits[i].rc, x, y)) at = i;
			}
			if (at != hover)
			{
				hover = at;
				dirty = true;
			}
			if (at >= 0 && ((msg == WM_LBUTTONUP) || (msg == WM_MOUSEMOVE && (wp & MK_LBUTTON) && hits[at].drag)) && hits[at].left)
			{
				auto action = hits[at].left; // the action may repaint and rebuild hits
				action();
				dirty = true;
			}
			if (at >= 0 && msg == WM_RBUTTONUP && hits[at].right)
			{
				auto action = hits[at].right;
				action();
				dirty = true;
			}
			if (msg == WM_MOUSEWHEEL && tab == welcomeTab)
			{
				auto c = Content();
				int maxScroll = std::max(0, welcomeH - int(c.bottom - c.top));
				scroll = std::clamp(scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(51), 0, maxScroll);
				dirty = true;
			}
			if (msg == WM_MOUSEWHEEL && tab >= 1 && tab <= 6)
			{
				auto c = Content();
				scroll = std::clamp(scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(40), 0, std::max(0, formH - int(c.bottom - c.top)));
				dirty = true;
			}
			if (msg == WM_MOUSEWHEEL && tab == 0)
			{
				auto tools = Tools();
				auto area = ToolArea();
				int rows = (int(tools.size()) + Columns() - 1) / Columns();
				int maxScroll = std::max(0, rows * (tileH + gap) - int(area.bottom - area.top));
				scroll = std::clamp(scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * (tileH + gap), 0, maxScroll);
				dirty = true;
			}
		}

		// Repaint when anything shown changes, from here, from TPT's keys, or from Lua.
		void Tick()
		{
			TickLocals();
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			auto *sim = model->GetSimulation();
			size_t count = 0;
			for (auto *menu : gc.GetMenuList()) count += menu->GetToolList().size();
			auto a = sim->playArea;
			uint64_t state = 1469598103934665603ULL;
			auto mix = [&](uint64_t v) { state = (state ^ v) * 1099511628211ULL; };
			mix(uint64_t(uintptr_t(gc.GetActiveTool(0))));
			mix(uint64_t(uintptr_t(gc.GetActiveTool(1))));
			auto deco = model->GetColourSelectorColour(); // TPT's picker or Lua changed the deco colour
			mix(uint64_t(deco.Red) << 24 | uint64_t(deco.Green) << 16 | uint64_t(deco.Blue) << 8 | deco.Alpha);
			for (uint64_t v : { uint64_t(count), uint64_t(gc.GetPaused()), uint64_t(gc.GetEdgeMode()), uint64_t(sim->gravityMode),
			                    uint64_t(sim->air->airMode), uint64_t(model->GetNewtonianGrvity()), uint64_t(gc.GetAHeatEnable()),
			                    uint64_t(gc.GetHudEnable()), uint64_t(model->GetDecoration()), uint64_t(model->GetGravityGrid()),
			                    uint64_t(OverlayGetInput()), uint64_t(showTPTMenus), uint64_t(a.pos.X), uint64_t(a.pos.Y),
			                    uint64_t(a.size.X), uint64_t(a.size.Y) })
			{
				mix(v);
			}
			if (state != shownState)
			{
				shownState = state;
				dirty = true;
			}
		}
	};

	// A small glass notice ("Input: Locked") that fades out.
	struct Toast : Panel
	{
		std::wstring message;
		DWORD shownAt = 0;

		Toast()
		{
			name = "Toast";
		}

		void Paint() override
		{
			theme->Popup(cv);
			auto size = text->Measure(message, true);
			text->Draw(cv, (cv.w - size.cx) / 2, (cv.h - size.cy) / 2, message, 0x000000, true, cv.w, theme->glow ? S(float(theme->glow)) : 0);
		}

		LRESULT HitTest(int, int) override
		{
			return HTTRANSPARENT;
		}

		void Show(HWND over, const std::wstring &newMessage)
		{
			message = newMessage;
			auto size = text->Measure(message, true);
			int w = size.cx + S(48), h = S(40);
			RECT rc;
			GetWindowRect(over, &rc);
			int x = (rc.left + rc.right - w) / 2, y = rc.top + S(40);
			if (!hwnd)
			{
				Create(x, y, w, h);
				SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
			}
			cv.Resize(w, h);
			SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
			shownAt = GetTickCount();
			dirty = true;
		}

		void Tick()
		{
			if (!hwnd || !shownAt) return;
			auto age = GetTickCount() - shownAt;
			if (age > 1800)
			{
				ShowWindow(hwnd, SW_HIDE);
				shownAt = 0;
				return;
			}
			opacity = age < 1200 ? 255 : int(255 * (1800 - age) / 600);
			Present();
		}
	};

	std::unique_ptr<SideWindow> palette;
	std::vector<std::unique_ptr<SideWindow>> torn; // tabs in their own windows (Overlay.Windows.Torn, a bit per tab)

	void DirtyAll()
	{
		if (palette) palette->dirty = true;
		for (auto &t : torn) t->dirty = true;
	}

	void SaveTorn()
	{
		int mask = 0;
		for (auto &t : torn) mask |= 1 << t->fixedTab;
		GlobalPrefs::Ref().Set("Overlay.Windows.Torn", mask);
	}

	// A window's saved spot and size (Overlay.Windows.<name>.*), else the default, kept on its monitor's work area.
	RECT PlaceWindow(const ByteString &name, int x, int y, int w, int h, int minW, int minH)
	{
		auto &prefs = GlobalPrefs::Ref();
		POINT saved{ prefs.Get("Overlay.Windows." + name + ".X", x), prefs.Get("Overlay.Windows." + name + ".Y", y) };
		POINT grip{ saved.x + w / 2, saved.y + S(10) }; // the title bar must be on a monitor
		if (MonitorFromPoint(grip, MONITOR_DEFAULTTONULL))
		{
			x = saved.x;
			y = saved.y;
		}
		w = std::max(prefs.Get("Overlay.Windows." + name + ".W", w), minW);
		h = std::max(prefs.Get("Overlay.Windows." + name + ".H", h), minH);
		// The whole window on its monitor's work area (a saved spot can hang off the edge).
		MONITORINFO mi{ sizeof(mi) };
		GetMonitorInfoW(MonitorFromPoint({ x + w / 2, y + S(10) }, MONITOR_DEFAULTTONEAREST), &mi);
		auto work = mi.rcWork;
		w = std::min(w, int(work.right - work.left));
		h = std::min(h, int(work.bottom - work.top));
		x = std::clamp(x, int(work.left), int(work.right) - w);
		y = std::clamp(y, int(work.top), int(work.bottom) - h);
		return { x, y, x + w, y + h };
	}

	void ApplyPassThrough(Panel &panel);

	// The scene carried across a restart or to the next run: saves of at most 255x255 cells (the save format's limit),
	// named by their top-left cell, next to powder.pref.
	std::wstring SceneTile(int x, int y)
	{
		return L"overlay-scene-" + std::to_wstring(x) + L"-" + std::to_wstring(y) + L".cps";
	}

	bool sceneSaved = false; // a restart saved it already, and the new TPT is loading it

	// In cells. A save bigger than the sim is refused outright, and the sim shrinks when the pixel size goes up.
	constexpr int sceneTile = 24;

	void SaveSceneTiles()
	{
		if (sceneSaved) return;
		sceneSaved = true;
		auto *model = GameController::Ref().GetModel();
		auto *sim = model->GetSimulation();
		std::vector<bool> used(size_t(XCELLS) * YCELLS);
		for (int i = 0; i < sim->parts.active; i++)
			if (sim->parts[i].type)
			{
				int x = int(sim->parts[i].x + 0.5f) / CELL, y = int(sim->parts[i].y + 0.5f) / CELL;
				if (x >= 0 && y >= 0 && x < XCELLS && y < YCELLS) used[size_t(y) * XCELLS + x] = true;
			}
		for (int y = 0; y < YCELLS; y += sceneTile)
			for (int x = 0; x < XCELLS; x += sceneTile)
			{
				auto tile = RectSized(Vec2(x, y), Vec2(std::min(sceneTile, XCELLS - x), std::min(sceneTile, YCELLS - y)));
				bool any = false; // most of a desktop is empty
				for (int cy = y; cy < y + tile.size.Y; cy++)
					for (int cx = x; cx < x + tile.size.X; cx++) any = any || used[size_t(cy) * XCELLS + cx] || sim->bmap[cy][cx];
				if (!any) continue;
				auto save = sim->Save(true, RectSized(tile.pos * CELL, tile.size * CELL));
				if (!save) continue;
				save->paused = model->GetPaused();
				auto data = save->Serialise().second; // .first is not success (TPT's own save path ignores it too)
				if (!data.empty()) Platform::WriteFile(data, Platform::WinNarrow(SceneTile(x, y)));
			}
		GlobalPrefs::Ref().Set("Overlay.PendingScene", true);
	}

	void LoadSceneTiles()
	{
		auto *model = GameController::Ref().GetModel();
		auto *sim = model->GetSimulation();
		GameController::Ref().GetView()->PauseRendererThread();
		for (int y = 0; y < YCELLS; y += sceneTile)
			for (int x = 0; x < XCELLS; x += sceneTile)
			{
				auto file = Platform::WinNarrow(SceneTile(x, y));
				std::vector<char> data;
				if (!Platform::FileExists(file) || !Platform::ReadFile(data, file)) continue;
				try
				{
					GameSave save(data);
					sim->Load(&save, true, Vec2(x, y));
				}
				catch (std::exception &) {} // a bad tile: that part of the scene is lost
				DeleteFileW(SceneTile(x, y).c_str());
			}
		// tiles past the edge of a smaller sim: gone, or a later run at a bigger size would bring old sand back
		for (auto &file : Platform::DirectorySearch(".", "overlay-scene-", { ".cps" }))
			Platform::RemoveFile(file);
	}

	// Saves the scene, sets the new pixel size and starts a fresh TPT, which loads the scene; this one quits.
	// The other pixel size or mode: the scene goes along in tiles, the new TPT loads it, this one quits.
	void Restart(int scale, bool windowed, bool force)
	{
		if (!force && scale == ui::Engine::Ref().windowFrameOps.scale && windowed == overlayWindowed) return;
		SaveSceneTiles();
		GlobalPrefs::Ref().Set("Scale", scale);
#ifdef _WIN32
		// The same command line, with overlay:on or overlay:window.
		wchar_t exe[MAX_PATH];
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring line = GetCommandLineW();
		for (auto *arg : { L" overlay:window", L" overlay:on" })
			if (auto at = line.find(arg); at != std::wstring::npos) line.erase(at, wcslen(arg));
		line += windowed ? L" overlay:window" : L" overlay:on";
		STARTUPINFOW si{ sizeof(si) };
		PROCESS_INFORMATION pi{};
		if (CreateProcessW(exe, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
		{
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			SDL_Event quit{};
			quit.type = SDL_QUIT;
			SDL_PushEvent(&quit);
		}
#else
		GlobalPrefs::Ref().Set("Overlay.Windowed", windowed);
		if (OverlayMac::Relaunch(nullptr))
		{
			SDL_Event quit{};
			quit.type = SDL_QUIT;
			SDL_PushEvent(&quit);
		}
#endif
	}

	void OpenTorn(int tab)
	{
		for (auto &t : torn)
			if (t->fixedTab == tab)
			{
				SetWindowPos(t->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				return;
			}
		auto w = std::make_unique<SideWindow>();
		w->fixedTab = w->tab = tab;
		w->name = ByteString("Tab") + ByteString(std::to_string(tab));
		w->title = SideWindow::tabNames[tab];
		POINT cur;
		GetCursorPos(&cur);
		auto r = PlaceWindow(w->name, cur.x - S(60), cur.y - S(12), S(560), S(560), S(360), S(300));
		w->grip = w->border;
		w->minSize = { S(360), S(300) };
		w->Create(r.left, r.top, r.right - r.left, r.bottom - r.top);
		w->Present();
		ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
		SetWindowPos(w->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		ApplyPassThrough(*w);
		torn.push_back(std::move(w));
		SaveTorn();
	}
	std::unique_ptr<Toast> toast;
	bool passThrough = false;

	// A TPT window (dialog, browser, menu) in its own app window. TPT still draws it (Engine::Hosted, every frame);
	// we show it at a whole-pixel scale in a Vista frame and send the mouse back. Menus get no frame.
	// A TPT dialog's controls as plain data, read each frame (TPT can delete a window between frames, so the
	// host never keeps pointers into it). Rects are in the dialog's own pixels.
	struct SkinItem
	{
		ui::Component::Kind kind;
		int x, y, w, h, clipT, clipB;
		std::wstring text;
		uint32_t rgb = 0; // a label's colour
		int align = 0;    // 0 left, 1 centre, 2 right
		bool enabled = true, hover = false, down = false, checked = false, focused = false, multiline = false;
		int value = 0, steps = 0;
		int caret = 0, selA = 0, selB = 0; // text boxes: cursor and selection, in characters
		bool operator==(const SkinItem &) const = default;
	};

	// TPT text minus its formatting codes; \x01 marks a switch to or from grey (TPT's "\bg").
	std::wstring Plain(const String &s, bool &tptGlyphs)
	{
		String out;
		for (size_t i = 0; i < s.size(); i++)
		{
			auto c = s[i];
			if (c == 0x0F) { i += 3; continue; }
			if (c == '\b' && i + 1 < s.size())
			{
				out.push_back(s[++i] == 'g' ? 1 : 2);
				continue;
			}
			if (c == 0x0E || c == 0x01) continue;
			if (c >= 0xE000 && c <= 0xF8FF) tptGlyphs = true; // TPT's own icon font
			out += c;
		}
		return Wide(out);
	}

	void SkinWalk(ui::Window *window, const std::vector<ui::Component *> &list, int clipT, int clipB, std::vector<SkinItem> &out)
	{
		using K = ui::Component::Kind;
		for (auto *c : list)
		{
			if (!c->Visible) continue;
			auto pos = c->GetScreenPos() - window->Position;
			SkinItem it{ c->GetKind(), pos.X, pos.Y, c->Size.X, c->Size.Y, clipT, clipB };
			it.enabled = c->Enabled;
			it.hover = c->MouseInside;
			it.down = c->MouseDownInside;
			it.focused = window->IsFocused(c);
			it.align = c->Appearance.HorizontalAlign == ui::Appearance::AlignCentre ? 1 : c->Appearance.HorizontalAlign == ui::Appearance::AlignRight ? 2 : 0;
			bool glyphs = false;
			switch (it.kind)
			{
			case K::Panel:
			{
				auto *p = static_cast<ui::Panel *>(c);
				std::vector<ui::Component *> kids;
				for (int i = 0; i < p->GetChildCount(); i++) kids.push_back(p->GetChild(i));
				// a scroll bar for a panel taller than its view
				it.value = -p->ViewportPosition.Y;
				it.steps = p->InnerSize.Y;
				out.push_back(it);
				SkinWalk(window, kids, std::max(clipT, pos.Y), std::min(clipB, pos.Y + c->Size.Y), out);
				continue;
			}
			case K::Label:
			case K::Textbox:
			{
				auto *l = static_cast<ui::Label *>(c);
				it.text = Plain(l->GetText(), glyphs);
				auto col = l->GetTextColour();
				it.rgb = (col.Red << 16) | (col.Green << 8) | col.Blue;
				it.multiline = l->GetMultiline();
				if (it.kind == K::Textbox)
				{
					auto *t = static_cast<ui::Textbox *>(c);
					if (t->GetHidden()) it.text = std::wstring(it.text.size(), L'\u25CF');
					it.caret = std::clamp(t->GetCursor(), 0, int(it.text.size()));
					if (t->HasSelection())
					{
						it.selA = std::clamp(t->GetSelectionStart(), 0, int(it.text.size()));
						it.selB = std::clamp(t->GetSelectionEnd(), 0, int(it.text.size()));
					}
				}
				break;
			}
			case K::Button:
			{
				auto *b = static_cast<ui::Button *>(c);
				it.text = Plain(b->GetText(), glyphs);
				it.checked = b->GetTogglable() && b->GetToggleState();
				if (c->Appearance.icon != NoIcon) glyphs = true;
				auto bg = c->Appearance.BackgroundInactive;
				if (bg.Red || bg.Green || bg.Blue) glyphs = true; // a colour swatch
				break;
			}
			case K::Checkbox:
				it.text = Plain(static_cast<ui::Checkbox *>(c)->GetText(), glyphs);
				it.checked = static_cast<ui::Checkbox *>(c)->GetChecked();
				break;
			case K::DropDown:
				it.text = Plain(static_cast<ui::DropDown *>(c)->GetOption().first, glyphs);
				break;
			case K::Slider:
				it.value = static_cast<ui::Slider *>(c)->GetValue();
				it.steps = static_cast<ui::Slider *>(c)->GetSteps();
				break;
			case K::ProgressBar:
				it.value = static_cast<ui::ProgressBar *>(c)->GetProgress();
				break;
			default:
				break;
			}
			if (c->Appearance.icon != NoIcon) glyphs = true;
			if (glyphs) it.kind = K::Other; // keep TPT's pixels for icon buttons and the like
			out.push_back(it);
		}
	}

	struct HostWindow : Panel
	{
		ui::Window *window;
		bool framed;
		int scale = 1, titleH = 0, border = 0;
		ui::Point pos{ 0, 0 }, size{ 0, 0 };
		std::vector<pixel> pixels;
		std::vector<SkinItem> items; // empty: show TPT's pixels as they are
		bool hoverClose = false;

		HostWindow(ui::Window *window) : window(window)
		{
			framed = window->Framed;
			name = "Host";
			title = window->Title.empty() ? L"The Powder Toy" : Wide(window->Title);
			if (framed)
			{
				titleH = S(28);
				border = S(6);
			}
		}

		ui::Point ClientOrigin() { return { border, titleH }; }
		RECT CloseRect() { return theme->CaptionRect(cv.w, border, CaptionClose); }
		bool OnTop() { return ui::Engine::Ref().GetWindow() == window; }

		// TPT's pixels for one rect (dialog pixels), scaled, between rows clipT and clipB.
		void Blit(int x, int y, int w, int h, int clipT, int clipB)
		{
			for (int ty = std::max(y, clipT); ty < std::min(y + h, clipB); ty++)
			{
				if (ty < 0 || ty >= size.Y) continue;
				for (int sy = 0; sy < scale; sy++)
				{
					int dy = titleH + ty * scale + sy;
					if (dy >= cv.h) break;
					for (int tx = std::max(x, 0); tx < std::min(x + w, size.X); tx++)
					{
						auto p = pixels[size_t(ty) * size.X + tx] | 0xFF000000;
						for (int sx = 0; sx < scale; sx++)
						{
							int dx = border + tx * scale + sx;
							if (dx < cv.w) cv.px[size_t(dy) * cv.w + dx] = p;
						}
					}
				}
			}
		}

		// The dialog in Vista's colours and Segoe UI, from the controls' data; anything unknown keeps TPT's pixels.
		void PaintSkin()
		{
			using K = ui::Component::Kind;
			using Style = Text::Style;
			int k = scale, ox = border, oy = titleH;
			cv.RoundRect(ox, oy, size.X * k, size.Y * k, 0, 0xF0F0F0, 0xF0F0F0, 255);
			const Style ui{ 12, FW_NORMAL, false, false, 0x000000, 9 * k };
			int lineH = text->Measure(L"Ag", ui).cy;
			// A text run: \x01 toggles grey, \x02 back to the base colour.
			auto runs = [&](int x, int y, int right, const std::wstring &s, uint32_t base, int align, bool wrap) {
				std::vector<std::pair<std::wstring, uint32_t>> words;
				uint32_t col = base;
				std::wstring w;
				auto flush = [&] {
					if (!w.empty()) words.push_back({ w, col });
					w.clear();
				};
				for (auto ch : s)
				{
					if (ch == 1) { flush(); col = 0x6D6D6D; }
					else if (ch == 2) { flush(); col = base; }
					else if (ch == L' ' || ch == L'\n') { w += ch; flush(); }
					else w += ch;
				}
				flush();
				int total = 0;
				for (auto &[t, c] : words) total += text->Measure(t, ui).cx;
				int cx = !wrap && align == 1 ? x + (right - x - total) / 2 : !wrap && align == 2 ? right - total : x;
				for (auto &[t, c] : words)
				{
					int tw = text->Measure(t, ui).cx;
					if (wrap && cx > x && (cx + tw > right || (!t.empty() && t.back() == L'\n')))
					{
						if (cx + tw > right) { cx = x; y += lineH; }
					}
					Style st = ui;
					st.rgb = c;
					text->Draw(cv, cx, y, t, st);
					cx += tw;
					if (wrap && !t.empty() && t.back() == L'\n') { cx = x; y += lineH; }
				}
			};
			// TPT's colours are for white-on-black; keep a hue, darken light ones.
			auto ink = [](uint32_t rgb) -> uint32_t {
				int r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
				if (std::abs(r - g) < 24 && std::abs(g - b) < 24) return r > 200 ? 0x000000 : 0x6D6D6D;
				return (r * 45 / 100) << 16 | (g * 45 / 100) << 8 | (b * 45 / 100);
			};
			for (auto &it : items)
			{
				int x = ox + it.x * k, y = oy + it.y * k, w = it.w * k, h = it.h * k;
				bool inside = it.y >= it.clipT && it.y + it.h <= it.clipB;
				if (it.kind == K::Other)
				{
					Blit(it.x, it.y, it.w, it.h, it.clipT, it.clipB);
					continue;
				}
				if (it.kind == K::Panel)
				{
					if (it.steps > it.h) // scroll bar
					{
						int bw = 4 * k, bx = x + w - bw, th = std::max(8 * k, h * it.h / it.steps), ty = y + (h - th) * it.value / std::max(1, it.steps - it.h);
						cv.RoundRect(bx, y, bw, h, 0, 0xE8E8E8, 0xE8E8E8, 255);
						cv.RoundRect(bx, ty, bw, th, 2 * k, 0xD4D4D4, 0xBCBCBC, 255);
					}
					continue;
				}
				if (it.y + it.h <= it.clipT || it.y >= it.clipB) continue;
				std::vector<std::pair<int, std::vector<uint32_t>>> keep; // rows outside the clip, as they were
				if (!inside)
				{
					int x0 = std::max(0, x - 2 * k), x1 = std::min(cv.w, x + w + 2 * k);
					for (int row = std::max(0, y - 2 * k); row < std::min(cv.h, y + h + 2 * k); row++)
					{
						if (row >= oy + it.clipT * k && row < oy + it.clipB * k) continue;
						keep.push_back({ row, std::vector<uint32_t>(cv.px.begin() + size_t(row) * cv.w + x0, cv.px.begin() + size_t(row) * cv.w + x1) });
					}
				}
				struct Restore
				{
					Canvas &cv;
					std::vector<std::pair<int, std::vector<uint32_t>>> &keep;
					int x0;
					~Restore()
					{
						for (auto &[row, px] : keep) std::copy(px.begin(), px.end(), cv.px.begin() + size_t(row) * cv.w + x0);
					}
				} restore{ cv, keep, std::max(0, x - 2 * k) };
				uint32_t fg = it.enabled ? 0x000000 : 0x8D8D8D;
				int ty = y + (h - lineH) / 2;
				switch (it.kind)
				{
				case K::Label:
					if (it.multiline || h > 20 * k) runs(x + 2 * k, y + 2 * k, x + w - 2 * k, it.text, ink(it.rgb), it.align, true);
					else runs(x + 2 * k, ty, x + w - 2 * k, it.text, ink(it.rgb), it.align, false);
					break;
				case K::Textbox:
					cv.RoundRect(x, y, w, h, 0, 0xFFFFFF, 0xFFFFFF, 255);
					cv.RoundFrame(x, y, w, h, 0, it.focused ? 0x3399FF : 0xABADB3, 255);
					if (it.selB > it.selA && !it.multiline)
					{
						int sx = x + 3 * k + int(text->Measure(it.text.substr(0, it.selA), ui).cx);
						int sw = int(text->Measure(it.text.substr(it.selA, it.selB - it.selA), ui).cx);
						cv.RoundRect(sx, ty, sw, lineH, 0, 0xCCE8FF, 0xCCE8FF, 255);
					}
					runs(x + 3 * k, it.multiline ? y + 2 * k : ty, x + w - 3 * k, it.text, fg, 0, it.multiline);
					if (it.focused && !it.multiline)
					{
						int cx = std::min(x + w - 3 * k, x + 3 * k + int(text->Measure(it.text.substr(0, it.caret), ui).cx));
						cv.RoundRect(cx, ty, std::max(1, k / 2), lineH, 0, 0x000000, 0x000000, 255);
					}
					break;
				case K::Button:
				{
					bool pressed = it.down || it.checked;
					uint32_t t = !it.enabled ? 0xF4F4F4 : pressed ? 0xE5F4FC : it.hover ? 0xEAF6FD : 0xF2F2F2;
					uint32_t b = !it.enabled ? 0xF4F4F4 : pressed ? 0x98D1EF : it.hover ? 0xA7D9F5 : 0xD5D5D5;
					uint32_t edge = !it.enabled ? 0xADB2B5 : pressed ? 0x2C628B : it.hover ? 0x3C7FB1 : 0x707070;
					cv.RoundRect(x + k / 2, y + k / 2, w - k, h - k, 3 * k / 2, t, b, 255);
					cv.RoundFrame(x + k / 2, y + k / 2, w - k, h - k, 3 * k / 2, edge, 255);
					runs(x + 4 * k, ty, x + w - 4 * k, it.text, fg, 1, false); // Vista centres button text
					break;
				}
				case K::Checkbox:
				{
					int bs = 10 * k, by = y + (h - bs) / 2;
					cv.RoundRect(x, by, bs, bs, 0, 0xFFFFFF, it.hover ? 0xDEF2FC : 0xF4F4F4, 255);
					cv.RoundFrame(x, by, bs, bs, 0, it.hover ? 0x3C7FB1 : 0x8E8F8F, 255);
					if (it.checked)
					{
						text->Draw(cv, x + 2 * k, by, L"\u2714", Style{ 12, FW_BOLD, false, false, 0x1C5180, 7 * k });
					}
					runs(x + bs + 4 * k, ty, x + w, it.text, fg, 0, false);
					break;
				}
				case K::DropDown:
				{
					cv.RoundRect(x + k / 2, y + k / 2, w - k, h - k, 3 * k / 2, it.hover ? 0xEAF6FD : 0xF2F2F2, it.hover ? 0xA7D9F5 : 0xD5D5D5, 255);
					cv.RoundFrame(x + k / 2, y + k / 2, w - k, h - k, 3 * k / 2, it.hover ? 0x3C7FB1 : 0x707070, 255);
					runs(x + 4 * k, ty, x + w - 14 * k, it.text, fg, 0, false);
					text->Draw(cv, x + w - 11 * k, ty, L"\u25BC", Style{ 12, FW_NORMAL, false, false, 0x404040, 6 * k });
					break;
				}
				case K::Separator:
					cv.RoundRect(x, y + h / 2, w, std::max(1, k / 2), 0, 0xD5DFE5, 0xD5DFE5, 255);
					break;
				case K::Slider:
				{
					int mid = y + h / 2, tx = x + (w - 6 * k) * it.value / std::max(1, it.steps);
					cv.RoundRect(x, mid - k, w, 2 * k, k, 0xE7EAEA, 0xE7EAEA, 255);
					cv.RoundFrame(x, mid - k, w, 2 * k, k, 0xB0B0B0, 255);
					cv.RoundRect(tx, y + k, 6 * k, h - 2 * k, 2 * k, it.hover ? 0xEAF6FD : 0xF2F2F2, it.hover ? 0xA7D9F5 : 0xD5D5D5, 255);
					cv.RoundFrame(tx, y + k, 6 * k, h - 2 * k, 2 * k, it.hover ? 0x3C7FB1 : 0x707070, 255);
					break;
				}
				case K::ProgressBar:
					cv.RoundRect(x, y, w, h, 0, 0xE6E6E6, 0xE6E6E6, 255);
					cv.RoundRect(x, y, w * std::clamp(it.value, 0, 100) / 100, h, 0, 0x37D33A, 0x06B025, 255);
					cv.RoundFrame(x, y, w, h, 0, 0xBCBCBC, 255);
					break;
				default:
					break;
				}
			}
		}

		void Paint() override
		{
			if (framed)
			{
				theme->Frame(cv, titleH, border, title);
				theme->CaptionButton(cv, CloseRect(), hoverClose, CaptionClose);
			}
			if (!items.empty())
			{
				PaintSkin();
				return;
			}
			for (int y = 0; y < size.Y * scale && titleH + y < cv.h; y++)
			{
				auto *src = pixels.data() + size_t(y / scale) * size.X;
				auto *dst = cv.px.data() + size_t(titleH + y) * cv.w + border;
				for (int x = 0; x < size.X * scale && border + x < cv.w; x++)
					dst[x] = src[x / scale] | 0xFF000000;
			}
		}

		LRESULT HitTest(int x, int y) override
		{
			return framed && y < titleH && !Inside(CloseRect(), x, y) ? HTCAPTION : HTCLIENT;
		}

		void Mouse(UINT msg, int x, int y, WPARAM wp) override
		{
			bool overClose = framed && msg != WM_MOUSELEAVE && Inside(CloseRect(), x, y);
			if (overClose != hoverClose)
			{
				hoverClose = overClose;
				dirty = true;
				Present();
			}
			if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
			{
				SetForegroundWindow(mainWindow); // TPT's window has the keyboard, and the dialog gets its keys from it
			}
			auto &engine = ui::Engine::Ref();
			if (!OnTop() || msg == WM_MOUSELEAVE)
			{
				return; // modal, as in TPT: only the top window takes input
			}
			if (overClose)
			{
				if (msg == WM_LBUTTONUP)
				{
					// The dialog's own way out (cancel), as if Escape was pressed.
					engine.onKeyPress(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, false, false, false, false);
					engine.onKeyRelease(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, false, false, false, false);
				}
				return;
			}
			auto origin = ClientOrigin();
			int ex = pos.X + (x - origin.X) / scale, ey = pos.Y + (y - origin.Y) / scale;
			switch (msg)
			{
			case WM_MOUSEMOVE: engine.onMouseMove(ex, ey); break;
			case WM_LBUTTONDOWN: SetCapture(hwnd); engine.onMouseDown(ex, ey, SDL_BUTTON_LEFT); break;
			case WM_RBUTTONDOWN: SetCapture(hwnd); engine.onMouseDown(ex, ey, SDL_BUTTON_RIGHT); break;
			case WM_MBUTTONDOWN: SetCapture(hwnd); engine.onMouseDown(ex, ey, SDL_BUTTON_MIDDLE); break;
			case WM_LBUTTONUP: ReleaseCapture(); engine.onMouseUp(ex, ey, SDL_BUTTON_LEFT); break;
			case WM_RBUTTONUP: ReleaseCapture(); engine.onMouseUp(ex, ey, SDL_BUTTON_RIGHT); break;
			case WM_MBUTTONUP: ReleaseCapture(); engine.onMouseUp(ex, ey, SDL_BUTTON_MIDDLE); break;
			case WM_MOUSEWHEEL: engine.onMouseWheel(ex, ey, GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA); break;
			}
		}
	};
	std::vector<std::unique_ptr<HostWindow>> hosts;

	void SetTheme(int i)
	{
		theme = themes[i];
		GlobalPrefs::Ref().Set("Overlay.Theme", i);
		ThemeChanged();
	}

	// The look's font: the UI text is remade in it (TempleOS's is embedded, registered for this process on first use).
	void UseThemeFont()
	{
		text->darkPage = ((theme->page >> 16 & 0xFF) + (theme->page >> 8 & 0xFF) + (theme->page & 0xFF)) < 3 * 0x60;
#ifdef _WIN32
		static bool registered = false;
		if (!registered && !wcscmp(theme->font, L"TempleOS"))
		{
			DWORD n = 0;
			auto ttf = templeos_font_ttf.AsCharSpan();
			AddFontMemResourceEx(const_cast<char *>(ttf.data()), DWORD(ttf.size()), nullptr, &n);
			registered = true;
		}
#endif // ponytail: Mac doesn't register it, and its Segoe UI stand-in shows
		if (text && (wcscmp(text->face, theme->font) || text->faceScale != theme->fontScale))
			text->face = theme->font, text->faceScale = theme->fontScale, text->SetScale();
	}

	void ThemeChanged()
	{
		UseThemeFont();
		DirtyAll();
		for (auto &h : hosts)
			if (h) h->dirty = true;
	}
	bool skinDialogs = true; // ponytail: no setting yet; false shows TPT's own look

	// A TPT drop-down list or context menu, shown as a real Windows menu. TrackPopupMenu runs its own message loop,
	// so it runs on a thread of its own (with its own hidden owner window) and the sim keeps going meanwhile.
	struct NativeMenu
	{
		ui::Window *window = nullptr;
		std::thread thread;
		std::atomic<int> picked{ -1 }; // -1 = still open, 0 = cancelled, n = item n - 1
		~NativeMenu()
		{
			if (thread.joinable()) thread.join();
		}
	};
	std::unique_ptr<NativeMenu> menu;

	void OpenMenu(ui::Window *window, POINT at)
	{
		menu = std::make_unique<NativeMenu>();
		menu->window = window;
		std::vector<std::tuple<std::wstring, bool, bool>> items;
		for (auto &it : window->MenuItems) items.push_back({ Wide(it.text), it.enabled, it.checked });
#ifdef __APPLE__
		// not an NSMenu: those hold the main thread, and with it the sim, until they close
		{
			auto *m = menu.get();
			OverlayMac::PopupList(items, at.x / MacScale(), at.y / MacScale(), [m](int picked) { m->picked = picked; });
			return;
		}
#endif
#ifdef _WIN32
		menu->thread = std::thread([m = menu.get(), items = std::move(items), at] {
			HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
			HMENU popup = CreatePopupMenu();
			for (size_t i = 0; i < items.size(); i++)
			{
				auto &[text, enabled, checked] = items[i];
				AppendMenuW(popup, MF_STRING | (enabled ? 0 : MF_GRAYED) | (checked ? MF_CHECKED : 0), i + 1, text.c_str());
				if (checked)
				{
					CheckMenuRadioItem(popup, UINT(i + 1), UINT(i + 1), UINT(i + 1), MF_BYCOMMAND);
				}
			}
			SetForegroundWindow(owner); // or the menu won't close on a click elsewhere
			int cmd = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0, owner, nullptr);
			DestroyMenu(popup);
			DestroyWindow(owner);
			m->picked = cmd;
		});
#endif
	}

	// Called each tick: hands a picked item (or a cancel) back to TPT on its own thread.
	void TickMenu()
	{
		if (!menu || menu->picked < 0) return;
		int picked = menu->picked;
		auto *window = menu->window;
		menu.reset();
		auto &engine = ui::Engine::Ref();
		if (engine.GetWindow() != window) return;
		if (picked > 0 && picked <= int(window->MenuItems.size()))
		{
			auto pick = window->MenuItems[picked - 1].pick;
			pick();
		}
		else
		{
			engine.onMouseDown(-(1 << 20), -(1 << 20), 1); // a click outside closes it
			engine.onMouseUp(-(1 << 20), -(1 << 20), 1);
		}
	}

	// One host per TPT window above the game: made when the window opens, gone when it closes, repainted when its
	// picture changes.
	void SyncHosts()
	{
		auto &engine = ui::Engine::Ref();
		std::vector<std::unique_ptr<HostWindow>> kept;
		HostWindow *below = nullptr;
		for (auto &hosted : engine.Hosted)
		{
			if (!hosted.window->MenuItems.empty())
			{
				if (!menu || menu->window != hosted.window)
				{
					RECT sr;
					GetWindowRect(mainWindow, &sr);
					POINT at;
					if (below)
					{
						RECT br;
						GetWindowRect(below->hwnd, &br);
						at = { br.left + below->border + (hosted.window->MenuAt.X - below->pos.X) * below->scale,
						       br.top + below->titleH + (hosted.window->MenuAt.Y - below->pos.Y) * below->scale };
					}
					else
					{
						int scale = std::max(1, int((sr.right - sr.left) / engine.g->Size().X));
						at = { sr.left + hosted.window->MenuAt.X * scale, sr.top + hosted.window->MenuAt.Y * scale };
					}
					OpenMenu(hosted.window, at);
				}
				continue;
			}
			auto it = std::find_if(hosts.begin(), hosts.end(), [&](auto &h) { return h && h->window == hosted.window; });
			std::unique_ptr<HostWindow> host;
			if (it != hosts.end())
			{
				host = std::move(*it);
			}
			if (host && host->size != hosted.size)
			{
				host.reset(); // ponytail: a window that changes size is remade (and re-placed)
			}
			if (!host)
			{
				host = std::make_unique<HostWindow>(hosted.window);
				host->size = hosted.size;
				host->owner = palette ? palette->hwnd : nullptr;
				// Where TPT would have drawn it: over the window below it, or on the sim layer.
				RECT sr;
				GetWindowRect(mainWindow, &sr);
				auto screen = engine.g->Size();
				int anchorScale = below ? below->scale : std::max(1, int((sr.right - sr.left) / screen.X));
				POINT at = below ? POINT{ 0, 0 } : POINT{ sr.left, sr.top };
				if (below)
				{
					RECT br;
					GetWindowRect(below->hwnd, &br);
					at = { br.left + below->border + (hosted.pos.X - below->pos.X) * anchorScale,
					       br.top + below->titleH + (hosted.pos.Y - below->pos.Y) * anchorScale };
				}
				else
				{
					at = { sr.left + hosted.pos.X * anchorScale, sr.top + hosted.pos.Y * anchorScale };
				}
				MONITORINFO mi{ sizeof(mi) };
				GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &mi);
				auto work = mi.rcWork;
				int chromeW = 2 * host->border, chromeH = host->titleH + host->border;
#ifdef __APPLE__
				int dialogScale = engine.GetScale() * int(std::lround(MacScale())); // TPT's scale is in points, these windows are in pixels
#else
				int dialogScale = engine.GetScale();
#endif
				host->scale = std::max(1, std::min({ dialogScale, int(work.right - work.left - chromeW) / std::max(1, hosted.size.X),
				                                      int(work.bottom - work.top - chromeH) / std::max(1, hosted.size.Y) }));
				int w = hosted.size.X * host->scale + chromeW, h = hosted.size.Y * host->scale + chromeH;
				int x = at.x - host->border, y = at.y - host->titleH;
				if (host->framed && !below)
				{
					// A dialog that TPT centres on its screen: centre it on the monitor instead, the sim layer's
					// scale can differ from the dialog's.
					x = (work.left + work.right - w) / 2;
					y = (work.top + work.bottom - h) / 2;
				}
				x = std::clamp(x, int(work.left), std::max(int(work.left), int(work.right) - w));
				y = std::clamp(y, int(work.top), std::max(int(work.top), int(work.bottom) - h));
				host->Create(x, y, w, h);
				ShowWindow(host->hwnd, SW_SHOWNOACTIVATE);
			}
			host->pos = hosted.pos;
			std::vector<SkinItem> items;
			if (host->framed && skinDialogs && hosted.size != engine.g->Size())
			{
				SkinWalk(hosted.window, hosted.window->GetComponents(), 0, hosted.size.Y, items);
				for (auto &[pos, size] : hosted.window->RawAreas)
				{
					items.push_back({ ui::Component::Kind::Other, pos.X, pos.Y, size.X, size.Y, 0, hosted.size.Y });
				}
			}
			if (host->pixels != hosted.pixels || host->items != items)
			{
				host->items = std::move(items);
				host->pixels = hosted.pixels;
				host->dirty = true;
				host->Present();
			}
			below = host.get();
			kept.push_back(std::move(host));
		}
		hosts = std::move(kept); // the rest close
	}

	// The notification-area (tray) icon: left click shows or hides the Elements window, right click opens a menu.
	NOTIFYICONDATAW tray{};
	// The taskbar's app buttons as UI Automation sees them (screen pixels), polled on a worker thread every half second
	// while a taskbar toy is on. A button whose name changed just after a window was created (Windows 11 names them
	// "App - 2 running windows") is the app that opened.
	struct TaskButton { std::wstring name; RECT r; };
	std::atomic<ULONGLONG> windowCreated = 0;
	std::mutex taskMutex;
	std::vector<TaskButton> taskButtons;
	std::vector<RECT> taskPopped;
#ifdef _WIN32
	void StartTaskButtons()
	{
		static bool started = false;
		if (started) return;
		started = true;
		std::thread([] {
			CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			IUIAutomation *uia = nullptr;
			IUIAutomationCondition *buttons = nullptr;
			VARIANT v{};
			v.vt = VT_I4, v.lVal = UIA_ButtonControlTypeId;
			if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) ||
			    FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &buttons)))
				return;
			std::vector<std::wstring> before;
			bool first = true;
			for (;; Sleep(500))
			{
				if (!fun.taskThrow && !fun.taskPop)
				{
					first = true;
					continue;
				}
				std::vector<TaskButton> now;
				auto scan = [&](HWND hwnd, bool any) { // any: every button (Windows 10's task list); else Windows 11's task buttons
					IUIAutomationElement *root = nullptr;
					IUIAutomationElementArray *found = nullptr;
					if (hwnd && SUCCEEDED(uia->ElementFromHandle(hwnd, &root)) && SUCCEEDED(root->FindAll(TreeScope_Descendants, buttons, &found)) && found)
					{
						int n = 0;
						found->get_Length(&n);
						for (int i = 0; i < n; i++)
						{
							IUIAutomationElement *e = nullptr;
							if (FAILED(found->GetElement(i, &e)) || !e) continue;
							BSTR cls = nullptr, name = nullptr;
							RECT r{};
							e->get_CurrentClassName(&cls);
							if ((any || (cls && wcsstr(cls, L"TaskListButton"))) && SUCCEEDED(e->get_CurrentName(&name)) && SUCCEEDED(e->get_CurrentBoundingRectangle(&r)) && r.right > r.left)
								now.push_back({ name ? name : L"", r });
							SysFreeString(cls);
							SysFreeString(name);
							e->Release();
						}
						found->Release();
					}
					if (root) root->Release();
				};
				for (const wchar_t *cls : { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd" })
					for (HWND tray = FindWindowExW(nullptr, nullptr, cls, nullptr); tray; tray = FindWindowExW(nullptr, tray, cls, nullptr))
					{
						scan(tray, false);
						HWND list = nullptr;
						EnumChildWindows(tray, [](HWND h, LPARAM lp) {
							wchar_t c[32];
							if (GetClassNameW(h, c, 32) && !wcscmp(c, L"MSTaskListWClass")) return *reinterpret_cast<HWND *>(lp) = h, FALSE;
							return TRUE;
						}, LPARAM(&list));
						scan(list, true);
					}
				std::lock_guard lock(taskMutex);
				if (!first && fun.taskPop && GetTickCount64() - windowCreated < 4000)
					for (auto &b : now)
						if (std::find(before.begin(), before.end(), b.name) == before.end()) taskPopped.push_back(b.r);
				before.clear();
				for (auto &b : now) before.push_back(b.name);
				taskButtons = std::move(now);
				first = false;
			}
		}).detach();
	}
#else
	void StartTaskButtons() {} // TickMacDock reads the Dock
#endif

	constexpr UINT trayMessage = WM_APP + 1;

	LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
		if (msg == taskbarCreated)
		{
			Shell_NotifyIconW(NIM_ADD, &tray); // Explorer restarted and lost the icon
			return 0;
		}
		static const UINT shellHook = RegisterWindowMessageW(L"SHELLHOOK");
		if (msg == shellHook)
		{
			if (wp == HSHELL_FLASH && fun.notify) fun.flashed.push_back(HWND(lp));
			if (wp == HSHELL_WINDOWCREATED) windowCreated = GetTickCount64();
			return 0;
		}
		if (msg == WM_ENDSESSION && wp)
		{
			SaveSceneTiles(); // shutting down or signing out: TPT gets no quit of its own
			return 0;
		}
		if (msg != trayMessage)
		{
			return DefWindowProcW(hwnd, msg, wp, lp);
		}
		if (lp == WM_LBUTTONUP)
		{
			OverlayWindows::TogglePalette();
		}
		if (lp == NIN_BALLOONUSERCLICK)
		{
			ShellExecuteW(nullptr, L"open", L"https://github.com/PVCPC/The-Desktop-Powder-Toy/releases/latest", nullptr, nullptr, SW_SHOWNORMAL);
		}
		if (lp == WM_RBUTTONUP)
		{
			// The plain Windows menu: settings and the way into the Vista windows.
			enum { elements = 1, pause, clear, inputBase = 10, showMenus = 20, exitItem = 99 };
			auto &prefs = GlobalPrefs::Ref();
			auto label = [&](const wchar_t *text, const char *keyName, const char *fallback) {
				return std::wstring(text) + L"\t" + Platform::WinWiden(prefs.Get(ByteString("Overlay.Keys.") + keyName, ByteString(fallback)));
			};
			HMENU menu = CreatePopupMenu();
			bool open = palette && IsWindowVisible(palette->hwnd);
			AppendMenuW(menu, MF_STRING | (open ? MF_CHECKED : 0), elements, label(L"Elements window", "Palette", "Ctrl+Alt+E").c_str());
			AppendMenuW(menu, MF_STRING, pause, GameController::Ref().GetPaused() ? L"Resume" : L"Pause");
			AppendMenuW(menu, MF_STRING, clear, label(L"Clear the sim", "Clear", "Ctrl+Alt+C").c_str());
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			std::wstring modes[] = { L"Input: Auto", label(L"Input: Pass-through", "PassThrough", "Ctrl+Alt+P"), label(L"Input: Locked to TPT", "Locked", "Ctrl+Alt+L") };
			for (int i = 0; i < int(OverlayInput::Count); i++)
			{
				AppendMenuW(menu, MF_STRING | (int(OverlayGetInput()) == i ? MF_CHECKED : 0), inputBase + i, modes[i].c_str());
			}
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING | (showTPTMenus ? MF_CHECKED : 0), showMenus, L"Show TPT's own menus");
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING, exitItem, L"Exit");
			POINT p;
			GetCursorPos(&p);
			SetForegroundWindow(hwnd); // so the menu closes when you click elsewhere
			int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, hwnd, nullptr);
			PostMessageW(hwnd, WM_NULL, 0, 0);
			DestroyMenu(menu);
			if (cmd == elements)
			{
				OverlayWindows::TogglePalette();
			}
			if (cmd == pause) GameController::Ref().SetPaused(!GameController::Ref().GetPaused());
			if (cmd == clear) GameController::Ref().ClearSim();
			if (cmd >= inputBase && cmd < inputBase + int(OverlayInput::Count))
			{
				OverlaySetInput(OverlayInput(cmd - inputBase));
			}
			if (cmd == showMenus)
			{
				showTPTMenus = !showTPTMenus;
				prefs.Set("Overlay.ShowTPTMenus", showTPTMenus);
				if (!overlayWindowed) Restart(ui::Engine::Ref().windowFrameOps.scale, false, true);
			}
			if (cmd == exitItem)
			{
				SDL_Event quit{};
				quit.type = SDL_QUIT;
				SDL_PushEvent(&quit);
			}
		}
		return 0;
	}

	void AddTrayIcon()
	{
		WNDCLASSW wc{};
		wc.lpfnWndProc = TrayProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"TPTOverlayTray";
		RegisterClassW(&wc);
		tray.cbSize = sizeof(tray);
		// A hidden top-level window, not a message-only one: those miss the TaskbarCreated broadcast.
		tray.hWnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
		tray.uID = 1;
		tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		tray.uCallbackMessage = trayMessage;
		wchar_t exe[MAX_PATH];
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		tray.hIcon = ExtractIconW(wc.hInstance, exe, 0);
		if (!tray.hIcon || tray.hIcon == HICON(1))
		{
			tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
		}
		wcscpy_s(tray.szTip, L"The Powder Toy (overlay)");
		Shell_NotifyIconW(NIM_ADD, &tray);
#ifdef _WIN32
		RegisterShellHookWindow(tray.hWnd); // taskbar buttons flashing (Notifications drop a ball)
#endif
		std::atexit([] { Shell_NotifyIconW(NIM_DELETE, &tray); });
#ifdef _WIN32
		// On a crash too. A forced kill (Task Manager's End task) can't be caught: Windows clears that icon on hover.
		static LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *e) -> LONG {
			Shell_NotifyIconW(NIM_DELETE, &tray);
			return previous ? previous(e) : EXCEPTION_CONTINUE_SEARCH;
		});
#else
#endif
	}

	void ApplyPassThrough(Panel &panel)
	{
		auto ex = GetWindowLongPtrW(panel.hwnd, GWL_EXSTYLE);
		SetWindowLongPtrW(panel.hwnd, GWL_EXSTYLE, passThrough ? ex | WS_EX_TRANSPARENT : ex & ~LONG_PTR(WS_EX_TRANSPARENT));
		panel.opacity = passThrough ? 110 : 255;
		panel.Present();
	}
}

#ifdef __APPLE__
// SDL can miss a button's release when our window goes back to ignoring the mouse mid-press, or another app takes it; TPT
// would keep drawing. The system knows what's really held.
static void ReleaseStuckButtons()
{
	static uint32_t sent = 0;
	int x, y;
	uint32_t held = SDL_GetMouseState(&x, &y);
	sent &= held;
	for (auto [button, index] : { std::pair{ SDL_BUTTON_LEFT, 0 }, { SDL_BUTTON_RIGHT, 1 } })
	{
		uint32_t mask = SDL_BUTTON(button);
		if (!(held & mask) || (sent & mask) || OverlayMac::ButtonDown(index)) continue;
		sent |= mask;
		SDL_Event up{};
		up.type = SDL_MOUSEBUTTONUP;
		up.button.button = uint8_t(button), up.button.state = SDL_RELEASED, up.button.clicks = 1, up.button.x = x, up.button.y = y;
		SDL_PushEvent(&up);
	}
}

// The Dock stands in for the taskbar: its buttons for the throw, a badge that went up is the Mac's "flashed its button",
// an app that started pops a block. Twice a second is as often as the Dock is read.
static void TickMacDock(Simulation *sim)
{
	static std::map<std::string, std::pair<int, bool>> dock; // badge, running
	bool wanted = fun.notify || fun.taskPop || fun.taskThrow;
	if (!wanted)
	{
		dock.clear();
		return;
	}
	if (sim->currentTick % 30) return;
	auto windows = OverlayMac::OtherWindows();
	std::map<std::string, std::pair<int, bool>> now;
	std::vector<TaskButton> buttons;
	double s = MacScale();
	RECT floor;
	GetWindowRect(macDock, &floor);
	for (auto &item : OverlayMac::DockItems())
	{
		RECT r{ long(item.l * s), long(item.t * s), long(item.r * s), long(item.b * s) };
		buttons.push_back({ Platform::WinWiden(item.name), r });
		now[item.name] = { item.badge, item.running };
		auto was = dock.find(item.name);
		if (was == dock.end()) continue; // the first look: nothing has changed yet
		if (fun.notify && item.badge > was->second.first)
			for (auto &w : windows) // front to back: its top window
				if (w.owner == item.name)
				{
					fun.flashed.push_back(HWND(uintptr_t(w.id)));
					break;
				}
		if (fun.taskPop && item.running && !was->second.second)
		{
			std::lock_guard lock(taskMutex);
			taskPopped.push_back({ r.left, std::min(r.top, floor.top), r.right, r.bottom }); // from the sim's floor, the Dock is below it
		}
	}
	dock = std::move(now);
	std::lock_guard lock(taskMutex);
	taskButtons = std::move(buttons);
}

static void MacInit()
{
	overlayFeel.holeBorder = false; // that's for the dark line DWM draws round a window
	OverlayMac::SetShiftSource([] {
		std::vector<OverlayMac::Shift> out;
		for (auto &s : OverlayWindows::WindowShifts())
			out.push_back({ s.x0, s.y0, s.x1, s.y1, s.dx, s.dy, uint32_t(uintptr_t(s.id)) });
		return out;
	});
	// icons as walls are on by default and need this; macOS asks once, and the Welcome tab has the status
	if (iconWalls && !OverlayMac::AccessibilityGranted()) OverlayMac::AskAccessibility();
}
#endif

void OverlayWindows::Init(void *mainHwnd)
{
	mainWindow = HWND(mainHwnd);
	auto &prefs = GlobalPrefs::Ref();
	windowWalls = prefs.Get("Overlay.WindowWalls", true);
	iconWalls = prefs.Get("Overlay.IconWalls", true);
	areaFrame = prefs.Get("Overlay.AreaFrame", false);
	overlayFeel.destructible = prefs.Get("Overlay.Feel.Destructible", false);
	overlayFeel.shardTint = prefs.Get("Overlay.Feel.ShardTint", true);
	overlayFeel.shardParticles = prefs.Get("Overlay.Feel.ShardParticles", false);
	overlayFeel.holeBorder = prefs.Get("Overlay.Feel.HoleBorder", true);
	overlayFeel.wallpaper = prefs.Get("Overlay.Feel.Wallpaper", false);
	overlayFeel.gradient = prefs.Get("Overlay.Gradient", false);
	overlayFeel.gradientTop = uint32_t(prefs.Get("Overlay.GradientTop", int(overlayFeel.gradientTop)));
	overlayFeel.gradientBottom = uint32_t(prefs.Get("Overlay.GradientBottom", int(overlayFeel.gradientBottom)));
	for (auto [v, name] : { std::pair{ &overlayFeel.grip, "Grip" }, { &overlayFeel.fling, "Fling" }, { &overlayFeel.push, "Push" }, { &overlayFeel.wind, "Wind" }, { &overlayFeel.scorch, "Scorch" }, { &overlayFeel.frost, "Frost" }, { &overlayFeel.acid, "Acid" }, { &overlayFeel.water, "Water" }, { &overlayFeel.acidEat, "AcidEat" }, { &overlayFeel.blast, "Blast" }, { &overlayFeel.holeOpacity, "HoleSolid" }, { &overlayFeel.shards, "Shards" }, { &overlayFeel.shardSize, "ShardSize" }, { &overlayFeel.blackHole, "BlackHole" }, { &overlayFeel.shake, "Shake" } })
		*v = prefs.Get(ByteString("Overlay.Feel.") + name, *v);
	fun.weatherKind = prefs.Get("Overlay.Fun.WeatherKind", 0);
	fun.buddyWindows = prefs.Get("Overlay.Buddy.Windows", 1);
	for (auto rows : { std::span<FunRow>(funRows), std::span<FunRow>(buddyRows) })
	for (auto &f : rows)
	{
		if (f.b) *f.b = prefs.Get(f.pref, *f.b);
		if (f.v) *f.v = prefs.Get(f.pref, *f.v);
	}
	// Default UI scale: the larger of Windows' display scaling (TPT is DPI aware) and screen height / 1080, in
	// quarter steps. Overridable.
	HDC screen = GetDC(nullptr);
	float dpiScale = GetDeviceCaps(screen, LOGPIXELSY) / 96.f;
	ReleaseDC(nullptr, screen);
	float heightScale = GetSystemMetrics(SM_CYSCREEN) / 1080.f;
	uiScale = prefs.Get("Overlay.UIScale", std::max(1.f, std::round(std::max(dpiScale, heightScale) * 4) / 4));
	showTPTMenus = prefs.Get("Overlay.ShowTPTMenus", false);
#ifdef __APPLE__
	mainWindow = macOverlay;
	MacInit();
	// this Mac's own look: Liquid Glass needs to see what's behind the window, 11-15 doesn't
	theme = themes[std::clamp(prefs.Get("Overlay.Theme", OverlayMac::ScreenCaptureGranted() ? 17 : 10), 0, int(std::size(themes)) - 1)];
#else
	theme = themes[std::clamp(prefs.Get("Overlay.Theme", defaultTheme), 0, int(std::size(themes)) - 1)];
#endif
	text = new Text;
	text->SetScale();
	UseThemeFont();
	AddTrayIcon();
}

static void TickRecording();
static void TickWindowWalls();

int OverlayWindows::WorkBottom(int x)
{
#ifdef _WIN32
	RECT sr;
	if (!mainWindow || !GetWindowRect(mainWindow, &sr) || sr.right <= sr.left || sr.bottom <= sr.top) return YRES;
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	MONITORINFO mi{ sizeof(mi) };
	if (!GetMonitorInfoW(MonitorFromPoint({ int(sr.left + x * sw / WINDOWW), int(sr.bottom - 1) }, MONITOR_DEFAULTTONEAREST), &mi)) return YRES;
	return std::clamp(int((mi.rcWork.bottom - sr.top) * WINDOWH / sh), 1, YRES);
#else
	return YRES; // ponytail: the Mac's Dock isn't asked yet
#endif
}

bool OverlayWindows::ActiveEdges(uintptr_t &id, int &left, int &right, int &top, int &bottom)
{
#ifdef _WIN32
	// as the desktop Interactive Buddy did it: the active window, or the last one that wasn't ours
	static HWND last;
	HWND h = GetForegroundWindow();
	DWORD pid = 0;
	if (h) GetWindowThreadProcessId(h, &pid);
	if (h && pid != GetCurrentProcessId()) last = h;
	h = last;
	wchar_t cls[32] = {};
	RECT sr, outer, client;
	if (!mainWindow || !h || !IsWindow(h) || !IsWindowVisible(h) || IsIconic(h) || !GetWindowRect(mainWindow, &sr) ||
	    sr.right <= sr.left || sr.bottom <= sr.top || !GetWindowRect(h, &outer)) return false;
	GetClassNameW(h, cls, 32);
	if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return false;
	DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &outer, sizeof(outer));
	GetClientRect(h, &client);
	POINT b{ 0, client.bottom };
	ClientToScreen(h, &b);
	MONITORINFO mi{ sizeof(mi) };
	GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
	auto sx = [&](int x) { return int((x - sr.left) * WINDOWW / (sr.right - sr.left)); };
	auto sy = [&](int y) { return int((y - sr.top) * WINDOWH / (sr.bottom - sr.top)); };
	id = uintptr_t(h);
	left = sx(outer.left), right = sx(outer.right);
	top = outer.top - mi.rcWork.top >= 90 ? sy(outer.top) : -1;
	bottom = b.y < mi.rcWork.bottom - 1 && b.y > mi.rcWork.top ? sy(b.y) : -1;
	return right > left;
#else
	return false; // ponytail: the Mac's active window isn't asked yet
#endif
}

void OverlayWindows::Tick()
{
	if (!mainWindow || !&GameController::Ref())
	{
		return;
	}
#ifdef __APPLE__
	ReleaseStuckButtons();
	TickMacDock(GameController::Ref().GetModel()->GetSimulation());
	OverlayMac::HoldDockClicks(fun.taskThrow && !overlayWindowed);
	OverlayMac::WantDesktop(!overlayWindowed && iconWalls && OverlayGetLayer() != OverlayLayer::BehindIcons, !overlayWindowed && (windowWalls || fun.bin || fun.notify || fun.taskPop || fun.taskThrow), !overlayWindowed && fun.typing);
#endif
	TickWindowWalls();
	static auto &release = *new std::unique_ptr<http::Request>(); // never destroyed: at exit it would outlive the request manager
	static bool checked = false;
	if (!checked)
	{
		checked = true;
		release = std::make_unique<http::Request>("https://api.github.com/repos/PVCPC/The-Desktop-Powder-Toy/releases/latest");
		release->Start();
	}
	if (release && release->CheckDone())
	{
		ByteString tag;
		try
		{
			auto [status, data] = release->Finish();
			auto at = data.find("\"tag_name\"");
			if (status == 200 && at != data.npos)
			{
				auto q = data.find('"', data.find(':', at) + 1) + 1;
				tag = data.substr(q, data.find('"', q) - q);
			}
		}
		catch (const http::RequestError &) {}
		release.reset();
		auto version = [](const std::wstring &v) { // "v1.2.3" or "1.2.3" -> {1, 2, 3}
			std::array<int, 3> n{};
			swscanf_s(v.c_str() + (v.size() && v[0] == L'v'), L"%d.%d.%d", &n[0], &n[1], &n[2]);
			return n;
		};
		auto &prefs = GlobalPrefs::Ref();
		if (tag.size() && version(Platform::WinWiden(tag)) > version(DESKTOP_VERSION) && tag != prefs.Get("Overlay.ToldVersion", ByteString()))
		{
			prefs.Set("Overlay.ToldVersion", tag);
			tray.uFlags = NIF_INFO;
			wcscpy_s(tray.szInfoTitle, (L"The Desktop Powder Toy " + Platform::WinWiden(tag) + L" is out").c_str());
			wcscpy_s(tray.szInfo, L"Click to download it.");
			tray.dwInfoFlags = NIIF_INFO;
			Shell_NotifyIconW(NIM_MODIFY, &tray);
			tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		}
	}
	static std::vector<Notification *> seen;
	auto notes = GameController::Ref().GetModel()->GetNotifications();
	if (notes != seen)
	{
		for (auto *n : notes)
			if (std::find(seen.begin(), seen.end(), n) == seen.end()) Notify(n->Message.ToUtf8().c_str());
		seen = notes;
		DirtyAll();
	}
	static bool areaRestored = false;
	if (!areaRestored)
	{
		auto &prefs = GlobalPrefs::Ref();
		int w = prefs.Get("Overlay.PlayArea.W", 0), h = prefs.Get("Overlay.PlayArea.H", 0); // 0: the whole sim, whatever its size
		if (!overlayWindowed) SetPlayArea(prefs.Get("Overlay.PlayArea.X", 0), prefs.Get("Overlay.PlayArea.Y", 0), w > 0 ? w : CELLS.X, h > 0 ? h : CELLS.Y);
		areaRestored = true;
		if (GlobalPrefs::Ref().Get("Overlay.PendingScene", false))
		{
			GlobalPrefs::Ref().Set("Overlay.PendingScene", false);
			LoadSceneTiles(); // from the last run, or a restart at another pixel size
		}
		GameController::Ref().GetView()->SkipIntroText(); // it's in the side window's Welcome tab
	}
	if (!palette && GlobalPrefs::Ref().Get("Overlay.Windows.Palette.Open", true))
	{
		TogglePalette();
	}
	if (palette)
	{
		palette->Tick();
		if (palette->dirty)
		{
			palette->Present();
		}
	}
	static bool tornRestored = false;
	if (!tornRestored)
	{
		tornRestored = true;
		int mask = GlobalPrefs::Ref().Get("Overlay.Windows.Torn", 0);
		for (int i = 0; i < int(std::size(SideWindow::tabNames)); i++)
			if (mask & (1 << i)) OpenTorn(i);
	}
	for (auto &t : torn)
	{
		t->Tick();
		if (t->dirty) t->Present();
	}
	if (std::erase_if(torn, [](auto &t) { return t->closing; })) SaveTorn();
	if (toast)
	{
		toast->Tick();
	}
	SyncHosts();
	TickMenu();
	TickRecording();
	if (later)
	{
		auto run = std::move(later);
		later = nullptr;
		run();
	}
	if (rebuildPalette)
	{
		rebuildPalette = false;
		reopenTab = palette ? palette->tab : -1;
		palette.reset();
		torn.clear(); // reopened at the new scale from the saved mask
		tornRestored = false;
		text->SetScale();
		TogglePalette();
	}
}

bool OverlayWindows::RecordingKey()
{
	return !recording.empty();
}

// While the Overlay tab records a shortcut: the first letter, digit or F key pressed with the modifiers held.
static void TickRecording()
{
	if (recording.empty()) return;
	auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
	if (down(VK_ESCAPE))
	{
		recording.clear();
		DirtyAll();
		return;
	}
	int key = 0;
	for (int vk = '0'; vk <= 'Z' && !key; vk++)
		if ((vk <= '9' || vk >= 'A') && down(vk)) key = vk;
	for (int vk = VK_F1; vk <= VK_F24 && !key; vk++)
		if (down(vk)) key = vk;
	bool ctrl = down(VK_CONTROL), alt = down(VK_MENU), shift = down(VK_SHIFT), win = down(VK_LWIN) || down(VK_RWIN);
	if (!key || (key < VK_F1 && !ctrl && !alt && !win)) return; // a bare letter would fire while typing
	ByteString combo;
	if (ctrl) combo += "Ctrl+";
	if (alt) combo += "Alt+";
	if (shift) combo += "Shift+";
	if (win) combo += "Win+";
	combo += key >= VK_F1 ? ByteString::Build("F", key - VK_F1 + 1) : ByteString(1, char(key));
	GlobalPrefs::Ref().Set("Overlay.Keys." + recording, combo);
	recording.clear();
	OverlayLoadKeys();
	DirtyAll();
}

void OverlayWindows::SaveScene()
{
#ifdef __APPLE__
	OverlayMac::Shutdown();
#endif
	SaveSceneTiles();
}

void OverlayWindows::ClearSim()
{
	GameController::Ref().ClearSim();
	Notify("Cleared");
}

void OverlayWindows::TogglePalette()
{
	if (!&GameController::Ref())
	{
		return; // the tray icon exists before the game does
	}
	bool open = !palette || !IsWindowVisible(palette->hwnd) || IsIconic(palette->hwnd);
	GlobalPrefs::Ref().Set("Overlay.Windows.Palette.Open", open);
	if (!open)
	{
		ShowWindow(palette->hwnd, SW_HIDE);
		return;
	}
	if (!palette)
	{
		palette = std::make_unique<SideWindow>();
		// Tall enough for every section in the navigation pane and for the Simulation tab.
		int w = S(560), h = std::max<int>(palette->NavRect(int(palette->Sections().size())).top + S(4) + palette->statusH + palette->border, S(560));
		// Default: just right of TPT's window, or its top-left corner if that's off screen.
		RECT rc;
		GetWindowRect(mainWindow, &rc);
		int x = rc.right + S(8), y = rc.top;
		if (x + w > GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN))
		{
			x = rc.left + S(8);
			y = rc.top + S(8);
		}
		auto r = PlaceWindow(palette->name, x, y, w, h, S(360), S(300));
		palette->grip = palette->border;
		palette->minSize = { S(360), S(300) };
		palette->Create(r.left, r.top, r.right - r.left, r.bottom - r.top);
	}
	palette->dirty = true;
	palette->Present();
	ShowWindow(palette->hwnd, SW_SHOWNOACTIVATE);
	ApplyPassThrough(*palette);
}

void OverlayWindows::SetPassThrough(bool on)
{
	passThrough = on;
	if (palette)
	{
		ApplyPassThrough(*palette);
	}
	for (auto &t : torn) ApplyPassThrough(*t);
}

void OverlayWindows::Notify(const char *message)
{
	if (!toast)
	{
		toast = std::make_unique<Toast>();
	}
	toast->Show(mainWindow, Platform::WinWiden(message));
}
bool OverlayWindows::HideTPTMenus()
{
	return !showTPTMenus && palette && IsWindowVisible(palette->hwnd);
}

// What a hole eaten in a window shows: the desktop wallpaper behind it, per monitor, in sim pixels (XRES x YRES,
// 0x00RRGGBB). Rebuilt now and then while there are holes (slideshows, moved overlay).
// ponytail: wallpaper drawn as "fill" (cover, centred) whatever the fit setting; read IDesktopWallpaper::GetPosition if it matters.
#ifdef _WIN32
static const std::vector<uint32_t> &HoleBackdrop(RECT sr, int sw, int sh)
{
	static std::vector<uint32_t> backdrop;
	static ULONGLONG built = 0;
	static RECT builtFor{};
	if (!backdrop.empty() && GetTickCount64() - built < 30000 && EqualRect(&sr, &builtFor)) return backdrop;
	built = GetTickCount64(), builtFor = sr;
	backdrop.assign(size_t(XRES) * YRES, 0x1A1A1A);
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // already on (S_FALSE) or another mode: either is fine
	IDesktopWallpaper *wall = nullptr;
	IWICImagingFactory *wic = nullptr;
	UINT monitors = 0;
	if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wall))) ||
	    FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
	    FAILED(wall->GetMonitorDevicePathCount(&monitors)))
		monitors = 0;
	COLORREF solid = 0x1A1A1A;
	if (wall) wall->GetBackgroundColor(&solid);
	for (UINT i = 0; i < monitors; i++)
	{
		LPWSTR id = nullptr, file = nullptr;
		RECT mr;
		if (FAILED(wall->GetMonitorDevicePathAt(i, &id)) || FAILED(wall->GetMonitorRECT(id, &mr)))
		{
			CoTaskMemFree(id);
			continue;
		}
		wall->GetWallpaper(id, &file);
		std::vector<uint8_t> px;
		UINT iw = 0, ih = 0;
		IWICBitmapDecoder *dec = nullptr;
		IWICBitmapFrameDecode *frame = nullptr;
		IWICFormatConverter *conv = nullptr;
		if (file && *file && SUCCEEDED(wic->CreateDecoderFromFilename(file, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
		    SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
		    SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
		    SUCCEEDED(conv->GetSize(&iw, &ih)) && iw && ih)
		{
			px.resize(size_t(iw) * ih * 4);
			if (FAILED(conv->CopyPixels(nullptr, iw * 4, UINT(px.size()), px.data()))) px.clear();
		}
		for (IUnknown *p : std::initializer_list<IUnknown *>{ conv, frame, dec })
			if (p) p->Release();
		CoTaskMemFree(file);
		CoTaskMemFree(id);
		double mw = mr.right - mr.left, mh = mr.bottom - mr.top;
		double scale = px.empty() ? 1 : std::max(mw / iw, mh / ih), ox = (mw - iw * scale) / 2, oy = (mh - ih * scale) / 2;
		for (int y = 0; y < YRES; y++)
			for (int x = 0; x < XRES; x++)
			{
				double sx = sr.left + (x + 0.5) * sw / WINDOWW, sy = sr.top + (y + 0.5) * sh / WINDOWH; // screen point
				if (sx < mr.left || sx >= mr.right || sy < mr.top || sy >= mr.bottom) continue;
				auto &b = backdrop[size_t(y) * XRES + x];
				if (px.empty())
				{
					b = GetRValue(solid) << 16 | GetGValue(solid) << 8 | GetBValue(solid);
					continue;
				}
				int ix = std::clamp(int((sx - mr.left - ox) / scale), 0, int(iw) - 1), iy = std::clamp(int((sy - mr.top - oy) / scale), 0, int(ih) - 1);
				auto *q = &px[(size_t(iy) * iw + ix) * 4];
				b = q[2] << 16 | q[1] << 8 | q[0];
			}
	}
	if (wic) wic->Release();
	if (wall) wall->Release();
	return backdrop;
}

#endif // HoleBackdrop
static std::map<HWND, RECT> wallRects, frameRects; // the wall windows when the sim last read them / at the frame drawn
static std::mutex frameLock; // frameRects: WindowShifts is also asked from the GPU drawing thread

#ifdef _WIN32
// What is really behind holed windows: the windows under each one, captured with PrintWindow a few times a second on
// a worker thread (scaled to sim pixels), over the wallpaper.
namespace
{
	struct Shot { int w = 0, h = 0; std::vector<uint32_t> px; }; // the whole window (GetWindowRect), in sim pixels
	std::mutex shotLock;
	std::map<HWND, Shot> shots;
	std::vector<HWND> shotWanted;
	RECT shotScreen{}; // the overlay's screen rect: sim pixel size
}

static void ShotWorker()
{
	HBITMAP bmp = nullptr;
	void *bits = nullptr;
	int bmpW = 0, bmpH = 0;
	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		std::vector<HWND> want;
		RECT sr;
		{
			std::lock_guard lock(shotLock);
			want = shotWanted;
			sr = shotScreen;
		}
		std::sort(want.begin(), want.end());
		want.erase(std::unique(want.begin(), want.end()), want.end()); // under several holed windows: shot once
		if (want.empty()) // nothing holed: let the big bitmap go
		{
			if (bmp) DeleteObject(bmp), bmp = nullptr, bmpW = bmpH = 0;
			continue;
		}
		int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
		std::map<HWND, Shot> got;
		for (auto hwnd : want)
		{
			RECT r;
			if (sw <= 0 || sh <= 0 || !GetWindowRect(hwnd, &r) || r.right <= r.left || r.bottom <= r.top) continue;
			int ww = r.right - r.left, wh = r.bottom - r.top;
			HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
			if (!bmp || bmpW < ww || bmpH < wh) // one bitmap, as big as the biggest window so far (not one per window per shot)
			{
				if (bmp) DeleteObject(bmp);
				bmpW = std::max(bmpW, ww), bmpH = std::max(bmpH, wh);
				BITMAPINFO bi{};
				bi.bmiHeader = { sizeof(BITMAPINFOHEADER), bmpW, -bmpH, 1, 32, BI_RGB };
				bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
				if (!bmp) bmpW = bmpH = 0;
			}
			if (bmp)
			{
				auto old = SelectObject(dc, bmp);
				if (PrintWindow(hwnd, dc, 2 /* PW_RENDERFULLCONTENT: GPU-drawn windows too */))
				{
					Shot s{ std::max(1, ww * WINDOWW / sw), std::max(1, wh * WINDOWH / sh), {} };
					s.px.resize(size_t(s.w) * s.h);
					for (int y = 0; y < s.h; y++)
						for (int x = 0; x < s.w; x++)
						{
							int ix = std::min(ww - 1, int((x + 0.5) * sw / WINDOWW)), iy = std::min(wh - 1, int((y + 0.5) * sh / WINDOWH));
							s.px[size_t(y) * s.w + x] = ((uint32_t *)bits)[size_t(iy) * bmpW + ix] & 0xFFFFFF;
						}
					got[hwnd] = std::move(s);
				}
				SelectObject(dc, old);
			}
			DeleteDC(dc);
			ReleaseDC(nullptr, screen);
		}
		std::lock_guard lock(shotLock);
		shots = std::move(got);
	}
}

// The wallpaper, and in each holed window's holes the windows under it, topmost first.
static const uint32_t *HoleBehind(const Simulation *sim, RECT sr)
{
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	static std::vector<uint32_t> behind;
	behind = HoleBackdrop(sr, sw, sh);
	static bool started = false;
	if (!started) std::thread(ShotWorker).detach(), started = true;
	std::vector<HWND> want;
	std::lock_guard lock(shotLock);
	shotScreen = sr;
	auto px = [](long v, int n, int nw) { return int(std::floor(double(v) * n / nw)); }; // screen -> sim pixels
	for (auto &[id, m] : sim->windowMarks)
	{
		HWND top = HWND(id);
		if (!m.any[Simulation::MarkHole] || !wallRects.count(top)) continue; // icons: just the wallpaper
		auto &hole = m.a[Simulation::MarkHole];
		struct Under { int x0, y0; const Shot *shot; uintptr_t id; bool desk; RECT seen; }; // seen: its visible frame, sim px
		std::vector<Under> under;
		for (HWND h = GetWindow(top, GW_HWNDNEXT); h && under.size() < 12; h = GetWindow(h, GW_HWNDNEXT))
		{
			BOOL cloaked = FALSE;
			DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
			wchar_t cls[32];
			GetClassNameW(h, cls, 32);
			RECT r;
			bool desk = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW"); // the desktop: icons, and a live wallpaper app's window
			if (h == mainWindow || !IsWindowVisible(h) || IsIconic(h) || cloaked || !GetWindowRect(h, &r))
				continue;
			int x0 = px(r.left - sr.left, WINDOWW, sw), y0 = px(r.top - sr.top, WINDOWH, sh);
			int x1 = px(r.right - sr.left, WINDOWW, sw), y1 = px(r.bottom - sr.top, WINDOWH, sh);
			if (x1 <= m.x || y1 <= m.y || x0 >= m.x + m.w || y0 >= m.y + m.h) continue;
			want.push_back(h);
			auto it = shots.find(h);
			// the shot takes in the invisible resize borders round the frame, black: not what is seen there
			RECT f = r;
			if (!desk) DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &f, sizeof(f));
			RECT seen{ px(f.left - sr.left, WINDOWW, sw), px(f.top - sr.top, WINDOWH, sh), px(f.right - sr.left, WINDOWW, sw), px(f.bottom - sr.top, WINDOWH, sh) };
			if (it != shots.end()) under.push_back({ x0, y0, &it->second, uintptr_t(h), desk, seen });
		}
		for (int j = 0; j < m.h; j++)
			for (int i = 0; i < m.w; i++)
			{
				int x = m.x + i, y = m.y + j;
				if (!hole[size_t(j) * m.w + i] || x < 0 || y < 0 || x >= XRES || y >= YRES) continue;
				for (auto &u : under)
				{
					int sx = x - u.x0, sy = y - u.y0;
					if (sx < 0 || sy < 0 || sx >= u.shot->w || sy >= u.shot->h) continue;
					if (x < u.seen.left || y < u.seen.top || x >= u.seen.right || y >= u.seen.bottom) continue;
					if (auto o = sim->windowMarks.find(u.id); o != sim->windowMarks.end() && o->second.any[Simulation::MarkHole]) // broken through here too
					{
						auto &om = o->second;
						int ox = x - om.x, oy = y - om.y;
						if (ox >= 0 && oy >= 0 && ox < om.w && oy < om.h && om.a[Simulation::MarkHole][size_t(oy) * om.w + ox]) continue;
					}
					uint32_t c = u.shot->px[size_t(sy) * u.shot->w + sx];
					if (u.desk && !c) continue; // the desktop window drew nothing here: the wallpaper file below
					behind[size_t(y) * XRES + x] = c;
					break;
				}
			}
	}
	shotWanted = std::move(want);
	return behind.data();
}
#else
// What is really behind holed windows: a capture of everything under each one, a few times a second (OverlayMac's worker
// thread), over the wallpaper. Without Screen Recording, just the wallpaper.
static const uint32_t *HoleBehind(const Simulation *sim, RECT sr)
{
	static std::vector<uint32_t> wallpaper, behind;
	static ULONGLONG built = 0;
	int sw = sr.right - sr.left;
	if (wallpaper.empty() || GetTickCount64() - built > 30000)
	{
		built = GetTickCount64();
		wallpaper.resize(size_t(XRES) * YRES);
		// the sim is the window's top-left corner: the wallpaper is read for the window, then cut down to it
		std::vector<uint32_t> whole(size_t(WINDOWW) * WINDOWH);
		OverlayMac::Wallpaper(WINDOWW, WINDOWH, whole.data());
		for (int y = 0; y < YRES; y++) std::copy_n(whole.begin() + size_t(y) * WINDOWW, XRES, wallpaper.begin() + size_t(y) * XRES);
	}
	behind = wallpaper;
	std::vector<uint32_t> want;
	auto px = [](long v, int n, int nw) { return int(std::floor(double(v) * n / nw)); }; // screen -> sim pixels
	for (auto &[id, m] : sim->windowMarks)
	{
		auto it = wallRects.find(HWND(id));
		if (!m.any[Simulation::MarkHole] || it == wallRects.end()) continue; // icons: just the wallpaper
		want.push_back(uint32_t(id));
		auto shot = OverlayMac::TakeShot(uint32_t(id));
		if (!shot) continue;
		int x0 = px(it->second.left - sr.left, WINDOWW, sw), y0 = px(it->second.top - sr.top, WINDOWH, sr.bottom - sr.top);
		auto &hole = m.a[Simulation::MarkHole];
		for (int j = 0; j < m.h; j++)
			for (int i = 0; i < m.w; i++)
			{
				int x = m.x + i, y = m.y + j, sx = x - x0, sy = y - y0;
				if (!hole[size_t(j) * m.w + i] || x < 0 || y < 0 || x >= XRES || y >= YRES || sx < 0 || sy < 0 || sx >= shot->w || sy >= shot->h) continue;
				behind[size_t(y) * XRES + x] = shot->px[size_t(sy) * shot->w + sx];
			}
	}
	OverlayMac::WantShots(want, double(WINDOWW) * MacScale() / sw);
	return behind.data();
}
#endif

void OverlayWindows::ApplySimMasks(uint32_t *argb, int w, const uint8_t *occupied)
{
	if (!&GameController::Ref())
	{
		return;
	}
	// ponytail: assumes the sim is drawn 1:1 at the top-left
	auto *model = GameController::Ref().GetModel();
	auto *sim = model->GetSimulation();
	auto area = sim->PlayRect();
	bool full = sim->playArea == CELLS.OriginRect();
	// The zoom window is a magnifier: opaque, its frame included, black particles and all.
	auto zoom = RectSized(model->GetZoomWindowPosition() - Vec2{ 2, 2 }, Vec2{ 1, 1 } * (model->GetZoomSize() * model->GetZoomFactor() + 4));
	bool zoomOn = model->GetZoomEnabled();
	// a row at a time (the play area and zoom box are spans of it), so the inner loop is plain enough to vectorise
	auto ab = area.BottomRight(), zb = zoom.BottomRight();
	OverlayBands(YRES, [&](int y0, int y1) {
	std::vector<uint32_t> saved;
	for (int y = y0; y < y1; y++)
	{
		auto *row = argb + size_t(y) * w;
		auto *occ = occupied + size_t(y) * XRES;
		bool inZoom = zoomOn && y >= zoom.pos.Y && y <= zb.Y;
		int zx0 = inZoom ? std::clamp(zoom.pos.X, 0, XRES) : 0, zx1 = inZoom ? std::clamp(zb.X + 1, 0, XRES) : 0;
		int zn = std::max(0, zx1 - zx0);
		saved.assign(row + zx0, row + zx0 + zn); // the magnifier is drawn as it is
		if (y < area.pos.Y || y > ab.Y)
			std::fill(row, row + XRES, 0u);
		else
		{
			int ax0 = std::clamp(area.pos.X, 0, XRES), ax1 = std::clamp(ab.X + 1, 0, XRES);
			std::fill(row, row + ax0, 0u);
			std::fill(row + ax1, row + XRES, 0u);
			for (int x = ax0; x < ax1; x++)
			{
				uint32_t p = row[x];
				uint8_t o = occ[x];
				// a window's wall (as of the frame: the live ones may have moved since) shows the window; a black particle is solid
				row[x] = (o & 2) ? 0u : (!(p & 0xFFFFFF) && (o & 1)) ? 0xFF000000u : p;
			}
		}
		for (int i = 0; i < zn; i++) row[zx0 + i] = saved[i] | 0xFF000000;
	}
	});
	const uint32_t *backdrop = nullptr;
	if (std::any_of(sim->windowMarks.begin(), sim->windowMarks.end(), [](auto &m) { return m.second.any[Simulation::MarkHole]; }))
	{
		RECT sr;
		GetWindowRect(mainWindow, &sr);
		if (sr.right > sr.left && sr.bottom > sr.top) backdrop = HoleBehind(sim, sr);
	}
	OverlayDrawMarks(sim, argb, w, occupied, 2, backdrop);
	if (!full)
	{
		// A faint frame just outside the area, so you can see where it ends (Simulation tab: Show its edge).
		auto br = area.BottomRight();
		// Around it, a band you can't see but can grab to drag the edges (alpha 0 lets clicks through).
		auto mark = [&](int x, int y, uint32_t c) {
			if (x >= 0 && y >= 0 && x < XRES && y < YRES) argb[y * w + x] = c;
		};
		for (int d = 4; d >= 1; d--)
		{
			uint32_t c = d == 1 && areaFrame ? 0x785080C0 : 0x01000000;
			for (int x = area.pos.X - d; x <= br.X + d; x++) mark(x, area.pos.Y - d, c), mark(x, br.Y + d, c);
			for (int y = area.pos.Y - d + 1; y < br.Y + d; y++) mark(area.pos.X - d, y, c), mark(br.X + d, y, c);
		}
	}
}

#ifdef _WIN32
// The desktop icons: where each sits on screen (grid or free placement) and its shape (the icon's own picture from the
// shell, opaque pixels solid), through the desktop's IFolderView. Positions are re-read every few frames, so dragged
// icons push sand live; shapes are cached per file and size. Empty when Explorer isn't there or icons are hidden. All
// of it runs on its own thread (fetching icon pictures takes seconds at first), so frames never wait on Explorer.
struct DeskIcon
{
	RECT r;
	std::shared_ptr<const std::vector<uint8_t>> shape; // (r.right - r.left) squared, 1 = solid; null = the whole square
	uintptr_t id; // the same item from pass to pass (a hash of its item ID)
	bool bin = false; // the Recycle Bin
};
// Null (a plain square for now) when not cached yet and this pass's budget of new pictures is spent: a desktop can
// hold hundreds of icons, and fetching them all at once takes seconds.
static std::shared_ptr<const std::vector<uint8_t>> IconShape(PCUITEMID_CHILD child, int size, int &budget)
{
	// keyed by the item's ID bytes (a name lookup per icon per read cost more than the rest of the read)
	static std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> cache;
	std::string key = std::to_string(size) + "|" + std::string((const char *)child, ILGetSize(child));
	if (auto it = cache.find(key); it != cache.end()) return it->second;
	if (budget-- <= 0) return nullptr;
	auto shape = std::make_shared<std::vector<uint8_t>>(size_t(size) * size, 1); // a square if the picture fails
	// The shell's own cached icon list, drawn at the desktop's size: ~1 ms an icon, where IShellItemImageFactory took ~30
	// (a full desktop sat as squares for 15 s). The desktop is the namespace root, so the child ID is an absolute one.
	SHFILEINFOW fi{};
	IImageList *images = nullptr;
	HICON icon = nullptr;
	if (SHGetFileInfoW(LPCWSTR(child), 0, &fi, sizeof(fi), SHGFI_PIDL | SHGFI_SYSICONINDEX) &&
	    SUCCEEDED(SHGetImageList(size > 48 ? SHIL_JUMBO : size > 32 ? SHIL_EXTRALARGE : SHIL_LARGE, IID_PPV_ARGS(&images))) &&
	    SUCCEEDED(images->GetIcon(fi.iIcon, ILD_TRANSPARENT, &icon)))
	{
		BITMAPINFO bi{};
		bi.bmiHeader = { sizeof(bi.bmiHeader), size, -size, 1, 32, BI_RGB };
		uint32_t *px = nullptr;
		HDC dc = CreateCompatibleDC(nullptr);
		if (HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, nullptr, 0))
		{
			auto old = SelectObject(dc, bmp);
			std::fill(px, px + size * size, 0u);
			DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
			GdiFlush();
			bool alpha = std::any_of(px, px + size * size, [](uint32_t c) { return c >> 24; });
			for (int i = 0; i < size * size; i++) // old icons without alpha: whatever the mask let through
				(*shape)[i] = alpha ? px[i] >> 24 >= 128 : (px[i] & 0xFFFFFF) != 0;
			SelectObject(dc, old);
			DeleteObject(bmp);
		}
		DeleteDC(dc);
		DestroyIcon(icon);
	}
	if (images) images->Release();
	return cache[key] = shape;
}
static std::vector<DeskIcon> ReadDesktopIcons(IFolderView2 *&view, HWND &list)
{
	std::vector<DeskIcon> icons;
	if (!view)
	{
		IShellWindows *windows = nullptr;
		if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) return icons;
		VARIANT loc{}, root{};
		loc.vt = VT_I4, loc.lVal = CSIDL_DESKTOP;
		long found = 0;
		IDispatch *disp = nullptr;
		IServiceProvider *services = nullptr;
		IShellBrowser *browser = nullptr;
		IShellView *shellView = nullptr;
		if (windows->FindWindowSW(&loc, &root, SWC_DESKTOP, &found, SWFO_NEEDDISPATCH, &disp) == S_OK &&
		    SUCCEEDED(disp->QueryInterface(IID_PPV_ARGS(&services))) &&
		    SUCCEEDED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) &&
		    SUCCEEDED(browser->QueryActiveShellView(&shellView)) && SUCCEEDED(shellView->QueryInterface(IID_PPV_ARGS(&view))))
		{
			HWND defView = nullptr;
			shellView->GetWindow(&defView);
			list = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
		}
		for (IUnknown *p : std::initializer_list<IUnknown *>{ shellView, browser, services, disp, windows })
			if (p) p->Release();
		if (!view) return icons;
	}
	FOLDERVIEWMODE mode;
	int size = 0;
	POINT spacing{};
	IEnumIDList *items = nullptr;
	if (FAILED(view->GetViewModeAndIconSize(&mode, &size)) || FAILED(view->GetSpacing(&spacing)) ||
	    FAILED(view->Items(SVGIO_ALLVIEW, IID_PPV_ARGS(&items))))
	{
		view->Release(), view = nullptr; // Explorer restarted: find it again
		return icons;
	}
	// ponytail: the icon rect is a guess from its position (no per-item rect from IFolderView); ~15 ms a new shape, so a
	// full desktop (443 items here) takes ~7 s to get them all
	if (IsWindowVisible(list))
	{
		PITEMID_CHILD item;
		int budget = 64;
		while (items->Next(1, &item, nullptr) == S_OK)
		{
			POINT p;
			if (SUCCEEDED(view->GetItemPosition(item, &p)))
			{
				ClientToScreen(list, &p);
				int x = p.x, y = p.y + 6; // matched by eye against sand resting on icons
				auto id = std::hash<std::string_view>{}({ (const char *)item, ILGetSize(item) });
				static PIDLIST_ABSOLUTE binItem = [] {
					PIDLIST_ABSOLUTE p = nullptr;
					SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &p);
					return p;
				}();
				bool bin = binItem && ILIsEqual(binItem, PCIDLIST_ABSOLUTE(item)); // a desktop item's ID is also its full one
				icons.push_back({ { x, y, x + size, y + size }, IconShape(item, size, budget), id, bin });
			}
			CoTaskMemFree(item);
		}
	}
	items->Release();
	return icons;
}
static std::vector<DeskIcon> DesktopIcons()
{
	static std::mutex lock;
	static std::vector<DeskIcon> latest;
	static std::atomic<int> wanted{ 0 }; // frames since the last ask; the thread idles when nobody asks
	wanted = 0;
	static bool started = [] {
		std::thread([] {
			SetThreadDescription(GetCurrentThread(), L"overlay desktop icons");
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			IFolderView2 *view = nullptr;
			HWND list = nullptr;
			int still = 0; // reads since the icons last changed
			while (true)
			{
				if (wanted++ > 10)
				{
					Sleep(200);
					continue;
				}
				bool had = view;
				auto icons = ReadDesktopIcons(view, list);
				{
					std::lock_guard g(lock);
					bool same = std::equal(icons.begin(), icons.end(), latest.begin(), latest.end(), [](auto &a, auto &b) {
						return EqualRect(&a.r, &b.r) && a.shape == b.shape;
					});
					still = same ? still + 1 : 0;
					latest = std::move(icons);
				}
				// A read is ~20 ms of round trips to Explorer: fast only while icons may be moving (they just changed, or
				// a button is held on the desktop), otherwise twice a second.
				auto dragging = [] {
					wchar_t cls[16]{};
					GetClassNameW(GetForegroundWindow(), cls, 16);
					return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) && (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW"));
				};
				if (!view) Sleep(had ? 0 : 1000); // looking for Explorer: once a second
				else if (still < 25 || dragging()) Sleep(8);
				else for (int i = 0; i < 20 && !dragging(); i++) Sleep(25);
			}
		}).detach();
		return true;
	}();
	(void)started;
	std::lock_guard g(lock);
	return latest;
}

#else
// The desktop icons and the Trash, as OverlayMac's Accessibility thread last read them (none without the permission)
struct DeskIcon
{
	RECT r;
	std::shared_ptr<const std::vector<uint8_t>> shape; // (r.right - r.left) squared, 1 = solid; null = the whole box
	uintptr_t id;
	bool bin = false; // the Trash
};

static std::vector<DeskIcon> DesktopIcons()
{
	std::vector<DeskIcon> out;
	double s = MacScale();
	for (auto &i : OverlayMac::Icons())
		out.push_back({ { long(i.l * s), long(i.t * s), long(i.r * s), long(i.b * s) }, i.shape, uintptr_t(i.id), false });
	if (fun.bin)
	{
		// The Dock is under the sim: the Trash is a low block on the floor above it, as wide as its icon
		RECT floor;
		GetWindowRect(macDock, &floor);
		for (auto &d : OverlayMac::DockItems())
			if (d.trash) out.push_back({ { long(d.l * s), floor.top - long(6 * s), long(d.r * s), floor.top }, nullptr, 3, true });
	}
	return out;
}
#endif

// Every frame: every visible window's rectangle, in sim pixels, blocks particles (Simulation::pixelWalls), so they
// pile on title bars and flow around windows. A window moving onto particles shoves each one along its move to the
// first free pixel past it, at the window's speed; one that appeared or grew lifts them out over its top. One with
// no room left stays put, hidden under the window (never deleted).

void OverlayWindows::FrameWalls(std::vector<uint8_t> &mask, uint8_t bit)
{
	{
		std::lock_guard lock(frameLock);
		frameRects = wallRects;
	}
	if (!&GameController::Ref()) return;
	auto &walls = GameController::Ref().GetModel()->GetSimulation()->pixelWalls;
	for (size_t i = 0; i < walls.size() && i < mask.size(); i++)
		if (walls[i]) mask[i] |= bit;
}

std::vector<OverlayWindows::WindowShift> OverlayWindows::WindowShifts()
{
	std::vector<WindowShift> out;
	RECT sr;
	GetWindowRect(mainWindow, &sr);
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	if (sw <= 0 || sh <= 0) return out;
	auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); }; // as TickWindowWalls
	std::lock_guard lock(frameLock);
#ifdef __APPLE__
	if (frameRects.empty()) return out;
	auto live = OverlayMac::OtherWindows(); // one read for them all, at every screen refresh
#endif
	for (auto &[hwnd, at] : frameRects)
	{
		RECT r;
#ifdef _WIN32
		if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) != S_OK) GetWindowRect(hwnd, &r);
#else
		auto it = std::find_if(live.begin(), live.end(), [hwnd = hwnd](auto &w) { return w.id == uint32_t(uintptr_t(hwnd)); });
		if (it == live.end()) continue;
		r = MacRect(*it);
#endif
		if (r.left == at.left && r.top == at.top) continue;
		out.push_back({ px(at.left - sr.left, WINDOWW, sw), px(at.top - sr.top, WINDOWH, sh), px(at.right - sr.left, WINDOWW, sw),
		                px(at.bottom - sr.top, WINDOWH, sh), float(double(r.left - at.left) * WINDOWW / sw),
		                float(double(r.top - at.top) * WINDOWH / sh), hwnd });
	}
	return out;
}

#ifdef _WIN32
// The exe name (lower case) of the app owning a window
static ByteString ExeName(HWND hwnd)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!proc) return {};
	wchar_t path[MAX_PATH];
	DWORD n = MAX_PATH;
	bool ok = QueryFullProcessImageNameW(proc, 0, path, &n);
	CloseHandle(proc);
	if (!ok) return {};
	std::wstring name(path, n);
	name = name.substr(name.find_last_of(L"\\/") + 1);
	CharLowerW(name.data());
	return Platform::WinNarrow(name);
}

static std::vector<ByteString> PassApps()
{
	std::vector<ByteString> out;
	for (auto &s : GlobalPrefs::Ref().Get("Overlay.PassApps", ByteString("")).PartitionBy(';'))
		if (s.size()) out.push_back(s);
	return out;
}

static void SetPassApps(const std::vector<ByteString> &apps)
{
	ByteString s;
	for (auto &a : apps) s += (s.size() ? ";" : "") + a;
	GlobalPrefs::Ref().Set("Overlay.PassApps", s);
}

// Apps with a normal window open now, but not us
static std::vector<ByteString> RunningApps()
{
	std::vector<ByteString> out;
	EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
		BOOL cloaked = FALSE;
		DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (!IsWindowVisible(hwnd) || cloaked || GetWindow(hwnd, GW_OWNER) || (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
		    !GetWindowTextLengthW(hwnd) || pid == GetCurrentProcessId())
		{
			return TRUE;
		}
		auto &out = *(std::vector<ByteString> *)lp;
		auto name = ExeName(hwnd);
		if (name.size() && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
		return TRUE;
	}, LPARAM(&out));
	std::sort(out.begin(), out.end());
	return out;
}

ByteString OverlayWindows::PassApp(void *hwnd)
{
	if (!hwnd) return {};
	auto apps = PassApps();
	if (apps.empty()) return {};
	auto name = ExeName(HWND(hwnd));
	return std::find(apps.begin(), apps.end(), name) != apps.end() ? name : ByteString();
}

// Throws what sits in the band above a sim rectangle into the air: up hard when the window under it closes, a
// lighter bump when one opens below it.
#else
static std::vector<ByteString> PassApps()
{
	std::vector<ByteString> out;
	for (auto &s : GlobalPrefs::Ref().Get("Overlay.PassApps", ByteString("")).PartitionBy(';'))
		if (s.size()) out.push_back(s);
	return out;
}

static void SetPassApps(const std::vector<ByteString> &apps)
{
	ByteString all;
	for (auto &a : apps) all += (all.size() ? ";" : "") + a;
	GlobalPrefs::Ref().Set("Overlay.PassApps", all);
}

static std::vector<ByteString> RunningApps()
{
	std::vector<ByteString> out;
	for (auto &w : OverlayMac::OtherWindows())
		if (w.owner.size() && std::find(out.begin(), out.end(), ByteString(w.owner).ToLower()) == out.end()) out.push_back(ByteString(w.owner).ToLower());
	return out;
}

ByteString OverlayWindows::PassApp(void *)
{
	auto front = OverlayMac::FrontName();
	for (auto &a : PassApps())
		if (std::string(a.ToLower()) == front) return a;
	return {};
}
#endif
static void TossAbove(Simulation *sim, int x0, int y0, int x1, bool close)
{
	auto &elements = SimulationData::CRef().elements;
	for (int y = std::max(0, y0 - (close ? 40 : 80)); y < std::min(YRES, y0); y++)
		for (int x = std::max(0, x0); x < std::min(XRES, x1); x++)
		{
			int r = sim->pmap[y][x];
			if (!r || !(elements[TYP(r)].Properties & (TYPE_PART | TYPE_LIQUID))) continue;
			auto &p = sim->parts[ID(r)];
			p.vy -= close ? 1.5f + 3 * sim->rng.uniform01() : 1 + 2 * sim->rng.uniform01();
			p.vx += (sim->rng.uniform01() - 0.5f) * (close ? 5 : 2);
		}
}

// The Fun tab's pets, weather and music, once per sim tick (not while paused)
// A cold spark (EMBR at room temperature: it sets nothing alight) in a glitter colour, drifting up
static void ColdSpark(Simulation *sim, int x, int y)
{
	if (x < CELL || y < CELL || x >= XRES - CELL || y >= YRES - CELL || sim->pmap[y][x] || sim->PixelWall(x, y)) return;
	if (int i = sim->create_part(-1, x, y, PT_EMBR); i >= 0)
	{
		auto &p = sim->parts[i];
		p.temp = 295.15f, p.life = sim->rng.between(20, 40);
		p.vx = (sim->rng.uniform01() - 0.5f) * 1.5f, p.vy = -sim->rng.uniform01();
		static const uint32_t glitter[] = { 0xFFFFF0A0, 0xFFA0E0FF, 0xFFFFA0E0, 0xFFC0FFA0 };
		p.dcolour = glitter[sim->rng.between(0, 3)];
	}
}

static void TickFun(Simulation *sim)
{
	static int last = -1;
	if (sim->currentTick == last) return;
	last = sim->currentTick;
	auto &elements = SimulationData::CRef().elements;

	// Pets: fighters that wander (FIGH's PetComm), dropped in from the top every two seconds while there are too few
	sim->petWander = fun.pets >= 1;
	static std::vector<int> pets;
	std::erase_if(pets, [sim](int i) { return sim->parts[i].type != PT_FIGH; });
	while (int(pets.size()) > int(fun.pets))
	{
		sim->kill_part(pets.back());
		pets.pop_back();
	}
	if (sim->currentTick % 120 == 0 && int(pets.size()) < int(fun.pets))
	{
		int i = sim->create_part(-1, sim->rng.between(XRES / 10, XRES * 9 / 10), 8, PT_FIGH);
		if (i >= 0) pets.push_back(i);
	}

	// Weather from the top edge; each flake, drop or leaf goes after a while (snow even once melted)
	static std::deque<std::tuple<int, int, int>> fallen; // particle, type, the tick it goes
	static int rainLast = -1, rainSince = -1; // the last tick rain fell, and since when it has been falling
	LASTINPUTINFO input{ sizeof(input) };
	// back after a minute or more away: the pets hop to greet you
	static bool away = false;
	if (GetLastInputInfo(&input))
	{
		DWORD idleMs = GetTickCount() - input.dwTime;
		if (idleMs >= 60000) away = true;
		else if (away && idleMs < 1000) away = false, sim->petGreet = sim->currentTick + int(fun.welcome * 60);
		sim->petGather = fun.gather >= 1 && idleMs >= DWORD(fun.gather * 60000);
	}
	// Day and night, by the clock: fireflies from 19:00 to 05:00 (and darker leaves), a flock of birds now and then
	// from 06:00 to 10:00
	int hour;
	{
		time_t now = time(nullptr);
		tm local;
		localtime_s(&local, &now);
		hour = local.tm_hour;
	}
	bool dusk = fun.dayNight && (hour >= 19 || hour < 5);
	// Pets nap: in the nap hours, once the mouse and keys have been still 10 s
	{
		int from = int(fun.napFrom) % 24, until = int(fun.napUntil);
		bool napHours = fun.napFrom >= 1 && (from < until ? hour >= from && hour < until : hour >= from || hour < until);
		LASTINPUTINFO still{ sizeof(still) };
		sim->petNap = napHours && GetLastInputInfo(&still) && GetTickCount() - still.dwTime >= 10000;
	}
	static std::vector<std::pair<int, int>> visitors; // particle, the tick it goes (fireflies: never, while it's dark)
	std::erase_if(visitors, [&](auto &v) {
		auto &p = sim->parts[v.first];
		bool firefly = v.second < 0;
		if (p.type != PT_BIRD || (p.tmp == 1) != firefly) return true; // gone, or the slot reused
		if ((firefly && !dusk) || (!firefly && sim->currentTick >= v.second)) return sim->kill_part(v.first), true;
		if (firefly) p.dcolour = (sim->currentTick + v.first * 37) % 160 < 50 ? 0xFFFFF070 : 0xFF404020; // blink
		return false;
	});
	auto open = [sim](int x, int y) { // on screen, not in a window or on anything
		return x >= CELL && y >= CELL && x < XRES - CELL && y < YRES - CELL && !sim->pmap[y][x] && !sim->PixelWall(x, y);
	};
	if (dusk && visitors.size() < 12 && sim->rng.chance(1, 60))
	{
		int x = sim->rng.between(XRES / 10, XRES * 9 / 10), y = YRES - sim->rng.between(40, 160);
		if (open(x, y))
			if (int i = sim->create_part(-1, x, y, PT_BIRD); i >= 0) sim->parts[i].tmp = 1, visitors.push_back({ i, -1 });
	}
	if (fun.dayNight && hour >= 6 && hour < 10 && sim->rng.chance(1, 7200))
	{
		// somewhere open in the top half (windows are walls: one spawned inside would be stuck)
		float vx = sim->rng.chance(1, 2) ? 1.5f : -1.5f;
		int x0 = 0, y0 = 0;
		for (int tries = 0, made = 0; tries < 60 && made < 7; tries++)
		{
			if (!made) x0 = sim->rng.between(XRES / 10, XRES * 9 / 10), y0 = sim->rng.between(CELL, std::max(CELL, YRES / 2));
			int x = x0 + sim->rng.between(-10, 10), y = y0 + sim->rng.between(-6, 6);
			if (open(x, y))
				if (int i = sim->create_part(-1, x, y, PT_BIRD); i >= 0)
					sim->parts[i].vx = vx, visitors.push_back({ i, sim->currentTick + 3600 }), made++;
		}
	}
	bool idle = fun.weatherIdle < 1 || (GetLastInputInfo(&input) && GetTickCount() - input.dwTime >= DWORD(fun.weatherIdle * 60000));
	if (fun.weather > 0 && idle)
	{
		int kind = fun.weatherKind;
		if (!kind)
		{
			time_t now = time(nullptr);
			tm local;
			localtime_s(&local, &now);
			int m = local.tm_mon + 1;
			kind = m == 12 || m <= 2 ? 1 : m >= 9 && m <= 11 ? 3 : 2;
			// Weekday weather: Monday rain, Tuesday fog, Wednesday hail, Thursday snow, Friday leaves; weekends by the season
			if (fun.weekday && local.tm_wday >= 1 && local.tm_wday <= 5) kind = std::array{ 2, 5, 4, 1, 3 }[local.tm_wday - 1];
		}
		static const uint32_t autumn[] = { 0xFFC0501A, 0xFFD9822B, 0xFFB8860B, 0xFF8B4513, 0xFFCC3300 };
		float n = fun.weather * XRES / 600.f;
		int count = int(n) + (sim->rng.uniform01() < n - int(n));
		for (int k = 0; k < count; k++)
		{
			int type = kind == 1 || kind == 4 ? PT_SNOW : kind == 2 ? PT_WATR : kind == 5 ? PT_FOG : PT_SAWD;
			if (kind == 5 && k % 4) continue; // fog: fewer, it lingers
			int y0 = kind == 5 ? sim->rng.between(YRES * 2 / 3, YRES - CELL - 1) : CELL; // fog forms low down
			int i = sim->create_part(-1, sim->rng.between(CELL, XRES - CELL - 1), y0, type); // the outer cell kills particles
			if (i < 0) continue;
			auto &p = sim->parts[i];
			if (kind == 1) p.temp = 263.15f, p.vx = sim->rng.uniform01() - 0.5f;
			if (kind == 2) p.vy = 3;
			if (kind == 3) p.dcolour = autumn[sim->rng.between(0, 4)], p.vx = (sim->rng.uniform01() - 0.5f) * 2;
			if (kind == 3 && dusk) p.dcolour = 0xFF000000 | ((p.dcolour >> 1) & 0x7F7F7F); // leaves after dark
			if (kind == 4) p.temp = 253.15f, p.vy = 4 + 2 * sim->rng.uniform01(), p.dcolour = overlayHailColour;
			float life = std::max(10.f, fun.weatherLast) * 60 * (0.6f + 0.8f * sim->rng.uniform01()); // spread: they thin out
			if (kind == 2) life = std::min(life, 300.f); // rain soaks away quickly
			if (kind == 5) life = std::min(life, 1200.f), p.temp = 288.15f, p.life = int(life) + 60; // fog: no rime while it lasts
			fallen.push_back({ i, type, sim->currentTick + int(life) });
		}
		if (kind == 2) rainLast = sim->currentTick;
		if (kind == 2 && sim->rng.uniform01() < fun.weather / 1200) // a storm: now and then a bolt from the top
			sim->create_part(-1, sim->rng.between(XRES / 20, XRES * 19 / 20), CELL, PT_LIGH, 20);
	}
	for (size_t k = 0, n = fallen.size(); k < n; k++) // all of them: their times aren't in order
	{
		auto [i, type, until] = fallen.front();
		fallen.pop_front();
		if (until > sim->currentTick)
		{
			fallen.push_back({ i, type, until });
			continue;
		}
		int t = sim->parts[i].type, x = int(sim->parts[i].x + 0.5f), y = int(sim->parts[i].y + 0.5f);
		if (type == PT_BRCK && t == PT_BRCK) // a sandcastle crumbles back into sand
		{
			sim->part_change_type(i, x, y, PT_SAND);
			continue;
		}
		if (type == PT_WATR && t == PT_WATR && fun.puddles >= 1 && y + 1 < YRES && (sim->pmap[y + 1][x] || sim->PixelWall(x, y + 1)))
		{
			// rain lying on something: a puddle, for a while longer (marked so it isn't kept twice)
			fallen.push_back({ i, PT_WATR | 0x10000, sim->currentTick + int(fun.puddles * 60 * (0.5f + sim->rng.uniform01())) });
			continue;
		}
		type &= 0xFFFF;
		if (t == type || (type == PT_SNOW && t == PT_WATR)) sim->kill_part(i);
	}
	// Holidays, by the date: pumpkins (balls) on Halloween, jelly hearts (JOIN) on Valentine's Day, fireworks in the
	// first hour of New Year's Day
	if (fun.holidays)
	{
		time_t now = time(nullptr);
		tm local;
		localtime_s(&local, &now);
		int month = local.tm_mon + 1, day = local.tm_mday;
		int cx = sim->rng.between(XRES / 10, XRES * 9 / 10);
		if (month == 10 && day == 31 && sim->currentTick % 900 == 0)
		{
			float vx = (sim->rng.chance(1, 2) ? 1 : -1) * (1.5f + sim->rng.uniform01());
			for (int y = -4; y <= 4; y++)
				for (int x = -4; x <= 4; x++)
					if (x * x + y * y <= 18)
						if (int i = sim->create_part(-1, cx + x, 12 + y, PT_BALL); i >= 0) sim->parts[i].ctype = 0xFF7518, sim->parts[i].tmp2 = int(vx * 100);
		}
		if (month == 2 && day == 14 && sim->currentTick % 300 == 0)
		{
			static const char *heart[] = { ".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..." };
			for (int y = 0; y < 12; y++)
				for (int x = 0; x < 14; x++)
					if (heart[y / 2][x / 2] == 'X')
						if (int i = sim->create_part(-1, cx + x, 10 + y, PT_JOIN); i >= 0) sim->parts[i].dcolour = 0xFFE0305A;
		}
		if (month == 1 && day == 1 && local.tm_hour == 0 && sim->rng.chance(1, 40))
		{
			int y = YRES - 30; // from the first free spot up from the bottom (over the taskbar)
			while (y > YRES / 2 && (sim->pmap[y][cx] || sim->PixelWall(cx, y))) y--;
			if (int i = sim->create_part(-1, cx, y, PT_FIRW); i >= 0)
			{
				auto &p = sim->parts[i];
				p.tmp = 1, p.life = sim->rng.between(20, 29); // lit, as by fire (FIRW.cpp)
				p.vy = -(p.life + 20) * 0.2f, p.vx = sim->rng.uniform01() - 0.5f;
			}
		}
	}
	// Wind: a gust that turns every few seconds; falling leaves flutter and drift with it, snow a little, rain not
	static float gust = 0, gustTo = 0;
	if (sim->currentTick % 240 == 0) gustTo = (sim->rng.uniform01() * 2 - 1) * 1.5f;
	gust += (gustTo - gust) * 0.01f;
	for (auto &[i, type, until] : fallen)
	{
		auto &p = sim->parts[i];
		int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
		if (p.type != type || type == PT_WATR || x < 0 || x >= XRES || y < 0 || y + 1 >= YRES || sim->pmap[y + 1][x] || sim->PixelWall(x, y + 1))
			continue; // gone, rain, or resting
		float k = type == PT_SAWD ? 1 : 0.4f;
		p.vx += (k * (gust + 0.6f * std::sin(sim->currentTick * 0.07f + i)) - p.vx) * 0.08f;
		if (type == PT_SAWD) p.vy = std::min(p.vy, 0.8f + 0.5f * std::sin(sim->currentTick * 0.11f + i * 1.7f)); // flutter
	}

	// Music: the speakers' peak level (Windows' own meter, nothing recorded); a jump over the recent level is a beat
	// that makes resting sand and liquid hop and fire flare
	if (fun.music > 0)
	{
#ifdef _WIN32
		static IAudioMeterInformation *meter = nullptr;
		static int retry = 0;
		if (!meter && --retry <= 0)
		{
			retry = 300;
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			IMMDeviceEnumerator *devices = nullptr;
			IMMDevice *speakers = nullptr;
			if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
			    SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &speakers)))
			{
				speakers->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void **)&meter);
			}
			if (speakers) speakers->Release();
			if (devices) devices->Release();
		}
		float peak = 0;
		if (meter && FAILED(meter->GetPeakValue(&peak)))
		{
			meter->Release(); // the speakers changed: find the new ones
			meter = nullptr;
		}
#else
		float peak = OverlayMac::AudioPeak();
#endif
		static float level = 0;
		static int since = 0;
		bool beat = ++since > 6 && peak > 0.03f && peak > level * 1.15f;
		level = level * 0.9f + peak * 0.1f;
		if (beat)
		{
			since = 0;
			float hop = fun.music * (0.4f + peak), chance = std::min(1.f, 0.3f + hop * 0.5f);
			for (int i = 0; i < std::min(NPART, sim->parts.active + 1); i++)
			{
				auto &p = sim->parts[i];
				if (p.type == PT_FIRE)
				{
					p.life += int(20 * hop);
				}
				else if (p.type && (elements[p.type].Properties & (TYPE_PART | TYPE_LIQUID)) && std::abs(p.vy) < 0.3f && sim->rng.uniform01() < chance)
				{
					p.vy -= 1.5f + 3 * hop;
					p.vx += (sim->rng.uniform01() - 0.5f) * hop * 2;
				}
			}
		}
	}
	// Sparkle trail: a moving pointer sheds a few cold sparks (EMBR at room temperature: they set nothing alight)
	if (auto &m = sim->petPointer; fun.trail && m.speed > 2 && m.x > CELL && m.y > CELL && m.x < XRES - CELL && m.y < YRES - CELL)
		for (int k = 0; k < 2; k++) ColdSpark(sim, m.x + sim->rng.between(-2, 2), m.y + sim->rng.between(-2, 2));
	// Confetti: the pointer wiggled fast (4 turns within 40 ticks) throws a burst of coloured flakes (sawdust, so the
	// wind flutters them like leaves) that go like the weather does
	static int lastX = -1, lastSign = 0, cooldown = 0;
	static std::deque<int> turns;
	if (auto &m = sim->petPointer; fun.confetti && m.x >= 0)
	{
		int dx = lastX >= 0 ? m.x - lastX : 0, sign = dx >= 4 ? 1 : dx <= -4 ? -1 : 0;
		if (sign && lastSign && sign != lastSign) turns.push_back(sim->currentTick);
		if (sign) lastSign = sign;
		while (!turns.empty() && turns.front() < sim->currentTick - 40) turns.pop_front();
		if (turns.size() >= 4 && sim->currentTick >= cooldown)
		{
			turns.clear(), cooldown = sim->currentTick + 60;
			static const uint32_t colours[] = { 0xFFFF4060, 0xFFFFC020, 0xFF40C0FF, 0xFF60E060, 0xFFC060FF, 0xFFFFFFFF };
			for (int k = 0; k < 40; k++)
			{
				int x = m.x + sim->rng.between(-6, 6), y = m.y + sim->rng.between(-6, 6);
				if (x < CELL || y < CELL || x >= XRES - CELL || y >= YRES - CELL || sim->pmap[y][x] || sim->PixelWall(x, y)) continue;
				if (int i = sim->create_part(-1, x, y, PT_SAWD); i >= 0)
				{
					auto &p = sim->parts[i];
					p.dcolour = colours[sim->rng.between(0, 5)];
					p.vx = (sim->rng.uniform01() - 0.5f) * 4, p.vy = -1 - sim->rng.uniform01() * 3;
					// ponytail: the queue goes in order, so behind long-lived leaves confetti waits for them
					fallen.push_back({ i, PT_SAWD, sim->currentTick + sim->rng.between(300, 500) });
				}
			}
		}
	}
	lastX = sim->petPointer.x;
	sim->petFetchRange = fun.fetch, sim->petKick = fun.kick, sim->birdRest = int(fun.birdRest * 60), sim->petHoles = fun.petHoles;
	// Rainbow: rain that fell half a minute or more, then stopped for two seconds
	if (rainLast == sim->currentTick)
	{
		if (rainSince < 0) rainSince = sim->currentTick;
	}
	else if (rainSince >= 0 && sim->currentTick - rainLast > 120)
	{
		if (rainLast - rainSince >= 1800 && fun.rainbow >= 1) rainbowUntil = GetTickCount64() + ULONGLONG(fun.rainbow * 1000), rainbowTwice = sim->rng.uniform01() < fun.rainbowDouble;
		rainSince = -1;
	}
	// Snowmen (ice, snow-white) on snow at least snowDepth deep, checked every 5 s
	static std::vector<std::pair<int, int>> snowmen; // bottom middle
	std::erase_if(snowmen, [sim](auto &s) { int r = sim->pmap[s.second - 5][s.first]; return !r || TYP(r) != PT_ICEI; });
	if (fun.snowmen >= 1 && sim->currentTick % 300 == 0 && int(snowmen.size()) < int(fun.snowmen))
		for (int tries = 0; tries < 20; tries++)
		{
			auto play = sim->PlayRect();
			int x = sim->rng.between(std::max(CELL, play.pos.X) + 8, std::min(XRES - CELL, play.pos.X + play.size.X) - 9);
			int y = std::max(CELL, play.pos.Y);
			while (y < YRES - CELL && !sim->pmap[y][x] && !sim->PixelWall(x, y)) y++;
			int depth = 0;
			while (y + depth < YRES && sim->pmap[y + depth][x] && TYP(sim->pmap[y + depth][x]) == PT_SNOW) depth++;
			if (depth < int(fun.snowDepth) || y < CELL + 26) continue;
			bool room = true;
			for (int yy = y - 24; yy < y && room; yy++)
				for (int xx = x - 6; xx <= x + 6 && room; xx++) room = !sim->pmap[yy][xx] && !sim->PixelWall(xx, yy);
			if (!room) continue;
			auto put = [&](int px, int py, int type, uint32_t colour) {
				if (sim->pmap[py][px]) return;
				if (int i = sim->create_part(-1, px, py, type); i >= 0) sim->parts[i].temp = 263.15f, sim->parts[i].dcolour = colour;
			};
			// the face and buttons first (the balls fill round them): coal eyes, a carrot nose (wood), coal buttons
			put(x - 1, y - 20, PT_COAL, 0xFF202020), put(x + 1, y - 20, PT_COAL, 0xFF202020);
			for (int k = 1; k <= 3; k++) put(x + k, y - 19, PT_WOOD, 0xFFFF7A1A);
			put(x, y - 12, PT_COAL, 0xFF202020), put(x, y - 14, PT_COAL, 0xFF202020);
			for (auto [r, my] : { std::pair{ 5, y - 5 }, { 4, y - 13 }, { 3, y - 19 } })
				for (int dy = -r; dy <= r; dy++)
					for (int dx = -r; dx <= r; dx++)
						if (dx * dx + dy * dy <= r * r + r) put(x + dx, my + dy, PT_ICEI, 0xFFF4F8FF);
			snowmen.push_back({ x, y });
			break;
		}
	// Paper planes: thrown now and then from a pet (else an edge), glide down slowly, land, and fade after 10 s
	static ULONGLONG nextPlane = 0;
	if (fun.planes < 1) nextPlane = 0;
	else if (!nextPlane) nextPlane = GetTickCount64() + ULONGLONG(fun.planes * 60000);
	else if (GetTickCount64() >= nextPlane && paperPlanes.size() < 8)
	{
		nextPlane = GetTickCount64() + ULONGLONG(fun.planes * 60000);
		bool right = sim->rng.chance(1, 2);
		PaperPlane pl{ right ? float(CELL + 2) : float(XRES - CELL - 3), float(YRES / 4), 0, -0.5f, -1 };
		for (int f = 0; f < MAX_FIGHTERS; f++)
			if (sim->fighters[f].spwn)
			{
				pl.x = (sim->fighters[f].legs[4] + sim->fighters[f].legs[12]) / 2, pl.y = sim->fighters[f].legs[5] - 18;
				right = pl.x < XRES / 2;
				break;
			}
		pl.vx = (right ? 1.5f : -1.5f) * fun.planeSpeed;
		paperPlanes.push_back(pl);
	}
	for (auto &pl : paperPlanes)
	{
		if (pl.landed >= 0)
		{
			pl.landed++;
			continue;
		}
		float sink = 0.25f * fun.planeSpeed;
		pl.vy += (sink - pl.vy) * 0.02f;
		if (std::abs(pl.vx) > 0.6f * fun.planeSpeed) pl.vx *= 0.999f;
		int nx = int(pl.x + pl.vx), ny = int(pl.y + pl.vy);
		if (nx < CELL || ny < CELL || nx >= XRES - CELL || ny >= YRES - CELL)
		{
			pl.landed = 600; // off the edge: gone
			continue;
		}
		if (sim->pmap[ny + 1][nx] || sim->PixelWall(nx, ny + 1) || sim->pmap[ny][nx] || sim->PixelWall(nx, ny)) pl.landed = 0;
		else pl.x += pl.vx, pl.y += pl.vy;
	}
	std::erase_if(paperPlanes, [](auto &pl) { return pl.landed >= 600; });
	funHour = hour;
	// Birds land and rest: a morning bird with something below it now and then glides down onto it (BIRD lands it)
	if (fun.perch)
		for (auto &[i, until] : visitors)
			if (auto &p = sim->parts[i]; until >= 0 && p.tmp2 == 0 && sim->rng.chance(1, 900))
			{
				int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
				for (int yy = y + 1; yy < std::min(YRES - CELL, y + 80); yy++)
					if (sim->pmap[yy][x] || sim->PixelWall(x, yy))
					{
						p.tmp2 = -1;
						break;
					}
			}
	// Mouse wind: a fast pointer blows the loose stuff near it along (not while a button is held: drawing or grabbing)
	static int windX = -1, windY = -1;
	if (auto &m = sim->petPointer; fun.wind > 0 && m.x >= 0 && windX >= 0 && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
	{
		float dx = float(m.x - windX), dy = float(m.y - windY);
		if (dx * dx + dy * dy > 9)
		{
			auto &els = SimulationData::CRef().elements;
			for (int y = std::max(0, m.y - 24); y < std::min(YRES, m.y + 25); y++)
				for (int x = std::max(0, m.x - 24); x < std::min(XRES, m.x + 25); x++)
					if (int r = sim->pmap[y][x]; r && (els[TYP(r)].Properties & (TYPE_PART | TYPE_LIQUID | TYPE_GAS)) && (x - m.x) * (x - m.x) + (y - m.y) * (y - m.y) < 576)
					{
						auto &p = sim->parts[ID(r)];
						p.vx = std::clamp(p.vx + dx * 0.12f * fun.wind, -10.f, 10.f);
						p.vy = std::clamp(p.vy + dy * 0.12f * fun.wind, -10.f, 10.f);
					}
		}
	}
	windX = sim->petPointer.x, windY = sim->petPointer.y;
	// Sandcastles: now and then a pet beside a sand pile builds a little castle (sand-coloured brick) from its sand
	static int castles = 0, castleCheck = 0;
	if (fun.castles && ++castleCheck >= 600)
	{
		castleCheck = 0;
		for (int f = 0; f < MAX_FIGHTERS && castles < 3; f++)
		{
			auto &fg = sim->fighters[f];
			if (!fg.spwn) continue;
			int fx = int(fg.legs[4] + fg.legs[12]) / 2, fy = int(fg.legs[5]);
			std::vector<int> sand;
			for (int y = std::max(0, fy - 24); y < std::min(YRES, fy + 6); y++)
				for (int x = std::max(0, fx - 30); x < std::min(XRES, fx + 31); x++)
					if (int r = sim->pmap[y][x]; r && TYP(r) == PT_SAND) sand.push_back(ID(r));
			// the castle: 11 wide, 8 tall; a door in the base, two towers with battlements
			static const char *shape[] = { // top row first
				"#.#.....#.#",
				"###.....###",
				"###.#.#.###",
				"###########",
				"###########",
				"###########",
				"####...####",
				"####...####",
			};
			int cw = 11, ch = int(std::size(shape)), need = 0;
			for (auto row : shape) for (int k = 0; k < cw; k++) need += row[k] == '#';
			if (int(sand.size()) < need + 30) continue;
			int cx = fx + (sim->rng.chance(1, 2) ? 18 : -18) - cw / 2, base = -1;
			for (int y = std::max(CELL, fy - 30); y < std::min(YRES - CELL, fy + 10) && base < 0; y++)
				if (sim->pmap[y][cx + cw / 2] || sim->PixelWall(cx + cw / 2, y)) base = y - 1; // just above the ground
			if (base < 0 || cx < CELL || cx + cw >= XRES - CELL || base - ch < CELL) continue;
			bool room = true;
			for (int r = 0; r < ch && room; r++)
				for (int k = 0; k < cw && room; k++) room = !sim->pmap[base - ch + 1 + r][cx + k] && !sim->PixelWall(cx + k, base - ch + 1 + r);
			if (!room) continue;
			for (int k = 0; k < need; k++) sim->kill_part(sand[k]); // the pile pays for it
			for (int r = 0; r < ch; r++)
				for (int k = 0; k < cw; k++)
					if (shape[r][k] == '#')
						if (int i = sim->create_part(-1, cx + k, base - ch + 1 + r, PT_BRCK); i >= 0)
						{
							sim->parts[i].dcolour = 0xFFD9B86A;
							fallen.push_back({ i, PT_BRCK, sim->currentTick + 18000 });
						}
			castles++;
		}
	}
	if (castles && sim->currentTick % 18000 == 0) castles = 0; // ponytail: a rough count; they crumble after 5 min anyway
}

static void TickWindowWalls()
{
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	// once per sim tick: frames drawn between ticks (the screen's rate is higher) have no particles to move. Every
	// other frame while paused, so windows still shove.
	static int lastTick = -1, skipped = 0;
	if (sim->currentTick == lastTick && ++skipped < 2) return;
	lastTick = sim->currentTick, skipped = 0;
	// wallRects: where each window was on the last tick
	std::vector<OverlayWall> wins;
	RECT sr;
	GetWindowRect(mainWindow, &sr);
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	static RECT grabRect;
	grabRect = sr;
#ifdef __APPLE__
	overlayGrabScreen = [](int x0, int y0, int w, int h, uint32_t *out) {
		double px = MacScale(), sw = (grabRect.right - grabRect.left) / px, sh = (grabRect.bottom - grabRect.top) / px;
		return sw > 0 && sh > 0 && OverlayMac::Grab(grabRect.left / px + x0 * sw / WINDOWW, grabRect.top / px + y0 * sh / WINDOWH, w * sw / WINDOWW, h * sh / WINDOWH, w, h, out);
	};
#else
	// shards take the colour under them: GDI's copy of the screen leaves out layered windows, so not the overlay
	overlayGrabScreen = [](int x0, int y0, int w, int h, uint32_t *out) {
		int sw = grabRect.right - grabRect.left, sh = grabRect.bottom - grabRect.top;
		if (sw <= 0 || sh <= 0) return false;
		HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
		BITMAPINFO bi{};
		bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
		void *bits = nullptr;
		HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		bool ok = false;
		if (bmp)
		{
			auto old = SelectObject(dc, bmp);
			SetStretchBltMode(dc, COLORONCOLOR);
			ok = StretchBlt(dc, 0, 0, w, h, screen, grabRect.left + x0 * sw / WINDOWW, grabRect.top + y0 * sh / WINDOWH,
			                w * sw / WINDOWW, h * sh / WINDOWH, SRCCOPY);
			if (ok)
				for (int i = 0; i < w * h; i++) out[i] = ((uint32_t *)bits)[i] & 0xFFFFFF;
			SelectObject(dc, old);
			DeleteObject(bmp);
		}
		DeleteDC(dc);
		ReleaseDC(nullptr, screen);
		return ok;
	};
#endif
	// windows are walls only when the sim is over them; icons whenever it's over the icons
	// the mouse over the desktop (where the overlay lets clicks through) stirs the air
	static POINT lastCursor{ -1, -1 };
	POINT cursor;
	if (!overlayWindowed && sw > 0 && sh > 0 && GetCursorPos(&cursor))
	{
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		int x = px(cursor.x - sr.left, WINDOWW, sw), y = px(cursor.y - sr.top, WINDOWH, sh);
		sim->petPointer = { x, y, lastCursor.x >= 0 ? float(std::hypot(px(cursor.x - lastCursor.x, WINDOWW, sw), px(cursor.y - lastCursor.y, WINDOWH, sh))) : 0.f };
		if (lastCursor.x >= 0 && x >= 0 && y >= 0 && x < XRES && y < YRES && GetAncestor(WindowFromPoint(cursor), GA_ROOT) != mainWindow)
		{
			OverlayCursorWind(sim, x, y, px(cursor.x - lastCursor.x, WINDOWW, sw), px(cursor.y - lastCursor.y, WINDOWH, sh));
		}
		lastCursor = cursor;
	}
	TickFun(sim);
	bool doWindows = windowWalls && OverlayGetLayer() == OverlayLayer::OnTop;
	overlayWallpaperSeen = doWindows || OverlayGetLayer() != OverlayLayer::OnTop;
	bool doIcons = iconWalls && OverlayGetLayer() != OverlayLayer::BehindIcons;
	// Mouse is a broom: a small solid block at the pointer where clicks go to the desktop, moving like a window
	POINT at;
	bool broom = fun.broom && !overlayWindowed && GetCursorPos(&at) && GetAncestor(WindowFromPoint(at), GA_ROOT) != mainWindow;
#ifdef __APPLE__
	// It covers one screen, so no gaps. The Dock is a solid shape, added with the windows below; only when it can't be read
	// (no Accessibility) is there a floor where it starts, or everything would drain out of the void edge.
	static std::vector<RECT> gaps;
	gaps.clear();
	double dockL, dockT, dockR, dockB;
	bool dockKnown = OverlayMac::DockFrame(dockL, dockT, dockR, dockB);
	if (windowWalls && !dockKnown)
	{
		RECT floor;
		GetWindowRect(macDock, &floor);
		gaps.push_back({ sr.left, floor.top, sr.right, sr.bottom });
	}
#else
	// The overlay spans every monitor; where none is (beside a shorter one) is solid, so sand stops at its edge
	static std::vector<RECT> gaps = [] {
		std::vector<RECT> mons, out;
		EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT r, LPARAM lp) -> BOOL {
			((std::vector<RECT> *)lp)->push_back(*r);
			return TRUE;
		}, LPARAM(&mons));
		RECT v{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN) };
		v.right = v.left + GetSystemMetrics(SM_CXVIRTUALSCREEN), v.bottom = v.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
		std::vector<long> ys{ v.top, v.bottom };
		for (auto &m : mons) ys.push_back(m.top), ys.push_back(m.bottom);
		std::sort(ys.begin(), ys.end());
		for (size_t i = 0; i + 1 < ys.size(); i++)
		{
			if (ys[i] == ys[i + 1]) continue;
			std::vector<std::pair<long, long>> spans; // monitors across this band
			for (auto &m : mons)
				if (m.top <= ys[i] && m.bottom >= ys[i + 1]) spans.push_back({ m.left, m.right });
			std::sort(spans.begin(), spans.end());
			long x = v.left;
			for (auto [l, r] : spans)
			{
				if (l > x) out.push_back({ x, ys[i], l, ys[i + 1] });
				x = std::max(x, r);
			}
			if (x < v.right) out.push_back({ x, ys[i], v.right, ys[i + 1] });
		}
		return out;
	}();
#endif
	if (overlayWindowed || (!doWindows && !doIcons && gaps.empty() && !broom) || sw <= 0 || sh <= 0)
	{
		wallRects.clear();
		std::vector<OverlayWall> him;
		OverlayBuddy::Walls(him); // he is still solid
		if (him.empty()) sim->pixelWalls.clear();
		else OverlayApplyWalls(sim, him);
		return;
	}
	std::map<HWND, RECT> now;
#ifdef __APPLE__
	static std::map<HWND, int> pids; // whose window it is
	if (doWindows)
		for (auto &o : OverlayMac::OtherWindows())
		{
			HWND hwnd = HWND(uintptr_t(o.id));
			RECT r = MacRect(o);
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); }; // screen -> sim pixels
			OverlayWall w{ std::max(0, px(r.left - sr.left, WINDOWW, sw)), std::max(0, px(r.top - sr.top, WINDOWH, sh)),
			               std::min(XRES, px(r.right - sr.left, WINDOWW, sw)), std::min(YRES, px(r.bottom - sr.top, WINDOWH, sh)), 0, 0, {} };
			now[hwnd] = r;
			pids[hwnd] = o.pid;
			if (w.x1 <= w.x0 || w.y1 <= w.y0) continue;
			if (auto it = wallRects.find(hwnd); it != wallRects.end())
			{
				w.mx = px(r.left - it->second.left, WINDOWW, sw);
				w.my = px(r.top - it->second.top, WINDOWH, sh);
			}
			w.id = uintptr_t(hwnd);
			wins.push_back(w);
		}
#else
	struct Ctx { std::vector<OverlayWall> *wins; std::map<HWND, RECT> *now; RECT sr; int sw, sh; } ctx{ &wins, &now, sr, sw, sh };
	if (doWindows) EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
		auto &c = *(Ctx *)lp;
		if (hwnd == mainWindow || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE; // most windows: cheap checks first
		auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		BOOL cloaked = FALSE;
		DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
		wchar_t cls[32];
		GetClassNameW(hwnd, cls, 32);
		if (cloaked || (ex & WS_EX_TRANSPARENT) ||
		    ((ex & WS_EX_TOOLWINDOW) && GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId() &&
		     wcscmp(cls, L"Shell_TrayWnd") && wcscmp(cls, L"Shell_SecondaryTrayWnd")) || // the taskbar is a floor
		    !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW"))
		{
			return TRUE;
		}
		RECT r; // the visible frame, without the invisible resize borders
		if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) != S_OK) GetWindowRect(hwnd, &r);
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); }; // screen -> sim pixels
		OverlayWall w{ std::max(0, px(r.left - c.sr.left, WINDOWW, c.sw)), std::max(0, px(r.top - c.sr.top, WINDOWH, c.sh)),
		               std::min(XRES, px(r.right - c.sr.left, WINDOWW, c.sw)), std::min(YRES, px(r.bottom - c.sr.top, WINDOWH, c.sh)), 0, 0, {} };
		(*c.now)[hwnd] = r;
		if (w.x1 <= w.x0 || w.y1 <= w.y0) return TRUE;
		if (auto it = wallRects.find(hwnd); it != wallRects.end())
		{
			w.mx = px(r.left - it->second.left, WINDOWW, c.sw);
			w.my = px(r.top - it->second.top, WINDOWH, c.sh);
		}
		w.id = uintptr_t(hwnd);
		c.wins->push_back(w);
		return TRUE;
	}, LPARAM(&ctx));
#endif
	if (!wallRects.empty() && (fun.closeToss || fun.openShake))
	{
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		auto toss = [&](const RECT &r, bool close) {
			TossAbove(sim, px(r.left - sr.left, WINDOWW, sw), px(r.top - sr.top, WINDOWH, sh), px(r.right - sr.left, WINDOWW, sw), close);
		};
		for (auto &[hwnd, r] : wallRects)
			if (fun.closeToss && !now.count(hwnd)) toss(r, true);
		for (auto &[hwnd, r] : now)
			if (fun.openShake && !wallRects.count(hwnd)) toss(r, false);
	}
	// Window lift: a window dragged up fast throws what sits on it
	if (fun.lift > 0)
	{
		auto &els = SimulationData::CRef().elements;
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		for (auto &[hwnd, r] : now)
			if (auto it = wallRects.find(hwnd); it != wallRects.end())
				if (int dy = px(r.top - it->second.top, WINDOWH, sh); dy <= -3)
				{
					int x0 = px(r.left - sr.left, WINDOWW, sw), x1 = px(r.right - sr.left, WINDOWW, sw), y0 = px(r.top - sr.top, WINDOWH, sh);
					for (int y = std::max(0, y0 - 80); y < std::min(YRES, y0); y++)
						for (int x = std::max(0, x0); x < std::min(XRES, x1); x++)
							if (int q = sim->pmap[y][x]; q && (els[TYP(q)].Properties & (TYPE_PART | TYPE_LIQUID)))
							{
								auto &p = sim->parts[ID(q)];
								p.vy = std::min(p.vy, dy * fun.lift * 0.5f);
								p.vx += (sim->rng.uniform01() - 0.5f) * 2 * fun.lift;
							}
				}
	}
	wallRects = std::move(now);
	// Busy apps run hot: what sits on a window's top warms toward 20 C + 100 C per core its app keeps busy (at 100%)
	static std::map<HWND, float> busy; // cores in use, sampled every half second
	if (fun.hot <= 0) busy.clear();
	else
	{
		if (sim->currentTick % 30 == 0)
		{
#ifdef __APPLE__
			static std::map<int, std::pair<double, double>> was; // pid: CPU time, when (seconds)
			std::map<int, std::pair<double, double>> is;
			double when = OverlayMac::Now();
			busy.clear();
			for (auto &[hwnd, r] : wallRects)
			{
				int pid = pids[hwnd];
				if (!is.count(pid))
					if (double cpu = OverlayMac::ProcessCpuSeconds(pid); cpu >= 0) is[pid] = { cpu, when };
				auto a = is.find(pid), b = was.find(pid);
				if (a != is.end() && b != was.end() && a->second.second > b->second.second)
					busy[hwnd] = float((a->second.first - b->second.first) / (a->second.second - b->second.second));
			}
			was = std::move(is);
#else
			static std::map<DWORD, std::pair<ULONGLONG, ULONGLONG>> was; // pid: CPU time, when (100 ns)
			std::map<DWORD, std::pair<ULONGLONG, ULONGLONG>> is;
			auto u64 = [](FILETIME f) { return ULARGE_INTEGER{ { f.dwLowDateTime, f.dwHighDateTime } }.QuadPart; };
			FILETIME ft;
			GetSystemTimeAsFileTime(&ft);
			busy.clear();
			for (auto &[hwnd, r] : wallRects)
			{
				DWORD pid = 0;
				GetWindowThreadProcessId(hwnd, &pid);
				if (!is.count(pid))
					if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
					{
						FILETIME c, e, k, u;
						if (GetProcessTimes(h, &c, &e, &k, &u)) is[pid] = { u64(k) + u64(u), u64(ft) };
						CloseHandle(h);
					}
				auto a = is.find(pid), b = was.find(pid);
				if (a != is.end() && b != was.end() && a->second.second > b->second.second)
					busy[hwnd] = float(a->second.first - b->second.first) / float(a->second.second - b->second.second);
			}
			was = std::move(is);
#endif
		}
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		for (auto &[hwnd, cores] : busy)
		{
			auto it = wallRects.find(hwnd);
			if (cores < 0.05f || it == wallRects.end()) continue;
			auto &r = it->second;
			float to = std::min(20 + 100 * cores * fun.hot, 600.f) + 273.15f;
			int x0 = std::max(0, px(r.left - sr.left, WINDOWW, sw)), x1 = std::min(XRES, px(r.right - sr.left, WINDOWW, sw));
			int y0 = px(r.top - sr.top, WINDOWH, sh);
			for (int y = std::max(0, y0 - 3); y < std::min(YRES, y0); y++)
				for (int x = x0; x < x1; x++)
					if (int q = sim->pmap[y][x]; q && sim->parts[ID(q)].temp < to) sim->parts[ID(q)].temp += (to - sim->parts[ID(q)].temp) * 0.05f;
		}
	}
	// The hourglass: glass on the taskbar near its right end; sand drops through the (plugged) neck one grain at a time
	// so the top holds the time left; it refills when it runs out. Rebuilt where anything broke it or the sim was cleared
	if (fun.hourglass >= 1)
	{
		static float minutes = 0;
		static ULONGLONG start = 0;
		if (minutes != fun.hourglass) minutes = fun.hourglass, start = GetTickCount64();
		RECT tr;
		HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
		if (tray && GetWindowRect(tray, &tr))
		{
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			int cx = px(tr.right - sr.left, WINDOWW, sw) - 40, by = px(tr.top - sr.top, WINDOWH, sh) - 1, ty = by - 24;
			auto half = [](int r) { return r < 12 ? 7 - r * 6 / 11 : 7 - (24 - r) * 6 / 11; }; // inside half-width, row 0-24
			if (cx > 10 && ty > 10 && by < YRES)
			{
				for (int r = -1; r <= 25; r++) // the glass: both sides of every row, the ends, and the plug in the neck
					for (int x = cx - 9; x <= cx + 9; x++)
					{
						int y = ty + r;
						bool glass = r == -1 || r == 25 || (r == 12 && std::abs(x - cx) <= 1) || (r >= 0 && r <= 24 && std::abs(x - cx) == half(r) + 1);
						if (glass && !sim->pmap[y][x] && !sim->PixelWall(x, y)) sim->create_part(-1, x, y, PT_GLAS);
					}
				// top bulb rows 0-11, bottom 13-24
				int top = 0, bottom = 0, total = 0;
				for (int r = 0; r < 12; r++) total += 2 * half(r) + 1;
				total = total * 3 / 4;
				for (int r = 0; r <= 24; r++)
					for (int x = cx - half(r); x <= cx + half(r); x++)
						if (int q = sim->pmap[ty + r][x]; q && TYP(q) == PT_SAND) (r < 12 ? top : bottom)++;
				double run = (GetTickCount64() - start) / 60000.0 / minutes;
				if (run >= 1) start = GetTickCount64(), run = 0; // ran out: turn it over
				int want = int(std::ceil(total * (1 - run)));
				auto add = [&](int r0, int r1) { // a grain in the lowest free spot of rows r0-r1
					for (int r = r1; r >= r0; r--)
						for (int x = cx - half(r); x <= cx + half(r); x++)
							if (!sim->pmap[ty + r][x]) return sim->create_part(-1, x, ty + r, PT_SAND) >= 0;
					return false;
				};
				auto take = [&](int r0, int r1) { // the lowest grain of rows r0-r1
					for (int r = r1; r >= r0; r--)
						for (int x = cx - half(r); x <= cx + half(r); x++)
							if (int q = sim->pmap[ty + r][x]; q && TYP(q) == PT_SAND) return sim->kill_part(ID(q)), true;
					return false;
				};
				for (int k = 0; k < 3 && top > want; k++, top--, bottom++) // through the neck, falling from just under it
				{
					take(0, 11);
					if (sim->pmap[ty + 13][cx] || sim->create_part(-1, cx, ty + 13, PT_SAND) < 0) add(13, 24);
				}
				for (int k = 0; k < 20 && top < want; k++, top++) // turned over (or cleared): back up top
				{
					if (bottom > 0 && take(13, 24)) bottom--;
					add(0, 11);
				}
				for (int k = 0; k < 20 && top + bottom < total; k++, bottom++) add(13, 24);
			}
		}
	}
	// Notifications: a window that flashed its taskbar button gets a ball dropped on it (one per window every 3 s)
	static std::map<HWND, ULONGLONG> dropped;
	for (HWND hwnd : fun.flashed)
	{
		auto it = wallRects.find(hwnd);
		if (it == wallRects.end() || GetTickCount64() - dropped[hwnd] < 3000) continue;
		dropped[hwnd] = GetTickCount64();
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		auto &r = it->second;
		int cx = px((r.left + r.right) / 2 - sr.left, WINDOWW, sw), cy = std::max(12, px(r.top - sr.top, WINDOWH, sh) - 20);
		for (int y = -3; y <= 3; y++)
			for (int x = -3; x <= 3; x++)
				if (x * x + y * y <= 10) sim->create_part(-1, cx + x, cy + y, PT_BALL);
	}
	fun.flashed.clear();
	// Taskbar toys: Ctrl-drag an app's taskbar button off the taskbar and a block in its icon's colour drops from the
	// pointer, thrown the way it moved; an app that opens pops one up out of its button. Blocks are stone in that colour.
	if (fun.taskThrow || fun.taskPop)
	{
		StartTaskButtons();
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		auto block = [&](const RECT &b, int cx, int cy, float vx, float vy) {
			uint32_t colour = 0x808080; // the most vivid pixel of the button: its icon
			int x0 = std::max(0, px(b.left - sr.left, WINDOWW, sw)), y0 = std::max(0, px(b.top - sr.top, WINDOWH, sh));
			int x1 = std::min(XRES, px(b.right - sr.left, WINDOWW, sw)), y1 = std::min(YRES, px(b.bottom - sr.top, WINDOWH, sh));
			std::vector<uint32_t> pix(size_t(std::max(0, x1 - x0)) * std::max(0, y1 - y0));
			if (pix.size() && overlayGrabScreen && overlayGrabScreen(x0, y0, x1 - x0, y1 - y0, pix.data()))
				for (int best = 40; auto c : pix)
				{
					int r = c >> 16 & 0xFF, g = c >> 8 & 0xFF, bl = c & 0xFF, s = std::max({ r, g, bl }) - std::min({ r, g, bl });
					if (s > best) best = s, colour = c & 0xFFFFFF;
				}
			for (int y = -5; y <= 5; y++)
				for (int x = -5; x <= 5; x++)
					if (int i = sim->create_part(-1, cx + x, cy + y, PT_STNE); i >= 0)
					{
						auto &part = sim->parts[i];
						part.dcolour = 0xFF000000 | colour;
						part.vx = vx, part.vy = vy;
					}
		};
		std::vector<TaskButton> buttons;
		std::vector<RECT> popped;
		{
			std::lock_guard lock(taskMutex);
			buttons = taskButtons;
			popped.swap(taskPopped);
		}
		for (auto &b : popped)
			if (fun.taskPop) block(b, px((b.left + b.right) / 2 - sr.left, WINDOWW, sw), px(b.top - sr.top, WINDOWH, sh) - 8, 0, -4);
		static bool armed = false, thrown = false;
		static RECT from;
		static POINT last;
		POINT c;
		GetCursorPos(&c);
		if (GetAsyncKeyState(VK_LBUTTON) >= 0) armed = thrown = false;
		else if (fun.taskThrow && !armed && !thrown && GetAsyncKeyState(VK_CONTROL) < 0)
			for (auto &b : buttons)
				if (PtInRect(&b.r, c)) armed = true, from = b.r;
		if (armed && (c.y < from.top - 8 || c.y > from.bottom + 8)) // off the taskbar
		{
			block(from, px(c.x - sr.left, WINDOWW, sw), px(c.y - sr.top, WINDOWH, sh),
			      std::clamp(float(px(c.x - last.x, WINDOWW, sw)), -15.f, 15.f), std::clamp(float(px(c.y - last.y, WINDOWH, sh)), -15.f, 15.f));
			armed = false, thrown = true;
		}
		last = c;
	}
	// Window gravity: gravity leans toward the side the active window is on (half a sideways pull at the screen's edge),
	// swinging over slowly. It takes over plain down gravity only, as custom gravity, and gives it back when switched off.
	// ponytail: a scene saved while leaning keeps custom gravity; reset Gravity by hand if that matters.
	static bool tilting = false;
	if (fun.tilt && (sim->gravityMode == GRAV_VERTICAL || (tilting && sim->gravityMode == GRAV_CUSTOM)))
	{
		float lean = 0;
		if (auto it = wallRects.find(GetForegroundWindow()); it != wallRects.end())
			lean = std::clamp(((it->second.left + it->second.right) / 2 - sr.left) / float(sw) * 2 - 1, -1.f, 1.f) * 0.5f;
		if (!tilting) sim->customGravityX = 0;
		sim->gravityMode = GRAV_CUSTOM, tilting = true;
		sim->customGravityX += (lean - sim->customGravityX) * 0.02f;
		sim->customGravityY = 1;
	}
	else if (tilting)
	{
		if (sim->gravityMode == GRAV_CUSTOM) sim->gravityMode = GRAV_VERTICAL;
		tilting = false;
	}
	// Typing sparks: keys pressed (new input with the pointer still and no button down) now and then puff cold sparks
	// up from the text cursor, or from the top of the window being typed in where the app has no standard one
	if (fun.typing)
	{
#ifdef __APPLE__
		static double lastKey = 0; // when the last key went down
		double keyAt = OverlayMac::Now() - OverlayMac::IdleSeconds(true);
		bool typed = keyAt > lastKey + 0.005;
		lastKey = keyAt;
		POINT at{ -1, -1 };
		if (typed && sim->rng.uniform01() < 0.4f)
		{
			double cx, cy;
			// from the window's top: a caret is inside the window, and sparks can't start inside a wall
			if (auto it = wallRects.find(GetForegroundWindow()); it != wallRects.end())
				at = { OverlayMac::Caret(cx, cy) ? std::clamp(long(cx * MacScale()), it->second.left, it->second.right) : it->second.left + long(sim->rng.uniform01() * (it->second.right - it->second.left)), it->second.top };
			else if (OverlayMac::Caret(cx, cy))
				at = { long(cx * MacScale()), long(cy * MacScale()) };
		}
#else
		static DWORD lastInput = 0;
		static POINT lastPoint;
		LASTINPUTINFO li{ sizeof(li) };
		POINT pt;
		GetCursorPos(&pt);
		bool typed = GetLastInputInfo(&li) && li.dwTime != lastInput && pt.x == lastPoint.x && pt.y == lastPoint.y &&
		             GetAsyncKeyState(VK_LBUTTON) >= 0 && GetAsyncKeyState(VK_RBUTTON) >= 0;
		lastInput = li.dwTime, lastPoint = pt;
		GUITHREADINFO gi{ sizeof(gi) };
		POINT at{ -1, -1 };
		if (typed && sim->rng.uniform01() < 0.4f)
		{
			if (GetGUIThreadInfo(0, &gi) && gi.hwndCaret)
				at = { gi.rcCaret.left, gi.rcCaret.top }, ClientToScreen(gi.hwndCaret, &at);
			else if (auto it = wallRects.find(GetForegroundWindow()); it != wallRects.end())
				at = { it->second.left + long(sim->rng.uniform01() * (it->second.right - it->second.left)), it->second.top };
		}
#endif
		if (at.x >= 0)
		{
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			int x = px(at.x - sr.left, WINDOWW, sw), y = px(at.y - sr.top, WINDOWH, sh) - 3;
			for (int k = 0; k < 4; k++) ColdSpark(sim, x + sim->rng.between(-3, 3), y + sim->rng.between(-2, 0));
		}
	}
	// The volcano: a stone cone on the taskbar left of the hourglass; it smokes while the PC's total CPU use is past the
	// slider, and throws lava while it is 20% past it. Rebuilt where broken, like the hourglass
	static float cpu = 0;
#ifdef __APPLE__
	if (fun.volcano > 0 && sim->currentTick % 30 == 0) cpu = OverlayMac::CpuLoad();
#else
	static ULONGLONG lastIdle = 0, lastTotal = 0;
	if (fun.volcano > 0 && sim->currentTick % 30 == 0)
	{
		FILETIME fi, fk, fu;
		if (GetSystemTimes(&fi, &fk, &fu))
		{
			auto u64 = [](FILETIME t) { return (ULONGLONG(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
			ULONGLONG idleT = u64(fi), total = u64(fk) + u64(fu); // kernel time includes the idle time
			if (lastTotal && total > lastTotal) cpu = 1 - float(idleT - lastIdle) / float(total - lastTotal);
			lastIdle = idleT, lastTotal = total;
		}
	}
#endif
	if (fun.volcano > 0)
	{
		RECT tr;
		HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
		if (tray && GetWindowRect(tray, &tr))
		{
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			int cx = px(tr.right - sr.left, WINDOWW, sw) - 100, by = px(tr.top - sr.top, WINDOWH, sh) - 1;
			if (cx > 20 && by > 30 && by < YRES)
			{
				for (int r = 0; r < 14; r++) // rows up from the base; the top two leave a crater
					for (int x = cx - 15 + r; x <= cx + 15 - r; x++)
						if (int y = by - r; !(r >= 12 && std::abs(x - cx) <= 1) && !sim->pmap[y][x] && !sim->PixelWall(x, y))
							if (int i = sim->create_part(-1, x, y, PT_STNE); i >= 0) sim->parts[i].dcolour = 0xFF6B4A35;
				int top = by - 13;
				if (cpu >= fun.volcano && sim->rng.chance(1, 6) && !sim->pmap[top - 2][cx])
					if (int i = sim->create_part(-1, cx, top - 2, PT_SMKE); i >= 0) sim->parts[i].vy = -1;
				// thrown lava (or the stone it cools into) goes after 8 s, so the taskbar doesn't fill up
				static std::deque<std::pair<int, ULONGLONG>> thrown;
				while (!thrown.empty() && GetTickCount64() >= thrown.front().second)
				{
					int i = thrown.front().first, ty = sim->parts[i].type;
					thrown.pop_front();
					if (ty == PT_LAVA || ty == PT_STNE) sim->kill_part(i);
				}
				if (cpu >= std::min(1.f, fun.volcano + 0.2f) && sim->rng.chance(1, 3) && !sim->pmap[top - 3][cx])
					if (int i = sim->create_part(-1, cx, top - 3, PT_LAVA); i >= 0)
					{
						thrown.push_back({ i, GetTickCount64() + 8000 });
						sim->parts[i].vy = -(2 + 2 * sim->rng.uniform01()) * fun.volcanoForce;
						sim->parts[i].vx = (sim->rng.uniform01() - 0.5f) * 2 * fun.volcanoForce;
					}
			}
		}
	}
	static POINT broomAt{ -1, -1 };
	if (broom)
	{
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		int cx = px(at.x - sr.left, WINDOWW, sw), cy = px(at.y - sr.top, WINDOWH, sh), b = 5;
		int ox = broomAt.x >= 0 ? broomAt.x : cx, oy = broomAt.x >= 0 ? broomAt.y : cy;
		// the whole path since the last tick is solid (a fast flick would otherwise jump over the sand in between), so
		// what it sweeps over is pushed out ahead of it
		OverlayWall w{ std::max(0, std::min(ox, cx) - b), std::max(0, std::min(oy, cy) - b), std::min(XRES, std::max(ox, cx) + b + 1),
		               std::min(YRES, std::max(oy, cy) + b + 1), cx - ox, cy - oy, [=](int x, int y) {
			               float dx = float(cx - ox), dy = float(cy - oy), l = dx * dx + dy * dy;
			               float t = l > 0 ? std::clamp(((x - ox) * dx + (y - oy) * dy) / l, 0.f, 1.f) : 0;
			               float ex = x - ox - t * dx, ey = y - oy - t * dy;
			               return ex * ex + ey * ey <= float(b * b);
		               } };
		w.id = 2; // not a window handle
		broomAt = { cx, cy };
		if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
	}
	else
	{
		broomAt = { -1, -1 };
	}
	for (auto &r : gaps)
	{
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		OverlayWall w{ std::max(0, px(r.left - sr.left, WINDOWW, sw)), std::max(0, px(r.top - sr.top, WINDOWH, sh)),
		               std::min(XRES, px(r.right - sr.left, WINDOWW, sw)), std::min(YRES, px(r.bottom - sr.top, WINDOWH, sh)), 0, 0, {} };
		if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
	}
#ifdef __APPLE__
	if (windowWalls && dockKnown)
	{
		// The Dock itself: solid, rounded at its ends like the real one, and past them nothing, so what slides off falls to
		// the void at the bottom of the screen
		auto px = [&](double v, int n, double from, int nw) { return int(std::lround((v * MacScale() - from) * n / nw)); };
		int x0 = px(dockL, WINDOWW, sr.left, sw), y0 = px(dockT, WINDOWH, sr.top, sh), x1 = px(dockR, WINDOWW, sr.left, sw), y1 = px(dockB, WINDOWH, sr.top, sh);
		float radius = 18.f * float(MacScale()) * WINDOWW / sw;
		OverlayWall w{ std::max(0, x0), std::max(0, y0), std::min(XRES, x1), std::min(YRES, y1), 0, 0, [=](int x, int y) {
			float dx = std::max({ x0 + radius - x - 0.5f, x + 0.5f - (x1 - radius), 0.f }), dy = std::max({ y0 + radius - y - 0.5f, y + 0.5f - (y1 - radius), 0.f });
			return dx * dx + dy * dy <= radius * radius;
		} };
		w.id = 4; // not a window handle
		if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
	}
#endif
	auto icons = doIcons ? DesktopIcons() : std::vector<DeskIcon>{};
	static std::map<uintptr_t, RECT> iconRects; // where each icon was on the last tick
	std::map<uintptr_t, RECT> iconsNow;
	if (doIcons)
		for (auto &icon : icons)
		{
			auto &r = icon.r;
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			OverlayWall w{ std::max(0, px(r.left - sr.left, WINDOWW, sw)), std::max(0, px(r.top - sr.top, WINDOWH, sh)),
			               std::min(XRES, px(r.right - sr.left, WINDOWW, sw)), std::min(YRES, px(r.bottom - sr.top, WINDOWH, sh)), 0, 0, {} };
			// each sim pixel takes the icon pixel under its centre (the whole square while its picture isn't in yet)
			w.solid = [&icon, sr, sw, sh](int x, int y) {
				int n = icon.r.right - icon.r.left;
				int ix = int(sr.left + (x + 0.5) * sw / WINDOWW) - icon.r.left, iy = int(sr.top + (y + 0.5) * sh / WINDOWH) - icon.r.top;
				return ix >= 0 && iy >= 0 && ix < n && iy < n && (!icon.shape || (*icon.shape)[size_t(iy) * n + ix]);
			};
			if (auto it = iconRects.find(icon.id); it != iconRects.end())
			{
				w.mx = px(r.left - it->second.left, WINDOWW, sw);
				w.my = px(r.top - it->second.top, WINDOWH, sh);
			}
			w.id = icon.id;
			iconsNow[icon.id] = r;
			if (fun.bin && icon.bin) // the Recycle Bin swallows whatever loose stuff lands on it
			{
				auto &elements = SimulationData::CRef().elements;
				for (int y = std::max(0, w.y0 - 3); y < std::min(YRES, w.y1); y++)
					for (int x = w.x0; x < w.x1; x++)
						if (int r = sim->pmap[y][x]; r && !(elements[TYP(r)].Properties & TYPE_SOLID)) sim->kill_part(ID(r));
			}
			if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
		}
	iconRects = std::move(iconsNow);
	OverlayBuddy::Walls(wins); // his body, in front

	OverlayApplyWalls(sim, wins);
#ifdef __APPLE__
	// A Mac window's corners are round: past the curve is open, unless something else stands there. Done on the finished
	// mask, so the walls' own passes keep their plain boxes.
	if (sim->pixelWalls.size() == size_t(XRES) * YRES)
	{
		static const float cornerPoints = [] { return OverlayMac::SystemMajor() >= 26 ? 16.f : 10.f; }();
		int r = int(std::lround(cornerPoints * MacScale() * WINDOWW / sw));
		auto covered = [&](const OverlayWall &self, int x, int y) {
			for (auto &o : wins)
				if (&o != &self && x >= o.x0 && x < o.x1 && y >= o.y0 && y < o.y1 && (!o.solid || o.solid(x, y))) return true;
			return false;
		};
		for (auto &w : wins)
		{
			if (!wallRects.count(HWND(w.id)) || w.x1 - w.x0 < 2 * r || w.y1 - w.y0 < 2 * r) continue;
			for (int j = 0; j < r; j++)
				for (int i = 0; i < r; i++)
				{
					float dx = r - i - 0.5f, dy = r - j - 0.5f;
					if (dx * dx + dy * dy <= float(r * r)) continue;
					for (auto [x, y] : { std::pair{ w.x0 + i, w.y0 + j }, { w.x1 - 1 - i, w.y0 + j }, { w.x0 + i, w.y1 - 1 - j }, { w.x1 - 1 - i, w.y1 - 1 - j } })
						if (!covered(w, x, y)) sim->pixelWalls[size_t(y) * XRES + x] = 0;
				}
		}
	}
#endif

	// Explosions shake the real windows they hit (off unless the Shake slider is up): a jolt that dies away, then the
	// window goes back exactly where it was. Not maximised or full-screen ones, and a window you grab stops shaking.
	struct Shaking { POINT home, set; float amp; };
	static std::map<HWND, Shaking> shaking;
	for (auto [id, kick] : overlayKicks)
	{
		HWND hwnd = HWND(id);
		RECT r;
		if (!wallRects.count(hwnd) || IsZoomed(hwnd) || !GetWindowRect(hwnd, &r)) continue; // icons aren't windows
		auto it = shaking.find(hwnd);
		float amp = std::min(40.f, kick * 4 * overlayFeel.shake) * sw / WINDOWW;
		if (it == shaking.end()) shaking[hwnd] = { { r.left, r.top }, { r.left, r.top }, amp };
		else it->second.amp = std::max(it->second.amp, amp);
	}
	for (auto it = shaking.begin(); it != shaking.end();)
	{
		auto &s = it->second;
		RECT r;
#ifdef __APPLE__
		// the window server reports a move a beat late, so where it is says nothing: a held button says it's you
		if (!IsWindow(it->first) || !GetWindowRect(it->first, &r) || GetAsyncKeyState(VK_LBUTTON) < 0)
#else
		if (!IsWindow(it->first) || !GetWindowRect(it->first, &r) || r.left != s.set.x || r.top != s.set.y)
#endif
		{
			it = shaking.erase(it); // closed, or moved by someone else (you): leave it be
			continue;
		}
		s.amp *= 0.8f;
		bool done = s.amp < 1;
		s.set = done ? s.home : POINT{ s.home.x + LONG(std::lround((sim->rng.uniform01() * 2 - 1) * s.amp)), s.home.y + LONG(std::lround((sim->rng.uniform01() * 2 - 1) * s.amp)) };
		SetWindowPos(it->first, nullptr, s.set.x, s.set.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
		it = done ? shaking.erase(it) : std::next(it);
	}
}

void OverlayWindows::DrawExtras(uint32_t *argb, int w, int h)
{
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	auto put = [&](int x, int y, uint32_t rgb, float a) { // premultiplied "over"
		if (x < 0 || y < 0 || x >= std::min(w, XRES) || y >= std::min(h, YRES) || a <= 0) return;
		auto &p = argb[y * w + x];
		auto mix = [&](int s) { return uint32_t(((rgb >> s) & 0xFF) * a + ((p >> s) & 0xFF) * (1 - a)) << s; };
		p = mix(0) | mix(8) | mix(16) | (uint32_t(255 * a + (p >> 24) * (1 - a)) << 24);
	};
	// the rainbow: a faint, see-through arc whose colours blend into each other, soft at both edges and fading toward
	// its feet; only where nothing else is drawn (behind it all), not over windows; fades in and out
	if (auto now = GetTickCount64(); now < rainbowUntil)
	{
		float left = (rainbowUntil - now) / 1000.f;
		float a = fun.rainbowGlow * 0.45f * std::min({ 1.f, (fun.rainbow - left) / 3, left / 3 });
		auto play = sim->PlayRect();
		int ph = play.size.Y, rows = std::max(7, ph / 14), cx = play.pos.X + play.size.X / 2, cy = play.pos.Y + ph + ph / 5;
		auto arc = [&](int outer, int rows, bool flip, float a) { // red outside (flip: inside)
			static const uint32_t stops[] = { 0xFF2020, 0xFF8C00, 0xFFE000, 0x30C030, 0x2080FF, 0x4B30C0, 0x9040E0 };
			for (int k = 0; k < rows; k++)
			{
				float f = 6.f * (flip ? rows - 1 - k : k) / (rows - 1), mix = f - int(f); // between colour stop int(f) and the next
				uint32_t c0 = stops[int(f)], c1 = stops[std::min(6, int(f) + 1)], rgb = 0;
				for (int s = 0; s < 24; s += 8) rgb |= uint32_t(((c0 >> s) & 0xFF) * (1 - mix) + ((c1 >> s) & 0xFF) * mix) << s;
				float edge = std::sin(float(M_PI) * (k + 0.5f) / rows); // soft inner and outer edges
				int r = outer - k, n = int(3.2f * r);
				for (int i = 0; i <= n; i++)
				{
					float t = float(M_PI) * (1 + float(i) / n), up = -std::sin(t); // 1 at the top, 0 at the feet
					int x = cx + int(r * std::cos(t)), y = cy + int(r * std::sin(t));
					if (x >= 0 && y >= 0 && x < std::min(w, XRES) && y < std::min(h, YRES) && !(argb[y * w + x] >> 24) && !sim->PixelWall(x, y))
						put(x, y, rgb, a * edge * std::min(1.f, up * 1.5f));
				}
			}
		};
		int outer = cy - play.pos.Y - ph / 4;
		arc(outer, rows, false, a);
		if (rainbowTwice) arc(outer + rows * 2, rows * 13 / 10, true, a * 0.4f); // the double: wider, fainter, colours reversed
	}
	// shooting stars: at night, now and then a bright head with a fading tail streaks down across the open desktop
	{
		// big: a meteor (slower, glowing orange, a smoke tail; it always comes down and crashes)
		struct Star { float x, y, dx, dy; ULONGLONG born; bool land, big; float hit = -1; ULONGLONG hitAt = 0; }; // land: comes down until it hits something
		static std::vector<Star> stars;
		static ULONGLONG nextStar = 0, nextMeteor = 0;
		auto now = GetTickCount64();
		bool night = funHour >= 20 || funHour < 5;
		// a meteor shower night: the peaks (month, day) of the yearly showers, and the day either side
		bool shower = false;
		if (fun.showers && night)
		{
			time_t tt = time(nullptr) - (funHour < 5 ? 86400 : 0); // after midnight it is still the night before
			tm local;
			localtime_s(&local, &tt);
			for (auto [mo, d] : { std::pair{ 1, 3 }, { 4, 22 }, { 5, 6 }, { 8, 12 }, { 10, 21 }, { 11, 17 }, { 12, 14 } })
				shower |= local.tm_mon + 1 == mo && std::abs(local.tm_mday - d) <= 1;
		}
		float perHour = shower ? std::max(fun.stars, 6.f) * 10 : fun.stars, meteorsPerHour = fun.meteors * (shower ? 3 : 1);
		auto spawn = [&](bool land, bool big) {
			auto play = sim->PlayRect();
			float dir = sim->rng.chance(1, 2) ? 1.f : -1.f, len = play.size.X * (0.15f + 0.15f * sim->rng.uniform01());
			stars.push_back({ play.pos.X + play.size.X * (0.15f + 0.7f * sim->rng.uniform01()), play.pos.Y + play.size.Y * 0.4f * sim->rng.uniform01(), dir * len, len * (big ? 0.9f : land ? 0.8f : 0.35f), now, land, big });
		};
		if (perHour < 1 || !night) nextStar = 0;
		else if (!nextStar) nextStar = now + ULONGLONG(3600000.0 / perHour * (0.3 + 1.4 * sim->rng.uniform01()));
		else if (now >= nextStar) nextStar = 0, spawn(fun.starsLand && sim->rng.chance(1, 3), false);
		if (meteorsPerHour < 1 || !night) nextMeteor = 0;
		else if (!nextMeteor) nextMeteor = now + ULONGLONG(3600000.0 / meteorsPerHour * (0.3 + 1.4 * sim->rng.uniform01()));
		else if (now >= nextMeteor) nextMeteor = 0, spawn(true, true);
		for (auto &st : stars)
		{
			float span = st.big ? 1600.f : 900.f, t = (now - st.born) / span, fade = st.land ? 1 : t > 0.7f ? (1 - t) / 0.3f : 1;
			float len = std::hypot(st.dx, st.dy), ux = st.dx / len, uy = st.dy / len;
			if (st.land && st.hit < 0)
				for (float d = 0; d <= len * t; d += 1) // the first window, particle or screen edge on its way down
				{
					int x = int(st.x + ux * d), y = int(st.y + uy * d);
					bool out = x < 0 || y < 0 || x >= XRES || y >= YRES;
					if (out || (d > 30 && (sim->PixelWall(x, y) || sim->pmap[y][x]))) // (not what it started over)
					{
						st.hit = d, st.hitAt = now;
						if (!out) overlayImpacts.push_back({ x, y, st.big ? 3.f : 1.f });
						break;
					}
				}
			if (st.hit >= 0) t = st.hit / len, fade = std::max(0.f, 1 - (now - st.hitAt) / 400.f); // it hit: the tail fades
			float maxTail = st.big ? 160.f : 90.f, hx = st.x + st.dx * t, hy = st.y + st.dy * t, tail = std::min(len * t, maxTail);
			for (int k = 0; k < int(tail); k++) // head first, the tail falls behind and fades
			{
				uint32_t c = !st.big ? (k < 3 ? 0xFFFFFF : 0xCFE0FF) : k < 4 ? 0xFFF4D0 : k < 30 ? 0xFFB040 : k < 70 ? 0xE05A20 : 0x6A6460; // a meteor: white-hot, fire, then smoke
				for (int o = 0; o < (st.big && k < 90 ? 3 : 1); o++) // a meteor is 3 px thick
				{
					int x = int(hx - ux * k - uy * (o - (st.big ? 1 : 0))), y = int(hy - uy * k + ux * (o - (st.big ? 1 : 0)));
					if (x >= 0 && y >= 0 && x < std::min(w, XRES) && y < std::min(h, YRES) && !(argb[y * w + x] >> 24) && !sim->PixelWall(x, y))
						put(x, y, c, fade * (1 - k / maxTail) * (o == 1 || !st.big ? 1 : 0.5f));
				}
			}
		}
		std::erase_if(stars, [now](auto &st) { return now - st.born >= (st.land ? 9000u : 900u) || (st.hitAt && now - st.hitAt >= 400); });
	}
	// paper planes: a white dart with a grey fold, nose first; landed ones fade over their last 2 s
	for (auto &pl : paperPlanes)
	{
		int d = pl.vx >= 0 ? 1 : -1, x = int(pl.x), y = int(pl.y);
		float a = pl.landed > 480 ? (600 - pl.landed) / 120.f : 1;
		for (int i = 0; i < 7; i++) put(x - d * i, y, 0xB8B8C4, a);
		for (int i = 2; i < 7; i++) put(x - d * i, y - 1, 0xF4F4F4, a);
		for (int i = 4; i < 7; i++) put(x - d * i, y - 2, 0xF4F4F4, a);
	}
	// napping pets: three small z's drifting up from each one's head and fading
	if (sim->petNap)
	{
		auto step = int(GetTickCount64() / 50);
		for (int f = 0; f < MAX_FIGHTERS; f++)
		{
			auto &pet = sim->fighters[f];
			if (!pet.spwn) continue;
			int hx = int(pet.legs[4] + pet.legs[12]) / 2, hy = int(std::min(pet.legs[5], pet.legs[13])) - 16;
			for (int k = 0; k < 3; k++)
			{
				int age = (step + k * 20 + f * 7) % 60; // 0-59
				int zx = hx + 3 + age / 8, zy = hy - age / 3;
				float a = 0.9f * (1 - age / 60.f);
				for (int i = 0; i < 3; i++) // z: top row, bottom row, and the middle of the diagonal
					put(zx + i, zy, 0xE0F0FF, a), put(zx + i, zy + 2, 0xE0F0FF, a);
				put(zx + 1, zy + 1, 0xE0F0FF, a);
			}
		}
	}
}

void OverlayWindows::ClearUnderWindows(uint32_t *argb, int w, int h, int ww, int wh)
{
	// ponytail: also clears where another app's window covers ours; walk the z-order if that ever matters.
	if (ww <= 0 || wh <= 0)
	{
		return;
	}
	RECT sr;
	GetWindowRect(mainWindow, &sr);
	auto map = [](long v, int n, int nw, bool up) { return int(std::clamp((v * n + (up ? nw - 1 : 0)) / nw, 0L, long(n))); };
	auto clear = [&](HWND hwnd) {
		if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd))
		{
			return;
		}
		RECT pr;
		GetWindowRect(hwnd, &pr);
		int x0 = map(pr.left - sr.left, w, ww, false), x1 = map(pr.right - sr.left, w, ww, true);
		int y0 = map(pr.top - sr.top, h, wh, false), y1 = map(pr.bottom - sr.top, h, wh, true);
		for (int y = y0; y < y1; y++)
		{
			std::fill(argb + size_t(y) * w + x0, argb + size_t(y) * w + x1, 0);
		}
	};
	clear(palette ? palette->hwnd : nullptr);
	for (auto &t : torn) clear(t->hwnd);
	for (auto &host : hosts)
	{
		clear(host->hwnd);
	}
}

// Which play-area edges are under the mouse: 1 left, 2 right, 4 top, 8 bottom (4 px outside to 1 inside).
static int AreaEdges(Simulation *sim, int x, int y)
{
	auto a = sim->PlayRect();
	auto br = a.BottomRight();
	if (x < a.pos.X - 4 || x > br.X + 4 || y < a.pos.Y - 4 || y > br.Y + 4) return 0;
	return (x <= a.pos.X + 1 ? 1 : x >= br.X - 1 ? 2 : 0) | (y <= a.pos.Y + 1 ? 4 : y >= br.Y - 1 ? 8 : 0);
}

bool OverlayWindows::AreaMouse(int x, int y, int what)
{
	static int drag = 0, shown = 0;
	static Vec2<int> from{ 0, 0 };
	static Rect<int> start = CELLS.OriginRect();
	if (!&GameController::Ref() || !overlayMode || overlayWindowed) return false; // windowed: the window's edges are the area's
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	if (what == 1)
	{
		drag = AreaEdges(sim, x, y);
		from = { x, y };
		start = sim->playArea;
	}
	else if (what == 2)
	{
		bool was = drag;
		drag = 0;
		return was;
	}
	else if (drag)
	{
		int dx = int(std::lround(double(x - from.X) / CELL)), dy = int(std::lround(double(y - from.Y) / CELL));
		int l = start.pos.X, t = start.pos.Y, r = l + start.size.X, b = t + start.size.Y;
		if (drag & 1) l = std::clamp(l + dx, 0, r - 8);
		if (drag & 2) r = std::clamp(r + dx, l + 8, XCELLS);
		if (drag & 4) t = std::clamp(t + dy, 0, b - 8);
		if (drag & 8) b = std::clamp(b + dy, t + 8, YCELLS);
		if (RectSized(Vec2(l, t), Vec2(r - l, b - t)) != sim->playArea) SetPlayArea(l, t, r - l, b - t);
	}
	int e = drag ? drag : AreaEdges(sim, x, y);
	if (e != shown)
	{
		static SDL_Cursor *we = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE), *ns = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS),
		                  *nwse = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE), *nesw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
		SDL_SetCursor(!e ? SDL_GetDefaultCursor() : e == 5 || e == 10 ? nwse : e == 6 || e == 9 ? nesw : e & 3 ? we : ns);
		shown = e;
	}
	return drag;
}

void OverlayWindows::SetWindowedView(int w, int h)
{
	if (!&GameController::Ref()) return;
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	// whole cells covering the view (a partial one would show a strip of wall)
	sim->playArea = RectSized(Vec2(0, 0), Vec2(std::clamp((w + CELL - 1) / CELL, 1, XCELLS), std::clamp((h + CELL - 1) / CELL, 1, YCELLS)));
	sim->ApplyPlayArea();
	GlobalPrefs::Ref().Set("Overlay.WindowedView.W", w);
	GlobalPrefs::Ref().Set("Overlay.WindowedView.H", h);
}

void OverlayWindows::SetPlayArea(int x, int y, int w, int h)
{
	if (!&GameController::Ref())
	{
		return;
	}
	auto area = RectSized(Vec2(x, y), Vec2(w, h)) & CELLS.OriginRect();
	if (area.size.X < 8 || area.size.Y < 8)
	{
		area = CELLS.OriginRect(); // too small to play in: back to the whole sim
	}
	GameController::Ref().GetModel()->GetSimulation()->playArea = area;
	auto &prefs = GlobalPrefs::Ref();
	prefs.Set("Overlay.PlayArea.X", area.pos.X);
	prefs.Set("Overlay.PlayArea.Y", area.pos.Y);
	bool full = area == CELLS.OriginRect();
	prefs.Set("Overlay.PlayArea.W", full ? 0 : area.size.X);
	prefs.Set("Overlay.PlayArea.H", full ? 0 : area.size.Y);
}


#ifdef _WIN32
int OverlayWindows::Form(const String &title, const String &prompt, const std::vector<String> &options, int &choice, String &text,
                         const std::vector<String> &extras, std::function<bool(int, const String &)> valid, int limit)
{
	if (!mainWindow) return -1;
	enum { idCombo = 100, idEdit, idExtra };
	struct State
	{
		std::function<bool(int, const String &)> valid;
		HWND combo = nullptr, edit, ok;
		int result = -1;
		void Check()
		{
			wchar_t buf[512];
			GetWindowTextW(edit, buf, 512);
			EnableWindow(ok, valid(int(SendMessageW(combo, CB_GETCURSEL, 0, 0)), Platform::WinNarrow(buf).FromUtf8()));
		}
	} st{ valid };
	static bool registered = false;
	if (!registered)
	{
		WNDCLASSW wc{};
		wc.lpfnWndProc = [](HWND w, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
			auto *s = (State *)GetWindowLongPtrW(w, GWLP_USERDATA);
			if (s && msg == WM_COMMAND)
			{
				int id = LOWORD(wp), code = HIWORD(wp);
				if ((id == idCombo && code == CBN_SELCHANGE) || (id == idEdit && code == EN_CHANGE)) s->Check();
				else if (id == IDOK && IsWindowEnabled(s->ok)) s->result = 1;
				else if (id == IDCANCEL) s->result = 0;
				else if (id >= idExtra) s->result = 2 + id - idExtra;
				return 0;
			}
			if (s && msg == WM_CLOSE) return s->result = 0, 0;
			return DefWindowProcW(w, msg, wp, lp);
		};
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
		wc.lpszClassName = L"TPTOverlayForm";
		RegisterClassW(&wc);
		registered = true;
	}
	HWND owner = palette && IsWindowVisible(palette->hwnd) ? palette->hwnd : mainWindow;
	HDC screen = GetDC(nullptr);
	int dpi = GetDeviceCaps(screen, LOGPIXELSY); // the system DPI, which SystemParametersInfo's font is sized for
	ReleaseDC(nullptr, screen);
	auto D = [dpi](int v) { return MulDiv(v, dpi, 96); };
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);

	int dy = options.empty() ? 32 : 0; // no drop-down: everything moves up
	int cw = D(340), ch = D(146 - dy);
	RECT wr{ 0, 0, cw, ch };
	DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
	AdjustWindowRectEx(&wr, style, FALSE, ex);
	RECT or_;
	GetWindowRect(owner, &or_);
	int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
	HWND dlg = CreateWindowExW(ex, L"TPTOverlayForm", Wide(title).c_str(), style, (or_.left + or_.right - ww) / 2,
	                           (or_.top + or_.bottom - wh) / 2, ww, wh, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
	auto child = [&](const wchar_t *cls, const wchar_t *txt, DWORD st, int x, int y, int w, int hh, int id) {
		HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, txt, WS_CHILD | WS_VISIBLE | st,
		                         D(x), D(y), D(w), D(hh), dlg, HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(c, WM_SETFONT, WPARAM(font), TRUE);
		return c;
	};
	child(L"STATIC", Wide(prompt).c_str(), 0, 12, 12, 316, 18, -1);
	if (!options.empty())
	{
		st.combo = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 12, 36, 160, 300, idCombo);
		for (auto &o : options) SendMessageW(st.combo, CB_ADDSTRING, 0, LPARAM(Wide(o).c_str()));
		SendMessageW(st.combo, CB_SETCURSEL, WPARAM(choice), 0);
	}
	st.edit = child(L"EDIT", Wide(text).c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 12, 68 - dy, 316, 24, idEdit);
	if (limit) SendMessageW(st.edit, EM_SETLIMITTEXT, WPARAM(limit), 0);
	int bx = 12;
	for (int i = 0; i < int(extras.size()); i++, bx += 81) child(L"BUTTON", Wide(extras[i]).c_str(), BS_PUSHBUTTON | WS_TABSTOP, bx, 108 - dy, 75, 26, idExtra + i);
	st.ok = child(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 172, 108 - dy, 75, 26, IDOK);
	child(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 253, 108 - dy, 75, 26, IDCANCEL);
	SetWindowLongPtrW(dlg, GWLP_USERDATA, LONG_PTR(&st));
	st.Check();
	st.result = -1;

	// ponytail: a modal loop on TPT's thread, so the sim waits while it's open (as with the menus)
	EnableWindow(owner, FALSE);
	ShowWindow(dlg, SW_SHOW);
	SetForegroundWindow(dlg);
	SetFocus(st.edit);
	SendMessageW(st.edit, EM_SETSEL, 0, -1);
	MSG msg;
	while (st.result < 0 && GetMessageW(&msg, nullptr, 0, 0))
	{
		if (IsDialogMessageW(dlg, &msg)) continue;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (msg.message == WM_QUIT) PostQuitMessage(int(msg.wParam));
	wchar_t buf[512];
	GetWindowTextW(st.edit, buf, 512);
	text = Platform::WinNarrow(buf).FromUtf8();
	if (st.combo) choice = int(SendMessageW(st.combo, CB_GETCURSEL, 0, 0));
	EnableWindow(owner, TRUE);
	DestroyWindow(dlg);
	DeleteObject(font);
	SetForegroundWindow(owner);
	return std::max(0, st.result);
}

// The Lua console: a read-only log over a command line. Enter runs (TPT's ! commands too; an unfinished block
// shows "..." and waits for the rest), Up/Down walk the history, Esc hides it.
namespace
{
	struct Console
	{
		HWND wnd = nullptr, out = nullptr, in = nullptr;
		HFONT font = nullptr;
		WNDPROC editProc = nullptr;
		std::vector<std::wstring> history;
		size_t at = 0;

		void Append(const std::wstring &s)
		{
			std::wstring t;
			for (auto c : s) // the edit control wants \r\n
			{
				if (c == L'\n') t += L'\r';
				t += c;
			}
			int n = GetWindowTextLengthW(out);
			SendMessageW(out, EM_SETSEL, n, n);
			SendMessageW(out, EM_REPLACESEL, FALSE, LPARAM((t + L"\r\n").c_str()));
		}

		void Run()
		{
			int n = GetWindowTextLengthW(in);
			std::wstring cmd(size_t(n), L'\0');
			GetWindowTextW(in, cmd.data(), n + 1);
			SetWindowTextW(in, L"");
			if (cmd.empty()) return;
			if (history.empty() || history.back() != cmd) history.push_back(cmd);
			at = history.size();
			Append(L"> " + cmd);
			auto *ci = GameController::Ref().GetCommandInterface();
			int code = ci->Command(Platform::WinNarrow(cmd).FromUtf8());
			auto result = Wide(ci->GetLastError());
			if (result.size()) Append(code && result != L"..." ? L"Error: " + result : result); // "...": waiting for the rest of a block
		}

		static LRESULT CALLBACK InputProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
		static LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp);

		void Layout()
		{
			RECT rc;
			GetClientRect(wnd, &rc);
			TEXTMETRICW tm;
			HDC dc = GetDC(in);
			auto old = SelectObject(dc, font);
			GetTextMetricsW(dc, &tm);
			SelectObject(dc, old);
			ReleaseDC(in, dc);
			int ih = tm.tmHeight + 10;
			MoveWindow(out, 0, 0, rc.right, rc.bottom - ih, TRUE);
			MoveWindow(in, 0, rc.bottom - ih, rc.right, ih, TRUE);
		}

		void Show()
		{
			if (!wnd)
			{
				WNDCLASSW wc{};
				wc.lpfnWndProc = Proc;
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
				wc.lpszClassName = L"TPTOverlayConsole";
				wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
				RegisterClassW(&wc);
				HDC screen = GetDC(nullptr);
				int dpi = GetDeviceCaps(screen, LOGPIXELSY);
				ReleaseDC(nullptr, screen);
				font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
				                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
				RECT work;
				SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
				int w = MulDiv(760, dpi, 96), h = MulDiv(420, dpi, 96);
				wnd = CreateWindowExW(WS_EX_APPWINDOW, L"TPTOverlayConsole", L"Lua console - The Powder Toy", WS_OVERLAPPEDWINDOW,
				                      (work.left + work.right - w) / 2, work.bottom - h - MulDiv(40, dpi, 96), w, h,
				                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
				out = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
				                      0, 0, 0, 0, wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
				in = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, wnd, nullptr,
				                     GetModuleHandleW(nullptr), nullptr);
				SendMessageW(out, WM_SETFONT, WPARAM(font), FALSE);
				SendMessageW(in, WM_SETFONT, WPARAM(font), FALSE);
				SendMessageW(out, EM_SETLIMITTEXT, 0, 0); // no 32k cap
				SendMessageW(in, EM_SETCUEBANNER, TRUE, LPARAM(L"Lua, or !command; Enter runs, Up/Down for history, Esc hides"));
				editProc = WNDPROC(SetWindowLongPtrW(in, GWLP_WNDPROC, LONG_PTR(InputProc)));
				Append(L"The Powder Toy's Lua console. Try: tpt.version.major, sim.partCount(), or !set type dust sand");
				Layout();
			}
			ShowWindow(wnd, IsIconic(wnd) ? SW_RESTORE : SW_SHOW);
			SetForegroundWindow(wnd);
			SetFocus(in);
		}
	} console;

	LRESULT CALLBACK Console::InputProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
	{
		if (msg == WM_KEYDOWN && wp == VK_RETURN) return console.Run(), 0;
		if (msg == WM_KEYDOWN && wp == VK_ESCAPE) return ShowWindow(console.wnd, SW_HIDE), 0;
		if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN) && !console.history.empty())
		{
			auto &c = console;
			c.at = wp == VK_UP ? (c.at ? c.at - 1 : 0) : std::min(c.at + 1, c.history.size());
			SetWindowTextW(w, c.at < c.history.size() ? c.history[c.at].c_str() : L"");
			SendMessageW(w, EM_SETSEL, GetWindowTextLengthW(w), GetWindowTextLengthW(w));
			return 0;
		}
		if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) return 0; // no beep
		return CallWindowProcW(console.editProc, w, msg, wp, lp);
	}

	LRESULT CALLBACK Console::Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch (msg)
		{
		case WM_SIZE: if (console.out) console.Layout(); return 0;
		case WM_SETFOCUS: if (console.in) SetFocus(console.in); return 0;
		case WM_CLOSE: ShowWindow(w, SW_HIDE); return 0;
		case WM_CTLCOLORSTATIC: // the read-only log on a white background, like a normal field
			SetBkColor(HDC(wp), GetSysColor(COLOR_WINDOW));
			return LRESULT(GetSysColorBrush(COLOR_WINDOW));
		}
		return DefWindowProcW(w, msg, wp, lp);
	}
}

bool OverlayWindows::ShowConsole()
{
	if (!mainWindow) return false;
	console.Show();
	return true;
}

void OverlayWindows::HideConsole()
{
	if (console.wnd) ShowWindow(console.wnd, SW_HIDE);
}
#endif
#ifdef __APPLE__
// What the Windows side gets from Explorer, the registry and its own controls.
ByteString Platform::WinNarrow(const std::wstring &source)
{
	return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(source);
}

std::wstring Platform::WinWiden(const ByteString &source)
{
	return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(source);
}

int OverlayWindows::Form(const String &title, const String &prompt, const std::vector<String> &options, int &choice, String &text,
                         const std::vector<String> &extras, std::function<bool(int, const String &)> valid, int)
{
	if (!mainWindow) return -1;
	std::vector<std::wstring> opts, more;
	for (auto &o : options) opts.push_back(Wide(o));
	for (auto &e : extras) more.push_back(Wide(e));
	auto value = Wide(text);
	int result = OverlayMac::Form(Wide(title), Wide(prompt), opts, choice, value, more, [&valid](int c, const std::wstring &v) {
		return !valid || valid(c, Platform::WinNarrow(v).FromUtf8());
	});
	if (result >= 1) text = Platform::WinNarrow(value).FromUtf8();
	return result;
}

namespace
{
	std::vector<std::wstring> consoleHistory;
}

bool OverlayWindows::ShowConsole()
{
	if (!mainWindow) return false;
	OverlayMac::ConsoleOpen({
		[](const std::wstring &cmd) {
			if (cmd.empty()) return;
			if (consoleHistory.empty() || consoleHistory.back() != cmd) consoleHistory.push_back(cmd);
			OverlayMac::ConsoleAppend(L"> " + cmd, false);
			auto *ci = GameController::Ref().GetCommandInterface();
			int code = ci->Command(Platform::WinNarrow(cmd).FromUtf8());
			auto result = Wide(ci->GetLastError());
			if (result.size()) OverlayMac::ConsoleAppend(code && result != L"..." ? L"Error: " + result : result, code && result != L"...");
		},
		[](int back) { return back > 0 && size_t(back) <= consoleHistory.size() ? consoleHistory[consoleHistory.size() - back] : std::wstring(); },
		[] { OverlayMac::FocusOverlay(); },
	});
	return true;
}

void OverlayWindows::HideConsole()
{
	OverlayMac::ConsoleClose();
}
#endif
#else
void OverlayWindows::Init(void *) {}
void OverlayWindows::Tick() {}
int OverlayWindows::WorkBottom(int) { return YRES; }
bool OverlayWindows::ActiveEdges(uintptr_t &, int &, int &, int &, int &) { return false; }
void OverlayWindows::TogglePalette() {}
void OverlayWindows::SetPassThrough(bool) {}
void OverlayWindows::Notify(const char *) {}
bool OverlayWindows::HideTPTMenus() { return false; }
void OverlayWindows::ApplySimMasks(uint32_t *, int, const uint8_t *) {}
void OverlayWindows::FrameWalls(std::vector<uint8_t> &, uint8_t) {}
std::vector<OverlayWindows::WindowShift> OverlayWindows::WindowShifts() { return {}; }
void OverlayWindows::ClearUnderWindows(uint32_t *, int, int, int, int) {}
void OverlayWindows::DrawExtras(uint32_t *, int, int) {}
void OverlayWindows::SetPlayArea(int, int, int, int) {}
void OverlayWindows::SetWindowedView(int, int) {}
bool OverlayWindows::AreaMouse(int, int, int) { return false; }
int OverlayWindows::Form(const String &, const String &, const std::vector<String> &, int &, String &, const std::vector<String> &,
                         std::function<bool(int, const String &)>, int) { return -1; }
bool OverlayWindows::ShowConsole() { return false; }
void OverlayWindows::HideConsole() {}
#endif
