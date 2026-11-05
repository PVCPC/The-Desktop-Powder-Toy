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
    held: bool, // the left button is down in the game
    open: [bool; WINDOWS.len()], // which of the game's windows were open
    placing: u32,                // frames left to look for windows a menu command opened (place_windows)
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
    Ok(Game { player, ground: Default::default(), size: (w, h), scale: 1, mouse: (-1.0, -1.0), held: false, open: [false; WINDOWS.len()], placing: 0, ready: false, readback: [None, None], pending: None, drawn: HashMap::new(), settings: SETTINGS, applied: [f64::NAN; 5] })
}

mod ground;
use ground::*;

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

// The game writes a first, short save of its own as it starts (cash, items, scripts, its face), and the rest (its
// physics and other settings) only later. One left from then (TDPT killed, crashed or quit by a script's os.exit within
// two minutes) holds him still where he starts on every run after: read as no save, he starts as new (it held nothing more)
fn complete_save(data: &[u8]) -> bool {
    let has = |key: &[u8]| data.windows(key.len()).any(|w| w == key);
    !has(b"initialized") || has(b"realPhysics") || has(b"playingTime")
}

impl ruffle_core::backend::storage::StorageBackend for Saves {
    fn get(&self, name: &str) -> Option<Vec<u8>> {
        std::fs::read(self.file(name)).ok().filter(|d| complete_save(d))
    }
    fn put(&mut self, name: &str, value: &[u8]) -> bool {
        // tests read the save but never write it: what one test does to him (his mood, his skin) stays out of the next run
        if cfg!(test) && self.0 == std::path::Path::new("buddy") {
            return true;
        }
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
    if b.placing > 0 {
        b.placing -= 1;
        place_windows(&mut b.open, b.size, &mut p);
    }
    // the Fist moving: where it is and how fast, for the sim to be shoved by it
    if b.ready && (mx, my) != b.mouse && b.mouse.0 >= 0.0 && matches!(p.get_root_variable("item"), Value::String(i) if i == "Fist")
        && let Ok(mut g) = b.ground.lock() && g.shots.len() < 256
    {
        g.shots.push([mx, my, mx - b.mouse.0, my - b.mouse.1, 8.0]);
    }
    if (mx, my) != b.mouse {
        // held, in steps of a few pixels: the game's windows drop a drag the moment the mouse is off their title bar,
        // which one jump a frame (the mouse is read once a frame) always is
        let (ox, oy) = b.mouse;
        let n = if b.held && ox >= 0.0 { ((mx - ox).hypot(my - oy) / 6.0).ceil().clamp(1.0, 200.0) as usize } else { 1 };
        for i in 1..=n {
            let t = i as f64 / n as f64;
            p.handle_event(PlayerEvent::MouseMove { x: (ox + (mx - ox) * t) * k, y: (oy + (my - oy) * t) * k });
        }
        b.mouse = (mx, my);
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
    // a TPT black hole near him: the game's own gravity vortex pull (gravitateToPoint, as its Weak and Strong Gravity
    // Vortex call it: power 50, or 20 and stat), once for each game frame that ran, after it (so his own vortex stays his)
    let (well, n) = b.ground.lock().map(|mut g| (g.well, std::mem::take(&mut g.frames))).unwrap_or((None, 0));
    if let Some([x, y, power]) = well {
        for _ in 0..n.min(4) {
            p.call_root_method("gravitateToPoint", vec![x.into(), y.into(), power.into(), (power <= 20.0).into()]).ok();
        }
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
// A command for the game's __hostMenuCommand; the windows it opens are put in the middle (place_windows)
fn menu_command(open: &mut [bool; WINDOWS.len()], placing: &mut u32, p: &mut Player, c: String) -> Option<Value> {
    *open = open_windows(p);
    *placing = 30;
    p.call_root_method("__hostMenuCommand", vec![c.into()]).ok()
}

fn open_windows(p: &mut Player) -> [bool; WINDOWS.len()] {
    WINDOWS.map(|w| matches!(p.get_root_variable(&format!("{w}._x")), Value::Number(_))) // (a clip reads as null)
}

// The game opens its windows (a frame or two after the command) near its 640x480 stage's top left; TDPT's stage is
// the whole screen, where that's a far corner, often under TDPT's own menu: in the middle instead.
fn place_windows(open: &mut [bool; WINDOWS.len()], stage: (u32, u32), p: &mut Player) {
    let now = open_windows(p);
    for ((w, was), now) in WINDOWS.iter().zip(*open).zip(now) {
        if now && !was {
            let mut size = |v: &str| match p.get_root_variable(&format!("{w}.{v}")) { Value::Number(n) => n, _ => 0.0 };
            let (ww, wh) = (size("width"), size("height"));
            p.set_root_variable(&format!("{w}._x"), ((stage.0 as f64 - ww) / 2.0).round().max(0.0).into()).ok();
            p.set_root_variable(&format!("{w}._y"), ((stage.1 as f64 - wh) / 2.0).round().max(0.0).into()).ok();
        }
    }
    *open = now;
}

// the game's windows (its _root variables), each opened from one of its menus
const WINDOWS: [&str; 9] = ["itemStoreWindow", "skinStoreWindow", "modeStoreWindow", "statsWindow", "helpWindow", "aboutWindow", "updatesWindow", "scriptWindow", "faceWindow"];

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
                if let Some(w) = c.strip_prefix("well:") {
                    let n: Vec<f64> = w.split(',').filter_map(|n| n.parse().ok()).collect();
                    game.ground.lock().unwrap().well = (n.len() == 3).then(|| [n[0], n[1], n[2]]);
                    continue;
                }
                if let Some(on) = c.strip_prefix("toys:") {
                    game.ground.lock().unwrap().toys = on == "1";
                    continue;
                }
                // he put a fire out (fireman): pleased, a hop
                if c == "happy:" {
                    if let Value::Number(e) = p.get_root_variable("emotion") {
                        p.set_root_variable("emotion", Value::Number((e + 15.0).min(100.0))).ok();
                    }
                    p.call_root_method("addAction", vec!["jumpUp".into(), 5.into()]).ok();
                    continue;
                }
                // a TPT stickman or fighter kicked him: knocked that way (dx, dy a frame) and scared
                if let Some(s) = c.strip_prefix("kick:") {
                    let n: Vec<f64> = s.split(',').filter_map(|n| n.parse().ok()).collect();
                    if let [dx, dy] = n[..] {
                        for part in PARTS {
                            for (f, d) in [("ox", dx), ("oy", dy)] {
                                if let Value::Number(o) = p.get_root_variable(&format!("{part}.{f}")) {
                                    p.set_root_variable(&format!("{part}.{f}"), Value::Number(o - d)).ok();
                                }
                            }
                        }
                        p.call_root_method("addAction", vec!["setModifier:scared:160".into(), 5.into()]).ok();
                    }
                    continue;
                }
                // TPT gold he picked up: that much more cash
                if let Some(n) = c.strip_prefix("cash:").and_then(|n| n.parse::<f64>().ok()) {
                    if let Value::Number(cash) = p.get_root_variable("cash") {
                        p.set_root_variable("cash", Value::Number(cash + n)).ok();
                    }
                    continue;
                }
                // a hot floor: that part (PARTS) hops up
                if let Some(i) = c.strip_prefix("hop:").and_then(|i| i.parse::<usize>().ok()).filter(|&i| i < PARTS.len()) {
                    if let Value::Number(o) = p.get_root_variable(&format!("{}.oy", PARTS[i])) {
                        p.set_root_variable(&format!("{}.oy", PARTS[i]), Value::Number(o + 4.0)).ok();
                    }
                    continue;
                }
                if let Some(s) = c.strip_prefix("stop:") {
                    let n: Vec<f64> = s.split(',').filter_map(|n| n.parse().ok()).collect();
                    if let [x, y, dx, dy] = n[..] {
                        game.ground.lock().unwrap().stops.push([x, y, dx, dy]);
                    }
                    continue;
                }
                if let Some(hide) = c.strip_prefix("hide:") {
                    game.ground.lock().unwrap().hide = hide.parse().unwrap_or(0);
                    continue;
                }
                match c.strip_prefix("act:").map(str::to_owned) {
                    Some(action) => p.call_root_method("addAction", vec![action.into(), 5.into()]).ok(),
                    None => menu_command(&mut game.open, &mut game.placing, &mut p, c),
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
                game.held = matches!(c, PlayerEvent::MouseDown { .. });
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

mod ffi;
pub use ffi::*;

#[cfg(test)]
mod tests;
