// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamScreenCreationRequest.generated.h"

class UDreamScreenCreationRequest;
class UDreamScreenUISubsystem;
class UDreamUserWidget;
class UDreamWidget;
class APlayerController;
struct FStreamableHandle;
struct FDreamTreeBinding;

UENUM(BlueprintType)
enum class EDreamScreenCreationState : uint8 { Queued, Loading, Creating, Ready, Failed, Cancelled };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamScreenCreated, UDreamScreenCreationRequest*, Request);

/** Only Ready exposes a page. Retaining this handle after completion does not retain the page. */
UCLASS(BlueprintType, Transient)
class DREAMGUI_API UDreamScreenCreationRequest : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category="DreamGUI|Screen")
	EDreamScreenCreationState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category="DreamGUI|Screen")
	UDreamUserWidget* GetPage() const;
	UFUNCTION(BlueprintCallable, Category="DreamGUI|Screen")
	void Cancel();
	UPROPERTY(BlueprintAssignable, Category="DreamGUI|Screen")
	FDreamScreenCreated OnFinished;
private:
	friend class UDreamScreenUISubsystem;
	struct FInitFrame
	{
		TWeakObjectPtr<UDreamUserWidget> Widget;
		TSharedPtr<FDreamTreeBinding> Binding;
		bool bStarted = false;
		uint8 FinishPhase = 0;
	};
	TArray<FInitFrame> InitStack;
	TArray<TWeakObjectPtr<UDreamWidget>> RegisterList;
	int32 RegisterIndex = 0;
	bool bCollected = false;
	bool bReconciled = false;
	int32 BeginIndex = 0;
	bool bCancel = false;
	bool bExplicitPlayer = false;
	EDreamScreenCreationState State = EDreamScreenCreationState::Queued;
	TSoftClassPtr<UDreamUserWidget> Class;
	TWeakObjectPtr<APlayerController> Player;
	TWeakObjectPtr<UDreamUserWidget> Result;
	UPROPERTY(Transient) TObjectPtr<UDreamUserWidget> Staging;
	TSharedPtr<FStreamableHandle> Load;
	int32 Sort = 0;
	enum class EWork : uint8 { Load, Allocate, Instance, Bind, Callbacks, Collect, Register, Reconcile, BeginPlay, Publish, Count };
	EWork Work = EWork::Load;
	double Costs[static_cast<uint8>(EWork::Count)] = {};
	double Submitted = 0, CpuMs = 0, LoadStarted = 0, LoadedAt = 0;
	FName StepClass;
	uint64 SubmittedFrame = 0;
	const TCHAR* Phase = TEXT("queue");
	bool Terminal() const;
	/** Whether the next Step attaches the page to the screen. */
	bool IsAboutToPublish() const;
	bool OwnerValid(const UDreamScreenUISubsystem& Screens) const;
	void Step(UDreamScreenUISubsystem& Screens);
	void Finish(EDreamScreenCreationState Outcome);
};
