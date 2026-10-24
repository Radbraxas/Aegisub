# Automatic transcription

*Subtitle → Transcribe Audio...* creates subtitle lines from the speech in the
loaded audio. It runs entirely on your computer with
[whisper.cpp](https://github.com/ggml-org/whisper.cpp); no audio leaves the
machine.

## Setup

You need two things, which you only have to select once:

1. **The whisper.cpp program** (`whisper-cli`, or `whisper-cli.exe` on Windows).
   - Windows: download a `whisper-bin-*.zip` from the
     [whisper.cpp releases](https://github.com/ggml-org/whisper.cpp/releases)
     and extract it.
   - macOS: `brew install whisper-cpp`, then select
     `/opt/homebrew/bin/whisper-cli` (Apple Silicon) or
     `/usr/local/bin/whisper-cli` (Intel).
   - Linux: build it from source as described in the whisper.cpp README.
2. **A model file** (`ggml-*.bin`) from
   [huggingface.co/ggerganov/whisper.cpp](https://huggingface.co/ggerganov/whisper.cpp).
   `ggml-large-v3-turbo.bin` is a good balance of accuracy and speed;
   `ggml-small.bin` or `ggml-base.bin` are faster on machines without a GPU.
   Models ending in `.en` only understand English.

## Options

- **Spoken language**: pick the language of the audio, or let whisper detect it.
- **Translate the speech to English**: whisper translates instead of
  transcribing. (whisper itself can only translate into English.)
- **Max. characters per line**: long sentences are split into several lines.
- **New lines**: add the transcription after the existing lines, or replace them.

The new lines use the style of the active line and can be undone with
*Edit → Undo*.

## How it works

The audio is resampled to 16 kHz mono and written to a temporary WAV file,
`whisper-cli` is run on it with JSON output (`-oj`) and progress reporting
(`-pp`), and the resulting segments are inserted as dialogue lines. The code is
in `src/transcription.cpp` (audio export, process handling, parsing) and
`src/dialog_transcribe.cpp` (dialog and line insertion).
