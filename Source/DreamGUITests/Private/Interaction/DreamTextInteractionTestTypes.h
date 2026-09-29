// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"
#include "DreamTextInteractionTestTypes.generated.h"

/**
 * A behaviour that counts its own lifecycle, for asserting that a widget is LIVE rather than merely
 * registered.
 *
 * The difference is invisible to pointer events -- they are dispatched to a registered widget whether
 * or not it ever began play -- and visible only to what a behaviour does in Awake, Start and Tick. So
 * the only honest witness is a behaviour that says when each of those happened.
 */
UCLASS()
class UDreamTextLifecycleProbe : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	int32 AwakeCount = 0;
	int32 StartCount = 0;
	int32 TickCount = 0;

protected:
	virtual void Awake() override { Super::Awake(); ++AwakeCount; }
	virtual void Start() override { Super::Start(); ++StartCount; }
	virtual void Tick(float DeltaTime) override { Super::Tick(DeltaTime); ++TickCount; }
};

/**
 * Something for a control's BlueprintAssignable events to call, counting what arrived.
 *
 * A control's public events are dynamic multicast delegates, and those can only be bound to a
 * UFUNCTION on a UObject -- a lambda will not do. So the assertions about what a control TOLD its
 * listeners need a real listener, and this is it: one handler per event signature it is used with,
 * each one counting and remembering the last thing it was handed.
 *
 * Counting at the listener rather than reading the control's state is the point. A control whose
 * state moved but whose event never fired is exactly the bug a Blueprint author meets -- the graph
 * bound to OnTextCommitted simply never runs -- and only a listener can see it.
 */
UCLASS()
class UDreamTextInteractionListener : public UObject
{
	GENERATED_BODY()

public:
	int32 ClickedCount = 0;

	int32 TextChangedCount = 0;
	FString LastChangedText;
	int32 TextCommittedCount = 0;
	FString LastCommittedText;
	int32 SubmittedCount = 0;

	int32 ValueChangedCount = 0;
	float LastChangedValue = 0.0f;
	int32 ValueCommittedCount = 0;
	float LastCommittedValue = 0.0f;

	int32 KeySelectedCount = 0;
	FKey LastSelectedKey;
	int32 ListeningChangedCount = 0;
	bool bLastListening = false;

	/** UDreamButton::OnClicked. */
	UFUNCTION()
	void HandleClicked() { ++ClickedCount; }

	/** UDreamTextInput::OnTextChanged -- FDreamTextInputChangedEvent, const FString&. */
	UFUNCTION()
	void HandleTextChanged(const FString& InText) { ++TextChangedCount; LastChangedText = InText; }
	/** UDreamTextInput::OnTextCommitted -- the same signature. */
	UFUNCTION()
	void HandleTextCommitted(const FString& InText) { ++TextCommittedCount; LastCommittedText = InText; }
	/** UDreamTextInput::OnSubmitted -- the same signature, the compatibility spelling of the commit. */
	UFUNCTION()
	void HandleSubmitted(const FString& InText) { ++SubmittedCount; }

	/** UDreamSpinBox::OnValueChanged -- FDreamSpinBoxValueChangedEvent, float. */
	UFUNCTION()
	void HandleValueChanged(float InValue) { ++ValueChangedCount; LastChangedValue = InValue; }
	/** UDreamSpinBox::OnValueCommitted -- the same signature. */
	UFUNCTION()
	void HandleValueCommitted(float InValue) { ++ValueCommittedCount; LastCommittedValue = InValue; }

	/** UDreamInputKeySelector::OnKeySelected -- FDreamInputKeySelectorKeyEvent, FKey by value. */
	UFUNCTION()
	void HandleKeySelected(FKey InKey) { ++KeySelectedCount; LastSelectedKey = InKey; }
	/** UDreamInputKeySelector::OnIsListeningChanged -- FDreamInputKeySelectorListeningEvent, bool. */
	UFUNCTION()
	void HandleListeningChanged(bool bInListening) { ++ListeningChangedCount; bLastListening = bInListening; }
};
