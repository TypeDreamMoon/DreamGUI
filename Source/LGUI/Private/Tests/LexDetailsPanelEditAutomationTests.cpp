// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexWidget.h"
#include "Engine/World.h"

/*
 * Editing a property in the Details panel, simulated the way the panel actually does it.
 *
 * The panel does NOT call the property's setter. It writes the property memory and then calls
 * PostEditChangeProperty, so a property whose behaviour lives only in its setter takes the value
 * and does nothing with it -- the number appears in the field and the widget does not move. That
 * failure is invisible to every test that goes through the setter, invisible at compile time, and
 * indistinguishable to an author from the feature simply not working. It cost a round of
 * "the render transform does nothing in the prefab editor" to find.
 *
 * So these reach for the property by name and poke it, exactly as the panel would.
 */

namespace LexDetailsEditTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, ULexWidget* Parent, const TCHAR* Name, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	/** Write a property and announce it, which is all the Details panel does. */
	template<typename T>
	bool EditAsDetailsPanel(FAutomationTestBase& Test, ULexWidget* Widget, const TCHAR* PropertyName, const T& NewValue)
	{
		FProperty* Property = ULexWidget::StaticClass()->FindPropertyByName(FName(PropertyName));
		if (Property == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("No property named %s on ULexWidget."), PropertyName));
			return false;
		}
		*Property->ContainerPtrToValuePtr<T>(Widget) = NewValue;
		FPropertyChangedEvent Event(Property);
		Event.SetActiveMemberProperty(Property);
		Widget->PostEditChangeProperty(Event);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexRenderTransformDetailsEditTest,
	"LGUI.Widget.DetailsEdit.RenderTransformTakesEffectFromThePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexRenderTransformDetailsEditTest::RunTest(const FString& Parameters)
{
	using namespace LexDetailsEditTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Widget = MakeWidget(TestWorld.World, Root, TEXT("Widget"), 100.0f, 100.0f);
	const FVector Before = Widget->GetWorldTransform().GetLocation();

	if (!EditAsDetailsPanel<FVector>(*this, Widget, TEXT("RenderTranslation"), FVector(0.0, 200.0, 0.0)))return false;
	TestTrue(TEXT("A translation typed into the panel moves the widget"),
		FMath::IsNearlyEqual(Widget->GetWorldTransform().GetLocation().Y, Before.Y + 200.0, 0.01));

	// Rotation and scale go through the same handler; if one is wired and another is not, the
	// symptom is a panel where some fields work and some silently do not.
	if (!EditAsDetailsPanel<FRotator>(*this, Widget, TEXT("RenderRotation"), FRotator(0.0, 45.0, 0.0)))return false;
	TestTrue(TEXT("A rotation typed into the panel rotates it"),
		Widget->HasRenderTransform() && !Widget->GetWorldTransform().GetRotation().IsIdentity());

	if (!EditAsDetailsPanel<FVector>(*this, Widget, TEXT("RenderScale"), FVector(1.0, 2.0, 2.0)))return false;
	TestTrue(TEXT("A scale typed into the panel scales it"),
		FMath::IsNearlyEqual(Widget->GetWorldTransform().GetScale3D().Y, 2.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveDetailsEditTest,
	"LGUI.Widget.DetailsEdit.PerspectiveTakesEffectFromThePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveDetailsEditTest::RunTest(const FString& Parameters)
{
	using namespace LexDetailsEditTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 400.0f, 300.0f);
	ULexWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 100.0f, 140.0f);

	TestFalse(TEXT("No scope to begin with"), Card->HasInheritedPerspective());

	// Ticking the checkbox has to reach the cached bit, which is what everything downstream reads.
	// Left to the setter alone, the box would appear ticked and the subtree would never learn of it.
	if (!EditAsDetailsPanel<bool>(*this, Table, TEXT("bPerspective"), true))return false;
	TestTrue(TEXT("Ticking Perspective in the panel establishes the scope"), Table->HasPerspectiveInHierarchy());
	TestTrue(TEXT("...and the subtree learns about it"), Card->HasInheritedPerspective());

	if (!EditAsDetailsPanel<bool>(*this, Table, TEXT("bPerspective"), false))return false;
	TestFalse(TEXT("And unticking it clears the subtree"), Card->HasInheritedPerspective());
	return true;
}

#endif
