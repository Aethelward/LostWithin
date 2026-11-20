# ElevenLabs SFX

Sound effects are generated with the ElevenLabs Sound Effects API from inside the Unreal editor and imported
straight into Content as SoundWaves. The list of sounds lives in Git; the API key never does.

| Piece | Where |
|---|---|
| Sound list (prompts, lengths, takes, folders) | `Tools/SFX/sfx-manifest.json` |
| Tool (editor Python, standard library only) | `Content/Python/lostwithin_sfx/` |
| Menu registration at editor start | `Content/Python/init_unreal.py` |
| Unit tests (run anywhere with Python 3.11+) | `Tools/SFX/tests/` |
| Raw downloads and parameter sidecars (ignored by Git) | `Saved/ElevenLabs/<id>/` |

## One-time setup: the API key

1. At <https://elevenlabs.io/app/developers>, create an API key. Restrict it to **Sound Effects** access, plus
   **User: read** if you want the credit display. Nothing else is needed.
2. On the Windows PC, open the key file in Notepad from PowerShell, paste the key, save and close:

   ```powershell
   New-Item -ItemType Directory -Force "$env:LOCALAPPDATA\LostWithin" | Out-Null
   notepad "$env:LOCALAPPDATA\LostWithin\elevenlabs-api-key.txt"
   ```

   Check it saved (this prints only the length, about 50 characters, not the key):
   `(Get-Item "$env:LOCALAPPDATA\LostWithin\elevenlabs-api-key.txt").Length`.
   On Linux (the Arch workstation), the key file is `~/.config/LostWithin/elevenlabs-api-key.txt`
   (`$XDG_CONFIG_HOME` when set). Create it in an editor, then `chmod 600` it and check it with `wc -c`; never print it.
   A hidden `Read-Host -AsSecureString` prompt is not recommended: Ctrl+V often does not paste into it, which
   silently saves an empty file. The tool reports an empty file as "is empty".

The tool reads `ELEVENLABS_API_KEY` from the environment first, then that file. The file is outside the project
folder, so it is never committed and never visible through the project share. Because it is read on every run,
the editor does not need a restart after you save it. Never paste the key into a chat, the manifest or a config.

## Generating sounds

**Menu:** Tools > LostWithin > **Generate missing SFX (ElevenLabs)**. It lists the takes it will create and the
estimated credits, and only calls the API after you click Yes. **SFX status / credits** shows what exists, what is
missing and your remaining credits. The menu appears after the editor restarts once with these files present.

**Python console or an agent (EngineWire MCP `execute_python`):**

```python
import lostwithin_sfx as sfx
sfx.status()                              # present/missing assets per entry
sfx.generate("monster_reveal", dry_run=True)  # takes and credits, no API call
sfx.generate("monster_reveal")               # generate and import the missing takes
sfx.generate("monster_reveal", force=True)   # regenerate and replace every take
sfx.generate_missing(dry_run=True)        # the whole manifest
sfx.credits()                             # used / limit / remaining
sfx.reload_modules()                      # after editing the .py files
```

Before the first editor restart, `Content/Python` may not be on the Python path yet. Add it once with
`import sys, unreal; sys.path.append(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()) + "Python")`.

Agents generate one entry per call (dry run first). A long batch in one MCP call can time out, and per-entry
calls keep the spend visible.

## How a run works

- Only missing takes are generated (`<folder>/<name>_01`, `_02`, …), so a run can be repeated safely. Delete an
  asset or pass `force=True` to replace a take you dislike.
- Each run is capped by `max_credits_per_run` (3000). Whole entries are taken in manifest order while they fit;
  the rest are listed as deferred, so click the menu again. An entry that alone exceeds the cap is refused.
  Pass `max_credits=` to override the cap for one call.
- Each response is checked as 16-bit PCM WAV and rewritten with a clean header before import. An unusable
  response is kept as `Saved/ElevenLabs/<id>/*.invalid` and nothing is imported.
- Imported SoundWaves get `looping` from the entry and metadata tags `ElevenLabs.Prompt`, `ElevenLabs.Model`,
  `ElevenLabs.Date` and `ElevenLabs.ManifestId`, and are saved immediately.
- A take whose length differs from the request by more than 0.25 s is imported with a warning.
- The editor is busy during each request (a progress dialog with Cancel is shown). Cancel stops after the take in
  progress.

## Manifest fields

```json
{
  "id": "ghoul_idle",                       // lowercase, unique
  "prompt": "Undead ghoul moaning ...",     // describe one sound; end with "close-mic, dry, no music"
  "duration": 2.5,                          // 0.5 to 30 seconds
  "loop": false,                            // true for ambience; also sets the SoundWave to loop
  "variations": 3,                          // takes: VO_Ghoul_Idle_01.._03 (1 to 10)
  "folder": "/Game/NPC/Ghoul/SFX/Voice",
  "name": "VO_Ghoul_Idle",                  // no _NN suffix
  "trim_silence": false,                    // optional: cut leading/trailing silence (not with loop)
  "normalize_peak_db": -3.0,                // optional: scale every take to this peak (-30 to 0 dBFS)
  "notes": "optional, ignored by the tool"
}
```

Short one-shots such as footsteps use `normalize_peak_db` and `trim_silence`: ElevenLabs returns takes anywhere from
-3 to -40 dBFS, and a 0.6 s request often contains a 0.1 s step. Processing order: WAV check, then normalization,
then trim. A take peaking below -50 dBFS is treated as unusable rather than amplified; any unusable response is
kept as `.invalid` and generated once more automatically before the run stops.

`defaults` sets `model_id` (`eleven_text_to_sound_v3`), `output_format` (`wav_48000`), `prompt_influence` (0.4,
higher follows the prompt more literally) and `variations`; any entry can override them.

## Costs and limits (checked 2026-11-18)

- 40 credits per second of requested audio: a 2.5 s take costs 100 credits, the 30 s maze bed 1,200.
- The manifest now has 29 entries (estimate 7,488 credits); the account counter has run far below the estimate
  (1,788 used after the first 22 entries plus part of the revision).
- 44.1 kHz output needs the Pro plan, so the manifest uses 48 kHz. MP3 and Opus are not accepted by the tool.
- `eleven_text_to_sound_v2` is deprecated on 2027-03-01; keep v3 unless an entry needs v2.
- Generation is not repeatable (there is no seed). The sidecar JSON and the asset tags record exactly what was
  sent.

## In the game

- **Monster voices:** `UMonsterVoiceComponent` (`Source/LostWithin/AI/MonsterVoiceComponent.h`), added as
  `MonsterVoice` to Lunger, BP_BoneDog, BP_Doll and BP_Ghoul with their `VO_<Creature>_*` takes. It only watches the
  existing AI: Idle every 8–20 s while roaming; Alert when the hearing component starts investigating (Lunger is
  deaf, so it has none); Panic when MonsterAIComponent starts fleeing a float-spinning cat; Thrown on ragdoll.
  Thrown > Panic > Alert > Idle; a higher one cuts off a lower one; Alert and Panic wait 3 s before repeating. The
  voice is spatialized from the mesh (falls silent around 27 m, muffled with distance). Tune `IdleIntervalRange`,
  `VolumeMultiplier`, `PitchRange` and `Attenuation` on the component. `log LogLostWithin Verbose` shows each vocal.
- **Sudden sightings.** The player's `MonsterRevealSound` component checks screen position and line of sight.
  A monster newly visible within 4.2 m can play one of three `SFX_MonsterReveal` stingers. It primes initial
  visibility, requires 0.75 s of hidden time for a returning monster and uses a 10 s cooldown. Play keeps the
  last sound index so the next valid sighting uses a different take.
- **Death.** Touching a lethal monster stops the live-sighting stinger and exploration playlist. The death
  camera starts `SFX_DeathJumpscare_01` (`Jumpscare Sound`) and the short `SFX_DeathImpact_01` (1.47 s,
  `Sting Sound`) together. YOU DIED plays the separate `SFX_DeathGameOver_01` (3.76 s) as a UI sound through
  pause. Try again starts a fresh run and playlist. The Output Log shows `Monster reveal`, `Death camera: death
  jumpscare`, `Death camera: death impact` and `Death end screen: game over` for these transitions.
- **Monster footsteps:** `UMonsterFootstepComponent` (`MonsterFootsteps` on each monster) plays one of four
  normalized `SFX_<Creature>_Step_NN` takes at the feet every `StrideLength` of ground distance: Lunger 75 cm, BoneDog
  45 cm, Doll 40 cm, Ghoul 70 cm. Faster movement (investigating, fleeing) steps faster; airborne, ragdolled or slower
  than `MinSpeed` (20 cm/s) is silent. Steps fade out by about 20 m. Tune `StrideLength`, `VolumeMultiplier`,
  `PitchRange` and `Attenuation` on the component; `log LogLostWithin Verbose` shows each step.
- **Toolbox:** `AToolboxActor::OpenSound` defaults to `SFX_ToolboxOpen_01` (BP_ToolBox inherits it).
- **Ambience:** five `AmbientSound` actors in Lvl_FirstPerson (outliner folder Audio/Ambience, tag
  `LostWithin.Ambience`, always loaded). `AMB_MazeBed` is a non-spatial bed at volume 0.3; `AMB_Wind` (north edge),
  `AMB_Drip` (east), `AMB_Hum` (centre) and `AMB_Creak` (near Lunger) are spatial: full volume within 3 m, fading
  over 15 m. Move them or change Volume Multiplier / Attenuation Overrides on each actor's Audio Component, then
  save the level (World Partition keeps each actor in its own external package).
- To add new takes for a creature: raise `variations` in the manifest, generate, then add the new assets to that
  Blueprint's `MonsterVoice` arrays.

## Tests

```bash
cd Tools/SFX/tests
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s . -p 'test_*.py'
```

They cover manifest validation (including the real manifest), credit planning, WAV handling, the HTTP client
(retries, errors, key redaction) and a full run with fake editor and API backends. No network or editor needed.
