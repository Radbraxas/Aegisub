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

/// @file transcription.cpp
/// @brief Automatic transcription of the loaded audio with whisper.cpp

#include "transcription.h"

#include "compat.h"

#include <libaegisub/audio/provider.h>
#include <libaegisub/background_runner.h>
#include <libaegisub/cajun/elements.h>
#include <libaegisub/io.h>
#include <libaegisub/json.h>
#include <libaegisub/log.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <istream>
#include <numbers>
#include <ostream>
#include <system_error>

#include <wx/intl.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif
#endif

namespace transcription {
namespace {

/// Windowed-sinc kernel used for resampling, tabulated for speed
class ResampleKernel {
	static constexpr int STEPS = 512; ///< Table entries per input sample
	int half_width;                   ///< Kernel radius in input samples
	std::vector<float> table;

public:
	/// @param cutoff Cutoff frequency in cycles per input sample (< 0.5)
	explicit ResampleKernel(double cutoff)
	: half_width(static_cast<int>(std::ceil(8.0 / (2.0 * cutoff)))) // 8 zero crossings per side
	{
		table.resize(static_cast<size_t>(half_width) * STEPS + 2);
		for (size_t i = 0; i < table.size(); ++i) {
			double x = double(i) / STEPS;
			double sinc_arg = 2.0 * cutoff * x;
			double sinc = x == 0 ? 1.0 : std::sin(std::numbers::pi * sinc_arg) / (std::numbers::pi * sinc_arg);
			double u = x / half_width;
			double window = u >= 1.0 ? 0.0 : 0.5 * (1.0 + std::cos(std::numbers::pi * u));
			table[i] = static_cast<float>(2.0 * cutoff * sinc * window);
		}
	}

	int HalfWidth() const { return half_width; }

	float operator()(double x) const {
		x = std::abs(x) * STEPS;
		auto i = static_cast<size_t>(x);
		if (i + 1 >= table.size()) return 0.f;
		float frac = static_cast<float>(x - double(i));
		return table[i] + (table[i + 1] - table[i]) * frac;
	}
};

void WriteLE16(std::ostream& out, uint16_t v) {
	const char bytes[2] = { static_cast<char>(v & 0xFF), static_cast<char>(v >> 8) };
	out.write(bytes, 2);
}

void WriteLE32(std::ostream& out, uint32_t v) {
	const char bytes[4] = {
		static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
		static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)
	};
	out.write(bytes, 4);
}

int64_t JsonInteger(json::UnknownElement const& e) {
	try {
		return static_cast<json::Integer const&>(e);
	}
	catch (json::Exception const&) {
		return static_cast<int64_t>(static_cast<json::Double const&>(e));
	}
}

json::UnknownElement const& JsonMember(json::Object const& obj, std::string_view key) {
	auto it = obj.find(key);
	if (it == obj.end())
		throw TranscriptionError("Malformed whisper.cpp output: missing \"" + std::string(key) + "\"");
	return it->second;
}

/// Splits a byte stream into lines
class LineSplitter {
	std::string pending;
	std::function<void(std::string const&)> const& on_line;

public:
	explicit LineSplitter(std::function<void(std::string const&)> const& on_line) : on_line(on_line) { }

	void Feed(const char *data, size_t len) {
		for (size_t i = 0; i < len; ++i) {
			if (data[i] == '\n' || data[i] == '\r') {
				if (!pending.empty()) on_line(pending);
				pending.clear();
			}
			else
				pending.push_back(data[i]);
		}
	}

	void Finish() {
		if (!pending.empty()) on_line(pending);
		pending.clear();
	}
};

#ifdef _WIN32
std::wstring Widen(std::string const& str) {
	if (str.empty()) return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
	std::wstring ret(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), ret.data(), len);
	return ret;
}

/// Quote an argument following the rules of CommandLineToArgvW
std::wstring QuoteArgument(std::wstring const& arg) {
	if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
		return arg;

	std::wstring ret = L"\"";
	for (auto it = arg.begin(); ; ++it) {
		size_t backslashes = 0;
		while (it != arg.end() && *it == L'\\') {
			++it;
			++backslashes;
		}

		if (it == arg.end()) {
			ret.append(backslashes * 2, L'\\');
			break;
		}
		if (*it == L'"') {
			ret.append(backslashes * 2 + 1, L'\\');
			ret.push_back(L'"');
		}
		else {
			ret.append(backslashes, L'\\');
			ret.push_back(*it);
		}
	}
	ret.push_back(L'"');
	return ret;
}
#endif

/// Removes a directory and everything in it when going out of scope
struct TempDirectory {
	agi::fs::path dir;
	~TempDirectory() {
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
};
}

void WriteWhisperWav(agi::AudioProvider const& provider, agi::fs::path const& path, agi::ProgressSink *ps) {
	if (provider.GetChannels() != 1 || provider.GetBytesPerSample() != 2 || provider.AreSamplesFloat())
		throw TranscriptionError("Transcription requires mono 16-bit audio");

	const int in_rate = provider.GetSampleRate();
	if (in_rate <= 0)
		throw TranscriptionError("Invalid audio sample rate");

	const double ratio = double(in_rate) / WHISPER_SAMPLE_RATE;
	const int64_t out_samples = static_cast<int64_t>(double(provider.GetNumSamples()) / ratio);
	if (out_samples <= 0)
		throw TranscriptionError("The audio is empty");
	if (out_samples * 2 > INT32_MAX - 36)
		throw TranscriptionError("The audio is too long to be transcribed in one go");

	// Low-pass just below the output Nyquist frequency to avoid aliasing
	const ResampleKernel kernel(0.475 * std::min(1.0, 1.0 / ratio));
	const int half = kernel.HalfWidth();

	agi::io::Save file(path, true);
	std::ostream& out = file.Get();

	const auto data_bytes = static_cast<uint32_t>(out_samples * 2);
	out.write("RIFF", 4);
	WriteLE32(out, data_bytes + 36);
	out.write("WAVEfmt ", 8);
	WriteLE32(out, 16);                       // Size of the fmt chunk
	WriteLE16(out, 1);                        // PCM
	WriteLE16(out, 1);                        // Channels
	WriteLE32(out, WHISPER_SAMPLE_RATE);
	WriteLE32(out, WHISPER_SAMPLE_RATE * 2);  // Bytes per second
	WriteLE16(out, 2);                        // Block align
	WriteLE16(out, 16);                       // Bits per sample
	out.write("data", 4);
	WriteLE32(out, data_bytes);

	constexpr int64_t block = WHISPER_SAMPLE_RATE * 16;
	std::vector<int16_t> in_buf;
	std::vector<char> out_buf;
	for (int64_t o0 = 0; o0 < out_samples; o0 += block) {
		if (ps && ps->IsCancelled())
			throw agi::UserCancelException("Transcription cancelled");

		const int64_t o1 = std::min(out_samples, o0 + block);
		const int64_t i0 = static_cast<int64_t>(std::floor(double(o0) * ratio)) - half;
		const int64_t i1 = static_cast<int64_t>(std::floor(double(o1 - 1) * ratio)) + half + 1;

		in_buf.resize(static_cast<size_t>(i1 - i0));
		provider.GetAudio(in_buf.data(), i0, i1 - i0); // Zero-fills anything outside the audio

		out_buf.resize(static_cast<size_t>(o1 - o0) * 2);
		for (int64_t o = o0; o < o1; ++o) {
			const double center = double(o) * ratio;
			const auto ic = static_cast<int64_t>(std::floor(center));
			const double frac = center - double(ic);

			double acc = 0, weights = 0;
			for (int k = -half + 1; k <= half; ++k) {
				const float w = kernel(double(k) - frac);
				acc += w * in_buf[static_cast<size_t>(ic + k - i0)];
				weights += w;
			}

			const double value = weights != 0 ? acc / weights : 0;
			const auto sample = static_cast<int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
			const auto u = static_cast<uint16_t>(sample);
			out_buf[static_cast<size_t>(o - o0) * 2] = static_cast<char>(u & 0xFF);
			out_buf[static_cast<size_t>(o - o0) * 2 + 1] = static_cast<char>(u >> 8);
		}
		out.write(out_buf.data(), static_cast<std::streamsize>(out_buf.size()));

		if (ps) ps->SetProgress(o1, out_samples);
	}
}

std::vector<std::string> WhisperArguments(Settings const& settings, agi::fs::path const& wav, agi::fs::path const& output_base) {
	std::vector<std::string> args = {
		"-m", settings.model.string(),
		"-f", wav.string(),
		"-of", output_base.string(),
		"-oj",  // Write the result as JSON
		"-pp",  // Report progress
		"-l", settings.language.empty() ? std::string("auto") : settings.language,
	};

	if (settings.translate)
		args.push_back("-tr");

	if (settings.max_line_length > 0) {
		args.push_back("-ml");
		args.push_back(std::to_string(settings.max_line_length));
		args.push_back("-sow"); // Split on words rather than tokens
	}

	if (settings.threads > 0) {
		args.push_back("-t");
		args.push_back(std::to_string(settings.threads));
	}

	return args;
}

int ParseProgressLine(std::string const& line) {
	static const std::string marker = "progress =";
	auto pos = line.find(marker);
	if (pos == std::string::npos) return -1;

	pos += marker.size();
	while (pos < line.size() && line[pos] == ' ') ++pos;

	int value = 0;
	bool any = false;
	while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
		value = value * 10 + (line[pos] - '0');
		any = true;
		++pos;
	}
	return any ? std::min(value, 100) : -1;
}

std::vector<Segment> ParseWhisperJson(std::istream &in) {
	std::vector<Segment> segments;
	try {
		auto root = agi::json_util::parse(in);
		json::Object const& obj = root;
		json::Array const& transcription = JsonMember(obj, "transcription");

		for (json::Object const& entry : transcription) {
			json::Object const& offsets = JsonMember(entry, "offsets");
			json::String const& text = JsonMember(entry, "text");
			segments.push_back(Segment{
				static_cast<int>(JsonInteger(JsonMember(offsets, "from"))),
				static_cast<int>(JsonInteger(JsonMember(offsets, "to"))),
				text
			});
		}
	}
	catch (json::Exception const& e) {
		throw TranscriptionError(std::string("Could not read the whisper.cpp output: ") + e.what());
	}
	return segments;
}

std::string CleanSegmentText(std::string text) {
	std::string ret;
	ret.reserve(text.size());
	for (char c : text) {
		switch (c) {
			// Braces would start override blocks in ASS
			case '{': ret += '('; break;
			case '}': ret += ')'; break;
			case '\n': ret += "\\N"; break;
			case '\r': break;
			default: ret += c;
		}
	}

	auto is_space = [](char c) { return c == ' ' || c == '\t'; };
	auto begin = std::find_if_not(ret.begin(), ret.end(), is_space);
	auto end = std::find_if_not(ret.rbegin(), std::string::reverse_iterator(begin), is_space).base();
	return std::string(begin, end);
}

#ifdef _WIN32
int RunProcess(agi::fs::path const& program, std::vector<std::string> const& args,
               std::function<void(std::string const&)> const& on_line,
               std::function<bool()> const& cancelled) {
	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE read_end = nullptr, write_end = nullptr;
	if (!CreatePipe(&read_end, &write_end, &sa, 0))
		throw TranscriptionError("Could not create a pipe for whisper.cpp");
	SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

	std::wstring cmdline = QuoteArgument(program.wstring());
	for (auto const& arg : args)
		cmdline += L" " + QuoteArgument(Widen(arg));

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nullptr;
	si.hStdOutput = write_end;
	si.hStdError = write_end;

	PROCESS_INFORMATION pi{};
	BOOL started = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	CloseHandle(write_end);
	if (!started) {
		CloseHandle(read_end);
		throw TranscriptionError("Could not start " + program.string() + " (error " + std::to_string(GetLastError()) + ")");
	}

	LineSplitter splitter(on_line);
	char buf[4096];
	bool was_cancelled = false;
	for (;;) {
		if (cancelled()) {
			TerminateProcess(pi.hProcess, 1);
			was_cancelled = true;
			break;
		}

		DWORD available = 0;
		if (!PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr))
			break; // Pipe closed: the process has exited

		if (available == 0) {
			if (WaitForSingleObject(pi.hProcess, 100) == WAIT_OBJECT_0) {
				// Drain whatever is left before finishing
				while (PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
					DWORD read = 0;
					if (!ReadFile(read_end, buf, std::min<DWORD>(available, sizeof(buf)), &read, nullptr) || read == 0)
						break;
					splitter.Feed(buf, read);
				}
				break;
			}
			continue;
		}

		DWORD read = 0;
		if (!ReadFile(read_end, buf, std::min<DWORD>(available, sizeof(buf)), &read, nullptr) || read == 0)
			break;
		splitter.Feed(buf, read);
	}
	splitter.Finish();

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exit_code = 1;
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	CloseHandle(read_end);

	if (was_cancelled)
		throw agi::UserCancelException("Transcription cancelled");
	return static_cast<int>(exit_code);
}
#else
int RunProcess(agi::fs::path const& program, std::vector<std::string> const& args,
               std::function<void(std::string const&)> const& on_line,
               std::function<bool()> const& cancelled) {
	int fds[2];
	if (pipe(fds) != 0)
		throw TranscriptionError("Could not create a pipe for whisper.cpp");

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
	posix_spawn_file_actions_adddup2(&actions, fds[1], 2);
	posix_spawn_file_actions_addclose(&actions, fds[0]);
	posix_spawn_file_actions_addclose(&actions, fds[1]);

	const std::string program_str = program.string();
	std::vector<char *> argv;
	argv.push_back(const_cast<char *>(program_str.c_str()));
	for (auto const& arg : args)
		argv.push_back(const_cast<char *>(arg.c_str()));
	argv.push_back(nullptr);

	pid_t pid = 0;
	int err = posix_spawn(&pid, program_str.c_str(), &actions, nullptr, argv.data(), environ);
	posix_spawn_file_actions_destroy(&actions);
	close(fds[1]);
	if (err != 0) {
		close(fds[0]);
		throw TranscriptionError("Could not start " + program_str + ": " + std::strerror(err));
	}

	LineSplitter splitter(on_line);
	char buf[4096];
	bool was_cancelled = false;
	for (;;) {
		if (cancelled()) {
			kill(pid, SIGTERM);
			was_cancelled = true;
			break;
		}

		pollfd pfd{fds[0], POLLIN, 0};
		int ready = poll(&pfd, 1, 100);
		if (ready < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (ready == 0) continue;

		ssize_t n = read(fds[0], buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break; // EOF: the process has exited
		splitter.Feed(buf, static_cast<size_t>(n));
	}
	splitter.Finish();
	close(fds[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }

	if (was_cancelled)
		throw agi::UserCancelException("Transcription cancelled");
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return -1;
}
#endif

std::vector<Segment> Transcribe(agi::AudioProvider const& provider, Settings const& settings,
                                agi::fs::path const& temp_dir, agi::ProgressSink *ps) {
	if (settings.executable.empty() || !agi::fs::FileExists(settings.executable))
		throw TranscriptionError("The whisper.cpp program was not found: " + settings.executable.string());
	if (settings.model.empty() || !agi::fs::FileExists(settings.model))
		throw TranscriptionError("The whisper model was not found: " + settings.model.string());

	std::error_code ec;
	std::filesystem::create_directories(temp_dir, ec);
	if (ec)
		throw TranscriptionError("Could not create the temporary folder " + temp_dir.string() + ": " + ec.message());
	TempDirectory cleanup{temp_dir};

	const auto wav = temp_dir / "audio.wav";
	const auto output_base = temp_dir / "transcript";

	ps->SetMessage(from_wx(_("Preparing the audio...")));
	WriteWhisperWav(provider, wav, ps);

	ps->SetMessage(from_wx(_("Recognizing speech with whisper.cpp...")));
	ps->SetProgress(0, 100);

	// Keep the last lines printed so they can be shown if whisper.cpp fails
	std::deque<std::string> recent_output;
	int exit_code = RunProcess(settings.executable, WhisperArguments(settings, wav, output_base),
		[&](std::string const& line) {
			int progress = ParseProgressLine(line);
			if (progress >= 0) {
				ps->SetProgress(progress, 100);
				return;
			}
			LOG_D("transcription") << line;
			recent_output.push_back(line);
			if (recent_output.size() > 15)
				recent_output.pop_front();
		},
		[&] { return ps->IsCancelled(); });

	const auto json_path = agi::fs::path(output_base.string() + ".json");
	if (exit_code != 0 || !agi::fs::FileExists(json_path)) {
		std::string details;
		for (auto const& line : recent_output)
			details += line + "\n";
		throw TranscriptionError("whisper.cpp failed (exit code " + std::to_string(exit_code) + ").\n\n" + details);
	}

	std::vector<Segment> segments;
	{
		auto in = agi::io::Open(json_path);
		segments = ParseWhisperJson(*in);
	}

	// Drop empty segments and the markers whisper uses for silence
	std::vector<Segment> result;
	for (auto& seg : segments) {
		seg.text = CleanSegmentText(seg.text);
		if (seg.text.empty() || seg.text == "[BLANK_AUDIO]" || seg.end_ms <= seg.start_ms)
			continue;
		result.push_back(std::move(seg));
	}
	return result;
}

}
