from __future__ import annotations

import ast
import os
import struct
import unittest
from unittest import mock

import support
import lostwithin_sfx as sfx
from lostwithin_sfx.api import ApiError, ApiKeyMissing
from lostwithin_sfx.wav import build_wav


class RunTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest_file = support.write_manifest(support.small_manifest())
        self.addCleanup(os.remove, self.manifest_file)
        self.backend = support.FakeBackend(self.manifest_file)
        self.client = support.FakeClient()
        env = mock.patch.dict(os.environ, {"ELEVENLABS_API_KEY": support.FAKE_KEY}, clear=False)
        env.start()
        self.addCleanup(env.stop)

    def run_missing(self, **kwargs):
        return sfx.generate_missing(backend=self.backend, client_factory=self.client, **kwargs)

    def test_dry_run_reports_without_calling_the_api(self) -> None:
        summary = self.run_missing(dry_run=True)
        self.assertEqual(self.client.calls, [])
        self.assertEqual([r["id"] for r in summary["planned"]], ["a", "b", "c"])
        self.assertEqual(summary["estimated_credits"], 160 + 40 + 400)
        self.assertEqual(self.backend.imported, [])

    def test_generates_and_imports_only_missing_takes(self) -> None:
        self.backend.existing.add("/Game/T/SFX_A_01")
        summary = self.run_missing()
        self.assertIsNone(summary["error"])
        self.assertEqual(self.backend.imported, ["/Game/T/SFX_A_02", "/Game/T/SFX_B_01", "/Game/T/AMB_C_01"])
        self.assertEqual(len(self.client.calls), 3)
        self.assertEqual(self.client.calls[2], {"text": "c loop", "duration_seconds": 10.0, "prompt_influence": 0.4,
                                                "loop": True, "model_id": "eleven_text_to_sound_v3",
                                                "output_format": "wav_48000"})
        again = self.run_missing()
        self.assertEqual(again["planned"], [])
        self.assertEqual(len(self.client.calls), 3, "a second run spends nothing")

    def test_cap_defers_whole_entries_and_refuses_oversized_ones(self) -> None:
        summary = self.run_missing(max_credits=250)
        self.assertEqual([r["id"] for r in summary["planned"]], ["a", "b"])
        self.assertEqual([r["id"] for r in summary["refused"]], ["c"])
        self.assertEqual(len(self.client.calls), 3)

        self.backend = support.FakeBackend(self.manifest_file)
        summary = self.run_missing(max_credits=420)
        self.assertEqual([r["id"] for r in summary["planned"]], ["a", "b"])
        self.assertEqual([r["id"] for r in summary["deferred"]], ["c"])

    def test_single_entry_over_cap_makes_no_call(self) -> None:
        summary = sfx.generate("c", max_credits=100, backend=self.backend, client_factory=self.client)
        self.assertEqual([r["id"] for r in summary["refused"]], ["c"])
        self.assertEqual(self.client.calls, [])

    def test_force_regenerates_existing_takes(self) -> None:
        self.backend.existing.update({"/Game/T/SFX_B_01"})
        summary = sfx.generate("b", force=True, backend=self.backend, client_factory=self.client)
        self.assertEqual(summary["completed"][0]["asset"], "/Game/T/SFX_B_01")
        self.assertEqual(len(self.client.calls), 1)

    def test_interactive_decline_spends_nothing(self) -> None:
        self.backend.confirm_answer = False
        summary = self.run_missing(interactive=True)
        self.assertTrue(summary["cancelled"])
        self.assertEqual(self.client.calls, [])
        self.assertIn("600 credits", self.backend.confirmations[0])

    def test_cancel_in_progress_stops_after_current_take(self) -> None:
        self.backend.cancel_after = 1
        summary = self.run_missing()
        self.assertTrue(summary["cancelled"])
        self.assertEqual(len(self.backend.imported), 1)

    def test_api_failure_is_reported_redacted_and_keeps_completed_takes(self) -> None:
        self.client.responses = [support.silence_wav(2.0), ApiError(500, "boom " + support.FAKE_KEY)]
        summary = self.run_missing()
        self.assertEqual(len(summary["completed"]), 1)
        self.assertIn("boom", summary["error"])
        self.assertNotIn(support.FAKE_KEY, summary["error"])
        self.assertFalse(any(support.FAKE_KEY in m for m in self.backend.messages))
        self.assertFalse(any(support.FAKE_KEY in str(d) for d in self.backend.downloads))

    def test_invalid_audio_twice_is_kept_and_not_imported(self) -> None:
        self.client.responses = [b"ID3 not a wav", b"ID3 still not"]
        summary = self.run_missing()
        self.assertIn("not a RIFF/WAVE", summary["error"])
        self.assertEqual(self.backend.invalid, [b"ID3 not a wav", b"ID3 still not"])
        self.assertEqual(self.backend.imported, [])
        self.assertEqual(len(self.client.calls), 2)

    def test_one_unusable_response_is_regenerated_once(self) -> None:
        self.client.responses = [b"ID3 not a wav"]
        summary = sfx.generate("b", backend=self.backend, client_factory=self.client)
        self.assertIsNone(summary["error"])
        self.assertEqual(self.backend.imported, ["/Game/T/SFX_B_01"])
        self.assertEqual(len(self.client.calls), 2)
        self.assertEqual(len(self.backend.invalid), 1)
        self.assertTrue(any("generated it again" in w for w in summary["warnings"]))

    def test_length_mismatch_is_a_warning(self) -> None:
        self.client.duration_scale = 0.5
        summary = sfx.generate("b", backend=self.backend, client_factory=self.client)
        self.assertEqual(len(summary["completed"]), 1)
        self.assertTrue(any("requested 1.00 s" in w for w in summary["warnings"]))

    def test_trimmed_entry_records_both_lengths(self) -> None:
        data = support.small_manifest()
        data["sounds"][1]["trim_silence"] = True
        data["sounds"][1]["normalize_peak_db"] = -3.0
        trimmed_file = support.write_manifest(data)
        self.addCleanup(os.remove, trimmed_file)
        backend = support.FakeBackend(trimmed_file)
        rate = 48000
        audio = b"\x00\x00" * int(rate * 0.4) + struct.pack("<h", 9000) * int(rate * 0.2) + b"\x00\x00" * int(rate * 0.4)
        client = support.FakeClient([build_wav(audio, rate)])
        summary = sfx.generate("b", backend=backend, client_factory=client)
        self.assertIsNone(summary["error"])
        done = summary["completed"][0]
        self.assertEqual(done["generated_duration"], 1.0)
        self.assertAlmostEqual(done["trimmed_duration"], 0.27, places=3)
        self.assertAlmostEqual(backend.downloads[0]["info"]["trimmed_duration"], 0.27, places=3)
        self.assertAlmostEqual(backend.downloads[0]["info"]["gain_db"], 8.22, places=1)  # 9000 → -3 dBFS

    def test_missing_key_stops_before_any_call(self) -> None:
        with mock.patch.dict(os.environ, {"ELEVENLABS_API_KEY": "", "LOCALAPPDATA": "/nonexistent"}):
            with self.assertRaises(ApiKeyMissing):
                self.run_missing()
        self.assertEqual(self.client.calls, [])

    def test_status_lists_present_and_missing(self) -> None:
        self.backend.existing.add("/Game/T/SFX_B_01")
        report = sfx.status(backend=self.backend)
        rows = {r["id"]: r for r in report["sounds"]}
        self.assertEqual(rows["b"]["present"], ["/Game/T/SFX_B_01"])
        self.assertEqual(rows["a"]["missing"], ["/Game/T/SFX_A_01", "/Game/T/SFX_A_02"])
        self.assertEqual(report["missing_takes"], 3)
        self.assertEqual(report["credits_missing"], 560)

    def test_credits_summary(self) -> None:
        account = sfx.credits(backend=self.backend, client_factory=self.client)
        self.assertEqual(account["remaining"], 8800)


class CompatibilityTests(unittest.TestCase):
    def test_modules_parse_as_python_3_11(self) -> None:
        """The editor embeds Python 3.11; the server runs newer Python, so check syntax explicitly."""
        for root, _, files in os.walk(support.PYTHON_DIR):
            for name in files:
                if name.endswith(".py"):
                    path = os.path.join(root, name)
                    with open(path, "r", encoding="utf-8") as handle:
                        ast.parse(handle.read(), filename=path, feature_version=(3, 11))


if __name__ == "__main__":
    unittest.main()
