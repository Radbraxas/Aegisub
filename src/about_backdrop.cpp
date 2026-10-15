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

/// @file about_backdrop.cpp
/// @brief Animated backdrop of the About dialog

#include "about_backdrop.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
struct RGB { double r, g, b; };

constexpr RGB grad_top{74, 81, 93};
constexpr RGB grad_bottom{24, 27, 33};
constexpr RGB burst_colors[] = {
	{240, 90, 110},  // coral
	{243, 210, 122}, // butter
	{159, 227, 210}, // mint
	{143, 208, 242}, // sky
};

/// 4x4 ordered dithering matrix, the source of the "digital" look
constexpr int bayer[4][4] = {
	{ 0,  8,  2, 10},
	{12,  4, 14,  6},
	{ 3, 11,  1,  9},
	{15,  7, 13,  5},
};

double DitherThreshold(int x, int y) {
	return (bayer[y & 3][x & 3] + 0.5) / 16.0;
}

double Smoothstep(double e0, double e1, double x) {
	double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
	return t * t * (3 - 2 * t);
}

double Hash(int x, int y) {
	uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return double((h ^ (h >> 16)) & 0xFFFFFF) / double(0xFFFFFF);
}

/// Smooth value noise in [0, 1]
double ValueNoise(double x, double y) {
	double fx = std::floor(x), fy = std::floor(y);
	int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
	double tx = x - fx, ty = y - fy;
	tx = tx * tx * (3 - 2 * tx);
	ty = ty * ty * (3 - 2 * ty);
	double a = Hash(ix, iy), b = Hash(ix + 1, iy);
	double c = Hash(ix, iy + 1), d = Hash(ix + 1, iy + 1);
	return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

/// Fractal noise: three octaves of value noise
double Fbm(double x, double y) {
	return 0.55 * ValueNoise(x, y) + 0.3 * ValueNoise(x * 2.03 + 17.1, y * 2.03 - 4.7)
		+ 0.15 * ValueNoise(x * 4.11 - 9.3, y * 4.11 + 21.9);
}

void Blend(uint8_t *px, RGB const& c, double alpha) {
	px[0] = static_cast<uint8_t>(px[0] + (c.r - px[0]) * alpha);
	px[1] = static_cast<uint8_t>(px[1] + (c.g - px[1]) * alpha);
	px[2] = static_cast<uint8_t>(px[2] + (c.b - px[2]) * alpha);
}

void FillCell(std::vector<uint8_t>& rgb, int width, int height, int x0, int y0, int size, RGB const& c, double alpha) {
	int x1 = std::min(width, x0 + size), y1 = std::min(height, y0 + size);
	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x)
			Blend(&rgb[(static_cast<size_t>(y) * width + x) * 3], c, alpha);
}

constexpr double burst_duration = 1.1;
}

AboutBackdrop::AboutBackdrop(int cell) : cell(std::max(1, cell)) { }

double AboutBackdrop::Random() {
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return double(seed & 0xFFFFFF) / double(0xFFFFFF);
}

void AboutBackdrop::AddBurst(double x, double y) {
	Burst burst{x, y, 0, {}};
	const int count = 22;
	for (int i = 0; i < count; ++i) {
		double angle = 2 * std::numbers::pi * (i + Random() * 0.6) / count;
		double speed = 70 + Random() * 130;
		burst.particles.push_back({x, y, std::cos(angle) * speed, std::sin(angle) * speed,
			static_cast<int>(Random() * std::size(burst_colors)) % static_cast<int>(std::size(burst_colors))});
	}
	bursts.push_back(std::move(burst));
}

void AboutBackdrop::Step(double dt) {
	dt = std::clamp(dt, 0.0, 0.1); // Avoid jumps after the window was hidden
	time += dt;

	const double drag = std::exp(-2.4 * dt);
	for (auto& burst : bursts) {
		burst.age += dt;
		for (auto& p : burst.particles) {
			p.x += p.vx * dt;
			p.y += p.vy * dt;
			p.vx *= drag;
			p.vy = p.vy * drag + 25 * dt; // A hint of gravity
		}
	}
	std::erase_if(bursts, [](Burst const& b) { return b.age >= burst_duration; });
}

void AboutBackdrop::BuildBackground(int width, int height) {
	bg_width = width;
	bg_height = height;
	background.resize(static_cast<size_t>(width) * height * 3);

	struct Glow { double x, y, radius, alpha; RGB color; };
	const Glow glows[] = {
		{0.15 * width, 0.10 * height, 0.45 * width, 0.35, {159, 227, 210}},
		{0.92 * width, 0.95 * height, 0.50 * width, 0.30, {246, 184, 166}},
		{0.60 * width, 0.00 * height, 0.35 * width, 0.20, {243, 224, 138}},
	};

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			double t = 0.45 * x / width + 0.55 * y / height;
			uint8_t *px = &background[(static_cast<size_t>(y) * width + x) * 3];
			px[0] = static_cast<uint8_t>(grad_top.r + (grad_bottom.r - grad_top.r) * t);
			px[1] = static_cast<uint8_t>(grad_top.g + (grad_bottom.g - grad_top.g) * t);
			px[2] = static_cast<uint8_t>(grad_top.b + (grad_bottom.b - grad_top.b) * t);
			for (auto const& g : glows) {
				double dx = (x - g.x) / g.radius, dy = (y - g.y) / g.radius;
				Blend(px, g.color, g.alpha * std::exp(-2.5 * (dx * dx + dy * dy)));
			}
		}
	}
}

void AboutBackdrop::Render(std::vector<uint8_t>& rgb, int width, int height) {
	if (width <= 0 || height <= 0) return;
	if (width != bg_width || height != bg_height)
		BuildBackground(width, height);
	rgb = background;

	const int cols = (width + cell - 1) / cell;
	const int rows = (height + cell - 1) / cell;

	// Two layers of clouds drifting right to left at different speeds, for depth
	struct Layer { double scale_x, scale_y, speed, offset, lo, hi, alpha; RGB color; };
	const Layer layers[] = {
		{0.050, 0.110, 0.20,  3.1, 0.50, 0.78, 0.16, {220, 228, 238}}, // far
		{0.034, 0.080, 0.45, 41.7, 0.55, 0.80, 0.30, {240, 244, 248}}, // near
	};

	for (int cy = 0; cy < rows; ++cy) {
		for (int cx = 0; cx < cols; ++cx) {
			const double threshold = DitherThreshold(cx, cy);
			for (auto const& l : layers) {
				double n = Fbm(cx * l.scale_x + time * l.speed + l.offset,
				               cy * l.scale_y + std::sin(time * 0.3 + l.offset) * 0.15 + l.offset);
				if (Smoothstep(l.lo, l.hi, n) > threshold)
					FillCell(rgb, width, height, cx * cell, cy * cell, cell, l.color, l.alpha);
			}
		}
	}

	// Click bursts: an expanding dithered ring plus pixels flying outwards
	for (auto const& b : bursts) {
		const double life = b.age / burst_duration;
		const double fade = 1 - life;

		const double ring_t = std::min(1.0, b.age / 0.9);
		const double radius = 6 + 110 * (1 - std::pow(1 - ring_t, 3));
		const int c0x = static_cast<int>((b.x - radius - cell) / cell), c1x = static_cast<int>((b.x + radius + cell) / cell);
		const int c0y = static_cast<int>((b.y - radius - cell) / cell), c1y = static_cast<int>((b.y + radius + cell) / cell);
		for (int cy = std::max(0, c0y); cy <= std::min(rows - 1, c1y); ++cy) {
			for (int cx = std::max(0, c0x); cx <= std::min(cols - 1, c1x); ++cx) {
				double dx = (cx + 0.5) * cell - b.x, dy = (cy + 0.5) * cell - b.y;
				double dist = std::sqrt(dx * dx + dy * dy);
				if (std::abs(dist - radius) < cell * 0.9 && DitherThreshold(cx, cy) < fade)
					FillCell(rgb, width, height, cx * cell, cy * cell, cell, {255, 255, 255}, 0.75 * fade);
			}
		}

		// Bright cross at the click point for the first moments
		if (b.age < 0.18) {
			int bx = static_cast<int>(b.x / cell), by = static_cast<int>(b.y / cell);
			for (auto [ox, oy] : {std::pair{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}})
				FillCell(rgb, width, height, (bx + ox) * cell, (by + oy) * cell, cell, {255, 255, 255}, 0.9);
		}

		int index = 0;
		for (auto const& p : b.particles) {
			++index;
			// Twinkle: skip some frames per particle
			if ((static_cast<int>(b.age * 24) + index) % 6 == 0) continue;
			int px = static_cast<int>(std::floor(p.x / cell)) * cell;
			int py = static_cast<int>(std::floor(p.y / cell)) * cell;
			FillCell(rgb, width, height, px, py, cell, burst_colors[p.color], fade);
		}
	}
}
