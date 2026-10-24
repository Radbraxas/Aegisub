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

/// @file dialog_transcribe.cpp
/// @brief Dialog for transcribing the loaded audio into subtitle lines

#include "dialogs.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "dialog_progress.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "transcription.h"

#include <libaegisub/audio/provider.h>
#include <libaegisub/path.h>

#include <chrono>
#include <iterator>
#include <memory>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/filepicker.h>
#include <wx/hyperlink.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace {
struct Language {
	const char *code;
	const char *name;
};

// Languages offered in the dialog. whisper.cpp supports more; these cover the most common cases.
const Language languages[] = {
	{"auto", "Detect automatically"},
	{"en", "English"},
	{"es", "Spanish"},
	{"pt", "Portuguese"},
	{"fr", "French"},
	{"de", "German"},
	{"it", "Italian"},
	{"ja", "Japanese"},
	{"ko", "Korean"},
	{"zh", "Chinese"},
	{"ru", "Russian"},
	{"ar", "Arabic"},
	{"hi", "Hindi"},
	{"id", "Indonesian"},
	{"pl", "Polish"},
	{"tr", "Turkish"},
	{"uk", "Ukrainian"},
	{"vi", "Vietnamese"},
	{"th", "Thai"},
	{"nl", "Dutch"},
};

class DialogTranscribe final : public wxDialog {
	wxFilePickerCtrl *executable;
	wxFilePickerCtrl *model;
	wxChoice *language;
	wxCheckBox *translate;
	wxSpinCtrl *max_line_length;
	wxRadioBox *placement;

	void OnOK(wxCommandEvent &evt);

public:
	DialogTranscribe(wxWindow *parent);
};

DialogTranscribe::DialogTranscribe(wxWindow *parent)
: wxDialog(parent, -1, _("Transcribe Audio"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	auto intro = new wxStaticText(this, -1,
		_("Creates subtitle lines from the speech in the loaded audio. Everything runs on this computer "
		  "using whisper.cpp; no audio is sent anywhere."));
	intro->Wrap(460);

	auto link = new wxHyperlinkCtrl(this, -1, _("Download whisper.cpp and its models"),
		"https://github.com/ggml-org/whisper.cpp");

#ifdef _WIN32
	wxString exe_wildcard = _("Programs") + " (*.exe)|*.exe";
#else
	wxString exe_wildcard = wxFileSelectorDefaultWildcardStr;
#endif
	executable = new wxFilePickerCtrl(this, -1, to_wx(OPT_GET("Tool/Transcription/Executable")->GetString()),
		_("Select the whisper.cpp program (whisper-cli)"), exe_wildcard,
		wxDefaultPosition, wxSize(320, -1), wxFLP_OPEN | wxFLP_FILE_MUST_EXIST | wxFLP_USE_TEXTCTRL);
	model = new wxFilePickerCtrl(this, -1, to_wx(OPT_GET("Tool/Transcription/Model")->GetString()),
		_("Select a whisper model"), _("Whisper models") + " (*.bin)|*.bin|" + _("All files") + " (*.*)|*.*",
		wxDefaultPosition, wxSize(320, -1), wxFLP_OPEN | wxFLP_FILE_MUST_EXIST | wxFLP_USE_TEXTCTRL);

	language = new wxChoice(this, -1);
	auto current_language = OPT_GET("Tool/Transcription/Language")->GetString();
	for (size_t i = 0; i < std::size(languages); ++i) {
		language->Append(wxGetTranslation(languages[i].name));
		if (current_language == languages[i].code)
			language->SetSelection(static_cast<int>(i));
	}
	if (language->GetSelection() == wxNOT_FOUND)
		language->SetSelection(0);

	translate = new wxCheckBox(this, -1, _("Translate the speech to English"));
	translate->SetValue(OPT_GET("Tool/Transcription/Translate")->GetBool());

	max_line_length = new wxSpinCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
		0, 200, static_cast<int>(OPT_GET("Tool/Transcription/Max Line Length")->GetInt()));

	wxString placements[] = { _("Add after the existing lines"), _("Replace all existing lines") };
	placement = new wxRadioBox(this, -1, _("New lines"), wxDefaultPosition, wxDefaultSize, 2, placements, 1);
	placement->SetSelection(OPT_GET("Tool/Transcription/Replace Lines")->GetBool() ? 1 : 0);

	auto grid = new wxFlexGridSizer(2, 5, 8);
	grid->AddGrowableCol(1, 1);
	grid->Add(new wxStaticText(this, -1, _("whisper.cpp program:")), wxSizerFlags().CenterVertical());
	grid->Add(executable, wxSizerFlags().Expand());
	grid->Add(new wxStaticText(this, -1, _("Model:")), wxSizerFlags().CenterVertical());
	grid->Add(model, wxSizerFlags().Expand());
	grid->Add(new wxStaticText(this, -1, _("Spoken language:")), wxSizerFlags().CenterVertical());
	grid->Add(language, wxSizerFlags().Expand());
	grid->Add(new wxStaticText(this, -1, _("Max. characters per line (0 = no limit):")), wxSizerFlags().CenterVertical());
	grid->Add(max_line_length);
	grid->AddSpacer(0);
	grid->Add(translate);

	auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	if (auto ok = FindWindow(wxID_OK))
		ok->SetLabel(_("Transcribe"));

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(intro, wxSizerFlags().Border(wxALL & ~wxBOTTOM, 10));
	main_sizer->Add(link, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 10));
	main_sizer->Add(grid, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, 10));
	main_sizer->Add(placement, wxSizerFlags().Expand().Border(wxALL, 10));
	main_sizer->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 10));
	SetSizerAndFit(main_sizer);
	CenterOnParent();

	Bind(wxEVT_BUTTON, &DialogTranscribe::OnOK, this, wxID_OK);
}

void DialogTranscribe::OnOK(wxCommandEvent &) {
	if (executable->GetPath().empty() || model->GetPath().empty()) {
		wxMessageBox(_("Please select the whisper.cpp program and a model file."), _("Transcribe Audio"),
			wxOK | wxICON_EXCLAMATION, this);
		return;
	}

	int lang = language->GetSelection();
	OPT_SET("Tool/Transcription/Executable")->SetString(from_wx(executable->GetPath()));
	OPT_SET("Tool/Transcription/Model")->SetString(from_wx(model->GetPath()));
	OPT_SET("Tool/Transcription/Language")->SetString(languages[lang == wxNOT_FOUND ? 0 : lang].code);
	OPT_SET("Tool/Transcription/Translate")->SetBool(translate->GetValue());
	OPT_SET("Tool/Transcription/Max Line Length")->SetInt(max_line_length->GetValue());
	OPT_SET("Tool/Transcription/Replace Lines")->SetBool(placement->GetSelection() == 1);
	EndModal(wxID_OK);
}

/// Put the transcribed segments into the subtitles as dialogue lines
void InsertSegments(agi::Context *c, std::vector<transcription::Segment> const& segments, bool replace) {
	auto active = c->selectionController->GetActiveLine();
	std::string style = active ? active->Style.get() : std::string("Default");

	// A freshly created file has a single empty line; replace it rather than keeping it at the top
	bool only_placeholder = c->ass->Events.size() == 1 && c->ass->Events.front().Text.get().empty();

	// Defer deleting removed lines until the selection no longer refers to them
	std::vector<std::unique_ptr<AssDialogue>> to_delete;
	if (replace || only_placeholder) {
		c->ass->Events.remove_and_dispose_if([](AssDialogue const&) { return true; },
			[&](AssDialogue *e) { to_delete.emplace_back(e); });
	}

	AssDialogue *first = nullptr;
	for (auto const& seg : segments) {
		auto line = new AssDialogue;
		line->Start = seg.start_ms;
		line->End = seg.end_ms;
		line->Style = style;
		line->Text = seg.text;
		c->ass->Events.push_back(*line);
		if (!first) first = line;
	}

	c->ass->Commit(_("transcription"), AssFile::COMMIT_DIAG_ADDREM);
	c->selectionController->SetSelectionAndActive({ first }, first);
}
}

void ShowTranscribeDialog(agi::Context *c) {
	auto provider = c->project->AudioProvider();
	if (!provider) {
		wxMessageBox(_("Open an audio or video file with audio first."), _("Transcribe Audio"),
			wxOK | wxICON_INFORMATION, c->parent);
		return;
	}

	{
		DialogTranscribe dialog(c->parent);
		if (dialog.ShowModal() != wxID_OK)
			return;
	}

	transcription::Settings settings;
	settings.executable = agi::fs::path(OPT_GET("Tool/Transcription/Executable")->GetString());
	settings.model = agi::fs::path(OPT_GET("Tool/Transcription/Model")->GetString());
	settings.language = OPT_GET("Tool/Transcription/Language")->GetString();
	settings.translate = OPT_GET("Tool/Transcription/Translate")->GetBool();
	settings.max_line_length = static_cast<int>(OPT_GET("Tool/Transcription/Max Line Length")->GetInt());
	settings.threads = static_cast<int>(OPT_GET("Tool/Transcription/Threads")->GetInt());
	bool replace = OPT_GET("Tool/Transcription/Replace Lines")->GetBool();

	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	auto temp_dir = config::path->Decode("?temp") / agi::fs::path("aegisub-transcription-" + std::to_string(stamp));

	std::vector<transcription::Segment> segments;
	std::string error;
	DialogProgress progress(c->parent, _("Transcribe Audio"), _("Preparing the audio..."));
	try {
		progress.Run([&](agi::ProgressSink *ps) {
			try {
				segments = transcription::Transcribe(*provider, settings, temp_dir, ps);
			}
			catch (agi::UserCancelException const&) {
				throw;
			}
			catch (agi::Exception const& e) {
				error = e.GetMessage();
			}
			catch (std::exception const& e) {
				error = e.what();
			}
		});
	}
	catch (agi::UserCancelException const&) {
		return;
	}

	if (!error.empty()) {
		wxMessageBox(to_wx(error), _("Transcription failed"), wxOK | wxICON_ERROR, c->parent);
		return;
	}

	if (segments.empty()) {
		wxMessageBox(_("No speech was recognized in the audio."), _("Transcribe Audio"),
			wxOK | wxICON_INFORMATION, c->parent);
		return;
	}

	InsertSegments(c, segments, replace);
}
