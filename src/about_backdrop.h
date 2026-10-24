// Copyright (c) 2026, Aegisub contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file about_backdrop.h
/// @brief Animated backdrop of the About dialog
///
/// An ASCII noise field: clouds made of monospace characters drift across a
/// dark background, with brighter characters along their edges and glyphs
/// that keep changing. Clicking plays a burst of characters at the cursor.
/// Pure computation into an RGB buffer, so it does not depend on any GUI
/// toolkit; the caller supplies the rendered glyphs.

#pragma once

#include <cstdint>
#include <vector>

class AboutBackdrop {
public:
	AboutBackdrop() = default;

	/// Set the glyphs to draw with
	/// @param cell_width  Width of one character cell in pixels
	/// @param cell_height Height of one character cell in pixels
	/// @param coverage    For each glyph, cell_width * cell_height bytes of
	///                    coverage (0-255), row-major, glyphs one after another
	void SetGlyphs(int cell_width, int cell_height, std::vector<uint8_t> coverage);

	/// Start a click burst at the given position (in pixels)
	void AddBurst(double x, double y);

	/// Advance the animation
	/// @param dt Elapsed time in seconds
	void Step(double dt);

	/// Draw the current frame
	/// @param rgb Buffer of width * height * 3 bytes, resized as needed
	void Render(std::vector<uint8_t>& rgb, int width, int height);

	/// Whether any click burst is still playing
	bool HasBursts() const { return !bursts.empty(); }

private:
	struct Particle {
		double x, y, vx, vy;
		int glyph_seed;
	};

	struct Burst {
		double x, y;
		double age;
		std::vector<Particle> particles;
	};

	int cell_w = 6, cell_h = 10;
	int glyph_count = 0;
	std::vector<uint8_t> glyphs;

	double time = 0;
	uint32_t seed = 0x9E3779B9u;
	std::vector<Burst> bursts;

	// Cached background for the current size
	int bg_width = 0, bg_height = 0;
	std::vector<uint8_t> background;

	void BuildBackground(int width, int height);
	void DrawGlyph(std::vector<uint8_t>& rgb, int width, int height, int x0, int y0,
	               unsigned glyph, double r, double g, double b, double alpha) const;
	double Random();
};
