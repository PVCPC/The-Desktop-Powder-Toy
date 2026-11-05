// Part of OverlayWindows.cpp (included inside OverlayWindows.cpp's anonymous namespace): the Fun and Buddy tabs' settings and rows
{
	float uiScale = 1;
	bool windowWalls = true;            // open windows are walls to particles (Overlay.WindowWalls)
	bool iconWalls = true;              // and so are the desktop icons (Overlay.IconWalls)
	bool areaFrame = false;             // a faint line around a play area smaller than the screen (Overlay.AreaFrame)
	// The Fun tab's settings (Overlay.Fun.*), all off by default
	struct
	{
		float pets = 0, weather = 0, music = 0; // pets: how many; weather and music 0-3
		float weatherIdle = 0, weatherLast = 90; // weather only after this many minutes idle (0: always); seconds a piece stays
		float hot = 0;                           // busy apps' windows warm what sits on them, 0-3
		float hourglass = 0;                     // minutes the taskbar hourglass runs (0: none)
		int weatherKind = 0;                    // 0 by the season, 1 snow, 2 rain, 3 leaves, 4 hail, 5 fog
		float napFrom = 0, napUntil = 7;         // pets nap from/until this hour (napFrom 0: never; 24 = midnight)
		float gather = 0, welcome = 2;           // pets gather after this many minutes away (0: never); hop this many s back
		float snowmen = 0, snowDepth = 20;       // at most this many snowmen, on snow at least this deep (px)
		float lift = 0;                          // how hard a window dragged up throws what sits on it
		float planes = 0, planeSpeed = 1;        // a paper plane every this many minutes (0: none), how fast
		float rainbow = 0, rainbowGlow = 0.6f;   // seconds a rainbow shows after rain (0: never), how strong
		float rainbowDouble = 0.25f;             // the chance it is a double rainbow
		float fetch = 0, kick = 1;               // pets fetch balls this far away (px, 0: no), kick strength
		float volcano = 0, volcanoForce = 1;     // CPU use (0-1) that wakes the taskbar volcano (0: none), lava force
		bool bin = false, broom = false, closeToss = false, openShake = false, holidays = false, notify = false, dayNight = false, trail = false, confetti = false;
		bool castles = false, perch = false;     // pets build sandcastles; morning birds land and rest
		float stars = 0, puddles = 0, wind = 0;  // shooting stars an hour at night; s rain lies as puddles; mouse wind
		bool starsLand = false;                  // now and then a shooting star comes down and hits something
		float birdRest = 10;                     // s a landed bird rests (about)
		bool petHoles = false, showers = false;  // pets use holes in windows; meteor showers on their nights
		bool taskThrow = false, taskPop = false; // Ctrl-drag a taskbar button off to throw a block; apps that open pop one out
		bool tilt = false, weekday = false, typing = false; // gravity leans to the active window; weather by weekday; typing sparks
		float typingChance = 40;                 // how many key presses in a hundred puff typing sparks
		float meteors = 0;                       // big meteors an hour at night
		bool buddy = false;                      // Interactive Buddy (OverlayBuddy; his file: Overlay.Fun.BuddySwf)
		bool buddyPets = false;                  // the pets notice him (OverlayBuddy reads Overlay.Fun.BuddyPets)
		bool buddyLooks = false;                 // he looks wet and frosted (Overlay.Fun.BuddyLooks)
		bool buddyTracks = false, buddySplash = false, buddyFireman = false, buddyCarry = false, buddyGlow = false, buddyWeapons = false, buddyFlee = false, buddyDirt = false, buddyDance = false, buddyFeed = false, buddyWater = false, buddyWaterOnly = false, buddyFireOnly = false, buddyGravity = false, buddyToysSim = false, buddyFist = false, buddyBlood = false, buddyClicks = false, buddyWells = false, buddyModes = false, buddyStickmen = false, buddyScripts = false, buddyRadio = false, buddyTears = false, buddyGold = false, buddyHotFloor = false; // footprints, splashes, he puts out fires, carries sand, his guns, runs from fire, dirt, radio, feeding, hoses (Overlay.Fun.Buddy*)
		// the Buddy tab (OverlayBuddy reads them from the prefs): his size and speech bubbles' (%), toys at once (0: no
		// limit), standing on the active window, smoothing, speech
		float buddySize = 100, buddySpeechSize = 100, buddyToys = 6, buddyVolume = 50;
		bool buddySmooth = true, buddySpeech = true;
		int buddyWindows = 1; // 0 walks through windows, 1 stands on the active one, 2 all solid (as to sand)
		std::vector<HWND> flashed; // windows that flashed their taskbar button (a new message) since the last tick
	} fun;
	// The Fun tab's rows, by group, and the pref each is saved as. A row with no tip starts a group (name = heading);
	// a row with neither b nor v is the weather kind. Slider units: 0 %, 1 count, 2 minutes, 3 seconds (10 s steps),
	// 4 clock hour, 5 px, 6 seconds (0 off), 7 an hour, 8 a percentage, 9 count (0 no limit)
	// kind: 0 as above, 1 Buddy's windows choice, 2 Buddy's game menus, 3 importing his save
	struct FunRow { const wchar_t *name, *tip = nullptr; bool *b = nullptr; float *v = nullptr; float lo = 0, hi = 0; const char *pref = ""; int unit = 0, kind = 0; };
	FunRow Check(const wchar_t *name, const wchar_t *tip, bool *b, const char *pref) { return { name, tip, b, nullptr, 0, 0, pref }; }
	FunRow Slide(const wchar_t *name, const wchar_t *tip, float *v, float lo, float hi, const char *pref, int unit = 0) { return { name, tip, nullptr, v, lo, hi, pref, unit }; }
	FunRow funRows[] = {
		{ L"Windows and icons" },
		Check(L"Open windows are walls", L"Sand, water and snow land on your windows and pile up on their title bars. Only while the sim is in front of your windows", &windowWalls, "Overlay.WindowWalls"),
		Check(L"Desktop icons are walls", L"Sand piles up on your desktop icons, in the shape of each icon. Not while the sim is behind the icons", &iconWalls, "Overlay.IconWalls"),
		Slide(L"Window grip", L"How well sand rides along on a window you drag. Off: it slides off", &overlayFeel.grip, 0, 1, "Overlay.Feel.Grip"),
		Slide(L"Fling", L"How hard sand flies off a window you drag and then stop short or turn back", &overlayFeel.fling, 0, 3, "Overlay.Feel.Fling"),
		Slide(L"Window push", L"How hard a window you move shoves the sand in its way", &overlayFeel.push, 0, 3, "Overlay.Feel.Push"),
		Slide(L"Window lift", L"Drag a window up fast and whatever sits on it is thrown into the air (how hard)", &fun.lift, 0, 3, "Overlay.Fun.Lift"),
		Check(L"Closing a window throws its sand", L"Close or minimise a window and the pile on it is tossed off", &fun.closeToss, "Overlay.Fun.CloseToss"),
		Check(L"New windows shake sand loose", L"A window that opens bumps the sand above it into the air", &fun.openShake, "Overlay.Fun.OpenShake"),
		Check(L"Recycle Bin eats sand", L"Sand that lands on the Recycle Bin icon disappears into it (needs Desktop icons are walls)", &fun.bin, "Overlay.Fun.Bin"),

		{ L"Breaking windows" },
		Check(L"Destructible windows and icons", L"Acid, explosions and great heat eat holes in windows and icons. Only in the overlay: your real windows and icons are never touched, and clearing the sim mends them. The rest of this group needs it on", &overlayFeel.destructible, "Overlay.Feel.Destructible"),
		Check(L"Windows crack first", L"A hit on whole glass only cracks it; a second hit on the cracks breaks it out. Cracks fade as windows heal", &overlayFeel.crack, "Overlay.Fun.Crack"),
		Slide(L"Windows heal", L"Holes and broken chunks grow back in from their edges; a big hole takes about this long", &overlayFeel.heal, 0, 10, "Overlay.Fun.Heal", 2),
		Slide(L"Acid eats windows", L"How fast acid eats holes in windows and icons", &overlayFeel.acidEat, 0, 3, "Overlay.Feel.AcidEat"),
		Slide(L"Blasts and burning", L"How hard explosions, bombs, DEST, singularities and anything 2000 C or hotter break windows and icons", &overlayFeel.blast, 0, 3, "Overlay.Feel.Blast"),
		Slide(L"Black holes", L"How far and how hard black holes (NBHL) tear at the windows and icons near them", &overlayFeel.blackHole, 0, 3, "Overlay.Feel.BlackHole"),
		Slide(L"Hole opacity", L"Off: holes show straight through to whatever is really behind the window. Up: the broken part fades back in", &overlayFeel.holeOpacity, 0, 1, "Overlay.Feel.HoleSolid"),
		Check(L"Hide window borders at holes", L"Where a hole breaks through a window's edge, the thin dark border Windows draws around it is covered too. Off: the border stays, showing where the real window is", &overlayFeel.holeBorder, "Overlay.Feel.HoleBorder"),
		Slide(L"Shards", L"How many bits chip off and fly when a window breaks. Off: none", &overlayFeel.shards, 0, 3, "Overlay.Feel.Shards"),
		Slide(L"Shard size", L"How big each chip is", &overlayFeel.shardSize, 0, 6, "Overlay.Feel.ShardSize"),
		Check(L"Shards take the window's colour", L"On: chips are the colour of the window they broke off. Off: dark grey", &overlayFeel.shardTint, "Overlay.Feel.ShardTint"),
		Check(L"Shards are sim particles", L"On: broken glass in the sim, which piles up, melts and reacts. Off: solid chunks that spin, fall and fade", &overlayFeel.shardParticles, "Overlay.Feel.ShardParticles"),
		Slide(L"Explosion shake", L"How hard explosions shake your REAL windows. They go back where they were afterwards. Works without Destructible", &overlayFeel.shake, 0, 3, "Overlay.Feel.Shake"),

		{ L"Marks on windows and the wallpaper" },
		Slide(L"Scorch", L"How fast fire and lava blacken the windows they touch (gone when you clear the sim)", &overlayFeel.scorch, 0, 3, "Overlay.Feel.Scorch"),
		Slide(L"Frost", L"How fast ice, LN2 and CRYO frost the windows they touch. The frost melts", &overlayFeel.frost, 0, 3, "Overlay.Feel.Frost"),
		Slide(L"Acid etching", L"How fast acid pits the windows it touches (gone when you clear the sim)", &overlayFeel.acid, 0, 3, "Overlay.Feel.Acid"),
		Slide(L"Water streaks", L"How much water streaks the windows it runs down. The streaks dry", &overlayFeel.water, 0, 3, "Overlay.Feel.Water"),
		Check(L"Fire and acid mark the wallpaper", L"Anything 300 C or hotter blackens the desktop background behind it, and acid pits it, at the Scorch and Acid etching speeds. Only drawn by the overlay: your real wallpaper is never changed, and clearing the sim wipes it", &overlayFeel.wallpaper, "Overlay.Feel.Wallpaper"),

		{ L"Mouse and keyboard" },
		Check(L"Mouse is a broom", L"Where clicks go to the desktop, the mouse pointer is a solid paddle that shoves and scoops sand", &fun.broom, "Overlay.Fun.Broom"),
		Slide(L"Mouse breeze", L"Moving the mouse stirs the air around the pointer: a soft breeze that drifts sand, smoke and gas (where clicks go to the desktop)", &overlayFeel.wind, 0, 3, "Overlay.Feel.Wind"),
		Slide(L"Mouse wind", L"Swipe the mouse fast and it flings the loose sand, liquid and smoke right next to the pointer. Stronger and closer in than Mouse breeze", &fun.wind, 0, 3, "Overlay.Fun.Wind"),
		Check(L"Sparkle trail", L"The mouse pointer leaves a short trail of cold sparks over the desktop", &fun.trail, "Overlay.Fun.Trail"),
		Check(L"Confetti", L"Wiggle the mouse quickly from side to side to throw a burst of confetti", &fun.confetti, "Overlay.Fun.Confetti"),
		Check(L"Typing sparks", L"Typing now and then puffs a few cold sparks up from the text cursor (or from the top of the window you're typing in)", &fun.typing, "Overlay.Fun.Typing"),
		Slide(L"Typing spark chance", L"How often a key press puffs typing sparks: 100% every key, 40% about two keys in five", &fun.typingChance, 5, 100, "Overlay.Fun.TypingChance", 8),

		{ L"Taskbar and apps" },
		Check(L"Throw from the taskbar", L"Hold Ctrl and drag an app's taskbar button up off the taskbar: a stone block in its icon's colour drops from the pointer, thrown the way you moved", &fun.taskThrow, "Overlay.Fun.TaskThrow"),
		Check(L"Apps pop out of the taskbar", L"When an app opens a window, a stone block in its icon's colour pops up out of its taskbar button", &fun.taskPop, "Overlay.Fun.TaskPop"),
		Check(L"Notifications drop a ball", L"When an app flashes its taskbar button (a new message in Discord, Teams and the like), a ball drops onto its window", &fun.notify, "Overlay.Fun.Notify"),
		Slide(L"Busy apps run hot", L"A window warms whatever sits on it by how hard its app is working: snow melts on a busy game or browser and stays on an idle one", &fun.hot, 0, 3, "Overlay.Fun.Hot"),
		Slide(L"Music bounce", L"Sand hops and fire flares to the beat of whatever your PC is playing (it only reads how loud it is)", &fun.music, 0, 3, "Overlay.Fun.Music"),
		Check(L"Gravity leans to your window", L"Gravity tilts toward the side of the screen the window you're using is on, so loose sand slides that way (only while Gravity is Vertical, in the Simulation tab)", &fun.tilt, "Overlay.Fun.Tilt"),
		Slide(L"Hourglass", L"A glass hourglass on the taskbar whose sand runs out in this many minutes, counted from when you set it; then it starts again", &fun.hourglass, 0, 120, "Overlay.Fun.Hourglass", 2),
		Slide(L"Volcano wakes at", L"A little volcano by the clock smokes once your PC's total CPU use passes this, and erupts lava at 20% more. Off: no volcano", &fun.volcano, 0, 1, "Overlay.Fun.Volcano"),
		Slide(L"Volcano force", L"How high the volcano throws its lava", &fun.volcanoForce, 0.25f, 3, "Overlay.Fun.VolcanoForce"),

		{ L"Pets" },
		Slide(L"Pets", L"Little stick figures that wander over your windows and taskbar (how many). Fire and long falls can kill them; new ones drop in", &fun.pets, 0, 10, "Overlay.Fun.Pets", 1),
		Check(L"Pets build sandcastles", L"A pet next to a big enough sand pile now and then builds a little castle from it. It crumbles back into sand after 5 minutes", &fun.castles, "Overlay.Fun.Castles"),
		Check(L"Pets explore holes", L"Pets drop through holes broken in windows instead of turning back, and stop to look out of a hole they stand in", &fun.petHoles, "Overlay.Fun.PetHoles"),
		Slide(L"Pets fetch", L"Pets run after a ball up to this far away and kick it back toward the mouse pointer", &fun.fetch, 0, 400, "Overlay.Fun.Fetch", 5),
		Slide(L"Fetch kick", L"How hard the pets kick the ball back", &fun.kick, 0.25f, 3, "Overlay.Fun.Kick"),
		Slide(L"Paper planes", L"Every this many minutes a pet (or the edge of the screen) throws a paper plane that glides until it lands", &fun.planes, 0, 30, "Overlay.Fun.Planes", 2),
		Slide(L"Plane speed", L"How fast the paper planes fly", &fun.planeSpeed, 0.25f, 3, "Overlay.Fun.PlaneSpeed"),
		Slide(L"Pets nap from", L"From this hour the pets stop and doze (z's float up) until you move the mouse; they doze off again after 10 s", &fun.napFrom, 0, 24, "Overlay.Fun.NapFrom", 4),
		Slide(L"Pets wake at", L"The hour the pets stop napping", &fun.napUntil, 1, 12, "Overlay.Fun.NapUntil", 4),
		Slide(L"Pets gather after", L"Leave the PC alone this many minutes and the pets gather in the middle of the screen", &fun.gather, 0, 30, "Overlay.Fun.Gather", 2),
		Slide(L"Welcome back hop", L"How long the pets hop for joy when you come back after a minute or more away", &fun.welcome, 0, 10, "Overlay.Fun.Welcome", 6),

		{ L"Weather" },
		Slide(L"Weather", L"Snow, rain (with lightning), leaves, hail or fog coming down from the top of the screen (how heavy)", &fun.weather, 0, 3, "Overlay.Fun.Weather"),
		{ L"Weather is", L"By season: snow from December to February, leaves from September to November, rain the rest of the year" },
		Check(L"Weekday weather", L"Monday rain, Tuesday fog, Wednesday hail, Thursday snow, Friday leaves; weekends by the season. Only while Weather is By season", &fun.weekday, "Overlay.Fun.Weekday"),
		Slide(L"Weather waits", L"Weather only starts once you have left the PC alone this many minutes, and stops when you come back. Off: always", &fun.weatherIdle, 0, 30, "Overlay.Fun.WeatherIdle", 2),
		Slide(L"Weather lasts", L"How long each flake, leaf or drop stays before it melts or fades; they thin out one by one", &fun.weatherLast, 10, 600, "Overlay.Fun.WeatherLast", 3),
		Slide(L"Puddles last", L"Rain that lands on something lies there as a puddle this many seconds more before it dries up", &fun.puddles, 0, 120, "Overlay.Fun.Puddles", 6),
		Slide(L"Rainbow after rain", L"When rain has fallen half a minute or more and stops, a rainbow shows for this many seconds", &fun.rainbow, 0, 120, "Overlay.Fun.Rainbow", 6),
		Slide(L"Rainbow brightness", L"How strong the rainbow's colours are (it stays see-through, like a real one)", &fun.rainbowGlow, 0.1f, 1, "Overlay.Fun.RainbowGlow"),
		Slide(L"Double rainbow chance", L"How often a second, fainter rainbow, its colours the other way round, shows above the first", &fun.rainbowDouble, 0, 1, "Overlay.Fun.RainbowDouble"),
		Slide(L"Snowmen", L"Where snow lies deep enough, up to this many snowmen appear (made of ice: they melt when it warms up)", &fun.snowmen, 0, 5, "Overlay.Fun.Snowmen", 1),
		Slide(L"Snowman needs", L"How deep the snow must be before a snowman appears on it", &fun.snowDepth, 6, 40, "Overlay.Fun.SnowDepth", 5),

		{ L"Sky and holidays" },
		Check(L"Day and night", L"By the clock: fireflies come out and fallen leaves go dark in the evening, birds fly over in the morning", &fun.dayNight, "Overlay.Fun.DayNight"),
		Check(L"Birds land and rest", L"The morning birds (Day and night) now and then land on a window, icon or pile and rest a while", &fun.perch, "Overlay.Fun.Perch"),
		Slide(L"Birds rest for", L"About how long a bird that landed sits before it flies on", &fun.birdRest, 2, 60, "Overlay.Fun.BirdRest", 6),
		Slide(L"Shooting stars", L"At night (8 pm to 5 am), about this many shooting stars an hour streak across the empty desktop", &fun.stars, 0, 30, "Overlay.Fun.Stars", 7),
		Check(L"Shooting stars land", L"Now and then a shooting star comes all the way down and hits a window or a pile with a burst of sparks; with Destructible on it knocks a chunk out of the window", &fun.starsLand, "Overlay.Fun.StarsLand"),
		Check(L"Meteor showers", L"On the real meteor shower nights (Quadrantids, Lyrids, Eta Aquariids, Perseids, Orionids, Leonids, Geminids) shooting stars come ten times as often, at least 60 an hour", &fun.showers, "Overlay.Fun.Showers"),
		Slide(L"Big meteors", L"At night, about this many big glowing meteors an hour crash down: a crater in the sand, and with Destructible on a big chunk out of a window (3 times as many on meteor shower nights)", &fun.meteors, 0, 10, "Overlay.Fun.Meteors", 7),
		Check(L"Holidays", L"Pumpkins roll about on Halloween, jelly hearts fall on Valentine's Day, fireworks go up after midnight on New Year's", &fun.holidays, "Overlay.Fun.Holidays"),

		{ L"Interactive Buddy" },
		Check(L"Interactive Buddy", L"The buddy from the Flash game Interactive Buddy lives on your desktop in front of the sim: drag him, throw him, give him toys. Bring your own copy of the game (Interactive Buddy v1.01 or v1.02, a .swf file; v1.02 has the pistol and machine gun; it isn't included) and pick it with Choose SWF. Once his file is chosen, his own settings are in the Buddy tab", &fun.buddy, "Overlay.Fun.Buddy"),
	};
	// The Buddy tab (shown once his file is chosen): the desktop Interactive Buddy's own settings, then the sim's
	FunRow buddyRows[] = {
		{ L"Buddy" },
		Check(L"Interactive Buddy", L"Turns him on or off. The game file is your own copy of Interactive Buddy v1.01 or v1.02 (a .swf file; it isn't included)", &fun.buddy, "Overlay.Fun.Buddy"),
		Slide(L"Size", L"How big Buddy is, whatever the sim's pixel size (100% is the game's own size)", &fun.buddySize, 25, 300, "Overlay.Buddy.Size", 8),
		{ L"Windows", L"Walk through: windows don't touch him. Active window: as in the desktop Interactive Buddy, he stands on the top edge of the window you're using (and its bottom edge from inside it) and rides along when you move it. Solid: every window and icon is solid to him like it is to sand (needs Open windows are walls)", nullptr, nullptr, 0, 0, "Overlay.Buddy.Windows", 0, 1 },
		Slide(L"Volume", L"How loud the game's sounds are (far left: silent)", &fun.buddyVolume, 0, 100, "Overlay.Buddy.Volume", 8),
		Check(L"Smooth motion", L"The game's 120 Hz smoothing of his movement", &fun.buddySmooth, "Overlay.Buddy.Smooth"),
		Check(L"Clicks go to the game", L"On: every left click on the sim is the game's (throw grenades, fire guns, use the item you picked anywhere), not TPT's. Off: only clicks on Buddy, his toys and the game's windows are; the rest draw with TPT. Ctrl+Alt+B switches it from any app", &fun.buddyClicks, "Overlay.Buddy.GameClicks"),
		{ L"Game" },
		{ L"Menus", L"The game's own menus, as in the desktop app (also on right-clicking Buddy): load and save, skins and the skin store, items and weapons, modes and the mode store, the game's settings, help", nullptr, nullptr, 0, 0, "", 0, 2 },
		{ L"Save", L"Brings in your save from the desktop Interactive Buddy app (money, skins, items, modes). Buddy's save here is kept beside it as a .bak", nullptr, nullptr, 0, 0, "", 0, 3 },
		{ L"Speech" },
		Check(L"Speech bubbles", L"What Buddy says shows in a bubble over him", &fun.buddySpeech, "Overlay.Buddy.Speech"),
		Slide(L"Speech bubble size", L"How big his speech bubbles are", &fun.buddySpeechSize, 25, 200, "Overlay.Buddy.SpeechSize", 8),
		{ L"Toys" },
		Slide(L"Toys at once", L"How many of the game's toys and objects can be out at the same time (the game's own default is 6). Changing it clears the ones out now. Far left: no limit", &fun.buddyToys, 0, 100, "Overlay.Buddy.Toys", 9),
		{ L"Buddy and the sim" },
		Check(L"Pets meet Buddy", L"The pets walk over to Buddy and hop hello, and run when he is thrown past them or something blows up by him", &fun.buddyPets, "Overlay.Fun.BuddyPets"),
		Check(L"Buddy gets wet and frosty", L"Water and rain leave Buddy darker for a while, and anything below -50 C (cryo, liquid nitrogen) frosts him pale blue", &fun.buddyLooks, "Overlay.Fun.BuddyLooks"),
		Check(L"Buddy glows when shocked", L"A spark, lightning or electrons running through Buddy make him flicker bright blue-white for half a second. (Sparks already run on through him into metal he touches, and into metal powder he carries)", &fun.buddyGlow, "Overlay.Fun.BuddyGlow"),
		Check(L"Footprints", L"Walking through sand and other powders, Buddy's feet push it aside and leave dents; with wet feet he leaves drops of water on dry ground", &fun.buddyTracks, "Overlay.Fun.BuddyTracks"),
		Check(L"Splash", L"Thrown into sand or water, Buddy sends it flying up and out around where he lands", &fun.buddySplash, "Overlay.Fun.BuddySplash"),
		Check(L"Fireman", L"When something burns near Buddy (and he isn't on fire himself), he sprays water at it from his hand for a few seconds, then rests before the next fire. When the fire is out he's pleased with himself and hops", &fun.buddyFireman, "Overlay.Fun.BuddyFireman"),
		Check(L"Buddy carries sand and water", L"When his hand goes into sand, water or another powder or liquid that isn't hot, he scoops up a handful that rides on his hand. Swing or throw him and it flies off; otherwise he drops it after a few seconds", &fun.buddyCarry, "Overlay.Fun.BuddyCarry"),
		Check(L"Buddy's weapons hit the sim", L"Bullets from his pistol and machine gun knock sand, water and gas along their path, break glass and throw sparks off what stops them; flames from his flamethrower, fireballs and molotov cocktails are real fire that lights wood, oil, gunpowder and anything else that burns; his stun gun on him sparks the metal he touches; a bullet ends where sand, water, a solid or a TPT wall stops it; the medieval flail's ball shoves sand and water; missiles trail real smoke", &fun.buddyWeapons, "Overlay.Fun.BuddyWeapons"),
		Check(L"Only TPT fire from his items", L"With the one above: the game's own flames are hidden, so his flamethrower, fireballs and molotovs make only TPT fire", &fun.buddyFireOnly, "Overlay.Fun.BuddyFireOnly"),
		Check(L"Buddy's gravity items pull the sim", L"His weak and strong gravity vortexes and the gravity shifter also pull sand, water and gas toward them, not just Buddy", &fun.buddyGravity, "Overlay.Fun.BuddyGravity"),
		Check(L"TPT black holes pull Buddy", L"Black holes (NBHL) near him pull him and his toys in the way the game's own Gravity Vortex does: a few weakly, 100 or more strongly", &fun.buddyWells, "Overlay.Fun.BuddyWells"),
		Check(L"Buddy's toys feel the sim", L"Balls, grenades, infants, the magical orb and the radio rest on sand, float in water (the bowling ball sinks), get blown by explosions and splash what they land in hard. TPT fire, lava, sparks or a blast set off his grenades, mines and molotovs", &fun.buddyToysSim, "Overlay.Fun.BuddyToys"),
		Check(L"Buddy's fist shoves sand", L"With the game's Fist picked (Items, Hand), moving it through sand, snow or water shoves them along the way it moves, harder the faster", &fun.buddyFist, "Overlay.Fun.BuddyFist"),
		Check(L"Buddy's gore is TPT blood", L"In the game's gore mode (Modes), his gore spills TPT blood (BLOD) where it flies: it stains sand, clouds water and feeds plants", &fun.buddyBlood, "Overlay.Fun.BuddyBlood"),
		Check(L"Buddy runs from fire and lava", L"Fire, lava or plasma close to Buddy and he runs away from it, the way he runs in his own game. (With Fireman on he still sprays water at fires further away)", &fun.buddyFlee, "Overlay.Fun.BuddyFlee"),
		Check(L"Buddy gets dirty", L"Coal, dust, gunpowder, clay, oil, broken metal and smoke leave dark smudges on Buddy, more the longer he's in them. Water washes them off; otherwise they wear off in about a minute", &fun.buddyDirt, "Overlay.Fun.BuddyDirt"),
		Check(L"Pets dance to Buddy's radio", L"When Buddy dances to his radio, the desktop pets dance along with him", &fun.buddyDance, "Overlay.Fun.BuddyDance"),
		Check(L"Buddy feeds the pets", L"Now and then, when Buddy is calm and a pet is near, he drops a few seeds for it. Pets go and eat seeds near them", &fun.buddyFeed, "Overlay.Fun.BuddyFeed"),
		Check(L"Buddy's hoses spray TPT water", L"Water from Buddy's hoses (wide, narrow and fire hose) is also real TPT water: it soaks sand, puts out fires and floods", &fun.buddyWater, "Overlay.Fun.BuddyWater"),
		Check(L"Buddy's game modes reach the sim", L"The game's own modes (Modes menu) act on the sim too: Earthquake shakes sand and water about while the screen shakes, and Low Gravity makes them fall about a third as hard (TPT's vertical gravity)", &fun.buddyModes, "Overlay.Fun.BuddyModes"),
		Check(L"TPT stickmen fight Buddy", L"A TPT stickman or fighter (STKM, STKM2, FIGH) that runs into Buddy kicks him away and scares him, at most once a second", &fun.buddyStickmen, "Overlay.Fun.BuddyStickmen"),
		Check(L"Buddy's scripts make TPT stuff", L"The game's scripting engine (Modes, Scripting Engine Access) can make TPT particles: assign(tpt, \"SAND\") makes 10 sand at the mouse; set tptX and tptY for the place and tptN for how many (up to 500), before the tpt line", &fun.buddyScripts, "Overlay.Fun.BuddyScripts"),
		Check(L"Buddy's radio shakes the sim", L"While his radio plays music, TPT sand, water and other powders and liquids near it hop on the beat (a steady beat, not the song's own)", &fun.buddyRadio, "Overlay.Fun.BuddyRadio"),
		Check(L"Buddy cries real tears", L"When Buddy is very sad, TPT water drips from his eyes; from both eyes the sadder he is", &fun.buddyTears, "Overlay.Fun.BuddyTears"),
		Check(L"TPT gold is Buddy's money", L"TPT gold (GOLD) that Buddy touches is picked up and added to his game money, a dollar a pixel", &fun.buddyGold, "Overlay.Fun.BuddyGold"),
		Check(L"Hot floors make Buddy hop", L"Standing on something warm (60 to 400 C, hot but not burning) Buddy hops from foot to foot", &fun.buddyHotFloor, "Overlay.Fun.BuddyHotFloor"),
		Check(L"Only TPT water from his hoses", L"With the one above: the game's own water drops are hidden, so his hoses spray only TPT water (Buddy still feels the spray)", &fun.buddyWaterOnly, "Overlay.Fun.BuddyWaterOnly"),
	};
	bool BuddyTabShown() { return !GlobalPrefs::Ref().Get("Overlay.Fun.BuddySwf", ByteString("")).empty(); }
	// Interactive Buddy's file: the game's .swf, from the user (turns him on)
	void ChooseBuddySwf()
	{
#ifdef _WIN32
		wchar_t file[MAX_PATH] = L"";
		OPENFILENAMEW of{};
		of.lStructSize = sizeof(of);
		of.lpstrFilter = L"Flash games (*.swf) *.swf ";
		of.lpstrFile = file;
		of.nMaxFile = MAX_PATH;
		of.lpstrTitle = L"Your copy of Interactive Buddy";
		of.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		if (!GetOpenFileNameW(&of)) return;
		GlobalPrefs::Ref().Set("Overlay.Fun.BuddySwf", Platform::WinNarrow(file));
		fun.buddy = true;
		GlobalPrefs::Ref().Set("Overlay.Fun.Buddy", true);
#endif
	}

	// paper planes in flight or landed (sim pixels; landed: ticks since), and when the rainbow goes (GetTickCount64)
	struct PaperPlane { float x, y, vx, vy; int landed; };
	std::vector<PaperPlane> paperPlanes;
	ULONGLONG rainbowUntil = 0;
	int funHour = 12; // the local hour, for drawing
	bool rainbowTwice = false; // this one is a double rainbow
	HWND mainWindow = nullptr;    // TPT's own window
	std::string recording;        // Overlay.Keys.<name> being recorded in the Overlay tab, or empty
	std::function<void()> later; // run on the next tick, outside any window's message handling
	bool rebuildPalette = false;  // the UI scale changed: remake the side window (not from inside its own click)
	int reopenTab = -1;
	int S(float v)
	{
		return int(std::lround(v * uiScale));
	}

