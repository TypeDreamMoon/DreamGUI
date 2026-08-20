// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "CoreMinimal.h"
#include "Event/DreamPointerEventData.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIMulticastDelegateBool, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIMulticastDelegateFloat, float);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIMulticastDelegateVector2, FVector2D);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIMulticastDelegateString, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIMulticastDelegateInt32, int32);

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIMulticastDelegatePointerEventData, UDreamPointerEventData*);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIMulticastDelegateBaseEventData, UDreamBaseEventData*);