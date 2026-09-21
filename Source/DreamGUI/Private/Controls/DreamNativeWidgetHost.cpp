// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamNativeWidgetHost.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Extensions/DreamUMGWidget.h"
#include "Extensions/DreamUMGWidgetInteraction.h"

void UDreamNativeWidgetHost::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Host"), HostNode);
}

void UDreamNativeWidgetHost::RealizeBuiltIn()
{
	using namespace DreamUI;

	// One node, stretched: the hosted widget is drawn at this control's size, so anything smaller
	// would be a second size to keep in step with the first.
	Realize(this,
		Node<UDreamUMGWidget>("Host").Stretch());
}

void UDreamNativeWidgetHost::WireParts()
{
	HostVisual = HostNode != nullptr ? Cast<UDreamUMGWidget>(HostNode->GetVisual()) : nullptr;
	if (HostNode != nullptr)
	{
		// Pointer events go INTO the hosted widget, which is the half that makes this a host rather
		// than a picture of one. EnsureComponent rather than an assumption, for the template road:
		// a node somebody drew has whatever components they added and not this one.
		EnsureComponent<UDreamUMGWidgetInteraction>(HostNode);
	}
}

void UDreamNativeWidgetHost::ApplyStyle()
{
	// No style struct and no ResolveStyle call: there is nothing about a host that a project theme
	// has an opinion on -- what it looks like is entirely the hosted widget's business. ApplyStyle is
	// overridden anyway because it is where a details-panel edit lands, and the two knobs this
	// control does have need pushing from somewhere.
	if (HostVisual == nullptr)
	{
		return;
	}
	HostVisual->SetResolutionScale(FMath::Max(ResolutionScale, 0.05f));
	HostVisual->SetBackgroundColor(BackgroundColor);
	HostVisual->SetWidgetClass(WidgetClass);
}

void UDreamNativeWidgetHost::SetWidgetClass(TSubclassOf<UUserWidget> InWidgetClass)
{
	if (WidgetClass == InWidgetClass)
	{
		return;
	}
	WidgetClass = InWidgetClass;
	if (HostVisual != nullptr)
	{
		// Straight to the bridge: it owns the instance's lifetime and re-creates it on a class change.
		HostVisual->SetWidgetClass(WidgetClass);
	}
}

UUserWidget* UDreamNativeWidgetHost::GetHostedWidget() const
{
	return HostVisual != nullptr ? HostVisual->GetUserWidgetObject() : nullptr;
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "NativeWidgetHost", UDreamNativeWidgetHost)
