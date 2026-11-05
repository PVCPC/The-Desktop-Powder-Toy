use ruffle_core::events::{MouseButton, PlayerEvent};
use ruffle_core::external::Value;
use sha2::Digest;
// needs the user's copy: TDPT_BUDDY_SWF=<path to Interactive Buddy v1.01 or v1.02.swf> cargo test
// with the bridge's hooks in and the sim empty, the game's own 160-tick physics replay (Interactive Buddy Native's
// fixture, TDPT_BUDDY_REPLAY=<its reference/physics-test-baseline.swf>) gives exactly what it gives without them
#[test]
fn replay_unchanged() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_REPLAY") else { return };
    let run = |hooked: bool| {
        let movie = ruffle_core::tag_utils::movie_from_path(&path, None).unwrap();
        let player = ruffle_core::PlayerBuilder::new().with_movie(movie).with_autoplay(true).with_player_version(Some(32)).with_viewport_dimensions(550, 400, 1.0).build();
        let ground = std::sync::Arc::new(std::sync::Mutex::new(super::Ground::default()));
        let mut p = player.lock().unwrap();
        p.preload(&mut ruffle_core::limits::ExecutionLimit::none());
        for _ in 0..600 {
            p.run_frame();
            // in once the game's functions are there (its first replay tick), as frame() does once it's ready
            if hooked && p.get_root_variable("__testTick") == Value::Number(1.0) {
                super::hook_ground(&mut p, &ground);
            }
            if p.get_root_variable("__testDone") == Value::String("true".into()) {
                let Value::String(out) = p.get_root_variable("__testOutput") else { panic!("no output") };
                return (out, ground.lock().unwrap().parts.len());
            }
        }
        panic!("replay never finished");
    };
    let ((plain, _), (hooked, parts)) = (run(false), run(true));
    assert!(parts > 0, "the hooks never ran");
    assert_eq!(plain.lines().count(), 160);
    assert!(plain == hooked, "the bridge changed the game's physics");
}

// a TPT black hole: the game's own vortex pull takes him toward it, and isn't sent back to the sim as his own vortex
#[test]
fn pulled_by_well() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let x = |game: &super::Game| match game.player.lock().unwrap().get_root_variable("body.x") { Value::Number(v) => v, o => panic!("{o:?}") };
    for _ in 0..40 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let x0 = x(&game);
    game.ground.lock().unwrap().well = Some([x0 + 300.0, 300.0, 20.0]);
    for _ in 0..40 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let x1 = x(&game);
    let g = game.ground.lock().unwrap();
    println!("body.x {x0:.1} -> {x1:.1}");
    assert!(x1 > x0 + 50.0, "not pulled: {x0} -> {x1}");
    assert!(!g.shots.iter().any(|s| s[4] == 5.0), "sent back as his vortex");
}

#[test]
fn menu_lines() {
    let mut out = String::new();
    super::flatten("<menu><node label='A &amp; B'/><node label='Tools'><node label='Grenades' enabled='false' selected='true'/><node/></node></menu>", "item", &mut out);
    assert_eq!(out, "1		select|item|0	A & B
1	p	select|item|1	Tools
2	gc	select|item|1.0	Grenades
2	s	select|item|1.1	
");
}

#[test]
fn saves_round_trip() {
    use ruffle_core::backend::storage::StorageBackend;
    let dir = std::env::temp_dir().join(format!("tdpt-buddy-saves-{}", std::process::id()));
    let mut saves = super::Saves(dir.clone());
    assert!(saves.put("localhost/tdpt/buddy.swf/ibSave", b"cash"));
    assert_eq!(saves.get("localhost/tdpt/buddy.swf/ibSave").as_deref(), Some(&b"cash"[..]));
    saves.remove_key("localhost/tdpt/buddy.swf/ibSave");
    assert_eq!(saves.get("localhost/tdpt/buddy.swf/ibSave"), None);
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn early_save_ignored() {
    // the game's own first save (no physics settings yet) is read as none; a whole one, or another file, as it is
    assert!(!super::complete_save(b"\x00daBud\x00cash\x00initialized\x01\x00scripts"));
    assert!(super::complete_save(b"\x00daBud\x00initialized\x01\x00realPhysics\x01\x00playingTime"));
    assert!(super::complete_save(b"something else"));
}

#[test]
fn ground_push_out() {
    // a floor from y = 100 down
    let g = super::Ground { rect: [0, 0, 200, 200], cells: (0..200 * 200).map(|i| (i / 200 >= 100) as u16).collect(), ..Default::default() };
    let (mut x, mut y, mut xv, mut yv) = (50.0, 95.0, 1.0, 6.0);
    let n = g.push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0).expect("in the floor");
    g.push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0); // (3 px a frame: two frames)
    assert!(n.1 < -0.9 && y <= 90.5 && y > 88.0, "{n:?} {y}");
    assert!(yv < 0.0 && xv < 1.0, "bounces up with friction: {xv} {yv}");
    assert!(g.under(x, y, 10.0));
    assert!(g.push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0).is_none());
}

// sand poured on him rests on him without pressing him down (he was crushed into the floor); sand under him still holds him
#[test]
fn sand_on_top() {
    let sand = |top: bool| super::Ground {
        rect: [0, 0, 200, 200],
        cells: (0..200 * 200).map(|i| if (i / 200 < 100) == top { 1 | super::POWDER } else { 0 }).collect(),
        ..Default::default()
    };
    let (mut x, mut y, mut xv, mut yv) = (50.0, 105.0, 0.0, 0.0);
    assert!(sand(true).push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0).is_none() && y == 105.0, "pushed down: {y}");
    let (mut x, mut y, mut xv, mut yv) = (50.0, 95.0, 0.0, 3.0);
    let g = sand(false);
    assert!(g.push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0).is_some());
    g.push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0);
    assert!(y <= 90.5, "sank in: {y}");
}

#[test]
fn encased_in_ice() {
    // ice everywhere but a hole of radius 11 round (100, 100): closed in a pixel from a part of radius 10
    let ice = |hole: f64| super::Ground {
        rect: [0, 0, 200, 200],
        cells: (0..200 * 200).map(|i| if ((i % 200) as f64 + 0.5 - 100.0).hypot((i / 200) as f64 + 0.5 - 100.0) > hole { 1 | super::ICE } else { 0 }).collect(),
        ..Default::default()
    };
    assert!(ice(11.0).encased(100.0, 100.0, 10.0));
    assert!(!ice(40.0).encased(100.0, 100.0, 10.0), "room round it: not held");
    let half = super::Ground { rect: [0, 0, 200, 200], cells: (0..200 * 200).map(|i| if i / 200 >= 100 { 1 | super::ICE } else { 0 }).collect(), ..Default::default() };
    assert!(!half.encased(100.0, 95.0, 10.0), "standing on ice isn't frozen in");
}

// a block under him: he lands on it and stands there instead of falling to the bottom of the stage
#[test]
fn stands_on_a_pile() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    let mut n = 0;
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {
        n += 1;
        assert!(n < 400, "never ready");
    }
    // a 600 px wide block, top at y = 400, filled in wherever he asks for
    let fill = |game: &super::Game| {
        let mut g = game.ground.lock().unwrap();
        let r = g.want;
        g.cells = (0..r[2] * r[3]).map(|i| { let (x, y) = (r[0] + i % r[2], r[1] + i / r[2]); (y >= 400 && (300..900).contains(&x)) as u16 }).collect();
        g.rect = r;
    };
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![600.into(), 200.into()]).unwrap();
    for _ in 0..200 {
        fill(&game);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let mut p = game.player.lock().unwrap();
    let y = |p: &mut ruffle_core::Player, n: &str| match p.get_root_variable(n) { Value::Number(v) => v, o => panic!("{n} {o:?}") };
    let (ly, lr, by) = (y(&mut p, "lLeg.y"), y(&mut p, "lLeg.rad"), y(&mut p, "body.y"));
    println!("lLeg bottom {:.1}, body {by:.1}, onGround {:?}, cam {:?} {:?} {:?}", ly + lr, p.get_root_variable("onGround"), p.get_root_variable("camx"), p.get_root_variable("camy"), p.get_root_variable("camz"));
    assert!((ly + lr - 400.0).abs() < 4.0, "feet not on the block: {}", ly + lr);
    assert!(by < 400.0, "body sank: {by}");
}

// slopes: he slides down a 45 degree one; buried under 200 px of sand he digs up through it (not through metal)
#[test]
fn slides_and_digs_out() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    let start = || {
        let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
        let mut out = vec![0u32; 1280 * 720];
        while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
        (game, out)
    };
    let (mut game, mut out) = start();
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![500.into(), 500.into()]).unwrap();
    for _ in 0..80 {
        let mut g = game.ground.lock().unwrap();
        let r = g.want;
        g.cells = (0..r[2] * r[3]).map(|i| (r[1] + i / r[2] >= 400 + (r[0] + i % r[2] - 300)) as u16).collect();
        g.rect = r;
        drop(g);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let x = n(&game, "body.x");
    println!("slope: body.x 500 -> {x:.0}");
    assert!(x > 540.0, "didn't slide down: {x}");
    for (cell, out_too) in [(super::POWDER | 1, true), (1, false)] {
        drop(game);
        (game, out) = start();
        game.player.lock().unwrap().call_root_method("setBuddyXY", vec![600.into(), 300.into()]).unwrap();
        let mut hole = vec![];
        for i in 0..400 {
            if i == 80 {
                hole = game.ground.lock().unwrap().parts.clone(); // the sand settles around him: his shape stays empty
            }
            let mut g = game.ground.lock().unwrap();
            let r = g.want;
            g.cells = (0..r[2] * r[3])
                .map(|i| {
                    let (x, y) = ((r[0] + i % r[2]) as f64 + 0.5, (r[1] + i / r[2]) as f64 + 0.5);
                    let mine = hole.iter().any(|p| (x - p[0]).hypot(y - p[1]) <= p[2] + 1.0);
                    if y >= 400.0 { 1 } else if !hole.is_empty() && y >= 200.0 && (300.0..900.0).contains(&x) && !mine { cell } else { 0 }
                })
                .collect();
            g.rect = r;
            drop(g);
            super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
        }
        let feet = n(&game, "lLeg.y") + n(&game, "lLeg.rad");
        println!("buried (powder {out_too}): feet at {feet:.0}");
        if out_too {
            assert!((feet - 200.0).abs() < 6.0, "didn't dig out onto the pile: {feet}");
        } else {
            assert!(feet > 390.0, "went up through metal: {feet}");
        }
    }
}

// a pool of water (TPT weight 30: 2 + 8) under him: he floats with part of him above the surface
#[test]
fn floats_in_water() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let fill = |game: &super::Game| {
        let mut g = game.ground.lock().unwrap();
        let r = g.want;
        g.cells = (0..r[2] * r[3]).map(|i| if r[1] + i / r[2] >= 450 { 2 + 8 } else { 0 }).collect();
        g.rect = r;
    };
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![600.into(), 200.into()]).unwrap();
    for _ in 0..400 {
        fill(&game);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let mut p = game.player.lock().unwrap();
    let y = |p: &mut ruffle_core::Player, n: &str| match p.get_root_variable(n) { Value::Number(v) => v, o => panic!("{n} {o:?}") };
    let (hy, by, byv) = (y(&mut p, "head.y"), y(&mut p, "body.y"), y(&mut p, "body.yv"));
    println!("head {hy:.1} body {by:.1} yv {byv:.2}");
    assert!(by > 430.0 && by < 520.0, "body not at the surface: {by}");
    assert!(hy < 450.0, "head under water: {hy}");
    assert!(byv.abs() < 1.5, "still bobbing hard: {byv}");
}

// a lost part (NaN, as a bad save brings back) is put back together instead of staying lost
#[test]
fn heals_nan() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    game.player.lock().unwrap().set_root_variable("lArm.y", Value::Number(f64::NAN)).unwrap();
    for _ in 0..40 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let mut p = game.player.lock().unwrap();
    for part in super::PARTS {
        let v = p.get_root_variable(&format!("{part}.y"));
        assert!(matches!(v, Value::Number(n) if n.is_finite()), "{part}.y = {v:?}");
    }
}

// a steady wind to the right blows him over to the right
#[test]
fn blown_by_air() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let x = |game: &super::Game| match game.player.lock().unwrap().get_root_variable("body.x") { Value::Number(v) => v, o => panic!("{o:?}") };
    let x0 = x(&game);
    {
        let mut g = game.ground.lock().unwrap();
        g.air_rect = [0, 0, 320, 180];
        g.air = vec![[3.0, 0.0, 0.0]; 320 * 180];
    }
    for _ in 0..60 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let x1 = x(&game);
    println!("wind: body {x0:.1} -> {x1:.1}");
    assert!(x1 > x0 + 50.0, "not blown: {x0} -> {x1}");
}

// a pressure jump just left of him (a TPT bomb) sets off the game's own explosion there, which throws him right
#[test]
fn thrown_by_blast() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    for _ in 0..40 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    let (x0, y0) = (n(&game, "body.x"), n(&game, "body.y"));
    let set = |game: &super::Game, p: f32| {
        let mut g = game.ground.lock().unwrap();
        g.air_rect = [0, 0, 320, 180];
        g.air = vec![[0.0; 3]; 320 * 180];
        let (u, v) = ((x0 as usize - 40) / 4, y0 as usize / 4);
        g.air[v * 320 + u][2] = p;
    };
    set(&game, 0.0);
    super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    set(&game, 60.0);
    super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    set(&game, 60.0); // steady pressure: no second blast
    for _ in 0..3 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let x1 = n(&game, "body.x");
    println!("blast: body {x0:.0} -> {x1:.0}");
    assert!(x1 > x0 + 100.0, "not thrown: {x0} -> {x1}");
}

// standing in fire sets him alight (the game spreads it: sadder, and cash); stepping into water puts it out
#[test]
fn burns_and_douses() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let floor = |game: &super::Game, c: u16| {
        let mut g = game.ground.lock().unwrap();
        let r = g.want;
        g.cells = (0..r[2] * r[3]).map(|i| if r[1] + i / r[2] >= 600 { c } else { 0 }).collect();
        g.rect = r;
    };
    let get = |game: &super::Game, k: &str| game.player.lock().unwrap().get_root_variable(k);
    for _ in 0..60 {
        floor(&game, 1 | super::HOT);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    println!("fire: body {:?} head {:?} emotion {:?} cash {:?}", get(&game, "body.onFire"), get(&game, "head.onFire"), get(&game, "emotion"), get(&game, "cash"));
    assert_eq!(get(&game, "head.onFire"), Value::Bool(true), "fire didn't spread");
    assert!(matches!(get(&game, "emotion"), Value::Number(e) if e < -1.0), "not upset");
    for _ in 0..200 {
        floor(&game, 2 + 8);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    println!("water: body {:?} head {:?}", get(&game, "body.onFire"), get(&game, "head.onFire"));
    assert_eq!(get(&game, "body.onFire"), Value::Bool(false), "still burning in water");
    assert_eq!(get(&game, "head.onFire"), Value::Bool(false), "head still burning");
}

// a floor of live wire shocks him (the game's own shock: cash, sadder); a frozen floor slows his fall onto it
#[test]
fn shocked_and_chilled() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let floor = |game: &super::Game, c: u16| {
        let mut g = game.ground.lock().unwrap();
        let r = g.want;
        g.cells = (0..r[2] * r[3]).map(|i| if r[1] + i / r[2] >= 600 { c } else { 0 }).collect();
        g.rect = r;
    };
    let get = |game: &super::Game, k: &str| game.player.lock().unwrap().get_root_variable(k);
    let mut shocked = false;
    for _ in 0..60 {
        floor(&game, 1 | super::SPARK);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
        shocked |= get(&game, "beingShocked") == Value::Bool(true);
    }
    println!("spark: shocked {shocked} emotion {:?} cash {:?}", get(&game, "emotion"), get(&game, "cash"));
    assert!(shocked, "never shocked");
    assert!(matches!(get(&game, "cash"), Value::Number(c) if c > 0.0), "no cash");
    let e0 = match get(&game, "emotion") { Value::Number(e) => e, o => panic!("{o:?}") };
    for _ in 0..60 {
        floor(&game, 1 | super::COLD);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let e1 = match get(&game, "emotion") { Value::Number(e) => e, o => panic!("{o:?}") };
    println!("cold: emotion {e0:.2} -> {e1:.2}");
    assert!(e1 < e0 - 0.5, "cold didn't bother him");
}

// a pool of acid upsets him (and he floats in it)
#[test]
fn hurt_by_acid() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    for _ in 0..60 {
        {
            let mut g = game.ground.lock().unwrap();
            let r = g.want;
            g.cells = (0..r[2] * r[3]).map(|i| if r[1] + i / r[2] >= 600 { super::ACID } else { 0 }).collect();
            g.rect = r;
        }
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let e = game.player.lock().unwrap().get_root_variable("emotion");
    println!("acid: emotion {e:?}");
    assert!(matches!(e, Value::Number(e) if e < -2.0), "acid didn't hurt: {e:?}");
}

// caustic gas upsets him; a BOMB under him blows him up
#[test]
fn hurt_and_bang() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    let floor = |game: &super::Game, c: u16| {
        let mut g = game.ground.lock().unwrap();
        let r = g.want;
        g.cells = (0..r[2] * r[3]).map(|i| if r[1] + i / r[2] >= 600 { c } else { 0 }).collect();
        g.rect = r;
    };
    for _ in 0..60 {
        floor(&game, 1 | super::HURT);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let e = n(&game, "emotion");
    assert!(e < -2.0, "caustic gas didn't hurt: {e}");
    let mut top = f64::MAX;
    for _ in 0..30 {
        floor(&game, 1 | super::BANG);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
        top = top.min(n(&game, "body.y"));
    }
    println!("hurt: emotion {e:.0}; bang: body up to {top:.0}");
    assert!(top < 500.0, "the bomb didn't throw him: {top}");
}

// TPT gravity off: he hangs in the air; sideways: he falls sideways
#[test]
fn follows_tpt_gravity() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    game.ground.lock().unwrap().gravity = (0.0, 0.0);
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![600.into(), 200.into()]).unwrap();
    for _ in 0..40 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let (x0, y0) = (n(&game, "body.x"), n(&game, "body.y"));
    game.ground.lock().unwrap().gravity = (1.0, 0.0);
    for _ in 0..20 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let (x1, y1) = (n(&game, "body.x"), n(&game, "body.y"));
    println!("gravity: off {x0:.0},{y0:.0}; sideways {x1:.0},{y1:.0}");
    assert!(y0 < 300.0, "fell with gravity off: {y0}");
    assert!(x1 > x0 + 50.0 && (y1 - y0).abs() < 30.0, "didn't fall sideways: {x0},{y0} -> {x1},{y1}");
}

// a portal floor drops him out of its exit; a VOID floor swallows him and he comes back at the top
#[test]
fn portal_and_void() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    let floor = |game: &super::Game, c: u16| {
        let mut g = game.ground.lock().unwrap();
        let r = g.want;
        g.cells = (0..r[2] * r[3]).map(|i| { let (x, y) = (r[0] + i % r[2], r[1] + i / r[2]); if y >= 600 && x > 700 { c } else { 0 } }).collect();
        g.rect = r;
    };
    game.ground.lock().unwrap().exit = Some((200.0, 150.0));
    let mut went = false;
    for _ in 0..80 {
        floor(&game, super::PORTAL);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
        went |= n(&game, "body.x") < 400.0;
    }
    assert!(went, "never came out of the portal");
    game.ground.lock().unwrap().exit = None;
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![900.into(), 500.into()]).unwrap();
    let mut back = false;
    for _ in 0..80 {
        floor(&game, super::EATER);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
        back |= n(&game, "body.y") < 200.0 && (n(&game, "body.x") - 640.0).abs() < 60.0;
    }
    assert!(back, "never came back after the void");
}

// the game's own bomb is passed on to TDPT; TPT's blast calling explode() is not
#[test]
fn game_booms() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    game.player.lock().unwrap().call_root_method("explode", vec![300.into(), 400.into(), 1.into()]).unwrap();
    let booms = std::mem::take(&mut game.ground.lock().unwrap().booms);
    assert_eq!(booms, vec![[300.0, 400.0, 1.0]]);
    super::OURS.store(true, super::Ordering::Relaxed);
    game.player.lock().unwrap().call_root_method("explode", vec![300.into(), 400.into(), 1.into()]).unwrap();
    super::OURS.store(false, super::Ordering::Relaxed);
    assert!(game.ground.lock().unwrap().booms.is_empty());
}

// a piece of gore reaches TPT (as blood): where it starts and how fast
#[test]
fn game_gore() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    game.ground.lock().unwrap().shots.clear();
    game.player.lock().unwrap().call_root_method("gore", vec![300.into(), 400.into(), 2.into(), (-3).into(), 1.into()]).unwrap();
    let shots = std::mem::take(&mut game.ground.lock().unwrap().shots);
    assert!(shots.contains(&[300.0, 400.0, 2.0, -3.0, 9.0]), "{shots:?}");
}

// his weapons reach the sim: the pistol's bullets as their path each frame (flying from the gun, at the mouse, to him),
// the flamethrower's flames as points; the game's own all-items cheat (keys) to have them
#[test]
fn weapon_shots() {
    use ruffle_core::events::{KeyDescriptor, KeyLocation, LogicalKey, NamedKey, PhysicalKey};
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    for c in "srdldrul".chars() {
        let (physical_key, logical_key) = match c {
            's' => (PhysicalKey::Space, LogicalKey::Character(' ')),
            'r' => (PhysicalKey::ArrowRight, LogicalKey::Named(NamedKey::ArrowRight)),
            'd' => (PhysicalKey::ArrowDown, LogicalKey::Named(NamedKey::ArrowDown)),
            'l' => (PhysicalKey::ArrowLeft, LogicalKey::Named(NamedKey::ArrowLeft)),
            _ => (PhysicalKey::ArrowUp, LogicalKey::Named(NamedKey::ArrowUp)),
        };
        let key = KeyDescriptor { physical_key, logical_key, key_location: KeyLocation::Standard };
        for event in [PlayerEvent::KeyDown { key }, PlayerEvent::KeyUp { key }] {
            game.player.lock().unwrap().handle_event(event);
            for _ in 0..3 {
                super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
            }
        }
    }
    let menus = super::read_menus(&mut game.player.lock().unwrap());
    let body = { let mut p = game.player.lock().unwrap(); (p.get_root_variable("body.x"), p.get_root_variable("body.y")) };
    let (bx, by) = match body {
        (Value::Number(x), Value::Number(y)) => (x, y),
        other => panic!("{other:?}"),
    };
    let (mx, my) = (bx - 300.0, by - 150.0);
    let mut fire = |game: &mut super::Game, item: &str| {
        let Some(line) = menus.lines().find(|l| l.split('\t').nth(3) == Some(item)) else { return None };
        let pick = line.split('\t').nth(2).unwrap().to_owned();
        game.player.lock().unwrap().call_root_method("__hostMenuCommand", vec![pick.into()]).unwrap();
        for _ in 0..10 {
            super::frame(game, 25.0, mx, my, &mut out);
        }
        game.ground.lock().unwrap().shots.clear();
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseMove { x: mx, y: my });
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseDown { x: mx, y: my, button: MouseButton::Left, index: None });
        for _ in 0..10 {
            super::frame(game, 25.0, mx, my, &mut out);
        }
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseUp { x: mx, y: my, button: MouseButton::Left });
        Some(std::mem::take(&mut game.ground.lock().unwrap().shots))
    };
    game.ground.lock().unwrap().hide = 2; // TPT fire only: the game's flames aren't drawn
    let flames = fire(&mut game, "Flamethrower").expect("no Flamethrower");
    assert!(flames.iter().filter(|s| s[4] == 2.0).count() > 10, "{flames:?}");
    {
        let mut p = game.player.lock().unwrap();
        let Value::Object(clips) = p.get_root_variable("particles") else { panic!() };
        let name = clips.keys().find(|n| n.starts_with("flame")).expect("no flames").clone();
        assert_eq!(p.get_root_variable(&format!("particles.{name}._visible")), Value::Bool(false));
    }
    if let Some(bullets) = fire(&mut game, "Pistol") {
        // v1.02: from the gun (the mouse), a frame's flight
        let b = bullets.iter().find(|s| s[4] == 1.0).unwrap_or_else(|| panic!("{bullets:?}"));
        assert!((b[0] - mx).hypot(b[1] - my) < 5.0 && (b[2] - b[0]).hypot(b[3] - b[1]) > 100.0, "{b:?}");
    }
    // his hoses: each new drop, at the nozzle (the mouse); hidden, the game's own drops aren't drawn
    game.ground.lock().unwrap().hide = 1;
    if let Some(drops) = fire(&mut game, "Fire Hose") {
        let d = drops.iter().find(|s| s[4] == 4.0).unwrap_or_else(|| panic!("{drops:?}"));
        assert!((d[0] - mx).hypot(d[1] - my) < 20.0 && d[2].hypot(d[3]) > 10.0, "{d:?}");
        let mut p = game.player.lock().unwrap();
        let Value::Object(clips) = p.get_root_variable("particles") else { panic!() };
        let name = clips.keys().find(|n| n.starts_with("smoke")).expect("no drops").clone();
        assert_eq!(p.get_root_variable(&format!("particles.{name}._visible")), Value::Bool(false));
    }
    // a strong vortex held: it pulls (hard) to the mouse
    if let Some(shots) = fire(&mut game, "Strong Gravity Vortex") {
        let w = shots.iter().find(|s| s[4] == 5.0).unwrap_or_else(|| panic!("{shots:?}"));
        assert!((w[0] - mx).hypot(w[1] - my) < 2.0 && w[2] == 3.0, "{w:?}");
    }
    // the Radio: placed, it plays and he dances to it
    if fire(&mut game, "Radio").is_some() {
        // (when he starts is the game's own dice: up to 8 s)
        let mut danced = false;
        for _ in 0..200 {
            super::frame(&mut game, 25.0, mx, my, &mut out);
            danced |= std::mem::take(&mut game.ground.lock().unwrap().shots).iter().any(|s| s[4] == 3.0);
            if danced {
                break;
            }
        }
        assert!(danced, "not dancing");
    }
    // the stun gun on his head: he's being shocked
    if let Some(line) = menus.lines().find(|l| l.split('\t').nth(3) == Some("Stun Gun")) {
        let pick = line.split('\t').nth(2).unwrap().to_owned();
        game.player.lock().unwrap().call_root_method("__hostMenuCommand", vec![pick.into()]).unwrap();
        for _ in 0..20 {
            super::frame(&mut game, 25.0, mx, my, &mut out);
        }
        let n = |k: &str| match game.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
        let (hx, hy) = (n("head.x"), n("head.y"));
        game.ground.lock().unwrap().shots.clear();
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseMove { x: hx, y: hy });
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseDown { x: hx, y: hy, button: MouseButton::Left, index: None });
        for _ in 0..3 {
            super::frame(&mut game, 25.0, hx, hy, &mut out);
        }
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseUp { x: hx, y: hy, button: MouseButton::Left });
        let shots = std::mem::take(&mut game.ground.lock().unwrap().shots);
        assert!(shots.iter().any(|s| s[4] == 6.0), "not shocked: {shots:?}");
    }
    // his toys on the sim: a ball dropped from the mouse lands on a block 100 px below it (its own patch of the sim,
    // not his)
    if let Some(line) = menus.lines().find(|l| l.split('\t').nth(3) == Some("Bowling Balls")) {
        let pick = line.split('\t').nth(2).unwrap().to_owned();
        game.player.lock().unwrap().call_root_method("__hostMenuCommand", vec![pick.into()]).unwrap();
        game.ground.lock().unwrap().toys = true;
        let fill = |game: &super::Game| {
            let mut g = game.ground.lock().unwrap();
            g.cells.clear();
            g.patches = g.toys_at.iter().map(|&r| super::Patch { rect: r, cells: (0..r[2] * r[3]).map(|i| (r[1] + i / r[2] >= my as i32 + 100 && (r[0] + i % r[2] - mx as i32).abs() < 200) as u16).collect(), ..Default::default() }).collect();
        };
        for f in 0..120 {
            fill(&game);
            if f == 20 {
                game.player.lock().unwrap().handle_event(PlayerEvent::MouseMove { x: mx, y: my });
                game.player.lock().unwrap().handle_event(PlayerEvent::MouseDown { x: mx, y: my, button: MouseButton::Left, index: None });
            }
            if f == 22 {
                game.player.lock().unwrap().handle_event(PlayerEvent::MouseUp { x: mx, y: my, button: MouseButton::Left });
            }
            super::frame(&mut game, 25.0, mx, my, &mut out);
        }
        let mut p = game.player.lock().unwrap();
        let Value::Number(n) = p.get_root_variable("objectList.length") else { panic!() };
        let k = |p: &mut ruffle_core::Player, f: &str| match p.get_root_variable(&format!("objectList.{}.targ.{f}", n as usize - 1)) { Value::Number(v) => v, o => panic!("{f} {o:?}") };
        let (y, r) = (k(&mut p, "y"), k(&mut p, "rad"));
        assert!((y + r - (my + 100.0)).abs() < 4.0, "ball not on the block: bottom {} block {}", y + r, my + 100.0);
    }
    // a bullet the sim stopped (TDPT's stop: where, and its way): it ends there, and the game drops it
    if let Some(line) = menus.lines().find(|l| l.split('\t').nth(3) == Some("Pistol")) {
        let mut out2 = vec![0u32; 1280 * 720]; // (fire has out)
        let pick = line.split('\t').nth(2).unwrap().to_owned();
        game.player.lock().unwrap().call_root_method("__hostMenuCommand", vec![pick.into()]).unwrap();
        for _ in 0..5 {
            super::frame(&mut game, 25.0, mx, my, &mut out2);
        }
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseMove { x: mx, y: my });
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseDown { x: mx, y: my, button: MouseButton::Left, index: None });
        let get = |game: &mut super::Game, k: &str| match game.player.lock().unwrap().get_root_variable(k) { Value::Number(n) => n, v => panic!("{k} {v:?}") };
        for _ in 0..10 {
            super::frame(&mut game, 25.0, mx, my, &mut out2);
            if get(&mut game, "bullets.length") > 0.0 {
                break;
            }
        }
        let i = get(&mut game, "bullets.length") as usize - 1;
        let (x, y, xv, yv) = (get(&mut game, &format!("bullets.{i}.x")), get(&mut game, &format!("bullets.{i}.y")), get(&mut game, &format!("bullets.{i}.xv")), get(&mut game, &format!("bullets.{i}.yv")));
        let (dx, dy) = (xv / xv.hypot(yv), yv / xv.hypot(yv));
        let (hx, hy) = (x - 30.0 * dx, y - 30.0 * dy);
        game.ground.lock().unwrap().stops.push([hx, hy, dx, dy]);
        super::frame(&mut game, 25.0, mx, my, &mut out2);
        assert_eq!((get(&mut game, &format!("bullets.{i}.x")), get(&mut game, &format!("bullets.{i}.y")), get(&mut game, &format!("bullets.{i}.ricNum"))), (hx, hy, -1.0));
        super::frame(&mut game, 25.0, mx, my, &mut out2);
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseUp { x: mx, y: my, button: MouseButton::Left });
        let n = get(&mut game, "bullets.length") as usize;
        assert!((0..n).all(|j| get(&mut game, &format!("bullets.{j}.x")) != hx), "not dropped");
    }
}

// a shorter stage (TDPT's play area ending at the taskbar) is a higher floor: he stands on its bottom
#[test]
fn floor_follows_stage() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    game.size = (1280, 600);
    let mut out = vec![0u32; 1280 * 600];
    for _ in 0..120 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let n = |k: &str| match game.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    let (ly, lr) = (n("lLeg.y"), n("lLeg.rad"));
    println!("stage 600: foot bottom {:.1}", ly + lr);
    assert!((585.0..=600.0).contains(&(ly + lr)), "not standing on the stage bottom: {}", ly + lr);
}

// the active window: he lands on its top edge, inside it on its bottom edge, and rides it when it moves
#[test]
fn window_ledges() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    let ledge = |game: &super::Game, dy: f64| game.ground.lock().unwrap().set_ledge(Some(super::Ledge { id: 7, left: 300.0, right: 900.0, rows: [400.0 + dy, 600.0 + dy] }));
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![600.into(), 200.into()]).unwrap();
    for _ in 0..90 {
        ledge(&game, 0.0);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let on = n(&game, "lLeg.y") + n(&game, "lLeg.rad");
    for i in 1..=30 {
        ledge(&game, -2.0 * i as f64);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let rode = n(&game, "lLeg.y") + n(&game, "lLeg.rad");
    // dragged sideways at 120 frames a second (three frames to each game step): he's drawn moving with it every
    // frame, not in the game's 40 Hz steps
    let slide = |game: &super::Game, dx: f64| game.ground.lock().unwrap().set_ledge(Some(super::Ledge { id: 7, left: 300.0 + dx, right: 900.0 + dx, rows: [340.0, 540.0] }));
    let (mut steps, mut x0) = (vec![], n(&game, "__host.current.0.values.0"));
    for i in 1..=30 {
        slide(&game, 3.0 * i as f64);
        super::frame(&mut game, 25.0 / 3.0, 0.0, 0.0, &mut out);
        let x = n(&game, "__host.current.0.values.0");
        steps.push(x - x0);
        x0 = x;
    }
    println!("ledges: drawn steps while dragged {steps:.1?}");
    assert!(steps.iter().all(|d| (d - 3.0).abs() < 1.5), "not drawn with the window each frame: {steps:?}");
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![600.into(), 480.into()]).unwrap();
    for _ in 0..90 {
        ledge(&game, -60.0);
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let inside = n(&game, "lLeg.y") + n(&game, "lLeg.rad");
    println!("ledges: on top {on:.1}, rode up to {rode:.1}, inside on {inside:.1}");
    assert!((on - 400.0).abs() < 3.0, "not on the window: {on}");
    assert!((rode - 340.0).abs() < 5.0, "didn't ride it up: {rode}");
    assert!((inside - 540.0).abs() < 3.0, "not on its bottom edge: {inside}");
}

// a window's side stops him like it stops powder: thrown at it, he stays out
#[test]
fn window_sides() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![500.into(), 600.into()]).unwrap();
    let mut right = 0f64;
    for i in 0..120 {
        {
            let mut g = game.ground.lock().unwrap();
            let r = g.want;
            g.cells = (0..r[2] * r[3]).map(|i| if r[0] + i % r[2] >= 700 { super::WINDOW } else { 0 }).collect();
            g.rect = r;
        }
        if (30..60).contains(&i) {
            let mut p = game.player.lock().unwrap();
            for part in super::PARTS {
                p.set_root_variable(&format!("{part}.xv"), 15.0.into()).unwrap();
            }
        }
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
        right = right.max(super::PARTS.iter().map(|p| n(&game, &format!("{p}.x")) + n(&game, &format!("{p}.rad"))).fold(0.0, f64::max));
    }
    println!("sides: rightmost {right:.1}");
    assert!(right < 702.0, "went into the window: {right}");
    // and dropped onto one (Windows: Solid), he stands on its top
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![600.into(), 200.into()]).unwrap();
    for _ in 0..120 {
        {
            let mut g = game.ground.lock().unwrap();
            let r = g.want;
            g.cells = (0..r[2] * r[3]).map(|i| if (300..900).contains(&(r[0] + i % r[2])) && r[1] + i / r[2] >= 400 { super::WINDOW } else { 0 }).collect();
            g.rect = r;
        }
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let feet = n(&game, "lLeg.y") + n(&game, "lLeg.rad");
    println!("sides: feet on the window top at {feet:.1}");
    assert!((feet - 400.0).abs() < 3.0, "not standing on the window: {feet}");
}

// only where things are drawn comes back, a frame late: his pixels are where he is, and none are left where he was
#[test]
fn frame_follows_him() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    let seen = |out: &[u32], x: f64, y: f64| (-5..=5).any(|d| out[(y as usize).saturating_add_signed(d) * 1280 + x as usize] >> 24 != 0);
    for at in [700.0, 1100.0] {
        game.player.lock().unwrap().call_root_method("setBuddyXY", vec![at.into(), 400.into()]).unwrap();
        for _ in 0..90 {
            super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
        }
        let (x, y) = (n(&game, "body.x"), n(&game, "body.y"));
        assert!(seen(&out, x, y), "nothing drawn on his body at {x},{y}");
    }
    let left = |out: &[u32]| (500..720).map(|y| out[y * 1280 + 600..y * 1280 + 800].iter().filter(|p| **p >> 24 != 0).count()).sum::<usize>();
    println!("frame: pixels left where he was {}", left(&out));
    assert_eq!(left(&out), 0, "pixels left behind where he was");
    // three buffers taking turns as with TDPT (which takes a frame only now and then): a frame just drawn never
    // has old pixels of him in it
    let mut bufs = [vec![0u32; 1280 * 720], vec![0u32; 1280 * 720], vec![0u32; 1280 * 720]];
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![700.into(), 400.into()]).unwrap();
    for i in 0..120 {
        if i == 60 {
            game.player.lock().unwrap().call_root_method("setBuddyXY", vec![1100.into(), 400.into()]).unwrap();
        }
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut bufs[0]);
        if i > 64 {
            assert_eq!(left(&bufs[0]), 0, "old pixels in a new frame at {i}");
        }
        bufs.swap(0, 1);
        if i % 5 == 0 {
            bufs.swap(1, 2);
        }
    }
}

// his sounds decode and mix (into a mixer with no speakers: nothing is played): the game's punch, as its
// scripting's playSound(name, volume) plays it
#[test]
fn sounds() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mixer = ruffle_core::backend::audio::AudioMixer::new(2, 44100);
    let proxy = mixer.proxy();
    let mut game = super::open_with(path.to_str().unwrap(), 1280, 720, Some(super::sound::Unplugged { mixer })).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let mut buf = vec![0f32; 2 * 44100 / 40];
    let mut loud = |game: &mut super::Game| {
        let mut n = 0;
        for _ in 0..40 {
            super::frame(game, 25.0, 0.0, 0.0, &mut out);
            proxy.mix::<f32>(&mut buf);
            n += buf.iter().filter(|v| v.abs() > 0.01).count();
        }
        n
    };
    game.player.lock().unwrap().call_root_method("playSound", vec!["punch1".into(), 100.into()]).unwrap();
    let silent = loud(&mut game); // he starts at volume 0, until TDPT says how loud
    game.player.lock().unwrap().audio_mut().set_volume(1.0);
    game.player.lock().unwrap().call_root_method("playSound", vec!["punch1".into(), 100.into()]).unwrap();
    let heard = loud(&mut game);
    println!("sounds: {silent} loud samples at volume 0, {heard} at 1");
    assert_eq!(silent, 0, "sound at volume 0");
    assert!(heard > 1000, "no sound mixed");
}

// drawn twice as sharp (scale 2) on the same stage: the frame is twice the size, he's at twice his stage place,
// in finer pixels (more of them on his body), and the stage size and his game place are unchanged
#[test]
fn sharp_frames() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    game.player.lock().unwrap().call_root_method("setBuddyXY", vec![700.into(), 400.into()]).unwrap();
    for _ in 0..90 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let (x, y) = (n(&game, "body.x"), n(&game, "body.y"));
    let drawn = |out: &[u32], w: usize, x0: usize, y0: usize, r: usize| (y0 - r..(y0 + r).min(out.len() / w)).map(|y| out[y * w + x0 - r..y * w + x0 + r].iter().filter(|p| **p >> 24 != 0).count()).sum::<usize>();
    let one = drawn(&out, 1280, x as usize, y as usize, 60);
    game.scale = 2;
    let mut big = vec![0u32; 2560 * 1440];
    for _ in 0..3 {
        super::frame(&mut game, 0.0, 0.0, 0.0, &mut big); // a frame late, as always
    }
    let (x2, y2) = (n(&game, "body.x"), n(&game, "body.y"));
    let two = drawn(&big, 2560, (x2 * 2.0) as usize, (y2 * 2.0) as usize, 120);
    let stage = game.player.lock().unwrap().get_root_variable("__host.stageWidth");
    println!("sharp: {one} px at 1x, {two} at 2x, stage {stage:?}, body {x:.0},{y:.0} -> {x2:.0},{y2:.0}");
    assert!(two as f64 > one as f64 * 3.5, "not drawn at twice the size: {one} -> {two}");
    assert!((x2 - x).abs() < 30.0 && (y2 - y).abs() < 30.0, "moved in the game");
}

// the Buddy tab's size: 200% doubles his parts, back to 100% restores them
#[test]
fn buddy_size() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let n = |g: &super::Game, k: &str| match g.player.lock().unwrap().get_root_variable(k) { Value::Number(v) => v, o => panic!("{k} {o:?}") };
    let normal = n(&game, "body.rad");
    game.settings[0] = 200.0;
    super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    let big = n(&game, "body.rad");
    game.settings[0] = 100.0;
    super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    println!("size: {normal} -> {big} -> {}", n(&game, "body.rad"));
    assert!((big / normal - 2.0).abs() < 0.01 && (n(&game, "body.rad") - normal).abs() < 0.01);
}

#[test]
fn hit_shapes() {
    let h = super::hits("e,10,20,100,100;u,150,0,30,40;c,170,0,10,10,42;e,0,0,NaN,10;junk;");
    assert_eq!(h.len(), 3);
    assert!(super::hit(&h, 60.0, 70.0) && super::hit(&h, 160.0, 39.0));
    assert!(!super::hit(&h, 12.0, 22.0) && !super::hit(&h, 140.0, 10.0)); // a round thing's corner; a gap
}

// The Fist drags him: rest it on his body, press, drag up and left, and he follows the mouse. (Swinging it into
// him is a punch, and the game's Open Hand doesn't drag at all: he reaches for the cursor instead.)
#[test]
fn drag_him() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    let body = |g: &super::Game| {
        let mut p = g.player.lock().unwrap();
        match (p.get_root_variable("body.x"), p.get_root_variable("body.y")) {
            (Value::Number(x), Value::Number(y)) => (x, y),
            other => panic!("{other:?}"),
        }
    };
    let mut n = 0;
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {
        n += 1;
        assert!(n < 400, "never ready");
    }
    let menus = super::read_menus(&mut game.player.lock().unwrap());
    let fist = menus.lines().find(|l| l.ends_with("\tFist")).unwrap_or_else(|| panic!("no Fist: {menus}"));
    let pick = fist.split('\t').nth(2).unwrap().to_owned();
    game.player.lock().unwrap().call_root_method("__hostMenuCommand", vec![pick.into()]).unwrap();
    for _ in 0..80 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    // ease the fist onto him from above, then let it rest (a bit off his centre: right on it, the game divides by 0)
    let (x, y) = body(&game);
    let x = x + 3.0;
    for i in (0..=60).rev() {
        super::frame(&mut game, 25.0, x, y - 2.0 * i as f64, &mut out);
    }
    let (x, y) = body(&game);
    let Value::String(data) = game.player.lock().unwrap().get_root_variable("__host.hitData") else { panic!("no hit data") };
    assert!(super::hit(&super::hits(&data), x, y), "body not clickable at {x},{y}: {data}");
    super::frame(&mut game, 25.0, x, y, &mut out);
    game.player.lock().unwrap().handle_event(PlayerEvent::MouseDown { x, y, button: MouseButton::Left, index: None });
    for i in 1..=40 {
        super::frame(&mut game, 25.0, x - 8.0 * i as f64, y - 6.0 * i as f64, &mut out);
    }
    let (gx, gy) = (x - 320.0, y - 240.0);
    let (bx, by) = body(&game);
    game.player.lock().unwrap().handle_event(PlayerEvent::MouseUp { x: gx, y: gy, button: MouseButton::Left });
    println!("body {x:.0},{y:.0} -> {bx:.0},{by:.0} (mouse at {gx:.0},{gy:.0})");
    assert!((bx - gx).hypot(by - gy) < 60.0, "he didn't follow the mouse");
}

#[test]
fn patches_rebuild_native() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let original = std::fs::read(path).unwrap();
    let v101 = super::hex(&sha2::Sha256::digest(&original)) == super::VERSIONS[0].0;
    let swf = super::patch(original).unwrap(); // (each patch's result is checked by its hash in there)
    if v101 {
        // the desktop app's own native build of v1.01
        assert_eq!(super::hex(&sha2::Sha256::digest(&swf)), "9720616ea241c1b7a4004c90b91ed100dfa21812701141a6bc89a961d28ae9eb");
    }
}

// the game's windows open in the middle of TDPT's stage, and follow a drag even at a big jump a frame
#[test]
fn store_window() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    let mut n = 0;
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {
        n += 1;
        assert!(n < 400, "never ready");
    }
    let player = game.player.clone();
    // (the save may own skins already: Buy New Skins... is the last)
    let buy = super::read_menus(&mut player.lock().unwrap()).lines().find(|l| l.split('\t').nth(3) == Some("Buy New Skins...")).expect("no Buy New Skins").split('\t').nth(2).unwrap().to_owned();
    super::menu_command(&mut game.open, &mut game.placing, &mut player.lock().unwrap(), buy);
    for _ in 0..40 {
        super::frame(&mut game, 25.0, 0.0, 0.0, &mut out);
    }
    let at = |game: &super::Game| {
        let mut p = game.player.lock().unwrap();
        match (p.get_root_variable("skinStoreWindow._x"), p.get_root_variable("skinStoreWindow._y")) {
            (Value::Number(x), Value::Number(y)) => (x, y),
            other => panic!("no store: {other:?}"),
        }
    };
    let (x, y) = at(&game);
    assert_eq!((x, y), (515.0, 193.0), "not in the middle");
    // its title bar, dragged 75 px a frame
    let (mx, my) = (x + 100.0, y + 15.0);
    super::frame(&mut game, 25.0, mx, my, &mut out);
    game.player.lock().unwrap().handle_event(PlayerEvent::MouseDown { x: mx, y: my, button: MouseButton::Left, index: None });
    game.held = true;
    for i in 1..=4 {
        super::frame(&mut game, 25.0, mx - 60.0 * i as f64, my + 45.0 * i as f64, &mut out);
    }
    game.player.lock().unwrap().handle_event(PlayerEvent::MouseUp { x: mx - 240.0, y: my + 180.0, button: MouseButton::Left });
    // (a frame behind the mouse: the game moves it on its next frame)
    let (dx, dy) = at(&game);
    assert!((dx - (x - 240.0)).abs() < 10.0 && (dy - (y + 180.0)).abs() < 10.0, "didn't follow the drag: {dx},{dy}");
    // closed again (its close button), so the game's save doesn't keep it open for the other tests
    let (cx, cy) = (dx + 235.0, dy + 16.0);
    super::frame(&mut game, 25.0, cx, cy, &mut out);
    game.player.lock().unwrap().handle_event(PlayerEvent::MouseDown { x: cx, y: cy, button: MouseButton::Left, index: None });
    game.player.lock().unwrap().handle_event(PlayerEvent::MouseUp { x: cx, y: cy, button: MouseButton::Left });
    super::frame(&mut game, 25.0, cx, cy, &mut out);
    assert!(!matches!(game.player.lock().unwrap().get_root_variable("skinStoreWindow._x"), Value::Number(_)), "didn't close");
}
// the game's other links to the sim: a script asking for TPT sand, its Earthquake and Low Gravity modes, missiles, the
// flail's ball, and a grenade set off by TPT fire on it (its fuse was over 100 frames)
#[test]
fn game_links() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let menus = super::read_menus(&mut game.player.lock().unwrap());
    let (mx, my) = (400.0, 300.0);
    let mut run = |game: &mut super::Game, n: usize| {
        for _ in 0..n {
            super::frame(game, 25.0, mx, my, &mut out);
        }
        std::mem::take(&mut game.ground.lock().unwrap().shots)
    };
    let pick = |game: &mut super::Game, label: &str| {
        let Some(line) = menus.lines().find(|l| l.split('\t').nth(3) == Some(label)) else { return false };
        let c = line.split('\t').nth(2).unwrap().to_owned();
        game.player.lock().unwrap().call_root_method("__hostMenuCommand", vec![c.into()]).unwrap();
        true
    };
    // pressed for 3 frames
    let click = |game: &mut super::Game, run: &mut dyn FnMut(&mut super::Game, usize) -> Vec<[f64; 5]>| {
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseMove { x: mx, y: my });
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseDown { x: mx, y: my, button: MouseButton::Left, index: None });
        let shots = run(game, 3);
        game.player.lock().unwrap().handle_event(PlayerEvent::MouseUp { x: mx, y: my, button: MouseButton::Left });
        shots
    };
    run(&mut game, 5);
    // very sad: his head, every third of a second (tears)
    game.player.lock().unwrap().set_root_variable("emotion", Value::Number(-90.0)).unwrap();
    let shots = run(&mut game, 25);
    assert!(shots.iter().any(|s| s[4] == 16.0 && s[2] > 0.0 && s[3] > 0.7), "no tears: {shots:?}");
    game.player.lock().unwrap().set_root_variable("emotion", Value::Number(0.0)).unwrap();
    // his radio, once its music plays
    {
        assert!(pick(&mut game, "Radio"), "no Radio item");
        run(&mut game, 5);
        let mut shots = click(&mut game, &mut run);
        for _ in 0..20 {
            shots.extend(run(&mut game, 10));
        }
        assert!(shots.iter().any(|s| s[4] == 15.0), "no radio: playingMusic {:?}", game.player.lock().unwrap().get_root_variable("playingMusic"));
    }
    game.player.lock().unwrap().call_root_method("executeText", vec!["assign(tptN, 30);assign(tpt, \"sand\")".into()]).unwrap();
    let shots = run(&mut game, 2);
    let sand = (b'S' as u32 | (b'A' as u32) << 8 | (b'N' as u32) << 16 | (b'D' as u32) << 24) as f64;
    assert!(shots.iter().any(|s| s[4] == 14.0 && s[2] == 30.0 && s[3] == sand), "no sand asked: {shots:?}");
    assert!(!run(&mut game, 2).iter().any(|s| s[4] == 14.0), "asked twice");
    if pick(&mut game, "Earthquake") && pick(&mut game, "Low Gravity") {
        let shots = run(&mut game, 20);
        assert!(shots.iter().any(|s| s[4] == 11.0 && s[0] > 0.5) && shots.iter().any(|s| s[4] == 12.0 && s[0] == 0.35), "{shots:?}");
        pick(&mut game, "Earthquake");
        pick(&mut game, "Low Gravity");
    }
    for (item, kind) in [("Missiles", 13.0), ("Medieval Flail", 10.0)] {
        if pick(&mut game, item) {
            run(&mut game, 5);
            let mut shots = click(&mut game, &mut run);
            shots.extend(run(&mut game, 10));
            assert!(shots.iter().any(|s| s[4] == kind), "{item}: {shots:?}");
        }
    }
    if pick(&mut game, "Grenades") {
        game.ground.lock().unwrap().toys = true;
        run(&mut game, 5);
        game.ground.lock().unwrap().booms.clear();
        click(&mut game, &mut run);
        // fire everywhere round each toy (TDPT's patches, each frame)
        for _ in 0..10 {
            let mut g = game.ground.lock().unwrap();
            g.patches = g.toys_at.iter().map(|&r| super::Patch { rect: r, cells: vec![super::HOT; (r[2] * r[3]) as usize], ..Default::default() }).collect();
            drop(g);
            run(&mut game, 1);
        }
        assert!(!game.ground.lock().unwrap().booms.is_empty(), "the grenade didn't go off");
    }
}
// the game's Dynamic Camera doesn't zoom (the sim round him can't), even after a blast
#[test]
fn dynamic_camera() {
    let Some(path) = std::env::var_os("TDPT_BUDDY_SWF") else { return };
    let mut game = super::open(path.to_str().unwrap(), 1280, 720).unwrap();
    let mut out = vec![0u32; 1280 * 720];
    while !super::frame(&mut game, 25.0, 0.0, 0.0, &mut out) {}
    let menus = super::read_menus(&mut game.player.lock().unwrap());
    let Some(line) = menus.lines().find(|l| l.split('\t').nth(3) == Some("Dynamic Camera")) else { return };
    let c = line.split('\t').nth(2).unwrap().to_owned();
    game.player.lock().unwrap().call_root_method("__hostMenuCommand", vec![c.clone().into()]).unwrap();
    game.player.lock().unwrap().call_root_method("explode", vec![600.into(), 650.into(), 1.into()]).unwrap();
    for _ in 0..40 {
        super::frame(&mut game, 25.0, 400.0, 300.0, &mut out);
        assert_eq!(game.player.lock().unwrap().get_root_variable("camz"), Value::Number(1.0));
    }
}
