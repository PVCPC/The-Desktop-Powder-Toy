//! Interactive Buddy inside TDPT: the user's own copy of the game, patched in memory with our bridge code
//! (bridge/*.patch: only the tags we add, none of the game) and run by Ruffle, drawn offscreen into TDPT's overlay
//! frame. C interface in tdpt_buddy.h.
mod sound;
use std::collections::HashMap;
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

// SHA-256 of each version the bridge fits (uncompressed), and its patches. v1.02 adds the pistol and machine gun.
const VERSIONS: [(&str, [&[u8]; 2]); 2] = [
    ("76da8a05bfe3472afe4a2bca2c09486b5be614881d45621489fa2f2686d16b0f", [include_bytes!("../bridge/enhanced.patch"), include_bytes!("../bridge/native.patch")]),
    ("b39c6e324240dd1ba90e7f5314e3ecfb3256071c1fa4508cc1b7a06ed400ffdf", [include_bytes!("../bridge/enhanced102.patch"), include_bytes!("../bridge/native102.patch")]),
];

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
    let hash = hex(&Sha256::digest(&swf));
    let Some((_, patches)) = VERSIONS.iter().find(|v| v.0 == hash) else {
        return Err("That file isn't Interactive Buddy v1.01 or v1.02".into());
    };
    for p in patches {
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
    scale: u32, // screen pixels per stage pixel he's drawn at (sharp on a screen TDPT shows at a bigger pixel size)
    mouse: (f64, f64),
    ready: bool,
    readback: [Option<wgpu::Buffer>; 2], // the GPU copies the frame into one while we read last frame's from the other
    pending: Option<Copy>,                // last frame's copy, not read yet
    drawn: HashMap<usize, Vec<[usize; 4]>>, // what each frame buffer (by address: TDPT and we take turns with them) has in it, x0 y0 x1 y1 boxes
    settings: [f64; 5], // from the Buddy tab: size %, speech on, speech size %, toys at once (0: no limit), smooth on
    applied: [f64; 5],  // what the game has
}

#[cfg(test)]
fn open(path: &str, w: u32, h: u32) -> Result<Game, String> {
    open_with::<sound::Speakers>(path, w, h, None)
}

// with sound: the game's own sounds go to the speakers (tests never: they stay silent)
fn open_with<A: ruffle_core::backend::audio::AudioBackend + 'static>(path: &str, w: u32, h: u32, sound: Option<A>) -> Result<Game, String> {
    let data = std::fs::read(path).map_err(|e| format!("Can't read {path}: {e}"))?;
    let swf = patch(data)?;
    let movie = ruffle_core::tag_utils::SwfMovieData::from_data(&swf, "file:///tdpt/buddy.swf".into(), None, None)
        .map_err(|e| e.to_string())?;
    let renderer = WgpuRenderBackend::for_offscreen((w, h), wgpu::Backends::PRIMARY, wgpu::PowerPreference::LowPower)
        .map_err(|e| e.to_string())?;
    let mut builder = PlayerBuilder::new();
    if let Some(sound) = sound {
        builder = builder.with_audio(sound);
    }
    let player = builder
        .with_movie(movie)
        .with_autoplay(true)
        .with_renderer(renderer)
        .with_viewport_dimensions(w, h, 1.0)
        .with_scale_mode(StageScaleMode::NoScale, true)
        .with_align(StageAlign::TOP | StageAlign::LEFT, true)
        .with_storage(Box::new(Saves("buddy".into())))
        .build();
    load_fonts(&mut player.lock().unwrap());
    player.lock().unwrap().audio_mut().set_volume(0.0); // silent until TDPT says how loud
    Ok(Game { player, ground: Default::default(), size: (w, h), scale: 1, mouse: (-1.0, -1.0), ready: false, readback: [None, None], pending: None, drawn: HashMap::new(), settings: SETTINGS, applied: [f64::NAN; 5] })
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
    shots: Vec<[f64; 5]>,         // his weapons since TDPT last asked: a bullet's path this frame (x0, y0, x1, y1, 1) or a flame (x, y, x, y, 2)
    bullets: Vec<(f64, f64)>,     // the game's bullets last frame: where each was
    drops: Vec<String>,           // his hoses' drops last frame (their clips' names)
    toys: bool,                   // his thrown things feel the sim (toys: command)
    toys_at: Vec<[i32; 4]>,       // the sim each one wants next frame (x, y, w, h), anywhere on his stage
    patches: Vec<Patch>,          // and what TDPT sent for them (tdpt_buddy_toy)
    hide: u8,                     // the game's own effects TDPT shows as the sim's instead, not drawn: 1 his hoses' water, 2 fire
    game_grav: f64,               // the game's grav this frame, before that
    ledge: Option<Ledge>,         // the active window's edges he can stand on (as in the desktop app)
    carry: (f64, f64),            // how far that window moved while he stood on it, not yet applied
    buried: u32,                  // frames his head has been deep in powder
    dig: bool,                    // digging out: powder doesn't stop him (the sim shoves it out of his way as he rises)
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
const POWDER: u16 = 0x800; // a powder (sand, snow): buried in it, he digs up through it (DIG)
const DIG: u32 = 40; // frames his head is deep in powder before he digs out
const ICE: u16 = 0x1000; // ice: a part closed in by it is held there until it melts (or he's dragged out)
const ACID: u16 = 31; // a kind: acid, a liquid (TPT weight 10) that eats him
const EATER: u16 = 30; // a kind: VOID or a black hole, which swallows him (he comes back at the top)
const PORTAL: u16 = 29; // a kind: a portal in, which moves him to its way out (Ground::exit)
const PARTS: [&str; 6] = ["body", "head", "lArm", "rArm", "lLeg", "rLeg"];
// where his parts sit around the body when he stands (from the game), to put him back together
const POSE: [(f64, f64); 6] = [(0.0, 0.0), (0.0, -39.4), (-22.1, -10.2), (22.1, -10.2), (-15.1, 32.0), (15.0, 32.0)];

impl Default for Ground {
    fn default() -> Self {
        Self { rect: [0; 4], cells: Vec::new(), want: [0; 4], last: (400.0, 300.0), air_rect: [0; 4], air: Vec::new(), cell: 4, before: ([0; 4], Vec::new()), calm: 0, zap: None, gravity: (0.0, 1.0), game_grav: 1.0, exit: None, jumped: 99, eaten: false, parts: Vec::new(), bang: None, booms: Vec::new(), shots: Vec::new(), bullets: Vec::new(), drops: Vec::new(), toys: false, toys_at: Vec::new(), patches: Vec::new(), hide: 0, ledge: None, carry: (0.0, 0.0), buried: 0, dig: false, support: 0 }
    }
}

// A patch of the sim around one of his toys (as Ground's own ground and air, which it stands in for)
#[derive(Default)]
struct Patch {
    rect: [i32; 4],
    cells: Vec<u16>,
    air_rect: [i32; 4],
    air: Vec<[f32; 3]>,
}

impl Patch {
    fn swap(&mut self, g: &mut Ground) {
        std::mem::swap(&mut self.rect, &mut g.rect);
        std::mem::swap(&mut self.cells, &mut g.cells);
        std::mem::swap(&mut self.air_rect, &mut g.air_rect);
        std::mem::swap(&mut self.air, &mut g.air);
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
        self.kind(x, y) == 1 && !(self.dig && self.cell(x, y) & POWDER != 0)
    }

    // powder over a part's top at least as deep as the part is wide (a few grains falling on him don't count)
    fn covered(&self, x: f64, y: f64, r: f64) -> bool {
        let (mut n, mut all) = (0, 0);
        for py in (y - 3.0 * r) as i32..(y - r) as i32 {
            for px in (x - r / 2.0) as i32..=(x + r / 2.0) as i32 {
                all += 1;
                n += (self.cell(px, py) & POWDER != 0) as u32;
            }
        }
        all > 0 && n * 10 >= all * 7
    }

    // powder touching a part's upper half (still in the sand it's digging out of; what it stands on doesn't count)
    fn sunk(&self, x: f64, y: f64, r: f64) -> bool {
        let e = r + 2.0;
        (y - e) as i32 <= y as i32
            && ((y - e) as i32..y as i32).any(|py| ((x - e) as i32..=(x + e) as i32).any(|px| (px as f64 + 0.5 - x).hypot(py as f64 + 0.5 - y) <= e && self.cell(px, py) & POWDER != 0))
    }

    // ice all round a part (most of a ring just outside it): frozen in
    fn encased(&self, x: f64, y: f64, r: f64) -> bool {
        let (mut n, mut all) = (0, 0);
        for k in 0..32 {
            let a = k as f64 * std::f64::consts::TAU / 32.0;
            all += 1;
            n += (self.cell((x + a.cos() * (r + 1.5)).floor() as i32, (y + a.sin() * (r + 1.5)).floor() as i32) & ICE != 0) as u32;
        }
        n * 10 >= all * 7
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

    // the solid pixels inside a circle: how many, and the sum of their offsets from its centre. Powder over the centre
    // doesn't count: poured on him it would press him down into the floor (crushed, he glitched through it); the sim
    // moves it out of his way instead
    fn overlap(&self, x: f64, y: f64, r: f64) -> (u32, f64, f64) {
        let (mut n, mut sx, mut sy) = (0, 0.0, 0.0);
        for py in (y - r).floor() as i32..=(y + r).ceil() as i32 {
            for px in (x - r).floor() as i32..=(x + r).ceil() as i32 {
                let (dx, dy) = (px as f64 + 0.5 - x, py as f64 + 0.5 - y);
                if dx * dx + dy * dy <= r * r && self.solid(px, py) && !(dy < 0.0 && self.cell(px, py) & POWDER != 0) {
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
    // A part buried deeper than its size stays put; buried in powder he digs out (Ground::dig).
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
        g.support = 0;
        g.last = (num(v, "body.x"), num(v, "body.y"));
        let mut bbox = [f64::MAX, f64::MAX, f64::MIN, f64::MIN];
        let mut standing = false;
        let (mut hot, mut warm, mut doused, mut cold, mut zap) = (false, 0, false, 0, None);
        let mut portal = false;
        g.parts.clear();
        g.dig = !held && g.buried > DIG;
        let (mut covered, mut sunk) = (false, false);
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
            let frozen = !held && ox.is_finite() && oy.is_finite() && g.encased(x, y, r);
            if frozen {
                (x, y, xv, yv) = (ox, oy, 0.0, 0.0);
            }
            let out = if held || frozen { None } else { g.push_out(&mut x, &mut y, &mut xv, &mut yv, r) };
            if part == "head" {
                covered = g.covered(x, y, r);
            }
            sunk |= g.dig && g.sunk(x, y, r);
            if g.dig {
                yv = yv.min(-1.5 - grav.max(0.0)); // wriggling up, about 60 px a second
                xv *= 0.8;
            }
            // landing on the active window's top or bottom edge from above, as in the desktop app (not when put
            // somewhere: setBuddyXY, a portal)
            let mut landed = side;
            // coming down onto a window's or icon's top (Windows: Solid): he stands on it
            if !held && !side && yv >= 0.0 && oy.is_finite() && oy < y && g.window(x, y, r) && !g.window(x, oy, r) {
                let mut ny = oy;
                while ny + 1.0 <= y && !g.window(x, ny + 1.0, r) {
                    ny += 1.0;
                }
                y = ny;
                yv = if yv.abs() < 2.0 { 0.0 } else { -yv * 0.25 };
                landed = true;
            }
            if let Some(l) = g.ledge.filter(|_| !held && yv >= 0.0 && oy.is_finite() && x + r > 0.0) {
                if let Some(k) = (0..2).find(|&k| x + r > l.left && x - r < l.right && oy + r <= l.rows[k] + 2.0 && y + r >= l.rows[k] && y - oy <= yv.abs() + 10.0) {
                    y = l.rows[k] - r;
                    yv = if yv.abs() < 2.0 { 0.0 } else { -yv * 0.25 };
                    g.support = k as u8 + 1;
                    landed = true;
                }
            }
            standing |= part.ends_with("Leg") && landed;
            if wet || blown || cold > 0 || out.is_some() || landed || g.dig || frozen {
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
        g.buried = if (covered || sunk) && !held { g.buried + 1 } else { 0 }; // digging goes on until he's out
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
    // his weapons for the sim: the game has moved its bullets (pistol, machine gun) and flames (flamethrower, fire) by now
    let g = ground.clone();
    p.hook_after_root_function("doObjectPhysics", Box::new(move |v| {
        let Ok(mut g) = g.lock() else { return };
        // a bullet's path: from where it was last frame (the same one: as far as its speed takes it), or from the gun (the
        // mouse) when it's new, to where it is
        let mut now = Vec::new();
        let gun = (num(v, "_xmouse"), num(v, "_ymouse"));
        for i in 0..num(v, "bullets.length").min(64.0) as usize {
            let b = |v: &mut dyn RootVars, f| num(v, &format!("bullets.{i}.{f}"));
            let (x, y, speed) = (b(v, "x"), b(v, "y"), b(v, "xv").hypot(b(v, "yv")));
            let near = |&(px, py): &(f64, f64)| (x - px).hypot(y - py) < speed * 1.5 + 5.0;
            if let Some(&(px, py)) = g.bullets.get(i).filter(|p| near(p)).or(Some(&gun).filter(|p| near(p)))
                && g.shots.len() < 256
            {
                g.shots.push([px, py, x, y, 1.0]);
            }
            now.push((x, y));
        }
        g.bullets = now;
        // his toys (balls, infants, the orb, the radio: the game's circles) feel the sim as his parts do: they rest on it, float
        // in liquid (but the heavy bowling ball), its air blows them; one stopped fast by it splashes (x, y, radius, speed, 7)
        g.toys_at.clear();
        let mut patches = std::mem::take(&mut g.patches);
        let grav = num(v, "grav");
        for i in 0..num(v, "objectList.length").min(64.0) as usize {
            let s = |f: &str| format!("objectList.{i}.{f}");
            if !g.toys || v.get(&s("type")) != Value::String("circle".into()) || v.get(&s("group")) == Value::String("buddy".into()) {
                continue;
            }
            let t = |v: &mut dyn RootVars, f: &str| num(v, &s(&format!("targ.{f}")));
            let (mut x, mut y, mut xv, mut yv, r, mass) = (t(v, "x"), t(v, "y"), t(v, "xv"), t(v, "yv"), t(v, "rad"), t(v, "mass"));
            if [x, y, xv, yv, r].iter().any(|n| !n.is_finite()) || r <= 0.0 {
                continue;
            }
            let speed = xv.hypot(yv);
            let m = r + 40.0 + speed * 2.0; // what it can touch by the next frame
            g.toys_at.push([(x - m) as i32, (y - m) as i32, (2.0 * m) as i32 + 1, (2.0 * m) as i32 + 1]);
            // its own patch of the sim in place of his, for this toy (no patch yet: his)
            let patch = patches.iter_mut().find(|p| { let [px, py, w, h] = p.rect; x >= px as f64 && y >= py as f64 && x < (px + w) as f64 && y < (py + h) as f64 });
            let mut patch = patch.map(|p| { p.swap(&mut g); p });
            let wet = !(mass >= 0.6) && grav.is_finite() && g.float(x, y, &mut xv, &mut yv, r, grav);
            let (ax, ay) = g.wind(x, y);
            xv += 0.1 * ax;
            yv += 0.1 * ay;
            let out = g.push_out(&mut x, &mut y, &mut xv, &mut yv, r);
            if wet || out.is_some() || ax != 0.0 || ay != 0.0 {
                for (f, n) in [("x", x), ("y", y), ("xv", xv), ("yv", yv)] {
                    v.set(&s(&format!("targ.{f}")), Value::Number(n));
                }
            }
            if let Some(p) = patch.take() {
                p.swap(&mut g);
            }
            if speed > 8.0 && xv.hypot(yv) < speed * 0.5 && g.shots.len() < 256 {
                g.shots.push([x, y, r, speed, 7.0]);
            }
        }
        g.patches = patches;
        // his stun gun shocking him
        if v.get("beingShocked") == Value::Bool(true) && g.shots.len() < 256 {
            g.shots.push([num(v, "body.x"), num(v, "body.y"), 0.0, 0.0, 6.0]);
        }
        // his gravity items: a vortex while held, the gravity shifter's point (kept till he's let go); how hard, 1-3
        if v.get("gravitating") == Value::Bool(true) && g.shots.len() < 256 {
            let pull = match v.get("item") {
                Value::String(s) if s == "Weak Gravity Vortex" => 1.0,
                Value::String(s) if s == "Strong Gravity Vortex" => 3.0,
                _ => 2.0,
            };
            g.shots.push([num(v, "gravitatingx"), num(v, "gravitatingy"), pull, 0.0, 5.0]);
        }
        // dancing (to his radio): a dance next in his actions (the game queues it at one of its priorities)
        if (0..9).any(|i| matches!(v.get(&format!("queue.{i}.0")), Value::String(a) if a.starts_with("dance")))
            && g.shots.len() < 256
        {
            let (x, y) = (num(v, "body.x"), num(v, "body.y"));
            g.shots.push([x, y, x, y, 3.0]);
        }
        let mut drops = Vec::new();
        if let Value::Object(clips) = v.get("particles") {
            for (name, clip) in clips {
                let Value::Object(c) = clip else { continue };
                // his hoses' water: drops ("smoke" clips with a strength) and their splashes where they hit; each drop new
                // since last frame
                if name.starts_with("smoke") && c.contains_key("strength") || name.starts_with("splash") {
                    if g.hide & 1 != 0 {
                        v.set(&format!("particles.{name}._visible"), Value::Bool(false));
                    }
                    let n = |k: &str| match c.get(k) { Some(Value::Number(n)) => *n, _ => f64::NAN };
                    if name.starts_with("smoke") {
                        // made after this hook last ran and moved once since: back at the nozzle
                        if !g.drops.contains(&name) && g.shots.len() < 256 {
                            g.shots.push([n("x") - n("xv"), n("y") - n("yv"), n("xv"), n("yv"), 4.0]);
                        }
                        drops.push(name);
                    }
                    continue;
                }
                // fire: flames (flamethrower, fireballs, molotovs), fire clips and burning debris (a molotov's glass)
                let fire = name.starts_with("fireClip") || name.starts_with("flame");
                if g.hide & 2 != 0 && (fire || name.starts_with("fireGlowClip")) {
                    v.set(&format!("particles.{name}._visible"), Value::Bool(false));
                }
                if !(fire || name.starts_with("debris") && c.get("onFire") == Some(&Value::Bool(true))) || g.shots.len() >= 256 {
                    continue;
                }
                if let (Some(Value::Number(x)), Some(Value::Number(y))) = (c.get("x"), c.get("y")) {
                    g.shots.push([*x, *y, *x, *y, 2.0]);
                }
            }
        }
        g.drops = drops;
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

// The frame's size in pixels: the stage's, scale times
fn pixels(size: (u32, u32), scale: u32) -> (u32, u32) {
    (size.0 * scale.max(1), size.1 * scale.max(1))
}

fn frame(b: &mut Game, dt_ms: f64, mx: f64, my: f64, out: &mut [u32]) -> bool {
    let mut p = b.player.lock().unwrap();
    let view = p.viewport_dimensions();
    let (pw, ph) = pixels(b.size, b.scale);
    let k = b.scale.max(1) as f64; // the stage stays b.size (no-scale mode: the viewport over the scale factor)
    if (view.width, view.height, view.scale_factor) != (pw, ph, k) {
        p.set_viewport_dimensions(ViewportDimensions { width: pw, height: ph, scale_factor: k });
        b.drawn.clear(); // the buffers are a new size: cleared whole
        (b.pending, b.readback) = (None, [None, None]); // and the copy on its way is the old size (its buffer still mapped)
    }
    if (mx, my) != b.mouse {
        b.mouse = (mx, my);
        p.handle_event(PlayerEvent::MouseMove { x: mx * k, y: my * k });
    }
    if b.ready && let Ok(mut g) = b.ground.lock() {
        if let Some((x, y, power)) = g.blast() {
            OURS.store(true, Ordering::Relaxed);
            p.call_root_method("explode", vec![x.into(), y.into(), power.into()]).ok();
            OURS.store(false, Ordering::Relaxed);
        }
        // the game's shock() was the mouse's zapper: it also lights toys near the mouse, so the mouse is at the spark for it
        if let Some((x, y)) = g.zap.take() {
            p.handle_event(PlayerEvent::MouseMove { x: x * k, y: y * k });
            p.call_root_method("shock", vec![x.into(), y.into()]).ok();
            p.handle_event(PlayerEvent::MouseMove { x: mx * k, y: my * k });
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
    // the window he stands on moved: he moves with it at once, every frame, as in the desktop app (all of him, speed
    // kept, and the smoothing's last two steps too, so he isn't drawn sweeping along after it)
    let (cx, cy) = b.ground.lock().map(|mut g| std::mem::take(&mut g.carry)).unwrap_or_default();
    if b.ready && (cx != 0.0 || cy != 0.0) && p.get_root_variable("fistDrag") != Value::Number(1.0) {
        let mut shift = |name: String, d: f64| {
            if let Value::Number(n) = p.get_root_variable(&name) {
                p.set_root_variable(&name, Value::Number(n + d)).ok();
            }
        };
        for (i, part) in PARTS.iter().enumerate() {
            for (f, d) in [("x", cx), ("y", cy), ("ox", cx), ("oy", cy), ("oox", cx), ("ooy", cy), ("_x", cx), ("_y", cy)] {
                shift(format!("{part}.{f}"), d);
            }
            // __hostParts starts with the six of PARTS; their rows' values start with _x, _y
            for row in ["previous", "current"] {
                shift(format!("__host.{row}.{i}.values.0"), cx);
                shift(format!("__host.{row}.{i}.values.1"), cy);
            }
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
    // only the part anything is drawn in comes back from the GPU (the whole 5120x1440 frame took 8-10 ms, so he
    // couldn't keep up at 120 Hz); 32 px more for glows and shadows
    let areas: Vec<_> = p.drawn_areas(32.0).into_iter().map(|a| a.map(|v| v * k)).collect();
    let renderer = (p.renderer() as &dyn std::any::Any).downcast_ref::<WgpuRenderBackend<TextureTarget>>().unwrap();
    read_back(&mut b.readback, &mut b.pending, renderer, out, &areas, &mut b.drawn);
    true
}

// A copy of the frame's drawn boxes on its way back from the GPU: which readback buffer, each box (x0, y0, x1, y1) and
// where its rows are in the buffer (padded row length, offset), the bytes used, and the submission to wait for
struct Copy {
    slot: usize,
    boxes: Vec<[usize; 4]>,
    rows: Vec<(usize, usize)>,
    total: usize,
    index: wgpu::SubmissionIndex,
}

// The frame from the GPU, already premultiplied, as 0xAARRGGBB (Ruffle's own capture_frame un-premultiplies into a
// new image every call: three times slower at 5120x1440). Only where things are drawn, and one frame late: this
// frame's copy is sent off and last frame's (done by now) is read, so we never wait for the GPU to draw (4-5 ms at
// 5120x1440, too slow for 120 Hz)
fn read_back(readback: &mut [Option<wgpu::Buffer>; 2], pending: &mut Option<Copy>, renderer: &WgpuRenderBackend<TextureTarget>, out: &mut [u32], areas: &[[f64; 4]], drawn: &mut HashMap<usize, Vec<[usize; 4]>>) {
    use rayon::prelude::*;
    let (target, d) = (renderer.target(), renderer.descriptors());
    let (fw, fh) = (target.size.width as usize, target.size.height as usize);
    if out.len() != fw * fh {
        return;
    }
    // this frame's boxes, copied into the buffer last frame's copy isn't in
    let boxes: Vec<[usize; 4]> = areas
        .iter()
        .map(|a| [a[0].clamp(0.0, fw as f64) as usize, a[1].clamp(0.0, fh as f64) as usize, a[2].clamp(0.0, fw as f64).ceil() as usize, a[3].clamp(0.0, fh as f64).ceil() as usize])
        .filter(|b| b[2] > b[0] && b[3] > b[1])
        .collect();
    let mut rows = Vec::new();
    let mut total = 0;
    for b in &boxes {
        let row = ((b[2] - b[0]) * 4).div_ceil(256) * 256;
        rows.push((row, total));
        total += row * (b[3] - b[1]);
    }
    let slot = pending.as_ref().map_or(0, |c| 1 - c.slot);
    if readback[slot].as_ref().is_none_or(|r| (r.size() as usize) < total) {
        readback[slot] = Some(d.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("buddy readback"),
            size: ((fw * 4).div_ceil(256) * 256 * fh).max(total) as u64, // room for the whole frame, made once
            usage: wgpu::BufferUsages::MAP_READ | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        }));
    }
    let buffer = readback[slot].as_ref().unwrap();
    let mut encoder = d.device.create_command_encoder(&Default::default());
    for (b, &(row, offset)) in boxes.iter().zip(&rows) {
        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo { origin: wgpu::Origin3d { x: b[0] as u32, y: b[1] as u32, z: 0 }, ..target.texture.as_image_copy() },
            wgpu::TexelCopyBufferInfo { buffer, layout: wgpu::TexelCopyBufferLayout { offset: offset as u64, bytes_per_row: Some(row as u32), rows_per_image: None } },
            wgpu::Extent3d { width: (b[2] - b[0]) as u32, height: (b[3] - b[1]) as u32, depth_or_array_layers: 1 },
        );
    }
    let index = d.queue.submit([encoder.finish()]);
    if total > 0 {
        buffer.slice(..total as u64).map_async(wgpu::MapMode::Read, |_| {});
    }
    let Some(last) = pending.replace(Copy { slot, boxes, rows, total, index }) else { return };
    // last frame's: what this buffer had (however many frames ago; all of it if we don't know) is cleared, then its
    // boxes go in
    match drawn.insert(out.as_ptr() as usize, last.boxes.clone()) {
        Some(old) => {
            for r in old {
                for y in r[1].min(fh)..r[3].min(fh) {
                    out[y * fw + r[0].min(fw)..y * fw + r[2].min(fw)].fill(0);
                }
            }
        }
        None => out.fill(0),
    }
    if last.total == 0 {
        return;
    }
    d.device.poll(wgpu::PollType::Wait { submission_index: Some(last.index), timeout: None }).ok();
    let buffer = readback[last.slot].as_ref().unwrap();
    {
        let map = buffer.slice(..last.total as u64).get_mapped_range().unwrap();
        for (b, &(row, offset)) in last.boxes.iter().zip(&last.rows) {
            let src = &map[offset..offset + row * (b[3] - b[1])];
            out[b[1] * fw..b[3] * fw].par_chunks_mut(fw).zip(src.par_chunks(row)).for_each(|(o, src)| {
                for (o, c) in o[b[0]..b[2]].iter_mut().zip(src.chunks_exact(4)) {
                    *o = u32::from(c[3]) << 24 | u32::from(c[0]) << 16 | u32::from(c[1]) << 8 | u32::from(c[2]);
                }
            });
        }
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
    scale: u32,
    volume: f32, // 0 silent, 1 the game's own loudness
    mouse: (f64, f64),
    clicks: Vec<PlayerEvent>,
    ready: Vec<u32>, // the newest finished frame, waiting for TDPT (when fresh)
    ready_boxes: Vec<[usize; 4]>, // where it has pixels (x0, y0, x1, y1): it's clear everywhere else
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
    front_boxes: Vec<[usize; 4]>,
}

// The game's thread: ticks and draws him 120 times a second (60 without smoothing), off TDPT's frame (a game frame alone is ~7 ms)
fn run(mut game: Game, shared: Arc<Mutex<Shared>>) {
    let mut work = Vec::new();
    let mut last = Instant::now();
    let mut menus_at = Instant::now();
    let mut saved_at = Instant::now();
    let mut due = Instant::now(); // when the next frame starts: on a fixed beat, so a late wake-up isn't carried on
    loop {
        let (mouse, clicks, commands) = {
            let mut s = shared.lock().unwrap();
            if s.quit {
                return;
            }
            game.size = s.size;
            game.scale = s.scale;
            game.settings = s.settings;
            if let Ok(mut p) = game.player.try_lock() && p.audio().volume() != s.volume {
                p.audio_mut().set_volume(s.volume);
            }
            (s.mouse, std::mem::take(&mut s.clicks), std::mem::take(&mut s.commands))
        };
        let dt = last.elapsed().as_secs_f64() * 1e3;
        last = Instant::now();
        let (pw, ph) = pixels(game.size, game.scale);
        work.resize(pw as usize * ph as usize, 0);
        let drawn = catch_unwind(AssertUnwindSafe(|| {
            let mut p = game.player.lock().unwrap();
            for c in commands {
                if let Some(on) = c.strip_prefix("toys:") {
                    game.ground.lock().unwrap().toys = on == "1";
                    continue;
                }
                if let Some(hide) = c.strip_prefix("hide:") {
                    game.ground.lock().unwrap().hide = hide.parse().unwrap_or(0);
                    continue;
                }
                match c.strip_prefix("act:").map(str::to_owned) {
                    Some(action) => p.call_root_method("addAction", vec![action.into(), 5.into()]).ok(),
                    None => p.call_root_method("__hostMenuCommand", vec![c.into()]).ok(),
                };
            }
            let k = game.scale.max(1) as f64; // clicks come in stage pixels, the player wants the frame's
            for c in clicks {
                let c = match c {
                    PlayerEvent::MouseDown { x, y, button, index } => PlayerEvent::MouseDown { x: x * k, y: y * k, button, index },
                    PlayerEvent::MouseUp { x, y, button } => PlayerEvent::MouseUp { x: x * k, y: y * k, button },
                    c => c,
                };
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
            Ok(true) if (game.size, game.scale) == (s.size, s.scale) => {
                let (w, h) = (pw as usize, ph as usize);
                s.ready_boxes = game.drawn.get(&(work.as_ptr() as usize)).cloned().unwrap_or_else(|| vec![[0, 0, w, h]]);
                std::mem::swap(&mut work, &mut s.ready);
                s.fresh = true;
                drop(s);
                let data = game.player.lock().unwrap().get_root_variable("__host.hitData");
                s = shared.lock().unwrap();
                s.hits = if let Value::String(d) = data { hits(&d) } else { Vec::new() };
                // his save (money, purchases) every 2 minutes too, not only when he's closed: a crash keeps most of it
                if saved_at.elapsed() > Duration::from_secs(120) {
                    drop(s);
                    game.player.lock().unwrap().flush_shared_objects();
                    saved_at = Instant::now();
                    s = shared.lock().unwrap();
                }
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
        // the game's smoothing draws him between its 40 Hz steps: 120 frames a second, as in the desktop app
        let rate = if game.settings[4] != 0.0 { 120.0 } else { 60.0 };
        due += Duration::from_secs_f64(1.0 / rate);
        let now = Instant::now();
        if due + Duration::from_secs_f64(1.0 / rate) < now {
            due = now; // a frame or more behind: start the beat again rather than rush to catch up
        }
        std::thread::sleep(due.saturating_duration_since(now));
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
/// stopped fast by the sim at x0, y0, radius x1, speed y1 before); how many
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

#[cfg(test)]
mod tests {
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
        assert!(sand(false).push_out(&mut x, &mut y, &mut xv, &mut yv, 10.0).is_some() && y <= 90.5, "sank in: {y}");
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
            for _ in 0..40 {
                super::frame(&mut game, 25.0, mx, my, &mut out);
            }
            let shots = std::mem::take(&mut game.ground.lock().unwrap().shots);
            assert!(shots.iter().any(|s| s[4] == 3.0), "not dancing: {shots:?}");
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
}

