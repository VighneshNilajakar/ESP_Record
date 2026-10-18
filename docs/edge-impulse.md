# Edge Impulse dataset workflow

This recorder produces long PCM16 WAV clips so they can be split into labeled windows in Edge Impulse.

1. Configure the impulse to use the same sample rate as the recordings (the reference is 16 kHz) and note the impulse's window/segment length.
2. Record multiple sessions for each label; avoid capturing all labels back-to-back in one identical room/pose only.
3. Use consistent recorder gain and microphone layout across labels.
4. Copy WAV files from the card to the computer and run `python tools/analyze_wav.py <file.wav>`.
5. Upload WAVs to Edge Impulse and use **Split sample** at the configured segment size (the HOTPIN model currently uses 1,000 ms segments per the user's project context).
6. Listen to / inspect individual split segments. Delete empty, mislabeled or clipped windows; auto splitting can miss a phrase or split across a boundary.
7. Keep train/test data separated by recording session where possible to avoid near-identical adjacent windows leaking between sets.
8. Include realistic `noise` and `unknown` negatives. They have different meanings: background/ambient non-target audio vs ordinary speech/other non-target sounds. Define the class policy clearly before recording.

A validation score is only meaningful when the dataset is representative. Do not assume that perfect validation accuracy proves reliable wake-word detection in the physical environment.
