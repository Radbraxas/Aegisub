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
#include <iterator>
#include <numbers>

namespace {
struct RGB { double r, g, b; };

constexpr RGB grad_top{38, 41, 46};
constexpr RGB grad_bottom{19, 20, 23};

constexpr RGB field_color{112, 142, 138};   // Dim teal-grey characters inside the clouds
constexpr RGB edge_color{86, 182, 236};     // Bright blue characters along the edges
constexpr RGB burst_colors[] = {
	{240, 110, 125}, // coral
	{243, 210, 122}, // butter
	{159, 227, 210}, // mint
	{143, 208, 242}, // sky
};

constexpr double burst_duration = 1.2;

double Smoothstep(double e0, double e1, double x) {
	double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
	return t * t * (3 - 2 * t);
}

uint32_t Hash3(int x, int y, int z) {
	uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u
		+ static_cast<uint32_t>(z) * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

double Hash01(int x, int y, int z = 0) {
	return double(Hash3(x, y, z) & 0xFFFFFF) / double(0xFFFFFF);
}

/// Smooth value noise in [0, 1]
double ValueNoise(double x, double y) {
	double fx = std::floor(x), fy = std::floor(y);
	int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
	double tx = x - fx, ty = y - fy;
	tx = tx * tx * (3 - 2 * tx);
	ty = ty * ty * (3 - 2 * ty);
	double a = Hash01(ix, iy), b = Hash01(ix + 1, iy);
	double c = Hash01(ix, iy + 1), d = Hash01(ix + 1, iy + 1);
	double top = a + (b - a) * tx, bottom = c + (d - c) * tx;
	return top + (bottom - top) * ty;
}

/// Fractal noise: three octaves of value noise
double Fbm(double x, double y) {
	return 0.55 * ValueNoise(x, y) + 0.3 * ValueNoise(x * 2.03 + 17.1, y * 2.03 - 4.7)
		+ 0.15 * ValueNoise(x * 4.11 - 9.3, y * 4.11 + 21.9);
}

void Blend(uint8_t *px, double r, double g, double b, double alpha) {
	px[0] = static_cast<uint8_t>(px[0] + (r - px[0]) * alpha);
	px[1] = static_cast<uint8_t>(px[1] + (g - px[1]) * alpha);
	px[2] = static_cast<uint8_t>(px[2] + (b - px[2]) * alpha);
}
}

void AboutBackdrop::SetGlyphs(int cell_width, int cell_height, std::vector<uint8_t> coverage) {
	const size_t glyph_size = static_cast<size_t>(std::max(1, cell_width)) * std::max(1, cell_height);
	if (cell_width <= 0 || cell_height <= 0 || coverage.size() < glyph_size) {
		glyph_count = 0;
		glyphs.clear();
		return;
	}
	cell_w = cell_width;
	cell_h = cell_height;
	glyph_count = static_cast<int>(coverage.size() / glyph_size);
	glyphs = std::move(coverage);
}

double AboutBackdrop::Random() {
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return double(seed & 0xFFFFFF) / double(0xFFFFFF);
}

void AboutBackdrop::AddBurst(double x, double y) {
	Burst burst{x, y, 0.0, {}};
	const int count = 18;
	for (int i = 0; i < count; ++i) {
		double angle = 2 * std::numbers::pi * (i + Random() * 0.7) / count;
		double speed = 60 + Random() * 140;
		burst.particles.push_back({x, y, std::cos(angle) * speed, std::sin(angle) * speed,
			static_cast<int>(Random() * 100000)});
	}
	bursts.push_back(std::move(burst));
}

void AboutBackdrop::Step(double dt) {
	dt = std::clamp(dt, 0.0, 0.1); // Avoid jumps after the window was hidden
	time += dt;

	const double drag = std::exp(-2.2 * dt);
	for (auto& burst : bursts) {
		burst.age += dt;
		for (auto& p : burst.particles) {
			p.x += p.vx * dt;
			p.y += p.vy * dt;
			p.vx *= drag;
			p.vy *= drag;
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
		{0.12 * width, 0.05 * height, 0.50 * width, 0.10, {159, 227, 210}},
		{0.95 * width, 1.00 * height, 0.55 * width, 0.09, {246, 184, 166}},
	};

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			double t = 0.35 * x / width + 0.65 * y / height;
			uint8_t *px = &background[(static_cast<size_t>(y) * width + x) * 3];
			px[0] = static_cast<uint8_t>(grad_top.r + (grad_bottom.r - grad_top.r) * t);
			px[1] = static_cast<uint8_t>(grad_top.g + (grad_bottom.g - grad_top.g) * t);
			px[2] = static_cast<uint8_t>(grad_top.b + (grad_bottom.b - grad_top.b) * t);
			for (auto const& g : glows) {
				double dx = (x - g.x) / g.radius, dy = (y - g.y) / g.radius;
				Blend(px, g.color.r, g.color.g, g.color.b, g.alpha * std::exp(-2.5 * (dx * dx + dy * dy)));
			}
		}
	}
}

void AboutBackdrop::DrawGlyph(std::vector<uint8_t>& rgb, int width, int height, int x0, int y0,
                              unsigned glyph, double r, double g, double b, double alpha) const {
	if (alpha <= 0.01) return;

	if (glyph_count == 0) {
		// No glyphs were supplied: draw a small block instead
		for (int y = std::max(0, y0 + cell_h / 4); y < std::min(height, y0 + cell_h * 3 / 4); ++y)
			for (int x = std::max(0, x0 + 1); x < std::min(width, x0 + cell_w - 1); ++x)
				Blend(&rgb[(static_cast<size_t>(y) * width + x) * 3], r, g, b, alpha * 0.6);
		return;
	}

	const uint8_t *mask = &glyphs[static_cast<size_t>(glyph % glyph_count) * cell_w * cell_h];
	for (int gy = 0; gy < cell_h; ++gy) {
		const int y = y0 + gy;
		if (y < 0 || y >= height) continue;
		for (int gx = 0; gx < cell_w; ++gx) {
			const int x = x0 + gx;
			if (x < 0 || x >= width) continue;
			const uint8_t cov = mask[gy * cell_w + gx];
			if (cov)
				Blend(&rgb[(static_cast<size_t>(y) * width + x) * 3], r, g, b, alpha * cov / 255.0);
		}
	}
}

void AboutBackdrop::Render(std::vector<uint8_t>& rgb, int width, int height) {
	if (width <= 0 || height <= 0) return;
	if (width != bg_width || height != bg_height)
		BuildBackground(width, height);
	rgb = background;

	const int cols = (width + cell_w - 1) / cell_w;
	const int rows = (height + cell_h - 1) / cell_h;

	// The noise field is sampled in pixel space so its shapes don't depend on the font size
	for (int cy = 0; cy < rows; ++cy) {
		for (int cx = 0; cx < cols; ++cx) {
			const double px = (cx + 0.5) * cell_w, py = (cy + 0.5) * cell_h;
			const double n = Fbm(px * 0.0105 + time * 0.22, py * 0.016 + std::sin(time * 0.25) * 0.2 + 3.7);

			const double inside = Smoothstep(0.50, 0.68, n);
			const double edge = 1.0 - std::clamp(std::abs(n - 0.555) / 0.022, 0.0, 1.0);
			const double sparkle = Hash01(cx, cy, 7);

			double alpha = 0;
			RGB color = field_color;
			if (edge > 0) {
				alpha = 0.16 + 0.44 * edge;
				color = edge_color;
			}
			else if (inside > 0)
				alpha = 0.05 + 0.16 * inside * (0.5 + 0.5 * sparkle);
			else if (sparkle < 0.12)
				alpha = 0.035; // Faint characters scattered everywhere

			if (alpha <= 0) continue;

			// Each cell changes its character at its own pace; edges flicker faster
			const double rate = (edge > 0 ? 3.0 : 0.6) + 1.5 * Hash01(cx, cy, 11);
			const auto tick = static_cast<int>(std::floor(time * rate + Hash01(cx, cy, 13) * 10));
			DrawGlyph(rgb, width, height, cx * cell_w, cy * cell_h, Hash3(cx, cy, tick),
				color.r, color.g, color.b, alpha);
		}
	}

	// Click bursts: a ring of scrambling characters plus glyphs flying outwards
	for (auto const& b : bursts) {
		const double fade = 1 - b.age / burst_duration;
		const double ring_t = std::min(1.0, b.age / 0.95);
		const double radius = 6 + 130 * (1 - std::pow(1 - ring_t, 3));
		const auto scramble = static_cast<int>(b.age * 30);

		const int c0x = std::max(0, static_cast<int>((b.x - radius) / cell_w) - 1);
		const int c1x = std::min(cols - 1, static_cast<int>((b.x + radius) / cell_w) + 1);
		const int c0y = std::max(0, static_cast<int>((b.y - radius) / cell_h) - 1);
		const int c1y = std::min(rows - 1, static_cast<int>((b.y + radius) / cell_h) + 1);
		for (int cy = c0y; cy <= c1y; ++cy) {
			for (int cx = c0x; cx <= c1x; ++cx) {
				const double dx = (cx + 0.5) * cell_w - b.x, dy = (cy + 0.5) * cell_h - b.y;
				const double dist = std::sqrt(dx * dx + dy * dy);
				const double band = 1.0 - std::abs(dist - radius) / (cell_h * 1.1);
				if (band <= 0) continue;
				// White at first, cooling down to blue
				const double warm = std::max(0.0, 1.0 - b.age * 2.0);
				DrawGlyph(rgb, width, height, cx * cell_w, cy * cell_h, Hash3(cx, cy, scramble),
					edge_color.r + (255 - edge_color.r) * warm, edge_color.g + (255 - edge_color.g) * warm,
					edge_color.b + (255 - edge_color.b) * warm, std::min(1.0, 0.95 * fade * (0.4 + band)));
			}
		}

		for (auto const& p : b.particles) {
			const auto& c = burst_colors[static_cast<size_t>(p.glyph_seed) % std::size(burst_colors)];
			const int gx = static_cast<int>(std::floor(p.x / cell_w)) * cell_w;
			const int gy = static_cast<int>(std::floor(p.y / cell_h)) * cell_h;
			DrawGlyph(rgb, width, height, gx, gy, Hash3(p.glyph_seed, scramble, 3), c.r, c.g, c.b, fade);
		}
	}
}
