import hackylens as hl

if not hl.audio_available():
    print("This board has no built-in audio support.")
else:
    # A quiet 440 Hz triangle wave with a short fade at both ends.
    rate = hl.AUDIO_RATE_HZ
    frames = rate // 4
    tone = bytearray(2 * frames)
    for i in range(frames):
        phase = i * 440 % rate
        sample = 5000 - (20000 * abs(phase - rate // 2)) // rate
        sample = sample * min(i, frames - 1 - i, 100) // 100
        tone[2 * i] = sample & 255
        tone[2 * i + 1] = (sample >> 8) & 255
    hl.audio_play(tone)
    print("Speak after the tone.")
    hl.sleep_ms(400)
    recording = bytearray(hl.AUDIO_MAX_BYTES)
    print("Recording", hl.audio_record_into(recording), "bytes.")
    hl.sleep_ms(200)
    hl.audio_play(recording)
