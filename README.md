> [!WARNING]
> **Unreal Engine 5.9 build: not fully tested yet.** This `5-9` branch builds and loads in UE 5.9, but
> it has not been tested end to end. Expect rough edges and report problems as issues. For production
> work, use the stable `main` branch.

# VibeMetaHumans — AI Editor Toolset for MetaHumans (Unreal Engine 5.9)

VibeMetaHumans is a sibling of [VibeUE](https://github.com/kevinpbuckley/VibeUE) that focuses on one
thing: letting an MCP agent (Claude Code, Cursor, Copilot, …) **create, edit, rig, animate and
place MetaHumans** inside the Unreal Editor. It plugs into Unreal 5.9's native
`ToolsetRegistry` + `ModelContextProtocol` server exactly like VibeUE does: no separate server,
no API key, no in-editor chat. Every tool is also a plain `unreal.<Service>.<tool>()` call in
editor Python.

It covers the four workflows of the in-editor MetaHuman pipeline:

| Workflow | Service | What it does |
|---|---|---|
| Create & edit a character (MetaHuman Creator) | `MetaHumanCharacterService` + native Creator tools | character creation, body and face editing, settings, rigging, wardrobe, asynchronous build and deferred save; see `metahuman-creator` |
| **Custom mesh to MetaHuman** | `MetaHumanCharacterService` + Creator subsystem | custom mesh conform, posed DNA export, A-pose commit, DCC round trips and Blueprint accessories |
| **Hair on a built MetaHuman** | `MetaHumanCharacterService` | add or import grooms; discover wardrobe assets with the native Asset toolset |
| **Dress a built MetaHuman** (2026-11-12, extended 2026-11-14) | `MetaHumanWardrobeService` | marketplace/Fab skeletal garments on a built Blueprint without a rebuild: `vmh_wardrobe.make_garment_variant` (pack garment copied with its own material and capped textures), `vmh_wardrobe.transfer_skin_weights` (weights onto a root-only Chaos render mesh), `describe_skeletal_mesh` (LODs, bones per LOD, sections, follower check), `fix_unevaluated_bone_weights` (cuff spikes and LOD streaks: every garment LOD against every body LOD it is drawn at), `make_full_body_from_dna` (the built body's missing skin back), `push_body_under_garments` (skin under a dress moved inward, nothing deleted), `hide_body_under_garments` (skin under closed garments deleted), `trim_inner_garment` (a shirt under a jacket), `push_garment_out_of_mesh` (a neckline out over the face mesh's neck skirt), `dump_runtime_poses` / `dump_actor_pose` (poses with the post-process correctives, as the game plays them), `measure_poke_through`, `capture_fit_views` (PNG views of the posed character from a private preview world: no level, no PIE), `vmh_wardrobe.add_skinned_accessory`, `snapshot_blueprint_components` / `restore_blueprint_components` (survive a rebuild, explicit clears kept; also `build(..., True)`), `post_build_fixup` (groom LODs, brow opacity, texture caps, Face anim class), `audit_placed_instances` (per-instance overrides on placed actors), `fill_groom_lods` |
| Facial animation from **webcam / DSLR video** | `MetaHumanAnimatorService` | ingest a video file → Footage Capture Data → MonoFootage Performance → process → AnimSequence / LevelSequence |
| Facial animation from an **iPhone** (Live Link Face) | `MetaHumanAnimatorService` + Epic `MetaHumanAnimatorIdentityTools` | ingest the take, build and auto-rig an Identity from a neutral frame, process a DepthFootage Performance |
| Facial animation from **audio only** | `MetaHumanAnimatorService` | SoundWave → Audio Performance with mood controls → process → export |
| **Real-time** face capture | `MetaHumanLiveLinkService` | webcam / microphone / Live Link Face sources, neutral calibration, head tracking, bind a subject to a placed MetaHuman, Take Recorder |
| Environment & discovery | `MetaHumanSetupService` + native asset tools | plugin and optional-content status, enum options, asset discovery and editors |
| Escape hatch | native Object toolset + `execute_python`; `MetaHumanObjectService` for hidden fields | inspect or patch properties and call reflected functions |
| Identity from DNA or footage | `MetaHumanIdentityService` | collision-safe create, DNA and brows import, optional training, save |

Every Vibe service tool returns a JSON string: `{"success": true, ...}` or
`{"success": false, "error_code": "...", "error": "..."}`.

> **Optional content.** MetaHuman Creator needs the **"MetaHuman Creator Core Data"** install
> option from the Epic Games Launcher (Library → Unreal Engine 5.9 → Options). Without it there are
> no presets, no texture synthesis and no wardrobe library. Cloud steps (auto-rig, high-resolution textures) need an Epic account
> signed in to the MetaHuman service.

---

## Install

1. Enable Unreal's MCP first (Edit → Plugins → **Unreal MCP**, which pulls in Toolset Registry) and
   turn on *Model Context Protocol → Auto Start Server* in Editor Preferences. Default endpoint:
   `http://127.0.0.1:8000/mcp`.
2. Enable the MetaHuman plugins the toolset drives: **MetaHuman Creator** (`MetaHumanCharacter`),
   **MetaHuman Animator** (`MetaHuman`), `MetaHumanSDK`, `MetaHumanCoreTech`, `MetaHumanLiveLink`,
   `LiveLink`, `Takes`, and `CaptureManagerEditor` (for footage ingest). The `.uplugin` lists them
   as dependencies so enabling VibeMetaHumans enables them too.
3. Clone this repo into `YourProject/Plugins/VibeMetaHumans`, add
   `{ "Name": "VibeMetaHumans", "Enabled": true, "TargetAllowList": ["Editor"] }` to the
   project's `.uproject`, and build the editor target (in Proteus: `BuildAndLaunchGame.ps1`, which is
   also copied here). The plugin is `EnabledByDefault: false` so an unbuilt checkout never
   triggers the "missing modules" dialog.
4. Verify in the editor console: `VibeMetaHumans.ListToolsets` (expect 7 toolsets).

Skill packs under `Skills/` are discovered natively by UE 5.9 (`<Plugin>/Skills/**/SKILL.md`) and
served through `ToolsetRegistry.AgentSkillToolset` → `ListSkills` / `GetSkills` as
`/Skills/VibeMetaHumans/<pack>`, the same way VibeUE's are. The engine re-reads the files on every
call, so edits need no reload or restart.

---

## Quick tour (editor Python)

Vibe services return JSON strings; native tools have their own result and asynchronous contracts.
See the skills for capture, authoring and recording details. Use `vibeue.get_toolset_schema` before
calling an engine tool: its arguments and results differ from the former service wrappers.

```python
import json, unreal, vibeue
mh = unreal.MetaHumanCharacterService
c = "/Game/MetaHumans/MH_Hero"
await call_tool("metahuman_toolset.metahuman.MetaHumanToolset.create", asset_path=c)
# Add the character for native edits and release it when done; see metahuman-creator.
# After rigging and downloading textures, build synchronously:
b = json.loads(mh.build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", ""))
# For long builds, use build_async instead and poll get_build_status in later calls.
print(vibeue.get_toolset_schema("CaptureManagerToolset.CaptureManagerIngestTools"))
```

Ingest and processing are asynchronous in the native toolsets; wait for completion and inspect the
result before using the capture data or exporting. Native edits need an edit session and native
settings structs, not the old JSON-patch wrapper. Use deferred save and check its final status.

Through the MCP endpoint the same tools appear as `VibeMetaHumans.MetaHumanCharacterService.Build`
etc. (`list_toolsets` / `describe_tools` / `call_tool`).

---

## Layout

```
VibeMetaHumans/
├── VibeMetaHumans.uplugin
├── Source/VibeMetaHumans/
│   ├── Public/Services/U*Service.h      # one UToolsetDefinition per domain, static AICallable UFUNCTIONs
│   ├── Private/Services/U*Service.cpp
│   ├── Public|Private/Utils/VmhJson.*   # JSON results, struct<->JSON, property patching, reflection calls
│   ├── Public|Private/Utils/VmhAssets.* # load/create/save assets, find actors
│   ├── Public|Private/Utils/VmhSkin.*   # CPU skinning from mesh descriptions, AnimSequence poses, AABB/winding queries
│   ├── Public|Private/Utils/VmhBlueprintSnapshot.* # record / restore a Blueprint's components around a rebuild
│   ├── Public|Private/Utils/VmhPreview.*  # a Blueprint posed in a private preview world (runtime poses, captures)
│   └── VibeMetaHumansModule.*           # reflection-based toolset registration, console commands
├── Content/Python/vibemetahumans.py     # agent helpers (status, toolset discovery)
├── Content/Python/vmh_wardrobe.py       # GeometryScript-backed wardrobe helpers behind three WardrobeService tools
├── Content/Python/vmh_facefit/          # face-fit kit: measure.py, fit.py (MediaPipe, outside the editor), ue_capture.py
├── Tests/postjam_smoke.py               # headless checks of the 2026-11-14 tools against a project with built MetaHumans
└── Skills/<pack>/SKILL.md               # metahuman (router), -setup, -creator, -wardrobe, -animator, -live-link, -shipping (native 5.9 discovery)
```

Design notes:

- Services only call APIs the MetaHuman plugins export. Where a Blueprint library lives in a
  private header (Capture Manager ingest, Take Recorder's Live Link source) the service goes through
  reflection (`VmhJson::CallStaticFunction` / `CallObjectFunction`).
- Native Creator settings edits read a settings struct, modify the copy, then commit it through the
  character editor subsystem. Use `get_symbol(name='unreal.MetaHumanCharacterEyesSettings')` to inspect a shape.
- Long operations can outlive a request. Check their status or async result before continuing or retrying.
- Nothing here needs the VibeUE plugin. The engine's own `execute_python` MCP tool is the easiest
  way to drive these services, and `SceneCaptureToolset.CaptureActiveEditorView` (or
  `CaptureGameViewport` in PIE) shows the result.

## Status

First cut, written against UE 5.8.3 headers. Not yet compiled or exercised in the editor in this
repository's history; see the commit log / issues for what has been validated.

## License

MIT — see `LICENSE`.
