# References after the Content folder restructuring

The FirstPerson map's external actors still imported materials such as
`/Game/materials/Horror_wall` and `/Game/materials/Horror_floor` after those
materials moved to `/Game/Level/Objects/materials/`. The materials and their
texture files are present at the new locations.

`Config/DefaultEngine.ini` now supplies package redirects for the verified
material, maze mesh, combined mesh, blacksmith object, cat and monster folder
moves. The monster setup and automation test also use the current
`/Game/NPC/Monster/` folder directly.

The missing first-person material parent `MI_DefaultColorway` and its dependency
`M_FlatCol` were restored from verified original local Git LFS objects to
`Content/LevelPrototyping/Materials/`. Existing assets were not overwritten.

## Apply and verify in UE5.6

1. Close Unreal Editor completely, allow the changed source/config/assets to
   sync to the development project, then reopen it. Core redirects are read
   during startup; Live Coding alone does not apply these config changes.
2. Open `FirstPerson/Lvl_FirstPerson` and allow shader compilation to finish.
   Check the walls, floor, ceiling, props and first-person material.
3. If a surface is still gray or checkerboard, open the affected mesh's material
   slot and its assigned material. Check Output Log for the first missing
   package or material compiler error. Report that exact path/error rather than
   retrying redirector cleanup.
4. Once materials load correctly, save the affected assets/maps. Keep the
   compatibility redirects while older packages or saves still contain old paths.
5. Rebuild `LostWithinEditor` before running the monster setup command, because
   its C++ asset paths changed. The material/config repair itself only needs the
   editor restart.

## Remaining missing template content

The diagnostic still finds four missing packages used by orphaned
`Variant_Horror/Lvl_Horror` external actors:

- `/Game/Variant_Horror/Lvl_Horror`
- `/Game/LevelPrototyping/Interactable/Door/BP_DoorFrame`
- `/Game/LevelPrototyping/Interactable/Door/Assets/Meshes/SM_DoorFrame_Edge`
- `/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray`

These have no verified moved destination in the working content tree and were
left unchanged. Restore the template level and its dependencies from version
control if that separate level is needed.

## Verification limits

A read-only UE5.6 package-import scan resolves numbered FNames and checks the
configured destination files. Raw string searches can incorrectly mark numbered
texture names as missing. Engine loading, shader compilation, rendering and
resaving must still be verified in Unreal on the development machine.

See [UE5.6 Core Redirects](https://dev.epicgames.com/documentation/en-us/unreal-engine/core-redirects-in-unreal-engine?application_version=5.6)
for the package redirect format.
