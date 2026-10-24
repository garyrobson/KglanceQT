# Test fixtures

The animation GIFs were generated for KGlanceQT using Pillow; they contain only
solid colours and rectangles and are covered by the repository's AGPL-3.0-only
license. Pillow is not needed to build or run the tests.

- `animation.gif`: 24 × 16 pixels, three composited frames (red, red with a green
  centre rectangle, blue), 100/160/220 ms delays, infinite looping. The second
  frame uses a partial image rectangle to exercise plugin frame composition.
- `animation-once.gif`: the same frames, 40/60/80 ms delays, no loop extension.
- `animation-over-limit.gif`: 257 alternating red/blue 4 × 4 frames at 20 ms,
  exercising the hard 256-frame decoding limit.
- `animation-corrupt.gif`: `animation.gif` with an invalid LZW code size in the
  last frame. Qt's GIF plugin can recover the two valid earlier frames without
  reporting an error; the test accepts either this usable prefix or an explained
  still-frame fallback if the installed plugin reports a decode failure.

`autoplay.webm` is the existing five-second video playback fixture.
