# Desktop overlay mode (experimental)

The to-do list and ideas for the overlay live here. Keep it current.

`powder.exe overlay:on` runs TPT as a borderless, always-on-top window over the desktop. Empty (black) pixels
are see-through. TPT presents its own frame with per-pixel alpha (`UpdateLayeredWindow`, `src/Overlay.cpp`), so
clicks on see-through pixels reach the desktop and windows behind. Windows only for now.

## Input modes

| Mode | Empty space | Particles and menus | Hotkey (default) |
|---|---|---|---|
| Auto | clicks go to the desktop | clicks go to TPT | Ctrl+Alt+O cycles modes |
| Pass-through | desktop | desktop (TPT takes no input) | Ctrl+Alt+P toggles |
| Locked | TPT (alpha 1: invisible but hit-testable) | TPT | Ctrl+Alt+L toggles |

The hotkeys are global (polled each frame, so they work while TPT is unfocused) and customisable in
`%APPDATA%\The Powder Toy\powder.pref`: `Overlay.Keys.CycleInput`, `Overlay.Keys.PassThrough` and
`Overlay.Keys.Locked`, as text like `"Ctrl+Alt+O"` (Ctrl, Alt, Shift, Win plus a letter, digit or F1-F24). The
current mode is saved as `Overlay.Input`. A fading glass notice shows the new mode.

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
   - [ ] Real Vista-style menus, drop-downs and dialogs (user request 2026-10-30): today the hosted ones are TPT's
     pixel UI inside a Vista frame. First: Settings, drop-down lists, context menus.
     - [x] Drop-down lists and context menus are real Windows menus (ui::Window::MenuItems; TrackPopupMenu on its
       own thread so the sim keeps running)
     - [ ] Dialogs: a Vista skin drawn from TPT's components (labels, buttons, checkboxes, text boxes)
5. **Notifications** (S): startup notices, update news and errors to toasts and a list in the main window.
6. **Sim layer position setting** (S-M): on top / under windows / behind the desktop icons.
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
   - [ ] Tools and brush: brush shape and size, replace mode, deco colour picker
   - [x] Display: view modes, HUD, decorations, gravity field, link to TPT's render options
   - [x] Simulation: play area, pause/step/reload/clear, edges, gravity, air, ambient heat
   - [x] Saves and stamps: buttons that open TPT's own browsers (a native Vista save list is a later step)
   - [x] Overlay settings: input mode, TPT's menus, shortcut list
   - [ ] Hotkey recording and UI scale in the Overlay tab (still powder.pref)
   - [ ] Tear tabs off into their own windows; dock the window to a screen edge
   - [ ] Play area: drag its edges on the desktop instead of +/- buttons; loop edges wrap at the area edge
   - [x] Bottom button row hidden with the rest (2026-10-30); its save name, vote and tags have no side-window
     home yet; the Saves tab's Online group has browse, upload, vote, tags and sign in
   - [x] Welcome tab (2026-10-30): TPT's startup text, rewritten as a Vista start page (headers, keycaps, tip
     bar, links) instead of drawn over the game; the side window opens on it
2. [ ] **Desktop UI** (L, user request 2026-10-30): see "Desktop UI design" above. Steps: window foundation; sim
   keeps running under dialogs; re-host every TPT dialog; native replacements; notifications; sim layer position;
   themes (= item 6).
3. [x] **Full-screen sim** (M): pick RES at build time (a meson option sized to the screen at the chosen pixel
   size). The windows already scale on their own, so the old "separate UI scale" item is no longer needed. RES at
   runtime (every static XRES/YRES array) stays a later L job, only if switching screens without a rebuild matters.
   Caveat: saves bigger than 612x384 won't open in stock TPT.
   - [ ] **Sim size at runtime** (L, in progress on branch `runtime-size`): the sim sizes itself to the screen at
     startup, no rebuild per monitor.
   - [ ] **Sim scale slider** (M, needs the item above): how big one sim pixel is on screen (1x, 2x, 3x...). A
     smaller scale gives a bigger sim with finer pixels; a larger one, a smaller sim with chunkier pixels. The sim
     is rebuilt at the new size and the current scene carried over (clipped if it no longer fits).
   - [ ] Loop edges wrap at the play-area edge.
4. [ ] **Particles collide with open windows** (M, needs 3): the visible windows' rectangles as walls, refreshed a
   few times a second. Then react to the active window (e.g. it shakes off sand when moved). Desktop icons (S-M,
   read from Explorer's icon list view). Files: undecided, needs a concrete idea first.
5. [ ] **Auto pass-through while a game or full-screen app has focus** (S).
6. [ ] **More OS themes** (M each): XP, 7, 10/11, classic Mac OS, macOS, Linux. Start by turning the `vista::`
   drawing into a theme interface when the second theme starts. Theme choice in Overlay settings.
7. [ ] Black particles inside the zoom window (S); menus fade when unused (S).
8. [ ] Other platforms: X11 / Wayland input regions, macOS (L; only if someone needs them).

Dropped: a Kingsway-style taskbar and start button. The tray icon plus the side window cover launching. Vista
tray menu: the tray menu stays native on purpose.

## Ideas

- **Menu inspiration: Kingsway** (2017), whose whole UI is a fake retro desktop. Every TPT panel would be its own
  draggable window: element palette, tools, render modes, settings, save browser, stamps. Windows can stack and
  close, a start-button style launcher and taskbar reopen them, and items can be dragged between windows (e.g. an
  element from the palette onto a quick-bar window). Combine with the Vista theme first, then other OS themes.
- Smart menu behaviour: menus fade when unused, then come back on hover or a hotkey.
- Per-app rules: pass-through automatically while a game or full-screen app is focused.

## Related

The Rust/Bevy port (`../src`, `../tools/gen_elements.py` in the local workspace) continues alongside it as the
longer-term engine: TPT's movement core is ported, and all 195 elements' properties are generated from upstream.
