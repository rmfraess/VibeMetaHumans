---
name: metahuman-creator
display_name: MetaHuman Creator (characters)
description: Create and edit MetaHuman Character assets with unreal.MetaHumanCharacterService - blank template or preset, body shape and body type, skin tone and skin properties, eyes, makeup, teeth and eyelashes, face coefficients, sculpt-landmark fitting to a reference picture, Mesh to MetaHuman imports (Identity, DNA, template meshes), cloud auto-rig and high-res textures, wardrobe and built-Blueprint hair/groom tools, non-blocking build and save, build to a Blueprint, spawn in the level, export DNA. Use for "make a MetaHuman", "change her hair/skin/body", "rig and build the character", "add hair to the built character".
vibeue_classes:
  - MetaHumanCharacterService
  - MetaHumanSetupService
  - MetaHumanObjectService
unreal_classes:
  - UMetaHumanCharacter
  - UMetaHumanCharacterEditorSubsystem
  - UMetaHumanCollection
  - UMetaHumanWardrobeItem
keywords:
  - metahuman creator
  - create metahuman
  - body shape
  - skin tone
  - eye color
  - makeup
  - hair groom
  - face landmarks
  - add hair
  - auto rig
  - assemble
  - build metahuman
  - deferred save
---

# MetaHuman Creator (characters)

```python
import json, unreal
mh = unreal.MetaHumanCharacterService
```

## Mental model

A **MetaHuman Character** asset (`/Game/MetaHumans/MH_Name`) stores face + body state, the look
settings (skin, eyes, makeup, head model = eyelashes/teeth, face evaluation = head scale) and an
internal wardrobe **collection** (slots → items). Edits go through the Creator editor subsystem; every
service tool opens the asset for edit on demand, commits the change and refreshes the preview. The
character becomes usable for animation after a cloud **auto-rig** (`rig_state: "Rigged"`), and
becomes a placeable actor after **build** (assembly) writes `BP_<Name>` plus meshes, materials and
grooms under `/Game/MetaHumans/<Name>`.

## Workflow

1. `create_character(package_path, name, preset_path)` — `preset_path` empty = blank template.
   Presets live in `/MetaHumanCharacter/Optional/Presets` (optional content) or can be any existing
   character; the new asset copies its state.
2. Shape the body: `set_body_shape(c, masc_fem, fat, muscularity, height_cm)` (0..1 each, negative
   = unchanged; height 135..220). Finer control: `get_body_constraints` → `set_body_constraints(c,
   '{"Height":182,"Upper Arm Length":0.6}')`. Fixed legacy proportions: `set_body_type(c, "m_tal_nrw")`;
   back to parametric: `set_body_type(c, "BlendableBody")`.
3. Shape the face: `get_face_coefficients` / `set_face_coefficients` (PCA vector; small deltas),
   `set_settings(c, "face_evaluation", '{"HeadScale":1.05}')`, `reset_face` to start over.
   Likeness from a scan or footage: `import_from_identity` (an Identity from
   `MetaHumanIdentityService`), `import_from_face_dna`, `import_from_template`.
   Likeness from a single reference PICTURE (no 3D scan, 2026-10-07): `get_face_landmarks(c)` reads the
   Creator's own sculpt landmarks (`{"landmarks":{"0":[x,y,z],...}, "likely_ears":[...], "likely_neck":[...],
   "likely_skull":[...]}`, centimetres, edit frame) - match them to a MediaPipe measurement of a front capture
   of the preview actor the way Master Smith's `face_fit.py` does, then
   `move_face_landmarks(c, '{"23":[0.3,0,-0.1],"41":[-0.2,0,0.15]}')` moves just those indices (deltas; pass
   `bAbsolute=true` for absolute positions) and holds every OTHER landmark in place by default
   (`HoldJson="auto"`; `"none"` lets everything else drift, a JSON array of indices holds exactly those),
   repeating translate+commit until every residual is under `ToleranceCm` (default 0.06) or
   `MaxIterations` (12) runs out - it returns the per-iteration residuals so you can see it converge.
   It REFUSES while the character wears any groom (spawning the Creator preview with grooms on hung the
   editor for minutes, 2026-10-06) and while the face rig is on (`remove_face_rig` first), and refuses any
   target landmark the position heuristic calls an ear unless `bAllowEars=true` - moving ear landmarks
   crumpled MissionCommander's ears (2026-10-07; `likely_ears` is read off positions each call, never a fixed
   index list, because the landmark SET differs per preset - 79 on one preset, 125 on another).
4. Look: `set_skin_tone(c, u, v)` (u = lightness 0 light → 1 dark, v = redness 0 → 1),
   `set_eye_color(c, u, v, "both")` (u = temperature cool → warm, v = brightness dark → light), and
   JSON patches for everything else:
   - `set_settings(c, "skin", '{"Skin":{"Roughness":1.0,"FaceTextureIndex":3},"Freckles":{"Density":0.6,"Mask":"Type2"}}')`
   - `set_settings(c, "eyes", '{"EyeLeft":{"Iris":{"IrisPattern":"Iris004"},"Pupil":{"Dilation":1.0}}}')`
   - `set_settings(c, "makeup", '{"Lips":{"Type":"Hollywood","Color":{"R":0.4,"G":0.02,"B":0.03,"A":1}},"Eyes":{"Type":"CatEye"}}')`
   - `set_settings(c, "head", '{"Eyelashes":{"Type":"LongCurl"},"Teeth":{"ToothLength":0.3,"Overbite":0.1}}')`
   Shapes: `get_settings(c, section)` or `MetaHumanObjectService.describe_struct("MetaHumanCharacterMakeupSettings")`.
5. Wardrobe: `get_wardrobe(c)` lists slots (`Hair, Eyebrows, Eyelashes, Mustache, Beard, Peachfuzz,
   Outfits`), items in the collection and the current selection. `add_wardrobe_item(c, "Hair",
   "/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/WI_Hair_L_StraightBangs", True)` adds and selects;
   `select_wardrobe_item(c, "Hair", "")` clears a slot. Items come from
   `MetaHumanSetupService.list_assets("wardrobe", "")` or `list_wardrobe_items("Hair")` (groom slots only;
   see **Hair on a built MetaHuman** below for a SECOND groom layered onto a built Blueprint).
6. Rig. Presets and edited faces arrive **Unrigged**; the rig comes from Epic's cloud auto-rig:
   `request_auto_rig(c, "JointsOnly", False)` (`JointsAndBlendShapes` for the heavier cinematic rig),
   then poll `get_summary(c)["rig_state"]` (or the shaped `get_auto_rig_status(c)`: `running|done|
   failed_or_not_requested`) until `Rigged`. The first request opens an Epic account
   sign-in page in the browser (device code, expires after ~10 min) - a human must complete it.
   Never pass `bBlocking=True` unless someone is at the machine: a blocking request parks the whole
   editor (and this MCP) until the login finishes or expires (`Login Failed`).
   Optional `request_texture_sources(c, True)` downloads 2k-8k skin sources (cloud, same login).
   **Offline fallback (no login):** import the engine's archetype head rig as a whole rig -
   `import_from_face_dna(c, "<UE>/Engine/Plugins/MetaHuman/MetaHumanCoreTechLib/Content/ArchetypeDNA/SKM_Face.dna", True, False)`
   gives `rig_state: Rigged` with a generic (archetype) face shape; skin, eyes, makeup, hair and
   body stay. Then clear the texture gate the same way Epic's own pipeline tests do:
   `MetaHumanObjectService.set_properties(c, '{"bHasHighResolutionTextures": true}')` (the build uses
   the locally synthesized textures; wrinkle/animated maps are skipped). Re-rig the real face later
   with `remove_face_rig(c)` + `request_auto_rig(...)` once signed in.
7. Build: `can_build(c)` then `build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", "")`. Pipeline
   `Optimized` + quality `Low|Medium|High` for games; `UEFN` for Fortnite. Result lists the
   Blueprints. `spawn_in_level(c, loc, rot, False)` places the live preview actor instead (it
   follows further edits).
   A build can take 4-5 minutes (texture bakes, groom bindings) and a client that waits on one tool
   call that long can time out and drop its Unreal tools (2026-10-06): `build_async(c, ...)` queues it
   on a later editor tick and returns at once (`status: "queued"`), then poll `get_build_status(c)`
   (`queued|running|done|failed`, `seconds`, `blueprints`) rather than waiting on `build` itself. The
   build still blocks the game thread once it starts - this only frees the RPC call, not the editor.
8. `save(c)` and `end_edit(c)`. **Never call `save` (or `MetaHumanCharacterService.save`) on a
   character that is still in an edit session and expect it to return quickly: it now ROUTES to
   `save_deferred` automatically and says so (`routed`), because a save made in the same call that
   changed a Creator character deadlocked the editor four times (2026-10-06/07) - a game-thread
   sleep-poll inside `UPackage::Save` for async texture-synthesis work that only the game thread
   advances. `save_deferred(c, delay_seconds=15, min_frames=45)` ends the edit session now and saves
   on a tick at least that far away; poll `get_save_status(c)` (`pending|skipped|saved|failed`) for the
   result - do not assume it saved just because the call returned. A newer `save_deferred` on the same
   character re-arms the timer; if it is back in an edit session when the tick fires, that save is
   skipped (call `save_deferred` again once you are done).**

## Hair on a built MetaHuman (2026-10-07)

A groom from `add_wardrobe_item` (step 5) is baked into the FIRST `build()`. To add, swap or layer a
groom on an ALREADY-BUILT Blueprint without rebuilding the whole character (DrHart's working recipe:
a second "spiky crown" groom layered over the base fringe, keeping every other polish round's work):

```python
b = json.loads(mh.build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", ""))
bp = b["blueprints"][0]                                                  # BP_<Name>
items = json.loads(mh.list_wardrobe_items("Hair"))["items"]              # path, name, slot per groom
r = json.loads(mh.add_groom_to_blueprint(bp, "HairTop", items[0]["path"], "Face", "Hair", True))
#   -> groom, binding, binding_duplicated, binding_saved, material_slots, materials_copied, compiled
```

`GroomAssetPath` takes a groom Wardrobe Item (`WI_Hair_...`, as above), the plugin's own
`GroomBindingAsset` directly, or a bare `GroomAsset` (e.g. from `import_groom`, which has no plugin
binding to retarget - the new component comes back unbound, no RBF transfer onto this face).
For a wardrobe item or plugin binding, the plugin's binding is DUPLICATED into
`<Blueprint's folder>/Accessories/<ComponentName>/` and retargeted onto the Blueprint's BUILT face
mesh - the same thing `build()`'s own groom pipeline does internally (`DuplicateObject`,
`SetTargetSkeletalMesh`, which kicks an ASYNC RBF rebuild). `MaterialFromComponent` ("Hair" above)
copies an existing groom component's override materials onto the new one's matching slots (by name,
then by position) so a dyed/tinted look carries over. `bSaveBinding=True` (the default) saves the
duplicated binding; pass `False` and save it yourself **at least a minute later** if you are about to
make several such calls in a row - saving a binding mid-build can hang the editor the same way a
Creator save can. `remove_component_from_blueprint(bp, "HairTop")` takes it back off.
`import_groom("E:/hair/Custom.abc", "/Game/MetaHumans/Dryad/Accessories/Hair", "Groom_Custom")`
brings in a groom made outside Unreal (guides/strands only; reports each group's curve/guide counts).

**Never** spawn a Creator preview (`spawn_meta_human_actor` / any front-capture helper) on a character
that still wears grooms in its wardrobe collection - that is a DIFFERENT thing from the built
Blueprint's own grooms above, and doing it hung the shared editor for about 9 minutes (2026-10-06):
`move_face_landmarks` already refuses for this reason; clear the Creator's groom slots (step 5),
finish sculpting, then add grooms back before building.

## Custom mesh to MetaHuman (UE 5.8, headless)

The Mesh Import tool's "Import > from custom mesh > Auto Solve" without its viewport (added 2026-10-04 for Master
Smith's characters). The mesh is a static or skeletal mesh already imported into the project: one combined body+head
(or a body and a separate head), centimetres, feet on z=0, facing **+Y** (the MetaHuman frame), A-pose with the arms
clear of the body and the fingers apart, bald, no eyelashes, nothing worn above the skin. A GLB exported from Blender
with the character facing -Y lands facing +Y in Unreal.

```python
c = json.loads(mh.create_character("/Game/MetaHumans", "MH_Dryad", ""))["character"]
r = json.loads(mh.import_from_custom_mesh(c, "/Game/Characters/Dryad/SM_Dryad_conform", "", True, True, 1024))
#   -> solved, seconds, facing, height_cm, face_tracking {curves, points}, warnings (feet off the floor, metres, facing)
#      1-3 minutes blocking for a 60k-vertex mesh; the character now holds the body in the MESH'S pose
mh.save_posed_dna(c, "/Game/Characters/Dryad/SM_Dryad_conform", "", "/Game/Characters/Dryad", "E:/out/Dryad/delivery/metahuman/in", "Dryad_Posed")
#   -> the UI's "Save Pose": one combined DNA still in that pose = the texture-baking target
mh.generate_skeletal_mesh_from_dna("/Game/Characters/Dryad/Dryad_Posed", "/Game/Characters/Dryad", "SKM_Dryad_Posed", "body")
mh.export_fbx("/Game/Characters/Dryad/SKM_Dryad_Posed", "E:/out/Dryad/delivery/metahuman/in/Dryad_Posed.fbx")
mh.commit_a_pose(c, "/Game/Characters/Dryad/SM_Dryad_conform", "")     # back into the MetaHuman A-pose
mh.request_auto_rig(c, "JointsOnly", True); mh.request_texture_sources(c, True)
b = json.loads(mh.build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", ""))
# textures baked in a DCC onto the posed mesh come back onto the built materials:
t = json.loads(mh.import_texture("E:/out/Dryad/delivery/metahuman/T_Dryad_Body_N.png", "/Game/MetaHumans/Dryad/Body/Baked", "T_Dryad_Body_N", "normal", True))["texture"]
mh.set_material_texture("/Game/MetaHumans/Dryad/Body/Materials/MI_Body_Baked", "Normal Baked", t)
# an accessory rigged to the MetaHuman skeleton (SK FBX imported onto metahuman_base_skel) onto the Blueprint:
mh.attach_skeletal_mesh_to_blueprint("/Game/MetaHumans/Dryad/BP_Dryad", "Hair", "/Game/MetaHumans/Dryad/Accessories/SK_Dryad_Hair", "Body", True)
# a rigid accessory rides a bone socket instead (a headset on "head"): relative transform in the socket's frame, centimetres
mh.attach_static_mesh_to_blueprint("/Game/MetaHumans/Dryad/BP_Dryad", "Headset", "/Game/MetaHumans/Dryad/Accessories/SM_Headset", "Body", "head", unreal.Vector(-161.5, 1.5, 0.1), unreal.Rotator(roll=0, pitch=-90, yaw=0))
# (unreal.Rotator takes roll, pitch, yaw in that order - name them; the built head sits about 23 cm behind the
#  conform mesh's head and 5 cm to the side: measure the built face mesh's bounds against the conform mesh's)
```

What the solver gets wrong is in the mesh (fused fingers, a closed armpit, hair conformed into the skull): fix the
mesh and run again; `import_from_custom_mesh` returns `warnings` for the feet, the units and the facing. The face
tracking renders the top 15% of the mesh (or the separate head) front-on, lit from the camera, in a private preview
scene and writes the picture to `Saved/VibeMetaHumans/FaceTrack_<mesh>.png` (`face_tracking.render`): look at it
when the tracker finds no face. A head with empty eye sockets or no eyes fails the tracker (the MetaHuman topology
template did, 2026-10-04; a MetaHuman face with its eyes gave 16 curves / 358 points); then the solve still runs
on the body and the face stays the archetype's. `export_fbx` fills empty
material slots with the default material for the export (an empty slot crashed the engine's exporter).
Two solves for one character (the AINavigator, 2026-10-05): the solve WITH face landmarks stood the posed body 19 cm
behind the mesh (and 2% taller), so a bake against its posed DNA missed; the solve WITHOUT (`bTrackFace=False`) sat
on the mesh to the millimetre but keeps the archetype face. Bake from a scratch body-only character's posed DNA
(then delete it), and keep the face-tracked character for the rig and the build; the textures carry over
because every MetaHuman shares the UV layout. Conform once per fresh character: a second
`import_from_custom_mesh` after `commit_a_pose` gave a twisted posed DNA.
Built materials and their texture parameters: `MI_Body_Baked` ("Basecolor Baked", "Normal Baked", "SRMF Baked",
"Scatter Baked"), `MI_Face_Skin_Baked_LOD3` / `_LOD5to7` (the same plus "Normal LOD Baked"), `MI_EyeL/R_Baked` ("Iris
Basecolor Baked", "Iris Normal Baked", "Sclera ..."), `MI_Teeth_Baked`.

`import_texture` finishes the texture's compilation before returning and REFUSES (`IMPORT_INCOMPLETE`,
nothing marked dirty) when it built as a 32x32 placeholder with no miplevels - a texture-pipeline
stall seen after long editor sessions (2026-10-07; the Output Log says "contains no miplevels"). It
rendered a face white, and the next save of ANY material instance that uses that texture hangs in
`SavePackage`. Restart the editor before importing again rather than retrying in the same session.
Replacing a garment or accessory mesh IN PLACE (same asset path, same skeleton, after it was fit or
shrunk outside Unreal) is `reimport_skeletal_mesh(asset_path, fbx_path, bKeepMaterials=True)`: the
Blueprint's component keeps pointing at the same asset, slot materials are restored by slot name
afterwards (an import with materials off can leave `WorldGridMaterial` on them), and it refuses
(`SKELETON_MISMATCH`) rather than silently retargeting the mesh onto a different skeleton.

## Example: a specific character

```python
c = json.loads(mh.create_character("/Game/MetaHumans", "MH_Marina", ""))["character"]
mh.set_body_shape(c, 0.85, 0.25, 0.45, 168)
mh.set_skin_tone(c, 0.35, 0.55)
mh.set_eye_color(c, 0.15, 0.75, "both")
mh.set_settings(c, "head", '{"Eyelashes":{"Type":"LongSlightCurl"}}')
mh.add_wardrobe_item(c, "Hair", "/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/WI_Hair_L_StraightBangs", True)
mh.add_wardrobe_item(c, "Eyebrows", "/MetaHumanCharacter/Optional/Grooms/Bindings/Eyebrows/WI_Eyebrows_Thin", True)
mh.request_auto_rig(c, "JointsOnly", False)   # then poll get_summary(c)["rig_state"] == "Rigged" (needs the Epic sign-in)
out = json.loads(mh.build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", ""))
mh.save(c); mh.end_edit(c)
```

## Gotchas

- `get_summary(c)` is the cheap read: rig state, DNA/texture flags, body type, skin/eye tone,
  active constraints, wardrobe selections.
- Body constraints are rejected on a fixed body type (`FIXED_BODY`); switch to `BlendableBody` first.
- `set_body_constraints(c, values, normalized, only_named=False)` activates the measurements you name;
  the others keep their active flag, so a later body-shape or height call does not switch off a neck an
  earlier call widened. Pass `only_named=True` to leave only the named ones plus `Height` active: pinning
  every constraint active blocked a neck widen entirely (it did nothing until only `Height` + `Neck` +
  `Neck Base` were active, MissionCommander 2026-10-07). When going through the raw subsystem, the values must still be
  `.copy()`'d and passed back as a whole array in Python - editing `get_body_constraints`' items in
  place and expecting the subsystem to see it does nothing. The result's `measurements` array gives
  `name/active/before/after/delta` for EVERY measurement, not just the ones you named, because a body
  change often moves others as a side effect (a neck widen moved Shoulder Height +1.4 cm too).
- `can_build` reasons: `Character is not rigged.` -> step 6; `missing textures, use Download Texture
  Sources` -> `request_texture_sources` (cloud) or the `bHasHighResolutionTextures` flag above.
- The whole-rig DNA import makes the body a fixed type (`fixed_body_type: true`), so body-shape tools
  are rejected afterwards; set body shape first.
- Wardrobe item paths must be Wardrobe Item assets (`WI_*`), not groom or cloth assets directly.
- Without the optional content `add_wardrobe_item` has nothing to add and `create_character` with a
  preset path fails with `PRESET_NOT_FOUND`; create from the blank template and say why.
- A build can take a minute or more (texture bakes, groom bindings). Do not call it in a loop.
- The Live Link / Animator inputs live on the **built** Blueprint, not on the preview actor.
- Read the engine-level reference skill `ue-metahuman-creator` (UnrealEngineSkills) when you need
  struct field semantics, pipeline internals or C++.
