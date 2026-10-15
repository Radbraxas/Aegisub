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
/// Draws dithered pixel clouds drifting across a dark gradient, plus a burst
/// of pixels wherever the user clicks. Pure computation into an RGB buffer so
/// that it does not depend on any GUI toolkit.

#pragma once

#include <cstdint>
#include <vector>

class AboutBackdrop {
public:
	/// @param cell Size in pixels of one "digital" pixel of the clouds
	explicit AboutBackdrop(int cell = 4);

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
		int color;
	};

	struct Burst {
		double x, y;
		double age = 0;
		std::vector<Particle> particles;
	};

	int cell;
	double time = 0;
	uint32_t seed = 0x9E3779B9u;
	std::vector<Burst> bursts;

	// Cached background gradient for the current size
	int bg_width = 0, bg_height = 0;
	std::vector<uint8_t> background;

	void BuildBackground(int width, int height);
	double Random();
};
