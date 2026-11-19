# Player, monsters and interactables

How a run works, what to place in a level, and what to tune. Source: `Source/LostWithin/` (classes named below).

## A run

When Play starts, `ALostWithinGameMode` (via `BP_FirstPersonGameMode`) sets up a random run that can always be won:

1. Picks a three-digit door combination (each digit 0-9, never 0-0-0).
2. Places one **Key**, one **Old Key** and one **Royal Key**, each at a different random key spawn point. The
   **Fairy Tale Kitty Key** is never placed; only the cat drops it.
3. Locks some toolboxes. Each locked toolbox needs one specific key, and no key opens two toolboxes, so the number of
   locked toolboxes is at most the number of keys you can actually get (the kitty key counts only when a cat is in the
   level).
4. Writes three hint notes, one per dial. Each contains its digit as a word and names its dial: *"The second lock
   remembers seven."* Hints go to note spawn points or inside toolboxes.
5. Puts flavour notes (no numbers) on the remaining note spawn points, and gives every other toolbox a battery, a
   flavour note or nothing.

The Output Log line `Run setup: combination ...` shows what was placed (useful for testing; players never see it).

## What to place in the level

| Actor | How |
|---|---|
| `ItemSpawnPoint` (Kind **Key**) | At least 3, a little above the floor where a key may appear. Gold arrow. |
| `ItemSpawnPoint` (Kind **Note**) | At least 3 (better 6+), where a note may lie. The note is laid flat on the floor below. Pale arrow. |
| `BP_ToolBox` (`/Game/Level/Objects/tool_box/`) | On the floor (its origin is the bottom centre). The lid hinges on the -Y side; leave room behind it for the open lid. |
| `BP_Paper` (`/Game/Item/paper/`) | Optional hand-placed notes; they get a random flavour phrase. |
| `BP_ScaryDoor` | The exit. Its skull padlock (`BP_PadlockBase-3X`) is a child actor of the door, on the padlock holder. |

All of these stay loaded with World Partition, so the run setup always finds them.

## Toolboxes

- **Lock Mode**: Random (default), Always Locked, Never Locked. **Fixed Key ID** (`Key`, `OldKey`, `RoyalKey`,
  `FairyTaleKittyKey`) pins a toolbox to one key when that key is obtainable.
- [E] on a locked toolbox: with the right key it unlocks (the key is used up) and opens; without it, it rattles
  and says which key it needs.
- Opening plays the one-second `tool_box_Open` clip and holds the lid open. The contents are placed inside the
  still-closed box as the lid starts to move, so the lid reveals them. They lie centred on the inner floor with
  their long side along the box, and a note lies flat. An empty toolbox says so after `Contents Delay`
  (1.05 seconds). The imported two-second open/close clip is preserved.
- Collision follows the visible box. A floor, four walls up to the rim (20.4 cm) and a lid cap block; the cavity
  (about 56.5 x 26 x 20 cm) is free. The lid cap (`LidCollision`) is attached to the lid bone `Cover_Box`, so it
  swings open with the lid and leaves no invisible barrier over the open box. The latches, hinges and lid handle
  are decorative and have no collision. The sizes come from the mesh fitted to 60 cm and scale with
  `Target Width Cm`.

## Notes

`BP_Paper` is a `PickupItemActor` with the **Paper Note** preset. Each note has its own item ID, so notes never stack.
Picking one up shows its text; [E] puts it away. In the inventory, right-click a note and choose **Read**.

## The ScaryDoor

- [E] on the locked door (or its padlock) opens the padlock view: point at a dial, turn it with the mouse wheel,
  [E] steps back. Player movement and looking are paused while the padlock view is active.
- When the dials match, the padlock opens and unlocks the door. [E] on the door opens it, and the run ends with
  **You escaped**.

## Monsters, noise and death

- Touching any monster (Lunger, BP_BoneDog, BP_Doll, BP_Ghoul) ends the run: the camera turns to the monster's
  face, stares, collapses and fades out, then **You died** offers Try again (new random run) or Quit. A monster
  thrown into ragdoll by the cat cannot kill.
- Footsteps make noise: walk ([Shift]) 4 m, run 9 m, sprint ([Ctrl]) 16 m. A dropped or thrown item hitting the floor
  makes a 30 m noise. Paper is silent.
- **BP_BoneDog, BP_Doll and BP_Ghoul** have a `MonsterHearingComponent`: they hurry to the noise at their fastest
  gait, search there for about 5 s, then roam again. A newer noise redirects them. The cat still wins: a monster near
  the spinning cat ignores noise. Lunger is deaf.
- **Light and monsters:** a lit flashlight draws the **BoneDog** when it is in the beam or within 15 m with a
  clear view of the lens. It runs at the player with its fastest gait, and stops the moment the light goes off or
  it loses sight of it. Light outranks hearing; the cat outranks both.
- **Wall torches** count as light within 400 cm of a torch's light, with a clear line (`ULostWithinLightSubsystem`,
  `TorchLitRadius`; lights named `TorchPointLight*`). That is about a fifth of the maze floor.
- The **Ghoul** chases the player in the dark. When the beam or a torch lights it, it shields its face and retreats
  (away from the torch), and when the light leaves it, it chases again (no flinch or recovery pause by default).
  It never steps into a torch's light: the chase stops at the edge and waits.
- The **Doll** freezes only while the player watches it and it is lit (beam or torch). Watched in the dark, or lit
  but not watched, it keeps walking toward the player (`bFreezeOnlyWhenLitAndWatched`; false restores the old
  freeze-when-watched rule).
- The **BoneDog** cannot pinpoint sounds: it heads for a point 40-100% of its pinpoint error away from the source,
  200 cm when it stands in light and 500 cm in the dark (BP_BoneDog `Hearing`; 0 = exact, the default for the others).
- While a monster chases or investigates, the roam loop waits and the Animation Blueprint plays the panic (fastest)
  gait.
- Crouch is gone ([C] does nothing).

## Tuning

| Where | Property |
|---|---|
| `BP_FirstPersonCharacter` | Noise: Walk/Run/Sprint Noise Radius |
| `PickupItemActor` (or a pickup Blueprint) | Pickup / Noise: Impact Noise Radius |
| `MonsterHearingComponent` on each NPC | Hearing Range Multiplier, Hear Footsteps / Item Impacts, Investigate Speed, Search Duration |
| `MonsterLightAttractionComponent` (added to BoneDogs at run start) | Attraction Radius (1500 cm), Chase Speed (0 = Panic Gait Speed), Repath Interval |
| `MonsterLightAverseComponent` on BP_Ghoul | Startled Duration / Recovery Duration (0 = same frame), Retreat Gait Speed, Dark Stalk Approach Speed, Torch Light Repels, Torch Edge Look Ahead |
| `MonsterSightFreezeComponent` on BP_Doll | Freeze Only When Lit And Watched, Torch Light Counts, Illuminated Max Distance, Stalk Walk Speed |
| `MonsterHearingComponent` | Pinpoint Error In Light / In Dark (BoneDog 200 / 500 cm) |
| `ULostWithinLightSubsystem` (code default) | Torch Lit Radius (400 cm), Torch Light Name Prefix |
| `BP_FirstPersonGameMode` | Run: Run Seed (0 = random), Spawnable Keys, Toolbox Lock Chance, loot weights, Flavor Note Count, Hint Note In Toolbox Chance, Hint Templates (`{number}`, `{ordinal}`), Flavor Phrases |
| `PlayerDeathComponent` on the character | Touch Tolerance |
| `DeathCameraActor` | phase durations, push-in, shake, collapse |

## Verify

- Automation: `Automation RunTests LostWithin.Gameplay` (run rules, run setup, noise/hearing, toolbox lock, paper
  note, touch death, escape door, light-item floor-impact hearing).
- Play: walk vs. sprint near the dog; drop a battery far away; read three hint notes and enter the digits; open the
  door; touch a monster.

## Starter set and editor verification (2026-11-18)

FirstPerson contains three key points, six note points and six toolboxes under
`Gameplay/RunSetup`, tagged `LostWithin.RunSetupStarter`. They are always loaded,
on reachable maze floor and separate from the small PlayerStart navmesh island.
Move these starter actors to your preferred locations in the editor. Save their
external packages together with the level.

BP_Paper uses the PaperNote preset and the paper mesh has Nanite disabled for its
translucent material. Dropped notes lie face up at roll 90 degrees with physics
off. The ScaryDoor has the base three-dial skull padlock on its holder; the child
compensates the holder scale, and the placed door has a matching fitted override.
One Hearing and one canonical CatReactionAI component are retained per hearing
NPC; Lunger has no Hearing component.

All 18 requested native checks pass on the loaded build. Live checks verified
key consumption, note text/drop preservation and inventory Read, C not crouching,
held-open toolbox lids, hint-driven padlock unlock, escape/Play again and actual
contact death/Try again. Real sprint footsteps and default battery floor impacts
draw all three hearing NPCs; Lunger stays deaf. Simulated pickups use CCD and
a mass-normalized impact threshold so the tiny default battery lands and is heard.
Detailed results and input-automation limits are in the task's VERIFY.md.
