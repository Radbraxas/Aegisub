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

/// @file transcription.h
/// @brief Automatic transcription of the loaded audio with whisper.cpp
///
/// Transcription runs fully offline: the audio is exported to a temporary WAV
/// file and handed to the whisper.cpp command line program (whisper-cli),
/// whose JSON output is read back as timed segments.

#pragma once

#include <libaegisub/exception.h>
#include <libaegisub/fs.h>

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace agi {
	class AudioProvider;
	class ProgressSink;
}

namespace transcription {

DEFINE_EXCEPTION(TranscriptionError, agi::Exception);

/// Sample rate whisper.cpp expects its input to have
constexpr int WHISPER_SAMPLE_RATE = 16000;

struct Settings {
	agi::fs::path executable;   ///< whisper.cpp command line program (whisper-cli)
	agi::fs::path model;        ///< ggml model file
	std::string language;       ///< ISO 639-1 code, or "auto" to detect it
	bool translate = false;     ///< Translate the speech to English instead of transcribing it
	int max_line_length = 0;    ///< Maximum characters per segment, 0 for no limit
	int threads = 0;            ///< Number of threads, 0 to let whisper.cpp decide
};

/// A timed piece of transcribed text
struct Segment {
	int start_ms;
	int end_ms;
	std::string text;
};

/// Write the audio of a provider as a 16 kHz mono 16-bit PCM WAV file
/// @param provider Audio to write. Must deliver mono 16-bit samples, which is
///                 the case for the audio provider of a project.
/// @param path     File to write
/// @param ps       Optional progress sink; checked for cancellation
void WriteWhisperWav(agi::AudioProvider const& provider, agi::fs::path const& path, agi::ProgressSink *ps);

/// Build the command line arguments for whisper.cpp (excluding the program itself)
/// @param output_base Output file path without extension; whisper.cpp appends ".json"
std::vector<std::string> WhisperArguments(Settings const& settings, agi::fs::path const& wav, agi::fs::path const& output_base);

/// Extract the progress percentage from a line printed by whisper.cpp
/// @return The percentage, or -1 if the line does not report progress
int ParseProgressLine(std::string const& line);

/// Read the segments from the JSON file written by whisper.cpp (-oj)
std::vector<Segment> ParseWhisperJson(std::istream &in);

/// Turn whisper.cpp text into text suitable for an ASS dialogue line
std::string CleanSegmentText(std::string text);

/// Run a program, calling on_line for every line it prints to stdout or stderr
/// @param program   Program to run
/// @param args      Arguments, not including the program itself
/// @param on_line   Called with each output line, on the calling thread
/// @param cancelled Polled regularly; if it returns true the program is terminated
/// @return The exit code of the program
/// @throws agi::UserCancelException if cancelled
/// @throws TranscriptionError if the program could not be started
int RunProcess(agi::fs::path const& program, std::vector<std::string> const& args,
               std::function<void(std::string const&)> const& on_line,
               std::function<bool()> const& cancelled);

/// Transcribe the audio of a provider
///
/// Exports the audio, runs whisper.cpp and reads its output. Temporary files
/// are created in temp_dir and removed afterwards.
/// @throws agi::UserCancelException if cancelled through the progress sink
/// @throws TranscriptionError on failure
std::vector<Segment> Transcribe(agi::AudioProvider const& provider, Settings const& settings,
                                agi::fs::path const& temp_dir, agi::ProgressSink *ps);

}
