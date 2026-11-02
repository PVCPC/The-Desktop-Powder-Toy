# Interactive Buddy in TDPT (experimental, branch `buddy`)

Goal: Interactive Buddy lives in the sim. Sand piles on him, water pours off him, air pushes him, heat and acid hurt
him, and he can knock particles around. It's a Fun option, off by default. Each user picks their own SWF file. **No SWF is
ever committed or shipped**, and only our own bridge code goes in the repo.

Source of the runtime: [PVCPC/Interactive-Buddy-Desktop-Rust](https://github.com/PVCPC/Interactive-Buddy-Desktop-Rust),
branch `native` (the game in Ruffle; `reference/HostBridge.as` and `PlatformBridge.as` already show the hook style).

What the game gives us (AS2, `_root`): `bodyParts` = body, head, lArm, rArm, lLeg, rLeg, each with `x y xv yv rad`
(and old positions `ox oy`), `doBodyPhysics`, `setBasicVars` (`onGround`), `explode(x, y, power)`, `setVel`,
`setBuddyXY`, `cam` / `camx camy camz` (the game's camera), `fistDrag` (being dragged), `emotion`, `cash`, `item`.

## 0. Decisions first
- [x] **Where Ruffle runs.** Interactive Buddy Native builds Ruffle for 32-bit Windows so its maths matches Adobe's
      player exactly; TDPT is 64-bit, so the two can't be linked into one program as they are. Options:
      - A. Buddy as a helper process (32-bit, exact), sharing memory with TDPT: frames, body parts, forces. Best for exactness.
      - B. Ruffle built 64-bit into TDPT as a Rust library. Simpler to ship, and the only option on the Mac (arm64),
        but the game's physics may drift slightly from Adobe's.
      **Tested 2026-11-02: B.** The 160-tick Adobe replay on 64-bit Ruffle: 5044 of 5600 values identical, the other 556
      off in the 12th-15th digit (largest 1.0e-12 px, first at tick 79, not growing); end positions the same to the
      thousandth of a pixel. Far below anything visible, and once the sim pushes him the sim's forces dominate anyway.
      So Ruffle is built 64-bit into TDPT, the same way on Windows and the Mac.
- [ ] Shipping size and build: Rust toolchain in the Windows and Mac builds; measure the added download size.
      Windows so far: meson builds `buddy/` with cargo (build.py fetches Ruffle at a pinned commit and applies
      `ruffle-host.patch`); powder.exe is 25.7 MB, the Windows zip about 10.5 MB (v0.9.0: 3.6 MB).

## 1. He's there (no interaction yet)
- [x] Fun tab row "Interactive Buddy" plus a "Choose SWF..." button; the path is saved in the prefs, and a missing or wrong file says so.
      (Windows; the Mac still needs its file picker.)
- [x] Patch the user's SWF on load inside TDPT (today `tools/prepare_native_swf.py` needs Java + JPEXS): ship only our
      compiled bridge bytecode and splice it into their file in memory; refuse files that don't look like Interactive Buddy.
      Done: `buddy/bridge/*.patch` hold only our inserted tags; v1.01 is checked by its hash before and after.
- [x] Run the game, and draw its frame in TDPT's overlay layer (same transparency rules as the sim). He runs on his
      own thread (about 1 ms a frame left on TDPT's).
      120 frames a second with smoothing on (60 off): only the drawn areas are read back from the GPU, a frame
      late so it never waits (about 6 ms a frame at 5120x1440, was 13.5).
      overlay:window draws him in the window too (its bottom is his floor; no taskbar floor or window ledges there).
- [x] Clicks: on the buddy or his toys they go to the game (the game's own hit shapes); elsewhere they go to TPT as now.
      Game menus: right-click him (Windows). The game's Fist drags him (rest it on him, then press); its Open Hand
      doesn't drag, he reaches for the cursor instead. Tested in `cargo test` (drag_him); the TDPT click path needs
      a real mouse to try.
- [x] Saves: the game's save (money, purchases) is a file in TPT's data folder, `buddy/`, written when he's turned
      off or TDPT closes.
- [ ] Sound (none yet).
- [x] Buddy tab (shown once his SWF is chosen in Fun): on/off + Choose SWF, size 25-300 % (the game's own
      __nativeSetBuddySize, so independent of TDPT's pixel size), active-window ledges, smoothing, speech bubbles and
      their size, toys at once (0 no limit), pets, wet/frosty. Prefs Overlay.Buddy.*, sent with tdpt_buddy_settings.
- [x] One coordinate mapping: game stage <-> sim pixels (TDPT pixel size). The game camera never zooms (camz stays 1);
      it only shakes up to ~9 px for a moment on explosions, left as the game has it.

## 2. Sim -> Buddy (he feels the sim)
- [x] **Solid ground:** solids and powders under/around each body part stop it (like PlatformBridge, but against the
      sim's pmap, every part, every direction). He stands on sand piles, walls, windows and icons.
      Done: Ruffle wraps the game's `doBodyPhysics`/`setBasicVars` with a native hook (`hook_after_root_function`);
      each part is pushed out of solid pixels and bounces like the game's floor, legs on solid set `onGround`. TDPT
      fills the ground map around him each frame (`tdpt_buddy_area`/`tdpt_buddy_ground`). Not while the fist drags him.
- [x] **Buoyancy and drag:** parts in liquid float and slow down by density; heavy liquids (mercury) float him higher.
      Done: liquids go in the ground map as 2 + TPT weight; a part is lifted by its wet share x weight/30 x 1.2 x the
      game's `grav` and slowed by it. Also: NaN parts (game divide by 0, or a save that kept one) are reset.
- [x] **Air:** the sim's air velocity (vx/vy) and pressure gradient push each part; pressure blasts throw him.
      Done: each part gets 0.1 x the air velocity of its cell per frame (sand gets 0.7). Pressure itself is left out:
      a blast's push already shows up in the air velocity.
- [x] **Explosions:** TPT explosions near him call the game's own `explode()` at that spot, scaled.
      Done: a pressure jump of more than 6 in a frame within his reach calls `explode(x, y, jump / 60)` (max 2; 1 = the
      game's own bomb) where it jumped most, at most one per 10 frames.
- [x] **Heat:** parts touching hot particles take damage and play the game's hurt/emotion reactions; above ~1000 C he
      catches fire (game effect if there is one, or our own flames on the body parts).
      Done: ground map temperature bits 0x20 (60 C+), 0x40 (400 C+). 400 C+ sets `body.onFire`; the game's doBodyPhysics
      spreads it to head and legs with its own addEmotion/addCash. Heat lowers `emotion` 0.03 per touching part per
      frame. Cool liquid on him puts the fire out (onFire and oldOnFire false, so the game doesn't react to it).
- [x] **Cold:** cryo/LN2 slow him, frost tint.
      Done: 0x60 (-50 C or colder) slows a touching part (speed x 0.85 per frame) and saddens him like heat.
      Frost tint not done (the frame has no per-part colour; would need the body clips' colour transforms).
- [x] **Acid and caustics:** acid on a part hurts him over time, and eats the acid like it eats other things.
      Done (hurting): ACID is kind 31 in the ground map, a liquid of weight 10 that saddens him twice as fast as heat.
      Eating the acid up belongs to section 3.
- [x] **Electricity:** SPRK touching him shocks him (twitch impulse plus a hurt reaction).
      Done: 0x80 (a SPRK pixel) calls the game's own `shock(x, y)` at the touching part (twitch, cash, sad). It also
      lights game toys near the mouse, as the game's zapper does.
- [x] **Gravity:** follows TPT's gravity mode (vertical, off, radial, custom) and Newtonian gravity.
      Done: TDPT sends `GetGravityField` at the middle of his area (Newtonian x10); after setBasicVars the game's `grav`
      is scaled by its downward part and the sideways part is added to each part's speed.
- [x] **Portals / void / black holes:** PRTI/PRTO carry him, VOID and black holes pull him in (and respawn).
      Done: PRTI is kind 29 (TDPT finds the PRTO on its channel and sends it), VOID/BHOL kind 30 (he comes back at
      the top middle via the game's setBuddyXY). NBHL's pull comes through the Newtonian gravity.
- [x] **Other elements:** caustic gas, virus, neutrons and protons hurt him like acid; electrons shock him like sparks;
      light (PHOT) warms him; DEST, BOMB and singularities blow up on him (the game's explode, plus a TPT blast).
      Ground cells are 16 bits now (0x100 hurts, 0x200 blows up). VOID and portals no longer count as liquids.

- [x] **Desktop floor and windows like the desktop app:** his stage ends 10 px into the taskbar (feet on it, as the
      app's did); he stands on the active window's top edge and, inside it, its bottom edge, and rides it when it moves
      (PlatformBridge.as's rules); every window's and icon's sides stop him like they stop powder.

## 3. Buddy -> Sim (the sim feels him)
- [x] His body parts are moving solid shapes in the sim (like windows are): sand piles on his head, falls off his shoulders.
      Done: each part is a round moving wall in front of the windows (OverlayBuddy::Walls), also when window and
      icon walls are off. Checked in TDPT: up to 100 grains rest on him, none inside, they slide off as he walks.
- [x] Moving parts shove particles (his speed into theirs), so kicking or throwing him splashes water and scatters sand.
      Done: the walls carry his speed, so the windows' own shove moves what he moves onto (with that speed).
- [x] He displaces air as he moves (a small push into the air grid).
      Done: 0.1 of each part's speed goes into the air cell it is in, every sim tick.
- [x] His temperature: he warms what he touches a little (body heat), so snow melts on him slowly.
      Done: anything within 2 px of a part moves 0.2% a tick toward 37 C.
- [x] **Reactions on his body:** acid is used up on him; a spark touching him runs through him into any conductor he
      touches; while he burns, fire spreads from him into the sim. Checked in TDPT: 15 acid drops used up, a dropped
      BOMB threw him across the screen, METL on his far side sparked the tick after its partner was sparked.
- [x] The game's own projectiles and items (grenades, bombs) become sim events: their explosions make a TPT blast too.
      Done: a hook on the game's explode() sends its spot to TDPT (tdpt_buddy_booms): pressure 40 x power over
      4 x power cells and a ball of fire. TPT's own blasts that call explode() are not sent back (no loop).

## 4. Fun extras (each a toggle, off by default)
- [x] Pets react to him (wave, run away when he explodes).
      Done (Fun "Pets meet Buddy"): they walk over and hop hello when he is calm (then leave him 15 s), and run
      when he flies past (speed > 8) or a game explosion goes off (2 s). Pets already run from his fire.
- [ ] He sits in TDPT puddles, slides down sand slopes, gets buried and digs out.
- [x] Weather hits him: snow piles on his head, rain makes him wet (darker tint).
      Done (Fun "Buddy gets wet and frosty"): cool liquid darkens a part for 10 s, -50 C or colder frosts it pale blue; fire dries him.
- [ ] Wallpaper and window marks from his fire and acid, like the rest of the sim.

## 5. Checks before merging into main
- [x] Off = TDPT exactly as before (the one-core bench and acid checksums unchanged; checked 2026-11-02).
- [ ] Game replay checks still match with the bridge loaded (no interaction while the sim is empty).
- [ ] No SWF in git, release zips or caches; the user's file is only read, never copied.
- [ ] Windows first, then the Mac (native arm64 Ruffle).
