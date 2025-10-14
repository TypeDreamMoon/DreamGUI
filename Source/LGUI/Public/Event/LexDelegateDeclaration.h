// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "CoreMinimal.h"
#include "Event/LexPointerEventData.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FLexUIMulticastDelegateBool, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FLexUIMulticastDelegateFloat, float);
DECLARE_MULTICAST_DELEGATE_OneParam(FLexUIMulticastDelegateVector2, FVector2D);
DECLARE_MULTICAST_DELEGATE_OneParam(FLexUIMulticastDelegateString, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FLexUIMulticastDelegateInt32, int32);

DECLARE_MULTICAST_DELEGATE_OneParam(FLexUIMulticastDelegatePointerEventData, ULexPointerEventData*);
DECLARE_MULTICAST_DELEGATE_OneParam(FLexUIMulticastDelegateBaseEventData, ULexBaseEventData*);