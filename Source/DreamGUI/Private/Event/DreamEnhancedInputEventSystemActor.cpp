// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamEnhancedInputEventSystemActor.h"

#include "DreamGUI.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DreamEnhancedInputEventSystemActor"

namespace DreamEnhancedInputEventSystemActorLocal
{
	/**
	 * A copy of one of the preset's input objects for this actor alone, with every reference to an
	 * object in InReplacements turned into a reference to its replacement.
	 *
	 * In the transient package, not under the actor, and that is load-bearing. Duplication runs
	 * PostLoad on every copy, and UInputMappingContext::PostLoad upgrades an old context by moving its
	 * deprecated Mappings array over DefaultKeyMappings whenever GetLinkerCustomVersion says it predates
	 * EnhancedInputMappingContextProfileMappingsUpdate. A copy has no linker, so the question falls to
	 * its outermost package: under the actor that is the level's package, which answers from the
	 * versions the level was saved with -- and for a level saved before that format, or whose save never
	 * recorded that version at all, the answer is "older" and the copy's mappings would be emptied. The
	 * transient package was never loaded and answers the current version, which is the truth: the
	 * object copied is in memory and already in the current format.
	 *
	 * No flag is carried over from the source -- RF_Standalone would keep an editor session's copies
	 * alive for good, RF_Public and RF_WasLoaded would claim things about an asset this is not -- and
	 * every copy, subobjects included, is RF_Transient.
	 */
	template <typename TObject>
	TObject* MakeRuntimeCopy(TObject* InSource, const TMap<UObject*, UObject*>& InReplacements)
	{
		UObject* const Outer = GetTransientPackageAsObject();
		const FName CopyName = MakeUniqueObjectName(Outer, InSource->GetClass(),
			FName(*FString::Printf(TEXT("%s_RuntimeCopy"), *InSource->GetName())));
		FObjectDuplicationParameters Parameters = InitStaticDuplicateObjectParams(InSource, Outer, CopyName, RF_NoFlags);
		Parameters.ApplyFlags = RF_Transient;
		// Seeded objects are not duplicated; references to them come out as references to the seed's
		// value. That is how the copied context's mappings, default and per-profile alike, come to
		// name the copied actions.
		Parameters.DuplicationSeed = InReplacements;
		return CastChecked<TObject>(StaticDuplicateObjectEx(Parameters));
	}

	/**
	 * ClaimInputThisFrame's bits: one per button and edge -- a press and a release of one button in one
	 * frame are two inputs, not one arriving twice -- and one per wheel direction at the top.
	 */
	uint32 ButtonEdgeBit(EDreamUIMouseButtonType InButton, bool bInPressed)
	{
		const uint32 ButtonIndex = FMath::Min<uint32>(static_cast<uint32>(InButton), 13u);
		return ButtonIndex * 2u + (bInPressed ? 0u : 1u);
	}
	constexpr uint32 WheelTowardPositiveBit = 28u;
	constexpr uint32 WheelTowardNegativeBit = 29u;
}

ADreamEnhancedInputEventSystemActor::ADreamEnhancedInputEventSystemActor()
{
	// Enhanced Input has no vector axis for absolute mouse position, so the position is read every
	// frame instead of on a movement event. The legacy preset can stay tickless; this one cannot.
	// (Paused frames are covered by HandleWorldTickStart: this tick keeps the engine's pause default.)
	PrimaryActorTick.bCanEverTick = true;

	// No input-component override here: AActor::EnableInput builds one from
	// UInputSettings::GetDefaultInputComponentClass(), and OverrideInputComponentClass is APawn-only.
	// A project on Enhanced Input already has that set to UEnhancedInputComponent; BindMouseInput
	// says so plainly if it is not.

	// The four actions and the context are deliberately left empty and set on the Blueprint. Filling
	// them from a path here would be a hard reference to plugin content baked into the class, which
	// is exactly the pattern the rest of the plugin just moved away from.
	//
	// The Blueprint that fills them ships with the plugin:
	// /DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput (IMC_DreamUIInputContext plus
	// IA_Trigger / IA_TriggerRight / IA_TriggerMiddle / IA_MouseWheel, all under /DreamGUI/EnhancedInput).
	// Set Project Settings > Plugins > Dream GUI > EventSystemActorClass to THAT, not to this class --
	// this class on its own has no mapping context and therefore no mouse.
}

void ADreamEnhancedInputEventSystemActor::BeginPlay()
{
	// Before Super, whose BeginPlay makes the bindings (BindMouseInput): the actions bound there and
	// the context pushed below have to be the copies, or the copies would be mapped with nothing bound
	// to them.
	MakeRuntimeInputCopies();
	Super::BeginPlay();
	AddMappingContextToLocalPlayer();
	WorldTickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ADreamEnhancedInputEventSystemActor::HandleWorldTickStart);
}

void ADreamEnhancedInputEventSystemActor::MakeRuntimeInputCopies()
{
	using namespace DreamEnhancedInputEventSystemActorLocal;

	// The properties hold what the project set: on a first BeginPlay because nothing has touched them,
	// on a later one because EndPlay put the originals back (RestoreOriginalInputObjects).
	RuntimeActionCopies.Reset();
	OriginalsOfRuntimeCopies.Reset();
	OriginalMappingContext = nullptr;

	const UInputMappingContext* const OriginalContext = MappingContext.Get();
	TMap<UObject*, UObject*> CopyOfOriginal;
	TObjectPtr<UInputAction>* const ActionProperties[] = {
		&TriggerLeftAction,
		&TriggerRightAction,
		&TriggerMiddleAction,
		&MouseWheelAction,
	};
	for (TObjectPtr<UInputAction>* ActionProperty : ActionProperties)
	{
		UInputAction* const Original = ActionProperty->Get();
		if (!IsValid(Original))
		{
			continue;
		}
		// Only an action this actor's own context maps is swapped for a copy. The copy is reached
		// through the copied context and nothing else; an action that only some other context maps
		// would go deaf the moment this actor bound a copy of it instead. Such an action is bound as it
		// is, and pauses as its own asset says.
		if (!IsValid(OriginalContext) || !OriginalContext->HasMappingForInputAction(Original))
		{
			UE_LOG(DreamGUI, Log, TEXT("[%s].%d '%s' is not mapped by the mapping context of '%s', so it is bound as it is and follows its own bTriggerWhenPaused, not the DreamUI pause setting."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Original->GetPathName(), *GetPathName());
			continue;
		}
		if (UObject* const* Existing = CopyOfOriginal.Find(Original))
		{
			*ActionProperty = CastChecked<UInputAction>(*Existing);//one action behind two buttons stays one action
			continue;
		}
		UInputAction* const Copy = MakeRuntimeCopy(Original, TMap<UObject*, UObject*>());
		CopyOfOriginal.Add(Original, Copy);
		RuntimeActionCopies.Add(Copy);
		OriginalsOfRuntimeCopies.Add(Original);
		*ActionProperty = Copy;
	}

	if (IsValid(MappingContext))
	{
		OriginalMappingContext = MappingContext;
		MappingContext = MakeRuntimeCopy(MappingContext.Get(), CopyOfOriginal);
	}
	ApplyPauseSettingToActionCopies();
}

void ADreamEnhancedInputEventSystemActor::RestoreOriginalInputObjects()
{
	TObjectPtr<UInputAction>* const ActionProperties[] = {
		&TriggerLeftAction,
		&TriggerRightAction,
		&TriggerMiddleAction,
		&MouseWheelAction,
	};
	for (TObjectPtr<UInputAction>* ActionProperty : ActionProperties)
	{
		if (UInputAction* const Original = FindOriginalOfCopy(ActionProperty->Get()))
		{
			*ActionProperty = Original;
		}
	}
	if (OriginalMappingContext != nullptr)
	{
		MappingContext = OriginalMappingContext;
	}
	RuntimeActionCopies.Reset();
	OriginalsOfRuntimeCopies.Reset();
	OriginalMappingContext = nullptr;
}

UInputAction* ADreamEnhancedInputEventSystemActor::FindOriginalOfCopy(const UInputAction* InAction) const
{
	if (InAction == nullptr)
	{
		return nullptr;
	}
	for (int32 CopyIndex = 0; CopyIndex < RuntimeActionCopies.Num(); ++CopyIndex)
	{
		if (RuntimeActionCopies[CopyIndex].Get() == InAction)
		{
			return OriginalsOfRuntimeCopies.IsValidIndex(CopyIndex) ? OriginalsOfRuntimeCopies[CopyIndex].Get() : nullptr;
		}
	}
	return nullptr;
}

const UInputAction* ADreamEnhancedInputEventSystemActor::GetOriginalAction(const UInputAction* InAction) const
{
	const UInputAction* const Original = FindOriginalOfCopy(InAction);
	return Original != nullptr ? Original : InAction;
}

void ADreamEnhancedInputEventSystemActor::ApplyPauseSettingToActionCopies()
{
	// UEnhancedPlayerInput::PrepareInputDelegatesForEvaluation reads the flag off the action on every
	// input frame ("bGamePaused && !Action->bTriggerWhenPaused" sets the frame's trigger state to None),
	// so writing it here is all it takes; nothing has to be rebuilt. The originals are left alone: they
	// are the project's objects, often loaded assets, and pause as they say.
	const bool bShouldTrigger = ShouldReceiveInputWhilePaused();
	for (const TObjectPtr<UInputAction>& Copy : RuntimeActionCopies)
	{
		if (IsValid(Copy) && Copy->bTriggerWhenPaused != bShouldTrigger)
		{
			Copy->bTriggerWhenPaused = bShouldTrigger;
		}
	}
}

void ADreamEnhancedInputEventSystemActor::HandleWorldTickStart(UWorld* InWorld, ELevelTick InTickType, float InDeltaSeconds)
{
	// Every world's tick starts on this delegate; only this actor's world is its business.
	if (InWorld == nullptr || InWorld != GetWorld())
	{
		return;
	}
	ApplyPauseSettingToActionCopies();

	// A paused frame runs only what ticks while paused, and this actor's own tick -- where the pointer
	// is followed the rest of the time -- keeps the engine's default and does not; a pause menu still
	// wants the pointer followed, so it is followed from here while the world is paused.
	if (InWorld->IsPaused() && IsValid(InputModule))
	{
		InputModule->InputMouseMove(GetPointerPosition());
	}
}

void ADreamEnhancedInputEventSystemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickStartHandle);
	WorldTickStartHandle.Reset();
	// The copy that was pushed comes off first, while MappingContext still names it; only then do the
	// properties go back to what the project set.
	RemoveMappingContextFromLocalPlayer();
	RestoreOriginalInputObjects();
	Super::EndPlay(EndPlayReason);
}

UEnhancedInputLocalPlayerSubsystem* ADreamEnhancedInputEventSystemActor::GetEnhancedInputSubsystem()const
{
	// This actor's player, not merely the first one: the event system already knows whose input it
	// handles, and on a split screen the first controller is somebody else's.
	const UDreamEventSystem* Events = GetEventSystem();
	const APlayerController* PlayerController = Events != nullptr ? Events->GetPlayerController() : nullptr;
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

void ADreamEnhancedInputEventSystemActor::AddMappingContextToLocalPlayer()
{
	if (!IsValid(MappingContext))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d '%s' has no MappingContext, so the mouse actions will never fire. This class ships with its context and actions empty for a Blueprint to fill; use /DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput, or set MappingContext and the four action properties on your own subclass."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName());
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (Subsystem == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No local player yet; the mapping context was not added."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	Subsystem->AddMappingContext(MappingContext, MappingContextPriority);
}

void ADreamEnhancedInputEventSystemActor::RemoveMappingContextFromLocalPlayer()
{
	// The other half of AddMappingContext, which nothing anywhere in the plugin used to call. A
	// ULocalPlayer outlives the level it was playing, and so does its Enhanced Input subsystem: the
	// context pushed by the actor in one level was still mapping mouse buttons to this plugin's UI
	// actions in the next, where this actor no longer exists to receive them.
	if (!IsValid(MappingContext))return;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem())
	{
		Subsystem->RemoveMappingContext(MappingContext);
	}
}

void ADreamEnhancedInputEventSystemActor::BindMouseInput()
{
	// Deliberately does not call Super: the point of this class is that the mouse arrives through
	// Input Actions instead of raw keys, and binding both would deliver every click twice.
	//
	// bConsumeBoundInput does not reach these four: an Input Action decides for itself whether it
	// consumes its keys, and that lives in the asset. The flag still governs the legacy navigation and
	// touch bindings inherited from the base, which this override does not replace.
	auto* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d InputComponent is not a UEnhancedInputComponent, so no mouse input is bound. ")
			TEXT("Set DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent in DefaultInput.ini, ")
			TEXT("or place ADreamStandaloneInputEventSystemActor instead."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	// Started, Completed and Canceled rather than Triggered: these are boolean actions, and the module
	// wants the press and the release as separate calls.
	//
	// Canceled is not optional. Any trigger that can refuse to complete -- Hold, Tap, Pressed with a
	// chord -- ends there instead of at Completed, and binding only the other two meant the release was
	// never delivered: the pointer stayed logically down forever, which is a drag that can never be let
	// go of. It costs one more binding and the handler already knows which event it was.
	const auto BindButton = [this, EnhancedInput](const UInputAction* InAction, void (ADreamEnhancedInputEventSystemActor::*InHandler)(const FInputActionInstance&))
	{
		if (!IsValid(InAction))
		{
			return;
		}
		EnhancedInput->BindAction(InAction, ETriggerEvent::Started, this, InHandler);
		EnhancedInput->BindAction(InAction, ETriggerEvent::Completed, this, InHandler);
		EnhancedInput->BindAction(InAction, ETriggerEvent::Canceled, this, InHandler);
	};
	const auto BindWheel = [this, EnhancedInput](const UInputAction* InAction)
	{
		if (IsValid(InAction))
		{
			EnhancedInput->BindAction(InAction, ETriggerEvent::Triggered, this, &ADreamEnhancedInputEventSystemActor::OnMouseWheelAction);
		}
	};

	// Each action that is one of this actor's copies is bound twice, to the same handler: the copy,
	// which the keys of this actor's own context now reach and which pauses as the DreamUI setting
	// says; and the original, which a project's own context may still map a key of its own to -- a pad
	// button on the UI click, say -- and which pauses as its own asset says. Binding only the copy made
	// every such key go dead the moment the copies arrived. The handlers take one input once per frame
	// however many of the two roads it came down (ClaimInputThisFrame).
	BindButton(TriggerLeftAction, &ADreamEnhancedInputEventSystemActor::OnTriggerLeft);
	BindButton(FindOriginalOfCopy(TriggerLeftAction), &ADreamEnhancedInputEventSystemActor::OnTriggerLeft);
	BindButton(TriggerRightAction, &ADreamEnhancedInputEventSystemActor::OnTriggerRight);
	BindButton(FindOriginalOfCopy(TriggerRightAction), &ADreamEnhancedInputEventSystemActor::OnTriggerRight);
	BindButton(TriggerMiddleAction, &ADreamEnhancedInputEventSystemActor::OnTriggerMiddle);
	BindButton(FindOriginalOfCopy(TriggerMiddleAction), &ADreamEnhancedInputEventSystemActor::OnTriggerMiddle);
	BindWheel(MouseWheelAction);
	BindWheel(FindOriginalOfCopy(MouseWheelAction));
}

void ADreamEnhancedInputEventSystemActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsValid(InputModule))
	{
		InputModule->InputMouseMove(GetPointerPosition());
	}
}

bool ADreamEnhancedInputEventSystemActor::ClaimInputThisFrame(uint32 InBit)
{
	const UWorld* World = GetWorld();
	const double RealTime = World != nullptr ? World->GetRealTimeSeconds() : 0.0;
	if (ForwardedInputFrameCounter != GFrameCounter || ForwardedInputRealTime != RealTime)
	{
		ForwardedInputFrameCounter = GFrameCounter;
		ForwardedInputRealTime = RealTime;
		ForwardedInputThisFrame = 0;
	}
	const uint32 Bit = 1u << InBit;
	if ((ForwardedInputThisFrame & Bit) != 0)
	{
		return false;
	}
	ForwardedInputThisFrame |= Bit;
	return true;
}

void ADreamEnhancedInputEventSystemActor::ReportDeviceForAction(const UInputAction* InAction)
{
	// An Input Action has no FKey of its own -- the mapping context decided that -- so this used to
	// report LeftMouseButton for all three buttons unconditionally, which told every prompt bar "mouse
	// and keyboard" even when the click had arrived from a gamepad face button through a pad mapping.
	// The keys mapped to the action are knowable, and the one the player is holding is the one that
	// produced this event.
	if (InAction == nullptr)return;
	const APlayerController* PlayerController = GetEventSystem() != nullptr ? GetEventSystem()->GetPlayerController() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (PlayerController == nullptr || Subsystem == nullptr)return;

	for (const FKey& Key : Subsystem->QueryKeysMappedToAction(InAction))
	{
		if (Key.IsValid() && PlayerController->IsInputKeyDown(Key))
		{
			ReportDeviceForKey(Key);
			return;
		}
	}
	// Nothing held: this is the release of a key the press already reported. Saying nothing leaves the
	// device where the press put it, which beats guessing between the keyboard and pad spellings.
}

void ADreamEnhancedInputEventSystemActor::ForwardTrigger(const FInputActionInstance& Instance, EDreamUIMouseButtonType ButtonType)
{
	// Which trigger event arrived is what says press or release. Reading it from the value instead
	// depended on EnhancedInput.bAlwaysGetRealValueFromActionInstanceData, and a Canceled -- the reason
	// the Canceled binding exists at all -- carries no value to read either way.
	const bool bPressed = Instance.GetTriggerEvent() == ETriggerEvent::Started;
	// One press and one release per button per frame. The copy and the original of this button's action
	// are both bound here, and one key that both contexts map -- an action that lets its key through to
	// lower contexts -- starts both in the same frame; the second is that same key again, not a second
	// press.
	if (!ClaimInputThisFrame(DreamEnhancedInputEventSystemActorLocal::ButtonEdgeBit(ButtonType, bPressed)))
	{
		return;
	}
	ReportDeviceForAction(Instance.GetSourceAction());
	if (IsValid(InputModule))
	{
		InputModule->InputTrigger(GetPointerPosition(), bPressed, ButtonType);
	}
}

void ADreamEnhancedInputEventSystemActor::OnTriggerLeft(const FInputActionInstance& Instance)
{
	ForwardTrigger(Instance, EDreamUIMouseButtonType::Left);
}

void ADreamEnhancedInputEventSystemActor::OnTriggerRight(const FInputActionInstance& Instance)
{
	ForwardTrigger(Instance, EDreamUIMouseButtonType::Right);
}

void ADreamEnhancedInputEventSystemActor::OnTriggerMiddle(const FInputActionInstance& Instance)
{
	ForwardTrigger(Instance, EDreamUIMouseButtonType::Middle);
}

void ADreamEnhancedInputEventSystemActor::OnMouseWheelAction(const FInputActionValue& Value)
{
	const float AxisValue = Value.Get<float>();
	if (FMath::IsNearlyZero(AxisValue))
	{
		return;
	}
	// One notch per direction per frame, for the reason ForwardTrigger gives a button's press.
	if (!ClaimInputThisFrame(AxisValue > 0.0f
		? DreamEnhancedInputEventSystemActorLocal::WheelTowardPositiveBit
		: DreamEnhancedInputEventSystemActorLocal::WheelTowardNegativeBit))
	{
		return;
	}
	ReportDeviceForKey(EKeys::MouseWheelAxis);
	if (IsValid(InputModule))
	{
		InputModule->InputScroll(FVector2D(AxisValue, AxisValue));
	}
}

#undef LOCTEXT_NAMESPACE
