---
name: metahuman-creator
display_name: MetaHuman Creator (characters)
description: Create and edit MetaHuman Character assets with unreal.MetaHumanCharacterService - blank template or preset, body shape and body type, skin tone and skin properties, eyes, makeup, teeth and eyelashes, face coefficients, Mesh to MetaHuman imports (Identity, DNA, template meshes), cloud auto-rig and high-res textures, wardrobe (hair, brows, lashes, beard, outfits), build to a Blueprint, spawn in the level, export DNA. Use for "make a MetaHuman", "change her hair/skin/body", "rig and build the character".
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
  - auto rig
  - assemble
  - build metahuman
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
   `MetaHumanSetupService.list_assets("wardrobe", "")`.
6. Rig. Presets and edited faces arrive **Unrigged**; the rig comes from Epic's cloud auto-rig:
   `request_auto_rig(c, "JointsOnly", False)` (`JointsAndBlendShapes` for the heavier cinematic rig),
   then poll `get_summary(c)["rig_state"]` until `Rigged`. The first request opens an Epic account
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
8. `save(c)` and `end_edit(c)`.

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
```

What the solver gets wrong is in the mesh (fused fingers, a closed armpit, hair conformed into the skull): fix the
mesh and run again; `import_from_custom_mesh` returns `warnings` for the feet, the units and the facing. The face
tracking renders the top 15% of the mesh (or the separate head) front-on, lit from the camera, in a private preview
scene and writes the picture to `Saved/VibeMetaHumans/FaceTrack_<mesh>.png` (`face_tracking.render`): look at it
when the tracker finds no face. A head with empty eye sockets or no eyes fails the tracker (the MetaHuman topology
template did, 2026-10-04; a MetaHuman face with its eyes gave 16 curves / 358 points); then the solve still runs
on the body and the face stays the archetype's. `export_fbx` fills empty
material slots with the default material for the export (an empty slot crashed the engine's exporter).
Built materials and their texture parameters: `MI_Body_Baked` ("Basecolor Baked", "Normal Baked", "SRMF Baked",
"Scatter Baked"), `MI_Face_Skin_Baked_LOD3` / `_LOD5to7` (the same plus "Normal LOD Baked"), `MI_EyeL/R_Baked` ("Iris
Basecolor Baked", "Iris Normal Baked", "Sclera ..."), `MI_Teeth_Baked`.

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
