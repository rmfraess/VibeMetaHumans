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
}

