//! Interactive Buddy inside TDPT: the user's own copy of the game, patched in memory with our bridge code
//! (bridge/*.patch: only the tags we add, none of the game) and run by Ruffle, drawn offscreen into TDPT's overlay
//! frame. C interface in tdpt_buddy.h.
use ruffle_core::events::{MouseButton, PlayerEvent};
use ruffle_core::external::Value;
use ruffle_core::{FloatDuration, Player, PlayerBuilder, StageAlign, StageScaleMode, ViewportDimensions};
use ruffle_render_wgpu::{backend::WgpuRenderBackend, target::TextureTarget, wgpu};
use sha2::{Digest, Sha256};
use std::ffi::{CStr, c_char};
use std::panic::{AssertUnwindSafe, catch_unwind};
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
    size: (u32, u32),
    mouse: (f64, f64),
    ready: bool,
    readback: Option<wgpu::Buffer>,
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
    Ok(Game { player, size: (w, h), mouse: (-1.0, -1.0), ready: false, readback: None })
}

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
        let (file, part) = (self.file(name), self.file(&format!("{name}.part")));
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
    p.tick(FloatDuration::from_millis(dt_ms));
    if !b.ready {
        if p.get_root_variable("__host.ready") != Value::Number(1.0) {
            return false;
        }
        b.ready = true;
        p.set_window_mode("transparent");
        p.call_root_method("__hostMenuCommand", vec!["surface|desktop".into()]).ok();
        p.set_root_variable("__hostSmooth", "true".into()).ok();
    }
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
#[derive(Default)]
struct Shared {
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
    let shared = Arc::new(Mutex::new(Shared { size: (w, h), mouse: (-1.0, -1.0), ..Default::default() }));
    // the player can't change threads, so it starts on the game's, which reports back how that went
    let (tell, told) = std::sync::mpsc::channel();
    let thread = {
        let shared = shared.clone();
        // Ruffle's AVM recursion needs more than the default 2 MB
        std::thread::Builder::new().name("buddy".into()).stack_size(16 << 20).spawn(move || {
            match catch_unwind(|| open(&path, w, h)) {
                Ok(Ok(game)) => {
                    tell.send(Ok(())).ok();
                    run(game, shared);
                }
                Ok(Err(e)) => drop(tell.send(Err(e))),
                Err(_) => drop(tell.send(Err("Ruffle failed to start".into()))),
            }
        })
    };
    match thread.map_err(|e| e.to_string()).and_then(|t| told.recv().map_err(|e| e.to_string())?.map(|()| t)) {
        Ok(t) => Box::into_raw(Box::new(Buddy { shared, thread: Some(t), front: Vec::new() })),
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
        // ease the fist onto him from above, then let it rest
        let (x, y) = body(&game);
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

