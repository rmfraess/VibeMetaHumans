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
| Facial animation from **webcam / DSLR video** | `MetaHumanAnimatorService` | ingest a video file → Footage Capture Data → MonoFootage Performance → process → AnimSequence / LevelSequence |
| Facial animation from an **iPhone** (Live Link Face) | `MetaHumanIdentityService` + `MetaHumanAnimatorService` | ingest the take, build and auto-rig an Identity from a neutral frame, process a DepthFootage Performance |
| Facial animation from **audio only** | `MetaHumanAnimatorService` | SoundWave → Audio Performance with mood controls → process → export |
| **Real-time** face capture | `MetaHumanLiveLinkService` | webcam / microphone / Live Link Face sources, neutral calibration, head tracking, bind a subject to a placed MetaHuman, Take Recorder |
| Environment & discovery | `MetaHumanSetupService` + native asset tools | plugin and optional-content status, enum options, asset discovery and editors |
| Escape hatch | `MetaHumanObjectService` + native Object toolset | inspect or patch properties and call reflected functions |

Every Vibe service tool returns a JSON string: `{"success": true, ...}` or
`{"success": false, "error_code": "...", "error": "..."}`.

> **Optional content.** MetaHuman Creator needs the **"MetaHuman Creator Core Data"** install
> option from the Epic Games Launcher (Library → Unreal Engine 5.8 → Options). Without it there are
> no presets, no texture synthesis and no wardrobe library; `get_status()` reports
> `optional_content_installed`. Cloud steps (auto-rig, high-resolution textures) need an Epic account
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
4. Verify in the editor console: `VibeMetaHumans.ListToolsets` (expect 6 toolsets) and from the
   agent: `unreal.MetaHumanSetupService.get_status()`.

Skill packs under `Content/Skills` register as native **AgentSkills** at startup
(`ToolsetRegistry.AgentSkillToolset` → `ListSkills` / `GetSkills`), the same way VibeUE's do.
`VibeMetaHumans.ReloadSkills` re-reads them without a restart.

---

## Quick tour (editor Python)

Vibe services return JSON strings; native tools have their own result and asynchronous contracts.
See the skills for capture, authoring and recording details. Use `vibeue.get_toolset_schema` before
calling an engine tool: its arguments and results differ from the former service wrappers.

```python
import json, unreal, vibeue
setup, mh = unreal.MetaHumanSetupService, unreal.MetaHumanCharacterService
print(json.loads(setup.get_status()))
c = json.loads(mh.create_character("/Game/MetaHumans", "MH_Hero", ""))["character"]
# Add the character for native edits and release it when done; see metahuman-creator.
# After rigging and downloading textures, build synchronously:
b = json.loads(mh.build(c, "Cinematic", "Cinematic", "/Game/MetaHumans", ""))
# For long builds, use build_async instead and poll get_build_status in later calls.
print(vibeue.get_toolset_schema("CaptureManagerToolset.CaptureManagerIngestTools"))
```

Ingest and processing are asynchronous in the native toolsets; wait for completion and inspect the
result before using the capture data or exporting. Native edits need an edit session and native
settings structs, not the old JSON-patch wrapper. Use deferred save and check its final status.

Through the MCP endpoint the same tools appear as `VibeMetaHumans.MetaHumanCharacterService.CreateCharacter`
etc. (`list_toolsets` / `describe_toolset` / `call_tool`).

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
│   └── VibeMetaHumansModule.*           # reflection-based toolset registration, console commands
├── Content/Python/init_unreal.py        # registers Content/Skills as AgentSkills
├── Content/Python/vibemetahumans.py     # agent helpers (status, toolset discovery)
└── Content/Skills/<pack>/SKILL.md       # metahuman (router), -setup, -creator, -animator, -live-link
```

Design notes:

- Services only call APIs the MetaHuman plugins export. Where a Blueprint library lives in a
  private header (Capture Manager ingest, Take Recorder's Live Link source) the service goes through
  reflection (`VmhJson::CallStaticFunction` / `CallObjectFunction`), which is also what
  `MetaHumanObjectService.call_function` exposes to agents.
- Native Creator settings edits read a settings struct, modify the copy, then commit it through the
  character editor subsystem. Use `MetaHumanObjectService.describe_struct("MetaHumanCharacterEyesSettings")` to inspect a shape.
- Long operations can outlive a request. Check their status or async result before continuing or retrying.
- Nothing here needs the VibeUE plugin, but they pair well: VibeUE's `execute_python_code` is the
  easiest way to drive these services, and its `capture_image` shows the result.

## Status

First cut, written against UE 5.8.3 headers. Not yet compiled or exercised in the editor in this
repository's history; see the commit log / issues for what has been validated.

## License

MIT — see `LICENSE`.
