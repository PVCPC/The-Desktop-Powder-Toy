// Part of OverlayWindows.cpp (included inside OverlayWindows.cpp's anonymous namespace): every look's window chrome and controls (Vista ... macOS 26)
	// Windows Vista Aero.
	struct Vista : Theme
	{
		uint32_t glassTop = 0x7FA8D0, glassBottom = 0x4C7BAA; // the default Aero "sky" tint
		int glassAlpha = 205;

		Vista()
		{
			name = L"Windows Vista";
			glow = 5;
		}

		// The glass frame: a translucent tinted body, a glossy upper half on the title bar, dark outer and light
		// inner edges. The client area (below the title bar, inset by the frame) is left for the caller.
		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, glassTop, glassBottom, glassAlpha);
			cv.RoundRect(1, 1, cv.w - 2, titleH / 2, r, 0xFFFFFF, 0xFFFFFF, 60, true); // gloss
			Extra(cv, titleH);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 170);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(80), S(5));
			// The client area's sunken edge.
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x000000, 90);
		}
		virtual void Extra(Canvas &, int) {}

		// Vista's glossy red close pill, or the light glass minimise and maximise buttons. hover brightens them.
		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			bool close = kind == CaptionClose;
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			uint32_t top = close ? (hover ? 0xF3A08A : 0xE0907C) : (hover ? 0xF0F7FF : 0xE3ECF6);
			uint32_t bottom = close ? (hover ? 0xD8341A : 0xC1361D) : (hover ? 0x5FA3E8 : 0x9AB5D3);
			cv.RoundRect(rc.left, rc.top, w, h, S(3), top, bottom, close ? 255 : 235);
			cv.RoundRect(rc.left + 1, rc.top + 1, w - 2, h / 2, S(3), 0xFFFFFF, 0xFFFFFF, 70, true);
			cv.RoundFrame(rc.left, rc.top, w, h, S(3), close ? 0x5A1E12 : 0x1F3B5A, 220);
			int cx = rc.left + w / 2, cy = rc.top + h / 2;
			if (close)
			{
				Glyph(cv, cx, cy, CaptionClose, 0xFFFFFF, S(7), S(2));
			}
			else if (kind == CaptionMax)
			{
				// A white window outline with a thick top, outlined dark.
				int bw = S(9), bh = S(7), bx = cx - bw / 2, by = cy - bh / 2;
				cv.RoundFrame(bx - 1, by - 1, bw + 2, bh + 2, 0, 0x1F3B5A, 200);
				cv.RoundFrame(bx, by, bw, bh, 0, 0xFFFFFF, 255);
				cv.RoundRect(bx, by, bw, S(2), 0, 0xFFFFFF, 0xFFFFFF, 255);
			}
			else
			{
				// The white bar, outlined dark like Vista's.
				int bw = S(9), bh = S(3), bx = cx - bw / 2, by = cy + S(1);
				cv.RoundRect(bx - 1, by - 1, bw + 2, bh + 2, 0, 0x1F3B5A, 0x1F3B5A, 200);
				cv.RoundRect(bx, by, bw, bh, 0, 0xFFFFFF, 0xFFFFFF, 255);
			}
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xF2F2F2, 0xEAF6FD, 0xE5F4FC }, mid1[] = { 0xEBEBEB, 0xD9F0FC, 0xC4E5F6 },
				mid2[] = { 0xDDDDDD, 0xBEE6FD, 0x98D1EF }, bottom[] = { 0xCFCFCF, 0xA7D9F5, 0x68B3DB },
				edge[] = { 0x707070, 0x3C7FB1, 0x2C628B };
			int r = S(3);
			cv.RoundRect(x, y, w, h / 2, r, top[state], mid1[state], 255, true);
			cv.RoundRect(x, y + h / 2, w, h - h / 2, r, mid2[state], bottom[state], 255);
			// Square off the seam between the halves.
			for (int i = 0; i < w; i++) cv.Blend(x + i, y + h / 2 - 1, mid1[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
			cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0xFFFFFF, 120);
		}

		void Popup(Canvas &cv) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, glassTop, glassBottom, glassAlpha);
			cv.RoundRect(1, 1, cv.w - 2, cv.h / 2, r, 0xFFFFFF, 0xFFFFFF, 60, true);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 170);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
		}
	};

	// Windows 7: paler, clearer glass with the diagonal light streaks, and wider caption buttons.
	struct Seven : Vista
	{
		Seven()
		{
			name = L"Windows 7";
			glassTop = 0xA9C8EA, glassBottom = 0x7FA4CF, glassAlpha = 185;
			capW[0] = 26, capW[1] = 26, capW[2] = 45, capH = 19, capTop = 0;
		}

		void Extra(Canvas &cv, int) override
		{
			// three soft bands, only where the frame already is
			for (int band = 0; band < 3; band++)
			{
				int x0 = cv.w / 5 + band * S(70) * (band + 1), bw = S(14) + band * S(8);
				for (int y = 0; y < cv.h; y++)
					for (int x = x0 - y / 2; x < x0 - y / 2 + bw; x++)
						if (x >= 0 && x < cv.w && (cv.px[size_t(y) * cv.w + x] >> 24)) cv.Blend(x, y, 0xFFFFFF, 28);
			}
		}
	};

	// Windows 8: flat, a solid coloured frame, a centred title and flat square caption buttons.
	struct Eight : Theme
	{
		static constexpr uint32_t frame = 0x6A9BD8, frameEdge = 0x4A7CBE;

		Eight()
		{
			name = L"Windows 8";
			radius = 0, boxRadius = 0;
			capW[0] = 26, capW[1] = 27, capW[2] = 45, capH = 20, capTop = 0;
			heading = 0x000000, navInk = 0x000000, edge = 0xACACAC, groupEdge = 0xDCDCDC;
			tabOff[0] = 0xF0F0F0, tabOff[1] = 0xE5E5E5, tabHot[0] = 0xECF4FC, tabHot[1] = 0xDCECFC, tabHotEdge = 0x7EB4EA;
			status[0] = status[1] = 0xF0F0F0, status[2] = 0xD7D7D7;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0xE5E5E5;
			navHot[0] = navHot[1] = 0xE5F3FB, navHot[2] = 0x70C0E7, navSel[0] = navSel[1] = 0xCBE8F6, navSel[2] = 0x26A0DA;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x707070, boxHot[0] = boxHot[1] = 0xF3F9FF, boxHot[2] = 0x3399FF;
			mark = 0x000000, dot[0] = dot[1] = 0x333333;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, frame, frame, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, frameEdge, 255);
			int tw = text->Measure(title).cx;
			text->Draw(cv, std::max(S(10), (cv.w - tw) / 2), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(130));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, frameEdge, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (close) cv.RoundRect(rc.left, rc.top, w, h, 0, hover ? 0xE04343 : 0xC75050, hover ? 0xE04343 : 0xC75050, 255);
			else if (hover) cv.RoundRect(rc.left, rc.top, w, h, 0, 0xFFFFFF, 0xFFFFFF, 70);
			Glyph(cv, rc.left + w / 2, rc.top + h / 2 + (kind == CaptionMin ? S(3) : 0), kind, close ? 0xFFFFFF : 0x000000, S(8), S(1.5f));
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xF0F0F0, 0xECF4FC, 0xDAECFC }, bottom[] = { 0xE5E5E5, 0xDCECFC, 0xC4E0FC }, edge[] = { 0xACACAC, 0x7EB4EA, 0x569DE5 };
			cv.RoundRect(x, y, w, h, 0, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, 0, edge[state], 255);
		}
	};

	// Windows 10: a white frame with a thin border, full-height caption buttons and flat controls.
	struct Ten : Theme
	{
		Ten()
		{
			name = L"Windows 10";
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 46, capH = 28, capTop = 1, capInset = 1;
			heading = 0x000000, navInk = 0x000000, edge = 0xD9D9D9, groupEdge = 0xDCDCDC, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xF0F0F0, tabHot[0] = tabHot[1] = 0xD8EAF9, tabHotEdge = 0x7EB4EA;
			status[0] = status[1] = 0xF0F0F0, status[2] = 0xD7D7D7;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0xF0F0F0;
			navHot[0] = navHot[1] = navHot[2] = 0xE5F3FF, navSel[0] = navSel[1] = 0xCCE8FF, navSel[2] = 0x99D1FF;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x333333, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x0078D7;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFFF, 0xFFFFFF, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x6E6E6E, 200);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(160));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (hover) cv.RoundRect(rc.left, rc.top, w, h, 0, close ? 0xE81123 : 0xE5E5E5, close ? 0xE81123 : 0xE5E5E5, 255);
			Glyph(cv, rc.left + w / 2, rc.top + h / 2, kind, close && hover ? 0xFFFFFF : 0x000000, S(10), 1);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t fill[] = { 0xE1E1E1, 0xE5F1FB, 0xCCE4F7 }, edge[] = { 0xADADAD, 0x0078D7, 0x005499 };
			cv.RoundRect(x, y, w, h, 0, fill[state], fill[state], 255);
			cv.RoundFrame(x, y, w, h, 0, edge[state], 255);
		}
	};

	// Windows 10 in light and dark mode (dark: Windows-10-Dark's colours and controls)
	struct TenLight : Ten { TenLight() { colour = L"Light"; } };
	struct TenDark : Ten
	{
		TenDark()
		{
			colour = L"Dark";
			page = 0x202020, ink = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0xFFFFFF, navInk = 0xFFFFFF, edge = 0x3F3F3F, groupEdge = 0x3F3F3F, strip = 0x191919;
			tabOff[0] = tabOff[1] = 0x2B2B2B, tabHot[0] = tabHot[1] = 0x3A3A3A, tabHotEdge = 0x5A5A5A;
			status[0] = status[1] = 0x191919, status[2] = 0x2B2B2B;
			nav[0] = nav[1] = 0x191919, nav[2] = 0x2B2B2B;
			navHot[0] = navHot[1] = navHot[2] = 0x2D2D2D, navSel[0] = navSel[1] = 0x4D4D4D, navSel[2] = 0x666666;
			box[0] = box[1] = 0x202020, box[2] = 0xCCCCCC, boxHot[0] = boxHot[1] = 0x202020, boxHot[2] = 0xFFFFFF;
			mark = 0xFFFFFF, dot[0] = dot[1] = 0xFFFFFF;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0x202020, 0x202020, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x434343, 255);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0xFFFFFF, false, cv.w - S(160));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (hover) cv.RoundRect(rc.left, rc.top, w, h, 0, close ? 0xE81123 : 0x333333, close ? 0xE81123 : 0x333333, 255);
			Glyph(cv, rc.left + w / 2, rc.top + h / 2, kind, 0xFFFFFF, S(10), 1);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0x2B2B2B, 0x2B2B2B, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x767676, 255);
		}
	};

	// Classic Mac OS (Platinum): grey windows, a ridged title bar with the name centred on a plain patch, a close
	// box on the left, zoom and collapse boxes on the right, black-edged rounded buttons.
	struct ClassicMac : Theme
	{
		ClassicMac()
		{
			name = L"Mac OS 8-9";
			page = 0xDDDDDD, pressedInk = 0xFFFFFF, heading = 0x000000, navInk = 0x000000, edge = 0x666666, groupEdge = 0x888888, strip = 0xDDDDDD;
			tabOff[0] = 0xCCCCCC, tabOff[1] = 0xBBBBBB, tabHot[0] = 0xE6E6E6, tabHot[1] = 0xD0D0D0, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xDDDDDD, status[2] = 0x888888;
			nav[0] = nav[1] = 0xEEEEEE, nav[2] = 0x888888;
			navHot[0] = navHot[1] = 0xE0E0F0, navHot[2] = 0xB0B0D0, navSel[0] = navSel[1] = 0xCCCCFF, navSel[2] = 0x666699;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x000000, boxHot[0] = boxHot[1] = 0xEEEEEE, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 4, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 13, capH = 13, capTop = 8, capGap = 5, capInset = 8;
			capLeft[CaptionClose] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0xFFFFFF, 220);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			// the ridges
			for (int y = S(6); y < titleH - S(6); y += 2)
				for (int x = S(5); x < cv.w - S(5); x++)
				{
					cv.Blend(x, y, 0xFFFFFF, 255);
					cv.Blend(x, y + 1, 0x999999, 255);
				}
			auto size = text->Measure(title, true);
			int tx = std::max(S(30), (cv.w - int(size.cx)) / 2);
			cv.RoundRect(tx - S(8), S(4), std::min(int(size.cx), cv.w - S(110)) + S(16), titleH - S(8), 0, page, page, 255);
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, 0x000000, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x888888, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			cv.RoundRect(rc.left - S(3), rc.top - S(2), w + S(6), h + S(4), 0, page, page, 255); // clear of the ridges
			cv.RoundRect(rc.left, rc.top, w, h, 0, hover ? 0x999999 : 0xEEEEEE, hover ? 0x777777 : 0xBBBBBB, 255);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			if (kind == CaptionMax) cv.RoundFrame(rc.left, rc.top, w / 2 + 1, h / 2 + 1, 0, 0x000000, 255);
			if (kind == CaptionMin)
				for (int i = 0; i < 2; i++) cv.RoundRect(rc.left, rc.top + h / 2 - S(1) + i * S(2), w, 1, 0, 0x000000, 0x000000, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xEEEEEE, 0xF6F6F6, 0x666666 }, bottom[] = { 0xCCCCCC, 0xD8D8D8, 0x444444 };
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 255);
			if (state != 2) cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0xFFFFFF, 150);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0xFFFFFF, 220);
		}
	};

	// Modern macOS: a light grey window with big rounded corners, the three coloured lights on the left, a centred
	// title, white rounded buttons that turn the accent blue when picked, a segmented control for tabs.
	struct ModernMac : Theme
	{
		ModernMac()
		{
			name = L"macOS 11-15";
			page = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0x6E6E73, navInk = 0x000000, edge = 0xD0D0D0, groupEdge = 0xDCDCDC, strip = 0xECECEC;
			segmentTabs = true;
			status[0] = status[1] = 0xECECEC, status[2] = 0xD8D8D8;
			nav[0] = nav[1] = 0xE8E8EC, nav[2] = 0xD8D8D8;
			navHot[0] = navHot[1] = navHot[2] = 0xDCDCE0, navSel[0] = navSel[1] = navSel[2] = 0xCFCFD5;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0xBFBFBF, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x9A9A9A;
			boxOn = 0x007AFF, mark = 0xFFFFFF, dot[0] = dot[1] = 0xFFFFFF;
			radius = 10, boxRadius = 3;
			capW[0] = capW[1] = capW[2] = 12, capH = 12, capTop = 8, capGap = 8, capInset = 12;
			capLeft[0] = capLeft[1] = capLeft[2] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(10);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xEDEDED, 0xE6E6E6, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 60);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, true, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xD0D0D0, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFEBC2E, 0x28C840, 0xFF5F57 }, rim[] = { 0xDEA123, 0x1AAB29, 0xE0443E }, ink[] = { 0x985712, 0x0B650D, 0x4D0000 };
			int d = rc.right - rc.left;
			cv.RoundRect(rc.left, rc.top, d, d, d / 2, fill[kind], fill[kind], 255);
			cv.RoundFrame(rc.left, rc.top, d, d, d / 2, rim[kind], 255);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, ink[kind], S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xFFFFFF, 0xF5F5F5, 0x3B95FF }, bottom[] = { 0xFFFFFF, 0xF0F0F0, 0x0A6CF0 }, edge[] = { 0xC4C4C4, 0xB4B4B4, 0x0A62D8 };
			int r = S(5);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(10), 0xF2F2F2, 0xF2F2F2, 245);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(10), 0x000000, 45);
		}
	};

	// macOS in light and dark mode (dark: macOS-Dark's colours and controls)
	struct MacLight : ModernMac { MacLight() { colour = L"Light"; } };
	struct MacDark : ModernMac
	{
		MacDark()
		{
			colour = L"Dark";
			page = 0x1E1E1E, ink = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0x98989D, navInk = 0xFFFFFF, edge = 0x3A3A3A, groupEdge = 0x3A3A3A, strip = 0x2A2A2A;
			status[0] = status[1] = 0x2A2A2A, status[2] = 0x1A1A1A;
			nav[0] = nav[1] = 0x2B2B2F, nav[2] = 0x1A1A1A;
			navHot[0] = navHot[1] = navHot[2] = 0x38383C, navSel[0] = navSel[1] = navSel[2] = 0x46464B;
			box[0] = box[1] = 0x3A3A3C, box[2] = 0x5A5A5E, boxHot[0] = boxHot[1] = 0x3A3A3C, boxHot[2] = 0x7A7A7E;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(10);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0x323232, 0x2C2C2C, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 200);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 30);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0xDCDCDC, true, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x1A1A1A, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0x6A6A6E, 0x75757A, 0x3B95FF }, bottom[] = { 0x5C5C60, 0x66666A, 0x0A6CF0 };
			int r = S(5);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 90);
		}

		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			if (on || hot) cv.RoundRect(rc.left, rc.top + S(1), w, h - S(4), S(5), on ? 0x636366 : 0x48484A, on ? 0x636366 : 0x48484A, 255);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(10), 0x2C2C2E, 0x2C2C2E, 245);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(10), 0x000000, 160);
		}
	};

	void Line(Canvas &cv, int x, int y, int w, int h, uint32_t c)
	{
		cv.RoundRect(x, y, w, h, 0, c, c, 255);
	}

	// The Windows 9x 3D edge: raised (buttons, windows) or sunken (fields, pressed buttons).
	void Bevel(Canvas &cv, int x, int y, int w, int h, bool sunken)
	{
		uint32_t o1 = sunken ? 0x808080 : 0xFFFFFF, o2 = sunken ? 0xFFFFFF : 0x000000, i1 = sunken ? 0x000000 : 0xDFDFDF, i2 = sunken ? 0xDFDFDF : 0x808080;
		Line(cv, x, y, w, 1, o1), Line(cv, x, y, 1, h, o1), Line(cv, x, y + h - 1, w, 1, o2), Line(cv, x + w - 1, y, 1, h, o2);
		Line(cv, x + 1, y + 1, w - 2, 1, i1), Line(cv, x + 1, y + 1, 1, h - 2, i1), Line(cv, x + 1, y + h - 2, w - 2, 1, i2), Line(cv, x + w - 2, y + 1, 1, h - 2, i2);
	}

	// Windows 98: grey 3D chrome, a navy-to-blue title bar, small bevelled caption buttons.
	struct NinetyEight : Theme
	{
		uint32_t titleLeft = 0x000080, titleRight = 0x1084D0; // the title bar fades from titleLeft to this

		NinetyEight()
		{
			name = L"Windows 98";
			page = 0xC0C0C0, heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x808080, groupEdge = 0x808080, strip = 0xC0C0C0;
			tabOff[0] = tabOff[1] = tabHot[0] = tabHot[1] = 0xC0C0C0, tabHotEdge = 0x808080;
			status[0] = status[1] = 0xC0C0C0, status[2] = 0x808080;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x808080;
			navHot[0] = navHot[1] = navHot[2] = 0xFFFFFF, navSel[0] = navSel[1] = navSel[2] = 0x000080;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x808080, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 16, capH = 14, capTop = 8, capGap = 2, capInset = 8;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			Bevel(cv, 0, 0, cv.w, cv.h, false);
			int bx = S(4), by = S(4), bw = cv.w - 2 * bx, bh = titleH - S(6);
			for (int i = 0; i < bw; i++)
			{
				float t = bw > 1 ? float(i) / (bw - 1) : 0;
				auto ch = [t](int a, int b) { return uint32_t(a + (b - a) * t); };
				Line(cv, bx + i, by, 1, bh, (ch(int(titleLeft >> 16), int(titleRight >> 16)) << 16) | (ch(int(titleLeft >> 8 & 0xFF), int(titleRight >> 8 & 0xFF)) << 8) | ch(int(titleLeft & 0xFF), int(titleRight & 0xFF)));
			}
			text->Draw(cv, S(8), by + (bh - text->Measure(title, true).cy) / 2, title, 0xFFFFFF, true, cv.w - S(90));
			Bevel(cv, border - 2, titleH - 2, cv.w - 2 * border + 4, cv.h - titleH - border + 4, true);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			Line(cv, rc.left, rc.top, w, h, page);
			Bevel(cv, rc.left, rc.top, w, h, false);
			(void)hover; // 98 had no hover
			Glyph(cv, rc.left + w / 2, rc.top + h / 2 + (kind == CaptionMin ? S(2) : 0), kind, 0x000000, S(7), 2);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			Line(cv, x, y, w, h, page);
			Bevel(cv, x, y, w, h, state == 2);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, page);
			Bevel(cv, 0, 0, cv.w, cv.h, false);
		}
	};

	// Windows XP's own Luna images (resources/luna.png, made by resources/luna.py from luna.msstyles, via
	// github.com/listumps/wine_themes): Blue, Olive Green and Silver blocks side by side, each part at the same place
	// in each, its states stacked down it.
	enum LunaPart { LunaCaption, LunaLeft, LunaRight, LunaBottom, LunaCapButton, LunaClose, LunaCloseGlyph, LunaMinGlyph, LunaMaxGlyph, LunaRestoreGlyph, LunaButton, LunaCheck, LunaRadio, LunaGroup, LunaTab, LunaPane };
	constexpr int lunaParts[][4] = { // x, y, w, h in a colour's block
		{ 0, 0, 66, 58 }, { 0, 58, 5, 62 }, { 0, 120, 5, 62 }, { 0, 182, 49, 10 }, { 0, 192, 21, 168 }, { 66, 0, 21, 168 }, { 66, 168, 13, 104 },
		{ 66, 272, 13, 104 }, { 87, 0, 13, 104 }, { 87, 104, 13, 104 }, { 87, 208, 20, 115 }, { 107, 0, 13, 156 }, { 107, 156, 13, 104 },
		{ 107, 260, 22, 20 }, { 107, 280, 18, 90 }, { 129, 0, 48, 48 },
	};
	constexpr int lunaBlock = 177;

	// State f (of n) of part p in column col over x, y, w, h: its l, r, t, b source pixels at the edges kept (scaled
	// as the UI is), the middle stretched, as the msstyles' SizingMargins.
	void Slice(Canvas &cv, const PlaneAdapter<std::vector<pixel_rgba>> *atlas, int sx, int sy, int sw, int sh, int x, int y, int w, int h, int l, int r, int t, int b)
	{
		if (!atlas || w <= 0 || h <= 0) return;
		int aw = atlas->Size().X;
		// a destination pixel's source along one side: margins lo and hi (source px) are S()-sized, shrunk to fit
		auto along = [&](int len, int src, int lo, int hi) {
			int dlo = S(float(lo)), dhi = S(float(hi));
			if (dlo + dhi > len) dlo = len * lo / std::max(1, lo + hi), dhi = len - dlo;
			std::vector<int> m(len);
			for (int d = 0; d < len; d++)
				m[d] = d < dlo ? d * lo / std::max(1, dlo)
				     : d >= len - dhi ? src - hi + (d - (len - dhi)) * hi / std::max(1, dhi)
				     : lo + (d - dlo) * (src - lo - hi) / std::max(1, len - dlo - dhi);
			return m;
		};
		auto mx = along(w, sw, l, r), my = along(h, sh, t, b);
		auto *px = atlas->data();
		for (int j = 0; j < h; j++)
			for (int i = 0; i < w; i++)
			{
				uint32_t c = px[size_t(sy + my[j]) * aw + sx + mx[i]];
				cv.Blend(x + i, y + j, c & 0xFFFFFF, c >> 24);
			}
	}

	void Luna(Canvas &cv, int col, LunaPart p, int f, int n, int x, int y, int w, int h, int l = 0, int r = 0, int t = 0, int b = 0)
	{
		static auto atlas = format::PixelsFromPNG(luna_png.AsCharSpan()), atlas2 = format::PixelsFromPNG(xp2_png.AsCharSpan());
		int sh = lunaParts[p][3] / n;
		if (col >= 3) // xp2.png: Royale, Royale Noir, Zune
			return Slice(cv, atlas2.get(), (col - 3) * lunaBlock + lunaParts[p][0], lunaParts[p][1] + f * sh, lunaParts[p][2], sh, x, y, w, h, l, r, t, b);
		Slice(cv, atlas.get(), col * lunaBlock + lunaParts[p][0], lunaParts[p][1] + f * sh, lunaParts[p][2], sh, x, y, w, h, l, r, t, b);
	}

	// A look's controls in images of a faithful recreation (resources/skins.py, OverlaySkins.h): a part's state f, or
	// false when the look draws that part itself.
	enum SkinPart { SkinButton, SkinCheck, SkinRadio, SkinTab, SkinClose, SkinMin, SkinMax };
	struct Skin
	{
		std::span<const char> png;
		const int (*parts)[4][8];
		bool Draw(Canvas &cv, SkinPart p, int f, int x, int y, int w, int h) const
		{
			static std::map<const char *, std::unique_ptr<PlaneAdapter<std::vector<pixel_rgba>>>> atlases;
			auto &atlas = atlases[png.data()];
			if (!atlas) atlas = format::PixelsFromPNG(png);
			auto &c = parts[p][f];
			if (!c[2]) return false;
			Slice(cv, atlas.get(), c[0], c[1], c[2], c[3], x, y, w, h, c[4], c[5], c[6], c[7]);
			return true;
		}
	};

#ifdef _WIN32
	// Windows' own drawing of a control (the visual style in use, its real Windows 11 parts on Windows 11). False when
	// Windows can't (no visual style).
	// Something GDI draws (draw(dc, rc)), drawn over black and over white: the difference gives its alpha.
	void CaptureGdi(Canvas &cv, int x, int y, int w, int h, const std::function<void(HDC, RECT &)> &draw)
	{
		BITMAPINFO bi{};
		bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32 };
		HDC dc = CreateCompatibleDC(nullptr);
		uint32_t *px[2];
		HBITMAP bm[2];
		RECT rc{ 0, 0, w, h };
		for (int k = 0; k < 2; k++)
		{
			bm[k] = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px[k], nullptr, 0);
			std::fill(px[k], px[k] + size_t(w) * h, k ? 0xFFFFFFFFu : 0xFF000000u);
			SelectObject(dc, bm[k]);
			draw(dc, rc);
		}
		GdiFlush();
		for (int j = 0; j < h; j++)
			for (int i = 0; i < w; i++)
			{
				uint32_t b = px[0][size_t(j) * w + i], wh = px[1][size_t(j) * w + i];
				int a = 255 - (int((wh >> 8) & 0xFF) - int((b >> 8) & 0xFF));
				if (a <= 0) continue;
				auto ch = [&](int sh) { return uint32_t(std::min(255, int((b >> sh) & 0xFF) * 255 / a)) << sh; };
				cv.Blend(x + i, y + j, ch(16) | ch(8) | ch(0), uint8_t(a));
			}
		DeleteDC(dc);
		DeleteObject(bm[0]), DeleteObject(bm[1]);
	}

	bool NativePart(Canvas &cv, const wchar_t *cls, int part, int state, int x, int y, int w, int h)
	{
		static std::map<std::wstring, HTHEME> handles;
		auto &t = handles[cls];
		if (!t) t = OpenThemeData(nullptr, cls);
		if (!t || w <= 0 || h <= 0) return false;
		CaptureGdi(cv, x, y, w, h, [&](HDC dc, RECT &rc) { DrawThemeBackground(t, dc, part, state, &rc, nullptr); });
		return true;
	}

	bool IsWindows11()
	{
		static int build = [] {
			RTL_OSVERSIONINFOW v{ sizeof(v) };
			auto get = (LONG(WINAPI *)(RTL_OSVERSIONINFOW *))GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
			return get && !get(&v) ? int(v.dwBuildNumber) : 0;
		}();
		return build >= 22000;
	}
#endif

	// A look drawn by Windows itself where this PC is that Windows (Windows 11 on Windows 11), else by hand.
	template<class Base> struct Native : Base
	{
#ifdef _WIN32
		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (!IsWindows11() || !NativePart(cv, L"BUTTON", BP_PUSHBUTTON, state == 2 ? PBS_PRESSED : state == 1 ? PBS_HOT : PBS_NORMAL, x, y, w, h)) Base::Button(cv, x, y, w, h, state);
		}
		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			if (!IsWindows11() || !NativePart(cv, L"TAB", TABP_TABITEM, on ? TIS_SELECTED : hot ? TIS_HOT : TIS_NORMAL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top)) Base::Tab(cv, rc, on, hot);
		}
		bool CheckBox(Canvas &cv, int x, int y, int b, bool on, bool round, bool hot) override
		{
			return (IsWindows11() && NativePart(cv, L"BUTTON", round ? BP_RADIOBUTTON : BP_CHECKBOX, (on ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL) + hot, x, y, b, b)) || Base::CheckBox(cv, x, y, b, on, round, hot);
		}
#endif
	};

	template<class Base> struct Skinned : Base
	{
		static constexpr SkinPart captionParts[] = { SkinMin, SkinMax, SkinClose }; // by CaptionKind
		Skin skin;
		Skinned(Skin skin) : skin(skin)
		{
			for (int k = 0; k < 3; k++) // caption buttons at their images' size
				if (int w = skin.parts[captionParts[k]][0][2]) this->capW[k] = w, this->capH = skin.parts[captionParts[k]][0][3];
		}
		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (!skin.Draw(cv, SkinButton, state, x, y, w, h)) Base::Button(cv, x, y, w, h, state);
		}
		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			if (!skin.Draw(cv, SkinTab, on ? 2 : hot, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top)) Base::Tab(cv, rc, on, hot);
		}
		bool CheckBox(Canvas &cv, int x, int y, int b, bool on, bool round, bool hot) override
		{
			return skin.Draw(cv, round ? SkinRadio : SkinCheck, (on ? 2 : 0) + hot, x, y, b, b) || Base::CheckBox(cv, x, y, b, on, round, hot);
		}
		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			if (!skin.Draw(cv, captionParts[kind], hover, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top)) Base::CaptionButton(cv, rc, hover, kind);
		}
	};

	// Windows XP Luna, in its own images: Blue (col 0), Olive Green (1) or Silver (2).
	struct XP : Theme
	{
		int col;
		uint32_t captionInk, captionShadow;

		XP(int col = 0) : col(col)
		{
			static const wchar_t *colours[] = { L"Blue", L"Olive Green", L"Silver", L"Royale", L"Royale Noir", L"Zune" };
			static const uint32_t inks[] = { 0xFFFFFF, 0xFFFFFF, 0x0E1010, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF }, shadows[] = { 0x0A1883, 0x41400A, 0xC7C2D1, 0x0A1883, 0x000000, 0x000000 },
				headings[] = { 0x0046D5, 0x99540A, 0x0046D5, 0x0046D5, 0x1C2A4A, 0xC8501C };
			name = L"Windows XP", colour = colours[col], captionInk = inks[col], captionShadow = shadows[col];
			page = 0xFCFCFE, heading = headings[col], navInk = 0x215DC6, navSelInk = 0xFFFFFF, edge = 0x919B9C, groupEdge = 0xD0D0BF, strip = col == 2 ? 0xE0DFE3 : col >= 3 ? 0xEBE9ED : 0xECE9D8;
			tabOff[0] = 0xFEFEFE, tabOff[1] = 0xECEBE6, tabHot[0] = 0xFFFFFF, tabHot[1] = 0xF4F3EE, tabHotEdge = 0xE68B2C;
			status[0] = status[1] = strip, status[2] = 0xACA899;
			nav[0] = 0xD6DFF7, nav[1] = 0xC6D3F7, nav[2] = 0x7BA2E7;
			navHot[0] = navHot[1] = 0xE8EEFB, navHot[2] = 0x7BA2E7, navSel[0] = navSel[1] = navSel[2] = 0x316AC5;
			radius = 3, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 21, capH = 21, capTop = 5, capGap = 3, capInset = 5;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Luna(cv, col, LunaCaption, 0, 2, 0, 0, cv.w, titleH, 28, 35, 9, 17);
			Luna(cv, col, LunaLeft, 0, 2, 0, titleH, border, cv.h - titleH - border, 0, 0, 2, 2);
			Luna(cv, col, LunaRight, 0, 2, cv.w - border, titleH, border, cv.h - titleH - border, 0, 0, 2, 2);
			Luna(cv, col, LunaBottom, 0, 2, 0, cv.h - border, cv.w, border, 5, 5, 2, 2);
			auto size = text->Measure(title, true);
			int ty = (titleH - size.cy) / 2;
			text->Draw(cv, S(9) + 1, ty + 1, title, captionShadow, true, cv.w - S(100));
			text->Draw(cv, S(9), ty, title, captionInk, true, cv.w - S(100));
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top, g = S(13);
			Luna(cv, col, kind == CaptionClose ? LunaClose : LunaCapButton, hover, 8, rc.left, rc.top, w, h, 5, 5, 5, 5);
			Luna(cv, col, kind == CaptionClose ? LunaCloseGlyph : kind == CaptionMin ? LunaMinGlyph : LunaMaxGlyph, hover, 8, rc.left + (w - g) / 2, rc.top + (h - g) / 2, g, g);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			Luna(cv, col, LunaButton, state, 5, x, y, w, h, 8, 8, 9, 9); // normal, hot, pressed (then disabled, default)
		}

		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			Luna(cv, col, LunaTab, on ? 2 : hot ? 1 : 0, 5, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 6, 6, 6, 6);
		}

		bool CheckBox(Canvas &cv, int x, int y, int b, bool on, bool round, bool hot) override
		{
			Luna(cv, col, round ? LunaRadio : LunaCheck, (on ? 4 : 0) + hot, round ? 8 : 12, x, y, b, b);
			return true;
		}

		bool GroupFrame(Canvas &cv, int x, int y, int w, int h) override
		{
			Luna(cv, col, LunaGroup, 0, 1, x, y, w, h, 4, 4, 4, 4);
			return true;
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFE1, 0xFFFFE1, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
		}
	};

	// TempleOS: white windows with a double blue border, blue text, [X]-style caption buttons in text.
	struct Temple : Theme
	{
		static constexpr uint32_t blue = 0x0000AA, cyan = 0x55FFFF;

		Temple()
		{
			name = L"TempleOS";
			font = L"TempleOS", fontScale = 0.65f; // its own, from github.com/PhilipPanda/TempleOS-Theme: wide, so smaller
			ink = blue, pressedInk = 0xFFFFFF, heading = blue, navInk = blue, navSelInk = 0xFFFFFF, edge = blue, groupEdge = blue, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xFFFFFF, tabHot[0] = tabHot[1] = cyan, tabHotEdge = blue;
			status[0] = status[1] = 0xFFFFFF, status[2] = blue;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = blue;
			navHot[0] = navHot[1] = cyan, navHot[2] = blue, navSel[0] = navSel[1] = navSel[2] = blue;
			box[0] = box[1] = 0xFFFFFF, box[2] = blue, boxHot[0] = boxHot[1] = cyan, boxHot[2] = blue;
			mark = blue, dot[0] = dot[1] = blue;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 24, capH = 20, capTop = 4, capGap = 2, capInset = 10;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFFF, 0xFFFFFF, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, blue, 255);
			cv.RoundFrame(S(3), S(3), cv.w - 2 * S(3), cv.h - 2 * S(3), 0, blue, 255);
			auto size = text->Measure(title);
			int tx = std::max(S(12), (cv.w - int(size.cx)) / 2);
			Line(cv, tx - S(6), 0, std::min(int(size.cx), cv.w - S(120)) + S(12), S(4), 0xFFFFFF); // the title sits in the border
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, blue, false, cv.w - S(120));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, blue, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const wchar_t *labels[] = { L"[_]", L"[\u25A1]", L"[X]" };
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			if (hover) Line(cv, rc.left, rc.top, w, h, blue);
			auto size = text->Measure(labels[kind]);
			text->Draw(cv, rc.left + (w - size.cx) / 2, rc.top + (h - size.cy) / 2, labels[kind], hover ? 0xFFFFFF : blue);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			uint32_t fill = state == 2 ? blue : state == 1 ? cyan : 0xFFFFFF;
			Line(cv, x, y, w, h, fill);
			cv.RoundFrame(x, y, w, h, 0, blue, 255);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, blue, 255);
			cv.RoundFrame(S(3), S(3), cv.w - 2 * S(3), cv.h - 2 * S(3), 0, blue, 255);
		}
	};

	// Windows 95: 98 without the title gradient.
	struct NinetyFive : NinetyEight
	{
		NinetyFive()
		{
			name = L"Windows 95";
			titleRight = 0x000080;
		}
	};

	// Windows ME: 98's chrome, ME's darker navy fading to light blue
	struct Millennium : NinetyEight
	{
		Millennium()
		{
			name = L"Windows ME";
			titleLeft = 0x0A246A, titleRight = 0xA6CAF0;
		}
	};

	// Windows 3.1: a grey sizing border, a solid navy title bar with the name centred, the control-menu box on the
	// left, arrow buttons on the right, white dialogs.
	struct Three : Theme
	{
		Three()
		{
			name = L"Windows 3.1";
			heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x000000, groupEdge = 0x000000, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xC0C0C0, tabHot[0] = tabHot[1] = 0xC0C0C0, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xFFFFFF, status[2] = 0x000000;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x000000;
			navHot[0] = navHot[1] = navHot[2] = 0xFFFFFF, navSel[0] = navSel[1] = navSel[2] = 0x000080;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x000000, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 20, capH = 20, capTop = 4, capGap = 0, capInset = 4;
			capLeft[CaptionClose] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xC0C0C0);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			int b = S(4);
			Line(cv, b, b, cv.w - 2 * b, titleH - b, 0x000080);
			cv.RoundFrame(b - 1, b - 1, cv.w - 2 * b + 2, titleH - b + 2, 0, 0x000000, 255);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(30), (cv.w - int(size.cx)) / 2), b + (titleH - b - size.cy) / 2, title, 0xFFFFFF, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x000000, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top, cx = rc.left + w / 2, cy = rc.top + h / 2;
			(void)hover;
			Line(cv, rc.left, rc.top, w, h, 0xC0C0C0);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			if (kind == CaptionClose)
			{
				// the control-menu box: a white bar with a dark shadow
				Line(cv, cx - S(6) + 1, cy - 1 + 1, S(12), S(3), 0x808080);
				Line(cv, cx - S(6), cy - 1, S(12), S(3), 0xFFFFFF);
				cv.RoundFrame(cx - S(6), cy - 1, S(12), S(3), 0, 0x000000, 255);
				return;
			}
			Bevel(cv, rc.left + 1, rc.top + 1, w - 2, h - 2, false);
			int k = S(4);
			for (int i = 0; i <= k; i++) // an arrow: down to minimise, up to maximise
				Line(cv, cx - i, kind == CaptionMin ? cy + k / 2 - i : cy - k / 2 + i, 2 * i + 1, 1, 0x000000);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			Line(cv, x, y, w, h, 0xC0C0C0);
			cv.RoundFrame(x, y, w, h, S(2), 0x000000, 255);
			uint32_t lt = state == 2 ? 0x808080 : 0xFFFFFF, rb = state == 2 ? 0xC0C0C0 : 0x808080;
			Line(cv, x + 1, y + 1, w - 2, S(2), lt), Line(cv, x + 1, y + 1, S(2), h - 2, lt);
			Line(cv, x + 1, y + h - 1 - S(2), w - 2, S(2), rb), Line(cv, x + w - 1 - S(2), y + 1, S(2), h - 2, rb);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0x000000, 255);
		}
	};

	// Windows 1.0 and 2.0 (B00merang's Windows-1.0 / Windows-2.0): white windows with black outlines, a blue title bar
	// with the name in a black box, rounded white buttons; 1.0 picks in blue, 2.0 in red
	struct Early : Three
	{
		static constexpr uint32_t bar = 0x5656EF;

		Early(const wchar_t *n, uint32_t pick)
		{
			name = n;
			tabOff[0] = tabOff[1] = tabHot[0] = tabHot[1] = 0xFFFFFF;
			navSel[0] = navSel[1] = navSel[2] = pick, pressedInk = 0xFFFFFF;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			int b = S(4);
			Line(cv, b, b, cv.w - 2 * b, titleH - b, bar);
			cv.RoundFrame(b - 1, b - 1, cv.w - 2 * b + 2, titleH - b + 2, 0, 0x000000, 255);
			auto size = text->Measure(title, true);
			int tw = std::min(int(size.cx), cv.w - S(110)), tx = std::max(S(30), (cv.w - tw) / 2) - S(12);
			Line(cv, tx, b, tw + S(24), titleH - b, 0x000000);
			text->Draw(cv, tx + S(12), b + (titleH - b - size.cy) / 2, title, 0xFFFFFF, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x000000, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			uint32_t c = state == 2 ? navSel[0] : 0xFFFFFF;
			cv.RoundRect(x, y, w, h, S(4), c, c, 255);
			cv.RoundFrame(x, y, w, h, S(4), 0x000000, 255);
		}
	};

	// Windows 11: Windows 10's layout with rounded corners, a light Mica grey, rounded controls and the accent blue on
	// picked buttons and ticked boxes.
	struct Eleven : Ten
	{
		Eleven()
		{
			name = L"Windows 11";
			radius = 8, boxRadius = 4, pressedInk = 0xFFFFFF, strip = 0xF3F3F3, edge = 0xE5E5E5;
			tabOff[0] = tabOff[1] = 0xF3F3F3, tabHot[0] = tabHot[1] = 0xEAEAEA, tabHotEdge = 0xE5E5E5;
			status[0] = status[1] = 0xF3F3F3, status[2] = 0xE5E5E5;
			nav[0] = nav[1] = 0xF3F3F3, nav[2] = 0xE5E5E5;
			navHot[0] = navHot[1] = navHot[2] = 0xEAEAEA, navSel[0] = navSel[1] = navSel[2] = 0xE0E0E0;
			box[0] = box[1] = 0xFDFDFD, box[2] = 0x8A8A8A, boxHot[0] = boxHot[1] = 0xF3F3F3, boxHot[2] = 0x8A8A8A;
			boxOn = 0x005FB8, mark = 0xFFFFFF, dot[0] = dot[1] = 0xFFFFFF;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(8);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xF3F3F3, 0xF3F3F3, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 60);
			text->Draw(cv, S(12), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(160));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (hover)
			{
				uint32_t c = close ? 0xC42B1C : 0xE9E9E9;
				cv.RoundRect(rc.left, rc.top, w, h, close ? S(8) : 0, c, c, 255, true); // the close button follows the corner
				if (close) Line(cv, rc.left, rc.top, w - S(8), h, c);
			}
			Glyph(cv, rc.left + w / 2, rc.top + h / 2, kind, close && hover ? 0xFFFFFF : 0x000000, S(10), 1);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			int r = S(4);
			uint32_t fill = state == 2 ? 0x005FB8 : state == 1 ? 0xF6F6F6 : 0xFBFBFB;
			cv.RoundRect(x, y, w, h, r, fill, fill, 255);
			cv.RoundFrame(x, y, w, h, r, state == 2 ? 0x005FB8 : 0xE5E5E5, 255);
			if (state != 2) Line(cv, x + r, y + h - 1, w - 2 * r, 1, 0xCCCCCC); // the darker bottom edge
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(8), 0xF9F9F9, 0xF9F9F9, 250);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(8), 0x000000, 50);
		}
	};

	// System 1-6: black and white, a striped title bar, the close box on the left and the zoom box on the right.
	struct System6 : ClassicMac
	{
		uint32_t stripe = 0x000000, stripeGap = 0xFFFFFF, boxFill = 0xFFFFFF;

		System6()
		{
			name = L"System 1-6";
			page = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x000000, groupEdge = 0x000000, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = tabHot[0] = tabHot[1] = 0xFFFFFF, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xFFFFFF, status[2] = 0x000000;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x000000;
			navHot[0] = navHot[1] = 0xFFFFFF, navHot[2] = 0x000000, navSel[0] = navSel[1] = navSel[2] = 0x000000;
			box[0] = box[1] = boxHot[0] = boxHot[1] = 0xFFFFFF, box[2] = boxHot[2] = 0x000000;
			radius = 6, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 11, capH = 11, capTop = 7, capGap = 4, capInset = 8;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Line(cv, 0, 0, cv.w, cv.h, page);
			cv.RoundFrame(0, 0, cv.w - 1, cv.h - 1, 0, 0x000000, 255);
			Line(cv, cv.w - 1, 1, 1, cv.h - 1, 0x000000), Line(cv, 1, cv.h - 1, cv.w - 1, 1, 0x000000); // the drop shadow
			Line(cv, 1, 1, cv.w - 3, titleH - 2, stripeGap);
			for (int y = S(5); y < titleH - S(5); y += 2) Line(cv, S(3), y, cv.w - S(3) * 2 - 1, 1, stripe);
			auto size = text->Measure(title, true);
			int tx = std::max(S(30), (cv.w - int(size.cx)) / 2);
			Line(cv, tx - S(8), S(3), std::min(int(size.cx), cv.w - S(110)) + S(16), titleH - S(6), stripeGap);
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, 0x000000, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 1, cv.h - titleH - border + 1, 0, 0x000000, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			Line(cv, rc.left - S(2), rc.top - S(2), w + S(4), h + S(4), stripeGap); // clear of the stripes
			Line(cv, rc.left, rc.top, w, h, hover ? 0x000000 : boxFill);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			uint32_t ink = hover ? 0xFFFFFF : 0x000000;
			if (kind == CaptionMax) cv.RoundFrame(rc.left, rc.top, w / 2 + 1, h / 2 + 1, 0, ink, 255);
			if (kind == CaptionMin) Line(cv, rc.left + S(2), rc.top + h / 2, w - S(4), 1, ink);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			int r = S(6);
			cv.RoundRect(x, y, w, h, r, state == 2 ? 0x000000 : boxFill, state == 2 ? 0x000000 : boxFill, 255);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 255);
			if (state == 1) cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0x000000, 255);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w - 1, cv.h - 1, 0, 0x000000, 255);
			Line(cv, cv.w - 1, 1, 1, cv.h - 1, 0x000000), Line(cv, 1, cv.h - 1, cv.w - 1, 1, 0x000000);
		}
	};

	// System 7: the same shapes, with grey stripes, light grey dialogs and a lavender highlight.
	struct System7 : System6
	{
		System7()
		{
			name = L"System 7";
			stripe = 0x999999, stripeGap = 0xDDDDDD, boxFill = 0xEEEEEE;
			page = 0xEEEEEE, strip = 0xDDDDDD, navSelInk = 0x000000;
			tabOff[0] = tabOff[1] = 0xDDDDDD, tabHot[0] = tabHot[1] = 0xEEEEEE;
			status[0] = status[1] = 0xDDDDDD, nav[0] = nav[1] = 0xFFFFFF;
			navHot[0] = navHot[1] = 0xEEEEFF, navSel[0] = navSel[1] = 0xCCCCFF, navSel[2] = 0x6666AA;
		}
	};

	// A glossy round light (Aqua gel), lighter at the top with a highlight.
	void Gel(Canvas &cv, int x, int y, int w, int h, int r, uint32_t c, uint32_t rim, int shine)
	{
		cv.RoundRect(x, y, w, h, r, Canvas::Mix(c, 0xFFFFFF, 0.45f), c, 255);
		cv.RoundFrame(x, y, w, h, r, rim, 255);
		if (shine) cv.RoundRect(x + w / 5, y + 1, w - 2 * (w / 5), h * 2 / 5, std::min(r, h / 5), 0xFFFFFF, 0xFFFFFF, shine);
	}

	// Mac OS X 10.0-10.6 (Aqua): pinstripes, glossy gel lights and pill buttons, blue gel for the picked one.
	struct Aqua : ModernMac
	{
		Aqua()
		{
			name = L"Mac OS X 10.0-10.6";
			page = 0xF2F2F2, strip = 0xE8E8E8, heading = 0x000000;
			status[0] = status[1] = 0xE8E8E8;
			nav[0] = 0xE8EDF5, nav[1] = 0xD6DDE5, navSel[0] = 0x6AA2E8, navSel[1] = navSel[2] = 0x3B7DD8, navSelInk = 0xFFFFFF;
			boxOn = 0x3D8EF0, radius = 6, boxRadius = 3;
			capW[0] = capW[1] = capW[2] = 14, capH = 14, capTop = 6, capGap = 7, capInset = 10;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xECECEC, 0xECECEC, 255, true);
			for (int y = r; y < cv.h - 1; y += 2) Line(cv, 1, y, cv.w - 2, 1, 0xF8F8F8); // the pinstripes
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 110, true);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x000000, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xA0A0A0, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xE8A92B, 0x5DB33B, 0xD9332A }, rim[] = { 0x9A6A10, 0x2F7A1C, 0x8E1A14 };
			int d = rc.right - rc.left;
			Gel(cv, rc.left, rc.top, d, d, d / 2, fill[kind], rim[kind], 200);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2 + 1, kind, 0x4A1A00, S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			bool on = state == 2;
			Gel(cv, x, y, w, h, h / 2, on ? 0x3D8EF0 : state == 1 ? 0xE6E6E6 : 0xDADADA, on ? 0x1A4D9A : 0x8A8A8A, 150);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(4), 0xF8F8F8, 0xF8F8F8, 240);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(4), 0x000000, 80);
		}
	};

	// OS X 10.7-10.9: a grey gradient title bar, softer lights, white gradient buttons, blue gradient when picked.
	struct Lion : ModernMac
	{
		Lion()
		{
			name = L"OS X 10.7-10.9";
			page = 0xEDEDED, strip = 0xE3E3E3, heading = 0x333333;
			nav[0] = 0xE6EBF0, nav[1] = 0xDCE2E8, navSel[0] = 0x6CB3FA, navSel[1] = navSel[2] = 0x2A7BE0, navSelInk = 0xFFFFFF;
			box[0] = 0xFFFFFF, box[1] = 0xE8E8E8, box[2] = 0x8E8E8E, boxOn = 0, mark = 0x000000, dot[0] = 0x6CB3FA, dot[1] = 0x0D7BF2;
			radius = 4, boxRadius = 3;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(5);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xEDEDED, 0xEDEDED, 255, true);
			cv.RoundRect(0, 0, cv.w, titleH, r, 0xEBEBEB, 0xD5D5D5, 255, true);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 100, true);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xAAAAAA, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFDBC40, 0x34C84A, 0xFC605C }, rim[] = { 0xDE9F34, 0x27AA35, 0xDE4A47 };
			int d = rc.right - rc.left;
			Gel(cv, rc.left, rc.top, d, d, d / 2, fill[kind], rim[kind], 70);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, 0x5A2A00, S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xFFFFFF, 0xFFFFFF, 0x6CB3FA }, bottom[] = { 0xF1F1F1, 0xE6E6E6, 0x0D7BF2 }, edge[] = { 0xA6A6A6, 0x8E8E8E, 0x2A68C8 };
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
		}
	};

	// OS X 10.10-10.15: flat, a plain light title bar with a regular-weight title, flat lights, flat blue when picked.
	struct Yosemite : ModernMac
	{
		Yosemite()
		{
			name = L"OS X 10.10-10.15";
			radius = 5, boxRadius = 3;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(5);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xECECEC, 0xECECEC, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 70);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xD0D0D0, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			uint32_t fill = state == 2 ? 0x1E8BFF : state == 1 ? 0xF7F7F7 : 0xFFFFFF;
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, fill, fill, 255);
			cv.RoundFrame(x, y, w, h, r, state == 2 ? 0x0A70F0 : 0xC8C8C8, 255);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(5), 0xF2F2F2, 0xF2F2F2, 245);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(5), 0x000000, 45);
		}
	};

	// macOS 26 (Liquid Glass): real glass. The window is kept out of screen captures; every display refresh the
	// screen behind it is grabbed and lightly blurred, and the glass shapes show it bent like a lens towards their
	// rounded edges with a little colour split, a specular rim lit from the top left (and its bounce bottom right).
	// The frame and the floating sidebar are glass; capsule tabs and buttons catch the light. Where each glass pixel
	// samples from is worked out once per paint (Bake), so a refresh is only the grab and a gather.
	struct Glass : ModernMac
	{

		Glass()
		{
			name = L"macOS 26";
			page = 0xFFFFFF, heading = 0x86868B, navInk = 0x1D1D1F, edge = 0xE3E3E8, groupEdge = 0xE8E8ED, stripAlpha = 0;
			navSel[0] = navSel[1] = 0xFFFFFF, navHot[0] = navHot[1] = 0xFFFFFF;
			box[2] = 0xC7C7CC, boxOn = 0x0088FF, radius = 18, boxRadius = 5;
			capW[0] = capW[1] = capW[2] = 14, capH = 14, capTop = 9, capGap = 9, capInset = 18;
		}

		// Apple's "regular" Liquid Glass (HIG, Materials): the backdrop is blurred and its luminosity pulled into a
		// band labels read over, and the whole look turns dark (white labels) over dark content.
		std::atomic<bool> dark{ false };
		bool Live() const override { return true; }
		uint32_t TabInk(bool on) const override { (void)on; return dark ? 0xF5F5F7 : 0x1D1D1F; }
		bool Prepare(Backdrop &bd, HWND hwnd, int w, int h) override
		{
			RECT wr;
			GetWindowRect(hwnd, &wr);
			int m = S(24); // room for the lens to reach past the edge
			int bw = w + 2 * m, bh = h + 2 * m;
			int hw = bw / 2, hh = bh / 2;
			auto &half = bd.small;
			half.resize(size_t(hw) * hh);
#ifdef __APPLE__
			bd.grab.window = hwnd;
#endif
			if (!bd.grab.Grab(wr.left - m, wr.top - m, bw, bh, half.data()))
			{
				auto &backdrop = bd.px;
				backdrop.resize(size_t(bw) * bh);
				// no desktop duplication (remote desktop, old driver): GDI, much slower
				HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
				BITMAPINFO bi{};
				bi.bmiHeader = { sizeof(bi.bmiHeader), bw, -bh, 1, 32, BI_RGB };
				void *bits;
				auto dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
				auto old = SelectObject(dc, dib);
				BitBlt(dc, 0, 0, bw, bh, screen, wr.left - m, wr.top - m, SRCCOPY);
				memcpy(backdrop.data(), bits, backdrop.size() * 4);
				SelectObject(dc, old);
				DeleteObject(dib);
				DeleteDC(dc);
				ReleaseDC(nullptr, screen);
				for (int j = 0; j < hh; j++)
					for (int i = 0; i < hw; i++)
					{
						int sum[3] = {};
						for (int y = 0; y < 2; y++)
							for (int x = 0; x < 2; x++)
							{
								uint32_t p = backdrop[size_t(2 * j + y) * bw + 2 * i + x];
								sum[0] += p & 0xFF, sum[1] += p >> 8 & 0xFF, sum[2] += p >> 16 & 0xFF;
							}
						half[size_t(j) * hw + i] = uint32_t(sum[0] / 4) | uint32_t(sum[1] / 4) << 8 | uint32_t(sum[2] / 4) << 16;
					}
			}
			// two box blurs each way (about a gaussian): shapes show through, fine detail like text goes soft
			for (int pass = 0; pass < 2; pass++) Blur(half, bd.scratch, hw, hh, std::max(1, S(2)));
			// light or dark: the mean luma of what's seen through the glass, with some hysteresis
			// ponytail: one look per window; macOS decides per bar
			int64_t sum = 0, n = 0;
			for (uint32_t i : bd.probe)
				if (i < half.size()) sum += Luma(half[i]), n++;
			if (!n) return false;
			int mean = int(sum / n);
			bool was = dark;
			if (mean < 100) dark = true;
			else if (mean > 125) dark = false;
			return dark != was;
		}
		static int Luma(uint32_t c) { return int((c >> 16 & 0xFF) * 54 + (c >> 8 & 0xFF) * 183 + (c & 0xFF) * 19) >> 8; }

		// A box blur of radius r, w by h, both ways; tmp is scratch. Running sums, rows only (cache friendly).
		static void Blur(std::vector<uint32_t> &px, std::vector<uint32_t> &tmp, int w, int h, int r)
		{
			tmp.resize(px.size());
			uint32_t inv = 65536 / (2 * r + 1);
			auto pack = [inv](const int *s) { return (uint32_t(s[0]) * inv >> 16) | (uint32_t(s[1]) * inv >> 16) << 8 | (uint32_t(s[2]) * inv >> 16) << 16; };
			auto add = [](int *s, uint32_t c, int k) { s[0] += k * int(c & 0xFF), s[1] += k * int(c >> 8 & 0xFF), s[2] += k * int(c >> 16 & 0xFF); };
			for (int y = 0; y < h; y++) // across: px to tmp
			{
				const uint32_t *row = &px[size_t(y) * w];
				uint32_t *out = &tmp[size_t(y) * w];
				int s[3] = {};
				for (int i = -r; i <= r; i++) add(s, row[std::clamp(i, 0, w - 1)], 1);
				for (int x = 0; x < w; x++)
				{
					out[x] = pack(s);
					add(s, row[std::min(x + r + 1, w - 1)], 1), add(s, row[std::max(x - r, 0)], -1);
				}
			}
			std::vector<int> s(size_t(w) * 3); // down: tmp to px, a running sum per column
			for (int i = -r; i <= r; i++)
				for (int x = 0; x < w; x++) add(&s[3 * x], tmp[size_t(std::clamp(i, 0, h - 1)) * w + x], 1);
			for (int y = 0; y < h; y++)
			{
				const uint32_t *in = &tmp[size_t(std::min(y + r + 1, h - 1)) * w], *outgoing = &tmp[size_t(std::max(y - r, 0)) * w];
				uint32_t *out = &px[size_t(y) * w];
				for (int x = 0; x < w; x++)
				{
					out[x] = pack(&s[3 * x]);
					add(&s[3 * x], in[x], 1), add(&s[3 * x], outgoing[x], -1);
				}
			}
		}

		// Calls f(x, y, cover, nx, ny, depth) for each canvas pixel inside the shape: coverage, the outward normal
		// and the distance in from the rounded edge.
		template<class F> static void Walk(const Shape &s, int cw, int ch, F f)
		{
			float hw = s.w / 2.f, hh = s.h / 2.f;
			for (int j = std::max(0, -s.y); j < s.h && s.y + j < ch; j++)
				for (int i = std::max(0, -s.x); i < s.w && s.x + i < cw; i++)
				{
					float px = i + 0.5f - hw, py = j + 0.5f - hh;
					float qx = std::abs(px) - (hw - s.r), qy = std::abs(py) - (hh - s.r);
					float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f), ol = std::hypot(ox, oy);
					float dist = ol + std::min(std::max(qx, qy), 0.f) - s.r;
					float cover = std::clamp(0.5f - dist, 0.f, 1.f);
					if (cover <= 0) continue;
					float nx, ny;
					if (ol > 0) nx = ox / ol, ny = oy / ol;
					else if (qx > qy) nx = 1, ny = 0;
					else nx = 0, ny = 1;
					f(s.x + i, s.y + j, cover, px < 0 ? -nx : nx, py < 0 ? -ny : ny, -dist);
				}
		}

		// A glass rounded rectangle: frost (0-1) mixes in the tint, lens is how far the edge bends the view. While
		// painting it only cuts a hole in the UI; Underlay fills it every refresh.
		void GlassRect(Canvas &cv, int x, int y, int w, int h, int r, float frost, uint32_t tint, float lens)
		{
			shapes.push_back({ x, y, w, h, r, frost, tint, lens });
			Walk(shapes.back(), cv.w, cv.h, [&](int px, int py, float cover, float, float, float) {
				auto &p = cv.px[size_t(py) * cv.w + px];
				uint32_t keep = uint32_t(256 * (1 - cover));
				p = ((p & 0xFF00FF) * keep >> 8 & 0xFF00FF) | ((p >> 8 & 0xFF00FF) * keep & 0xFF00FF00);
			});
		}

		void Bake(Backdrop &bd, const Canvas &ui, std::vector<Baked> &out) override
		{
			int m = S(24), hw = (ui.w + 2 * m) / 2, hh = (ui.h + 2 * m) / 2;
			float band = float(S(26));
			auto at = [&](float x, float y) { // a pixel of the half-size backdrop
				return uint32_t(std::clamp(int(y + m) / 2, 0, hh - 1) * hw + std::clamp(int(x + m) / 2, 0, hw - 1));
			};
			for (auto &s : shapes)
				Walk(s, ui.w, ui.h, [&](int x, int y, float cover, float nx, float ny, float depth) {
					uint32_t idx = uint32_t(y) * ui.w + x;
					if (ui.px[idx] >> 24 == 255) return; // under solid UI: nothing to see
					float k = depth < band ? 1 - depth / band : 0;
					float bend = s.lens * (1 - std::sqrt(std::max(0.f, 1 - k * k))); // a dome: flat inside, steep at the rim
					// specular: a thin bright rim where the light (top left) or its bounce (bottom right) hits, a soft
					// glow just inside it, and a faint darker band where the lens turns
					float rim = std::clamp(1 - depth / S(1.8f), 0.f, 1.f), light = std::abs(-0.6f * nx - 0.8f * ny);
					float glow = std::clamp(1 - depth / S(7), 0.f, 1.f);
					float dark = 0.10f * k * (1 - k) * 4 * (1 - light), white = std::min(1.f, rim * (0.35f + 0.65f * light) + 0.3f * glow * glow * light);
					out.push_back({ idx,
						{ at(x - nx * bend * 1.18f, y - ny * bend * 1.18f), at(x - nx * bend, y - ny * bend), at(x - nx * bend * 0.82f, y - ny * bend * 0.82f) }, // red bends most, blue least
						s.tint, uint8_t(255 * cover), uint8_t(255 * s.frost), uint8_t(255 * dark), uint8_t(255 * white) });
				});
			bd.probe.clear();
			for (size_t i = 0; i < out.size(); i += 7) bd.probe.push_back(out[i].src[1]);
		}

		void Underlay(Backdrop &bd, Canvas &cv, const Canvas &ui, const std::vector<Baked> &baked) override
		{
			auto &half = bd.small, &scratch = bd.scratch;
			cv.w = ui.w, cv.h = ui.h, cv.px = ui.px;
			scratch.resize(cv.px.size());
			auto mix = [](uint32_t a, uint32_t b, uint32_t t) { // t of 255 toward b
				uint32_t o = 0;
				for (int sh = 0; sh < 24; sh += 8) o |= ((a >> sh & 0xFF) * (255 - t) + (b >> sh & 0xFF) * t) / 255 << sh;
				return o;
			};
			bool isDark = dark;
			for (auto &b : baked) scratch[b.idx] = 0;
			for (auto &b : baked) // the glass, shapes in paint order (premultiplied)
			{
				uint32_t c = (half[b.src[0]] & 0xFF0000) | (half[b.src[1]] & 0xFF00) | (half[b.src[2]] & 0xFF);
				// luminosity into a band: 43-100% light, 8-73% dark; hue and saturation kept
				int y = Luma(c), d = (isDark ? 20 : 110) + y * 165 / 255 - y;
				uint32_t lum = 0;
				for (int sh = 0; sh < 24; sh += 8) lum |= uint32_t(std::clamp(int(c >> sh & 0xFF) + d, 0, 255)) << sh;
				c = mix(mix(mix(lum, isDark ? 0x2C2C2E : b.tint, b.frost), 0, b.dark), 0xFFFFFF, b.white);
				uint32_t &g = scratch[b.idx], keep = 255 - b.cover, o = 0;
				for (int sh = 0; sh < 24; sh += 8) o |= ((c >> sh & 0xFF) * b.cover + (g >> sh & 0xFF) * keep) / 255 << sh;
				g = o | (b.cover + (g >> 24) * keep / 255) << 24;
			}
			for (auto &b : baked) // the UI over it
			{
				uint32_t u = ui.px[b.idx], g = scratch[b.idx], keep = 255 - (u >> 24), o = 0;
				for (int sh = 0; sh < 32; sh += 8) o |= ((u >> sh & 0xFF) + (g >> sh & 0xFF) * keep / 255) << sh;
				cv.px[b.idx] = o;
			}
		}

		// A capsule or panel over the page: a milky fill, a white rim brightest at the top, a faint dark outline.
		static void Pane(Canvas &cv, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int a)
		{
			cv.RoundRect(x, y, w, h, r, top, bottom, a);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 28);
			cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, std::max(0, r - 1), 0xFFFFFF, 200);
			cv.RoundRect(x + r, y + 1, w - 2 * r, 1, 0, 0xFFFFFF, 0xFFFFFF, 255); // the top glint
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			navInk = TabInk(false);
			GlassRect(cv, 0, 0, cv.w, cv.h, S(18), 0.04f, 0xFAFAFC, float(S(24)));
			cv.RoundFrame(0, 0, cv.w, cv.h, S(18), 0x000000, 40);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(100), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, TabInk(false), true, cv.w - S(200));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFFBD2E, 0x28C941, 0xFF5F57 }, ink[] = { 0x985712, 0x0B650D, 0x4D0000 };
			int d = rc.right - rc.left;
			cv.RoundRect(rc.left, rc.top, d, d, d / 2, Canvas::Mix(fill[kind], 0xFFFFFF, 0.15f), fill[kind], 255);
			cv.RoundFrame(rc.left, rc.top, d, d, d / 2, 0x000000, 35);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, ink[kind], S(6), std::max(1, S(1.3f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (state == 2) Pane(cv, x, y, w, h, h / 2, 0x3DA2FF, 0x0077F0, 255);
			else Pane(cv, x, y, w, h, h / 2, state == 1 ? 0xFFFFFF : 0xFBFBFD, state == 1 ? 0xF0F0F5 : 0xF2F2F6, 235);
		}

		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top - S(4);
			if (on && dark) Pane(cv, rc.left, rc.top + S(1), w, h, h / 2, 0x6C6C70, 0x5A5A5E, 230);
			else if (on) Pane(cv, rc.left, rc.top + S(1), w, h, h / 2, 0xFFFFFF, 0xF4F4F8, 230);
			else if (hot) cv.RoundRect(rc.left, rc.top + S(1), w, h, h / 2, 0xFFFFFF, 0xFFFFFF, dark ? 40 : 110);
		}

		void NavPane(Canvas &cv, int x, int y, int w, int h) override
		{
			cv.RoundRect(x, y, w + 1, h, 0, page, page, 255);
			GlassRect(cv, x + S(6), y + S(6), w - S(10), h - S(12), S(14), 0.3f, 0xF4F4F7, float(S(20)));
		}

		void NavItem(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (state == 0) return;
			cv.RoundRect(x + S(4), y, w - S(8), h, S(8), 0xFFFFFF, 0xFFFFFF, dark ? (state == 2 ? 70 : 35) : (state == 2 ? 200 : 110));
		}

		void Popup(Canvas &cv) override
		{
			GlassRect(cv, 0, 0, cv.w, cv.h, S(16), 0.3f, 0xFAFAFC, float(S(20)));
			cv.RoundFrame(0, 0, cv.w, cv.h, S(16), 0x000000, 40);
		}
	};

	Three threeTheme;
	NinetyFive ninetyFiveTheme;
	NinetyEight ninetyEightTheme;
	Millennium meTheme;
	Early twoTheme{ L"Windows 2.0", 0xFC5252 }, oneTheme{ L"Windows 1.0", 0x5057FF };
	XP xpTheme, xpOliveTheme{ 1 }, xpSilverTheme{ 2 }, xpRoyaleTheme{ 3 }, xpNoirTheme{ 4 }, xpZuneTheme{ 5 };
	Skinned<Vista> vistaTheme{ { skin_vista_png.AsCharSpan(), skin_vista_parts } };
	Skinned<Seven> sevenTheme{ { skin_seven_png.AsCharSpan(), skin_seven_parts } };
	Skinned<Eight> eightTheme{ { skin_eight_png.AsCharSpan(), skin_eight_parts } };
	Skinned<TenLight> tenTheme{ { skin_ten_png.AsCharSpan(), skin_ten_parts } };
	Native<Eleven> elevenTheme;
	Skinned<ClassicMac> classicMacTheme{ { skin_mac9_png.AsCharSpan(), skin_mac9_parts } };
	Skinned<MacLight> modernMacTheme{ { skin_macos_png.AsCharSpan(), skin_macos_parts } };
	Temple templeTheme;
	Skinned<System6> system6Theme{ { skin_system6_png.AsCharSpan(), skin_system6_parts } };
	Skinned<System7> system7Theme{ { skin_system7_png.AsCharSpan(), skin_system7_parts } };
	Skinned<Aqua> aquaTheme{ { skin_aqua_png.AsCharSpan(), skin_aqua_parts } };
	Skinned<Lion> lionTheme{ { skin_lion_png.AsCharSpan(), skin_lion_parts } };
	Skinned<Yosemite> yosemiteTheme{ { skin_yosemite_png.AsCharSpan(), skin_yosemite_parts } };
	Skinned<TenDark> tenDarkTheme{ { skin_tendark_png.AsCharSpan(), skin_tendark_parts } };
	Skinned<MacDark> macDarkTheme{ { skin_macosdark_png.AsCharSpan(), skin_macosdark_parts } };
	Glass glassTheme;
	Theme *const themes[] = { &threeTheme, &ninetyFiveTheme, &ninetyEightTheme, &xpTheme, &vistaTheme, &sevenTheme, &eightTheme, &tenTheme, &elevenTheme, &classicMacTheme, &modernMacTheme, &templeTheme,
		&system6Theme, &system7Theme, &aquaTheme, &lionTheme, &yosemiteTheme, &glassTheme, &xpOliveTheme, &xpSilverTheme, &xpRoyaleTheme, &xpNoirTheme, &xpZuneTheme, &tenDarkTheme, &macDarkTheme,
		&meTheme, &twoTheme, &oneTheme }; // new ones at the end: the pref is the index
	const std::vector<int> windowsThemes = { 27, 26, 0, 1, 2, 25, 3, 4, 5, 6, 7, 8 }, macThemes = { 12, 13, 9, 14, 15, 16, 10, 17 };
	constexpr int templeIndex = 11;
	// A generation's colour schemes, the first the one its picker shows (Windows XP: Blue, Olive Green, Silver, Royale, Royale Noir, Zune)
	const std::vector<std::vector<int>> themeColours = { { 3, 18, 19, 20, 21, 22 }, { 7, 23 }, { 10, 24 } };
	int Generation(int i)
	{
		for (auto &c : themeColours)
			if (std::count(c.begin(), c.end(), i)) return c[0];
		return i;
	}
	constexpr int defaultTheme = 4; // Vista
	Theme *theme = &vistaTheme;

