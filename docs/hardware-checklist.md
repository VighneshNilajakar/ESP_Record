# Hardware validation checklist

Automated CI proves that the code builds and that the portable logic is correct.
It cannot prove that a microphone is wired correctly, that a chip's I2S peripheral
behaves as expected, or that a card is readable. That is what this checklist is
for.

**Rules**

1. Never mark a board as working in `docs/support-matrix.md` before completing
   this list for it, and commit both changes together.
2. Record what you observed, not what you expected. "Heard a click" is a result;
   "should work" is not.
3. A clip that counted the right number of frames but is silent still fails. A
   full frame count proves only that the peripheral moved data, not that it was
   your microphone's data.

## Per board and profile

Record the board revision, the core version, the card, and the supply.

- [ ] **Board / revision:** _______________________
- [ ] **Arduino-ESP32 core version:** _______________
- [ ] **Microphone:** _______________________
- [ ] **Storage:** _______________________
- [ ] **Supply:** _______________________ (note if it is USB, and which port/cable)

### 1. It builds and boots

- [ ] Compiles for this board and core (CI or `pio run -e <env>`).
- [ ] Uploads without a boot loop.
- [ ] Serial Monitor opens at the documented baud and shows the banner.
- [ ] `STATUS` reports the expected format, strategy and card size.

### 2. The microphone is actually captured

- [ ] `RECORD noise` completes and reports the requested frame count.
- [ ] **A clip of speech contains speech.** Play it back, or confirm with
      `python tools/analyze_wav.py <file>`: peak and RMS must be clearly above
      the silence warning thresholds (peak > 8, RMS > 2 on a 16-bit scale).
- [ ] A silent room produces a quiet clip, not a loud one. Near-full-scale
      samples while silent means the gain is wrong, not the mic.
- [ ] Clipping is under 0.1 % of samples. If it is higher, reduce the right
      shift before collecting a dataset.
- [ ] DC offset is under 5 % of full scale. A large offset points at I2S
      conversion, microphone bias or data alignment.
- [ ] **No repeating clicks.** Listen for a periodic tick. If present, stop and
      investigate I2S alignment and DMA block handling; do not filter it away.
- [ ] Duration matches: a 10 s clip reports roughly 10 s and
      `44 + rate * seconds * 2 * channels` bytes for mono PCM16.
- [ ] Recorded sample rate is 16 kHz (or whatever the model pipeline expects).

### 3. Channel and slot

- [ ] Moving the microphone's L/R pin changes which channel is recorded, or the
      profile explicitly documents that only one slot is wired.

### 4. Storage

- [ ] `LIST` shows every clip that was recorded.
- [ ] Removing and reinserting the card, then `LIST`, still works.
- [ ] Power-cycling the board, then `LIST`, still shows the files.
- [ ] A saved file opens in at least two independent tools (e.g. Audacity and
      the platform's default player) **and plays back at the right length**.
- [ ] `python tools/analyze_wav.py` reports no RIFF/data size mismatch on any
      clip. A mismatch means an interrupted write.

### 5. Failure handling

- [ ] `DISCARD` drops the pending clip and frees memory.
- [ ] `RECORD` while a clip is pending is refused with a clear message.
- [ ] With no card inserted, `RECORD` (streaming) fails cleanly and leaves the
      indicator off.
- [ ] With no card inserted, `SAVE` (full buffer) fails, **keeps the clip in
      RAM**, and reports that `SAVE` can be retried after the card is fixed.
- [ ] After inserting the card, `SAVE` completes the clip.
- [ ] Removing the indicator LED, or using a board without one, still records.

### 6. Audio quality across a real session

- [ ] Several sessions per label, same placement, same gain, no level drift.
- [ ] Positive and negative classes are genuinely different recordings rather
      than the same file renamed.
- [ ] Enough distinct sessions that training and test splits do not share a
      single recording. This is the most common way a keyword model ends up
      measuring memorisation instead of generalisation.

## Recording what you did

When you complete this for a profile, add a line to `SOURCES.md` or the profile
section of `docs/hardware.md` with: date, board revision, core version,
microphone, storage, supply, and what you observed. That is what turns "probably
works" into evidence a maintainer can act on.