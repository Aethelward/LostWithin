# Subject 09 story and UI

This integrates `New Lost Within.md` with the existing maze. The approved gameplay differences are deliberate:
all four current monster behaviors and the cat sanctuary remain; all 11 toolboxes keep their locks and loot.
The three code clues and two existing lore notes carry the five authored story entries. They still appear
at randomized note points or inside toolboxes, and collected notes can be read and dropped as before.

## Controls

| Input | Action |
| --- | --- |
| W A S D / mouse | Move / look |
| Space | Jump |
| Left Ctrl / Left Shift | Sprint / walk |
| E | Interact, read, search, collect, pet; put away a world-read note (the left mouse button does not interact) |
| F / R | Toggle flashlight / replace a battery from inventory |
| Tab | Inventory; right-click an item for Read / Use / Drop |
| Esc | Close note/inventory first, otherwise pause |
| E on the exit, then mouse wheel | Skull padlock dial view: point at a dial and turn it; E steps back |

Notes, inventory and the padlock view leave the world running. Pause provides Resume, Option, Restart and Main
Menu. Options returns to its originating menu. Display changes need confirmation within 15 seconds; audio,
quality and confirmed video choices persist in GameUserSettings.

## Narrative

`Gameplay/LostWithinStory` holds the orientation and five authored definitions. Story identity and explicit
clue metadata travel with `FInventoryItem`; unrelated numbers in lore never enter the old clue validator.
The narrative component tracks actual reads, first-close reactions, objectives, control hints and battery
warnings. Finding the last clue first never claims all digits have been found. A correct code is sufficient
to escape, even without every note. Restart/travel constructs fresh narrative state.

## Presentation and body contact

The existing hidden Manny mesh evaluates locomotion, flashlight hold, pickup and aim. Evann Body retargets
that final pose; the face and clothing copy Body. The held light attaches to Body's right hand.

Dedicated `PA_BodyContact_*` assets under `/Game/Gameplay/Subject09/Contact` contain conservative fitted body
volumes. They are separate from the meshes' existing ragdoll assets. Runtime contact primitives follow
animated bones after pose evaluation, including off-screen, and sweep between frames. Only an unobstructed
player/monster body contact in an active run starts death. Navigation capsule overlap, a gap, a wall/closed
door and a ragdolled monster cannot kill. `TouchTolerance` is retained for serialization compatibility only.

The player's profile also carries the 11 fitted torso, shoulder and thigh capsules of Evann's own `PHYS_evann`
(copied into `PA_BodyContact_Subject09`, 46 bodies); the generated profile alone left the pelvis and chest
uncovered. Each shape is swept from its previous pose, except after a one-frame owner move beyond
`MaxSweepDistance` (300 cm): a teleport, respawn or recovery move restarts from the new pose, so its path is
never treated as contact.

Monsters approaching the player close in to contact: hearing and vibration investigations walk onto the
source (25 cm acceptance to the monster's centre, no stop-on-overlap), and the Doll stalk and Ghoul dark stalk move
to the player with a 5 cm acceptance. They used to stop 90-120 cm plus a capsule in front, so bodies never met.

PlayerMovement and BodyContact are dedicated object channels. The player/monster navigation capsules overlap
each other while retaining environment collision; pickups and ragdolls retain their original pawn-ignore
behavior. Existing monster perception, roaming, animation selection, special reactions and cat logic are
unchanged.

## Explicit editor setup

After a full Windows build and editor reopen, open `/Game/FirstPerson/Lvl_FirstPerson`, with Play stopped:

```python
unreal.LostWithinSubject09EditorLibrary.setup_subject09()
```

This creates the two IK rigs, retargeter and five contact profiles only when missing. It preserves existing
profile tuning and original ragdoll assets, adds one always-loaded orientation spawn point on the starting
table, and places the existing flashlight beside it. All writes use engine APIs and scoped package saves.
Repeat setup is safe. A running Play session is rejected without stopping it.

Compile-only does not load native changes. Build/reopen uses `BuildCheck.ps1` and the one-shot
`Tools/Subject09/build_and_reopen.py`; each editor close needs the permission required by the current session.

## Verification

Run `Automation RunTests LostWithin` on the rebuilt editor. Focused new tests are under
`LostWithin.Gameplay.Subject09`; `Gameplay.TouchDeath` covers visible/swept contact, walls, distance and
ragdoll immunity. The existing note test checks metadata across an actual pickup/drop, and FlashlightAndAim
checks the visible MetaHuman and attachment alongside the original aim/pose assertions.

Complete an owned Play pass through the menu, starting table, note Read/drop/recollection, wrong/correct
padlock dials, contact death/retry, escape/replay and Main Menu. Verify video changes and automatic revert in a
standalone Windows game. Record actual results in the ecosystem task's VERIFY.md; saved-package reload is
required before considering asset integration complete.

## Exit door

The exit uses the skull padlock's own dial view, as before Subject 09 (user decision 2026-11-19): E on the
locked door opens it, the dials take the run's three digits, matching dials unlock the door and E opens it. The
Subject 09 escape subtitle and outcome screen follow after `EscapeDelay` (1.6 s). The three-digit keypad remains in
source behind `UEscapeDoorComponent::bUseKeypadUI` (off).
