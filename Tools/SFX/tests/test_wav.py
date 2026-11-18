from __future__ import annotations

import math
import struct
import unittest

import support
from lostwithin_sfx.wav import (AudioFormatError, build_wav, normalize_peak, normalize_response, parse_wav,
                                peak_dbfs, trim_silence)


def _wav_with_fmt(fmt: bytes, pcm: bytes, data_size: int = -1, extra_chunk: bytes = b"") -> bytes:
    size = len(pcm) if data_size < 0 else data_size
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + extra_chunk + b"data" + struct.pack("<I", size) + pcm
    return b"RIFF" + struct.pack("<I", len(body)) + body


class WavTests(unittest.TestCase):
    def test_round_trip(self) -> None:
        info, pcm = parse_wav(support.silence_wav(1.5))
        self.assertEqual((info.channels, info.sample_rate, info.sample_width), (1, 48000, 2))
        self.assertEqual(info.frames, 72000)
        self.assertAlmostEqual(info.duration, 1.5)
        self.assertEqual(len(pcm), 144000)

    def test_wav_response_is_normalized(self) -> None:
        wav, info = normalize_response(support.silence_wav(2.0), "wav_48000")
        self.assertEqual(wav[:4], b"RIFF")
        self.assertAlmostEqual(info.duration, 2.0)

    def test_raw_pcm_response_is_wrapped(self) -> None:
        wav, info = normalize_response(b"\x01\x00" * 24000, "pcm_48000")
        self.assertEqual(wav[:4], b"RIFF")
        self.assertEqual(info.sample_rate, 48000)
        self.assertAlmostEqual(info.duration, 0.5)

    def test_streamed_placeholder_data_size_uses_actual_bytes(self) -> None:
        fmt = struct.pack("<HHIIHH", 1, 1, 48000, 96000, 2, 16)
        wav, info = normalize_response(_wav_with_fmt(fmt, b"\x00\x00" * 4800, data_size=0xFFFFFFFF), "wav_48000")
        self.assertEqual(info.frames, 4800)
        self.assertEqual(struct.unpack("<I", wav[40:44])[0], 9600, "re-emitted header carries the real size")

    def test_odd_sized_extra_chunk_is_skipped_with_padding(self) -> None:
        fmt = struct.pack("<HHIIHH", 1, 2, 48000, 192000, 4, 16)
        extra = b"LIST" + struct.pack("<I", 3) + b"abc" + b"\x00"
        info, _ = parse_wav(_wav_with_fmt(fmt, b"\x00" * 400, extra_chunk=extra))
        self.assertEqual((info.channels, info.frames), (2, 100))

    def test_extensible_pcm_accepted_and_float_rejected(self) -> None:
        base = struct.pack("<HHIIHH", 0xFFFE, 1, 48000, 96000, 2, 16)
        pcm_guid = struct.pack("<H", 1) + b"\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71"
        float_guid = struct.pack("<H", 3) + pcm_guid[2:]
        tail = struct.pack("<HHI", 22, 16, 4)
        info, _ = parse_wav(_wav_with_fmt(base + tail + pcm_guid, b"\x00\x00" * 10))
        self.assertEqual(info.frames, 10)
        with self.assertRaises(AudioFormatError):
            parse_wav(_wav_with_fmt(base + tail + float_guid, b"\x00\x00" * 10))

    def test_rejects_non_wav_24_bit_and_empty_audio(self) -> None:
        with self.assertRaises(AudioFormatError):
            normalize_response(b"ID3\x04\x00 mp3 bytes", "wav_48000")
        fmt24 = struct.pack("<HHIIHH", 1, 1, 48000, 144000, 3, 24)
        with self.assertRaises(AudioFormatError):
            parse_wav(_wav_with_fmt(fmt24, b"\x00" * 30))
        with self.assertRaises(AudioFormatError):
            parse_wav(build_wav(b"", 48000))
        with self.assertRaises(AudioFormatError):
            normalize_response(support.silence_wav(1.0), "mp3_44100_128")

    def test_trim_silence_cuts_to_the_sound_with_pads(self) -> None:
        rate = 48000
        quiet = b"\x05\x00" * int(rate * 0.25)        # 5/32767 is far below -45 dBFS
        loud = struct.pack("<h", 12000) * int(rate * 0.1)
        wav, info = trim_silence(build_wav(quiet + loud + quiet, rate))
        expected = int(rate * 0.1) + int(rate * 0.010) + int(rate * 0.060)
        self.assertEqual(info.frames, expected)
        self.assertAlmostEqual(info.duration, 0.17, places=3)
        self.assertEqual(parse_wav(wav)[0].frames, expected)

    def test_trim_silence_handles_stereo_and_edges(self) -> None:
        rate = 48000
        frame_loud_right = struct.pack("<hh", 0, -32768)
        wav, info = trim_silence(build_wav(frame_loud_right * 100 + b"\x00\x00\x00\x00" * rate, rate, channels=2))
        self.assertEqual(info.channels, 2)
        self.assertEqual(info.frames, 100 + int(rate * 0.060), "no pre-pad before the first frame")

    def test_trim_silence_rejects_all_silent_audio(self) -> None:
        with self.assertRaises(AudioFormatError):
            trim_silence(support.silence_wav(0.5))

    def test_normalize_peak_reaches_target_and_reports_gain(self) -> None:
        rate = 48000
        quiet_tap = struct.pack("<h", 400) * 100 + struct.pack("<h", -360) * 100   # about -38 dBFS
        wav, info, gain_db = normalize_peak(build_wav(quiet_tap, rate), -3.0)
        self.assertAlmostEqual(peak_dbfs(wav), -3.0, places=2)
        self.assertAlmostEqual(gain_db, -3.0 - 20 * math.log10(400 / 32767), places=3)
        self.assertEqual(parse_wav(wav)[0].frames, info.frames)

    def test_normalize_peak_refuses_near_silent_audio(self) -> None:
        nearly_silent = struct.pack("<h", 70) * 1000   # about -53 dBFS, like the rejected doll take
        with self.assertRaises(AudioFormatError) as caught:
            normalize_peak(build_wav(nearly_silent, 48000), -3.0)
        self.assertIn("near silent", str(caught.exception))
        with self.assertRaises(AudioFormatError):
            normalize_peak(support.silence_wav(0.5), -3.0)

    def test_normalize_peak_never_clips(self) -> None:
        loud = struct.pack("<h", 32767) * 10 + struct.pack("<h", -32768) * 10
        wav, _, gain_db = normalize_peak(build_wav(loud, 48000), 0.0)
        self.assertLessEqual(abs(gain_db), 0.001)
        before = struct.unpack("<20h", loud)
        after = struct.unpack("<20h", parse_wav(wav)[1])
        self.assertTrue(all(abs(a - b) <= 1 for a, b in zip(before, after)), "within one step of the input")
        self.assertLessEqual(peak_dbfs(wav), 0.001)

    def test_data_before_fmt_is_rejected(self) -> None:
        bad = b"RIFF" + struct.pack("<I", 20) + b"WAVE" + b"data" + struct.pack("<I", 4) + b"\x00" * 4
        with self.assertRaises(AudioFormatError):
            parse_wav(bad)


if __name__ == "__main__":
    unittest.main()
