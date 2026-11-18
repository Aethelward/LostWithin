"""WAV checks and PCM wrapping (no `unreal` import).

Responses are parsed by hand rather than with the `wave` module, so WAVE_FORMAT_EXTENSIBLE headers and streamed
files with placeholder chunk sizes both work. The result is always re-emitted as a canonical 16-bit PCM WAV that
the Unreal importer accepts.
"""

from __future__ import annotations

import array
import math
import struct
import sys
from typing import Dict, List, Tuple

WAVE_FORMAT_PCM = 0x0001
WAVE_FORMAT_EXTENSIBLE = 0xFFFE
_PCM_SUBFORMAT_PREFIX = struct.pack("<H", WAVE_FORMAT_PCM)


class AudioFormatError(ValueError):
    pass


class WavInfo:
    __slots__ = ("channels", "sample_rate", "sample_width", "frames")

    def __init__(self, channels: int, sample_rate: int, sample_width: int, frames: int) -> None:
        self.channels = channels
        self.sample_rate = sample_rate
        self.sample_width = sample_width
        self.frames = frames

    @property
    def duration(self) -> float:
        return self.frames / float(self.sample_rate) if self.sample_rate else 0.0

    def as_dict(self) -> Dict[str, float]:
        return {"channels": self.channels, "sample_rate": self.sample_rate,
                "sample_width": self.sample_width, "frames": self.frames,
                "duration": round(self.duration, 4)}


def build_wav(pcm: bytes, sample_rate: int, channels: int = 1, sample_width: int = 2) -> bytes:
    if sample_width != 2:
        raise AudioFormatError("only 16-bit PCM is supported")
    block_align = channels * sample_width
    if len(pcm) % block_align:
        pcm = pcm[: len(pcm) - (len(pcm) % block_align)]
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    fmt = struct.pack("<HHIIHH", WAVE_FORMAT_PCM, channels, sample_rate,
                      sample_rate * block_align, block_align, sample_width * 8)
    return header + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(pcm)) + pcm


def _read_chunks(data: bytes) -> Tuple[bytes, bytes]:
    """Returns the raw fmt chunk body and the PCM data of a RIFF/WAVE file."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise AudioFormatError("response is not a RIFF/WAVE file")
    offset = 12
    fmt = None
    while offset + 8 <= len(data):
        chunk_id = data[offset:offset + 4]
        size = struct.unpack("<I", data[offset + 4:offset + 8])[0]
        body_start = offset + 8
        if chunk_id == b"data":
            if fmt is None:
                raise AudioFormatError("WAV data chunk comes before its fmt chunk")
            remaining = len(data) - body_start
            # Streamed WAVs may carry 0 or 0xFFFFFFFF as the data size; use what actually arrived.
            if size == 0 or size > remaining:
                size = remaining
            return fmt, data[body_start:body_start + size]
        if chunk_id == b"fmt ":
            if size < 16 or body_start + size > len(data):
                raise AudioFormatError("truncated fmt chunk")
            fmt = data[body_start:body_start + size]
        offset = body_start + size + (size & 1)
    if fmt is None:
        raise AudioFormatError("WAV has no fmt chunk")
    raise AudioFormatError("WAV has no data chunk")


def parse_wav(data: bytes) -> Tuple[WavInfo, bytes]:
    """Returns (WavInfo, pcm_bytes) for a 16-bit PCM WAV, or raises AudioFormatError."""
    fmt, pcm = _read_chunks(data)
    audio_format, channels, sample_rate, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
    if audio_format == WAVE_FORMAT_EXTENSIBLE:
        if len(fmt) < 40 or fmt[24:26] != _PCM_SUBFORMAT_PREFIX:
            raise AudioFormatError("WAV extensible format is not PCM")
    elif audio_format != WAVE_FORMAT_PCM:
        raise AudioFormatError("WAV format {} is not PCM".format(audio_format))
    if bits != 16:
        raise AudioFormatError("WAV is {}-bit; only 16-bit PCM is supported".format(bits))
    if channels < 1 or sample_rate <= 0:
        raise AudioFormatError("WAV header has invalid channels or sample rate")

    block_align = channels * 2
    frames = len(pcm) // block_align
    if frames == 0:
        raise AudioFormatError("WAV contains no audio frames")
    return WavInfo(channels, sample_rate, 2, frames), pcm[: frames * block_align]


def _frame_peaks(pcm: bytes, channels: int) -> List[int]:
    """Loudest absolute 16-bit sample of each frame across its channels."""
    magnitudes = [abs(value) for value in _samples(pcm)]
    if channels == 1:
        return magnitudes
    return [max(magnitudes[i:i + channels]) for i in range(0, len(magnitudes) - channels + 1, channels)]


MIN_USABLE_PEAK_DB = -50.0


def _samples(pcm: bytes) -> "array.array":
    samples = array.array("h")
    samples.frombytes(pcm)
    if sys.byteorder == "big":
        samples.byteswap()
    return samples


def _to_bytes(samples: "array.array") -> bytes:
    if sys.byteorder == "big":
        samples = array.array("h", samples)
        samples.byteswap()
    return samples.tobytes()


def peak_dbfs(wav_bytes: bytes) -> float:
    _, pcm = parse_wav(wav_bytes)
    peak = max((abs(value) for value in _samples(pcm)), default=0)
    return 20.0 * math.log10(peak / 32767.0) if peak else float("-inf")


def normalize_peak(wav_bytes: bytes, target_db: float) -> Tuple[bytes, WavInfo, float]:
    """Scales the audio so its loudest sample sits at target_db dBFS; returns (wav, info, gain_db).

    Near-silent audio (peak below MIN_USABLE_PEAK_DB) is refused rather than amplified into noise.
    """
    info, pcm = parse_wav(wav_bytes)
    samples = _samples(pcm)
    peak = max((abs(value) for value in samples), default=0)
    peak_db = 20.0 * math.log10(peak / 32767.0) if peak else float("-inf")
    if peak_db < MIN_USABLE_PEAK_DB:
        raise AudioFormatError("audio is near silent (peak {:.1f} dBFS, below {} dBFS)".format(peak_db, MIN_USABLE_PEAK_DB))
    gain_db = target_db - peak_db
    gain = 10.0 ** (gain_db / 20.0)
    scaled = array.array("h", (max(-32768, min(32767, int(round(value * gain)))) for value in samples))
    return build_wav(_to_bytes(scaled), info.sample_rate, info.channels, 2), info, gain_db


def trim_silence(wav_bytes: bytes, threshold_db: float = -45.0, pre_ms: float = 10.0,
                 post_ms: float = 60.0) -> Tuple[bytes, WavInfo]:
    """Cuts leading and trailing audio quieter than threshold_db (dBFS), keeping short pads around the sound."""
    info, pcm = parse_wav(wav_bytes)
    threshold = 32767.0 * (10.0 ** (threshold_db / 20.0))
    peaks = _frame_peaks(pcm, info.channels)
    loud = [index for index, peak in enumerate(peaks) if peak > threshold]
    if not loud:
        raise AudioFormatError("audio is silent below {} dBFS; nothing to keep".format(threshold_db))
    first = max(0, loud[0] - int(info.sample_rate * pre_ms / 1000.0))
    last = min(info.frames, loud[-1] + 1 + int(info.sample_rate * post_ms / 1000.0))
    block = info.channels * 2
    return build_wav(pcm[first * block:last * block], info.sample_rate, info.channels, 2), WavInfo(
        info.channels, info.sample_rate, 2, last - first)


def normalize_response(data: bytes, output_format: str) -> Tuple[bytes, WavInfo]:
    """Converts an API response into (canonical_wav_bytes, WavInfo) according to output_format."""
    kind, _, rate = output_format.partition("_")
    if kind not in ("wav", "pcm"):
        raise AudioFormatError("output format {} cannot be imported; use wav_* or pcm_*".format(output_format))
    if kind == "pcm" and data[:4] != b"RIFF":
        if len(data) < 2:
            raise AudioFormatError("PCM response is empty")
        data = build_wav(data, int(rate))
    info, pcm = parse_wav(data)
    return build_wav(pcm, info.sample_rate, info.channels, 2), info
