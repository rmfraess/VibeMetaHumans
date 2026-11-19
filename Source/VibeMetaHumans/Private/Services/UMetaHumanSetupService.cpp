// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Services/UMetaHumanSetupService.h"

#include "Editor.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "Interfaces/IPluginManager.h"
#include "MetaHumanBodyType.h"
#include "MetaHumanCharacterAssemblySettings.h"
#include "MetaHumanCharacterEditorModule.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorWardrobeSettings.h"
#include "MetaHumanPerformance.h"
#include "MetaHumanTypes.h"
#include "Misc/Paths.h"
#include "Utils/VmhAssets.h"
#include "Utils/VmhJson.h"
#include "Utils/VibeMetaHumansPaths.h"
#include "VibeMetaHumansModule.h"

FString UMetaHumanSetupService::GetEnumOptions()
{
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("body_type"), VmhJson::EnumNames(StaticEnum<EMetaHumanBodyType>()));
	Out->SetStringField(TEXT("rig_type"), VmhJson::EnumNames(StaticEnum<EMetaHumanRigType>()));
	Out->SetStringField(TEXT("pipeline_type"), VmhJson::EnumNames(StaticEnum<EMetaHumanDefaultPipelineType>()));
	Out->SetStringField(TEXT("quality_level"), VmhJson::EnumNames(StaticEnum<EMetaHumanQualityLevel>()));
	Out->SetStringField(TEXT("performance_input_type"), VmhJson::EnumNames(StaticEnum<EDataInputType>()));
	Out->SetStringField(TEXT("head_movement_mode"), VmhJson::EnumNames(StaticEnum<EPerformanceHeadMovementMode>()));
	if (const UEnum* Mood = FindFirstObject<UEnum>(TEXT("EAudioDrivenAnimationMood"), EFindFirstObjectOptions::NativeFirst))
	{
		Out->SetStringField(TEXT("audio_mood"), VmhJson::EnumNames(Mood));
	}
	if (const UEnum* Controls = FindFirstObject<UEnum>(TEXT("EAudioDrivenAnimationOutputControls"), EFindFirstObjectOptions::NativeFirst))
	{
		Out->SetStringField(TEXT("audio_output_controls"), VmhJson::EnumNames(Controls));
	}
	Out->SetStringField(TEXT("settings_sections"), TEXT("skin, eyes, makeup, head, face_evaluation, viewport"));
	Out->SetStringField(TEXT("wardrobe_slots"), TEXT("Hair, Eyebrows, Eyelashes, Mustache, Beard, Peachfuzz, Outfits, SkeletalMesh"));
	return VmhJson::Ok(Out);
}
