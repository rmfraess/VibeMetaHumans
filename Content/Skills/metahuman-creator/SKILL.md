---
name: metahuman-creator
display_name: MetaHuman Creator (characters)
description: Create and edit MetaHuman Character assets with unreal.MetaHumanCharacterService - blank template or preset, body shape and body type, skin tone and skin properties, eyes, makeup, teeth and eyelashes, face coefficients, sculpt-landmark fitting to a reference picture, Mesh to MetaHuman imports (Identity, DNA, template meshes), cloud auto-rig and high-res textures, wardrobe items and their per-item settings (groom colour, ombre, highlights), custom grooms as Creator Hair wardrobe items (Legacy02 binding, copied WI, WardrobePaths folder), built-Blueprint hair/groom tools, non-blocking build and save, build to a Blueprint, spawn in the level, export DNA. Use for "make a MetaHuman", "change her hair/skin/body", "colour his beard", "rig and build the character", "add hair to the built character", "make my groom selectable in the Creator".
vibeue_classes:
  - MetaHumanCharacterService
  - MetaHumanSetupService
  - MetaHumanObjectService
unreal_classes:
  - UMetaHumanCharacter
  - UMetaHumanCharacterEditorSubsystem
  - UMetaHumanCollection
  - UMetaHumanWardrobeItem
  - UGroomAsset
  - UGroomBindingAsset
keywords:
  - metahuman creator
  - create metahuman
  - body shape
  - skin tone
  - eye color
  - makeup
  - hair groom
  - hair color
  - groom color
  - face landmarks
  - add hair
  - custom groom
  - custom hair
  - groom binding
  - wardrobe path
  - card hair
  - alembic groom
  - groom turntable
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
internal wardrobe **collection** (slots → items). Edits go through the Creator editor subsystem;
remaining service tools may open the asset for edit on demand; native subsystem edits require an acquired edit session. The
character becomes usable for animation after a cloud **auto-rig** (`rig_state: "Rigged"`), and
becomes a placeable actor after **build** (assembly) writes `BP_<Name>` plus meshes, materials and
grooms under `/Game/MetaHumans/<Name>`.

## Workflow

1. `create_character(package_path, name, preset_path)` — `preset_path` empty = blank template.
   Presets live in `/MetaHumanCharacter/Optional/Presets` (optional content) or can be any existing
   character; the new asset copies its state.
2. Open for native edits: obtain `sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)`
   and load the character object. Acquire `sub.try_add_object_to_edit(character)`; if it returns false,
   do not release a session you did not acquire. The `metahuman_toolset.metahuman.MetaHumanToolset`
   also exposes `begin_edit`, `set_body_shape` and `set_skin_tone` with session-based arguments;
   inspect its schema with `vibeue.get_toolset_schema` before `vibeue.exec_tool`. Its edit session is
   not interchangeable with the old service's path argument. End an owned session when done.
   For finer control use `sub.get_body_constraints` and `sub.set_body_constraints`, passing the full
   constraint array, then `sub.commit_body_state`. Fixed legacy proportions: `set_body_type`.
3. Face: `sub.get_face_model_coefficients` / `sub.set_face_model_coefficients`, or
   `sub.get_face_landmarks` / `sub.translate_face_landmarks`, then commit the face state. Use the
   subsystem's `import_from_identity`, `import_from_face_dna` or `import_from_template` for likeness.
   Native landmark editing does not provide the removed wrapper's convergence loop, held-landmark
   policy or safety refusals. Clear grooms and remove the face rig before sculpting; previewing a
   groomed character has hung the editor. Ear edits can crumple ears; identify indices per character
   and avoid them unless explicitly intended. Landmark sets differ between presets.
4. Look: skin tone is lightness and redness; use the native session tool above or read
   `character.skin_settings`, modify the copy and call `sub.commit_skin_settings(character, settings)`.
   Do likewise with eyes, makeup and head settings and their matching subsystem commit functions.
   For eyes, read `character.get_editor_property("eyes_settings")`, edit the struct and call
   `sub.commit_eyes_settings(character, settings)`. `MetaHumanObjectService.describe_struct`
   can help inspect shapes. Native commits use structs, not the former JSON patch.
5. Wardrobe: acquire an edit session with `sub.try_add_object_to_edit(c)`, then `pc = sub.get_preview_collection(c)`.
   `pc.get_slot_names()` lists slots; `pc.get_default_instance().get_slot_selections()` lists current
   slot names and selected items. Slots and the
   principal asset each one's items wrap: `Hair, Eyebrows, Eyelashes, Mustache, Beard, Peachfuzz`
   (`GroomBindingAsset`), `Outfits` with its user-facing `Top Garment` / `Bottom Garment` (`ChaosOutfitAsset`),
   `SkeletalMesh` (`SkeletalMesh`, skeletal clothing, multi-select). Load a wardrobe item `wi`, then
   `key = pc.try_add_item_from_wardrobe_item("Hair", wi)` (for an existing item use
   `pc.get_item_keys_for_wardrobe_item(wi)`); select via
   `pc.get_default_instance().set_single_slot_selection("Hair", key)`. Clear with
   `unreal.MetaHumanPaletteItemKey()` as the key. Commit and update the preview with
   `sub.on_edit_preview_collection(c); sub.assemble_for_preview(c)`. Selecting REPLACES the slot's selection
   (`SetSingleSlotSelection`), so layering several `SkeletalMesh` pieces needs the instance's
   `try_add_slot_selection` (no service tool yet). Items come from
   `EditorToolset.AssetToolset.FindAssets` (inspect its schema; search the optional wardrobe folders;
   see **Hair on a built MetaHuman** below for a SECOND groom layered onto a built Blueprint, and
   **Custom hair as a Creator wardrobe item** for making your own groom wearable here).
   Per-item settings - groom colour (Melanin, Redness, Roughness, Whiteness, Lightness, DyeColor, plus the Ombre,
   Regions and Highlights groups when the item's groom pipeline enables them: `bSupportsOmbre` / `bSupportsRegions`
   / `bSupportsHightlights`, Epic's spelling, all on by default) and any outfit parameters - live on the character, the same values the Creator's
   Hair / Wardrobe panels edit: `get_wardrobe_item_parameters(c, "Hair")` (empty slot = every worn item) returns
   each item's `parameters`, `types`, `defaults` and `overridden` names;
   `set_wardrobe_item_parameters(c, "Hair", '{"Melanin":0.48,"Redness":0.53,"DyeColor":[1,0.2,0.2]}')` sets them
   (`ItemName` picks one of several items in a slot; `bClearOverrides=true` resets to the item's defaults first).
   Colour grooms HERE, never on a built Blueprint's `MI_WI_*` materials: every rebuild regenerates those, while
   the character's values go into every build. Overrides belong to the worn item - select the groom first, then
   set its colours.
   While the character is open for editing (the Creator's tab, or a native edit session), the Creator works on a PREVIEW
   copy of the collection and copies it back over the asset after each of its own edits. The wardrobe tools read
   and write that live copy and commit it, so they are safe beside an open Creator tab and the two never
   overwrite each other (before 2026-10-07 they wrote the asset directly, and the Creator's next edit reverted
   them: a Blueprint built with one set of grooms while the MetaHuman showed another).
6. Rig. Presets and edited faces arrive **Unrigged**; the rig comes from Epic's cloud auto-rig:
   native `sub.request_auto_rigging(char, p)` with `p = unreal.MetaHumanCharacterAutoRiggingRequestParams()`
   (rig type `JOINTS_ONLY`; `JOINTS_AND_BLEND_SHAPES` for the heavier cinematic rig; the character must be added for edit),
   then poll `get_summary(c)["rig_state"]` (or the shaped `get_auto_rig_status(c)`: `running|done|
   failed_or_not_requested`) until `Rigged`. The first request opens an Epic account
   sign-in page in the browser (device code, expires after ~10 min) - a human must complete it.
   Never pass `bBlocking=True` unless someone is at the machine: a blocking request parks the whole
   editor (and this MCP) until the login finishes or expires (`Login Failed`).
   Download 2k–8k texture sources using `sub.request_texture_sources` with native request parameters.
   This is a cloud operation with the same login requirement. Native `sub.import_from_face_dna` can
   import an archetype rig as an offline fallback, but it replaces the face with a generic shape.
   Do not force the high-resolution texture flag as a substitute for real downloaded sources.
7. Build: use `sub.can_build_meta_human` to check the loaded character, then
   `build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", "")`. Alternatively, queue
   `build_async(c, "Cinematic", "Cinematic", "/Game/MetaHumans", "")` and poll
   `get_build_status(c)` in later calls. Optimized quality Low/Medium/High is for games.
   The queued build still blocks the game thread once it starts. For a live preview actor, use
   `sub.spawn_meta_human_actor`; it follows edits and is not the built gameplay Blueprint.
8. Save with `save_deferred(c)` and poll `get_save_status(c)`; it ends the edit session. **Never call `save` (or `MetaHumanCharacterService.save`) on a
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

A groom added to the preview collection (step 5) is baked into the FIRST `build()`. To add, swap or layer a
groom on an ALREADY-BUILT Blueprint without rebuilding the whole character (a working recipe:
a second "spiky crown" groom layered over the base fringe, keeping every other polish round's work):

```python
b = json.loads(mh.build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", ""))
bp = b["blueprints"][0]                                                  # BP_<Name>
# Choose a groom Wardrobe Item path with EditorToolset.AssetToolset.FindAssets.
item_path = "/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/WI_Hair_L_StraightBangs"
r = json.loads(mh.add_groom_to_blueprint(bp, "HairTop", item_path, "Face", "Hair", True))
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
Creator save can. To remove `HairTop`, gather handles with
`subsys = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)` and
`subsys.k2_gather_subobject_data_for_blueprint(bp)`, find the named target handle, then call
`subsys.delete_subobject(handles[0], target_handle, bp)` using the root handle as context.

Full signatures: `add_groom_to_blueprint(bp, component, groom_or_item, target_component="Face",
material_from_component="", save_binding=True, socket_name="", inverse_socket_transform=False,
relative_location=(0,0,0), relative_rotation=(0,0,0))` and `import_groom(abc, folder, name, rigid_binding=True,
hair_width_cm=0.013, tip_scale=0.6)` (width <= 0 keeps the file's widths; nothing is saved - save in a later call).
A groom made outside Unreal (guides/strands only; the result reports each group's curve/guide counts) goes on a
built Blueprint RIGIDLY on the head bone - the three-argument `import_groom` already sets Rigid binding, a
0.013 cm width and tip 0.6:

```python
g = json.loads(mh.import_groom("E:/hair/Custom.abc", "/Game/MetaHumans/Dryad/Accessories/Hair", "Groom_Custom"))
# later call: save the groom, then
mh.add_groom_to_blueprint(bp, "HairCustom", "/Game/MetaHumans/Dryad/Accessories/Hair/Groom_Custom", "Body", "", False, "head", True)
```

`socket_name="head"` with `inverse_socket_transform=True` cancels the head bone's reference-pose transform, so a
groom authored in the mesh's component space lands where it was authored and follows the head. Hide the stock
`Hair` component yourself if the custom groom replaces it. Do not bind a dense custom groom to the face mesh
instead: an RBF bind of ~40k strands kept 12 cores busy for 16+ minutes.

**Never** spawn a Creator preview (`spawn_meta_human_actor` / any front-capture helper) on a character
that still wears grooms in its wardrobe collection - that is a DIFFERENT thing from the built
Blueprint's own grooms above, and doing it hung the shared editor for about 9 minutes (2026-10-06):
Native landmark edits do not refuse for this reason; clear the Creator's groom slots (step 5),
finish sculpting, then add grooms back before building.

## Custom hair as a Creator wardrobe item

Any custom groom can become a choice in the Creator's Hair slot, worn like library hair: it follows face
edits, takes the Hair colour settings and goes into every build (unlike the built-Blueprint path above).
The Hair slot lists only Wardrobe Items (`WI_*`) whose Principal Asset is a `GroomBindingAsset`; a dropped
`GroomAsset` does nothing. Library grooms are bound to one of the template heads in
`/MetaHumanCharacter/Optional/Grooms/GroomMesh` (`SKM_Groom_Head_Legacy01`, `SKM_Groom_Head_Legacy02` or
`MH_Groom_Head`; in 5.8 the Hair bindings split 18 Legacy02 / 17 Legacy01 / 1 MH_Groom_Head), and the Creator's
groom pipeline duplicates that binding and retargets it onto each character's face. The head you author on must
match the binding you copy: `Hair_S_Clean_Binding` (used below) is bound to Legacy02. So: author in Legacy02's
space, copy that binding and its item, point them at the custom groom, and put the item where the Creator looks.

1. **Author** on `SKM_Groom_Head_Legacy02`, in its component space (centimetres, face toward +Y). Each curve's
   FIRST point is its root; at most 255 points per curve (more are trimmed); roots on the scalp (MetaHuman groom
   verification warns past 4 mm).
2. **Import** `import_groom(abc, folder, name, False, 0.011, 0.5)`. `bRigidBinding=False` keeps the import
   default, **Skinning**, which Creator hair needs (it is skinned through its binding; Rigid is only for the
   unbound built-Blueprint path above). The two numbers turn the width override on (with no widths in the file
   every strand gets 0.01 cm, and `hair_width` does nothing unless `hair_width_override` is on) at root scale 1,
   tip 0.5. The default conversion places each Alembic point unchanged (1 unit = 1 cm, no axis swap), but
   `GroomImportOptions` is a per-user config object: a rotation or scale once confirmed in the import dialog is
   reused by every later import, scripted ones included. A groom that lands turned or scaled -> import with the
   raw task below and set `conversion_settings` yourself. An `.abc` that contains any polygon mesh, or a project
   without the "Alembic Groom Importer" plugin, goes to the geometry Alembic importer instead.
3. **Match the library** (observed in 5.8 on the library hair: width ~0.011 cm, root 1, tip 0.4-0.65, binding
   type Skinning). Copy the library groom's `hair_groups_materials` (`MI_Hair`, `MI_Hair_Cards`, `MI_Hair_Helmet`):
   a group whose slot name matches none falls back to slot 0, `MI_Hair`.
4. **Binding**: duplicate `Hair_S_Clean_Binding`, set `b.groom = g` (the property's BlueprintSetter `SetGroom`,
   which also moves the binding's change subscription to the new groom) and build it with
   `b.build(unreal.OnGroomBindingAssetBuildComplete())` (asynchronous; the Python overload needs the delegate).
   `set_editor_property("groom", g)` also rebuilds, but leaves the binding subscribed to the library groom. A
   binding is accepted only for the same groom with the same group count.
5. **Item**: duplicate `WI_Hair_S_Clean` - it keeps the library's groom pipeline (a `WI_` without a pipeline,
   including one the Creator auto-creates, fails validation and Wear skips it) - and repoint `principal_asset`.
   The copy inherits the library item's pipeline options. `WI_Hair_S_Clean` sets no `BakedGroomTexture`, so the
   copy gets no hair baked into the face at far LODs (from `GroomTextureMinLOD`, 5); copying an item that has one
   (e.g. `WI_Hair_L_Straight`, `WI_Hair_S_Messy`) bakes THAT style onto the face - replace it or clear it. It also
   inherits the colour-group flags (`WI_Hair_S_Clean` stores a non-default `bSupportsRegions`, so its copy may have
   no Regions group): read them with `w.get_editor_property("pipeline").get_editor_property("supports_regions")`
   (likewise `supports_ombre`, `supports_hightlights`). And it shows the Short Clean thumbnail until you set
   `thumbnail_image` / `thumbnail_name`.
6. **Save in a LATER call** (groom, binding, item), not in the call that made them.
7. **Register the folder** so the Creator's Hair panel lists it. Folders are scanned NON-recursively, and for every
   binding directly in a watched folder with no `WI_` pointing at it the Creator creates a pipeline-less one that
   fails validation - so keep only the `WI_` files in the watched folder and each groom + binding in a subfolder.
   `Config/DefaultMetaHumanCharacter.ini` (read at editor start; in a running editor add the same entry to the
   "New Wardrobe Default Asset Paths" project setting, which refreshes the wardrobe at once; Python cannot build
   the entry struct):
   ```ini
   [/Script/MetaHumanCharacterEditor.MetaHumanCharacterEditorSettings]
   +WardrobePaths=(ContentDirectoryToMonitor=(Path="/Game/Characters/Wardrobe/Hair"),SlotName="Hair",ClassesToFilter=("/Script/HairStrandsCore.GroomBindingAsset"),bPureVirtual=False)
   ```
   Other slots register the same way (`Outfits` + `/Script/ChaosOutfitAssetEngine.ChaosOutfitAsset`, `SkeletalMesh`
   + `/Script/Engine.SkeletalMesh`), but there is no library item to copy for them (the library `SkeletalMesh`
   section is pure virtual), and a mesh or outfit dropped straight into a watched folder gets an auto-created `WI_`
   with NO pipeline, which fails validation. Give such a `WI_` a pipeline, an instance of the Blueprint class
   `DefaultSkeletalMeshPipeline` / `DefaultOutfitPipeline` in `/MetaHumanCharacter/BuildPipeline/DefaultWardobePipeline/`
   (Epic's spelling): `w.set_editor_property("pipeline", unreal.new_object(cls, outer=w))` with `cls` the loaded
   `..._C` class; setting `pipeline` runs `SetPipeline`, which adds the default editor pipeline (per source, not
   tested). Skeletal clothing must be skinned to the MetaHuman body
   skeleton (MetaHuman verification checks it) and is not refit to the body; Chaos outfits are, and warn without a
   `BodyHiddenFaceMapTexture`. Per source the Creator's "Individual Assets" drop box wraps a dropped asset with the
   fallback pipeline from the settings' default asset pipelines - not tested.
8. **Wear and colour** using step 5's preview-collection route with the loaded `WI` asset,
   then `set_wardrobe_item_parameters` - a new groom wears the default auburn until coloured; which of the
   Ombre / Regions / Highlights groups it offers follows the copied pipeline's flags (step 5).
   Find optional-content or project wardrobe items with `EditorToolset.AssetToolset.FindAssets`. Wardrobe changes do not need the rig removed.
9. **Strands only, so far.** A custom Alembic groom has no card or mesh LODs and no baked texture, while the copied
   pipeline keeps the library's LOD transition (strands -> cards -> ...). Only Cinematic builds and the Creator
   preview were checked: Optimized / Low builds and far LODs may show no hair. Generate cards in the groom (Hair
   Cards) or check a low-quality build before relying on it.

```python
eal = unreal.EditorAssetLibrary
LIB = "/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/"
ROOT = "/Game/Characters/Wardrobe/Hair"                     # watched folder: WI_ files only
SUB, NAME = ROOT + "/Custom01", "GR_Hair_Custom01"           # groom + binding in a subfolder
GROOM, BIND, WI = SUB + "/" + NAME, SUB + "/Hair_Custom01_Binding", ROOT + "/WI_Hair_Custom01"
# call 1
mh.import_groom("E:/hair/Custom01.abc", SUB, NAME, False, 0.011, 0.5)
g = unreal.load_asset(GROOM)
lib_groom = unreal.load_asset(LIB + "Hair_S_Clean_Binding").get_editor_property("groom")
g.set_editor_property("hair_groups_materials", lib_groom.get_editor_property("hair_groups_materials"))
b = eal.load_asset(BIND) if eal.does_asset_exist(BIND) else eal.duplicate_asset(LIB + "Hair_S_Clean_Binding", BIND)
b.groom = g                                                     # SetGroom
b.build(unreal.OnGroomBindingAssetBuildComplete())              # async rebuild
w = eal.load_asset(WI) if eal.does_asset_exist(WI) else eal.duplicate_asset(LIB + "WI_Hair_S_Clean", WI)
pa, p = w.get_editor_property("principal_asset"), b.get_path_name()
pa.import_text('(Asset="%s",AssetIdentifier="%s")' % (p, p))   # EditorOnlyAssetReference
w.set_editor_property("principal_asset", pa)
assert w.get_editor_property("pipeline")                        # the copied groom pipeline
# call 2, later: save
for a in (GROOM, BIND, WI): eal.save_asset(a, only_if_is_dirty=False)
# then: add/select the loaded WI using step 5, commit the preview collection,
# and call mh.set_wardrobe_item_parameters(c, "Hair", '{"Melanin":0.3}')
```

Raw import (explicit conversion) and the groom-struct rule - read the array with `get_editor_property`, edit nested
structs with `set_editor_property`, and WRITE THE WHOLE ARRAY BACK with `set_editor_property` (that is what rebuilds
and dirties the groom; edits to `g.hair_groups_rendering`, an attribute read, are a lost copy):

```python
ABC, SUB, NAME = "E:/hair/Custom01.abc", "/Game/Characters/Wardrobe/Hair/Custom01", "GR_Hair_Custom01"
GROOM = SUB + "/" + NAME
o = unreal.GroomImportOptions()
cs = o.get_editor_property("conversion_settings")
cs.set_editor_property("rotation", unreal.Vector(0, 0, 0)); cs.set_editor_property("scale", unreal.Vector(1, 1, 1))
o.set_editor_property("conversion_settings", cs)
t = unreal.AssetImportTask()
for k, v in (("filename", ABC), ("destination_path", SUB), ("destination_name", NAME), ("automated", True),
             ("replace_existing", True), ("save", False), ("factory", unreal.HairStrandsFactory()), ("options", o)):
    t.set_editor_property(k, v)                                  # automated: no dialog (and nothing saved to config)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
g, rend = unreal.load_asset(GROOM), []
for r in g.get_editor_property("hair_groups_rendering"):
    gs = r.get_editor_property("geometry_settings")
    for k, v in (("hair_width_override", True), ("hair_width", 0.011), ("hair_root_scale", 1.0), ("hair_tip_scale", 0.5)):
        gs.set_editor_property(k, v)
    r.set_editor_property("geometry_settings", gs); rend.append(r)
g.set_editor_property("hair_groups_rendering", rend)
```

Hair that thins or vanishes in small views (portraits, captures) is Auto LOD scaling the curve count by screen
size, not the LOD `screen_size` (with the single imported LOD that value picks nothing):
`g.set_editor_property("lod_mode", unreal.GroomLODMode.MANUAL)` or raise `auto_lod_bias`.

**From card hair.** A card-hair pack (polygon cards + alpha and root textures) can be converted to strands outside
Unreal: fit the pack's head onto Legacy02 (width/depth by the cranium, height separately by crown-to-nose so
hairlines stay off the brows); take each UV island as one card and grow strands along it only where the alpha is
painted (sample V flipped), root end from the root map; where the map is flat, one style-wide geometric vote (which
V end lies nearer the scalp, area-weighted over all cards). Never decide per card by the nearer end: the outer locks
flip upside down. End strands at alpha gaps; thin fade/undercut cards, cut strands that enter the face, push the
rest out of the scalp, join floating roots; wind dreadlock strands as helices inside each painted column.
Blender (Z-up, -Y forward) to Legacy02: write each point as (x, -z, -y) and export with `global_scale=100`, which
lands at (x, -y, z) cm in Legacy02 component space after UE's default Alembic conversion (observed). Observed with
Blender 5.2: its Alembic export of a Curves object writes positions only (no widths, root UVs or groups) - set width
on import, one file per group.

**Reviewing a groom from several angles.** Observed in 5.8: the Creator viewport is no place for it - Head Sculpt
shows no grooms and Python cannot spawn actors into the Creator's preview world. Stage it in the level instead:
a transient `SkeletalMeshActor` with Legacy02 and a `GroomActor` whose `groom_component` (`gc`) is attached to it,
set with `gc.set_groom_asset(groom)` then `gc.set_binding_asset(binding)` (Python has only the one-argument
`set_groom_asset`; the two-argument C++ overload is not exposed, and `gc.binding_asset = b` fails as an attribute,
though `gc.set_editor_property("binding_asset", b)` before `set_groom_asset` works too); a `SceneCapture2D` (fixed exposure - observed,
a black backdrop drives auto exposure up) with key and fill lights orbiting with the camera; front, three-quarters,
sides, back and top. Drive it from `unreal.register_slate_post_tick_callback`: wait several hundred ticks before the
first shot (bindings build asynchronously) and a few dozen per angle; observed, the first groom's first angles can
still come out bald, so capture it again at the end. `RenderingLibrary.export_render_target` has the file written
when it returns - a blank or bald frame means the groom was not ready. Keep the editor in front, or turn off the
background throttle that stops viewports and ticks:
`unreal.get_default_object(unreal.EditorPerformanceSettings).set_editor_property("throttle_cpu_when_not_foreground", False)`.
Keep the Python handles of the transient actors (`get_all_level_actors` skips them) and destroy them afterwards.

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
# Export the posed DNA with unreal.MetaHumanCharacterExportBlueprintLibrary.export_posed_dna;
# commit the A-pose with sub.commit_posed_state_as_a_pose using a native target mesh key.
# Check their UE 5.9 signatures and parameter structs; do not pass the former wrapper arguments.
b = json.loads(mh.build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", ""))
# Import baked textures with mh.import_texture, then use
# EditorToolset.MaterialInstanceToolset.SetTextureParameter on the loaded material instance.
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
`import_from_custom_mesh` after committing the A-pose gave a twisted posed DNA.
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

Create the character, acquire an edit session, adjust body and look via the native tools above,
select wardrobe, rig and download textures. Then build and verify the result:

```python
out = json.loads(mh.build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", ""))
```

Save with `save_deferred` and verify `get_save_status` before treating the asset as saved.

## Gotchas

- `get_summary(c)` is the cheap read: rig state, DNA/texture flags, body type, skin/eye tone,
  active constraints, wardrobe selections.
- Body constraints are rejected on a fixed body type (`FIXED_BODY`); switch to `BlendableBody` first.
- Body constraints must be copied, adjusted and passed back as a whole array; commit the body
  state afterwards. Activating every constraint once blocked a neck widening; keep only needed
  constraints active. Changes may move other measurements as a side effect.
- `sub.can_build_meta_human` may report missing rig or texture sources; complete the cloud steps.
- The whole-rig DNA import makes the body a fixed type (`fixed_body_type: true`), so body-shape tools
  are rejected afterwards; set body shape first.
- Wardrobe item paths must be Wardrobe Item assets (`WI_*`), not groom or cloth assets directly
  (**Custom hair as a Creator wardrobe item** wraps a custom groom in one).
- Without the optional content the built-in wardrobe items are unavailable and `create_character` with a
  preset path fails with `PRESET_NOT_FOUND`; create from the blank template and say why.
- A build can take a minute or more (texture bakes, groom bindings). Do not call it in a loop.
- The Live Link / Animator inputs live on the **built** Blueprint, not on the preview actor.
- Read the engine-level reference skill `ue-metahuman-creator` (UnrealEngineSkills) when you need
  struct field semantics, pipeline internals or C++, and `ue-hair-and-grooms` for groom assets, Alembic
  import, bindings and Creator hair internals.
- Before `import_from_template`, copy or commit the character asset: restoring a face-coefficient
  snapshot afterwards does not recover the original shape (it was ~0.1 cm mean, ~0.9 cm max off).
- `import_from_template` keeps broad shape changes but strips small neck and under-jaw features
  (an Adam's apple landed ~15%, jaw-angle brushes ~35%); face-surface details land fully. Put
  neck and jaw-border detail into a post-build mesh or normal-map edit.
- Lower an upper eyelid with a forward component as well as down, or the cornea bulge pokes
  through the lid (a dark notch, then a scalloped lid line). Check a zoomed eye crop before saving.
- Creator-preview grooms vanish after a wardrobe or colour change until the binding rebuilds
  (minutes, silently); a preview spawned after an Editor restart has none until
  `assemble_for_preview`. Re-assemble, wait at least 3 minutes before judging hair, and restart the
  Editor if bindings never finish (`Waiting for groom bindings to be ready 0/1`).
- `reload_packages` on a character while its edit session is open has crashed the Editor and left
  Python unstable: release it first (`RemoveObjectToEdit` on the editor subsystem), then reload.
- Matching face landmarks is not likeness: proportions can match within a few percent while jaw,
  chin, nose and lid forms are visibly wrong, and a photo-over-render blend hides gaps. Judge form
  side by side at equal pupil scale under matched light; use landmarks for proportions only.
- Image-to-3D heads are not likeness targets: generated heads drift toward a generic face and
  scored further from the references than the MetaHuman. A multi-view head also has arbitrary
  scale and roll: scale by pupil distance, level the roll from the irises, set depth by the median
  midline-profile offset, and treat solid-mesh hair widths as hair, not skull.
