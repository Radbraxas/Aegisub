// Copyright (c) 2005, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#include "about_backdrop.h"
#include "libresrc/libresrc.h"
#include "format.h"
#include "version.h"

#include <chrono>
#include <vector>

#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/image.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/window.h>

namespace {
/// Header of the About dialog: drifting pixel clouds behind the logo, with a
/// burst of pixels wherever it is clicked
class AboutAnimation final : public wxWindow {
	AboutBackdrop backdrop;
	wxTimer timer;
	std::chrono::steady_clock::time_point last_tick = std::chrono::steady_clock::now();
	std::vector<uint8_t> pixels;

	wxImage logo_source;
	wxBitmap logo;
	wxSize logo_size;

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		const wxSize size = GetClientSize();
		if (size.x <= 0 || size.y <= 0) return;

		backdrop.Render(pixels, size.x, size.y);
		wxImage frame(size.x, size.y, pixels.data(), true);
		dc.DrawBitmap(wxBitmap(frame), 0, 0);

		// The logo is drawn at the display's full resolution so it stays sharp
		if (logo_size != size) {
			const double scale = GetContentScaleFactor();
			logo = wxBitmap(logo_source.Scale(static_cast<int>(size.x * scale), static_cast<int>(size.y * scale),
				wxIMAGE_QUALITY_HIGH), -1, scale);
			logo_size = size;
		}
		dc.DrawBitmap(logo, 0, 0, true);
	}

	void OnTimer(wxTimerEvent &) {
		const auto now = std::chrono::steady_clock::now();
		backdrop.Step(std::chrono::duration<double>(now - last_tick).count());
		last_tick = now;
		Refresh(false);
	}

	void OnClick(wxMouseEvent &evt) {
		backdrop.AddBurst(evt.GetX(), evt.GetY());
		evt.Skip();
	}

public:
	AboutAnimation(wxWindow *parent)
	: wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
	, backdrop(FromDIP(4))
	, timer(this)
	, logo_source(GETIMAGE(about_logo).ConvertToImage())
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetInitialSize(FromDIP(wxSize(420, 240)));
		SetCursor(wxCursor(wxCURSOR_HAND));

		Bind(wxEVT_PAINT, &AboutAnimation::OnPaint, this);
		Bind(wxEVT_TIMER, &AboutAnimation::OnTimer, this);
		Bind(wxEVT_LEFT_DOWN, &AboutAnimation::OnClick, this);
		timer.Start(33);
	}

	~AboutAnimation() {
		timer.Stop();
	}
};
}

void ShowAboutDialog(wxWindow *parent) {
	wxDialog d(parent, -1, _("About Aegisub"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX);

	wxString translatorCredit = _("Translated into LANGUAGE by PERSON\n");
	if (translatorCredit == "Translated into LANGUAGE by PERSON\n")
		translatorCredit.clear();

	// Generate about string
	wxString aboutString = wxString("Aegisub ") + GetAegisubShortVersionString() + ".\n"
		"Copyright (c) 2005-2026 Rodrigo Braz Monteiro, Niels Martin Hansen, Thomas Goyne et al.\n\n"
		"Programmers:\n"
		"    Alysson Souza e Silva\n"
		"    Amar Takhar\n"
		"    Charlie Jiang\n"
		"    Dan Donovan\n"
		"    Daniel Moscoviter\n"
		"    David Conrad\n"
		"    David Lamparter\n"
		"    Eric Batalitzky\n"
		"    Evgeniy Stepanov\n"
		"    Fredrik Mellbin\n"
		"    Grigori Goronzy\n"
		"    Karl Blomster\n"
		"    Mike Matsnev\n"
		"    Moritz Brunner\n"
		"    Muhammad Lukman Nasaruddin\n"
		"    Niels Martin Hansen\n"
		"    Patryk Pomykalski\n"
		"    Qirui Wang\n"
		"    Ravi Pinjala\n"
		"    Rodrigo Braz Monteiro\n"
		"    Simone Cociancich\n"
		"    Thomas Goyne\n"
		"User manual written by:\n"
		"    Karl Blomster\n"
		"    Niels Martin Hansen\n"
		"    Rodrigo Braz Monteiro\n"
		"Icons by:\n"
		"    Philip Cash\n"
		"Additional thanks to:\n"
		"    Mentar\n"
		"    Sigurd Tao Lyngse\n"
		"    Everyone in the Aegisub IRC channel\n"
		"    Everyone who ever reported a bug\n"
		+ translatorCredit + "\n"
		"Aegisub includes portions from the following other projects:\n"
		"    wxWidgets - Copyright (c) Julian Smart, Robert Roebling et al;\n"
		"    wxStyledTextCtrl - Copyright (c) Robin Dunn, Neil Hodgson;\n"
		"    Scintilla - Copyright (c) Neil Hodgson;\n"
		"    Boost - Copyright (c) Beman Dawes, David Abrahams et al;\n"
		"    UniversalCharDet - Copyright (c) Netscape Communications Corp.;\n"
		"    ICU - Copyright (c) International Business Machines Corp.;\n"
		"    Lua - Copyright (c) Lua.org, PUC-Rio;\n"
		"    LuaJIT - Copyright (c) Mike Pall;\n"
		"    luabins - Copyright (c) Alexander Gladysh;\n"
#ifdef WITH_HUNSPELL
		"    Hunspell - Copyright (c) Kevin Hendricks;\n"
#endif
#ifdef WITH_PORTAUDIO
		"    PortAudio - Copyright (c) Ross Bencina, Phil Burk;\n"
#endif
#ifdef WITH_FFMS2
		"    FFmpeg - Copyright (c) Fabrice Bellard;\n"
		"    FFMS2 - Copyright (c) Fredrik Mellbin;\n"
		"    dav1d - Copyright (c) VideoLAN and dav1d authors;\n"
#endif
#ifdef WITH_AVISYNTH
		"    Avisynth 2.5 - Copyright (c) Ben Rudiak-Gould et al;\n"
#endif
#ifdef WITH_CSRI
		"    csri - Copyright (c) David Lamparter;\n"
# ifdef __WINDOWS__
		"    vsfilter - Copyright (c) Gabest et al;\n"
# endif
#endif
		"    libass - Copyright (c) Evgeniy Stepanov, Grigori Goronzy;\n"
		"    Matroska Parser - Copyright (c) Mike Matsnev;\n"
		"    Freetype - Copyright (c) David Turner, Robert Wilhelm, Werner Lemberg;\n"
		"    Fontconfig - Copyright (c) Keith Packard et al;\n"
#ifdef WITH_FFTW3
		"    FFTW - Copyright (c) Matteo Frigo, Massachusetts Institute of Technology;\n"
#endif
#ifdef WITH_UPDATE_CHECKER
		"    libcurl - Copyright (c) Daniel Stenberg et al;\n"
#endif
#ifdef WITH_LIBPORTAL
		"    libportal - Copyright (c) libportal authors;\n"
#endif
		+ _("\nSee the help file for full credits.\n")
#ifdef BUILD_CREDIT
		+ fmt_tl("Built by %s on %s.", GetAegisubBuildCredit(), GetAegisubBuildTime())
#endif
		;

	// Replace copyright symbol
	wxChar copySymbol = 0xA9;
	aboutString.Replace("(c)", wxString(copySymbol));

	wxTextCtrl *textctrl = new wxTextCtrl(&d, -1, aboutString, wxDefaultPosition, wxSize(-1, 200), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);

	wxSizer *MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(new AboutAnimation(&d), 0, wxCENTER, 0);
	MainSizer->Add(new wxStaticLine(&d, wxID_ANY), 0, wxEXPAND, 0);
	MainSizer->Add(textctrl, 0, wxEXPAND, 0);
	MainSizer->Add(new wxStaticLine(&d, wxID_ANY), 0, wxEXPAND, 0);
	MainSizer->Add(d.CreateButtonSizer(wxOK), 0, wxEXPAND | wxALL, 6);

	d.SetSizerAndFit(MainSizer);
	d.CentreOnParent();
	d.ShowModal();
}
