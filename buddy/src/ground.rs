// The sim around him (Ground: what TDPT tells him of it) and the hooks into his game that use it
use super::*;

// The sim around him, so he stands on it, floats in it and burns in it: one byte per sim pixel over rect (x, y, w, h sim
// pixels, which are his stage pixels). Low 5 bits: 0 nothing, 1 solid for him, 2 + w/4 liquid of TPT weight w, ACID,
// PORTAL, EATER; then
// WARM, HOT or COLD (2 bits), SPARK, HURT and BANG. Rect (x, y, w, h sim pixels, which are his stage pixels). TDPT fills it each frame with the part he asks for (want).
pub(crate) struct Ground {
    pub(crate) rect: [i32; 4],
    pub(crate) cells: Vec<u16>,
    pub(crate) want: [i32; 4],
    pub(crate) last: (f64, f64), // his body, last frame
    pub(crate) air_rect: [i32; 4], // in sim cells (CELL pixels)
    pub(crate) air: Vec<[f32; 3]>, // the sim's air velocity and pressure there
    pub(crate) cell: i32,
    pub(crate) before: ([i32; 4], Vec<f32>), // the pressure last frame (air_rect, values)
    pub(crate) calm: u32,                    // frames until the next blast may go off
    pub(crate) zap: Option<(f64, f64)>,      // a part touching a spark this frame: the game's shock() goes off there
    pub(crate) gravity: (f64, f64),          // TPT's gravity where he is, 1 = the game's own (down: 0, 1)
    pub(crate) exit: Option<(f64, f64)>,     // where the portal in his reach comes out
    pub(crate) jumped: u32,                  // frames since he went through one (not straight back in)
    pub(crate) eaten: bool,                  // swallowed this frame: he comes back at the top
    pub(crate) parts: Vec<[f64; 6]>,         // his parts after this frame: x, y, radius, xv, yv, 1 burning (for the sim to feel him)
    pub(crate) bang: Option<(f64, f64)>,     // a part touching something that blows up on him: the game's explode() there
    pub(crate) booms: Vec<[f64; 3]>,         // the game's explosions since TDPT last asked: x, y, power (1 = its bomb)
    pub(crate) shots: Vec<[f64; 5]>,         // his weapons since TDPT last asked: a bullet's path this frame (x0, y0, x1, y1, 1) or a flame (x, y, x, y, 2)
    pub(crate) bullets: Vec<(f64, f64)>,     // the game's bullets last frame: where each was
    pub(crate) stops: Vec<[f64; 4]>,         // bullets the sim stopped (from TDPT): where, and the way each was going
    pub(crate) drops: Vec<String>,           // his hoses' drops last frame (their clips' names)
    pub(crate) toys: bool,                   // his thrown things feel the sim (toys: command)
    pub(crate) toys_at: Vec<[i32; 4]>,       // the sim each one wants next frame (x, y, w, h), anywhere on his stage
    pub(crate) patches: Vec<Patch>,          // and what TDPT sent for them (tdpt_buddy_toy)
    pub(crate) well: Option<[f64; 3]>,       // a TPT black hole pulling him the game's own way (x, y, its power; well: command)
    pub(crate) frames: u32,                  // game frames run since frame() last looked
    pub(crate) hide: u8,                     // the game's own effects TDPT shows as the sim's instead, not drawn: 1 his hoses' water, 2 fire
    pub(crate) game_grav: f64,               // the game's grav this frame, before that
    pub(crate) ledge: Option<Ledge>,         // the active window's edges he can stand on (as in the desktop app)
    pub(crate) carry: (f64, f64),            // how far that window moved while he stood on it, not yet applied
    pub(crate) buried: u32,                  // frames his head has been deep in powder
    pub(crate) dig: bool,                    // digging out: powder doesn't stop him (the sim shoves it out of his way as he rises)
    pub(crate) support: u8,                  // what he stands on: 0 nothing, 1 its top edge, 2 its bottom edge
}

// The desktop app's one-way ledges (PlatformBridge.as): the active window's top edge from outside and its bottom
// edge from inside, stage pixels; a row is NaN when it's off screen or too near the top
#[derive(Clone, Copy, PartialEq)]
pub(crate) struct Ledge {
    pub(crate) id: u64,
    pub(crate) left: f64,
    pub(crate) right: f64,
    pub(crate) rows: [f64; 2],
}

impl Ledge {
    // which row (1 top, 2 bottom) a foot rests on: within 4 px, not going up fast
    pub(crate) fn foot(&self, x: f64, y: f64, r: f64, yv: f64) -> u8 {
        let over = x + r > self.left && x - r < self.right && yv >= -2.0;
        (1..=2).find(|&k| over && (y + r - self.rows[k as usize - 1]).abs() <= 4.0).unwrap_or(0)
    }
}

pub(crate) const WARM: u16 = 0x20; // 60 C and up: hurts
pub(crate) const HOT: u16 = 0x40; // 400 C and up: sets him on fire
pub(crate) const COLD: u16 = 0x60; // -50 C and down: slows him, hurts
pub(crate) const SPARK: u16 = 0x80; // electricity (a spark or an electron): shocks him
pub(crate) const HURT: u16 = 0x100; // caustic gas, radiation, virus: hurts like acid
pub(crate) const BANG: u16 = 0x200; // DEST, BOMB, a singularity: blows up on him
pub(crate) const WINDOW: u16 = 0x400; // a window or icon: its sides stop him like they stop powder; he's in front of it otherwise
pub(crate) const POWDER: u16 = 0x800; // a powder (sand, snow): buried in it, he digs up through it (DIG)
pub(crate) const DIG: u32 = 40; // frames his head is deep in powder before he digs out
pub(crate) const ICE: u16 = 0x1000; // ice: a part closed in by it is held there until it melts (or he's dragged out)
pub(crate) const ACID: u16 = 31; // a kind: acid, a liquid (TPT weight 10) that eats him
pub(crate) const EATER: u16 = 30; // a kind: VOID or a black hole, which swallows him (he comes back at the top)
pub(crate) const PORTAL: u16 = 29; // a kind: a portal in, which moves him to its way out (Ground::exit)
pub(crate) const PARTS: [&str; 6] = ["body", "head", "lArm", "rArm", "lLeg", "rLeg"];
// where his parts sit around the body when he stands (from the game), to put him back together
pub(crate) const POSE: [(f64, f64); 6] = [(0.0, 0.0), (0.0, -39.4), (-22.1, -10.2), (22.1, -10.2), (-15.1, 32.0), (15.0, 32.0)];

impl Default for Ground {
    fn default() -> Self {
        Self { rect: [0; 4], cells: Vec::new(), want: [0; 4], last: (400.0, 300.0), air_rect: [0; 4], air: Vec::new(), cell: 4, before: ([0; 4], Vec::new()), calm: 0, zap: None, gravity: (0.0, 1.0), game_grav: 1.0, exit: None, jumped: 99, eaten: false, parts: Vec::new(), bang: None, booms: Vec::new(), shots: Vec::new(), bullets: Vec::new(), stops: Vec::new(), drops: Vec::new(), toys: false, toys_at: Vec::new(), patches: Vec::new(), well: None, frames: 0, hide: 0, ledge: None, carry: (0.0, 0.0), buried: 0, dig: false, support: 0 }
    }
}

// A patch of the sim around one of his toys (as Ground's own ground and air, which it stands in for)
#[derive(Default)]
pub(crate) struct Patch {
    pub(crate) rect: [i32; 4],
    pub(crate) cells: Vec<u16>,
    pub(crate) air_rect: [i32; 4],
    pub(crate) air: Vec<[f32; 3]>,
}

impl Patch {
    pub(crate) fn swap(&mut self, g: &mut Ground) {
        std::mem::swap(&mut self.rect, &mut g.rect);
        std::mem::swap(&mut self.cells, &mut g.cells);
        std::mem::swap(&mut self.air_rect, &mut g.air_rect);
        std::mem::swap(&mut self.air, &mut g.air);
    }
}

impl Ground {
    pub(crate) fn kind(&self, x: i32, y: i32) -> u8 {
        (self.cell(x, y) & 0x1F) as u8
    }

    // what a circle touches (within a pixel): 1 << WARM, HOT, COLD (as 1, 2, 3) for each one there, SPARK, 1 for
    // acid, 0x10 a portal, 0x20 an eater, HURT, BANG
    pub(crate) fn touch(&self, x: f64, y: f64, r: f64) -> u16 {
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

    pub(crate) fn cell(&self, x: i32, y: i32) -> u16 {
        let [rx, ry, w, h] = self.rect;
        let (u, v) = (x - rx, y - ry);
        if u >= 0 && v >= 0 && u < w && v < h { self.cells.get((v * w + u) as usize).copied().unwrap_or(0) } else { 0 }
    }

    // the sim's air velocity at a pixel
    pub(crate) fn wind(&self, x: f64, y: f64) -> (f64, f64) {
        let [rx, ry, w, h] = self.air_rect;
        let (u, v) = ((x as i32).div_euclid(self.cell.max(1)) - rx, (y as i32).div_euclid(self.cell.max(1)) - ry);
        if u < 0 || v < 0 || u >= w || v >= h {
            return (0.0, 0.0);
        }
        self.air.get((v * w + u) as usize).map_or((0.0, 0.0), |a| (a[0] as f64, a[1] as f64))
    }

    // A TPT explosion near him: the sim's pressure jumping by more than 6 in a frame. Where it jumped most (sim pixels)
    // and how hard, as the game's explode() power (1 = one of its own bombs, at a jump of about 60). One per 10 frames.
    pub(crate) fn blast(&mut self) -> Option<(f64, f64, f64)> {
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

    pub(crate) fn solid(&self, x: i32, y: i32) -> bool {
        self.kind(x, y) == 1 && !(self.dig && self.cell(x, y) & POWDER != 0)
    }

    // powder over a part's top at least as deep as the part is wide (a few grains falling on him don't count)
    pub(crate) fn covered(&self, x: f64, y: f64, r: f64) -> bool {
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
    pub(crate) fn sunk(&self, x: f64, y: f64, r: f64) -> bool {
        let e = r + 2.0;
        (y - e) as i32 <= y as i32
            && ((y - e) as i32..y as i32).any(|py| ((x - e) as i32..=(x + e) as i32).any(|px| (px as f64 + 0.5 - x).hypot(py as f64 + 0.5 - y) <= e && self.cell(px, py) & POWDER != 0))
    }

    // ice all round a part (most of a ring just outside it): frozen in
    pub(crate) fn encased(&self, x: f64, y: f64, r: f64) -> bool {
        let (mut n, mut all) = (0, 0);
        for k in 0..32 {
            let a = k as f64 * std::f64::consts::TAU / 32.0;
            all += 1;
            n += (self.cell((x + a.cos() * (r + 1.5)).floor() as i32, (y + a.sin() * (r + 1.5)).floor() as i32) & ICE != 0) as u32;
        }
        n * 10 >= all * 7
    }

    // a part's circle touches a window
    pub(crate) fn window(&self, x: f64, y: f64, r: f64) -> bool {
        let r2 = r * r;
        (-(r as i32)..=r as i32).any(|dy| (-(r as i32)..=r as i32).any(|dx| (dx * dx + dy * dy) as f64 <= r2 && self.cell(x as i32 + dx, y as i32 + dy) & WINDOW != 0))
    }

    // the window the desktop app let him stand on changed or moved: if he stood on it, he rides it
    pub(crate) fn set_ledge(&mut self, next: Option<Ledge>) {
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
    pub(crate) fn wet(&self, x: f64, y: f64, r: f64) -> (f64, f64) {
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
    pub(crate) fn float(&self, x: f64, y: f64, xv: &mut f64, yv: &mut f64, r: f64, grav: f64) -> bool {
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
    pub(crate) fn overlap(&self, x: f64, y: f64, r: f64) -> (u32, f64, f64) {
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
    pub(crate) fn push_out(&self, x: &mut f64, y: &mut f64, xv: &mut f64, yv: &mut f64, r: f64) -> Option<(f64, f64)> {
        let (n, sx, sy) = self.overlap(*x, *y, r);
        if n == 0 {
            return None;
        }
        let len = sx.hypot(sy);
        let (nx, ny) = if len > 1e-9 { (-sx / len, -sy / len) } else { (0.0, -1.0) };
        // out of powder at most 3 px a frame: sand poured over a foot lifts him over a few frames, instead of popping the
        // foot out at once, which his springy body turned into a jump (out of solids at once)
        let soft = self.cell((*x - nx * r) as i32, (*y - ny * r) as i32) & POWDER != 0;
        for _ in 0..if soft { 3 } else { (2.0 * r) as usize + 2 } {
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
    pub(crate) fn under(&self, x: f64, y: f64, r: f64) -> bool {
        (-(r as i32) / 2..=(r as i32) / 2).any(|dx| (1..=2).any(|dy| self.solid(x as i32 + dx, (y + r) as i32 + dy)))
    }
}

pub(crate) fn num(v: &mut dyn RootVars, name: &str) -> f64 {
    match v.get(name) {
        Value::Number(n) => n,
        _ => f64::NAN,
    }
}

// After the game moves his parts each frame, the sim pushes them out of itself; standing on it counts as standing.
pub(crate) fn hook_ground(p: &mut Player, ground: &Arc<Mutex<Ground>>) {
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
        let count = num(v, "bullets.length").min(64.0) as usize;
        // one the sim stopped (TDPT found where): the bullet on that line past that point ends there (the game drops a
        // bullet with ricNum below 0 next frame)
        let mut gone = vec![false; count];
        for [hx, hy, dx, dy] in std::mem::take(&mut g.stops) {
            let mut best = None;
            for i in (0..count).filter(|&i| !gone[i]) {
                let (u, w) = (num(v, &format!("bullets.{i}.x")) - hx, num(v, &format!("bullets.{i}.y")) - hy);
                let along = u * dx + w * dy;
                if along > -2.0 && (u * dy - w * dx).abs() < 3.0 && best.is_none_or(|(a, _)| along < a) {
                    best = Some((along, i));
                }
            }
            if let Some((_, i)) = best {
                gone[i] = true;
                for (f, n) in [("x", hx), ("y", hy), ("xv", 0.0), ("yv", 0.0), ("ricNum", -1.0)] {
                    v.set(&format!("bullets.{i}.{f}"), Value::Number(n));
                }
            }
        }
        for i in (0..count).filter(|&i| !gone[i]) {
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
            // his grenades, mines and molotovs: TPT fire, lava, a spark or a blast on one sets it off (its fuse, life, to 1)
            if matches!(v.get(&s("targ.objectType")), Value::String(o) if o == "grenade" || o == "mine" || o == "molotov")
                && g.touch(x, y, r) & (4 | SPARK | BANG) != 0
                && num(v, &s("targ.life")) > 1.0
            {
                v.set(&s("targ.life"), Value::Number(1.0));
            }
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
        // the flail's ball (x, y, its move a frame, 10) and missiles (x, y, their move a frame, 13)
        for i in 0..num(v, "objectList.length").min(64.0) as usize {
            let kind = match v.get(&format!("objectList.{i}.type")) {
                Value::String(t) if t == "immovable" && v.get(&format!("objectList.{i}.group")) == Value::String("wreckingBall".into()) => 10.0,
                Value::String(t) if t == "missile" => 13.0,
                _ => continue,
            };
            let t = |v: &mut dyn RootVars, f: &str| num(v, &format!("objectList.{i}.targ.{f}"));
            let b = [t(v, "x"), t(v, "y"), t(v, "xv"), t(v, "yv"), kind];
            if b.iter().all(|n| n.is_finite()) && g.shots.len() < 256 {
                g.shots.push(b);
            }
        }
        // the game's modes: Earthquake shaking (how hard, 11), Low Gravity (the game's gravity then, 12)
        if v.get("modeContainer.earthquake") == Value::Bool(true) && num(v, "shake") > 0.5 && g.shots.len() < 256 {
            g.shots.push([num(v, "shake"), 0.0, 0.0, 0.0, 11.0]);
        }
        if v.get("modeContainer.lowGrav") == Value::Bool(true) && g.shots.len() < 256 {
            g.shots.push([0.35, 0.0, 0.0, 0.0, 12.0]); // (the game's own low gravity: 0.35 of normal)
        }
        // a script (the game's ShockScript) asking for TPT stuff: assign(tpt, "SAND") with tptX, tptY (the mouse if
        // not set) and tptN (10): x, y, how many, the element's name as up to 4 bytes (14); asked once, then cleared
        if let Value::String(name) = v.get("variables.tpt")
            && !name.is_empty()
        {
            let or = |v: &mut dyn RootVars, k: &str, d: f64| Some(num(v, k)).filter(|n| n.is_finite()).unwrap_or(d);
            let (mx, my) = (num(v, "_xmouse"), num(v, "_ymouse"));
            let (x, y) = (or(v, "variables.tptX", mx), or(v, "variables.tptY", my));
            let n = or(v, "variables.tptN", 10.0).clamp(1.0, 500.0);
            let code = name.bytes().take(4).enumerate().fold(0u32, |c, (k, b)| c | (b.to_ascii_uppercase() as u32) << (8 * k));
            if g.shots.len() < 256 {
                g.shots.push([x, y, n, code as f64, 14.0]);
            }
            v.set("variables.tpt", Value::Undefined);
        }
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
        // the game's Dynamic Camera zooms out (to ~0.95) on a blast: the sim round him can't zoom with it, so he'd be
        // drawn tens of pixels off it; its zoom stays 1 (its shake, a few pixels, stays as the game has it). (After
        // cameraStuff, before doGraphics; the player has room for 5 hooks, all taken)
        if num(v, "camz") != 1.0 {
            v.set("camz", Value::Number(1.0));
        }
        let Ok(mut g) = g.lock() else { return };
        // TPT's gravity where he is: the game's own (down, grav per frame) scaled by its downward part; the sideways
        // part is added in doBodyPhysics
        g.game_grav = num(v, "grav");
        g.frames += 1;
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
    // a piece of the game's gore (gore mode: gore(x, y, xv, yv, size)) as TPT blood
    let g = ground.clone();
    p.hook_after_root_function("gore", Box::new(move |v| {
        // (not waiting: TPT's own blasts call the game's explode, and so its gore, with the ground held; that gore is lost)
        let Ok(mut g) = g.try_lock() else { return };
        let (x, y, xv, yv) = (num(v, "__hostArg0"), num(v, "__hostArg1"), num(v, "__hostArg2"), num(v, "__hostArg3"));
        if (x + y + xv + yv).is_finite() && g.shots.len() < 256 {
            g.shots.push([x, y, xv, yv, 9.0]);
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
