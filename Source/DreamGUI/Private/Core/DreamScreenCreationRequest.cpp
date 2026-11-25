// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#include "Core/DreamScreenCreationRequest.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamTreeBinding.h"
#include "Core/DreamGUISettings.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "RenderTimer.h"
#include "DreamGUI.h"
#include "UObject/StrongObjectPtr.h"

UDreamScreenCreationRequest* UDreamScreenUISubsystem::OpenScreenBudgeted(TSoftClassPtr<UDreamUserWidget> PageClass, APlayerController* OwningPlayer, int32 SortOrder)
{
	if (bTornDownForWorld || !GetWorld() || GetWorld()->bIsTearingDown) return nullptr;
	auto* Request = NewObject<UDreamScreenCreationRequest>(this);
	Request->Class = PageClass;
	Request->Player = OwningPlayer;
	Request->bExplicitPlayer = OwningPlayer != nullptr;
	Request->Sort = SortOrder;
	Request->Submitted = FPlatformTime::Seconds();
	Request->SubmittedFrame = GFrameCounter;
	CreationQueue.Add(Request);
	if (!CreationTick.IsValid()) CreationTick = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UDreamScreenUISubsystem::TickCreation);
	return Request;
}

double UDreamScreenUISubsystem::GetCreationBudgetMs() const
{
	const UDreamGUISettings* Settings = GetDefault<UDreamGUISettings>();
	const double Floor = FMath::Clamp(Settings->CreationBudgetMs, .1f, 16.f);
	const double Target = Settings->CreationFrameTargetMs;
	// GGameThreadTime is the previous frame's busy game-thread time, set as it was drawn; zero where nothing draws, as in
	// a run without a renderer, which then gets the most this allows.
	if (Target <= 0.)
	{
		return Floor;
	}
	const double LastGameThreadMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
	// What the frame spent on everything but creation, which is what this frame will spend on it again. Eight
	// milliseconds are kept back for what varies from one frame to the next and for a step that runs past the end:
	// with four, a handset's filled frames reached 29 ms and now and then presented a vsync late, at 50.
	const bool bCreatedLastFrame = LastCreationFrame + 1 == GFrameCounter;
	const double OtherMs = FMath::Max(0., LastGameThreadMs - (bCreatedLastFrame ? LastCreationMs : 0.));
	constexpr double MarginMs = 8.;
	return FMath::Clamp(Target - OtherMs - MarginMs, Floor, FMath::Max(Floor, Target * .5));
}

void UDreamScreenUISubsystem::TickCreation(UWorld* World, ELevelTick, float DeltaSeconds)
{
	if (World != GetWorld() || bInCreationTick) return;
	TGuardValue<bool> Guard(bInCreationTick, true);
	// DeltaSeconds is the frame before this one: from the second frame after submission on, a frame creation worked in.
	for (UDreamScreenCreationRequest* Pending : CreationQueue)
	{
		if (Pending != nullptr && GFrameCounter >= Pending->SubmittedFrame + 2)
		{
			Pending->LongestFrameMs = FMath::Max(Pending->LongestFrameMs, DeltaSeconds * 1000.);
		}
	}
	const double Budget = GetCreationBudgetMs();
	const double TickStart = FPlatformTime::Seconds();
	const double End = TickStart + Budget * .001;
	int32 Idle = 0;
	bool bSteppedThisFrame = false;
	while (!CreationQueue.IsEmpty() && !bTornDownForWorld && Idle < CreationQueue.Num() && FPlatformTime::Seconds() < End)
	{
		// Keep the current request rooted while callbacks submit/cancel other jobs.
		TStrongObjectPtr<UDreamScreenCreationRequest> Request(CreationQueue[0].Get());
		CreationQueue.RemoveAt(0, 1, EAllowShrinking::No);
		const bool bWaiting = Request->SubmittedFrame == GFrameCounter ||
			(Request->State == EDreamScreenCreationState::Loading && Request->Load && !Request->Load->HasLoadCompleted() && !Request->bCancel && Request->OwnerValid(*this));
		// Publishing attaches the whole page, and the canvas then builds every one of its widgets' geometry later in the
		// same frame: a frame that publishes does nothing else here, and stops after it.
		const bool bPublishes = !bWaiting && Request->IsAboutToPublish();
		if (bPublishes && bSteppedThisFrame)
		{
			CreationQueue.Insert(Request.Get(), 0);
			break;
		}
		if (!bWaiting)
		{
			bSteppedThisFrame = true;
			const double Start = FPlatformTime::Seconds();
			Request->Step(*this);
			const double Ms = (FPlatformTime::Seconds() - Start) * 1000.;
			Request->CpuMs += Ms;
			Request->Costs[static_cast<uint8>(Request->Work)] += Ms;
			if (Request->State == EDreamScreenCreationState::Ready)
			{
				const double* T = Request->Costs;
				// Once a page, in the log file and not on the console; the step that went over the budget is Verbose,
				// because a page can have dozens of them and every game that opens pages would print them all.
				// longest-frame is the longest frame before this one; the frame that publishes ends after this line.
				UE_LOG(DreamGUI, Log, TEXT("Budgeted screen %s: submit-to-publish=%.2fms frames=%llu longest-frame=%.2fms step-wall=%.2fms queue=%.2fms class-wait=%.2fms allocate=%.2fms instance=%.2fms bind=%.2fms callbacks=%.2fms collect=%.2fms register=%.2fms reconcile=%.2fms begin=%.2fms publish/notify=%.2fms"),
					*Request->Class.GetAssetName(), (FPlatformTime::Seconds()-Request->Submitted)*1000., GFrameCounter - Request->SubmittedFrame, Request->LongestFrameMs, Request->CpuMs,
					(Request->LoadStarted-Request->Submitted)*1000., (Request->LoadedAt-Request->LoadStarted)*1000., T[1], T[2], T[3], T[4], T[5], T[6], T[7], T[8], T[9]);
			}
			if (Ms > Budget) UE_LOG(DreamGUI, Verbose, TEXT("Screen creation over budget: %s %s %s %.2f ms"), *Request->Class.GetAssetName(), *Request->StepClass.ToString(), Request->Phase, Ms);
		}
		Idle = bWaiting ? Idle + 1 : 0;
		if (!Request->Terminal()) CreationQueue.Add(Request.Get());
		if (bPublishes)
		{
			break;
		}
	}
	LastCreationMs = (FPlatformTime::Seconds() - TickStart) * 1000.;
	LastCreationFrame = GFrameCounter;
	if (bTornDownForWorld)
	{
		for (UDreamScreenCreationRequest* R : CreationQueue) R->Finish(EDreamScreenCreationState::Cancelled);
		CreationQueue.Reset();
	}
	if (CreationQueue.IsEmpty())
	{
		FWorldDelegates::OnWorldPostActorTick.Remove(CreationTick);
		CreationTick.Reset();
	}
}

UDreamUserWidget* UDreamScreenCreationRequest::GetPage() const { return Result.Get(); }
void UDreamScreenCreationRequest::Cancel() { if (!Terminal()) bCancel = true; }
bool UDreamScreenCreationRequest::Terminal() const { return State >= EDreamScreenCreationState::Ready; }
bool UDreamScreenCreationRequest::IsAboutToPublish() const
{
	// Every stage Step runs before "publish", in its order, is behind it.
	return State == EDreamScreenCreationState::Creating && !bCancel && InitStack.IsEmpty() && bCollected
		&& RegisterIndex >= RegisterList.Num() && bReconciled && BeginIndex >= RegisterList.Num();
}
bool UDreamScreenCreationRequest::OwnerValid(const UDreamScreenUISubsystem& Screens) const
{
	return !bExplicitPlayer || (Player.IsValid() && Player->GetWorld() == Screens.GetWorld() && Player->GetLocalPlayer());
}
void UDreamScreenCreationRequest::Finish(EDreamScreenCreationState Outcome)
{
	if (Terminal()) return;
	State = Outcome;
	if (Load && !Load->HasLoadCompleted()) Load->CancelHandle();
	Load.Reset();
	// An unfinished nested tree has not yet been attached to its host. Release these
	// roots explicitly as DestroyWidget only walks attached children.
	if (Outcome != EDreamScreenCreationState::Ready)
	{
		for (int32 I = InitStack.Num()-1; I >= 0; --I)
		{
			if (auto* W = InitStack[I].Widget.Get())
			{
				auto* Root = W->GetContentRoot();
				if (IsValid(Root) && !Root->GetParent()) Root->DestroyWidget();
			}
		}
	}
	InitStack.Reset(); RegisterList.Reset();
	if (IsValid(Staging) && Outcome != EDreamScreenCreationState::Ready) Staging->DestroyWidget();
	Staging = nullptr;
	OnFinished.Broadcast(this);
}

void UDreamScreenCreationRequest::Step(UDreamScreenUISubsystem& Screens)
{
	if (bCancel || !OwnerValid(Screens) || Screens.GetWorld()->bIsTearingDown) { Finish(EDreamScreenCreationState::Cancelled); return; }
	if (State == EDreamScreenCreationState::Queued)
	{
		Phase = TEXT("load"); Work = EWork::Load;
		if (Class.IsNull()) { Finish(EDreamScreenCreationState::Failed); return; }
		LoadStarted = FPlatformTime::Seconds();
		State = EDreamScreenCreationState::Loading;
		if (!Class.Get()) Load = UAssetManager::GetStreamableManager().RequestAsyncLoad(Class.ToSoftObjectPath());
		return;
	}
	if (State == EDreamScreenCreationState::Loading)
	{
		Phase = TEXT("allocate"); Work = EWork::Allocate;
		if (!Class.Get() || !Class.Get()->IsChildOf(UDreamUserWidget::StaticClass()) || Class.Get()->HasAnyClassFlags(CLASS_Abstract)) { Finish(EDreamScreenCreationState::Failed); return; }
		LoadedAt = FPlatformTime::Seconds();
		Staging = BeginCreateDreamWidget(Screens.GetWorld(), Class.Get());
		if (!Staging) { Finish(EDreamScreenCreationState::Failed); return; }
		Staging->SetWidgetActive(false);
		Staging->SetOwningPlayer(Player.Get());
		// While the tree is still empty: its visuals then register with the page's canvas as they arrive, and
		// publication has none of them to move.
		Screens.ApplyPageCanvas(Staging, Sort);
		InitStack.AddDefaulted_GetRef().Widget = Staging.Get();
		State = EDreamScreenCreationState::Creating;
		return;
	}
	if (!IsValid(Staging) || Staging->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists)) { Finish(EDreamScreenCreationState::Cancelled); return; }
	if (!InitStack.IsEmpty())
	{
		FInitFrame& F = InitStack.Last();
		auto* Widget = F.Widget.Get();
		if (!Widget || Widget->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists)) { Finish(EDreamScreenCreationState::Failed); return; }
		StepClass = Widget->GetClass()->GetFName();
		if (!F.bStarted)
		{
			Phase = TEXT("instance tree"); Work = EWork::Instance;
			if (!Widget->BeginTreeInitialization()) { InitStack.Pop(EAllowShrinking::No); return; }
			F.bStarted = true;
			F.Binding = UDreamWidgetGeneratedClass::BeginBinding(Widget, Widget->GetClass(), UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(Widget->GetClass()));
			return;
		}
		Phase = TEXT("bind tree"); Work = EWork::Bind;
		UDreamUserWidget* Child = nullptr;
		if (F.Binding && F.Binding->Stage == 0) Phase = TEXT("parent links");
		if (F.Binding && F.Binding->Stage == 1) Phase = TEXT("reference fixup");
		if (F.Binding && UDreamWidgetGeneratedClass::AdvanceBinding(*F.Binding, Child))
		{
			if (Child) InitStack.AddDefaulted_GetRef().Widget = Child;
			return;
		}
		Phase = TEXT("initialize callbacks"); Work = EWork::Callbacks;
		if (F.FinishPhase == 0) { ++F.FinishPhase; F.Binding.Reset(); Widget->FinishTreeCallbacks(); return; }
		if (F.FinishPhase == 1) { ++F.FinishPhase; Phase = TEXT("named slots"); Widget->FinishTreeSlots(); return; }
		Phase = TEXT("runtime bindings");
		Widget->FinishTreeBindings();
		InitStack.Pop(EAllowShrinking::No);
		return;
	}
	if (!bCollected)
	{
		Phase = TEXT("stage registration"); Work = EWork::Collect;
		// Detached, no viewport or canvas parent. Suppress activity before registering components;
		// don't begin play or attach to a player's screen between slices.
		Staging->SetWidgetActive(false);
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(Staging, Widgets, true);
		for (auto* W : Widgets) RegisterList.Emplace(W);
		bCollected = true;
		return;
	}
	if (RegisterIndex < RegisterList.Num())
	{
		Phase = TEXT("register"); Work = EWork::Register;
		if (auto* W = RegisterList[RegisterIndex++].Get()) { StepClass = W->GetClass()->GetFName(); W->OnRegister(); }
		return;
	}
	if (!bReconciled)
	{
		Phase = TEXT("reconcile"); Work = EWork::Reconcile;
		ReconcileDreamWidgetHierarchy(Staging);
		const auto* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Screens.GetWorld());
		if (!Manager || !Manager->HasBegunPlay()) BeginIndex = RegisterList.Num();
		bReconciled = true;
		return;
	}
	if (BeginIndex < RegisterList.Num())
	{
		Phase = TEXT("begin play"); Work = EWork::BeginPlay;
		BeginRegisteredDreamWidget(RegisterList[BeginIndex++].Get());
		return;
	}
	Phase = TEXT("publish"); Work = EWork::Publish;
	if (bCancel || Screens.GetWorld()->bIsTearingDown || !OwnerValid(Screens)) { Finish(EDreamScreenCreationState::Cancelled); return; }
	Screens.AddToViewport(Staging, Sort);
	if (bCancel || Screens.GetWorld()->bIsTearingDown || !OwnerValid(Screens))
	{
		if (Screens.IsInViewport(Staging)) Screens.RemoveFromViewport(Staging);
		Finish(EDreamScreenCreationState::Cancelled);
		return;
	}
	if (!Screens.IsInViewport(Staging)) { Finish(EDreamScreenCreationState::Failed); return; }
	Result = Staging.Get();
	Finish(EDreamScreenCreationState::Ready);
}
