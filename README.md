# GD Auto-Clipper

A [Geometry Dash](https://www.robtopgames.com/) ([Geode](https://geode-sdk.org/)) mod for
**macOS** that automatically saves a clip of the runs you care about. Beat a level — or hit any
`start%`→`end%` you choose — and the mod tells **OBS** to save the moment, with game audio *and*
your mic. No button-pressing, no separate app.

There are auto-clip tools for Windows GD; this fills the gap on Mac.

See **[about.md](about.md)** for full setup and all settings (it's also the in-game mod page).

## How it works

OBS's **Replay Buffer** continuously keeps the last few minutes of gameplay in memory. When a run
ends and matches one of your rules, the mod tells OBS (over obs-websocket, from inside the mod) to
save that buffer. You get a clip of what just happened.

## Settings (press F4 in-game)

`rules`, `practice`, `pad-after`, `obs-host`, `obs-port`, `obs-password`. Rules are `A-B` ranges
("started at or before A%, reached at least B%"), comma-separated — e.g. `0-100, 0-90` clips a
clear *or* a death that still reached 90%.

## Building

Requires the [Geode SDK + CLI](https://docs.geode-sdk.org/) and CMake. On Apple Silicon:

```sh
geode build -- -DCMAKE_OSX_ARCHITECTURES=arm64
```

(The mod and Geode's prebuilt libs are arm64; GD runs as arm64 on M-series Macs.)

## License

[MIT](LICENSE)
