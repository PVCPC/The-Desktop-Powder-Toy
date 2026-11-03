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
      Done: `buddy/bridge/*.patch` hold only our inserted tags; v1.01 and v1.02 (2026-11-03: the pistol and machine gun) are
      checked by their hash before and after. The v1.02 patches are v1.01's tags put at the same places (outline shape
      583 at depth 0x99 where v1.01 has 580 at 0x88); all 27 tests pass on both.
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
- [ ] Sound (none yet): the game's own sounds through TDPT's audio (Ruffle's audio backend), with a volume / mute
      row in the Buddy tab, off when TDPT is muted.
- [x] Saves also every 2 minutes (checked: TDPT killed after 2 min 17 s, his save was there).
- [ ] Clicks in overlay:window: dragging him, the Fist and right-click menus are untested there (window-relative
      mouse while TPT has it captured).
- [x] Buddy tab next to Fun.
- [x] Shown at the screen's rate: TDPT's overlay shows ~30 frames a second at 5120x1440 (sim 7 ms + TPT drawing
      13 ms + present 10 ms), so he has his own click-through layered window over it (Windows), updated from a
      thread of its own each screen refresh, only where his frame changed (tdpt_buddy_boxes; ~2-3 ms a frame); the
      overlay catches clicks on his pixels (alpha 1). The window he stands on is read there too, every refresh.
      Not behind the icons or with the wet/frosty looks: those still draw him into the overlay's frame.
      He makes 120 frames a second in TDPT, even on a 5120x1440 stage (~6 ms each; frames on a fixed beat).
- [x] The game's zapper: shock() lit toys near the mouse; the game's mouse is now at the spark for that call.
- [x] Buddy tab (shown once his SWF is chosen in Fun): on/off + Choose SWF, size 25-300 % (the game's own
      __nativeSetBuddySize, so independent of TDPT's pixel size), active-window ledges, smoothing, speech bubbles and
      their size, toys at once (0 no limit), pets, wet/frosty. Prefs Overlay.Buddy.*, sent with tdpt_buddy_settings.
      Windows: walk through / stand on the active window (moves with it every frame, as the app) / solid like sand.
      Game: File/Skins/Items/Modes/Settings/Help buttons (the game's menus, buy stores inside), Import from the
      desktop app (its daBud.sol, ours kept as .bak). Fun and Buddy tab sections fold (click the heading).
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
- [x] He sits in TDPT puddles, slides down sand slopes, gets buried and digs out.
      Done: he floats in puddles (section 2) and slides down slopes (27-56 degrees tested). Powders are 0x800 in the
      ground map: with his head under powder at least 3 head-radii deep for 40 frames he digs up through it (about
      60 px a second) until no powder touches his upper half; the sim shoves the sand out of his way. Not through
      solids. Checked in TDPT: buried under a 320x340 px block of sand, out on top of the pile in about 10 s.
- [x] Weather hits him: snow piles on his head, rain makes him wet (darker tint).
      Done (Fun "Buddy gets wet and frosty"): cool liquid darkens a part for 10 s, -50 C or colder frosts it pale blue; fire dries him.
- [x] Wallpaper and window marks from his fire and acid, like the rest of the sim.
      Already so: his fire is real sim FIRE (OverlayBuddy::Step), which scorches windows and the wallpaper like any
      fire; he has no acid of his own.

## 4c. More interactivity (ideas agreed 2026-11-03; first: footprints, splash, fireman)
Each one is a toggle with a description in the Fun tab (Buddy section), off by default, like the 4. extras.
Rule (user, 2026-11-03): never re-make what the original game already does; check the game first (probe its
variables in a cargo test, or render its frames). Only add links between the game and the sim.
He feels the sim:
- [x] Electricity lights him up: he glows while a spark runs through him, and passes it on to what he holds.
      Done (Overlay.Fun.BuddyGlow): SPRK, ELEC or LIGH touching him: 0.5 s blue-white flicker; carried metal powder
      already sparks (it touches his hand).
- [x] Big blasts daze him: knocked out a few seconds, stars over his head, then he gets up.
      Already in the game: a blast lays him flat ~7 s with its own stars round his head (starNum), then he gets up.
- [x] He gets dirty: sand, coal dust and soot stick to him as a tint (like wet and frost); he shakes it off later.
      Done (Fun toggle "Buddy gets dirty"): COAL/BCOL/DUST/GUNP/CLST/OIL/BRMT/SMKE touching a part: +1 a grain a tick
      (0-3600), -1 a tick, water -3 a grain; dark brown smudges in fixed spots, more the dirtier. Tested by counters.
- [x] He runs from fire and lava (he doesn't now: only the pets do): away from them, behind a window or icon.
      Done (Fun toggle "Buddy runs from fire and lava"): fire/lava/plasma within 100 px -> tdpt_buddy_command
      "act:runToPoint:X" (the game's addAction(action, 5)), 250 px away from most of it; every 2 s at most; not while he
      burns (the game's own panic). Not behind windows or icons: he runs along the floor.
      The game has its own running away (runFromGrenade, scared, runToPoint): trigger that from sim fire and lava.
- [ ] A spinning gravity well makes him orbit it and get dizzy. The game has its own pull (gravitateToPoint,
      gravitating): link sim gravity wells to it; no new dizzy effect.
He changes the sim:
- [x] Footprints: walking through powder leaves dents; wet feet leave drops on dry ground. (Overlay.Fun.BuddyTracks)
- [x] Splash: flung into sand or water, it sprays out around where he lands. (Overlay.Fun.BuddySplash)
- [x] He picks up and throws particles: an arm scoops a handful (sand, water...), it rides with his hand, and his
      swing or a fling throws it as real particles.
      Done (Overlay.Fun.BuddyCarry): up to 40 from his right hand, thrown over 6 px a frame, else dropped after 8 s.
- [~] He builds when bored: dropped, the game has its own idle and bored actions (idleAction, bored).
- [x] He feeds his pets: drops plant or seed particles for them to eat.
      Done (Fun toggle "Buddy feeds the pets"): calm, with a pet within 150 px, about every 10 s he drops 4 SEED from
      his hand; pets run to the nearest seed within 60 px and eat it with a hop (any seeds, while on).
Games:
- [x] His weapons hit the sim: bullets (pistol, machine gun in v1.02, and the rest) knock particles about and break
      what they hit; the flamethrower lights what burns (wood, plant, gunpowder, oil...); sand doesn't burn in TPT
      but melts to glass if hot enough.
      Done (Fun toggle "Buddy's weapons hit the sim"): tdpt_buddy_shots = each bullet's path per frame (from where it
      was, or the gun) and each flame (particles.fireClip*/flame*). Gas is pushed along; powder/liquid: the bullet stops,
      a spray back out and up; a solid: +2 pressure (glass breaks) and embers. Flames make TPT fire. Test: weapon_shots.
      Not done: bullets stopped by sim walls in the game itself (they fly on through).- [~] Catch: dropped, the game already has him catch balls (catch, caughtBalls).
- [ ] His items as real TPT stuff (user, 2026-11-03): a setting per kind, Game / TPT / Both (Game = as now). Probed by
      using each item in the game (particles clips, objectList, root vars):
      water: Wide Nozzle Hose, Narrow Nozzle Hose, Fire Hose (a strong water hose, not fire): particles.splash* drops -> WATR;
      fire: Flamethrower (done as Both), Fireballs, Molotov Cocktails -> FIRE;
      gravity: Weak/Strong Gravity Vortex (gravitating, gravitatingx/y) -> a TPT gravity point; Gravity Shifter (grav) -> TPT gravity;
      Stun Gun -> sparks into what it touches; Shotgun pellets: check they are bullets too;
      objects (objectList): Baseballs, Rubber Balls, Bowling Balls, Infants, Magical Orb, Radio, Flail push sand and water
      and float or sink in it, like Buddy's own parts. Explosions (Grenades, Mines, Missiles, Explode At Mouse): already TPT blasts.
      User said yes to all (2026-11-03). Plus: the Radio playing makes the pets dance too (Buddy dances to it in the game).
      Radio dance DONE (Fun toggle "Pets dance to Buddy's radio"): a dance in his action queue (any priority) -> shots kind 3
      -> sim->petDance; pets step left, hop, step right, hop round where they stood (tested: stay within ~12 px).
      Hoses DONE (Fun "Buddy's hoses spray TPT water" = Both, + "Only TPT water from his hoses" = TPT): drops are
      particles.smoke* with strength (splash* where they hit); each new one -> shots kind 4 -> 3 WATR at 0.6 its speed;
      hide:1 sets their _visible false (he still feels the spray). Game = both off.
      Fire DONE: fireballs and molotovs make the same particles.flame* as the flamethrower, so "Buddy's weapons hit the sim"
      already made them TPT fire (Both); now also a molotov's burning debris (debris* onFire). "Only TPT fire from his
      items" = hide:2 (flame*, fireClip*, fireGlowClip* not drawn).
      Gravity DONE (Fun "Buddy's gravity items pull the sim"): gravitating + gravitatingx/y, _root.item names which
      (weak 1, shifter 2, strong 3) -> shots kind 5 -> powders/liquids/gases within 250 px get 0.3*k*(1.5-d/250) a tick
      toward it (a strong one lifts most of a sand pile). The shifter's grav 0 is NOT copied to TPT's global gravity.
      Shotgun: checked, its pellets are bullets too (17 paths a shot), already hit the sim.
      Stun gun DONE (in "Buddy's weapons hit the sim"): beingShocked -> shots kind 6 -> next tick the spark-through-him
      path sparks conductors he touches (tested: a METL plate under him lights up).
      Toys DONE (Fun "Buddy's toys feel the sim", toys: command): objectList circles (not group buddy), read through
      objectList.N.targ.* (x y rad xv yv mass); same float (mass < 0.6) / wind / push_out as his parts; each toy gets its own
      patch of sim + air anywhere on the stage (tdpt_buddy_toys/tdpt_buddy_toy); stopped fast -> shots kind 7 -> his splash fling. Test: a bowling ball rests on a
      block (falls through with it off). Flail (wreckingBall, immovable) left out. The items list is DONE.
- [x] Fireman: when fire starts he grabs water and puts it out, happy if he saves a window. (Overlay.Fun.BuddyFireman:
      sprays water from his hand 3 s at fire 40-250 px away, rests 10 s; no "happy" yet, needs the game's emotes)

## 4b. Elements he doesn't react to yet
- [x] Clone (CLNE, PCLN, BCLN, ACLN...): touching him copies nothing; decide (it can't clone him, so: solid, as now?).
      Decided: solid, as now.
- [x] Freezing solid: water that turns to ice around him should hold him (he's encased until it melts or he's
      knocked out), not only slow him.
      Done: ICEI is 0x1000 in the ground map; a part with ice on 70% of a ring just outside it stays put
      (Ground::encased, test encased_in_ice) until it melts or he's dragged.
- [x] Room-temperature gases (GAS, CO2, OXYG, HYGN, smoke...): they pass through him; gases should drift around him
      and push him a little like air, flammable ones catch from his fire.
      Already so: his parts are pixel walls to every particle (gases go round him), he feels the air that moves them,
      and his fire is real FIRE, which lights GAS and HYGN.
- [x] Go through TPT's whole element list once and note each one's effect on him (or none), so nothing is missed.
      Done: BUDDY-ELEMENTS.md (every element, by his ground map's rules). Gap found and fixed: lightning (LIGH)
      now shocks him like a spark.

## 5. Checks before merging into main
- [x] Off = TDPT exactly as before (the one-core bench and acid checksums unchanged; checked 2026-11-02).
- [ ] Game replay checks still match with the bridge loaded (no interaction while the sim is empty).
- [ ] No SWF in git, release zips or caches; the user's file is only read, never copied.
- [ ] Windows first, then the Mac (native arm64 Ruffle).
- [ ] Off = TDPT exactly as before again after everything above (one-core bench and acid checksums).
- [ ] Shipping size (section 0) measured and written in the release notes.
- [ ] Welcome tab / README: what Buddy is, that each user picks their own copy of the game's SWF (none shipped).
- [ ] Clean up: delete buddy/examples/zz_dump.rs (never committed) and the bytecode dumps used to write the bridge.

## 6. Mac
- [ ] Build buddy/ (Rust, Ruffle arm64) in the Mac build on the mini, and check the download size.
- [ ] Choose SWF: the Mac file picker.
- [ ] Right-click menus with submenus (the game's Items menu) on the Mac.
- [ ] Fonts for the game's text (the Windows build finds them from the system).
- [ ] OverlayWindows::WorkBottom (the Dock / bottom of the screen) and ActiveEdges (the front window's edges) on
      the Mac; both are stubs now, so he has no taskbar floor and no window ledges there.
- [ ] All the tests and a TDPT run on the mini, as on Windows.
