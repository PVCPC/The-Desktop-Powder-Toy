# Desktop overlay mode (experimental)

The to-do list and ideas for the overlay live here. Keep it current.

`powder.exe overlay:window` is TPT's normal window with the side windows and tray (Overlay tab -> Size -> Runs as
switches between the two, restarting with the scene). `powder.exe` (or `overlay:on`) runs TPT as a borderless, always-on-top window over the desktop. Empty (black) pixels
are see-through. TPT presents its own frame with per-pixel alpha (`UpdateLayeredWindow`, `src/Overlay.cpp`), so
clicks on see-through pixels reach the desktop and windows behind. Windows, and macOS since 0.2 (`src/OverlayMac.mm`: a clear NSWindow whose frame is a Core Animation layer, `ignoresMouseEvents` from the pixel under the mouse, window walls from `CGWindowListCopyWindowInfo`, a menu bar item for the tray; TPT's own menus instead of the side window). The wall physics shared by both is `src/OverlayWalls.cpp`.

## Input modes

| Mode | Empty space | Particles and menus | Hotkey (default) |
|---|---|---|---|
| Auto | TPT while a TPT window is in front, the desktop once another app is | TPT | Ctrl+Alt+O cycles modes |
| Pass-through | desktop | desktop (the side windows still work) | Ctrl+Alt+P toggles |
| Locked | TPT (alpha 1: invisible but hit-testable) | TPT | Ctrl+Alt+L toggles |

The hotkeys are global (polled each frame, so they work while TPT is unfocused) and customisable in
`%APPDATA%\The Powder Toy\powder.pref`: `Overlay.Keys.CycleInput`, `Overlay.Keys.PassThrough` and
`Overlay.Keys.Locked`, as text like `"Ctrl+Alt+O"` (Ctrl, Alt, Shift, Win plus a letter, digit or F1-F24). The
current mode is saved as `Overlay.Input`. A fading glass notice shows the new mode. Ctrl+Alt+C
(`Overlay.Keys.Clear`) clears the sim.

Desktop physics (OverlayWalls.cpp, shared by every platform): sand on a window rides along when it moves and is
flung off when it stops short or turns back (window speed averaged over ticks, since positions don't arrive in step
with them); a moving window shoves what it moves onto; what touches a window marks it (`Simulation::windowMarks`): 300 C and up scorches, -20 C and
below frosts (melts), acid pits, water streaks (dries); scorch and pits stay until the sim is cleared; the mouse stirs the air where the overlay lets clicks through. The
Simulation tab's "Desktop physics" sliders scale each (`Overlay.Feel.Grip/Fling/Push/Wind/Scorch/Frost/Acid/Water`). Windows only so
far for scorch drawing and the mouse breeze (macOS: ApplySimMasks/cursor hook still to do).

The scene is kept between runs: quitting (or Windows shutting down) saves it as `overlay-scene-*.cps` tiles next to
`powder.pref`, and the next start loads and deletes them.

While nothing moves and TPT isn't in use, the overlay draws 15 frames a second and the sim ticks at most 20 times
(`overlayIdle`); anything moving brings back the full rate.

## Windows

TPT's menus are moving into their own desktop windows (`src/OverlayWindows.cpp`): real top-level layered windows,
drawn in software with per-pixel alpha, styled like Windows Vista Aero. They don't take keyboard focus from TPT.

- **The Powder Toy** side window, with tabs (Ctrl+Alt+E, `Overlay.Keys.Palette`, or the tray icon):
  - **Elements**: menu sections down the left (Explorer navigation pane), tools as Vista buttons, the hovered
    tool's description in the status bar. Left click = primary tool (red outline), right click = secondary (blue).
  - **Simulation**: play area size, pause/step/reload/clear, edges, gravity, Newtonian gravity, air, ambient heat.
  - **Display**: TPT's view modes, HUD, decorations, gravity field, TPT's full render options.
  - **Saves**: open, save, stamps, reload, TPT's options.
  - **Overlay**: where clicks go, show TPT's own menus, the shortcut list.
  Drag by the title bar; the position is saved (`Overlay.Windows.Palette.X/Y`). In Pass-through mode it fades and
  lets clicks through. The status bar says what the hovered control does.
- Buttons show TPT's own icons (walls, tools) where it has them, colour swatches otherwise.
- **Minimise** and close both hide a window to the tray icon.
- While the side window is open, TPT's own side bar and tool strip are hidden (see-through, click-through);
  so is the bottom button row (it would sit on the taskbar). The tray menu's "Show TPT's own menus" brings them back (`Overlay.ShowTPTMenus`).
- **Tray icon** (notification area, bottom right): a plain Windows menu on purpose. It holds settings and opens
  the Vista windows. Left click shows or hides the Elements window; right click: Elements window, the three input
  modes, Show TPT's own menus, Exit. Removed on exit and on a crash, re-added if Explorer restarts. A forced kill
  (Task Manager) can't be caught; Windows clears that leftover icon when you hover it.
- UI scale: `Overlay.UIScale` (default: the larger of Windows display scaling and screen height / 1080, in
  quarter steps).
- **Sim size**: the build sets the largest sim (`meson configure build -Dsim_cells_x=422 -Dsim_cells_y=110`, in
  cells of 4 pixels; 422x110 cells x 3 pixel scale fills a 5120x1440 screen). The **play area** inside it changes
  at run time (Simulation tab; `Overlay.PlayArea.X/Y/W/H`, in cells). Outside it is wall (solid or loop edges) or a
  particle and air sink (void edges), and see-through on the desktop; a faint blue frame marks the edge. Saves
  cover the play area only, and loads land at its top-left, so a save made at "Stock TPT size" (612x384) opens in
  normal TPT, and any save opens in any area (bigger ones are clipped).
- Black particles stay visible and clickable (the sim's particle map decides, not the colour). Not inside the
  zoom window yet.

## Desktop UI design (2026-10-30)

The overlay puts TPT on the desktop, so its UI has to behave like a desktop app, not a game screen. Today it
doesn't: TPT's ~25 dialogs (plus any window a Lua script makes) are drawn inside the full-screen, always-on-top
overlay, and TPT only runs the top screen, so **the sim freezes while any dialog is open**.

### Rules

1. **Two layers.** The *sim layer* is the full-screen, see-through overlay: particles only, never a dialog or menu.
   Everything else is an *app window*.
2. **App windows behave like normal windows**: not always on top (other apps can cover them), a title bar, minimise,
   maximise where it makes sense, close, resize by dragging any edge, Aero Snap, Alt+Tab, remembered position and
   size per window, moved to the right monitor and DPI.
3. **One app on the taskbar.** The main window (today's side window) has the taskbar button and the tray icon.
   Other windows are owned by it: they minimise and restore with it and don't clutter the taskbar.
4. **The sim never stops for UI.** Dialogs are modal only to the window they came from (e.g. the save browser is
   modal to the main window), never to the sim. Pausing is the user's choice.
5. **Windows take the keyboard when they need it** (search, save names, the console), and give it back.
   TPT's single-key shortcuts work when the sim layer or the main window is active; a few global hotkeys
   (Ctrl+Alt+...) work everywhere.
6. **Nothing pops up over your work uninvited.** Startup notices, update news and errors become toasts or a
   notifications list in the main window; only things that need an answer open a window.
7. **Scales with the window and the monitor**: the content of a resized window lays out again (native windows) or
   scales (re-hosted TPT dialogs). Per-monitor DPI.
8. **Themes are skins on top of real window behaviour**: the frame is ours (Vista now, other OSes later), but
   hit testing, snapping, minimise and the taskbar are Windows' own, so a theme can't break them.
9. **Where the sim layer sits is a setting**: on top of everything (now), under app windows, or on the desktop
   behind the icons.

### How to get there (in order)

1. **Window foundation** (M): turn our Panel into a real top-level window: normal z-order, owner = main window,
   taskbar button on the main window, WM_NCHITTEST frame for resize and snap, minimise/maximise/close, saved
   size, keyboard focus on demand. The side window becomes the main window.
   - [x] Done (2026-10-30): the side window is the main app window (taskbar button, not on top, resize by any
     edge, snap, minimise/maximise/close, saved size); the sim layer is cleared under it so sand stays off it and
     clicks reach it. Left for later: owned windows (none exist until step 3) and keyboard focus on demand.
2. **Sim keeps running under dialogs** (S-M): in overlay mode, tick and draw the game every frame even when a
   dialog is on top of TPT's window stack.
   - [x] Done (2026-10-30): Engine::LiveBackground (on in overlay mode) sim-ticks and redraws the windows under
     the top one instead of showing a frozen, faded copy. Only the top window gets input.
3. **Re-host every TPT dialog** (M): any TPT window other than the game opens in its own app window: drawn from its
   own surface, mouse and keys routed back with the offset, title from the dialog, close = the dialog's own
   cancel. This moves *all* of TPT's UI (and Lua scripts' windows) off the overlay at once. They keep TPT's look
   inside our frame and scale by whole pixels.
   - [x] Done (2026-10-30): Engine::Hosted cuts every window above the game out of each frame; OverlayWindows
     hosts each in a Vista-framed window owned by the side window (frameless for menus and drop-downs, placed over
     the window they came from), at TPT's scale or the largest whole scale that fits the monitor. Mouse goes back
     with the offset; a click on the sim layer counts as a click outside the window. Close = the dialog's own exit
     (Escape). Titles come from ui::Window::Title (set in each dialog; RTTI is off). Left: typing into a hosted
     dialog's text field is untested (clicking a host gives TPT's window the keyboard); hosts don't resize.
4. **Native replacements, most used first** (M each): screens rebuilt in our own controls, laid out for the
   window size: element search, property tool, sign tool, deco colour picker, local saves and stamps (a real file
   list with thumbnails, drag and drop), options, render options, the Lua console. Rare or online screens (login,
   tags, online browser, profile) stay re-hosted until someone needs them native.
   - [x] Element search (2026-10-30): a search box over the Elements tab's tiles. Clicking it gives the side window
     the keyboard; typing filters every section by name (prefix matches first) and description, Enter picks the
     first, Esc clears; both hand the keyboard back to TPT. Tested with posted keys.
   - [x] Local saves and stamps (2026-10-30): the Saves tab lists Saves\*.cps and the stamps with thumbnails
     (rendered one per tick, folders re-listed every 2 s). Click opens a save / places a stamp; right-click =
     Open/Place, Show in folder, Delete (asks first); Show folder buttons; a .cps or .stm dropped on the window opens
     or places it. Tested: thumbnails, click-to-open. Not tested (needs real input): the right-click menu, delete,
     drag and drop. Stamps show their IDs (TPT's rename is a later add).
   - [x] Deco colour picker (2026-10-30): Windows' colour dialog (Tools tab); the swatch still opens TPT's (alpha).
   - [x] Property and sign tools (2026-10-30): real Windows dialogs (OverlayWindows::Form: a drop-down, a text
     field, OK/Cancel/extra buttons; comctl32 v6 manifest for themed controls). Property: OK greys out until the
     value parses. Sign: Delete and Move (Move hands over to TPT's sign window, which follows the mouse). Tested with
     posted messages: property WATR saved to prefs, new sign created. Modal on TPT's thread: the sim waits while open.
     Not shown: picking a property from a particle (TPT's window still does that).
   - [x] Options, most used (2026-10-30), through TPT's OptionsModel (same prefs): Simulation tab "Heat and air"
     (heat sim, water equalisation, air temperature/pressure, vorticity, convection, temperature scale) and Overlay
     tab "TPT settings" (perfect circle, pressure in saves, smooth scrolling, quit without asking, avatars, deco
     colour space) + "All of TPT's options..." for the rest. Lists = native menus, numbers = a Windows dialog that
     only accepts valid, in-range values. Tested: vorticity 5 refused, 0.5 saved. Not native yet: window/scale,
     FPS/draw limits, data folder, startup request, console key, clipboard, std redirect.
   - [x] Render options (2026-10-30): already all in the Display tab (view modes, effects, air, colour modes).
   - [x] Lua console (2026-10-30): a real window (TPT's ` key, ui.console, Esc/close hides): Consolas log over a
     command line; Enter runs through TPT's CommandInterface (Lua and ! commands, "..." for an unfinished block),
     Up/Down history, errors marked. Tested with posted keys: results, prints, multi-line block, errors, !set,
     history. Prints from scripts outside a command still go to TPT's on-screen log.
   - [x] Real Vista-style menus, drop-downs and dialogs (user request 2026-10-30): today the hosted ones are TPT's
     pixel UI inside a Vista frame. First: Settings, drop-down lists, context menus.
     - [x] Drop-down lists and context menus are real Windows menus (ui::Window::MenuItems; TrackPopupMenu on its
       own thread so the sim keeps running)
     - [x] Dialogs: a Vista skin drawn from TPT's components (2026-10-30). Component::GetKind says what each
       control is; SyncHosts snapshots them (SkinItem) every frame and HostWindow::PaintSkin draws labels, buttons,
       checkboxes, text boxes, drop-downs, sliders, progress bars and scroll bars in Vista colours and Segoe UI.
       Icon buttons, colour swatches, unknown controls and Window::RawAreas (save thumbnails) keep TPT's pixels.
       Render options is a whole-screen view: Window::InPlace draws it on the sim layer instead of hosting it.
       Half-scrolled rows clip and the caret follows the cursor (b4fb719).
5. **Notifications** (S): startup notices, update news and errors to toasts and a list in the main window.
   - [x] Done 2026-10-30: TPT's notifications (update news, server notices) are a toast each and a yellow row at the
     top of the Welcome tab (Open, Dismiss) instead of buttons over the desktop. Errors stay TPT's own dialogs (hosted).
6. **Sim layer position setting** (S-M): on top / under windows / behind the desktop icons.
   - [x] Done 2026-10-30: Overlay tab "Sim layer" (Overlay.Layer). Under windows = bottom of the z-order, pushed back
     down when clicked. Behind icons = child of the desktop (WorkerW, or Progman under SHELLDLL_DefView on 24H2+),
     look-only; the manifest now declares Windows 8+ so a layered window can be a child. Z-order and parent checked
     in tests; the user should eyeball behind-icons once (the test desktop was covered by windows).
7. **Themes** (M each): the frame and controls as a theme interface, then XP, 7, 10/11, Mac, Linux.

Open questions: whether some windows (the element palette, a tool strip) should dock to screen edges like the
taskbar; whether to let tabs tear off into their own windows (the Kingsway feel), which falls out of step 1 cheaply.

## To-do

Done (2026-10-30): overlay window; per-pixel alpha; input modes and global hotkeys; mode notices; Elements window
(Vista) with icons, minimise and close; tray icon and menu; TPT's bars hidden while the Elements window is open;
black particles solid; DPI-aware UI scale. Later the same day: sim size as a build option, a play area that
resizes at run time (saves follow it), the tabbed side window (Elements, Simulation, Display, Saves, Overlay).

Assessment, in the order I'd do it (effort: S = an hour or two, M = a session, L = several sessions):

1. **One large Vista side window with tabs for all of TPT's menus** (M per tab), docked to a screen edge, with
   tabs that can be torn off into their own windows (the Kingsway feel). The Elements window becomes its first
   tab. Then:
   - [x] Tools and brush (2026-10-30, Tools tab): brush shape and size, replace mode / specific delete, decorations
     and the deco colour (Pick colour… = Windows' colour dialog, alpha kept; the swatch opens TPT's picker with alpha)
   - [x] Display: view modes, HUD, decorations, gravity field, link to TPT's render options
   - [x] Simulation: play area, pause/step/reload/clear, edges, gravity, air, ambient heat
   - [x] Saves and stamps: buttons that open TPT's own browsers (a native Vista save list is a later step)
   - [x] Native online save browser (2026-10-31, Saves tab "Online saves"): search box (Enter), Popular / New
     (+ Favourites / Mine when signed in), period, pages of thumbnails with votes and author; click = TPT's
     preview. Tested against powdertoy.co.uk: front page, "rocket" search, click opens the preview.
   - [x] Overlay settings: input mode, TPT's menus, shortcut list
   - [x] Hotkey recording and UI scale in the Overlay tab
   - [x] Tear tabs off into their own windows; dock the window to a screen edge (2026-10-30: right-click a tab =
     its own window, reopened at start (Overlay.Windows.Torn); app windows stick to work-area edges within 16 px.
     Tested: opens, pref saved, close clears it. Dragging a tab out: not done, right-click covers it)
   - [x] Play area: drag its edges or corners on the desktop (2026-10-30; resize cursor, snaps to cells, min 8;
     logic tested with a hook, real-mouse drag for the user to try). Loop wrap: done, see below.
   - [x] Bottom button row hidden with the rest (2026-10-30); its save name, vote and tags have no side-window
     home yet; the Saves tab's Online group has browse, upload, vote, tags and sign in
   - [x] Welcome tab (2026-10-30): TPT's startup text, rewritten as a Vista start page (headers, keycaps, tip
     bar, links) instead of drawn over the game; the side window opens on it
2. [x] **Desktop UI** (L, user request 2026-10-30): see "Desktop UI design" above. Steps: window foundation; sim
   keeps running under dialogs; re-host every TPT dialog; native replacements; notifications; sim layer position;
   themes (= item 6). All done 2026-10-30.
3. [x] **Full-screen sim** (M): pick RES at build time (a meson option sized to the screen at the chosen pixel
   size). The windows already scale on their own, so the old "separate UI scale" item is no longer needed. RES at
   runtime (every static XRES/YRES array) stays a later L job, only if switching screens without a rebuild matters.
   Caveat: saves bigger than 612x384 won't open in stock TPT.
   - [x] **Sim size at runtime** (L): the sim sizes itself to the screen at startup, no rebuild per monitor
     (2026-10-30; tested at 3x = 1688x440 and 2x = 2540x680; a full play area is saved as 0x0 so it follows).
   - [x] **Sim scale slider** (M, needs the item above; 2026-10-30: Overlay tab "Sim pixel size" 2x-6x restarts TPT at
     the new size - restart tested. Scene carry-over as <=255-cell tiles (save format limit): 5859/5859 particles back at 2x): how big one sim pixel is on screen (1x, 2x, 3x...). A
     smaller scale gives a bigger sim with finer pixels; a larger one, a smaller sim with chunkier pixels. The sim
     is rebuilt at the new size and the current scene carried over (clipped if it no longer fits).
   - [x] Loop edges wrap at the play-area edge (2026-10-30, Simulation::LoopWrap; tested: 2107 sand kept, all inside).
4. [x] **Particles collide with open windows** (M, needs 3): the visible windows' rectangles as walls, refreshed a
   few times a second. Then react to the active window (e.g. it shakes off sand when moved). Desktop icons (S-M,
   read from Explorer's icon list view). Files: undecided, needs a concrete idea first.
   Done 2026-10-30: every visible window (DWM frame bounds, not cloaked/tool/transparent) is invisible WL_WALL cells,
   every other frame (moving windows push particles out along the move at the window's speed, 2026-10-30; tested:
   a pile rides up on a rising window, ~2% of the pushed grains lost; the taskbar is a floor), only in empty cells and only those given back; a moved or closed window drops its sand. Simulation
   tab toggle (Overlay.WindowWalls). Desktop icons: done 2026-10-31, read through the desktop's shell view
   (IShellWindows -> IFolderView2 item positions, twice a second; no reading Explorer's memory), added to the same
   pixel mask; Overlay tab toggle (Overlay.IconWalls). 2026-10-31: each icon's real shape (its picture from
   IShellItemImageFactory, opaque pixels; cached, 16 new per pass, squares until loaded), positions re-read every
   ~8 ms on a background thread (dragging and free placement work), walls in the on-top and under-windows layers.
   Pushing never tunnels through another window or icon (that threw sand off the top edge in void mode).
   Behind-icons layer on 24H2+: UpdateLayeredWindow fails (87) on a child of Progman, so it's colour-keyed and
   BitBlt-ed; placed at the monitor corner and kept under SHELLDLL_DefView (SDL's always-on-top cleared).
   That layer takes no clicks, so Locked input there is drawing mode: it comes up as the under-windows layer
   until Auto/Pass-through. Icon rects: GetItemPosition is the icon's own left (no centring in the spacing
   cell); top = position + 6 px, matched against sand resting on real icons.
   Speed: the scale-up runs in parallel bands (std::execution::par). Per frame now ~10 ms of overlay work:
   ~4.5 ms is UpdateLayeredWindow's 32 MB copy (needs a GPU presenter, DirectComposition, to get 120 fps).
   2026-10-31: GPU presenter (4a2bc04) then 120 fps drawing with 60 Hz physics: drawing runs at the display
   rate (DrawLimitDisplay), the frame goes up sim-sized, and particles that moved between the last two sim
   frames (same slot + type, < 8 px) are cut out and drawn as GPU squares sliding from old to new position
   (one tick of latency). Window walls run once per sim tick. The black-particle mask uses the shown frame's
   positions (live pmap is a tick ahead: it painted black squares). No sliding while zoom is on.
   Measured: 117-121 frames/s with ~3000 grains sliding, sim steady at 60.
   Later (user: no added latency): no tick of lag any more. The newest sim frame is shown at once and moving
   grains are carried on at their speed (not into a still grain or a wall); TPT's render thread is off on the
   overlay (it shows the frame from a tick before); one GPU frame queued. Sand on a rising window is lifted with
   it each drawn frame (WindowShifts: windows moved since the frame's walls); the window cut-out uses the frame's
   walls, not the live ones (those erased the pile's bottom rows: the "sinking"). Measured with a rising test
   window: pile 0-1 px above the title bar, as at rest. Frame loops in parallel bands: prep 2.1 -> 0.5 ms.
   Behind-icons layer on the GPU too: the view window is re-parented into the desktop window, just above ours
   (ours goes alpha 0); tested: sand falls behind the icons. Switching layers at run time: untested.
   2026-10-30 later: pixel-exact and every frame (user: "smoother, less blocky"). Windows are no longer WL_WALL
   cells but Simulation::pixelWalls, a per-pixel mask eval_move and brush drawing respect; unlike walls it never
   deletes particles, which fixes the ~2% grain loss. Tested: a pile rides a rising window with a smooth edge,
   2416/2416 grains kept through rise and sideways slide.
5. [x] **Auto pass-through while a game or full-screen app has focus** (S). Done 2026-10-30: SHQueryUserNotificationState
   twice a second (busy / D3D full screen / presentation, foreground not TPT); the input mode itself is kept.
   Overlay tab checkbox, Overlay.AutoPassThrough.
6. [x] **More OS themes** (M each). Done 2026-10-30: `Theme` interface (frame, caption buttons, buttons, popups,
   colours) with Windows 3.1, 95, 98, XP, Vista, 7, 8, 10, 11, Classic Mac (Platinum), macOS and TempleOS. Overlay
   tab -> Look (Overlay.Theme, live switch). Linux skins not done (add when wanted).
   - [x] **Look as two dropdowns** (user request 2026-10-30): a Windows dropdown (every Windows version above) and a
     macOS dropdown, instead of one radio list. Done 2026-10-30 (native popup menus; TempleOS is its own toggle).
   - [x] **The 8 eras of Mac UI** (user request 2026-10-30), each its own entry in the macOS dropdown: System 1-6
     (1-bit), System 7 (colour), Mac OS 8-9 Platinum, Mac OS X 10.0-10.6 (Aqua), 10.7-10.9 (Lion-Mavericks grey),
     10.10-10.15 (Yosemite-Catalina flat), 11-15 (Big Sur-Sequoia), 26 (Liquid Glass). Eras 4-8 as the user
     defined them. The current Classic Mac = Platinum (era 3) and macOS = 11-15 (era 7). Done 2026-10-30.
     macOS 26 is real Liquid Glass, Apple's "regular" variant (HIG Materials): what's behind the window, via DXGI
     desktop duplication (the GPU halves it), blurred, luminosity pulled into a band labels read over, lens and
     specular rim at the edges; light or dark (white labels) by the brightness behind. A thread recomposes it every
     display refresh. The window is kept out of screen captures and recordings while macOS 26 is on (else it would
     see itself). Light or dark is judged from what shows through the glass; per bar (as macOS) not done.
7. [x] Black particles inside the zoom window (S); menus fade when unused (S). Done 2026-10-30: the zoom box is opaque
   (a magnifier); TPT's own menus, when shown, fade out after 3 s without the mouse over them.
8. [-] Other platforms: X11 / Wayland input regions, macOS (L; only if someone needs them). Deferred by the
   user 2026-10-30.

Dropped: a Kingsway-style taskbar and start button. The tray icon plus the side window cover launching. Vista
tray menu: the tray menu stays native on purpose.

## Rough edges (user request 2026-10-30: fix and polish all of them; tick each off as it's fixed)

- [x] Vista skin: rows half-scrolled out of a scroll panel vanish instead of being clipped (now clipped at the panel edge)
- [x] Vista skin: the text caret is always at the end; no selection highlight (caret follows the cursor, selection drawn light blue)
- [x] Typing into a hosted dialog's text field is untested (tested: typing and arrow keys in Save)
- [x] Hosted dialogs don't resize (by design: TPT lays its dialogs out at a fixed size)
- [x] Render options (drawn in place) sits partly under the taskbar (its switches are native in the Display tab now)
- [x] Tools tab sits after Welcome; it belongs next to Elements
- [ ] Shortcut recording is untested with real key presses (tests can't press keys on this PC; needs the user to try)
- [x] Changing the UI scale keeps the side window's pixel size (now scales with it)
- [x] The Saves tab doesn't show the open save's name, votes or tags (now an Open save group)
- [x] Remade windows leave a ghost of the old title (the old window was never destroyed; ~Panel destroys it now)
- [x] Long button labels spilled past the button's left edge

## Desktop physics to-do (user 2026-10-31, in this order)

- [x] (2026-10-31: `Simulation::windowMarks`, tested on a test window) More window marks, on the scorch system (Simulation::windowSoot gets a kind per pixel): frost from ice, LN2 and
  cryo (pale, melts away slowly), acid etching (greenish pits), water streaks (faint, dry out). A slider each.
- [x] Icons carry sand (2026-10-31): an icon only moves when dropped, so its pile jumps with it (no fling: a jump has no
  speed). Icons have ids (hash of the item ID) and moves; riders sit on the top of the shape per column. Window drag
  tests unchanged; not tried on real icons (needs the user to drop one).
- [-] Water clinging to windows: DROPPED (user 2026-10-31: TPT doesn't do it in game, so no new mechanics that
  rewrite TPT's physics).
- [x] Destructible windows and icons (2026-10-31; Simulation tab switch, off by default; acid/blast strength sliders):
  acid eats ragged holes (yellow-green rim); blast shock waves (pressure over 3 AND jumping up) tear round chunks,
  bombs blow small ones, DEST and singularities destroy, 2000 C+ (plasma, thermite, lightning) burns slowly (ember + char rim).
  Broken bits fly off as drawn chunks in the window's colour (Shards / Shard size sliders, toggles for colour and for sim-particle
  glass instead); black holes (NBHL) tear at windows near them; Explosion shake (off by default) jolts the REAL windows a blast hits
  and puts them back.
  Holes show what is really behind the window (the windows under it, PrintWindow 4x a second on a worker thread, over the
  wallpaper); Hole opacity 0 = fully see-through, up = the broken part fades back in. `MarkHole` in
  `Simulation::windowMarks`, taken out of the sim's walls; holes show the wallpaper (IDesktopWallpaper + WIC, per
  monitor, drawn as fill). Overlay only, cleared with the sim. Tested on my test window with acid and real C-4 (PLEX).
- [x] Explosions shake nearby windows (2026-10-31: "Explosion shake" slider, off by default; jolt that dies away, then back home; not maximised; a grab cancels).
- [x] Falling-sand CPU (2026-10-31): timed with ~95k grains raining: TPT's own UpdateParticles ~11-13 ms/tick (the bulk,
  single-threaded, left alone), its renderer ~2.7 ms; the overlay's per-frame work went 6.4 -> 3.3 ms (frame mask pass
  row spans instead of per-pixel rect tests 2.1 -> 0.5; window scan does cheap checks before DWM calls; windows that
  didn't move or resize skip the push scan 0.7 -> 0.2). Lots of sand still fills a core: that is TPT's sim.

## Ideas

- **Menu inspiration: Kingsway** (2017), whose whole UI is a fake retro desktop. Every TPT panel would be its own
  draggable window: element palette, tools, render modes, settings, save browser, stamps. Windows can stack and
  close, a start-button style launcher and taskbar reopen them, and items can be dragged between windows (e.g. an
  element from the palette onto a quick-bar window). Combine with the Vista theme first, then other OS themes.
- Smart menu behaviour: menus fade when unused, then come back on hover or a hotkey.
- [x] Per-app rules (2026-10-31): Overlay tab list "Pass clicks through while these apps are in front"
  (Overlay.PassApps, exe names; Add a running app picks from open windows). Tested: python.exe in front ->
  pass-through notice, closed -> back to normal.

## Related

The Rust/Bevy port (`../src`, `../tools/gen_elements.py` in the local workspace) continues alongside it as the
longer-term engine: TPT's movement core is ported, and all 195 elements' properties are generated from upstream.
