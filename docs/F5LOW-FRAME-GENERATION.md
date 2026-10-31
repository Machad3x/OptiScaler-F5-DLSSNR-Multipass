# F5Low frame generation

**F5Low** is an FG Output of its own, next to FSR FG, DLSSG and XeFG. It is our own frame generation, up to 6x.

## What you need

- An NVIDIA GPU. On other GPUs F5Low is greyed out in the menu.
- The `plugins` folder from the release zip next to OptiScaler.dll, with `xefg_cuda.dll`, `xefg_cuda.ini` and
  `libxess_fg.dll` in it. Nothing to set: F5Low finds them there. (`XeFGPath` is for the XeFG output only.)

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
F5Low: loaded ...\plugins\xefg_cuda.dll
F5Low: Frame Generator XeFG Tensor (XEFG_CUDA_GENERATOR=own)
Max supported interpolations: 5 (F5Low library reports 5, MaxInterpolations 5)
```

`xefg_cuda.ini` in the plugins folder has its own settings (pacing, logging); the defaults are fine.
