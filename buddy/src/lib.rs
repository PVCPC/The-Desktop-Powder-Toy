//! Interactive Buddy inside TDPT: the user's own copy of the game, patched in memory with our bridge code
//! (bridge/*.patch: only the tags we add, none of the game) and run by Ruffle, drawn offscreen into TDPT's overlay
//! frame. C interface in tdpt_buddy.h.
use ruffle_core::events::{MouseButton, PlayerEvent};
use ruffle_core::external::Value;
use ruffle_core::{FloatDuration, Player, PlayerBuilder, RootVars, StageAlign, StageScaleMode, ViewportDimensions};
use ruffle_render_wgpu::{backend::WgpuRenderBackend, target::TextureTarget, wgpu};
use sha2::{Digest, Sha256};
use std::ffi::{CStr, c_char};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

// SHA-256 of Interactive Buddy v1.01 (uncompressed), the only version the bridge fits
const ORIGINAL: &str = "76da8a05bfe3472afe4a2bca2c09486b5be614881d45621489fa2f2686d16b0f";
const PATCHES: [&[u8]; 2] = [include_bytes!("../bridge/enhanced.patch"), include_bytes!("../bridge/native.patch")];

fn hex(data: &[u8]) -> String {
    data.iter().map(|b| format!("{b:02x}")).collect()
}

fn u32_at(d: &[u8], o: usize) -> usize {
    u32::from_le_bytes(d[o..o + 4].try_into().unwrap()) as usize
}

// the header's length (after the 8-byte signature) and the raw tags
fn split(body: &[u8]) -> (usize, Vec<&[u8]>) {
    let header = (5 + 4 * (body[0] as usize >> 3) + 7) / 8 + 4;
    let (mut at, mut tags) = (header, Vec::new());
    while at < body.len() {
        let code = u16::from_le_bytes([body[at], body[at + 1]]) as usize;
        let (start, len) = if code & 63 == 63 { (at + 6, u32_at(body, at + 2)) } else { (at + 2, code & 63) };
        let end = (start + len).min(body.len());
        tags.push(&body[at..end]);
        at = end;
    }
    (header, tags)
}

// a patch: "TDPTSWF1", SHA-256 of the result, its frame count (u16), then insertions (before old tag index, bytes)
fn apply(swf: &[u8], patch: &[u8]) -> Vec<u8> {
    assert_eq!(&patch[..8], b"TDPTSWF1");
    let (header, old) = split(&swf[8..]);
    let mut body = swf[8..8 + header].to_vec();
    body[header - 2..].copy_from_slice(&patch[40..42]);
    let (count, mut at) = (u32_at(patch, 42), 46);
    let mut adds = Vec::new();
    for _ in 0..count {
        let (before, len) = (u32_at(patch, at), u32_at(patch, at + 4));
        adds.push((before, &patch[at + 8..at + 8 + len]));
        at += 8 + len;
    }
    for i in 0..=old.len() {
        adds.iter().filter(|a| a.0 == i).for_each(|a| body.extend_from_slice(a.1));
        if let Some(tag) = old.get(i) {
            body.extend_from_slice(tag);
        }
    }
    let mut out = [b"FWS", &swf[3..4], &(body.len() as u32 + 8).to_le_bytes()[..]].concat();
    out.extend(body);
    out
}

fn patch(mut swf: Vec<u8>) -> Result<Vec<u8>, String> {
    if swf.starts_with(b"CWS") {
        let mut body = Vec::new();
        std::io::Read::read_to_end(&mut flate2::read::ZlibDecoder::new(&swf[8..]), &mut body).map_err(|e| e.to_string())?;
        swf = [b"FWS", &swf[3..4], &(body.len() as u32 + 8).to_le_bytes()[..], &body].concat();
    }
    if hex(&Sha256::digest(&swf)) != ORIGINAL {
        return Err("That file isn't Interactive Buddy v1.01".into());
    }
    for p in PATCHES {
        swf = apply(&swf, p);
        assert_eq!(Sha256::digest(&swf)[..], p[8..40], "bridge patch");
    }
    Ok(swf)
}

// the game, on its own thread
struct Game {
    player: Arc<Mutex<Player>>,
    ground: Arc<Mutex<Ground>>,
    size: (u32, u32),
    mouse: (f64, f64),
    ready: bool,
    readback: Option<wgpu::Buffer>,
    settings: [f64; 5], // from the Buddy tab: size %, speech on, speech size %, toys at once (0: no limit), smooth on
    applied: [f64; 5],  // what the game has
}

fn open(path: &str, w: u32, h: u32) -> Result<Game, String> {
    let data = std::fs::read(path).map_err(|e| format!("Can't read {path}: {e}"))?;
    let swf = patch(data)?;
    let movie = ruffle_core::tag_utils::SwfMovieData::from_data(&swf, "file:///tdpt/buddy.swf".into(), None, None)
        .map_err(|e| e.to_string())?;
    let renderer = WgpuRenderBackend::for_offscreen((w, h), wgpu::Backends::PRIMARY, wgpu::PowerPreference::LowPower)
        .map_err(|e| e.to_string())?;
    let player = PlayerBuilder::new()
        .with_movie(movie)
        .with_autoplay(true)
        .with_renderer(renderer)
        .with_viewport_dimensions(w, h, 1.0)
        .with_scale_mode(StageScaleMode::NoScale, true)
        .with_align(StageAlign::TOP | StageAlign::LEFT, true)
        .with_storage(Box::new(Saves("buddy".into())))
        .build();
    load_fonts(&mut player.lock().unwrap());
    Ok(Game { player, ground: Default::default(), size: (w, h), mouse: (-1.0, -1.0), ready: false, readback: None, settings: SETTINGS, applied: [f64::NAN; 5] })
}

// The sim around him, so he stands on it, floats in it and burns in it: one byte per sim pixel over rect (x, y, w, h sim
// pixels, which are his stage pixels). Low 5 bits: 0 nothing, 1 solid for him, 2 + w/4 liquid of TPT weight w, ACID,
// PORTAL, EATER; then
// WARM, HOT or COLD (2 bits), SPARK, HURT and BANG. Rect (x, y, w, h sim pixels, which are his stage pixels). TDPT fills it each frame with the part he asks for (want).
struct Ground {
    rect: [i32; 4],
    cells: Vec<u16>,
    want: [i32; 4],
    last: (f64, f64), // his body, last frame
    air_rect: [i32; 4], // in sim cells (CELL pixels)
    air: Vec<[f32; 3]>, // the sim's air velocity and pressure there
    cell: i32,
    before: ([i32; 4], Vec<f32>), // the pressure last frame (air_rect, values)
    calm: u32,                    // frames until the next blast may go off
    zap: Option<(f64, f64)>,      // a part touching a spark this frame: the game's shock() goes off there
    gravity: (f64, f64),          // TPT's gravity where he is, 1 = the game's own (down: 0, 1)
    exit: Option<(f64, f64)>,     // where the portal in his reach comes out
    jumped: u32,                  // frames since he went through one (not straight back in)
    eaten: bool,                  // swallowed this frame: he comes back at the top
    parts: Vec<[f64; 6]>,         // his parts after this frame: x, y, radius, xv, yv, 1 burning (for the sim to feel him)
    bang: Option<(f64, f64)>,     // a part touching something that blows up on him: the game's explode() there
    booms: Vec<[f64; 3]>,         // the game's explosions since TDPT last asked: x, y, power (1 = its bomb)
    game_grav: f64,               // the game's grav this frame, before that
    ledge: Option<Ledge>,         // the active window's edges he can stand on (as in the desktop app)
    carry: (f64, f64),            // how far that window moved while he stood on it, not yet applied
    support: u8,                  // what he stands on: 0 nothing, 1 its top edge, 2 its bottom edge
}

// The desktop app's one-way ledges (PlatformBridge.as): the active window's top edge from outside and its bottom
// edge from inside, stage pixels; a row is NaN when it's off screen or too near the top
#[derive(Clone, Copy, PartialEq)]
struct Ledge {
    id: u64,
    left: f64,
    right: f64,
    rows: [f64; 2],
}

impl Ledge {
    // which row (1 top, 2 bottom) a foot rests on: within 4 px, not going up fast
    fn foot(&self, x: f64, y: f64, r: f64, yv: f64) -> u8 {
        let over = x + r > self.left && x - r < self.right && yv >= -2.0;
        (1..=2).find(|&k| over && (y + r - self.rows[k as usize - 1]).abs() <= 4.0).unwrap_or(0)
    }
}

const WARM: u16 = 0x20; // 60 C and up: hurts
const HOT: u16 = 0x40; // 400 C and up: sets him on fire
const COLD: u16 = 0x60; // -50 C and down: slows him, hurts
const SPARK: u16 = 0x80; // electricity (a spark or an electron): shocks him
const HURT: u16 = 0x100; // caustic gas, radiation, virus: hurts like acid
const BANG: u16 = 0x200; // DEST, BOMB, a singularity: blows up on him
const WINDOW: u16 = 0x400; // a window or icon: its sides stop him like they stop powder; he's in front of it otherwise
const ACID: u16 = 31; // a kind: acid, a liquid (TPT weight 10) that eats him
const EATER: u16 = 30; // a kind: VOID or a black hole, which swallows him (he comes back at the top)
const PORTAL: u16 = 29; // a kind: a portal in, which moves him to its way out (Ground::exit)
const PARTS: [&str; 6] = ["body", "head", "lArm", "rArm", "lLeg", "rLeg"];
// where his parts sit around the body when he stands (from the game), to put him back together
const POSE: [(f64, f64); 6] = [(0.0, 0.0), (0.0, -39.4), (-22.1, -10.2), (22.1, -10.2), (-15.1, 32.0), (15.0, 32.0)];

impl Default for Ground {
    fn default() -> Self {
        Self { rect: [0; 4], cells: Vec::new(), want: [0; 4], last: (400.0, 300.0), air_rect: [0; 4], air: Vec::new(), cell: 4, before: ([0; 4], Vec::new()), calm: 0, zap: None, gravity: (0.0, 1.0), game_grav: 1.0, exit: None, jumped: 99, eaten: false, parts: Vec::new(), bang: None, booms: Vec::new(), ledge: None, carry: (0.0, 0.0), support: 0 }
    }
}

impl Ground {
    fn kind(&self, x: i32, y: i32) -> u8 {
        (self.cell(x, y) & 0x1F) as u8
    }

    // what a circle touches (within a pixel): 1 << WARM, HOT, COLD (as 1, 2, 3) for each one there, SPARK, 1 for
    // acid, 0x10 a portal, 0x20 an eater, HURT, BANG
    fn touch(&self, x: f64, y: f64, r: f64) -> u16 {
        let r = r + 1.0;
        let mut seen = 0;
        for py in (y - r).floor() as i32..=(y + r).ceil() as i32 {
            for px in (x - r).floor() as i32..=(x + r).ceil() as i32 {
                let (dx, dy) = (px as f64 + 0.5 - x, py as f64 + 0.5 - y);
                if dx * dx + dy * dy <= r * r {
                    let c = self.cell(px, py);
                    seen |= (1 << (c >> 5 & 3)) & 0x0E | c & (SPARK | HURT | BANG) | (c & 0x1F == ACID) as u16;
                    seen |= (c & 0x1F == PORTAL) as u16 * 0x10 | (c & 0x1F == EATER) as u16 * 0x20;
                }
            }
        }
        seen
    }

    fn cell(&self, x: i32, y: i32) -> u16 {
        let [rx, ry, w, h] = self.rect;
        let (u, v) = (x - rx, y - ry);
        if u >= 0 && v >= 0 && u < w && v < h { self.cells.get((v * w + u) as usize).copied().unwrap_or(0) } else { 0 }
    }

    // the sim's air velocity at a pixel
    fn wind(&self, x: f64, y: f64) -> (f64, f64) {
        let [rx, ry, w, h] = self.air_rect;
        let (u, v) = ((x as i32).div_euclid(self.cell.max(1)) - rx, (y as i32).div_euclid(self.cell.max(1)) - ry);
        if u < 0 || v < 0 || u >= w || v >= h {
            return (0.0, 0.0);
        }
        self.air.get((v * w + u) as usize).map_or((0.0, 0.0), |a| (a[0] as f64, a[1] as f64))
    }

    // A TPT explosion near him: the sim's pressure jumping by more than 6 in a frame. Where it jumped most (sim pixels)
    // and how hard, as the game's explode() power (1 = one of its own bombs, at a jump of about 60). One per 10 frames.
    fn blast(&mut self) -> Option<(f64, f64, f64)> {
        let [rx, ry, w, h] = self.air_rect;
        let (old_rect, old) = &self.before;
        let mut best = (6.0f32, 0, 0);
        for v in 0..h {
            for u in 0..w {
                let (ou, ov) = (u + rx - old_rect[0], v + ry - old_rect[1]);
                if ou < 0 || ov < 0 || ou >= old_rect[2] || ov >= old_rect[3] {
                    continue;
                }
                let jump = self.air[(v * w + u) as usize][2] - old[(ov * old_rect[2] + ou) as usize];
                if jump > best.0 {
                    best = (jump, u, v);
                }
            }
        }
        self.before = (self.air_rect, self.air.iter().map(|a| a[2]).collect());
        self.calm = self.calm.saturating_sub(1);
        if best.0 <= 6.0 || self.calm > 0 {
            return None;
        }
        self.calm = 10;
        let c = self.cell as f64;
        Some(((best.1 + rx) as f64 * c + c / 2.0, (best.2 + ry) as f64 * c + c / 2.0, (best.0 as f64 / 60.0).min(2.0)))
    }

    fn solid(&self, x: i32, y: i32) -> bool {
        self.kind(x, y) == 1
    }

    // a part's circle touches a window
    fn window(&self, x: f64, y: f64, r: f64) -> bool {
        let r2 = r * r;
        (-(r as i32)..=r as i32).any(|dy| (-(r as i32)..=r as i32).any(|dx| (dx * dx + dy * dy) as f64 <= r2 && self.cell(x as i32 + dx, y as i32 + dy) & WINDOW != 0))
    }

    // the window the desktop app let him stand on changed or moved: if he stood on it, he rides it
    fn set_ledge(&mut self, next: Option<Ledge>) {
        if let (Some(old), Some(new)) = (self.ledge, next)
            && old.id == new.id
            && self.support != 0
            && old.rows[self.support as usize - 1].is_finite()
            && new.rows[self.support as usize - 1].is_finite()
        {
            let k = self.support as usize - 1;
            self.carry.0 += new.left - old.left;
            self.carry.1 += new.rows[k] - old.rows[k];
        }
        self.ledge = next;
    }

    // how much of a circle is in liquid (0-1), and that liquid's mean TPT weight
    fn wet(&self, x: f64, y: f64, r: f64) -> (f64, f64) {
        let (mut all, mut n, mut w) = (0, 0, 0.0);
        for py in (y - r).floor() as i32..=(y + r).ceil() as i32 {
            for px in (x - r).floor() as i32..=(x + r).ceil() as i32 {
                let (dx, dy) = (px as f64 + 0.5 - x, py as f64 + 0.5 - y);
                if dx * dx + dy * dy <= r * r {
                    all += 1;
                    if let c @ (2..=28 | 31) = self.kind(px, py) {
                        n += 1;
                        w += if c as u16 == ACID { 10.0 } else { (c - 2) as f64 * 4.0 };
                    }
                }
            }
        }
        if n == 0 { (0.0, 0.0) } else { (n as f64 / all as f64, w / n as f64) }
    }

    // A part in liquid: lifted by what it pushes aside (as heavy as 1.2 x water: he floats in water with his top out,
    // high in mercury, sinks slowly in oil) and slowed by it. grav = the game's own gravity per frame.
    fn float(&self, x: f64, y: f64, xv: &mut f64, yv: &mut f64, r: f64, grav: f64) -> bool {
        let (frac, weight) = self.wet(x, y, r);
        if frac == 0.0 {
            return false;
        }
        *yv -= grav * frac * weight / 30.0 * 1.2;
        let drag = 1.0 - 0.12 * frac * (weight / 30.0).min(2.0);
        *xv *= drag;
        *yv *= drag;
        true
    }

    // the solid pixels inside a circle: how many, and the sum of their offsets from its centre
    fn overlap(&self, x: f64, y: f64, r: f64) -> (u32, f64, f64) {
        let (mut n, mut sx, mut sy) = (0, 0.0, 0.0);
        for py in (y - r).floor() as i32..=(y + r).ceil() as i32 {
            for px in (x - r).floor() as i32..=(x + r).ceil() as i32 {
                let (dx, dy) = (px as f64 + 0.5 - x, py as f64 + 0.5 - y);
                if dx * dx + dy * dy <= r * r && self.solid(px, py) {
                    n += 1;
                    sx += dx;
                    sy += dy;
                }
            }
        }
        (n, sx, sy)
    }

    // A body part (circle) inside solid: out the way away from the solid, a pixel at a time, and its speed into the
    // surface bounced like the game's own floor (half back, small speeds stop). The way out, if it was in.
    // ponytail: a part buried deeper than its size stays put (sand dumped on him); dig out if that looks wrong
    fn push_out(&self, x: &mut f64, y: &mut f64, xv: &mut f64, yv: &mut f64, r: f64) -> Option<(f64, f64)> {
        let (n, sx, sy) = self.overlap(*x, *y, r);
        if n == 0 {
            return None;
        }
        let len = sx.hypot(sy);
        let (nx, ny) = if len > 1e-9 { (-sx / len, -sy / len) } else { (0.0, -1.0) };
        for _ in 0..(2.0 * r) as usize + 2 {
            if self.overlap(*x, *y, r).0 == 0 {
                break;
            }
            *x += nx;
            *y += ny;
        }
        let into = *xv * nx + *yv * ny;
        if into < 0.0 {
            let k = if into > -2.0 { 1.0 } else { 1.5 };
            *xv -= k * into * nx;
            *yv -= k * into * ny;
            if ny < -0.5 {
                *xv *= 0.9; // friction on what he lands on
            }
        }
        Some((nx, ny))
    }

    // something solid just under a foot (within 2 pixels)
    fn under(&self, x: f64, y: f64, r: f64) -> bool {
        (-(r as i32) / 2..=(r as i32) / 2).any(|dx| (1..=2).any(|dy| self.solid(x as i32 + dx, (y + r) as i32 + dy)))
    }
}

fn num(v: &mut dyn RootVars, name: &str) -> f64 {
    match v.get(name) {
        Value::Number(n) => n,
        _ => f64::NAN,
    }
}

// After the game moves his parts each frame, the sim pushes them out of itself; standing on it counts as standing.
fn hook_ground(p: &mut Player, ground: &Arc<Mutex<Ground>>) {
    let g = ground.clone();
    p.hook_after_root_function("doBodyPhysics", Box::new(move |v| {
        let Ok(mut g) = g.lock() else { return };
        let held = v.get("fistDrag") == Value::Number(1.0); // held: he goes where the hand goes
        let grav = num(v, "grav");
        // a part the game lost (NaN: its own divide by 0, or a save that kept one) stays lost and is saved that way:
        // stand him up again where he last was
        if PARTS.iter().any(|part| ["x", "y", "ox", "oy", "xv", "yv"].iter().any(|f| !num(v, &format!("{part}.{f}")).is_finite())) {
            let (bx, by) = g.last;
            for (part, (dx, dy)) in PARTS.iter().zip(POSE) {
                for (f, n) in [("x", bx + dx), ("y", by + dy), ("ox", bx + dx), ("oy", by + dy), ("xv", 0.0), ("yv", 0.0)] {
                    v.set(&format!("{part}.{f}"), Value::Number(n));
                }
            }
        }
        // the window he stands on moved: he moves with it (all of him, speed kept)
        let (cx, cy) = std::mem::take(&mut g.carry);
        if !held && (cx != 0.0 || cy != 0.0) {
            for part in PARTS {
                for (f, d) in [("x", cx), ("y", cy), ("ox", cx), ("oy", cy), ("oox", cx), ("ooy", cy)] {
                    let n = num(v, &format!("{part}.{f}"));
                    if n.is_finite() {
                        v.set(&format!("{part}.{f}"), Value::Number(n + d));
                    }
                }
            }
        }
        g.support = 0;
        g.last = (num(v, "body.x"), num(v, "body.y"));
        let mut bbox = [f64::MAX, f64::MAX, f64::MIN, f64::MIN];
        let mut standing = false;
        let (mut hot, mut warm, mut doused, mut cold, mut zap) = (false, 0, false, 0, None);
        let mut portal = false;
        g.parts.clear();
        for part in PARTS {
            let (mut x, mut y) = (num(v, &format!("{part}.x")), num(v, &format!("{part}.y")));
            let (mut xv, mut yv, r) = (num(v, &format!("{part}.xv")), num(v, &format!("{part}.yv")), num(v, &format!("{part}.rad")));
            if [x, y, xv, yv, r].iter().any(|n| !n.is_finite()) || r <= 0.0 {
                continue;
            }
            let wet = !held && grav.is_finite() && g.float(x, y, &mut xv, &mut yv, r, grav);
            let touch = g.touch(x, y, r);
            portal |= touch & 0x10 != 0;
            g.eaten |= touch & 0x20 != 0;
            warm += (touch & 1 != 0 || touch & HURT != 0) as u32 * 2; // acid, caustic gas, radiation hurt twice as much as heat
            if touch & BANG != 0 {
                g.bang = Some((x, y));
            }
            hot |= touch & 1 << (HOT >> 5) != 0;
            warm += (touch & (1 << (WARM >> 5) | 1 << (HOT >> 5)) != 0) as u32;
            doused |= touch & 1 << (HOT >> 5) == 0 && g.wet(x, y, r).0 > 0.3;
            if touch & 1 << (COLD >> 5) != 0 && !held {
                cold += 1;
                xv *= 0.85; // stiff with cold
                yv *= 0.85;
            }
            if touch & SPARK != 0 {
                zap = Some((x, y));
            }
            // the sim's air pushes him like a heavy powder (sand follows it at 0.7, he at 0.1); blasts throw him
            let (ax, ay) = if held { (0.0, 0.0) } else { g.wind(x, y) };
            let blown = ax != 0.0 || ay != 0.0;
            xv += 0.1 * ax;
            yv += 0.1 * ay;
            if !held && g.gravity.0 != 0.0 && g.game_grav.is_finite() {
                xv += g.game_grav * g.gravity.0;
            }
            let blown = blown || g.gravity.0 != 0.0;
            // a window's side stops him going into it sideways (powder too); from above he's in front of it
            let (ox, oy) = (num(v, &format!("{part}.ox")), num(v, &format!("{part}.oy")));
            let side = !held && ox.is_finite() && oy.is_finite() && (x - ox).abs() <= xv.abs() + 10.0 && g.window(x, oy, r) && !g.window(ox, oy, r);
            if side {
                x = ox;
                xv *= -0.3;
            }
            let out = if held { None } else { g.push_out(&mut x, &mut y, &mut xv, &mut yv, r) };
            // landing on the active window's top or bottom edge from above, as in the desktop app (not when put
            // somewhere: setBuddyXY, a portal)
            let mut landed = side;
            if let Some(l) = g.ledge.filter(|_| !held && yv >= 0.0 && oy.is_finite() && x + r > 0.0) {
                if let Some(k) = (0..2).find(|&k| x + r > l.left && x - r < l.right && oy + r <= l.rows[k] + 2.0 && y + r >= l.rows[k] && y - oy <= yv.abs() + 10.0) {
                    y = l.rows[k] - r;
                    yv = if yv.abs() < 2.0 { 0.0 } else { -yv * 0.25 };
                    g.support = k as u8 + 1;
                    landed = true;
                }
            }
            standing |= part.ends_with("Leg") && landed;
            if wet || blown || cold > 0 || out.is_some() || landed {
                for (f, n) in [("x", x), ("y", y), ("xv", xv), ("yv", yv)] {
                    v.set(&format!("{part}.{f}"), Value::Number(n));
                }
            }
            standing |= part.ends_with("Leg") && out.is_some_and(|(_, ny)| ny < -0.5);
            let fire = v.get(&format!("{part}.onFire")) == Value::Bool(true);
            g.parts.push([x, y, r, xv, yv, fire as u8 as f64]);
            let reach = r + 64.0 + xv.abs().max(yv.abs()) * 2.0; // what he can touch by the next frame
            bbox = [bbox[0].min(x - reach), bbox[1].min(y - reach), bbox[2].max(x + reach), bbox[3].max(y + reach)];
        }
        if let Some(l) = g.ledge.filter(|_| !held) {
            for leg in ["lLeg", "rLeg"] {
                let f = l.foot(num(v, &format!("{leg}.x")), num(v, &format!("{leg}.y")), num(v, &format!("{leg}.rad")), num(v, &format!("{leg}.yv")));
                if f != 0 {
                    g.support = f;
                }
            }
        }
        if standing {
            v.set("onGround", Value::Bool(true));
        }
        // fire: the game spreads it from his body to his head and legs (sad, and cash, like its own fire); it never goes
        // out by itself, but a cool liquid puts it out (old = new, so the game doesn't count it as catching fire)
        let burning = v.get("body.onFire") == Value::Bool(true);
        if hot && !burning && v.get("body.flammable") != Value::Bool(false) {
            v.set("body.onFire", Value::Bool(true));
            v.set("body.fireStrength", Value::Number(100.0));
        } else if doused && !hot && burning {
            for part in PARTS {
                for f in ["onFire", "oldOnFire"] {
                    v.set(&format!("{part}.{f}"), Value::Bool(false));
                }
                v.set(&format!("{part}.fireStrength"), Value::Number(0.0));
            }
        }
        // heat, cold and acid hurt a little each frame per part touching them (the game's addEmotion(-10) is about -6.5)
        if warm + cold > 0 {
            let e = num(v, "emotion");
            if e.is_finite() {
                v.set("emotion", Value::Number((e - 0.03 * (warm + cold) as f64).max(-100.0)));
            }
        }
        g.zap = zap;
        // through a portal: every part moves by the same amount, speed kept
        g.jumped = g.jumped.saturating_add(1);
        if portal && !held && g.jumped > 20 && let Some((ex, ey)) = g.exit {
            let (dx, dy) = (ex - num(v, "body.x"), ey - num(v, "body.y"));
            if dx.is_finite() && dy.is_finite() {
                for part in PARTS {
                    for (f, d) in [("x", dx), ("y", dy), ("ox", dx), ("oy", dy)] {
                        let n = num(v, &format!("{part}.{f}"));
                        v.set(&format!("{part}.{f}"), Value::Number(n + d));
                    }
                }
                g.jumped = 0;
            }
        }
        if bbox[0] <= bbox[2] {
            g.want = [bbox[0] as i32, bbox[1] as i32, (bbox[2] - bbox[0]) as i32 + 1, (bbox[3] - bbox[1]) as i32 + 1];
        }
    }))
    .ok();
    let g = ground.clone();
    p.hook_after_root_function("setBasicVars", Box::new(move |v| {
        let Ok(mut g) = g.lock() else { return };
        // TPT's gravity where he is: the game's own (down, grav per frame) scaled by its downward part; the sideways
        // part is added in doBodyPhysics
        g.game_grav = num(v, "grav");
        if g.gravity != (0.0, 1.0) && g.game_grav.is_finite() {
            v.set("grav", Value::Number(g.game_grav * g.gravity.1));
        }
        let on = ["lLeg", "rLeg"].iter().any(|leg| {
            let (x, y, r) = (num(v, &format!("{leg}.x")), num(v, &format!("{leg}.y")), num(v, &format!("{leg}.rad")));
            x.is_finite() && y.is_finite() && r > 0.0 && (g.under(x, y, r) || g.ledge.is_some_and(|l| l.foot(x, y, r, num(v, &format!("{leg}.yv"))) != 0))
        });
        if on {
            v.set("onGround", Value::Bool(true));
        }
    }))
    .ok();
    // the game's own explosions (bombs, grenades, scripts) go to TPT as a blast; not the ones TPT itself set off
    let g = ground.clone();
    p.hook_after_root_function("explode", Box::new(move |v| {
        if OURS.load(Ordering::Relaxed) {
            return;
        }
        let Ok(mut g) = g.lock() else { return };
        let (x, y, power) = (num(v, "explodex"), num(v, "explodey"), num(v, "explodep"));
        if x.is_finite() && y.is_finite() && power > 0.0 && g.booms.len() < 16 {
            g.booms.push([x, y, power.min(5.0)]);
            g.calm = 30; // its pressure isn't another explosion for him
        }
    }))
    .ok();
}

static OURS: AtomicBool = AtomicBool::new(false); // TPT's blast is calling the game's explode()

// The game's saves (his money, what's been bought), a file each in this folder: TPT runs in its data folder, so
// they sit next to its own.
struct Saves(std::path::PathBuf);

impl Saves {
    fn file(&self, name: &str) -> std::path::PathBuf {
        let name: String = name.chars().map(|c| if c.is_ascii_alphanumeric() || "-_.".contains(c) { c } else { '_' }).collect();
        self.0.join(name + ".sol")
    }
}

impl ruffle_core::backend::storage::StorageBackend for Saves {
    fn get(&self, name: &str) -> Option<Vec<u8>> {
        std::fs::read(self.file(name)).ok()
    }
    fn put(&mut self, name: &str, value: &[u8]) -> bool {
        // written beside, then moved over, so a crash mid-save keeps the last good one
        // a temp file per writer: two players writing the same one mix their bytes into a broken save
        let (file, part) = (self.file(name), self.file(&format!("{name}.{}.{:?}.part", std::process::id(), std::thread::current().id())));
        std::fs::create_dir_all(&self.0).is_ok() && std::fs::write(&part, value).is_ok() && std::fs::rename(&part, &file).is_ok()
    }
    fn remove_key(&mut self, name: &str) {
        std::fs::remove_file(self.file(name)).ok();
    }
}

// the game's text uses these device fonts; Ruffle's own default stands in for any missing
fn load_fonts(player: &mut Player) {
    use ruffle_core::backend::ui::FontDefinition;
    use ruffle_core::font::FontFileData;
    #[cfg(windows)]
    let (dir, files) = (
        std::path::PathBuf::from(std::env::var_os("WINDIR").unwrap_or_else(|| "C:\\Windows".into())).join("Fonts"),
        [
            ("Arial", false, false, "arial.ttf"), ("Arial", true, false, "arialbd.ttf"), ("Arial", false, true, "ariali.ttf"),
            ("Arial", true, true, "arialbi.ttf"), ("Arial Black", false, false, "ariblk.ttf"), ("Comic Sans MS", false, false, "comic.ttf"),
            ("Comic Sans MS", true, false, "comicbd.ttf"), ("Verdana", false, false, "verdana.ttf"), ("Verdana", true, false, "verdanab.ttf"),
            ("Tahoma", false, false, "tahoma.ttf"), ("Tahoma", true, false, "tahomabd.ttf"), ("Times New Roman", false, false, "times.ttf"),
            ("Courier New", false, false, "cour.ttf"),
        ],
    );
    #[cfg(not(windows))]
    let (dir, files) = (
        std::path::PathBuf::from("/System/Library/Fonts/Supplemental"),
        [
            ("Arial", false, false, "Arial.ttf"), ("Arial", true, false, "Arial Bold.ttf"), ("Arial", false, true, "Arial Italic.ttf"),
            ("Arial", true, true, "Arial Bold Italic.ttf"), ("Arial Black", false, false, "Arial Black.ttf"), ("Comic Sans MS", false, false, "Comic Sans MS.ttf"),
            ("Comic Sans MS", true, false, "Comic Sans MS Bold.ttf"), ("Verdana", false, false, "Verdana.ttf"), ("Verdana", true, false, "Verdana Bold.ttf"),
            ("Tahoma", false, false, "Tahoma.ttf"), ("Tahoma", true, false, "Tahoma Bold.ttf"), ("Times New Roman", false, false, "Times New Roman.ttf"),
            ("Courier New", false, false, "Courier New.ttf"),
        ],
    );
    for (name, bold, italic, file) in files {
        if let Ok(data) = std::fs::read(dir.join(file)) {
            player.register_device_font(FontDefinition::FontFile { name: name.into(), is_bold: bold, is_italic: italic, data: FontFileData::new(data), index: 0 });
        }
    }
}

fn frame(b: &mut Game, dt_ms: f64, mx: f64, my: f64, out: &mut [u32]) -> bool {
    let mut p = b.player.lock().unwrap();
    let view = p.viewport_dimensions();
    if (view.width, view.height) != b.size {
        p.set_viewport_dimensions(ViewportDimensions { width: b.size.0, height: b.size.1, scale_factor: 1.0 });
    }
    if (mx, my) != b.mouse {
        b.mouse = (mx, my);
        p.handle_event(PlayerEvent::MouseMove { x: mx, y: my });
    }
    if b.ready && let Ok(mut g) = b.ground.lock() {
        if let Some((x, y, power)) = g.blast() {
            OURS.store(true, Ordering::Relaxed);
            p.call_root_method("explode", vec![x.into(), y.into(), power.into()]).ok();
            OURS.store(false, Ordering::Relaxed);
        }
        // ponytail: the game's shock() also lights its toys near the mouse (it was the mouse's zapper); fine for now
        if let Some((x, y)) = g.zap.take() {
            p.call_root_method("shock", vec![x.into(), y.into()]).ok();
        }
        if g.calm == 0 && let Some((x, y)) = g.bang.take() {
            OURS.store(true, Ordering::Relaxed);
            p.call_root_method("explode", vec![x.into(), y.into(), 0.6.into()]).ok();
            OURS.store(false, Ordering::Relaxed);
            g.calm = 15;
        }
        if std::mem::take(&mut g.eaten) {
            p.call_root_method("setBuddyXY", vec![(b.size.0 as f64 / 2.0).into(), 100.into()]).ok();
        }
    }
    p.tick(FloatDuration::from_millis(dt_ms));
    if !b.ready {
        if p.get_root_variable("__host.ready") != Value::Number(1.0) {
            return false;
        }
        b.ready = true;
        p.set_window_mode("transparent");
        p.call_root_method("__hostMenuCommand", vec!["surface|desktop".into()]).ok();
        hook_ground(&mut p, &b.ground);
    }
    // the Buddy tab's settings, as the desktop app sets them (each only when it changed: a new toy limit clears his toys)
    let (s, a) = (b.settings, b.applied);
    if s[0] != a[0] {
        p.call_root_method("__nativeSetBuddySize", vec![s[0].into()]).ok();
    }
    if s[1] != a[1] {
        p.set_root_variable("__hostSpeechEnabled", if s[1] != 0.0 { "true" } else { "false" }.into()).ok();
    }
    if s[2] != a[2] {
        p.set_root_variable("__hostSpeechScale", Value::String(s[2].to_string().into())).ok();
    }
    if s[3] != a[3] {
        p.call_root_method("__nativeSetObjectLimit", vec![s[3].into()]).ok();
    }
    if s[4] != a[4] {
        p.set_root_variable("__hostSmooth", if s[4] != 0.0 { "true" } else { "false" }.into()).ok();
    }
    b.applied = s;
    p.call_root_method("__hostRender", vec![]).ok();
    p.render();
    p.call_root_method("__hostRestore", vec![]).ok();
    let renderer = (p.renderer() as &dyn std::any::Any).downcast_ref::<WgpuRenderBackend<TextureTarget>>().unwrap();
    read_back(&mut b.readback, renderer, out);
    true
}

// The frame from the GPU, already premultiplied, as 0xAARRGGBB (Ruffle's own capture_frame un-premultiplies into a
// new image every call: three times slower at 5120x1440)
fn read_back(readback: &mut Option<wgpu::Buffer>, renderer: &WgpuRenderBackend<TextureTarget>, out: &mut [u32]) {
    use rayon::prelude::*;
    let (target, d) = (renderer.target(), renderer.descriptors());
    let w = target.size.width as usize;
    let row = (w * 4).div_ceil(256) * 256;
    let size = (row * target.size.height as usize) as u64;
    if readback.as_ref().is_none_or(|r| r.size() != size) {
        *readback = Some(d.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("buddy readback"),
            size,
            usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        }));
    }
    let buffer = readback.as_ref().unwrap();
    let mut encoder = d.device.create_command_encoder(&Default::default());
    encoder.copy_texture_to_buffer(
        target.texture.as_image_copy(),
        wgpu::TexelCopyBufferInfo { buffer, layout: wgpu::TexelCopyBufferLayout { offset: 0, bytes_per_row: Some(row as u32), rows_per_image: None } },
        target.size,
    );
    let index = d.queue.submit([encoder.finish()]);
    let slice = buffer.slice(..);
    slice.map_async(wgpu::MapMode::Read, |_| {});
    d.device.poll(wgpu::PollType::Wait { submission_index: Some(index), timeout: None }).ok();
    {
        let map = slice.get_mapped_range().unwrap();
        out.par_chunks_mut(w).zip(map.par_chunks(row)).for_each(|(o, src)| {
            for (o, c) in o.iter_mut().zip(src.chunks_exact(4)) {
                *o = u32::from(c[3]) << 24 | u32::from(c[0]) << 16 | u32::from(c[1]) << 8 | u32::from(c[2]);
            }
        });
    }
    buffer.unmap();
}

// What clicks can land on, from the bridge's __host.hitData: "kind,x,y,w,h[,depth];" per thing. e: a round thing
// (his body parts, balls), r: a box, u: a game window or list, c: a window's close button.
fn hits(data: &str) -> Vec<(char, [f64; 4])> {
    data.split(';')
        .filter_map(|row| {
            let f: Vec<_> = row.split(',').collect();
            let kind = f.first()?.chars().next().filter(|k| "eruc".contains(*k))?;
            let v: Vec<f64> = f.get(1..5)?.iter().map(|v| v.parse().ok()).collect::<Option<_>>()?;
            (v.iter().all(|v| v.is_finite()) && v[2] > 0.0 && v[3] > 0.0).then(|| (kind, [v[0], v[1], v[2], v[3]]))
        })
        .collect()
}

fn hit(hits: &[(char, [f64; 4])], x: f64, y: f64) -> bool {
    hits.iter().any(|&(kind, [l, t, w, h])| {
        let (dx, dy) = ((x - l - w / 2.0) / (w / 2.0), (y - t - h / 2.0) / (h / 2.0));
        x >= l && y >= t && x <= l + w && y <= t + h && (kind != 'e' || dx * dx + dy * dy <= 1.15)
    })
}

// The game's own menus (hidden on the desktop), read through the bridge, as lines for TDPT's right-click menu:
// "depth\tflags\tcommand\tlabel", flags p: a submenu, g: greyed, c: ticked, s: a separator; command for the game's
// __hostMenuCommand.
const MENUS: [(&str, &str); 6] = [("file", "File"), ("skins", "Skins"), ("item", "Items"), ("modes", "Modes"), ("settings", "Settings"), ("help", "Help")];

fn read_menus(p: &mut Player) -> String {
    let mut out = String::new();
    for (category, label) in MENUS {
        p.call_root_method("__hostMenuCommand", vec![format!("menu|{category}|native").into()]).ok();
        let xml = p.get_root_variable("__hostMenuXml");
        p.call_root_method("__hostMenuCommand", vec![format!("close|{category}").into()]).ok();
        if let Value::String(xml) = xml {
            out += &format!("0\tp\t\t{label}\n");
            flatten(&xml, category, &mut out);
        }
    }
    out
}

fn flatten(xml: &str, category: &str, out: &mut String) {
    use quick_xml::events::Event;
    let mut reader = quick_xml::Reader::from_str(xml);
    let (mut path, mut count) = (Vec::new(), vec![0usize]); // the open node's indices; children seen at each level
    loop {
        let (tag, open) = match reader.read_event() {
            Ok(Event::Start(t)) if t.name().as_ref() == b"menu" => continue,
            Ok(Event::Start(t)) => (t, true),
            Ok(Event::Empty(t)) => (t, false),
            Ok(Event::End(t)) if t.name().as_ref() != b"menu" && !path.is_empty() => {
                path.pop();
                count.pop();
                continue;
            }
            Ok(Event::Eof) | Err(_) => return,
            _ => continue,
        };
        let (mut label, mut flags) = (String::new(), String::new());
        for a in tag.attributes().flatten() {
            let v = a.decode_and_unescape_value(reader.decoder()).unwrap_or_default();
            match a.key.as_ref() {
                b"label" => label = v.replace(['\t', '\n'], " "),
                b"enabled" if v == "false" => flags.push('g'),
                b"selected" if v == "true" => flags.push('c'),
                _ => {}
            }
        }
        let index = *count.last().unwrap();
        *count.last_mut().unwrap() += 1;
        let at: Vec<String> = path.iter().chain([&index]).map(|i: &usize| i.to_string()).collect();
        if open {
            flags.push('p');
        } else if label.is_empty() {
            flags.push('s');
        }
        *out += &format!("{}\t{flags}\tselect|{category}|{}\t{label}\n", path.len() + 1, at.join("."));
        if open {
            path.push(index);
            count.push(0);
        }
    }
}

// What the TDPT thread and the game's thread share
// the desktop app's defaults: normal size, speech on at 100%, 6 toys at once (the game's own), smoothing on
const SETTINGS: [f64; 5] = [100.0, 1.0, 100.0, 6.0, 1.0];

#[derive(Default)]
struct Shared {
    settings: [f64; 5],
    size: (u32, u32),
    mouse: (f64, f64),
    clicks: Vec<PlayerEvent>,
    ready: Vec<u32>, // the newest finished frame, waiting for TDPT (when fresh)
    hits: Vec<(char, [f64; 4])>, // what clicks can land on in it
    menus: String,         // read_menus, about once a second
    commands: Vec<String>, // picked from them, for the game
    fresh: bool,
    quit: bool,
    dead: bool,
}

pub struct Buddy {
    shared: Arc<Mutex<Shared>>,
    ground: Arc<Mutex<Ground>>,
    thread: Option<std::thread::JoinHandle<()>>,
    front: Vec<u32>, // the frame TDPT is drawing
}

// The game's thread: ticks and draws him about 60 times a second, off TDPT's frame (a game frame alone is ~7 ms)
fn run(mut game: Game, shared: Arc<Mutex<Shared>>) {
    let mut work = Vec::new();
    let mut last = Instant::now();
    let mut menus_at = Instant::now();
    loop {
        let start = Instant::now();
        let (mouse, clicks, commands) = {
            let mut s = shared.lock().unwrap();
            if s.quit {
                return;
            }
            game.size = s.size;
            game.settings = s.settings;
            (s.mouse, std::mem::take(&mut s.clicks), std::mem::take(&mut s.commands))
        };
        let dt = last.elapsed().as_secs_f64() * 1e3;
        last = Instant::now();
        work.resize(game.size.0 as usize * game.size.1 as usize, 0);
        let drawn = catch_unwind(AssertUnwindSafe(|| {
            let mut p = game.player.lock().unwrap();
            for c in commands {
                p.call_root_method("__hostMenuCommand", vec![c.into()]).ok();
            }
            for c in clicks {
                if let PlayerEvent::MouseDown { x, y, .. } = c {
                    p.handle_event(PlayerEvent::MouseMove { x, y }); // over what's pressed first
                }
                p.handle_event(c);
            }
            drop(p);
            frame(&mut game, dt, mouse.0, mouse.1, &mut work)
        }));
        let mut s = shared.lock().unwrap();
        match drawn {
            Ok(true) if game.size == s.size => {
                std::mem::swap(&mut work, &mut s.ready);
                s.fresh = true;
                drop(s);
                let data = game.player.lock().unwrap().get_root_variable("__host.hitData");
                s = shared.lock().unwrap();
                s.hits = if let Value::String(d) = data { hits(&d) } else { Vec::new() };
                if menus_at.elapsed() > Duration::from_secs(1) {
                    drop(s);
                    let menus = read_menus(&mut game.player.lock().unwrap());
                    menus_at = Instant::now();
                    s = shared.lock().unwrap();
                    s.menus = menus;
                }
            }
            Ok(_) => {}
            Err(_) => {
                s.dead = true;
                return;
            }
        }
        drop(s);
        std::thread::sleep(Duration::from_secs_f64(1.0 / 60.0).saturating_sub(start.elapsed()));
    }
}

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
    let shared = Arc::new(Mutex::new(Shared { size: (w, h), mouse: (-1.0, -1.0), settings: SETTINGS, ..Default::default() }));
    // the player can't change threads, so it starts on the game's, which reports back how that went
    let (tell, told) = std::sync::mpsc::channel();
    let thread = {
        let shared = shared.clone();
        // Ruffle's AVM recursion needs more than the default 2 MB
        std::thread::Builder::new().name("buddy".into()).stack_size(16 << 20).spawn(move || {
            match catch_unwind(|| open(&path, w, h)) {
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
        Ok((t, ground)) => Box::into_raw(Box::new(Buddy { shared, ground, thread: Some(t), front: Vec::new() })),
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
    }
    if b.front.len() == w as usize * h as usize { b.front.as_ptr() } else { std::ptr::null() }
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

/// A command from tdpt_buddy_menus, for the game
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

#[cfg(test)]
mod tests {
    use ruffle_core::events::{MouseButton, PlayerEvent};
    use ruffle_core::external::Value;
    use sha2::Digest;
    // needs the user's copy: TDPT_BUDDY_SWF=<path to Interactive Buddy v1.01.swf> cargo test
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
    fn ground_push_out() {
        // a floor from y = 100 down
        let g = super::Ground { rect: [0, 0, 200, 200], cells: (0..200 * 200).map(|i| (i / 200 >= 100) as u16).collect(), ..Default::default() };
        let (mut x, mut y, mut xv, mut yv) = (50.0, 95.0, 1.0, 6.0);
        let n = g.push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0).expect("in the floor");
        assert!(n.1 < -0.9 && y <= 90.5 && y > 88.0, "{n:?} {y}");
        assert!(yv < 0.0 && xv < 1.0, "bounces up with friction: {xv} {yv}");
        assert!(g.under(x, y, 10.0));
        assert!(g.push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0).is_none());
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
        let swf = super::patch(std::fs::read(path).unwrap()).unwrap();
        assert_eq!(super::hex(&sha2::Sha256::digest(&swf)), "9720616ea241c1b7a4004c90b91ed100dfa21812701141a6bc89a961d28ae9eb");
    }
}

