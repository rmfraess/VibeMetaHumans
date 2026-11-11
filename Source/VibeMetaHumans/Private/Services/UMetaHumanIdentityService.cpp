// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Services/UMetaHumanIdentityService.h"

#include "CaptureData.h"
#include "DNACommon.h"
#include "MetaHumanIdentity.h"
#include "MetaHumanIdentityParts.h"
#include "MetaHumanIdentityPose.h"
#include "MetaHumanIdentityPromotedFrames.h"
#include "Misc/Paths.h"
#include "PromotedFrameUtils.h"
#include "UObject/Package.h"
#include "Utils/VmhAssets.h"
#include "Utils/VmhJson.h"
#include "VibeMetaHumansModule.h"

namespace
{
	const TCHAR* IdentityFactoryPath = TEXT("/Script/MetaHumanIdentityEditor.MetaHumanIdentityFactoryNew");

	UMetaHumanIdentity* LoadIdentity(const FString& Path, FString& OutError)
	{
		UObject* Object = VmhAssets::Load(Path);
		UMetaHumanIdentity* Identity = Cast<UMetaHumanIdentity>(Object);
		if (!Identity)
		{
			OutError = Object
				? FString::Printf(TEXT("%s is a %s, not a MetaHumanIdentity"), *Path, *Object->GetClass()->GetName())
				: FString::Printf(TEXT("No MetaHuman Identity at %s"), *Path);
		}
		return Identity;
	}

	UMetaHumanIdentityFace* GetFace(UMetaHumanIdentity* Identity, bool bCreate)
	{
		UMetaHumanIdentityPart* Part = bCreate
			? Identity->GetOrCreatePartOfClass(UMetaHumanIdentityFace::StaticClass())
			: Identity->FindPartOfClass(UMetaHumanIdentityFace::StaticClass());
		return Cast<UMetaHumanIdentityFace>(Part);
	}

	TSharedPtr<FJsonObject> StatusJson(UMetaHumanIdentity* Identity)
	{
		TSharedPtr<FJsonObject> Out = VmhJson::Obj();
		Out->SetStringField(TEXT("identity"), Identity->GetPathName());
		Out->SetBoolField(TEXT("logged_in"), Identity->IsLoggedInToService());
		Out->SetBoolField(TEXT("auto_rig_in_progress"), Identity->IsAutoRiggingInProgress());
		Out->SetBoolField(TEXT("tracking_in_progress"), Identity->IsFrameTrackingPipelineProcessing());
		FText Diagnostics;
		if (Identity->DiagnosticsIndicatesProcessingIssue(Diagnostics))
		{
			Out->SetStringField(TEXT("diagnostics_warning"), Diagnostics.ToString());
		}
		if (UMetaHumanIdentityFace* Face = GetFace(Identity, false))
		{
			TSharedPtr<FJsonObject> FaceJson = VmhJson::Obj();
			FaceJson->SetBoolField(TEXT("has_dna"), Face->HasDNABuffer());
			FaceJson->SetBoolField(TEXT("has_predictive_solvers"), Face->HasPredictiveSolvers());
			FaceJson->SetBoolField(TEXT("conformal_rig_valid"), Face->IsConformalRigValid());
			TArray<TSharedPtr<FJsonValue>> Poses;
			for (const UMetaHumanIdentityPose* Pose : Face->GetPoses())
			{
				if (!Pose)
				{
					continue;
				}
				TSharedPtr<FJsonObject> PoseJson = VmhJson::Obj();
				if (const UEnum* PoseEnum = StaticEnum<EIdentityPoseType>())
				{
					PoseJson->SetStringField(TEXT("type"), PoseEnum->GetNameStringByValue(static_cast<int64>(Pose->PoseType)));
				}
				PoseJson->SetBoolField(TEXT("fit_eyes"), Pose->bFitEyes);
				PoseJson->SetStringField(TEXT("camera"), Pose->Camera);
				PoseJson->SetStringField(TEXT("capture_data"), VmhAssets::PathOf(Pose->GetCaptureData()));
				PoseJson->SetNumberField(TEXT("promoted_frames"), Pose->PromotedFrames.Num());
				Poses.Add(MakeShared<FJsonValueObject>(PoseJson));
			}
			FaceJson->SetArrayField(TEXT("poses"), Poses);
			Out->SetObjectField(TEXT("face"), FaceJson);
		}
		else
		{
			Out->SetStringField(TEXT("face"), TEXT("none"));
		}
		if (UMetaHumanIdentityBody* Body = Cast<UMetaHumanIdentityBody>(Identity->FindPartOfClass(UMetaHumanIdentityBody::StaticClass())))
		{
			TSharedPtr<FJsonObject> BodyJson = VmhJson::Obj();
			BodyJson->SetNumberField(TEXT("body_type_index"), Body->BodyTypeIndex);
			BodyJson->SetNumberField(TEXT("height"), Body->Height);
			Out->SetObjectField(TEXT("body"), BodyJson);
		}
		return Out;
	}
}

FString UMetaHumanIdentityService::GetStatus(const FString& IdentityPath)
{
	FString Error;
	UMetaHumanIdentity* Identity = LoadIdentity(IdentityPath, Error);
	if (!Identity)
	{
		return VmhJson::Error(TEXT("IDENTITY_NOT_FOUND"), Error);
	}
	return VmhJson::Ok(StatusJson(Identity));
}

FString UMetaHumanIdentityService::RequestAutoRig(const FString& IdentityPath)
{
	FString Error;
	UMetaHumanIdentity* Identity = LoadIdentity(IdentityPath, Error);
	if (!Identity)
	{
		return VmhJson::Error(TEXT("IDENTITY_NOT_FOUND"), Error);
	}
	UMetaHumanIdentityFace* Face = GetFace(Identity, false);
	if (!Face || !Face->IsConformalRigValid())
	{
		return VmhJson::Error(TEXT("NOT_CONFORMED"), TEXT("The identity's face is not conformed yet (create_identity_from_footage does that)"));
	}
	if (!Identity->IsLoggedInToService())
	{
		Identity->LogInToAutoRigService();
		if (!Identity->IsLoggedInToService())
		{
			return VmhJson::Error(TEXT("NOT_LOGGED_IN"), TEXT("Not logged in to the MetaHuman service; complete the Epic login then retry"));
		}
	}
	Identity->CreateDNAForIdentity(/*bLogOnly*/ true);
	TSharedPtr<FJsonObject> Out = StatusJson(Identity);
	Out->SetStringField(TEXT("auto_rig"), TEXT("requested (asynchronous) - poll get_status"));
	return VmhJson::Ok(Out);
}

FString UMetaHumanIdentityService::ExportDna(const FString& IdentityPath, const FString& DnaOutPath, const FString& BrowsOutPath)
{
	FString Error;
	UMetaHumanIdentity* Identity = LoadIdentity(IdentityPath, Error);
	if (!Identity)
	{
		return VmhJson::Error(TEXT("IDENTITY_NOT_FOUND"), Error);
	}
	if (!Identity->ExportDNADataToFiles(DnaOutPath, BrowsOutPath))
	{
		return VmhJson::Error(TEXT("EXPORT_FAILED"), TEXT("ExportDNADataToFiles returned false (no DNA on the identity, or the paths are not writable)"));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("identity"), Identity->GetPathName());
	Out->SetStringField(TEXT("dna"), DnaOutPath);
	Out->SetStringField(TEXT("brows"), BrowsOutPath);
	return VmhJson::Ok(Out);
}
