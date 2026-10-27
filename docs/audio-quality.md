# Audio quality and gain tuning

## Diagnose before adjusting gain

After each recording the reference sketch prints peak magnitude, RMS level and clipped-sample count. The `tools/analyze_wav.py` script computes similar statistics from the saved WAV and reports short-window levels.

A 16-bit sample peak close to 32768 means the signal is reaching full scale. Many clipped samples indicate too much digital gain. A very low RMS with a low peak may mean a quiet source, wrong channel/slot, incorrect bit alignment, or excessive right-shifting. A high hiss floor suggests sensor/electrical noise or power/wiring issues; a gain change cannot improve physical signal-to-noise ratio.

## I2S conversion is not universal

The reference profile receives 32-bit I2S words and shifts them to produce PCM16. The `ADR_I2S_PCM_RIGHT_SHIFT` setting is therefore coupled to:

- microphone data alignment (left/right aligned in a 32-bit slot);
- I2S driver conversion behavior;
- valid sample bits and channel slot;
- whether a library has already converted 32-bit input to 16-bit PCM.

On the reference board the exact 24-to-16 reduction is 16, but the sketch ships 15 (+6 dB) because the measured levels were quiet. That is a deliberate gain choice, not a different alignment; 16 restores unity. 14 was rejected: one loud take reached 31863/32768 and an earlier run clipped 571/80000 frames.

Do not port `rightShift=14` blindly to another microphone or newer driver. First capture raw samples, check for changing values as you speak, confirm channel selection, then determine the correct signed conversion. Some microphones/driver combinations can place meaningful bits differently.

## Repeatable training data

Use a fixed microphone position, distance, sampling rate, channel, PCM conversion, gain, and any filtering during one dataset session. Keep the same audio preprocessing in inference or normalize both training and inference consistently.

- `hey_hotpin`: repeat the exact phrase naturally with pauses.
- `noise`: capture typical non-target background conditions.
- `unknown`: capture ordinary speech and other sounds that should not activate the wake word.

Avoid making `noise` artificially silent or denoising it until it stops representing the real target environment. A model needs realistic negatives.

## Clicks and periodic artifacts

If repeated clicks appear at regular DMA-block intervals, inspect the I2S driver, buffer size/timeout, task scheduling, channel/slot format, signed conversion and core version. Save a short raw sample and inspect it before introducing filters. A low-pass filter may reduce high-frequency hiss but is not a proper repair for dropped, duplicated or malformed DMA blocks.
