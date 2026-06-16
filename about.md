# GD Auto-Clipper

**Automatically saves a clip of the runs you care about** — beat a level (or hit any
start%→end% you choose) and the mod tells OBS to save the moment, with game audio **and**
your mic, no button-pressing required.

Built for **macOS** (where there wasn't a clip tool like this), using OBS's Replay Buffer.

---

## How it works

You can't start recording *after* you win — the run's already over. So this mod relies on
**OBS's Replay Buffer**, which continuously keeps the last few minutes of gameplay in memory.
When a run matches one of your rules, the mod tells OBS to **save that buffer** — giving you a
clip of what *just* happened. If **ffmpeg** is available, the clip is automatically trimmed down
to the actual run length (+ padding) and named after the level; otherwise you get the full
replay-buffer length (the classic "instant replay" model).

The mod talks to OBS directly over **obs-websocket** — there's no separate app to run.

---

## Requirements

- **OBS Studio** (free — obsproject.com)
- **obs-websocket** — built into OBS 28+ (just needs to be enabled)
- A **Replay Buffer** set up in OBS

---

## Setup

### 1. In OBS (one time)

1. **Add your capture sources** (Sources → **+**):
   - **Display Capture** (or macOS Screen Capture) → your screen = the **video**
   - **macOS Audio Capture** → your game = the **game audio**
   - Settings → **Audio** → **Mic/Auxiliary Audio** → your mic = your **voice / key clicks**
2. **Enable the Replay Buffer:** Settings → **Output** → **Replay Buffer** → enable it and set
   **Maximum Replay Time** to how long you want your clips (e.g. 180 seconds). Bump
   **Maximum Memory** until any "not enough memory" warning disappears.
3. **Enable the WebSocket:** Tools → **WebSocket Server Settings** → **Enable WebSocket server**.
   Click **Show Connect Info** and copy the **password**.

### 2. In Geometry Dash

Press **F4** to open the settings, then:
- Paste your OBS **password** into the **OBS password** field.
- Set your **Record rules** (see below).

That's it. Keep OBS open while you play — the mod starts the replay buffer for you and pops a
**"Clip saved!"** message whenever it grabs one.

Clips are saved wherever OBS is set to save recordings: **OBS → Settings → Output →
Recording Path** (default `~/Movies`).

---

## Settings (press F4)

| Setting | What it does |
|---|---|
| **Record rules** | Which runs to clip, as `A-B` ranges. A run is clipped if it **started at or before A%** and **reached at least B%**. Comma-separate several. See examples below. |
| **Record in practice mode** | If off (default), runs completed in practice mode are ignored. |
| **Seconds before run** | Extra footage kept before the run started (only when ffmpeg trimming is on). Default 5. |
| **Seconds after run** | Extra footage kept after the run ends, so the win/death lands in the clip. Default 5. |
| **OBS host** | OBS WebSocket host. Leave as `localhost` unless OBS runs on another machine. |
| **OBS port** | OBS WebSocket port (from Tools → WebSocket Server Settings). Default `4455`. |
| **OBS password** | Your OBS WebSocket password. Leave blank only if you disabled authentication. |
| **ffmpeg path** | Optional. Path to ffmpeg to auto-trim clips to the run length. Blank = auto-detect (`~/bin`, Homebrew, `/usr`). Without ffmpeg, clips are the full replay-buffer length. |

### Record rules — examples

- `0-100` → only **full clears** from the start.
- `0-80` → any **full run that reaches at least 80%**.
- `43-100` → any run that **covered 43%→100%** (a 43%-startpos run that finishes, *and* a full
  0% run — but **not** a 51%-startpos run, since that one skipped the 43–51 stretch).
- `0-100, 0-90` → clips your **clear** — or, if you die, clips the run anyway as long as it reached **at least 90%** (a "don't lose a great attempt" safety net).

**When does it clip?** At the **end of a run** — the moment you die or finish — checking the furthest % you reached against your rules. It never interrupts you mid-run, and you get **one clip per attempt** showing how that run actually ended.

---

## Notes

- **Clips include all your OBS audio** — set up game audio + mic in OBS and both end up in the clip.
- **Clip length:** with ffmpeg, clips are trimmed to the run length + padding. Without it, you get the full Replay Buffer length (lower OBS's Maximum Replay Time for shorter clips).
- If you see **"Clip failed"**, check that OBS is open and the Replay Buffer + WebSocket are enabled.
- Your settings live under the mod's gear icon too — **F4** is just a shortcut to open them.
