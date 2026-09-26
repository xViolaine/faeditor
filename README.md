# FA Editor

macOS editor for Roland FA-06/07/08, with experimental Temporary Scene support for FANTOM-06/07/08.

Qt 6.11 QML UI + C++ models/controllers over documented Roland SysEx.

## Mac App Store

A signed binary is available as **[Editor for Roland FA](https://apps.apple.com/fi/app/editor-for-roland-fa/id6794627204?mt=12)** on the Mac App Store (**USD $9.99** / local currency). An iPad/iPhone build uses the same editor with a touch layout (`MobileMain.qml`). Public Actions here only compile and test; signed TestFlight uploads run from a private workflow (see [`docs/CI_RELEASE.md`](docs/CI_RELEASE.md) and [`docs/ios-editor.md`](docs/ios-editor.md)). Source in this repository remains free under MIT for building yourself.

<p>
  <img src="docs/screenshots/01-mixer.jpg" alt="Mixer" width="420" />
  <img src="docs/screenshots/02-sets.jpg" alt="Sets and tones" width="420" />
</p>
<p>
  <img src="docs/screenshots/03-effects.jpg" alt="Effects" width="420" />
  <img src="docs/screenshots/04-tone.jpg" alt="Tone edit" width="420" />
</p>

## Requirements

- macOS
- CMake ≥ 3.21
- Qt 6.11.1 at `/Volumes/Datastore/Qt/6.11.1/macos` (override with `CMAKE_PREFIX_PATH`)
- Xcode command-line tools
- Network once (Fetches [RtMidi](https://github.com/thestk/rtmidi))

## Build

The normal `FA` build is the shipping application. It detects a connected FA or
FANTOM-0 from Roland's Universal Identity Reply and selects the matching protocol
adapter automatically. The default command and target remain unchanged:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/Volumes/Datastore/Qt/6.11.1/macos
cmake --build build -j
ctest --test-dir build --output-on-failure
open build/FAEditor.app
```

An optional standalone FANTOM development shell can still be built for adapter work:

```bash
cmake -S . -B build-fantom -DFAEDITOR_PRODUCT=FANTOM -DCMAKE_PREFIX_PATH=/Volumes/Datastore/Qt/6.11.1/macos
cmake --build build-fantom -j
ctest --test-dir build-fantom --output-on-failure
open build-fantom/FantomEditor.app
```

The standalone shell and the normal app use the same documented FANTOM-0 adapter.
The normal app is the intended user workflow; a second installation is not required.
FANTOM writes target documented Temporary Scene/Tone areas only.

## Usage

1. On the FA: System → USB Driver → **Vendor (MIDI+Audio)**; install Roland USB driver if needed.
2. In the app: **MIDI → Auto-connect instrument** (or pick ports — skip DAW CTRL). The app identifies FA versus FANTOM-0 automatically.
3. Tab **1. Sets & Tones**: pick a User/Preset slot from the **FA Set** dropdown (top bar) to recall it, select a part (left), click a tone to assign (right; icon previews). Optional **Scan** for User names. Local projects are in the **Library** tab.
4. **Mixer**, **Effects**, and **Tone** edit Temporary data on the FA. **Push Temp** for a full Temporary rewrite. To keep a **User** slot permanently, use **Write** on the FA itself (SysEx only edits Temporary).

## Reorder User Studio Sets

The FA has no SysEx command that stores into a User Studio Set slot, so slots are
rearranged in an SD-card backup instead:

1. On the FA, back up to the SD card and copy the `.SVD` file to your computer.
2. **File → Reorder User Studio Sets (SVD Backup)…**, open the backup, then drag sets
   by their ≡ handle, use ▲/▼ (Alt+Up/Down), **Move** or **Swap** to a slot number,
   or **Used Sets to Top** to close gaps.
3. **Save Reordered Backup…** writes a *new* backup pair (default `REORDER.SVD` plus
   `REORDER.BIN`). Copy **both** files to the backup folder on the SD card and use
   Restore on the FA — the FA reports a read error if the `.BIN` partner is missing.
   Keep names to 8 characters.

Only whole records are moved: each Studio Set together with its per-slot companion
record, and Favorites that point at a User Studio Set are renumbered to follow it.
Everything else in the backup is kept byte-for-byte, and the result is re-read and
checked before it is saved. Keep the original backup — restoring it undoes the change.

## DAW export

- **File → Export Current Studio Set as MIDI…** writes a Standard MIDI File type 1 with a conductor track and 16 named part tracks. Each part carries its MIDI channel, Bank Select MSB/LSB, Program Change, volume, pan, chorus send, and reverb send. Import the file into a DAW and route the resulting tracks to the FA MIDI port.
- **File → Export FA Tone Names (.midnam)…** writes the complete bundled FA tone catalog as a MIDI Name Document, grouped by the FA bank MSB/LSB values. Pro Tools and other MIDNAM-aware software can use it for named patch selection. Logic Pro does not document direct MIDNAM import; use the Studio Set MIDI export for Logic projects.

## Layout

- `src/app/` — shared application controller plus the optional standalone development shell
- `src/platform/` — shared capability boundary, automatic Roland identification, and device address adapters
- `src/model/`, `src/project/`, `src/undo/` — reusable model, local-backup/library, and undo primitives
- `qml/components/`, `qml/theme/` — shared Logic-inspired controls and theme
- `qml/Main.qml` — shipping workflow with automatic FA/FANTOM-0 workspace selection; `qml/FantomMain.qml` is the optional adapter-development shell
- `resources/tones/soundlist.json` — FA-06/07/08 preset tone catalog from Roland Sound List (`scripts/import_soundlist.py`)
- `resources/waves/waveforms.json` — waveform name catalog (`scripts/import_waveforms.py`)
- `roland_specs/` — official PDFs (reference only)
- [`docs/svd-format.md`](docs/svd-format.md) — experimental SVD1/MI73 container and packed-tone research notes

## Notes

Headless tests use `FakeInstrumentPlatform`; they never require MIDI hardware. Run them with
`ctest --test-dir build --output-on-failure`. The fake stores tone sections per part and can
inject deterministic connection/timeout-style failures.

- Unofficial third-party tool — not affiliated with Roland Corporation.
- Device editing uses documented SysEx. Experimental offline SVD1 research is
  isolated and must validate complete parameter schemas before enabling device writes.
- FA exposes Studio Set *data* only as Temporary (`18 00 00 00`); User/Preset slots are recalled via Setup Bank Select (`01 00 00 04`).

## Privacy

See [`PRIVACY.md`](PRIVACY.md). The app does not collect personal data.

## License

FA Editor source code is licensed under the **MIT License** — see [`LICENSE`](LICENSE).

Third-party components (Qt, RtMidi, Font Awesome Free, Material Icons, and
Roland reference documentation) are attributed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
