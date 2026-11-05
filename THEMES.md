# Real looks for TDPT's themes (branch `buddy`)

Goal: every look in Overlay > Look draws from the real thing, or from a very faithful recreation of it, not a
hand-drawn guess. The user's call: use these public repos and credit each one in README.md ("Libraries and other assets
used"). Each look's images get packed into one PNG in resources/ (no taller than 384 px: TPT's PNG reader refuses
anything bigger), with a script beside it that rebuilds it. Each look should add well under 100 KB.

Done 2026-11-03: Vista, 7, 8, 10, System 1-6, System 7, Mac OS 8-9, 10.0-10.6, 10.7-10.9, 10.10-10.15 and 11-15 draw
their buttons, check boxes, radio buttons and tabs (where the source has them) from resources/skin_<look>.png
(resources/skins.py <checkouts> resources src/OverlaySkins.h; Skinned<Look> in OverlayWindows.cpp). TempleOS uses
its own font (resources/templeos_font.ttf, 0.65 size: it is wide; Plain() swaps symbols it lacks). Credits screen and
README credit every source. Not worth images: 3.1/95/98 (B00merang's are the same flat bevels we draw; their xfwm4
XPMs use GTK colour symbols), 7's xfwm4 caption buttons (7 Basic's, one width), macOS 26 Tahoe (CSS only, no images).
Private copies: PVCPC/<repo name> (8 made, the rest retried slowly: GitHub rate-limits repo creation).

Also done 2026-11-03: Windows 11 is drawn by Windows itself on Windows 11 (Native<Eleven>: OpenThemeData /
DrawThemeBackground, nothing shipped; the hand-drawn look elsewhere). XP has Royale, Royale Noir and Zune colours
(resources/xp2.png, luna.py with colour prefixes). The Welcome page draws its keys as the look's push buttons and its
tip as the look's tooltip, and its top as each family greeted you: 95's "Welcome to" with buttons and "Did you know...",
XP/Vista's Welcome Center banner with tiles, 7 to 11's Getting Started, the old Macs' small centred page, the modern
Macs' icon, heading and Continue. Windows 10 and macOS 11-15 have Light and Dark colours (Windows-10-Dark, macOS-Dark; macOS-Dark's GTK 2
buttons are still light, so that look draws its own). 95's colours: Windows95Theme's .theme only sets the teal
desktop, the rest are Windows 10 defaults; ours are 95's own. Look also has a Gradient wallpaper (overlay-drawn where
the desktop shows, two colours picked with Windows' colour dialog; default the README gif's navy to teal). XP Embedded dropped: only B00merang's GTK images exist, not the msstyles layout
the Luna parts need; not worth one more blue.

Done so far: Windows XP uses the real Luna images (resources/luna.png and luna.py, from wine_themes). Blue, Olive Green
and Silver are a Colour row under it. The Welcome page takes the look's page, heading and text colours.

## Rules (learned)
- The theme pref is the index into `themes[]`: add new looks **only at the end**. List them in `windowsThemes` /
  `macThemes` in display order.
- A generation's colours or variants go in `themeColours` (the first one is the picker's) and show as the Colour row.
- Themes draw through `Luna()`-style 9-slice blits with the source's own sizing margins. The hooks are Frame,
  CaptionButton, Button, Tab, CheckBox, GroupFrame and Popup.
- .gitignore ignores `*.png` and `*.py`: add these with `git add -f`.
- Windows 11 on Windows: draw from the PC's own C:\Windows\Resources\Themes\aero.msstyles at run time (never ship it).
  On Mac, keep the hand-drawn Windows 11.

## Sources (private PVCPC copies of each, as the user asked)
| Our look | Source | Licence |
|---|---|---|
| Windows 3.1 | github.com/B00merang-Project/Windows-3.11 (also github.com/matthewmx86/Win3xE) | GPL-3 / BSD-2 |
| Windows 95 | github.com/B00merang-Project/Windows-95; exact colours: github.com/ke2083/Windows95Theme | GPL-3 |
| Windows 98 | github.com/B00merang-Project/Windows-Classic | GPL-3 |
| Windows XP | github.com/listumps/wine_themes (real luna.msstyles; Royale, Zune too); B00merang Windows-XP (Royale Noir, Embedded) | none / GPL-3 |
| Windows Vista | github.com/B00merang-Project/Windows-Vista | GPL-3 |
| Windows 7 | github.com/B00merang-Project/Windows-7 | GPL-3 |
| Windows 8 | github.com/B00merang-Project/Windows-8.1 | GPL-3 |
| Windows 10 | github.com/B00merang-Project/Windows-10 (+ Windows-10-Dark, Windows-10-Fluent) | GPL-3 |
| Windows 11 | the PC's own aero.msstyles (Windows only) | |
| System 1-6 | github.com/B00merang-Project/System-4 | GPL-3 |
| System 7 | github.com/moltensoftware/Symptom-7 (icons: moltensoftware/SevenIcons) | GPL-3 / none |
| Mac OS 8-9 | github.com/B00merang-Project/Mac-OS-9 | GPL-3 |
| Mac OS X 10.0-10.6 | github.com/B00merang-Project/Mac-OS-X-Cheetah, OS-X-Leopard | GPL-3 |
| OS X 10.7-10.9 | github.com/B00merang-Project/OS-X-Mavericks | GPL-3 |
| OS X 10.10-10.15 | github.com/B00merang-Project/OS-X-Yosemite | GPL-3 |
| macOS 11-15 | github.com/B00merang-Project/macOS (+ macOS-Dark) | GPL-3 |
| macOS 26 | github.com/kayozxo/GNOME-macOS-Tahoe | MIT |
| TempleOS | github.com/PhilipPanda/TempleOS-Theme (fonts/templeos_font.ttf) | MIT |

Also: github.com/bartekl1/windows-ui-assets (icons, cursors, sounds and wallpapers per Windows version; no frames or
buttons). Index of B00merang's themes: b00merang-project.github.io/windows and /macos.

## To do (in order)
1. Private PVCPC copies of every repo above.
2. Swap each look to its source's images, oldest Windows first: 3.1, 95, 98, Vista, 7, 8, 10, then 11 (from the PC),
   then the Macs: System 1-6, System 7, Mac OS 8-9, 10.0-10.6, 10.7-10.9, 10.10-10.15, 11-15, 26. Then TempleOS's
   real font.
3. Extra colours and variants as Colour rows: XP Royale, Royale Noir, Zune, Embedded; 10 Dark; macOS Dark; possibly
   B00merang's Windows 1.0, 2.0, Longhorn and Catalina as their own looks.
4. Welcome page in each look's parts: key boxes as the look's push buttons, the tip box as its tooltip, and headings,
   underlines and links in its colours.
5. Welcome screen per family, in our own words (no copied Microsoft or Apple text):
   - Classic Windows (3.1, 95, 98): a grey dialog with a side banner, a topic list and a "Did you know" tip.
   - Welcome Center (XP, Vista): a header and a grid of task tiles.
   - Getting Started (7, 8, 10, 11): in each one's colours.
   - Classic Mac (System 1-9): a small window with a picture and plain text.
   - Modern Mac (10.0 to 26): an icon, a centred heading and a Continue button.
6. Check 95's colours against Windows95Theme's .theme file.
7. [x] (Low priority, user 2026-11-03) New looks: Windows 1.0, Windows 2.0 and Windows ME. Done 2026-11-05: ME = 98's
   chrome in ME's title colours; 1.0 and 2.0 = B00merang's colours (blue bar, name in a black box, white rounded buttons).
7. Credit every source in README.md as each look lands.
8. Then back to Buddy: more interactivity (BUDDY.md).
