// The C interface (tdpt_buddy.h)
use super::*;

fn say(error: *mut c_char, len: usize, text: &str) {
    if error.is_null() || len == 0 {
        return;
    }
    let n = text.len().min(len - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(text.as_ptr(), error as *mut u8, n);
        *error.add(n) = 0;
    }
}

/// # Safety
/// path is a NUL-terminated UTF-8 path; error, if not null, has room for error_len bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_open(path: *const c_char, w: u32, h: u32, error: *mut c_char, error_len: usize) -> *mut Buddy {
    let path = unsafe { CStr::from_ptr(path) }.to_string_lossy().into_owned();
    let shared = Arc::new(Mutex::new(Shared { size: (w, h), scale: 1, mouse: (-1.0, -1.0), settings: SETTINGS, ..Default::default() }));
    // the player can't change threads, so it starts on the game's, which reports back how that went
    let (tell, told) = std::sync::mpsc::channel();
    let thread = {
        let shared = shared.clone();
        // Ruffle's AVM recursion needs more than the default 2 MB
        std::thread::Builder::new().name("buddy".into()).stack_size(16 << 20).spawn(move || {
            match catch_unwind(|| open_with(&path, w, h, sound::Speakers::new())) {
                Ok(Ok(game)) => {
                    tell.send(Ok(game.ground.clone())).ok();
                    run(game, shared);
                }
                Ok(Err(e)) => drop(tell.send(Err(e))),
                Err(_) => drop(tell.send(Err("Ruffle failed to start".into()))),
            }
        })
    };
    match thread.map_err(|e| e.to_string()).and_then(|t| told.recv().map_err(|e| e.to_string())?.map(|g| (t, g))) {
        Ok((t, ground)) => Box::into_raw(Box::new(Buddy { shared, ground, thread: Some(t), front: Vec::new(), front_boxes: Vec::new() })),
        Err(e) => {
            say(error, error_len, &e);
            std::ptr::null_mut()
        }
    }
}

/// His newest frame (w x h, premultiplied 0xAARRGGBB, rows top down), valid until the next call; null while there is
/// none yet (or he crashed). Also passes the mouse (stage pixels) and the stage size on to the game.
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_frame(b: *mut Buddy, mx: f64, my: f64, w: u32, h: u32) -> *const u32 {
    let b = unsafe { &mut *b };
    let Ok(mut s) = b.shared.lock() else { return std::ptr::null() };
    if s.dead {
        return std::ptr::null();
    }
    s.mouse = (mx, my);
    s.size = (w.max(1), h.max(1));
    if s.fresh {
        s.fresh = false;
        std::mem::swap(&mut s.ready, &mut b.front);
        std::mem::swap(&mut s.ready_boxes, &mut b.front_boxes);
    }
    let (pw, ph) = pixels(s.size, s.scale);
    if b.front.len() == pw as usize * ph as usize { b.front.as_ptr() } else { std::ptr::null() }
}

/// His sounds' loudness: 0 silent (as he starts), 1 the game's own
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_volume(b: *mut Buddy, volume: f32) {
    let b = unsafe { &mut *b };
    if let Ok(mut s) = b.shared.lock() {
        s.volume = volume.clamp(0.0, 1.0);
    }
}

/// Draws him scale times sharper (whole screen pixels per stage pixel, 1 or more): tdpt_buddy_frame's frames and
/// tdpt_buddy_boxes are then scale times the stage's size; everything else stays in stage pixels.
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_scale(b: *mut Buddy, scale: u32) {
    let b = unsafe { &mut *b };
    if let Ok(mut s) = b.shared.lock() {
        s.scale = scale.clamp(1, 8);
    }
}

/// Where the frame tdpt_buddy_frame last gave has pixels: up to n boxes (x0, y0, x1, y1 stage pixels) into out; it is
/// clear everywhere else. Returns how many there are.
/// # Safety
/// b is from tdpt_buddy_open; out has room for n * 4.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_boxes(b: *mut Buddy, out: *mut i32, n: usize) -> usize {
    let b = unsafe { &mut *b };
    for (i, r) in b.front_boxes.iter().take(n).enumerate() {
        for k in 0..4 {
            unsafe { *out.add(i * 4 + k) = r[k] as i32 };
        }
    }
    b.front_boxes.len()
}

/// Whether a click at (x, y) stage pixels lands on him, his toys or the game's windows (not on the hand the game
/// draws under the mouse)
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_hit(b: *mut Buddy, x: f64, y: f64) -> bool {
    let b = unsafe { &mut *b };
    b.shared.lock().is_ok_and(|s| !s.dead && hit(&s.hits, x, y))
}

/// The game's menus as text (see MENUS), copied into out (NUL-terminated, cut short if it doesn't fit); returns
/// the length it needs.
/// # Safety
/// b is from tdpt_buddy_open; out has room for len bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_menus(b: *mut Buddy, out: *mut c_char, len: usize) -> usize {
    let b = unsafe { &mut *b };
    let menus = b.shared.lock().map(|s| s.menus.clone()).unwrap_or_default();
    say(out, len, &menus);
    menus.len() + 1
}

/// A command from tdpt_buddy_menus, for the game; or "act:" and one of his own actions ("act:runToPoint:200") for him to do next
/// # Safety
/// b is from tdpt_buddy_open; command is NUL-terminated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_command(b: *mut Buddy, command: *const c_char) {
    let b = unsafe { &mut *b };
    let command = unsafe { CStr::from_ptr(command) }.to_string_lossy().into_owned();
    if let Ok(mut s) = b.shared.lock() {
        s.commands.push(command);
    }
}

/// The Buddy tab's settings: his size (25-300 %), speech bubbles (0 off), their size (25-200 %), toys at once (0: no
/// limit) and smoothing (0 off), as the desktop app had them
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_settings(b: *mut Buddy, size: f64, speech: i32, speech_size: f64, toys: i32, smooth: i32) {
    let b = unsafe { &mut *b };
    if let Ok(mut s) = b.shared.lock() {
        s.settings = [size.clamp(25.0, 300.0), (speech != 0) as u8 as f64, speech_size.clamp(25.0, 200.0), toys.max(0) as f64, (smooth != 0) as u8 as f64];
    }
}

/// The part of the sim he needs next, for tdpt_buddy_ground: rect = x, y, w, h (sim pixels, maybe partly off the sim)
/// # Safety
/// b is from tdpt_buddy_open; rect has room for 4.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_area(b: *mut Buddy, rect: *mut i32) {
    let b = unsafe { &mut *b };
    let want = b.ground.lock().map(|g| g.want).unwrap_or_default();
    unsafe { std::ptr::copy_nonoverlapping(want.as_ptr(), rect, 4) };
}

/// The sim over rect (x, y, w, h): w * h cells, rows top down, as in Ground
/// # Safety
/// b is from tdpt_buddy_open; rect has 4; cells has w * h.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_ground(b: *mut Buddy, rect: *const i32, cells: *const u16) {
    let b = unsafe { &mut *b };
    let rect: [i32; 4] = unsafe { std::ptr::read(rect as *const [i32; 4]) };
    let n = (rect[2].max(0) as usize) * (rect[3].max(0) as usize);
    if let Ok(mut g) = b.ground.lock() {
        g.rect = rect;
        g.cells.clear();
        g.cells.extend_from_slice(unsafe { std::slice::from_raw_parts(cells, n) });
    }
}

/// His toys' patches of the sim (x, y, w, h each, sim pixels, up to max) for tdpt_buddy_toy this frame; how many
/// # Safety
/// b is from tdpt_buddy_open; rects has 4 * max.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_toys(b: *mut Buddy, rects: *mut i32, max: i32) -> i32 {
    let b = unsafe { &mut *b };
    let Ok(mut g) = b.ground.lock() else { return 0 };
    g.patches.clear();
    let n = g.toys_at.len().min(max.max(0) as usize);
    unsafe { std::ptr::copy_nonoverlapping(g.toys_at.as_ptr() as *const i32, rects, n * 4) };
    n as i32
}

/// One toy's patch: the sim over rect as in tdpt_buddy_ground, its air over air_rect as in tdpt_buddy_air
/// # Safety
/// b is from tdpt_buddy_open; rect and air_rect have 4; cells w * h; air 3 per air cell.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_toy(b: *mut Buddy, rect: *const i32, cells: *const u16, air_rect: *const i32, air: *const f32) {
    let b = unsafe { &mut *b };
    let (rect, air_rect): ([i32; 4], [i32; 4]) = unsafe { (std::ptr::read(rect as *const _), std::ptr::read(air_rect as *const _)) };
    let n = |r: [i32; 4]| (r[2].max(0) as usize) * (r[3].max(0) as usize);
    let p = Patch { rect, cells: unsafe { std::slice::from_raw_parts(cells, n(rect)) }.to_vec(), air_rect, air: unsafe { std::slice::from_raw_parts(air as *const [f32; 3], n(air_rect)) }.to_vec() };
    if let Ok(mut g) = b.ground.lock() {
        g.patches.push(p);
    }
}

/// The sim's air over rect (x, y, w, h in cells of `cell` sim pixels): w * h of vx, vy, pressure
/// # Safety
/// b is from tdpt_buddy_open; rect has 4; v has 3 * w * h.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_air(b: *mut Buddy, cell: i32, rect: *const i32, v: *const f32) {
    let b = unsafe { &mut *b };
    let rect: [i32; 4] = unsafe { std::ptr::read(rect as *const [i32; 4]) };
    let n = (rect[2].max(0) as usize) * (rect[3].max(0) as usize);
    if let Ok(mut g) = b.ground.lock() {
        g.cell = cell;
        g.air_rect = rect;
        g.air.clear();
        g.air.extend(unsafe { std::slice::from_raw_parts(v as *const [f32; 3], n) });
    }
}

/// TPT's gravity where he is, 1 = normal (straight down is 0, 1)
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_gravity(b: *mut Buddy, gx: f64, gy: f64) {
    let b = unsafe { &mut *b };
    if let Ok(mut g) = b.ground.lock()
        && gx.is_finite()
        && gy.is_finite()
    {
        g.gravity = (gx, gy);
    }
}

/// Where the portal in his reach (a PORTAL in his ground map) comes out, sim pixels; has = 0: none
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_portal(b: *mut Buddy, has: i32, x: f64, y: f64) {
    let b = unsafe { &mut *b };
    if let Ok(mut g) = b.ground.lock() {
        g.exit = (has != 0 && x.is_finite() && y.is_finite()).then_some((x, y));
    }
}

/// The active window's edges he can stand on, stage pixels: its left and right, its top edge and its inside bottom edge
/// (NaN: that one is off); id 0: none (as in the desktop app, only the active window)
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_ledge(b: *mut Buddy, id: u64, left: f64, right: f64, top: f64, bottom: f64) {
    let b = unsafe { &mut *b };
    if let Ok(mut g) = b.ground.lock() {
        g.set_ledge((id != 0 && left.is_finite() && right > left).then_some(Ledge { id, left, right, rows: [top, bottom] }));
    }
}

/// His body parts (up to max): x, y, radius, xv, yv, 1 burning each, stage pixels (per game frame); how many
/// # Safety
/// b is from tdpt_buddy_open; out has room for 6 * max.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_parts(b: *mut Buddy, out: *mut f64, max: usize) -> usize {
    let b = unsafe { &mut *b };
    let Ok(g) = b.ground.lock() else { return 0 };
    let n = g.parts.len().min(max);
    unsafe { std::ptr::copy_nonoverlapping(g.parts.as_ptr() as *const f64, out, n * 6) };
    n
}

/// The game's explosions since the last call (up to max): x, y, power each (1 = its bomb); how many
/// # Safety
/// b is from tdpt_buddy_open; out has room for 3 * max.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_booms(b: *mut Buddy, out: *mut f64, max: usize) -> usize {
    let b = unsafe { &mut *b };
    let Ok(mut g) = b.ground.lock() else { return 0 };
    let n = g.booms.len().min(max);
    unsafe { std::ptr::copy_nonoverlapping(g.booms.as_ptr() as *const f64, out, n * 3) };
    g.booms.clear();
    n
}

/// His weapons since the last call (up to max): x0, y0, x1, y1, kind each (1 a bullet's path, 2 a flame at x0, y0, 3 he
/// dances at x0, y0, 4 a new drop from his hose at x0, y0 with speed x1, y1 a frame, 5 his gravity pulls to x0, y0, x1
/// hard: 1 weak vortex, 2 gravity shifter, 3 strong vortex, 6 his stun gun is shocking him, 7 a toy of his
/// stopped fast by the sim at x0, y0, radius x1, speed y1 before, 8 the Fist at x0, y0 moving x1, y1 since the frame
/// before, 9 a piece of gore at x0, y0 flying x1, y1 a frame, 10 the flail's ball at x0, y0 moving x1, y1 a frame, 11 the
/// game's Earthquake shaking x0 hard, 12 its Low Gravity (x0 of normal), 13 a missile at x0, y0 moving x1, y1 a frame, 14 a
/// script asking for x1 of the TPT element named by the bytes of y1 at x0, y0, 15 his radio playing at x0, y0, 16 he's
/// sad: his head at x0, y0, radius x1, how sad y1 (0-1), every third of a second); how many
/// # Safety
/// b is from tdpt_buddy_open; out has room for 5 * max.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_shots(b: *mut Buddy, out: *mut f64, max: usize) -> usize {
    let b = unsafe { &mut *b };
    let Ok(mut g) = b.ground.lock() else { return 0 };
    let n = g.shots.len().min(max);
    unsafe { std::ptr::copy_nonoverlapping(g.shots.as_ptr() as *const f64, out, n * 5) };
    g.shots.clear();
    n
}

/// what: 1 left button down, 2 up, at (x, y) stage pixels
/// # Safety
/// b is from tdpt_buddy_open.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_mouse(b: *mut Buddy, x: f64, y: f64, what: i32) {
    let b = unsafe { &mut *b };
    let Ok(mut s) = b.shared.lock() else { return };
    s.mouse = (x, y);
    s.clicks.push(if what == 1 {
        PlayerEvent::MouseDown { x, y, button: MouseButton::Left, index: None }
    } else {
        PlayerEvent::MouseUp { x, y, button: MouseButton::Left }
    });
}

/// # Safety
/// b is from tdpt_buddy_open, or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn tdpt_buddy_close(b: *mut Buddy) {
    if b.is_null() {
        return;
    }
    let mut b = unsafe { Box::from_raw(b) };
    if let Ok(mut s) = b.shared.lock() {
        s.quit = true;
    }
    if let Some(t) = b.thread.take() {
        t.join().ok();
    }
}
