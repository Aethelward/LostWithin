from __future__ import annotations

import copy
import unittest

import support
from lostwithin_sfx.manifest import ManifestError, estimate_credits, load_manifest, parse_manifest, plan_run


class RealManifestTests(unittest.TestCase):
    def test_project_manifest_is_valid_and_matches_the_planned_batch(self) -> None:
        manifest = load_manifest(support.MANIFEST_FILE)
        self.assertEqual(len(manifest.sounds), 42)
        total = sum(estimate_credits(e.duration, e.variations) for e in manifest.sounds)
        self.assertEqual(total, 8708)
        self.assertNotIn("death_sting", manifest.ids(), "replaced by death_jumpscare + death_game_over")
        steps = [e for e in manifest.sounds if e.id.endswith("_step")]
        self.assertEqual(sorted(e.id for e in steps), ["bonedog_step", "doll_step", "ghoul_step", "lunger_step"])
        self.assertTrue(all(e.trim_silence and not e.loop and e.normalize_peak_db == -3.0 for e in steps))
        reveal = manifest.get("monster_reveal")
        self.assertEqual(reveal.variations, 3)
        self.assertTrue(reveal.trim_silence)
        self.assertLess(manifest.get("death_impact").duration, 2.6)
        self.assertEqual(manifest.max_credits_per_run, 3000)
        for entry in manifest.sounds:
            self.assertLessEqual(estimate_credits(entry.duration, entry.variations), manifest.max_credits_per_run,
                                 entry.id + " must fit in one run")
            self.assertEqual(entry.model_id, "eleven_text_to_sound_v3")
            self.assertEqual(entry.output_format, "wav_48000")
        self.assertTrue(all(e.loop for e in manifest.sounds if e.name.startswith("AMB_")))
        # Monster-senses cues (2026-11-20): no loops; every one-shot cue is trimmed (the plate idle bed is not).
        senses = [e for e in manifest.sounds if e.folder.endswith(("/Mech", "/Senses"))]
        self.assertEqual(len(senses), 13)
        self.assertTrue(all(not e.loop for e in senses))
        self.assertTrue(all(e.trim_silence for e in senses if e.id != "lunger_plate_idle"))
        self.assertFalse(any(e.loop for e in manifest.sounds if not e.name.startswith("AMB_")))
        self.assertNotIn("lunger_alert", manifest.ids(), "Lunger is deaf and has no alert vocals")

    def test_asset_paths_follow_name_and_take(self) -> None:
        entry = load_manifest(support.MANIFEST_FILE).get("ghoul_idle")
        self.assertEqual(entry.asset_path(3), "/Game/NPC/Ghoul/SFX/Voice/VO_Ghoul_Idle_03")
        self.assertEqual(entry.object_path(1), "/Game/NPC/Ghoul/SFX/Voice/VO_Ghoul_Idle_01.VO_Ghoul_Idle_01")


class ValidationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.data = support.small_manifest()

    def assertRejected(self, mutate, fragment: str) -> None:
        data = copy.deepcopy(self.data)
        mutate(data)
        with self.assertRaises(ManifestError) as caught:
            parse_manifest(data)
        self.assertIn(fragment, str(caught.exception))

    def test_defaults_and_overrides_apply(self) -> None:
        self.data["sounds"][1]["model_id"] = "eleven_text_to_sound_v2"
        manifest = parse_manifest(self.data)
        self.assertEqual(manifest.get("a").prompt_influence, 0.4)
        self.assertEqual(manifest.get("a").variations, 2)
        self.assertEqual(manifest.get("b").model_id, "eleven_text_to_sound_v2")
        self.assertTrue(manifest.get("c").loop)

    def test_rejects_duplicate_ids(self) -> None:
        self.assertRejected(lambda d: d["sounds"][1].update(id="a"), "duplicate id")

    def test_rejects_duration_out_of_range(self) -> None:
        self.assertRejected(lambda d: d["sounds"][0].update(duration=0.4), "duration")
        self.assertRejected(lambda d: d["sounds"][0].update(duration=30.5), "duration")

    def test_rejects_folder_outside_game(self) -> None:
        self.assertRejected(lambda d: d["sounds"][0].update(folder="/Engine/X"), "folder")
        self.assertRejected(lambda d: d["sounds"][0].update(folder="/Game/../X"), "folder")

    def test_rejects_name_with_take_suffix_or_bad_characters(self) -> None:
        self.assertRejected(lambda d: d["sounds"][0].update(name="SFX_A_01"), "name")
        self.assertRejected(lambda d: d["sounds"][0].update(name="SFX A"), "name")

    def test_rejects_unknown_fields_models_and_formats(self) -> None:
        self.assertRejected(lambda d: d["sounds"][0].update(pitch=2), "unknown field")
        self.assertRejected(lambda d: d["sounds"][0].update(model_id="other"), "model_id")
        self.assertRejected(lambda d: d["sounds"][0].update(output_format="mp3_44100_128"), "output_format")
        self.assertRejected(lambda d: d["sounds"][0].update(prompt_influence=1.5), "prompt_influence")
        self.assertRejected(lambda d: d["sounds"][0].update(variations=0), "variations")

    def test_trim_silence_is_optional_and_not_allowed_on_loops(self) -> None:
        self.data["sounds"][1]["trim_silence"] = True
        manifest = parse_manifest(self.data)
        self.assertTrue(manifest.get("b").trim_silence)
        self.assertFalse(manifest.get("a").trim_silence)
        self.assertRejected(lambda d: d["sounds"][2].update(trim_silence=True), "seamless loop")
        self.assertRejected(lambda d: d["sounds"][0].update(trim_silence="yes"), "trim_silence")

    def test_normalize_peak_db_range(self) -> None:
        self.data["sounds"][0]["normalize_peak_db"] = -3
        self.assertEqual(parse_manifest(self.data).get("a").normalize_peak_db, -3.0)
        self.assertIsNone(parse_manifest(self.data).get("b").normalize_peak_db)
        self.assertRejected(lambda d: d["sounds"][0].update(normalize_peak_db=1.0), "normalize_peak_db")
        self.assertRejected(lambda d: d["sounds"][0].update(normalize_peak_db=-40), "normalize_peak_db")
        self.assertRejected(lambda d: d["sounds"][0].update(normalize_peak_db="loud"), "normalize_peak_db")

    def test_rejects_two_entries_producing_the_same_asset(self) -> None:
        self.assertRejected(lambda d: d["sounds"][1].update(name="sfx_a", variations=1), "also produced by")

    def test_rejects_bad_top_level(self) -> None:
        self.assertRejected(lambda d: d.update(version=2), "version")
        self.assertRejected(lambda d: d.update(max_credits_per_run=0), "max_credits_per_run")
        self.assertRejected(lambda d: d.update(sounds=[]), "sounds")
        self.assertRejected(lambda d: d["sounds"][0].update(prompt="  "), "prompt")
        self.assertRejected(lambda d: d["sounds"][0].update(prompt="x" * 451), "at most 450")
        self.data["sounds"][0]["prompt"] = "x" * 450
        parse_manifest(self.data)


class CreditPlanningTests(unittest.TestCase):
    def test_credit_estimate(self) -> None:
        self.assertEqual(estimate_credits(2.5), 100)
        self.assertEqual(estimate_credits(1.5, 2), 120)
        self.assertEqual(estimate_credits(0.51), 21)

    def test_plan_takes_whole_entries_in_order_and_defers_or_refuses_the_rest(self) -> None:
        manifest = parse_manifest(support.small_manifest())
        a, b, c = manifest.sounds  # a: 2 x 80, b: 1 x 40, c: 1 x 400
        selected, deferred, refused, total = plan_run([(a, [1, 2]), (c, [1]), (b, [1])], 300)
        self.assertEqual([r[0].id for r in selected], ["a", "b"])
        self.assertEqual(total, 200)
        self.assertEqual(deferred, [])
        self.assertEqual([r[0].id for r in refused], ["c"])

        selected, deferred, refused, total = plan_run([(c, [1]), (a, [1, 2]), (b, [1])], 450)
        self.assertEqual([r[0].id for r in selected], ["c", "b"])
        self.assertEqual([r[0].id for r in deferred], ["a"])
        self.assertEqual(total, 440)

    def test_plan_skips_entries_with_nothing_missing(self) -> None:
        manifest = parse_manifest(support.small_manifest())
        selected, deferred, refused, total = plan_run([(manifest.sounds[0], [])], 100)
        self.assertEqual((selected, deferred, refused, total), ([], [], [], 0))


if __name__ == "__main__":
    unittest.main()
