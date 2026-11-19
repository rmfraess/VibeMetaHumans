// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Services/UMetaHumanLiveLinkService.h"

#include "Animation/AnimInstance.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"
#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"
#include "LiveLinkComponentController.h"
#include "LiveLinkFaceSourceBlueprint.h"
#include "LiveLinkRole.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkTypes.h"
#include "MetaHumanLiveLinkSubjectSettings.h"
#include "MetaHumanLocalLiveLinkSourceBlueprint.h"
#include "MetaHumanLocalLiveLinkSourceSettings.h"
#include "MetaHumanVideoBaseLiveLinkSubjectSettings.h"
#include "Nodes/HyprsenseRealtimeNode.h"
#include "UObject/UnrealType.h"
#include "Utils/VmhAssets.h"
#include "Utils/VmhJson.h"
#include "VibeMetaHumansModule.h"

#if WITH_VMH_TAKES
#include "LevelSequence.h"
#include "Recorder/TakeRecorderBlueprintLibrary.h"
#include "Recorder/TakeRecorderPanel.h"
#include "TakeRecorderSource.h"
#include "TakeRecorderSources.h"
#endif

namespace
{
	ILiveLinkClient* GetClient(FString& OutError)
	{
		if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			OutError = TEXT("Live Link client is not available (enable the LiveLink plugin)");
			return nullptr;
		}
		return &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	}

	TSharedPtr<FJsonObject> VideoFormatJson(const FMetaHumanLiveLinkVideoFormat& Format, int32 Index)
	{
		TSharedPtr<FJsonObject> Out = VmhJson::Obj();
		Out->SetNumberField(TEXT("index"), Index);
		Out->SetNumberField(TEXT("width"), Format.Resolution.X);
		Out->SetNumberField(TEXT("height"), Format.Resolution.Y);
		Out->SetNumberField(TEXT("fps"), Format.FrameRate);
		Out->SetStringField(TEXT("type"), Format.Type);
		Out->SetStringField(TEXT("name"), Format.Name);
		return Out;
	}

	bool PickVideoDevice(const FString& DeviceName, FMetaHumanLiveLinkVideoDevice& OutDevice, TArray<FString>& OutNames)
	{
		TArray<FMetaHumanLiveLinkVideoDevice> Devices;
		UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoDevices(Devices, /*IncludeMediaBundles*/ true);
		for (const FMetaHumanLiveLinkVideoDevice& D : Devices)
		{
			OutNames.Add(D.Name);
		}
		for (const FMetaHumanLiveLinkVideoDevice& D : Devices)
		{
			if (DeviceName.IsEmpty() || D.Name.Contains(DeviceName) || D.Url.Contains(DeviceName))
			{
				OutDevice = D;
				return true;
			}
		}
		return false;
	}

	int32 ChooseFormat(const TArray<FMetaHumanLiveLinkVideoFormat>& Formats)
	{
		// Prefer 720p-1080p at 24-60 fps; otherwise the first format.
		int32 Best = INDEX_NONE;
		float BestScore = -1.f;
		for (int32 i = 0; i < Formats.Num(); ++i)
		{
			const FMetaHumanLiveLinkVideoFormat& F = Formats[i];
			if (F.Resolution.X < 1280 || F.Resolution.X > 1920 || F.FrameRate < 24.f || F.FrameRate > 60.f)
			{
				continue;
			}
			const float Score = (F.Resolution.X == 1280 ? 2.f : 1.f) + (FMath::IsNearlyEqual(F.FrameRate, 30.f, 1.f) ? 1.f : 0.f);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = i;
			}
		}
		return Best != INDEX_NONE ? Best : (Formats.Num() > 0 ? 0 : INDEX_NONE);
	}
}

FString UMetaHumanLiveLinkService::AddVideoSource(const FString& DeviceName, const FString& SubjectName, int32 FormatIndex)
{
	FString Error;
	if (!GetClient(Error))
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	FMetaHumanLiveLinkVideoDevice Device;
	TArray<FString> Names;
	if (!PickVideoDevice(DeviceName, Device, Names))
	{
		return VmhJson::Error(TEXT("DEVICE_NOT_FOUND"), FString::Printf(TEXT("No video device matches '%s'. Devices: %s"), *DeviceName, *FString::Join(Names, TEXT(" | "))));
	}

	TArray<FMetaHumanLiveLinkVideoTrack> Tracks;
	bool bTimedOut = false;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoTracks(Device, Tracks, bTimedOut, 5.f);
	if (Tracks.Num() == 0)
	{
		return VmhJson::Error(TEXT("NO_TRACKS"), FString::Printf(TEXT("%s exposed no video tracks%s"), *Device.Name, bTimedOut ? TEXT(" (timed out)") : TEXT("")));
	}
	TArray<FMetaHumanLiveLinkVideoFormat> Formats;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoFormats(Tracks[0], Formats, bTimedOut, true, 5.f);
	const int32 Chosen = FormatIndex >= 0 ? FormatIndex : ChooseFormat(Formats);
	if (!Formats.IsValidIndex(Chosen))
	{
		return VmhJson::Error(TEXT("NO_FORMAT"), FString::Printf(TEXT("%s has %d usable formats; FormatIndex %d is invalid"), *Device.Name, Formats.Num(), FormatIndex));
	}

	FLiveLinkSourceHandle Source;
	bool bSourceOk = false;
	UMetaHumanLocalLiveLinkSourceBlueprint::CreateVideoSource(Source, bSourceOk);
	if (!bSourceOk)
	{
		return VmhJson::Error(TEXT("SOURCE_FAILED"), TEXT("CreateVideoSource failed (see Output Log)"));
	}
	const FString Subject = SubjectName.IsEmpty() ? TEXT("MetaHumanVideo") : SubjectName;
	FLiveLinkSubjectKey Key;
	bool bSubjectOk = false;
	UMetaHumanLocalLiveLinkSourceBlueprint::CreateVideoSubject(Source, Formats[Chosen], Subject, Key, bSubjectOk, 5.f, 0.1f, 5.f, FMonocularAnimationPipelineModels());
	if (!bSubjectOk)
	{
		return VmhJson::Error(TEXT("SUBJECT_FAILED"), FString::Printf(TEXT("CreateVideoSubject failed for %s (camera busy, or the real-time models are missing - check the Output Log)"), *Device.Name));
	}

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("device"), Device.Name);
	Out->SetObjectField(TEXT("format"), VideoFormatJson(Formats[Chosen], Chosen));
	Out->SetStringField(TEXT("subject"), Key.SubjectName.ToString());
	Out->SetStringField(TEXT("source"), Key.Source.ToString());
	Out->SetStringField(TEXT("next"), TEXT("capture_neutrals(subject) with a relaxed face, then bind_subject_to_actor"));
	return VmhJson::Ok(Out);
}

FString UMetaHumanLiveLinkService::AddAudioSource(const FString& DeviceName, const FString& SubjectName)
{
	FString Error;
	if (!GetClient(Error))
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	TArray<FMetaHumanLiveLinkAudioDevice> Devices;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioDevices(Devices, true);
	const FMetaHumanLiveLinkAudioDevice* Device = nullptr;
	TArray<FString> Names;
	for (const FMetaHumanLiveLinkAudioDevice& D : Devices)
	{
		Names.Add(D.Name);
		if (!Device && (DeviceName.IsEmpty() || D.Name.Contains(DeviceName) || D.Url.Contains(DeviceName)))
		{
			Device = &D;
		}
	}
	if (!Device)
	{
		return VmhJson::Error(TEXT("DEVICE_NOT_FOUND"), FString::Printf(TEXT("No audio device matches '%s'. Devices: %s"), *DeviceName, *FString::Join(Names, TEXT(" | "))));
	}
	TArray<FMetaHumanLiveLinkAudioTrack> Tracks;
	bool bTimedOut = false;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioTracks(*Device, Tracks, bTimedOut, 5.f);
	if (Tracks.Num() == 0)
	{
		return VmhJson::Error(TEXT("NO_TRACKS"), FString::Printf(TEXT("%s exposed no audio tracks"), *Device->Name));
	}
	TArray<FMetaHumanLiveLinkAudioFormat> Formats;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioFormats(Tracks[0], Formats, bTimedOut, 5.f);
	if (Formats.Num() == 0)
	{
		return VmhJson::Error(TEXT("NO_FORMAT"), FString::Printf(TEXT("%s exposed no audio formats"), *Device->Name));
	}
	FLiveLinkSourceHandle Source;
	bool bSourceOk = false;
	UMetaHumanLocalLiveLinkSourceBlueprint::CreateAudioSource(Source, bSourceOk);
	if (!bSourceOk)
	{
		return VmhJson::Error(TEXT("SOURCE_FAILED"), TEXT("CreateAudioSource failed (see Output Log)"));
	}
	const FString Subject = SubjectName.IsEmpty() ? TEXT("MetaHumanAudio") : SubjectName;
	FLiveLinkSubjectKey Key;
	bool bSubjectOk = false;
	UMetaHumanLocalLiveLinkSourceBlueprint::CreateAudioSubject(Source, Formats[0], Subject, Key, bSubjectOk, 5.f, 0.1f, 5.f);
	if (!bSubjectOk)
	{
		return VmhJson::Error(TEXT("SUBJECT_FAILED"), FString::Printf(TEXT("CreateAudioSubject failed for %s"), *Device->Name));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("device"), Device->Name);
	Out->SetNumberField(TEXT("channels"), Formats[0].NumChannels);
	Out->SetNumberField(TEXT("sample_rate"), Formats[0].SampleRate);
	Out->SetStringField(TEXT("subject"), Key.SubjectName.ToString());
	Out->SetStringField(TEXT("source"), Key.Source.ToString());
	return VmhJson::Ok(Out);
}

FString UMetaHumanLiveLinkService::ListSources()
{
	FString Error;
	ILiveLinkClient* Client = GetClient(Error);
	if (!Client)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FGuid& Guid : Client->GetSources())
	{
		TSharedPtr<FJsonObject> Item = VmhJson::Obj();
		Item->SetStringField(TEXT("guid"), Guid.ToString());
		Item->SetStringField(TEXT("type"), Client->GetSourceType(Guid).ToString());
		Item->SetStringField(TEXT("status"), Client->GetSourceStatus(Guid).ToString());
		Item->SetStringField(TEXT("machine"), Client->GetSourceMachineName(Guid).ToString());
		if (const ULiveLinkSourceSettings* Settings = Client->GetSourceSettings(Guid))
		{
			Item->SetStringField(TEXT("settings_class"), Settings->GetClass()->GetName());
		}
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetNumberField(TEXT("count"), Items.Num());
	Out->SetArrayField(TEXT("sources"), Items);
	return VmhJson::Ok(Out);
}

FString UMetaHumanLiveLinkService::RemoveSource(const FString& SourceGuid)
{
	FString Error;
	ILiveLinkClient* Client = GetClient(Error);
	if (!Client)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	TArray<FString> Removed;
	if (SourceGuid.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		for (const FGuid& Guid : Client->GetSources())
		{
			Removed.Add(Guid.ToString());
			Client->RemoveSource(Guid);
		}
	}
	else
	{
		FGuid Guid;
		if (!FGuid::Parse(SourceGuid, Guid))
		{
			return VmhJson::Error(TEXT("BAD_GUID"), FString::Printf(TEXT("'%s' is not a source guid (see list_sources) or 'all'"), *SourceGuid));
		}
		Client->RemoveSource(Guid);
		Removed.Add(Guid.ToString());
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetArrayField(TEXT("removed"), VmhJson::StringArray(Removed));
	Out->SetNumberField(TEXT("remaining"), Client->GetSources().Num());
	return VmhJson::Ok(Out);
}

// =====================================================================
// Binding and recording
// =====================================================================

FString UMetaHumanLiveLinkService::BindSubjectToActor(const FString& ActorLabel, const FString& SubjectName, bool bSwitchFaceToLiveLinkAnimBP)
{
	FString Error;
	ILiveLinkClient* Client = GetClient(Error);
	if (!Client)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	AActor* Actor = VmhAssets::FindActor(ActorLabel);
	if (!Actor)
	{
		return VmhJson::Error(TEXT("ACTOR_NOT_FOUND"), FString::Printf(TEXT("No actor labelled '%s' in the editor level"), *ActorLabel));
	}
	const FName SubjectFName(*SubjectName);
	const TSubclassOf<ULiveLinkRole> Role = Client->GetSubjectRole_AnyThread(FLiveLinkSubjectName(SubjectFName));

	TArray<FString> Set;
	auto Bind = [&Set, SubjectFName, Role](UObject* Target)
	{
		if (!Target)
		{
			return;
		}
		for (TFieldIterator<FStructProperty> It(Target->GetClass()); It; ++It)
		{
			if (It->Struct == FLiveLinkSubjectName::StaticStruct())
			{
				Target->Modify();
				It->ContainerPtrToValuePtr<FLiveLinkSubjectName>(Target)->Name = SubjectFName;
				FPropertyChangedEvent Event(*It, EPropertyChangeType::ValueSet);
				Target->PostEditChangeProperty(Event);
				Set.Add(Target->GetName() + TEXT(".") + It->GetName());
			}
			else if (It->Struct == FLiveLinkSubjectRepresentation::StaticStruct())
			{
				Target->Modify();
				FLiveLinkSubjectRepresentation* Rep = It->ContainerPtrToValuePtr<FLiveLinkSubjectRepresentation>(Target);
				Rep->Subject = FLiveLinkSubjectName(SubjectFName);
				if (Role)
				{
					Rep->Role = Role;
				}
				FPropertyChangedEvent Event(*It, EPropertyChangeType::ValueSet);
				Target->PostEditChangeProperty(Event);
				Set.Add(Target->GetName() + TEXT(".") + It->GetName());
			}
		}
	};

	Bind(Actor);
	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	TArray<FString> AnimBPSwitched;
	for (UActorComponent* Component : Components)
	{
		if (ULiveLinkComponentController* Controller = Cast<ULiveLinkComponentController>(Component))
		{
			FLiveLinkSubjectRepresentation Rep = Controller->GetSubjectRepresentation();
			Rep.Subject = FLiveLinkSubjectName(SubjectFName);
			if (Role)
			{
				Rep.Role = Role;
			}
			Controller->Modify();
			Controller->SetSubjectRepresentation(Rep);
			Set.Add(Controller->GetName() + TEXT(".SubjectRepresentation"));
			continue;
		}
		Bind(Component);

		// The assembled MetaHuman drives its face through an AnimBP (ABP_MH_LiveLink) whose LLink_Face_Subj
		// variable names the subject - the same path Epic's preview actor uses.
		if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component))
		{
			const bool bIsFace = SkelComp->GetName().Contains(TEXT("Face")) ||
				(SkelComp->GetSkeletalMeshAsset() && SkelComp->GetSkeletalMeshAsset()->GetName().Contains(TEXT("Face")));
			UAnimInstance* AnimInstance = SkelComp->GetAnimInstance();
			const bool bHasSubjectVar = AnimInstance && AnimInstance->GetClass()->FindPropertyByName(TEXT("LLink_Face_Subj")) != nullptr;
			if (bIsFace && !bHasSubjectVar && bSwitchFaceToLiveLinkAnimBP)
			{
				if (UClass* LiveLinkAnimClass = LoadObject<UClass>(nullptr, TEXT("/MetaHumanCharacter/Animation/ABP_MH_LiveLink.ABP_MH_LiveLink_C")))
				{
					SkelComp->Modify();
					SkelComp->SetAnimInstanceClass(LiveLinkAnimClass);
					AnimInstance = SkelComp->GetAnimInstance();
					AnimBPSwitched.Add(SkelComp->GetName());
				}
			}
			if (AnimInstance)
			{
				Bind(AnimInstance);
			}
		}
	}

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("actor"), Actor->GetPathName());
	Out->SetStringField(TEXT("subject"), SubjectName);
	Out->SetStringField(TEXT("role"), Role ? Role->GetName() : TEXT(""));
	Out->SetArrayField(TEXT("properties_set"), VmhJson::StringArray(Set));
	Out->SetArrayField(TEXT("anim_blueprint_switched_on"), VmhJson::StringArray(AnimBPSwitched));
	if (Set.Num() == 0)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK_INPUT"),
			TEXT("The actor exposes no Live Link subject property (neither on the actor, its components, nor a face AnimBP with LLink_Face_Subj). "
			     "Use an assembled MetaHuman Blueprint (MetaHumanCharacterService.build), or add a LiveLinkComponentController."), Out);
	}
	Out->SetStringField(TEXT("hint"), TEXT("In the editor the pose updates live; in PIE/runtime make sure the Live Link source is still present and the face AnimBP is the Live Link one."));
	return VmhJson::Ok(Out);
}

