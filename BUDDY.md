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
- [ ] **Where Ruffle runs.** Interactive Buddy Native builds Ruffle for 32-bit Windows so its maths matches Adobe's
      player exactly; TDPT is 64-bit, so the two can't be linked into one program as they are. Options:
      - A. Buddy as a helper process (32-bit, exact), sharing memory with TDPT: frames, body parts, forces. Best for exactness.
      - B. Ruffle built 64-bit into TDPT as a Rust library. Simpler to ship, and the only option on the Mac (arm64),
        but the game's physics may drift slightly from Adobe's.
      Pick one after a quick test: run the replay checks (`physics-test-*.swf`) on a 64-bit Ruffle and count differences.
- [ ] Shipping size and build: Rust toolchain in the Windows and Mac builds; measure the added download size.

## 1. He's there (no interaction yet)
- [ ] Fun tab row "Interactive Buddy" plus a "Choose SWF..." button; the path is saved in the prefs, and a missing or wrong file says so.
- [ ] Patch the user's SWF on load inside TDPT (today `tools/prepare_native_swf.py` needs Java + JPEXS): ship only our
      compiled bridge bytecode and splice it into their file in memory; refuse files that don't look like Interactive Buddy.
- [ ] Run the game, and draw its frame in TDPT's overlay layer (same transparency rules as the sim).
- [ ] Clicks: on the buddy or his toys they go to the game; elsewhere they go to TPT as now. Game menus are reachable.
- [ ] One coordinate mapping: game stage <-> sim pixels (accounting for the game camera `camx/camy/camz` and the TDPT pixel size).

## 2. Sim -> Buddy (he feels the sim)
- [ ] **Solid ground:** solids and powders under/around each body part stop it (like PlatformBridge, but against the
      sim's pmap, every part, every direction). He stands on sand piles, walls, windows and icons.
- [ ] **Buoyancy and drag:** parts in liquid float and slow down by density; heavy liquids (mercury) float him higher.
- [ ] **Air:** the sim's air velocity (vx/vy) and pressure gradient push each part; pressure blasts throw him.
- [ ] **Explosions:** TPT explosions near him call the game's own `explode()` at that spot, scaled.
- [ ] **Heat:** parts touching hot particles take damage and play the game's hurt/emotion reactions; above ~1000 C he
      catches fire (game effect if there is one, or our own flames on the body parts).
- [ ] **Cold:** cryo/LN2 slow him, frost tint.
- [ ] **Acid and caustics:** acid on a part hurts him over time, and eats the acid like it eats other things.
- [ ] **Electricity:** SPRK touching him shocks him (twitch impulse plus a hurt reaction).
- [ ] **Gravity:** follows TPT's gravity mode (vertical, off, radial, custom) and Newtonian gravity.
- [ ] **Portals / void / black holes:** PRTI/PRTO carry him, VOID and black holes pull him in (and respawn).

## 3. Buddy -> Sim (the sim feels him)
- [ ] His body parts are moving solid shapes in the sim (like windows are): sand piles on his head, falls off his shoulders.
- [ ] Moving parts shove particles (his speed into theirs), so kicking or throwing him splashes water and scatters sand.
- [ ] He displaces air as he moves (a small push into the air grid).
- [ ] His temperature: he warms what he touches a little (body heat), so snow melts on him slowly.
- [ ] The game's own projectiles and items (grenades, bombs) become sim events: their explosions make a TPT blast too.

## 4. Fun extras (each a toggle, off by default)
- [ ] Pets react to him (wave, run away when he explodes).
- [ ] He sits in TDPT puddles, slides down sand slopes, gets buried and digs out.
- [ ] Weather hits him: snow piles on his head, rain makes him wet (darker tint).
- [ ] Wallpaper and window marks from his fire and acid, like the rest of the sim.

## 5. Checks before merging into main
- [ ] Off = TDPT exactly as before (the one-core bench and acid checksums unchanged).
- [ ] Game replay checks still match with the bridge loaded (no interaction while the sim is empty).
- [ ] No SWF in git, release zips or caches; the user's file is only read, never copied.
- [ ] Windows first, then the Mac (native arm64 Ruffle).
