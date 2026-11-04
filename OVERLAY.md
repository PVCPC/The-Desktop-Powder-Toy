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

Desktop physics (OverlayWalls.cpp, shared by every platform): sand on a window rides along when it moves, layer by
layer (the bottom grips, each layer above may slip, more on jolts; solids rigid, liquids slosh, a window going down
pulls nothing down), and is
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
8. [-] Other platforms: X11 / Wayland input regions (L; only if someone needs them). Deferred by the user 2026-10-30.
   macOS: done, see "macOS" below.

Dropped: a Kingsway-style taskbar and start button. The tray icon plus the side window cover launching. Vista
tray menu: the tray menu stays native on purpose.

## Rough edges (user request 2026-10-30: fix and polish all of them; tick each off as it's fixed)

- [x] Vista skin: rows half-scrolled out of a scroll panel vanish instead of being clipped (now clipped at the panel edge)
- [x] Vista skin: the text caret is always at the end; no selection highlight (caret follows the cursor, selection drawn light blue)
- [x] Typing into a hosted dialog's text field is untested (tested: typing and arrow keys in Save)
- [x] Hosted dialogs don't resize (by design: TPT lays its dialogs out at a fixed size)
- [x] Render options (drawn in place) sits partly under the taskbar (its switches are native in the Display tab now)
- [x] Tools tab sits after Welcome; it belongs next to Elements
- [x] Shortcut recording and the hotkeys: CONFIRMED WORKING by the user (said so 4 times by 2026-10-31; never ask again)
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
- [x] Multi-core sim (2026-10-31): plain particles (no update function or ChangeType, not energy, slow) update in 32 px
  blocks, nine passes 64 px apart, per-block RNGs (`SimRNG`), create/kill/change-type locked (`parallelLock`); the
  rest after, one at a time. Air passes split by rows. Sand rain ~190k grains: 30 -> 53 ticks/s; same results as the
  one-core order. Left single-core: RecalcFreeParticles (pmap order).
- [x] Multi-core drawing (2026-10-31): render_parts draws plain particles (cached look, one flat pixel) on all cores
  first, highest index winning a shared pixel (atomic max per pixel); effects follow in order, now always over them.
  409k plain particles: 6.2 -> 1.4 ms, same picture (hash, old and new alternating in one run).

- [x] Every monitor (2026-10-31): sim sized to the virtual screen, overlay at its corner; where no monitor is
  (beside a shorter one) is a wall (gap rects, TickWindowWalls). Untested on a real second monitor.
- [x] First-run sim pixel size: 2 x max(DPI scale, height/1080) (OverlayDefaultScale); TPT's own guess skipped.
- [x] Resizable overlay:window (2026-10-31): the sim is screen-sized there too; the window shows its top-left corner
  and the play area follows the window (blit, SetWindowedView; Overlay.WindowedView.W/H). Tested: 612x384, 494x287
  and 161x120 views keep exactly the stone inside them. TPT's own bars aren't shown in that mode (side windows).
- [x] Fun group (2026-10-31; Simulation tab, Overlay.Fun.*, all off): pets = FIGH with `sim->petWander` (FIGH.cpp
  PetComm: wander, turn at drops, hop steps), topped up every 120 ticks; weather from the top row (snow/rain/sawdust
  leaves, by month or fixed), each removed after a while; Recycle Bin icon kills loose particles on it; music =
  IAudioMeterInformation peak, a jump over the running level makes resting powders hop; broom = 8x8 moving wall at
  the cursor where clicks go to the desktop; window closed/opened = TossAbove on the band over it (TickFun,
  TickWindowWalls). Tested headless: pets walk, snow falls and piles, bin eats 30/30 grains, close throws the
  pile up. Music and broom untested (no simulated input or audio).
- [x] (2026-10-31) BIRD, FISH, BALL, JOIN elements; pets flee heat; rain lightning (v0.7.1). Pets ride moving windows,
  broom sweeps its path, Weather waits (idle minutes) / Weather lasts (seconds), stronger music bounce (de6c0c5).

## Fun to-do (user 2026-10-31: "add them all", in this order)

- [x] Grab and throw balls and jelly with the mouse (2026-10-31: with BALL or JOIN selected, pressing on one grabs it; it
  chases the pointer and flies on with that speed when let go; a JOIN body hangs off the grabbed pixel. Tested with a
  scripted hand: ball, jelly (137/137 px) and rope (61/61) carried and thrown. Real mouse untested.)
- [x] Busy apps run hot (2026-10-31, Fun slider "Busy apps run hot"): what sits on a window warms toward 20 C + 100 C per
  core its app keeps busy (CPU sampled every 30 ticks); tested: snow on a busy window melted in 2 s, on an idle one stayed (covers "snow
  melts on hot windows").
- [x] Pets play with balls (2026-10-31): a stick figure (FIGH, STKM) next to a slow ball kicks it away from itself (tested: 200 -> 281 px).
- [x] Balls sink into powders and liquids (2026-10-31): they push what they move into up out of the way, slowed by it
  and buoyed by the level beside them; tested: floats half sunk in water, settles a third deep in sand, nothing lost.
- [x] Wind (2026-10-31): a gust that turns every 4 s; falling leaves flutter and drift with it (tested: mean -0.6 to +0.35
  px/tick sideways while falling 0.55), snow at 40%, rain not.
- [x] Jelly sinking through a 1 px JOIN rope (2026-10-31): no diagonal squeezing between corner pixels, and the cells
  along each body's springs block other bodies (a stretched rope's gaps); tested: jelly on a sagging bridge stays put for 1500 ticks.
- [x] Pets notice you (2026-10-31): a slow pointer near a pet draws it over and it stops under it, a fast one
  startles it away for 1.5 s; back after a minute away, pets hop to greet you. Tested with a scripted pointer
  (overlay:window): walked 40 -> 98 to a pointer at 90, ran 70 px from a fast one. Greeting untested (needs real idle).
- [x] Holidays (2026-10-31, Fun toggle "Holidays", by the date): Halloween = an orange ball pumpkin every 15 s, thrown
  in sideways so it rolls; 14 Feb = a red jelly (JOIN) heart every 5 s; 1 Jan 00:00-01:00 = lit FIRW fireworks from
  near the bottom. Tested (other dates faked): pumpkins roll ~90 px after landing, hearts stay whole (108 px), fireworks
  burst. Balls: still air now brakes a roll 10x less (wind still pushes as before).
- [x] Taskbar hourglass (2026-10-31, Fun slider "Hourglass", 1-120 min from when it is set): GLAS on the taskbar near its
  right end; grains drop through the plugged neck so the top holds the time left, then it turns over; rebuilt where
  broken or cleared. Tested at 1 min: 88 grains drained evenly (74/14 -> 6/82), turned over to 88/0, nothing lost.
- [x] Notifications (2026-10-31, Fun toggle "Notifications drop a ball"): the tray window is a shell hook window;
  HSHELL_FLASH (an app flashing its taskbar button) drops a small ball onto that window, at most one per window per
  3 s. Tested with my own form flashing twice: balls landed on it and stayed.
- [x] Day and night (2026-10-31, Fun toggle "Day and night", by the clock): 19:00-05:00 up to 12 fireflies (BIRD tmp 1:
  slow loners) blink low over open desktop, and fallen leaves come down darker; 06:00-10:00 a flock of 7 birds now and
  then (about every 2 min) sets off from open desktop. Spawns skip windows (walls: anything inside is stuck). Tested
  with the hour faked: fireflies drift at 0.3 px/tick, about a third lit; flocks fly across and leave.
- [x] Released v0.7.2 (2026-10-31) with all of the above.

## Fun to-do 2 (my own picks, 2026-11-01)

- [x] Sparkle trail (Fun toggle "Sparkle trail"): a moving pointer sheds 2 cold sparks a tick (EMBR at 22 C, life
  20-40, glitter colours). Tested with a scripted pointer: ~50 sparks while moving, none 40 ticks after it stops, a
  sawdust floor under them never caught.
- [x] Pets chase fireflies: a firefly within 80 px (scanned every 20 ticks) draws the pet under it, where it jumps at
  it. Tested: the pet ran 29 -> 150 after one and followed it back.
- [x] Confetti (Fun toggle "Confetti"): 4 side-to-side turns of the pointer within 40 ticks throw 40 coloured
  sawdust flakes (wind flutters them), gone after 5-8 s; at most one burst a second. Tested: a slow sweep threw
  none, a wiggle threw one burst of 38 that floated down in ~2 s and was gone by ~8 s.
- [x] Released v0.7.3 (2026-11-01).

## Fun to-do 3 (user 2026-11-01: "add that new batch", each with sliders for length, times, intensity)

The "More fun" group under Fun; the far left is off. (2026-11-01: both groups and Desktop physics are now the Fun tab, one table funRows[] of rows by topic, each with its description drawn under it.)

- [x] Pets nap: "Pets nap from" / "Pets wake at" (hours); in those hours, after 10 s with no input, pets stand still
  and z's float up from them (drawn on the frame, OverlayWindows::DrawExtras). Tested: pet held still.
- [x] Pets gather: "Pets gather after" (minutes away) -> pets walk to the middle of the play area; "Welcome back hop"
  (0-10 s) sets how long they hop when you return. Tested: pet walked 20 -> 63 (middle 80) and waited.
- [x] Snowmen: "Snowmen" (0-5) / "Snowman needs" (6-40 px of snow): every 5 s, a spot with snow that deep gets three
  stacked ice balls (snow white, 263 K, so they melt when it warms) with coal eyes and buttons and a wood carrot.
  Tested: two built on cold snow (173 ice, 4 coal, 3 wood each).
- [x] Window lift: "Window lift" (0-3): a window whose top moves up 3+ sim px in a tick throws the powder and liquid
  within 80 px above it (vy = move x strength / 2, a little sideways spread). Normal mode only (TickWindowWalls).
  Tested with my own form jerked up 80 px: sand on it flew from y 439 to 212 at strength 2.
- [x] Paper planes: "Paper planes" (every N minutes) / "Plane speed": thrown from the first pet (else a screen edge),
  glides and sinks slowly, lands on anything solid or a window, fades out after 10 s. Drawn, not particles.
- [x] Rainbow after rain: "Rainbow after rain" (seconds) / "Rainbow brightness": rain (Weather) that fell 30 s+ and
  then stops for 2 s shows a 7-band arc sized to the play area, fading in and out, behind everything and not over
  windows. Checked on a frame dump.
- [x] Pets fetch: "Pets fetch" (range px) / "Fetch kick": pets run after a BALL in range (not one already within
  30 px of the pointer); BALL kicks it toward the pointer. Tested: pet ran 75 -> 17, ball kicked 20 -> 126 toward it.
- [x] Taskbar volcano: "Volcano wakes at" (total CPU, GetSystemTimes every 30 ticks) / "Volcano force": a stone
  cone 100 px left of the tray end smokes past the level and throws lava 20% past it; thrown lava (or its stone)
  goes after 8 s. Normal mode only. Tested in a free gap: smoke at idle, lava under 16 busy threads.
- [x] Release v0.7.4 (2026-11-01).
- [x] Rainbow like a real one (user 11-01: "faint and see through", "a little wider", "double rainbow chance"):
  blended colours, soft edges, fading feet, at most 27% strong at default brightness; ph/14 rows wide; "Double rainbow chance" slider (default 25%) adds a wider, fainter arc outside with the colours reversed. v0.7.5.

## Fun to-do 4 (user 2026-11-01: "sliders or toggles? Whichever makes more sense")

On/off things are Fun toggles; things with an amount are More fun sliders.

- [x] Pets build sandcastles (toggle, pref Castles): every 10 s, a pet with a sand pile of 97+ grains within 30 px
  turns 67 of them into a brick castle 18 px to its side, if there's room. The castle crumbles back to sand after
  5 min; at most 3 every 5 min. Tested: 67 bricks built, 67 sand gone.
- [x] Birds land and rest (toggle, pref Perch): a morning bird (Day and night) over something solid now and then
  glides down onto it and sits 5-15 s before flying on (BIRD tmp2: <0 landing, >0 resting). Tested: 3 resting
  with it on, none with it off.
- [x] Shooting stars (slider 0-30 an hour, pref Stars): from 8 pm to 5 am, a white streak with a fading tail
  crosses the open desktop (drawn, not over windows). Checked on a frame dump.
- [x] Puddles last (slider 0-120 s, pref Puddles): rain that lands on something stays that long (+-50%) before it
  dries. Tested: 114 water drops lying around with 60 s against 43 with it off.
- [x] Mouse wind (slider 0-3, pref Wind): a fast mouse swipe (not dragging) blows powder, liquid and gas within
  24 px along with it. Tested: sand pile shifted 3.5 px over 8 s of swipes; none with it off.
- [x] Release v0.7.6 (2026-11-01).
- [x] Shooting stars land (toggle, pref StarsLand; user 11-01): 1 in 3 stars falls steeper and keeps going until it
  hits a window, icon or particle (not in its first 30 px): sparks (EMBR) and a puff of pressure, and with
  Destructible on a chunk out of the window (overlayImpacts, handled in OverlayApplyWalls). Tested: 21 hits in 20 s
  in a fast-stars run, sparks made.
- [x] Windows heal (slider 0-10 min, pref Fun.Heal = overlayFeel.heal): holes grow back in from their edges, one ragged
  ring every heal/20 of the time; a spot with a particle in it waits. Tested on a fake wall: 430 -> 220 hole pixels in
  ~30 s at 1 min; unchanged with it off.
- [x] Birds rest for (slider 2-60 s, pref BirdRest -> sim->birdRest): rest is 0.5x-1.5x of it. Tested: 2 s -> at
  most 177 ticks, 30 s -> up to 1979.
- [x] Release v0.7.7 (2026-11-01).
- [x] Windows crack first (toggle, pref Crack; MarkCrack drawn under holes): with Destructible on, a hit on whole glass
  draws 5-8 jagged cracks; a hit on 3+ crack pixels (cracks from the last 45 ticks count as the same blast) breaks
  it. Cracks fade with healing. Tested on a fake wall: hit 1 -> 84 crack px, 0 hole; hit 2 -> 431 hole px.
- [x] Pets explore holes (toggle, pref PetHoles -> sim->petHoles, Simulation::HoleAt): pets drop through holes 1 in 3
  times at a ledge, and stop 2-5 s to look out of one they stand in. Tested: in the hole 6/16 checks vs 2/16 off.
- [x] Meteor showers (toggle, pref Showers): on the peak nights (+-1 day) of the Quadrantids, Lyrids, Eta Aquariids,
  Perseids, Orionids, Leonids and Geminids, shooting stars come 10x as often (at least 60 an hour).
- [x] Big meteors (slider 0-10 an hour, pref Meteors; 3x on shower nights): a thick glowing streak that crashes:
  OverlayImpact size 3, a crater (R 15: middle killed, rim thrown out), sparks and smoke, a big chunk with
  Destructible. Tested: crater took 480 -> 393 sand.
- [x] Weather kinds Hail (SNOW at -20 C, falling fast, chips windows with Destructible) and Fog (FOG low down, a
  quarter as many). Tested: hail 1959 pellets in 5 s; fog stays in the bottom third (y 446-715 of 720).
- [x] Hole bugs (user 11-01): stacked windows now keep their own holes (OverlayApplyWalls owner pass: a pixel is wall
  if any covering window is whole there; a hit holes the window that shows there, then the one behind); rims and
  marks only where their window shows; the backdrop goes under see-through particles too (gas, glow) instead of
  letting the real window show; windows behind skip their own holes and their invisible resize border (was a black
  outline); holes follow a dragged window every frame drawn (FollowMarks), not just every tick. Tested on two fake
  walls: one hit opened 469 px with nothing behind, 49 with a window behind.
- [x] Memory (user 11-01 "10 GB?"): measured 50 s of landing stars and big meteors on real windows: ours flat at
  ~700 MB, no other process grew. Shots now reuse one bitmap, shoot each window once, twice a second, and let it
  go when no hole is left; mark layers are made only when a window gets that kind.
- [x] Release v0.7.8 (2026-11-01).

## Speed and drags (user 2026-11-01: "how much faster at 1.8 million pixels? and the dragged windows")
- [x] Drawing thread (Overlay.cpp GpuLoop): the main thread hands each frame over and goes back to the sim; the
  thread draws at every vblank, sliding windows that moved since the frame (WindowShifts, up to 16, in the bg shader)
  with their holes, marks and riding sand. Measured with a test window moved in code: ~190 presents/s while moving.
  Sim went from ~20-25 ticks/s to its normal ~62-67 on a light scene (the present no longer blocks it). FollowMarks
  removed (the slide replaces it).
- [x] "Hide window borders at holes" (Overlay.Feel.HoleBorder, default on): hole pixels on a window's edge also
  cover 1-2 px outward where no window is, hiding DWM's dark border line there.
- [x] Heavy scenes (user 11-01 "5x"): benchmark = seeded 2560x720 plain-window scene (water/sand/plant/fire, ~280k
  particles), checksum of every particle after 600 ticks. Compiler: LTO, /arch:AVX2, PGO all within noise (AVX2 also
  changes results) - dropped. Overlay passes 10 -> 6 ms (OverlayBands from 600 rows, marks rows on all cores, the
  drawing thread holds the hand-over lock only to swap). ParallelSafe: 62 elements whose update only reaches 2-3 px
  and no shared state (audited: statics, far loops, links by index, global sim state; not if a script replaced the
  update) join the parallel blocks: one core 12 -> 31-33 ticks/s; lava/ice 10.5 -> 17; acid/wood/fire 10 -> 13 (create/
  kill lock). Results per element within a seed's own spread (fire/wood: two one-core seeds differ 17%, multi vs one
  2%). "Multicore physics" toggle (Simulation.Multicore, Running group); ensureDeterminism forces one core, which
  reproduces the one-core checksum exactly. TPT's renderer thread on the overlay: no gain, left off.
- [x] 0.8.1 (user: "the same results as single core, just more performance"): a fire scene was drawing-bound, not
  sim-bound: render_fire ~100 ms/frame at 2560x720 (each fire cell adds over 12x12 px, one core). Now rows of cells on
  all cores (capped adds give the same pixels in any order; checked 0 differing pixels vs the old loop every frame), fade
  pass still in order. Fire scene one core 10 -> 13, multicore 12.6 -> 34 ticks/s. RecalcFreeParticlesParallel: exact
  (per-row replay of the pmap writes in index order, deaths after in index order; not with ChangeType elements around
  or a recount) - only with multicore physics: on one core it measured 7% slower (cache), so one core keeps the loop.
  Multicore blocks now 25 passes / 128 px apart: a block's particles reach ~53 px (velocity 20 + liquid search 30), so
  two blocks 64 px apart could both move into one gap pixel. Bit-identical multicore physics is impossible (one shared
  RNG stream drawn in index order); exact mode = toggle off.
- [x] 0.8.2: Multicore physics OFF by default (user). 0.8.3: one-core profile (xperf, build-prof with -Ddebug=true):
  heat 15%, element updates 13%, UpdateParticle 10%, movement 9%, Recalc 6%. Inlining is no lever (LTO gave nothing).
  9950X3D: the sim thread on the V-Cache die ran 12.3 -> 14.5 ticks/s (exact checksum), so Windows.cpp SetupCrt pins
  the main thread to the cores with the biggest L3 when L3 sizes differ (no-op elsewhere). Multicore 33 -> 35.6.
- [x] 0.8.4 Scorched wallpaper (toggle in Desktop physics, pref Overlay.Feel.Wallpaper, off): particles >= 300 C
  soot and acid pits on the desktop where no wall is, as windowMarks[0] (id 0 = no window), drawn under particles;
  only while overlayWallpaperSeen (window walls on, or layer behind windows). Scorch/Acid sliders set the rate.
- [x] 0.8.5 Taskbar toys (Fun toggles TaskThrow / TaskPop, off): UI Automation worker polls taskbar buttons every 0.5 s
  (Win11 class *TaskListButton*, Win10 buttons under MSTaskListWClass); pop = a button name not seen before within 4 s of
  HSHELL_WINDOWCREATED; throw = Ctrl + left button down on a button, then the pointer leaves it vertically by 8 px.
  Block = 11x11 STNE, dcolour = most saturated pixel of the button. Pop tested live (own window); throw needs real input.
- [x] 0.8.6 Fun toggles Tilt / Weekday / Typing (off): Tilt takes GRAV_VERTICAL as GRAV_CUSTOM, customGravityX eases
  to +-0.5 by the foreground window's side, gives VERTICAL back when off (tested: 0.32 lean, mode 0 after). Weekday
  overrides the season kind Mon-Fri. Typing = GetLastInputInfo changed with the pointer still; sparks at the
  GetGUIThreadInfo caret or the window top (ColdSpark, shared with the trail). Typing untested live (needs keys).
- [ ] Next for ~5x, all elements kept: (A) per-block particle slots so create/kill needs no lock; (B) RecalcFreeParticles
  on all cores (~5 ms); (C) review the rest one by one (PLNT, VINE, SPRK...); (D) sleeping regions, as a toggle.

## macOS (0.8.6, native arm64)

The Mac runs the same UI source as Windows: the side window and its tabs, every theme, hosted dialogs, toasts, the tray
menu. `OverlayWindows.cpp` is one file for both; `OverlayMacCompat.h` and `OverlayMacWin32.h/.mm` give it the Win32 it is
written against on AppKit (top-level windows with a message procedure, GDI text through CoreText, popup menus, monitors,
the tray as a status item), and `OverlayMac.mm` holds the Mac's own overlay. Only what really differs is behind `#ifdef`.
A check when touching it: `unifdef -D_WIN32 -U__APPLE__` old vs new shows exactly what changed for Windows.

- **Units:** Win32 coordinates here are pixels, points x backing scale, so canvases are sharp on Retina.
- **Frames:** a CAMetalLayer and its own thread (`DrawLoop`), Metal's copy of `GpuLoop`. Measured 107-118 presents/s on a
  120 Hz panel with 60 sim ticks/s. The layer is sRGB-tagged, or TPT's colours oversaturate on P3 screens.
- **Clicks:** it's all in the pixels, as on Windows, with one change: in Auto, bare desktop is always TPT's, not only while
  TPT is in front (user 11-01: having to click a particle to get back to drawing). Walls, app windows and icons are alpha 0,
  so their clicks go to them. Right-click on bare desktop is the secondary tool, and on a window or icon that thing's menu.
  It also keeps macOS's "click wallpaper to reveal desktop" from firing.
- **Icons:** walls take the exact pixels of Finder's desktop window (needs Screen Recording), thumbnails included, re-read
  when an icon moves and every 4 s; without the permission, each file's own icon.
- **Window corners** are round in the walls, 16 pt on macOS 26, 10 before: past the curve is cleared from the finished
  mask unless something else stands there.
- **Signing:** `tools/package-mac.sh` uses a self-signed certificate from `~/.config/desktop-powder-toy/` when there is
  one, so macOS keeps permissions across updates. No Apple account. Not notarised, so Open Anyway on another Mac.
- **Look:** defaults to the Mac's own theme, Liquid Glass with Screen Recording, else 11-15. The Windows looks stay choices.
- **The Dock:** a solid wall in its real rounded shape, with void past its ends: the sim reaches the bottom of the screen,
  behind the Dock. The hourglass and volcano stand on it. Without Accessibility, a flat floor where it starts.
- **Permissions, all optional:** Accessibility = icons as walls, the Dock, the caret, Explosion shake. Screen Recording =
  Liquid Glass's backdrop, what's behind holes, shard colours. System audio (14.2+) = Music bounce.
- **Scene tiles** are 24 cells, not 255, on both systems: GameSave refuses a save larger than the sim, so 255 lost the scene
  going to a smaller sim.

- **Throw from the Dock:** while it's on, an event tap keeps Ctrl-presses on Dock icons from reaching the Dock, or it would
  open the icon's menu and letting go over that would pick an item (needs Accessibility).

Different on the Mac: Form and the console are AppKit, TPT's drop-downs are a small window of their own (`PopupList`,
NSMenu would hold the main thread and the sim), and typing sparks start at the window's top edge over the caret, since
sparks can't start inside a wall.

Not done yet: one screen at a time, not spanning them (a window can't span displays when each has its own Space); Music
bounce hasn't been run with permission granted; not notarised, by choice, so first launch needs Open Anyway, and macOS may
ask for permissions again after an update.

Multicore physics on the M5 Pro, 485k sand and water: 24 -> 50 ticks/s. Build: `tools/package-mac.sh`.

## UI fixes (user 2026-11-03)
- [x] Gradient wallpaper painted over icon names and soft icon edges: it now leaves each icon's whole grid cell (name
      included) to the real desktop, unless TDPT is behind the icons.
- [x] Mouse wheel scrolls every tab, smoothly (a third of the way each frame).
- [x] No taskbar button: the menu window opens from the tray icon only.
- [x] Sim pixel size 1x showed TPT and Buddy only in the top left: the saved play area (in cells) was 2x's, so at 1x
      it covered a quarter. A size change now scales the saved area with it.
- [x] "Clicks go to" Auto stopped catching clicks: it caught them only while TPT was the app in front, and clicks on
      the menu never bring it to the front. A menu click now counts too, until another app comes to the front.
- [ ] 1x is slow (~30 fps, user 2026-11-03): per frame at 5120x1440 sim 7 ms, TPT draw 13.6 ms, overlay present
      14 ms, all per pixel, not per particle (2x: 1.9 / 2.5 / 4.3).
      Multicore drawing toggle (Simulation tab, on): window/icon wall map by rows on all cores, fire glow only round
      the fire (checked: 330 frames of lava and fire, 0 cells differ). 1x empty: draw 12.3 -> 7 ms, present 10 -> 7.7.
      Left: sim 6 ms with nothing in it, the rest of TPT's draw, the 30 MB upload.
- [ ] "All of TPT's Options..." opens TPT's own dialog, far too small: put those settings in a tab of their own.

## Ideas

- **Menu inspiration: Kingsway** (2017), whose whole UI is a fake retro desktop. Every TPT panel would be its own
  draggable window: element palette, tools, render modes, settings, save browser, stamps. Windows can stack and
  close, a start-button style launcher and taskbar reopen them, and items can be dragged between windows (e.g. an
  element from the palette onto a quick-bar window). Combine with the Vista theme first, then other OS themes.
- [x] Smart menu behaviour (done 2026-10-30, see item 7): menus fade when unused, then come back on hover or a hotkey.
- [x] Per-app rules (2026-10-31): Overlay tab list "Pass clicks through while these apps are in front"
  (Overlay.PassApps, exe names; Add a running app picks from open windows). Tested: python.exe in front ->
  pass-through notice, closed -> back to normal.

## Related

The Rust/Bevy port (`../src`, `../tools/gen_elements.py` in the local workspace) continues alongside it as the
longer-term engine: TPT's movement core is ported, and all 195 elements' properties are generated from upstream.
