// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUITextInputTarget.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"

void DreamUITextInputRouter::SetActiveTarget(UObject* InTarget, int32 InUserIndex)
{
	if (InTarget == nullptr || !ensureMsgf(InTarget->Implements<UDreamUITextInputTarget>(),
		TEXT("%s claimed the keyboard but does not take text input"), *InTarget->GetPathName()))
	{
		return;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(InTarget);
	if (Input == nullptr)
	{
		return;
	}
	// A field is one player's at a time: claiming a player's keyboard lets go of any other player's it held.
	ClearActiveTarget(InTarget);
	// Only a player who is somebody (UDreamUIInputSubsystem::HasPlayerAt): a claim for an index nobody is would make a
	// phantom player to hold it, whose keyboard nobody types on.
	if (!Input->HasPlayerAt(FMath::Max(0, InUserIndex)))
	{
		return;
	}
	if (UDreamUIInputUser* User = Input->GetOrCreateUser(FMath::Max(0, InUserIndex)))
	{
		User->SetTextTarget(InTarget);
	}
}

void DreamUITextInputRouter::ClearActiveTarget(const UObject* InTarget)
{
	const UDreamUIInputSubsystem* Input = InTarget != nullptr ? UDreamUIInputSubsystem::Get(InTarget) : nullptr;
	if (Input == nullptr)
	{
		return;
	}
	TArray<UDreamUIInputUser*> Users;
	Input->GetUsers(Users);
	for (UDreamUIInputUser* User : Users)
	{
		User->ClearTextTarget(InTarget);
	}
}

IDreamUITextInputTarget* DreamUITextInputRouter::GetActiveTarget(const UObject* InWorldContext, int32 InUserIndex)
{
	const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(InWorldContext);
	const UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(InUserIndex) : nullptr;
	return User != nullptr ? Cast<IDreamUITextInputTarget>(User->GetTextTarget()) : nullptr;
}

bool DreamUITextInputRouter::RouteCharacter(const UObject* InWorldContext, int32 InUserIndex, TCHAR InCharacter)
{
	if (IDreamUITextInputTarget* Target = GetActiveTarget(InWorldContext, InUserIndex))
	{
		return Target->InsertTextCharacter(InCharacter);
	}
	return false;
}

int32 DreamUITextInputRouter::GetUserIndexForController(const UGameViewportClient* InViewportClient, int32 InControllerId)
{
	const ULocalPlayer* LocalPlayer = GEngine != nullptr && InViewportClient != nullptr
		? GEngine->GetLocalPlayerFromControllerId(InViewportClient, InControllerId) : nullptr;
	const UGameInstance* GameInstance = LocalPlayer != nullptr ? LocalPlayer->GetGameInstance() : nullptr;
	const int32 Index = GameInstance != nullptr ? GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer) : INDEX_NONE;
	// A controller no local player has is the first player's: a keyboard in a one-player game is controller 0, and so
	// is everything else there.
	return Index != INDEX_NONE ? Index : 0;
}

bool DreamUITextInputRouter::RouteViewportCharacter(const UGameViewportClient* InViewportClient, int32 InControllerId, TCHAR InCharacter)
{
	UDreamUIInputSubsystem* Input = InViewportClient != nullptr ? UDreamUIInputSubsystem::Get(InViewportClient->GetWorld()) : nullptr;
	return Input != nullptr && Input->HandleViewportCharacter(GetUserIndexForController(InViewportClient, InControllerId), InCharacter);
}
