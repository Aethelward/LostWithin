# Maze instances and Lunger

`/Game/FirstPerson/Lvl_FirstPerson` uses one reusable mesh per structural type:

| Mesh (under `/Game/Level/MAZE`) | Visible instances | Spatial groups |
| --- | ---: | ---: |
| `SM_Wall` | 235 | 24 |
| `SM_Floor` | 139 | 18 |
| `SM_DoorFrame` | 26 | 13 |
| `SM_Roof` | 138 | 17 |

The Outliner groups live under `Geometry/Instances/`. Each native Actor owns a Hierarchical Instanced
Static Mesh component as its root. Approximately 20 m spatial groups retain useful culling and streaming.
All placements use the shared assets; the entire maze was not baked into one unique mesh.

`SM_DoorFrame` is an assembled U-shaped frame: 480 cm wide, 24 cm deep, 360 cm high, with its pivot at
opening base center. The clear opening is 120 cm wide and 264 cm high. Its collision consists of exactly
three boxes, one for each post and the header. There is no collision hull spanning the opening. The unique
interactive ScaryDoor frame/leaf/padlock remain part of their gameplay Blueprint and retain their own assets.

Walls and frames use `Horror_wall`; floors use `Horror_floor`; roof tiles use `Horror_ceiling`. Source UVs
and winding were verified after OBJ import of the assembled frame. The wall and roof meshes' third UV
channel marks their caps. Per-instance custom float 0 preserves alternate cap UV orientation: 38 wall flags,
136 roof flags. Their materials rotate cap UVs and correct tangent normals only for flagged instances.
Frame UVs are baked from the source pieces and their instance flags remain zero. Preserve UV markers,
custom data and material graphs when editing these assets.

Each wall group also has a hidden, non-rendering HISM child using the same `SM_Wall` asset. Its 235
collision instances match the snapped walls' axis-aligned bounds; visible walls have collision disabled.
Floor/frame/roof roots supply collision directly. Physical components use BlockAll, QueryAndPhysics and
navigation relevance. When editing a wall instance, update its corresponding collision instance too.
Enable navigation updates when replacing physical instances. Rebuild and save Recast, then check navigation
after reloading the map.

The end-to-end pass fits shared edge coordinates to the surveyed layout. It aligns 167 floor seams,
166 roof seams, 86 straight wall ends and 145 corners or junctions. At a corner, one run fills the
junction and the others stop at its face. The floor top is Z −45.772 cm. Wall and frame bases meet it,
and their tops meet the common roof underside at Z 314.228 cm. The covered rooms and the open-air exit
landing keep the same topology. Door apertures remain 120 × 264 cm. The saved geometry audit found no
intersecting structural volumes and a maximum seam residual below 0.001 cm. One further redundant wall
instance and its collision partner were removed. The 34 torches mount with their local +Y rear plate
against the wall and their cup facing the room. The point sources sit inside the rooms and cast dynamic
and volumetric shadows. Their range is 900 cm. A controlled Play render verified occlusion across a solid wall.

The 156 original doorway parts included 77 exactly overlapping pairs and one header pair offset by
1.415 cm. They were replaced by 26 complete frames. The combined roof's 136 cuboids and two separate tiles
became 138 instances. The 159 replaced actors and their 158 unique source meshes were removed only after
replacement validation and a saved map reload. Two coincident wall instances were also removed.

The monster Blueprint is `/Game/NPC/Monster/Lunger`, and its placed actor is labeled `Lunger`. Its actor
identity, placement, roaming, animation, physics and native reaction component are preserved. The animation
Blueprint and dedicated physics asset keep their existing names. The old `BP_Monster` asset is a compatibility
redirector; retain it while an older editor DLL still contains the original setup/test path. Current source
uses `Lunger`. The path-only source changes passed a compile-only Windows build on 2026-11-18; that command
does not replace the running DLL. Use the normal closed-editor build when loading those source changes.

The earlier wall/floor migration removed 372 actors and 371 source mesh assets. The later frame/roof pass
replaced another 159 actors with 30 groups (129 fewer actors). Other geometry and gameplay actor transforms
were preserved, including the user's additional toolboxes.

Backups and SHA-256 manifests are under `Saved/Backups/LungerMazeInstancing/BeforeMigration/`,
`Saved/Backups/MazeModules/BeforeMigration/` and `Saved/Backups/MazeAlignment/BeforeAlignment/`.
Placement and verification evidence is under `Saved/Temp/`. The latest plan and collision results are
under `Saved/Temp/MazeAlignment/`.
Do not restore an individual old external actor package into the converted map: it would duplicate geometry.

For future level work, save the affected World Partition external actor packages as well as the map. After
actor deletion, use the engine package-saving path that cleans empty packages; verify the old package files
are actually gone and cold-reload the map before deleting their meshes. A successful map-only save is
insufficient evidence that external actor changes persisted.

Latest validation is in the ecosystem task `.ai/tasks/2026-11-18-220827-maze-end-to-end-alignment/VERIFY.md`.
The earlier migrations are recorded under `.ai/tasks/2026-11-18-203555-maze-doorframes-roof-edge-snap/`
and `.ai/tasks/2026-11-18-165910-lunger-maze-instancing/`.
