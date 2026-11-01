# F5Low frame generation

**F5Low** is an FG Output of its own, next to FSR FG, DLSSG and XeFG. It is our own frame generation, up to 6x.

## What you need

- An NVIDIA GPU. On other GPUs F5Low is greyed out in the menu.
- `f5low.dll` in the `plugins` folder inside OptiScaler's folder
  (`<game>\OptiScaler\plugins\`, or `<game>\plugins\` without an OptiScaler subfolder; `[Plugins] Path` changes it).
  It is part of the release zip; without it F5Low is greyed out. An `f5low.ini` next to it is optional (the defaults
  are fine). It used to be called `xefg_cuda.dll`: rename an old copy to `f5low.dll`; an old `xefg_cuda.ini` keeps
  working until you put an `f5low.ini` next to it.
- Intel's `libxess_fg.dll`, version **1.3.4.95** or **1.3.1.78**, in OptiScaler's folder (or `XeFGPath`, or next to
  `f5low.dll`). The zip ships 1.3.4.95. f5low.dll contains none of Intel's code: **on every start it builds its frame generator
  from your `libxess_fg.dll`** in memory, in the background (about half a second); frame generation starts by itself
  when it is ready (until then only the real frames are shown). Nothing is written to disk, so there is no cache
  folder (old `f5low_cache` folders from earlier versions are removed by f5low itself); its log, if you turn it on,
  goes to `%LOCALAPPDATA%\f5low\f5low.log` when the plugins folder is not writable. With another `libxess_fg.dll` version, or if the build fails, f5low.dll hands over to
  Intel's own XeFG (up to 4x, as with FG Output XeFG). The Frame Generation page shows which one runs: "own frame
  generator running", "running on Intel's XeFG" or "unavailable", with the reason on hover. Needs a D3D12 game.

## Turning it on

1. Open the OptiScaler menu, Frame Generation: pick an **FG Input** and set **FG Output** to **F5Low**.
2. **Frame Generator** appears under it: **XeFG Tensor** (our XeFG frame generation on the tensor cores) is the
   only choice for now. DLSS-G and libxess will follow.
3. Save Settings and restart the game.

In the ini: `[FrameGen] FGOutput=f5low`, `[F5Low] FrameGenerator=auto` (= xefg_tensor).

## Options

F5Low uses the same options as XeFG (the "Frame Generation (F5Low)" section, `[XeFG]` in the ini): multiplier,
UI composition, borderless, latency output and so on. `[XeFG] MaxInterpolations` caps the multiplier (5 = up to 6x).

Changing between XeFG and F5Low needs a restart: only one of the two libraries can be loaded per game session.

## Checking that it runs

The overlay says `F5Low 2x` (or 3x ... 6x). With `LogToFile=true` the log has:

```
F5Low: loaded ...\plugins\f5low.dll
F5Low: Frame Generator XeFG Tensor (XEFG_CUDA_GENERATOR=own)
Max supported interpolations: 5 (F5Low library reports 5, MaxInterpolations 5)
```

If it cannot start at all, a notice appears and no frames are generated: set FG Output to XeFG and restart.
With `LogToFile=true` the log then says why (`F5Low: ...`).

`f5low.ini` in the plugins folder has its own settings (pacing, logging); the defaults are fine.
