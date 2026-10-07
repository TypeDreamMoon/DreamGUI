// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Controls/DreamMenuAnchor.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "Driver/DreamDriverLocators.h"

#include "DreamPieTestTypes.generated.h"

/**
 * A UMG user widget that is one UButton filling it, built in C++ as a widget blueprint's tree would hold it: the root
 * constructed in the widget tree before the Slate widget is taken, so TakeWidget builds the SButton under it.
 */
UCLASS(NotBlueprintable, HideDropdown)
class UDreamPieUMGButtonPage : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The button the page is. Null until the page's Slate widget has been taken. */
	UPROPERTY(Transient)
	TObjectPtr<UButton> HostedButton = nullptr;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override
	{
		if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
		{
			HostedButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("HostedButton"));
			WidgetTree->RootWidget = HostedButton;
		}
		return Super::RebuildWidget();
	}
};

/**
 * Where a UMG button's four events land, counted: the evidence that a press reached the UMG widget itself, rather than
 * only the bridge that forwards to it. Outside the play world and holding nothing of it, as a play session requires of
 * anything that outlives a step.
 */
UCLASS()
class UDreamPieUMGButtonCounter : public UObject
{
	GENERATED_BODY()

public:
	int32 HoveredCount = 0;
	int32 PressedCount = 0;
	int32 ReleasedCount = 0;
	int32 ClickedCount = 0;

	UFUNCTION()
	void HandleHovered() { ++HoveredCount; }

	UFUNCTION()
	void HandlePressed() { ++PressedCount; }

	UFUNCTION()
	void HandleReleased() { ++ReleasedCount; }

	UFUNCTION()
	void HandleClicked() { ++ClickedCount; }

	/** All four bound on InButton. */
	void Listen(UButton* InButton)
	{
		if (InButton != nullptr)
		{
			InButton->OnHovered.AddDynamic(this, &UDreamPieUMGButtonCounter::HandleHovered);
			InButton->OnPressed.AddDynamic(this, &UDreamPieUMGButtonCounter::HandlePressed);
			InButton->OnReleased.AddDynamic(this, &UDreamPieUMGButtonCounter::HandleReleased);
			InButton->OnClicked.AddDynamic(this, &UDreamPieUMGButtonCounter::HandleClicked);
		}
	}
};

/**
 * What a button wrapping a menu anchor does with its click, the UMG way: ask ShouldOpenDueToClick, and open only when it
 * says yes. The anchor is held weakly, so nothing of the play world is kept alive past the session by a test that failed
 * before it could let go.
 */
UCLASS()
class UDreamPieMenuTrigger : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UDreamMenuAnchor> Anchor;
	int32 TriggerClickedCount = 0;

	UFUNCTION()
	void HandleTriggerClicked()
	{
		++TriggerClickedCount;
		if (UDreamMenuAnchor* Pinned = Anchor.Get(); Pinned != nullptr && Pinned->ShouldOpenDueToClick())
		{
			Pinned->Open();
		}
	}
};

namespace DreamPieTests
{
	/**
	 * A locator that answers with whatever InResolve hands back when a step asks -- for a part of a control a play session
	 * builds only once it is up (a tab, a row, a header), which no locator written in the test body could name yet.
	 */
	class FLiveLocator : public IDreamElementLocator
	{
	public:
		FLiveLocator(TFunction<UDreamWidget*()> InResolve, const FString& InDescription)
			: Resolve(MoveTemp(InResolve))
			, Description(InDescription)
		{
		}

		virtual void Locate(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets) const override
		{
			if (UDreamWidget* Widget = Resolve ? Resolve() : nullptr)
			{
				OutWidgets.Add(Widget);
			}
		}

		virtual FString Describe() const override { return Description; }

	private:
		TFunction<UDreamWidget*()> Resolve;
		FString Description;
	};

	inline FDreamLocatorRef Live(TFunction<UDreamWidget*()> InResolve, const FString& InDescription)
	{
		return MakeShared<FLiveLocator>(MoveTemp(InResolve), InDescription);
	}
}
