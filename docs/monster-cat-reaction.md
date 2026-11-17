# Monster movement and active-cat reaction

Normal movement needs built navigation. The movement repair covers the maze's
collidable static geometry with a real NavMeshBoundsVolume, adds 200 cm of
padding, matches the navigation agent to the monster capsule, then builds and
saves navigation in `/Game/FirstPerson/Lvl_FirstPerson`.

The monster flees from the nearest active float/spin cat within 500 cm. Within
100 cm it receives one outward/upward throw into ragdoll. After 3 seconds it
tries to stand at the landed hip position. If a ceiling or obstacle blocks the
capsule, it stays ragdolled and retries every 0.25 seconds.

Fear covers the complete float/spin sequence, including pauses between spin
windows. Inactive cats are ignored. Distances use actor origins in 3D; escape
and throw directions use the horizontal plane. After recovery, the monster can
react again if an active cat approaches.

## Build and apply the asset changes

1. Close Unreal Editor and build the **LostWithinEditor** target with **Unreal
   Engine 5.6**. This change adds reflected members and an editor commandlet;
   use a full editor build rather than relying on an existing Live Coding session.
2. Open `/Game/FirstPerson/Lvl_FirstPerson` and stop Play In Editor.
3. In **Tools → Debug → Output Log**, run:

   ```text
   LostWithin.SetupMonsterCatReaction
   ```

4. Check the log for `Monster movement setup saved` followed by
   `Saved BP_Monster movement and cat reaction`. Press **P** in the level viewport:
   navigation should cover the walkable maze floor under the monster. Setup
   reports failure if projection or reachable destinations fail near a placed
   monster or player start. If neither exists, it checks sampled floor geometry.
5. Open `BP_Monster` and verify
   that it contains a `MonsterAIComponent` (named `CatReactionAI` if newly added).
   Its character mesh must use `/Game/NPC/Monster/PA_MonsterCatRagdoll` as its physics
   asset override.

The setup saves the working version of `BP_Monster`, its physics variant, the
target level and that level's dirty external actor packages. It also persists
the supported navigation agents in `Config/DefaultEngine.ini`. Other maps are
not switched or saved during interactive use. Save any edits you want to keep
in the target level before starting.

It enables placed/spawned AI possession, actor/movement ticking, walking and
capsule collision, while retaining valid authored speeds and AIController
subclasses. The capsule and mesh do not carve navigation holes. Existing
roaming, acoustic sensing, chase and delay chains remain in the Blueprint.
`RoamStep` starts from BeginPlay if it was disconnected. Unchecked reachable
queries become a single evaluated destination/success pair; a failed query
skips movement and continues the original delayed retry.

The cat guards remain before normal movement/speed commands. Their false
branches continue after skipped commands, keeping delayed roaming scheduled
during the reaction. A separate articulated physics asset is generated from
the existing skeleton.

The original `base_basic_pbr_PhysicsAsset` and skeletal mesh asset are preserved.
The generated variant has bodies for hips, spine, neck, head, shoulders and legs,
connected by limited joints. Self-collision is disabled; collision with the world
stays enabled. Inspect it in Physics Asset Editor and use **Simulate** to check
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

Select the component in `BP_Monster` to adjust:

| Property | Default | Purpose |
|---|---|---|
| Cat Flee Radius | 500 cm | Active-cat fear range |
| Cat Ragdoll Radius | 100 cm | Close-range throw threshold |
| Flee Distance | 500 cm | Navigation destination distance away from the cat |
| Ragdoll Duration | 3 s | First attempt to stand up |
| Ragdoll Launch Strength | 1,200 cm/s | Horizontal velocity change away from the cat |
| Ragdoll Upward Strength | 600 cm/s | Upward velocity change |
| Ragdoll Recovery Bone | Hips | Simulated body used to locate recovery |

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

The tests cover active/inactive selection, range boundaries, nearest-cat choice,
spin gaps, destroyed cats, movement priority, one throw per ragdoll, blocked
recovery, landed relocation, restoration of custom settings, repeated reactions,
coincident positions, and timer cleanup on destruction. The throw test loads
the existing monster skeletal mesh and its original physics asset, so it also
runs before the Blueprint setup. It does not replace editor testing of the new
articulated asset.

Run `LostWithin.AI.MonsterMovement.PIE` in the graphical editor with
`Lvl_FirstPerson` open after setup. It starts and ends its own PIE session,
spawns the production Blueprint on reachable floor near the player, and checks
AI possession, a single reaction component, enabled ticks, at least 50 cm of
horizontal displacement and nonzero horizontal velocity within 20 game seconds.
It observes a loaded placed monster too when one exists. It does not inject
movement input or manually tick the AI. Nearby cats must remain inactive.

Verify these scenarios in PIE after applying setup:

0. Keep cats inactive. A placed monster must roam, react to player movement/noise,
   chase, then return to roaming after the original silence delay. Repeat with
   a dynamically spawned monster. Check a distant maze section after World
   Partition streams it in. Save/reopen the map and repeat the movement check.
1. An inactive cat within 1 m causes no reaction. Trigger the cat sequence and
   approach a monster: fear starts within 5 m and persists during spin pauses.
2. With two active cats nearby, the monster moves away from the closer one.
   Its normal roaming/chasing must not steer it back toward the cat.
3. Bring the active cat within 1 m, including while floating. The monster must
   visibly fly away and upward with articulated body movement and floor/wall
   collision. It must not receive additional throws while down.
4. After 3 s, the monster stands near where the body landed, reattaches the mesh
   correctly, and resumes movement. A low ceiling defers standing until clear.
5. Move the cat out of range, deactivate the effect, or destroy the cat. Normal
   movement resumes after recovery. A nearby inactive cat must not retain fear.
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

On 2026-11-18, the connected editor applied and saved navigation in FirstPerson,
including the World Partition bounds package and a 40 cm radius / 190 cm height
agent. After reopening the map, a 20-second PIE observation measured 297.66 cm
of movement for the placed monster and 304.22 cm for a spawned copy, both at a
peak walking speed of 120 cm/s with AIControllers. The temporary copy was
discarded when PIE stopped.

The new source compile actions passed after UE5.6 API corrections. A full DLL
link remains blocked while Unreal holds the module open; no completed Live
Coding patch was observed. Close the editor and fully build before applying the
new native cat component, guards and articulated physics setup. The focused
automation and cat/ragdoll verification remain pending.
