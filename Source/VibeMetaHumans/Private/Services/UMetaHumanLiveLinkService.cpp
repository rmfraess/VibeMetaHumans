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

	const TCHAR* SubjectStateName(ELiveLinkSubjectState State)
	{
		switch (State)
		{
		case ELiveLinkSubjectState::Connected: return TEXT("Connected");
		case ELiveLinkSubjectState::Unresponsive: return TEXT("Unresponsive");
		case ELiveLinkSubjectState::Disconnected: return TEXT("Disconnected");
		case ELiveLinkSubjectState::InvalidOrDisabled: return TEXT("InvalidOrDisabled");
		case ELiveLinkSubjectState::Paused: return TEXT("Paused");
		default: return TEXT("Unknown");
		}
	}

	bool FindSubjectKey(ILiveLinkClient* Client, const FString& SubjectName, FLiveLinkSubjectKey& OutKey)
	{
		for (const FLiveLinkSubjectKey& Key : Client->GetSubjects(true, true))
		{
			if (Key.SubjectName.ToString().Equals(SubjectName, ESearchCase::IgnoreCase))
			{
				OutKey = Key;
				return true;
			}
		}
		return false;
	}

	TSharedPtr<FJsonObject> SubjectJson(ILiveLinkClient* Client, const FLiveLinkSubjectKey& Key)
	{
		TSharedPtr<FJsonObject> Out = VmhJson::Obj();
		Out->SetStringField(TEXT("subject"), Key.SubjectName.ToString());
		Out->SetStringField(TEXT("source"), Key.Source.ToString());
		const TSubclassOf<ULiveLinkRole> Role = Client->GetSubjectRole_AnyThread(Key);
		Out->SetStringField(TEXT("role"), Role ? Role->GetName() : TEXT(""));
		Out->SetStringField(TEXT("state"), SubjectStateName(Client->GetSubjectState(Key.SubjectName)));
		Out->SetBoolField(TEXT("enabled"), Client->IsSubjectEnabled(Key, false));
		if (const UObject* Settings = Client->GetSubjectSettings(Key))
		{
			Out->SetStringField(TEXT("settings_class"), Settings->GetClass()->GetName());
			Out->SetStringField(TEXT("settings_object"), Settings->GetPathName());
		}
		return Out;
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

	UObject* GetSubjectSettingsObject(ILiveLinkClient* Client, const FString& SubjectName, FLiveLinkSubjectKey& OutKey, FString& OutError)
	{
		if (!FindSubjectKey(Client, SubjectName, OutKey))
		{
			OutError = FString::Printf(TEXT("No Live Link subject named '%s' (see list_subjects)"), *SubjectName);
			return nullptr;
		}
		UObject* Settings = Client->GetSubjectSettings(OutKey);
		if (!Settings)
		{
			OutError = FString::Printf(TEXT("Subject '%s' has no settings object"), *SubjectName);
		}
		return Settings;
	}
}

// =====================================================================
// Sources
// =====================================================================

FString UMetaHumanLiveLinkService::ListVideoDevices(bool bIncludeFormats)
{
	TArray<FMetaHumanLiveLinkVideoDevice> Devices;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoDevices(Devices, true);
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FMetaHumanLiveLinkVideoDevice& D : Devices)
	{
		TSharedPtr<FJsonObject> Item = VmhJson::Obj();
		Item->SetStringField(TEXT("name"), D.Name);
		Item->SetStringField(TEXT("url"), D.Url);
		Item->SetBoolField(TEXT("media_bundle"), D.IsMediaBundle);
		if (bIncludeFormats)
		{
			TArray<FMetaHumanLiveLinkVideoTrack> Tracks;
			bool bTimedOut = false;
			UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoTracks(D, Tracks, bTimedOut, 5.f);
			TArray<TSharedPtr<FJsonValue>> FormatItems;
			if (Tracks.Num() > 0)
			{
				TArray<FMetaHumanLiveLinkVideoFormat> Formats;
				UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoFormats(Tracks[0], Formats, bTimedOut, true, 5.f);
				for (int32 i = 0; i < Formats.Num(); ++i)
				{
					FormatItems.Add(MakeShared<FJsonValueObject>(VideoFormatJson(Formats[i], i)));
				}
			}
			Item->SetBoolField(TEXT("timed_out"), bTimedOut);
			Item->SetArrayField(TEXT("formats"), FormatItems);
		}
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetNumberField(TEXT("count"), Devices.Num());
	Out->SetArrayField(TEXT("devices"), Items);
	return VmhJson::Ok(Out);
}

FString UMetaHumanLiveLinkService::ListAudioDevices()
{
	TArray<FMetaHumanLiveLinkAudioDevice> Devices;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioDevices(Devices, true);
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FMetaHumanLiveLinkAudioDevice& D : Devices)
	{
		TSharedPtr<FJsonObject> Item = VmhJson::Obj();
		Item->SetStringField(TEXT("name"), D.Name);
		Item->SetStringField(TEXT("url"), D.Url);
		Item->SetBoolField(TEXT("media_bundle"), D.IsMediaBundle);
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetNumberField(TEXT("count"), Devices.Num());
	Out->SetArrayField(TEXT("devices"), Items);
	return VmhJson::Ok(Out);
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

FString UMetaHumanLiveLinkService::AddLiveLinkFaceSource(const FString& Address, const FString& SubjectName, int32 Port)
{
	FString Error;
	if (!GetClient(Error))
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	if (Address.IsEmpty())
	{
		return VmhJson::Error(TEXT("NO_ADDRESS"), TEXT("Address is the phone's IP address shown in the Live Link Face app (e.g. 192.168.1.42)"));
	}
	FLiveLinkSourceHandle Source;
	bool bOk = false;
	ULiveLinkFaceSourceBlueprint::CreateLiveLinkFaceSource(Source, bOk);
	if (!bOk)
	{
		return VmhJson::Error(TEXT("SOURCE_FAILED"), TEXT("CreateLiveLinkFaceSource failed (see Output Log)"));
	}
	const FString Subject = SubjectName.IsEmpty() ? TEXT("iPhone") : SubjectName;
	bool bConnected = false;
	ULiveLinkFaceSourceBlueprint::Connect(Source, Subject, Address, bConnected, Port);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("address"), Address);
	Out->SetNumberField(TEXT("port"), Port);
	Out->SetStringField(TEXT("subject"), Subject);
	Out->SetBoolField(TEXT("connect_requested"), bConnected);
	if (!bConnected)
	{
		return VmhJson::Error(TEXT("CONNECT_FAILED"), TEXT("Connect returned false: check the IP, that the phone is on the same network, and that Live Link Face is open in MetaHuman Animator mode"), Out);
	}
	Out->SetStringField(TEXT("next"), TEXT("list_subjects until state is Connected, then bind_subject_to_actor"));
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

FString UMetaHumanLiveLinkService::ListSubjects()
{
	FString Error;
	ILiveLinkClient* Client = GetClient(Error);
	if (!Client)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FLiveLinkSubjectKey& Key : Client->GetSubjects(true, true))
	{
		Items.Add(MakeShared<FJsonValueObject>(SubjectJson(Client, Key)));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetNumberField(TEXT("count"), Items.Num());
	Out->SetArrayField(TEXT("subjects"), Items);
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
// Subject tuning
// =====================================================================

FString UMetaHumanLiveLinkService::CaptureNeutrals(const FString& SubjectName)
{
	FString Error;
	ILiveLinkClient* Client = GetClient(Error);
	if (!Client)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	FLiveLinkSubjectKey Key;
	UObject* Settings = GetSubjectSettingsObject(Client, SubjectName, Key, Error);
	if (!Settings)
	{
		return VmhJson::Error(TEXT("SUBJECT_NOT_FOUND"), Error);
	}
	UMetaHumanLiveLinkSubjectSettings* MH = Cast<UMetaHumanLiveLinkSubjectSettings>(Settings);
	if (!MH)
	{
		return VmhJson::Error(TEXT("NOT_METAHUMAN_SUBJECT"), FString::Printf(TEXT("Subject '%s' settings are %s, not a MetaHuman Live Link subject"), *SubjectName, *Settings->GetClass()->GetName()));
	}
	MH->CaptureNeutrals();
	TSharedPtr<FJsonObject> Out = SubjectJson(Client, Key);
	Out->SetStringField(TEXT("captured"), TEXT("neutral frame + head pose"));
	return VmhJson::Ok(Out);
}

FString UMetaHumanLiveLinkService::SetHeadTracking(const FString& SubjectName, bool bOrientation, bool bTranslation, bool bStabilization)
{
	FString Error;
	ILiveLinkClient* Client = GetClient(Error);
	if (!Client)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	FLiveLinkSubjectKey Key;
	UObject* Settings = GetSubjectSettingsObject(Client, SubjectName, Key, Error);
	if (!Settings)
	{
		return VmhJson::Error(TEXT("SUBJECT_NOT_FOUND"), Error);
	}
	if (UMetaHumanVideoBaseLiveLinkSubjectSettings* Video = Cast<UMetaHumanVideoBaseLiveLinkSubjectSettings>(Settings))
	{
		Video->SetHeadOrientation(bOrientation);
		Video->SetHeadTranslation(bTranslation);
		Video->SetHeadStabilization(bStabilization);
	}
	else
	{
		// Live Link Face (phone) subjects expose the same setters via reflection but a different class.
		TSharedPtr<FJsonObject> Result;
		TSharedPtr<FJsonObject> A = VmhJson::Obj(); A->SetBoolField(TEXT("HeadOrientation"), bOrientation);
		TSharedPtr<FJsonObject> B = VmhJson::Obj(); B->SetBoolField(TEXT("HeadTranslation"), bTranslation);
		FString CallError;
		if (!VmhJson::CallObjectFunction(Settings, TEXT("SetHeadOrientation"), A, Result, CallError) ||
			!VmhJson::CallObjectFunction(Settings, TEXT("SetHeadTranslation"), B, Result, CallError))
		{
			return VmhJson::Error(TEXT("NOT_SUPPORTED"), FString::Printf(TEXT("Subject '%s' (%s) has no head tracking toggles: %s"), *SubjectName, *Settings->GetClass()->GetName(), *CallError));
		}
	}
	TSharedPtr<FJsonObject> Out = SubjectJson(Client, Key);
	Out->SetBoolField(TEXT("head_orientation"), bOrientation);
	Out->SetBoolField(TEXT("head_translation"), bTranslation);
	Out->SetBoolField(TEXT("head_stabilization"), bStabilization);
	return VmhJson::Ok(Out);
}

FString UMetaHumanLiveLinkService::SubjectSettings(const FString& SubjectName, const FString& JsonPatch)
{
	FString Error;
	ILiveLinkClient* Client = GetClient(Error);
	if (!Client)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK"), Error);
	}
	FLiveLinkSubjectKey Key;
	UObject* Settings = GetSubjectSettingsObject(Client, SubjectName, Key, Error);
	if (!Settings)
	{
		return VmhJson::Error(TEXT("SUBJECT_NOT_FOUND"), Error);
	}
	TArray<FString> Changed;
	if (!JsonPatch.TrimStartAndEnd().IsEmpty())
	{
		FString ParseError;
		TSharedPtr<FJsonObject> Patch = VmhJson::Parse(JsonPatch, ParseError);
		if (!Patch.IsValid())
		{
			return VmhJson::Error(TEXT("INVALID_JSON"), ParseError);
		}
		if (!VmhJson::PatchObject(Settings, Patch, Changed, Error))
		{
			return VmhJson::Error(TEXT("PATCH_FAILED"), Error);
		}
	}
	TSharedPtr<FJsonObject> Out = SubjectJson(Client, Key);
	Out->SetObjectField(TEXT("settings"), VmhJson::ObjectToJson(Settings, {}));
	Out->SetArrayField(TEXT("changed"), VmhJson::StringArray(Changed));
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

FString UMetaHumanLiveLinkService::StartTakeRecording(const TArray<FString>& SubjectNames)
{
#if WITH_VMH_TAKES
	if (SubjectNames.Num() == 0)
	{
		return VmhJson::Error(TEXT("NO_SUBJECTS"), TEXT("Pass at least one Live Link subject name"));
	}
	if (UTakeRecorderBlueprintLibrary::IsRecording())
	{
		return VmhJson::Error(TEXT("ALREADY_RECORDING"), TEXT("Take Recorder is already recording; call stop_take_recording first"));
	}
	UTakeRecorderPanel* Panel = UTakeRecorderBlueprintLibrary::OpenTakeRecorderPanel();
	if (!Panel)
	{
		return VmhJson::Error(TEXT("NO_PANEL"), TEXT("Could not open the Take Recorder panel"));
	}
	UTakeRecorderSources* Sources = Panel->GetSources();
	if (!Sources)
	{
		return VmhJson::Error(TEXT("NO_SOURCES"), TEXT("Take Recorder panel has no sources object"));
	}
	UClass* LiveLinkSourceClass = VmhJson::FindClass(TEXT("/Script/LiveLinkSequencer.TakeRecorderLiveLinkSource"));
	if (!LiveLinkSourceClass)
	{
		return VmhJson::Error(TEXT("NO_LIVE_LINK_RECORDER"), TEXT("TakeRecorderLiveLinkSource class not found (LiveLink plugin's LiveLinkSequencer module)"));
	}

	// Add one Live Link recorder source per subject (the panel keeps them for later takes).
	TArray<FString> Added;
	for (const FString& Subject : SubjectNames)
	{
		UTakeRecorderSource* Source = Sources->AddSource(LiveLinkSourceClass);
		if (!Source)
		{
			continue;
		}
		if (FNameProperty* Prop = CastField<FNameProperty>(LiveLinkSourceClass->FindPropertyByName(TEXT("SubjectName"))))
		{
			Source->Modify();
			Prop->SetPropertyValue_InContainer(Source, FName(*Subject));
			FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
			Source->PostEditChangeProperty(Event);
		}
		Added.Add(Subject);
	}

	FText CannotStart;
	if (!Panel->CanStartRecording(CannotStart))
	{
		return VmhJson::Error(TEXT("CANNOT_START"), CannotStart.ToString());
	}
	Panel->StartRecording();

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetArrayField(TEXT("subjects"), VmhJson::StringArray(SubjectNames));
	Out->SetArrayField(TEXT("sources_added"), VmhJson::StringArray(Added));
	Out->SetBoolField(TEXT("recording"), UTakeRecorderBlueprintLibrary::IsRecording());
	return VmhJson::Ok(Out);
#else
	return VmhJson::Error(TEXT("TAKES_UNAVAILABLE"), TEXT("The Takes plugin was not present when VibeMetaHumans was built"));
#endif
}

FString UMetaHumanLiveLinkService::StopTakeRecording()
{
#if WITH_VMH_TAKES
	const bool bWas = UTakeRecorderBlueprintLibrary::IsRecording();
	if (bWas)
	{
		UTakeRecorderBlueprintLibrary::StopRecording();
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetBoolField(TEXT("was_recording"), bWas);
	if (UTakeRecorderPanel* Panel = UTakeRecorderBlueprintLibrary::GetTakeRecorderPanel())
	{
		Out->SetStringField(TEXT("last_recorded_sequence"), VmhAssets::PathOf(Panel->GetLastRecordedLevelSequence()));
	}
	Out->SetStringField(TEXT("hint"), TEXT("The recorded Level Sequence holds an animation track per subject; the AnimSequence assets are saved next to it under Cinematics/Takes."));
	return VmhJson::Ok(Out);
#else
	return VmhJson::Error(TEXT("TAKES_UNAVAILABLE"), TEXT("The Takes plugin was not present when VibeMetaHumans was built"));
#endif
}

FString UMetaHumanLiveLinkService::GetTakeRecorderStatus()
{
#if WITH_VMH_TAKES
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetBoolField(TEXT("enabled"), UTakeRecorderBlueprintLibrary::IsTakeRecorderEnabled());
	Out->SetBoolField(TEXT("recording"), UTakeRecorderBlueprintLibrary::IsRecording());
	if (UTakeRecorderPanel* Panel = UTakeRecorderBlueprintLibrary::GetTakeRecorderPanel())
	{
		Out->SetStringField(TEXT("last_recorded_sequence"), VmhAssets::PathOf(Panel->GetLastRecordedLevelSequence()));
		Out->SetStringField(TEXT("pending_sequence"), VmhAssets::PathOf(Panel->GetLevelSequence()));
	}
	return VmhJson::Ok(Out);
#else
	return VmhJson::Error(TEXT("TAKES_UNAVAILABLE"), TEXT("The Takes plugin was not present when VibeMetaHumans was built"));
#endif
}
