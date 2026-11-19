# Monster movement and active-cat reaction

Normal movement needs built navigation. The movement repair covers the maze's
collidable static geometry with a real NavMeshBoundsVolume, adds 200 cm of
padding, matches the navigation agent to the monster capsule, then builds and
saves navigation in `/Game/FirstPerson/Lvl_FirstPerson`.

All four roaming monsters react: `Lunger`, `BP_BoneDog`, `BP_Doll` and
`BP_Ghoul`. They react whenever a cat's **float-spin sequence is running**: any
spin window, the pauses between windows, and an active sequence after its last
window. Idle cats, disabled float/spin components and despawned cats cause no
reaction. (Until 2026-11-18 only spin #3 counted.)

How the player starts the cat's float-spin (chaos mode), since 2026-11-19: pet the cat ([E]), turn the view
one full circle within 5 m of it, and repeat three times (pet → spin → pet → spin → pet → spin). Turning back
cancels a partial turn, and petting twice without spinning starts the ritual over. The prompt shows no chaos
progress; it shows only the Fairy Tale Kitty Key count (5 pets), and a key still held drops when chaos starts.
Tune it on the `ProximityFloatSpinComponent` with `Require Pet Spin Sequence`, `Pet Spin Rounds`,
`Player Spin Degrees` and `Player Spin Radius`. Turn the ritual off to go back to the old
`Spin Trigger Interaction Threshold` pet count.

A monster panic-flees from the nearest eligible cat within 500 cm of its actor
center. While fleeing it plays its fastest gait clip with both its speed and its
animation rate multiplied by 1.5, so the feet stay matched to the ground; normal
speed and animation rate return when the scare ends. A cat within **300 cm** of
the monster's capsule surface takes priority and causes one outward/upward throw
into ragdoll. This includes a hovering cat whose actor center is farther away.
After 3 seconds the monster tries to stand at the landed hip position, retrying
every 0.25 seconds while a ceiling or obstacle blocks the capsule. A monster
never stays down: if it still cannot stand 2 seconds later (no walkable floor,
blocked, or fallen away), it stands on the nearest navigation within 3 m of the
body, or otherwise returns to where the throw started.

Both ranges use 3D distance; escape and throw directions use the horizontal
plane. Throw distance accounts for capsule rotation and scale. With multiple
eligible cats, the closest by capsule distance triggers the throw before any
center-distance flee candidate. Lighting visibility does not control gameplay;
the reaction uses the same evaluated spin-window timing as the disco stage.

When the sequence ends or the cat leaves range, fleeing stops and the roaming AI
is allowed again. An existing ragdoll completes recovery even if the sequence
ends. Another throw is possible after recovery while an active cat remains in
range.

## Build and apply the asset changes

For VS Code, open the folder containing `LostWithin.uproject` in a **local
Windows window**. Save your work and close Unreal, then press **Ctrl+Shift+B**
to run **Build LostWithinEditor (close Unreal first)**. The task uses the
existing Windows C++ toolchain; the Visual Studio application is not required.
A Remote-SSH window on the source server cannot run this Windows build. Keep
that SSH connection open while using the separate local window so the MCP
tunnel remains available when Unreal reopens.

The task calls `BuildCheck.ps1`, discovers the project's registered engine,
shows compiler output in the terminal and returns a failed task if the build
fails. If engine discovery fails, run the script in a Windows PowerShell
terminal with `-EngineRoot` pointing to your Unreal installation folder (the
folder containing `Engine`), or set `UE_ENGINE_ROOT` before opening VS Code.
The original positional project-path argument is still supported.

Wait for **Build succeeded**, then reopen the project and continue at step 2.
This builds and links the editor modules; it does not run the asset setup or
tests automatically.

1. Close Unreal Editor and build the **LostWithinEditor** target with **Unreal
   Engine 5.6**. This change adds reflected members and an editor commandlet;
   use a full editor build rather than relying on an existing Live Coding session.
2. Open `/Game/FirstPerson/Lvl_FirstPerson` and stop Play In Editor.
3. In **Tools → Debug → Output Log**, run:

    ```text
    LostWithin.SetupMonsterCatReaction
    ```

4. Check the log for `Monster movement setup saved`, one `Saved <Blueprint>
movement guards and cat reaction` line for each of the four monsters, and
   `Cat reaction configured for 4 monsters`. Press **P** in the level viewport:
   navigation should cover the walkable maze floor under the monster. Setup
   reports failure if projection or reachable destinations fail near a placed
   monster or player start. If neither exists, it checks sampled floor geometry.
5. Open each monster Blueprint and verify that it contains one
   `MonsterAIComponent` (named `CatReactionAI` if newly added) with its Panic
   Gait Speed set. The physics asset overrides are
   `/Game/NPC/Lunger/PA_MonsterCatRagdoll`, `/Game/NPC/Doll/PA_DollCatRagdoll` and
   `/Game/NPC/BoneDog/PA_BoneDogCatRagdoll`; `BP_Ghoul` keeps its imported
   16-body physics asset.

The setup saves the working version of each monster Blueprint, the generated
physics variants, the target level and that level's dirty external actor packages. It also persists
the supported navigation agents in `Config/DefaultEngine.ini`. Other maps are
not switched or saved during interactive use. Save any edits you want to keep
in the target level before starting.

It enables placed/spawned AI possession, actor/movement ticking, walking and
capsule collision, while retaining valid authored speeds and AIController
subclasses. The capsule and mesh do not carve navigation holes. Existing
roaming and delay chains remain in each Blueprint. All four monsters are
roam-only and keep all of their animation clips.
`RoamStep` starts from BeginPlay if it was disconnected. Unchecked reachable
queries become a single evaluated destination/success pair; a failed query
skips movement and continues the original delayed retry.

The cat guards remain before normal movement/speed commands. Their false
branches continue after skipped commands, keeping delayed roaming scheduled
during the reaction. For the Monster, Doll and BoneDog a separate articulated
physics asset is generated from the existing skeleton; their imported assets had
only 3-4 undersized bodies.

The original `base_basic_pbr_PhysicsAsset` and skeletal mesh assets are preserved.
The human variants (Monster, Doll) have bodies for hips, spine, neck, head,
shoulders and legs; the BoneDog variant has hips, spine, neck, head and all four
legs. Bodies are connected by limited joints. Shapes account for imported reference-bone scale
and use continuous collision detection for fast throws. Self-collision is disabled;
collision with the world stays enabled. Inspect it in Physics Asset Editor and use **Simulate** to check
the fit before gameplay tuning.

Re-running setup reuses the existing component, guards, covering bounds volume,
matching agent and generated asset, preserving subsequent physics tuning.
Duplicate reaction components, unsupported query wiring, a conflicting physics
asset, unusable navigation, and failed saves report errors. Resolve the first
reported error and rerun setup before starting PIE. A navigation failure after
the Blueprint save is explicitly reported as incomplete setup.

The same setup is available unattended after building the editor target:

```text
UnrealEditor-Cmd "LostWithin.uproject" -run=SetupMonsterCatReaction -unattended -nop4 -NullRHI -nosound
```

The commandlet loads `Lvl_FirstPerson` itself. Run it from the project directory, with the UE5.6 executable available
on PATH. Close the graphical editor before using the commandlet. It returns a
nonzero exit code on setup failure.

## Tuning

Select the `CatReactionAI` component in a monster Blueprint to adjust:

| Property                | Default              | Purpose                                                                                      |
| ----------------------- | -------------------- | -------------------------------------------------------------------------------------------- |
| Cat Flee Radius         | 500 cm               | Fear range from an active float-spin cat, measured between actor centers                     |
| Cat Ragdoll Radius      | 300 cm               | Throw range from an active cat to the capsule surface                                        |
| Flee Distance           | 500 cm               | Navigation destination distance away from the cat                                            |
| Panic Gait Speed        | per monster          | Natural speed of the fastest gait clip: Monster 62.6, Doll 157.3, BoneDog and Ghoul 350 cm/s |
| Panic Speed Multiplier  | 1.5                  | Multiplies both MaxWalkSpeed and the animation rate while fleeing                            |
| Ragdoll Duration        | 3 s                  | First attempt to stand up                                                                    |
| Recovery Fallback Delay | 2 s                  | Extra retry time before standing on nearby navigation or the throw origin                    |
| Ragdoll Launch Strength | 1,200 cm/s           | Horizontal velocity change away from the cat                                                 |
| Ragdoll Upward Strength | 600 cm/s             | Upward velocity change                                                                       |
| Ragdoll Recovery Bone   | Hips (Ghoul: pelvis) | Simulated body used to locate recovery                                                       |

Setup fills Panic Gait Speed only while it is unset, so tuned values survive a
rerun. Each Animation Blueprint shows the monster's fastest gait (`PanicState`
on the Blueprint) while fleeing and moving, and its rest idle while fleeing but
blocked. With the 300 cm throw range, the flee-only band is narrow (about
1.6 m); raise Cat Flee Radius if monsters should run more and be thrown less.

Escape paths refresh every 0.25 seconds. If no navigation path exists, the
monster uses character movement away from the cat, respecting world collision,
and logs a setup warning. It needs an AIController; setup enables possession for
placed and spawned instances. For escape around obstacles, ensure the level has
usable navigation data.

## Verification

Run the focused Unreal automation tests from the editor Output Log:

```text
Automation RunTests LostWithin.AI.MonsterCatReaction
Automation RunTests LostWithin.AI.MonsterMovement.PIE
```

Or run headless from the project directory:

```text
UnrealEditor-Cmd "LostWithin.uproject" -ExecCmds="Automation RunTests LostWithin.AI.MonsterCatReaction" -TestExit="Automation Test Queue Empty" -unattended -nop4 -NullRHI -nosound
```

The tests cover fear in every phase of an active float-spin sequence (before the
first window, windows, gaps, after the last window, unwindowed), rejection of
idle, disabled and out-of-range cats, the 300 cm capsule-surface throw boundary,
hovering, rotated/scaled capsules, nearest eligible selection, capsule-distance
throw priority, destroyed cats, one throw per ragdoll, blocked standing, landed
relocation, the 2 s stand-up fallback to the throw origin, panic speed and
animation rate applied and restored, restoration of custom settings, repeated
reactions, coincident positions and timer cleanup on destruction. The asset
regression checks articulated body/joint geometry, collider bounds after bone
scaling, continuous collision detection and preservation of the original mesh
physics asset for the Monster, Doll and BoneDog ragdolls. The throw tests load
the existing monster skeletal mesh and its original physics asset, so they also
run before the Blueprint setup. The GeneratedAssetGeometry test requires setup
to have created and assigned the articulated variants.

Run `LostWithin.AI.MonsterMovement.PIE` in the graphical editor with
`Lvl_FirstPerson` open after setup. It starts and ends its own PIE session,
spawns the production Blueprint on reachable floor near the player, and checks
AI possession, a single reaction component, enabled ticks, at least 50 cm of
horizontal displacement and nonzero horizontal velocity within 20 game seconds.
It observes a loaded placed monster too when one exists. It does not inject
movement input or manually tick the AI. Nearby cats must remain inactive.

Verify these scenarios in PIE after applying setup:

0. Keep cats inactive. A placed monster must roam and play its normal random
   idles/one-shots. Its current design has no hearing or chase. Repeat with
   a dynamically spawned monster. Check a distant maze section after World
   Partition streams it in. Save/reopen the map and repeat the movement check.
1. An idle cat within 3 m of the capsule causes no reaction. Trigger the cat
   sequence: from the first moment it runs (windows and pauses alike), each
   monster within 5 m panic-flees with its fastest gait, faster than normal and
   without visible foot sliding.
2. With two eligible cats nearby, the monster moves away from the closer one.
   An idle cat must not hide that threat. A cat in capsule throw range must
   preempt a closer center-distance flee candidate.
3. Bring the active cat within 3 m of each monster's capsule, including while
   hovering above it. The monster must visibly fly away and upward with
   articulated body movement and floor/wall collision. It must not receive
   additional throws while down.
4. After 3 s, the monster stands near where the body landed, reattaches the mesh
   correctly, and resumes movement. A low ceiling defers standing; if it cannot
   stand within a further 2 s it stands on nearby navigation or where the throw
   started. Repeat throws many times: every monster always gets back up.
5. Move the cat out of range, let its sequence end, disable its float/spin
   component, or destroy the cat. Fleeing stops, normal speed and animation rate
   return, and normal movement is allowed after any pending recovery.
6. Let delayed roaming run through a reaction and recovery. It must continue
   afterward. Repeat the throw and destroy a ragdolled monster; no stale timer
   callback or missing-body errors should appear.
7. Run setup a second time. Check that no duplicate AI components, movement
   guards, bounds volumes, navigation profiles or regenerated physics bodies
   appear. Check that navigation stays available after reopening the level.
8. To verify retry behaviour, temporarily disable floor navigation in a test
   copy of the level. Failed reachable-point queries must skip the move and
   still reach the original Delay/RoamStep continuation. Restore/build navigation
   and check that the same monster resumes roaming without another BeginPlay.

The record below verified the earlier spin #3-only, 1 m, BP_Monster-only behaviour.
Results for the all-monster panic behaviour are recorded in the
`cat-panic-all-monsters` task's VERIFY.md.

Verified on 2026-11-18: the full UE5.6 Win64 editor build and asset setup completed.
All six cat-reaction automation cases and the production Blueprint movement
PIE test passed. FirstPerson was saved and reopened, and repeated setup retained
one reaction component, one bounds volume, the 40/190 navigation agent and the
same physics and animation assets.

Two production PIE sessions confirmed idle/earlier-spin rejection, spin #3
fleeing, an outward/upward articulated throw with the cat 115 cm from the actor
center but 34 cm from its capsule, floor collision, and recovery after 3.01 s.
The second session also observed normal delayed roaming resume at 62.6 cm/s.
The walk animation follows actual speed during fleeing even when a delayed
normal-AI branch changes bRoaming. All five original animation choices remain.

These observations used temporary PIE-only fixture settings to end spin #3
before recovery. Saved cat windows remain 0.27–1.77, 3.12–5.10 and 6.06–42.89,
with disco index 2 and the original directed path enabled. Additional distant
streaming and navigation-removal scenarios above are manual checks for future
level edits, not claimed as executed. See the task VERIFY.md for full results.
