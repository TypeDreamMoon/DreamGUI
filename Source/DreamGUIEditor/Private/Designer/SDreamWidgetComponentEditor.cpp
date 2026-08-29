// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "SDreamWidgetComponentEditor.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDesignerDetails"

namespace
{
	/**
	 * Transient state belongs to an instance, never to a copy of one. UDreamUIBehaviour caches the
	 * widget it was registered against, and GetWidget() trusts that cache ahead of its own outer, so
	 * a component whose properties were copied wholesale would keep reporting -- and keep listening
	 * to -- whichever widget the source lived on. A freshly constructed component has these null.
	 *
	 * The whole property is reset rather than its references nulled one by one: transient references
	 * live inside containers as well as in properties of their own (UUIDropdown's created-item array
	 * is one property), and an array of nulls is not safer than an array of foreign pointers -- the
	 * dropdown walks that array without a validity check. Nothing here has to be preserved; every
	 * one of these is derived again on demand.
	 */
	void DreamUIClearTransientObjectReferences(UObject* InObject)
	{
		TArray<const FStructProperty*> EncounteredStructProperties;
		for (TFieldIterator<FProperty> PropertyIt(InObject->GetClass()); PropertyIt; ++PropertyIt)
		{
			if (!PropertyIt->HasAnyPropertyFlags(CPF_Transient))continue;
			EncounteredStructProperties.Reset();
			if (!PropertyIt->ContainsObjectReference(EncounteredStructProperties, EPropertyObjectReferenceType::Strong | EPropertyObjectReferenceType::Weak))continue;
			for (int32 ArrayIndex = 0; ArrayIndex < PropertyIt->ArrayDim; ArrayIndex++)
			{
				PropertyIt->ClearValue_InContainer(InObject, ArrayIndex);
			}
		}
	}
}

/**
 * Whether a component of this class may be put on a widget at all. The clipboard is the one add path
 * that starts from an existing component rather than from the class picker, so without asking here a
 * class the picker refuses arrives on a widget by way of somewhere it was once allowed.
 *
 * Declared here rather than in the panel's header because the panel is a Slate widget no headless
 * test can construct; DreamPanelsAuditAutomationTests declares these prototypes itself.
 */
bool DreamUIWidgetComponentClipboard_CanPasteClass(const UClass* InComponentClass)
{
	// The class picker's own filter, deliberately. It looks like a menu-presentation rule, but the
	// eight native behaviours it hides are the framework's own -- UDreamPanelSlot, UDreamVisual,
	// UDreamLayout, and the helpers a control creates for itself. Those are placed by whatever owns
	// them, so a hand-pasted second one is something the owner will fight with. A component being
	// present on a widget is not evidence a user may put another one somewhere else.
	return FDreamWidgetComponentClassFilter::IsComponentClassAllowed(InComponentClass);
}

/**
 * Whether a component may be taken off a widget to be put back down elsewhere -- what Copy, Cut and
 * Duplicate each end in. Asked here as well as at the paste, because a refusal that arrives only at
 * the paste arrives too late: Cut has already deleted the component by then, and the clipboard is one
 * static shared by every panel, so an item no paste will ever accept leaves Paste greyed out for
 * everything until something else is copied over the top.
 */
bool DreamUIWidgetComponentClipboard_CanTakeComponent(const UDreamUIBehaviour* InComponent)
{
	if (!IsValid(InComponent))return false;
	return DreamUIWidgetComponentClipboard_CanPasteClass(InComponent->GetClass());
}

/**
 * A clipboard-safe stand-alone copy of InSource, so that cutting -- which deletes the component the
 * copy came from -- still leaves something to paste.
 */
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_Snapshot(UDreamUIBehaviour* InSource)
{
	if (!IsValid(InSource))return nullptr;
	auto Snapshot = NewObject<UDreamUIBehaviour>(GetTransientPackage(), InSource->GetClass(), NAME_None, RF_Transient);
	UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
	UEditorEngine::CopyPropertiesForUnrelatedObjects(InSource, Snapshot, Options);
	DreamUIClearTransientObjectReferences(Snapshot);
	return Snapshot;
}

/**
 * Recreate InSource on InTargetWidget. The component is added empty first so that the registration
 * UDreamWidget::AddComponent performs binds against the target widget's events, and only then are the
 * authored values copied over the top.
 */
UDreamUIBehaviour* DreamUIWidgetComponentClipboard_PasteOnto(UDreamWidget* InTargetWidget, UDreamUIBehaviour* InSource)
{
	if (!IsValid(InTargetWidget) || !IsValid(InSource))return nullptr;
	if (!DreamUIWidgetComponentClipboard_CanPasteClass(InSource->GetClass()))return nullptr;
	auto NewComponent = InTargetWidget->AddComponent(InSource->GetClass());
	if (!IsValid(NewComponent))return nullptr;
	UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
	UEditorEngine::CopyPropertiesForUnrelatedObjects(InSource, NewComponent, Options);
	DreamUIClearTransientObjectReferences(NewComponent);
	// Registration already happened, and UDreamUIBehaviour::OnUnregister unsubscribes through the cache
	// rather than through GetWidget(): left empty by the clear above, the component would still be
	// listening to this widget long after it was removed from it.
	NewComponent->GetWidget();
	return NewComponent;
}

/**
 * One clipboard for every panel in the editor, so a component can be pasted onto another prefab.
 *
 * Reached through an accessor rather than named directly so that emptying it is a single call the
 * module can make: a strong pointer left holding a component releases it during static teardown,
 * after the object system it releases into has gone, and only in the sessions that happened to end
 * with something on the clipboard. DreamUIWidgetComponentClipboard_Reset below is that call.
 */
TStrongObjectPtr<UDreamUIBehaviour>& DreamUIWidgetComponentClipboard()
{
	static TStrongObjectPtr<UDreamUIBehaviour> Clipboard;
	return Clipboard;
}

/** Drop the clipboard while the editor is still up. Call from module shutdown. */
void DreamUIWidgetComponentClipboard_Reset()
{
	DreamUIWidgetComponentClipboard().Reset();
}

#undef LOCTEXT_NAMESPACE
