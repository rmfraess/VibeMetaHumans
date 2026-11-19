---
name: metahuman-setup
display_name: MetaHuman Setup and Discovery
description: Check the MetaHuman environment (plugins, optional content, cloud login), look up enum option names with unreal.MetaHumanSetupService, and list MetaHuman assets (characters, presets, identities, performances, capture data, wardrobe items, built Blueprints) or open their editors through Epic AssetToolset.FindAssets / EditorAppToolset.OpenEditorForAsset. Use at the start of any MetaHuman task and whenever a tool says an asset or option was not found.
vibeue_classes:
  - MetaHumanSetupService
keywords:
  - metahuman status
  - optional content
  - metahuman creator core data
  - list metahumans
  - presets
  - wardrobe items
---

# MetaHuman Setup and Discovery

```python
import json, unreal
svc = unreal.MetaHumanSetupService
```

## Environment check

Run first in any MetaHuman session (inside `execute_python`):

```python
plugins = await call_tool("PluginToolset.PluginToolset.ListEnabledPlugins")
optional = unreal.MetaHumanGeneratorSubsystemWrapper.is_optional_content_installed()
creator = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem) is not None
```

| Check | If bad |
|---|---|
| a MetaHuman plugin missing from `plugins` | Ask the user to enable it in Edit → Plugins (or the `.uproject`) and restart; nothing else fixes this |
| `optional` is False ("MetaHuman Creator Core Data" launcher option) | Presets, wardrobe items and texture synthesis are unavailable. Creator still works from the blank template; Animator and Live Link are unaffected. Tell the user how to install it (Epic Games Launcher → Library → engine → Options) |
| `creator` is False | Creator plugin not loaded |

Asset counts come from `EditorToolset.AssetToolset.FindAssets` (below); zero presets with optional
content installed means the asset registry has not scanned yet.

## Asset discovery

Use `EditorToolset.AssetToolset.FindAssets` through `vibeue.exec_tool` after inspecting
`vibeue.get_toolset_schema("EditorToolset.AssetToolset")`. Search the appropriate project or plugin
content root for characters, presets, identities, performances, capture data and wardrobe items.

Wardrobe item naming (optional content): `/MetaHumanCharacter/Optional/Grooms/Bindings/<Slot>/WI_<...>`
where `<Slot>` is `Hair`, `Eyebrows`, `Eyelashes`, `Mustaches`, `Beards`, `Peachfuzz`; clothing under
`/MetaHumanCharacter/Optional/Clothing/WI_<...>` (slot `Outfits`).

## get_enum_options()

Names accepted by the other services: `body_type` (`f_med_nrw` … `m_tal_ovw`, `BlendableBody`),
`rig_type` (`JointsOnly`, `JointsAndBlendShapes`), `pipeline_type` (`Cinematic`, `Optimized`, `UEFN`),
`quality_level` (`Low`, `Medium`, `High`, `Cinematic`), `performance_input_type` (`DepthFootage`,
`Audio`, `MonoFootage`), `head_movement_mode`, `audio_mood`, `audio_output_controls`, plus the
settings section names and wardrobe slot names.

## Open an asset editor

Use `unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets` with loaded assets to open the Character, Identity or Performance editor. Use it when the user
wants to look at a result; it is not needed for any scripted step.

## Typical opening sequence

```python
assert "MetaHumanCharacter" in await call_tool("PluginToolset.PluginToolset.ListEnabledPlugins")
import vibeue
print(vibeue.get_toolset_schema("EditorToolset.AssetToolset"))  # FindAssets arguments
```
