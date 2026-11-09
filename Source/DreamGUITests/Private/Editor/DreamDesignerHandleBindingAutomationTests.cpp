// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamDesignerGradientHandles.h"
#include "Designer/DreamDesignerHandles.h"
#include "DetailCustomization/DreamPropertyMeta.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * Handles a property asks for by its tag (meta=(DreamGUI_Handle="Kind")): a rect block's two gradients are tagged with the
 * Gradient kind, the viewport finds them on the selected widget, and each gets a provider of its own -- the same one frame
 * after frame, gone with the kind.
 */
namespace DreamDesignerHandleBindingTestLocal
{
	UDreamRectBlock* MakeBlock(UWorld* InWorld)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient | RF_Transactional);
		Widget->SetWidth(200.0f);
		Widget->SetHeight(100.0f);
		Widget->OnRegister();
		return Widget->CreateNewVisual<UDreamRectBlock>();
	}

	TArray<FName> NamesOf(TConstArrayView<TSharedRef<IDreamDesignerHandleProvider>> InProviders)
	{
		TArray<FName> Names;
		for (const TSharedRef<IDreamDesignerHandleProvider>& Provider : InProviders)
		{
			Names.Add(Provider->GetName());
		}
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerHandleBindingTest,
	"DreamGUI.Editor.DesignerHandles.APropertyTaggedWithAKindGetsAProviderOfThatKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerHandleBindingTest::RunTest(const FString&)
{
	using namespace DreamDesignerHandleBindingTestLocal;
	const FName Gradient = DreamDesignerGradientHandles::KindName;
	for (const TCHAR* Name : { TEXT("BodyGradient"), TEXT("BorderGradient") })
	{
		const FProperty* Property = UDreamRectBlock::StaticClass()->FindPropertyByName(Name);
		FString Args;
		TestEqual(*FString::Printf(TEXT("%s is tagged with the Gradient kind"), Name), DreamPropertyMeta::GetHandle(Property, &Args), Gradient);
		TestTrue(TEXT("...with no arguments"), Args.IsEmpty());
	}
	if (!TestTrue(TEXT("The editor module registers the kind"), FDreamDesignerHandles::IsKindRegistered(Gradient)))
	{
		return false;
	}

	DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Editor };
	UDreamRectBlock* Block = MakeBlock(World.World);
	if (!TestNotNull(TEXT("A rect block on a widget"), Block))
	{
		return false;
	}
	UDreamWidget* Widget = Block->GetWidget();
	TArray<FDreamDesignerHandleBinding> Bindings;
	FDreamDesignerHandles::CollectBindings(Widget, Bindings);
	const FDreamDesignerHandleBinding* Body = Bindings.FindByPredicate([](const FDreamDesignerHandleBinding& InBinding) { return InBinding.Property == TEXT("BodyGradient"); });
	const FDreamDesignerHandleBinding* Border = Bindings.FindByPredicate([](const FDreamDesignerHandleBinding& InBinding) { return InBinding.Property == TEXT("BorderGradient"); });
	TestTrue(TEXT("The widget's bindings hold its visual's two gradients"), Body != nullptr && Border != nullptr);
	TestTrue(TEXT("...on the rect block, head and property one"), Body != nullptr && Body->Owner.Get() == Block && Body->HeadProperty == Body->Property && Body->Kind == Gradient);
	TestTrue(TEXT("...the body's before the border's"), Body != nullptr && Border != nullptr && Body < Border);

	const FName BodyName = FDreamDesignerHandles::MakeBoundName(Gradient, TEXT("BodyGradient"));
	const FName BorderName = FDreamDesignerHandles::MakeBoundName(Gradient, TEXT("BorderGradient"));
	TestEqual(TEXT("A bound provider is named for its kind and its property"), BodyName, FName(TEXT("Gradient.BodyGradient")));
	const TArray<TSharedRef<IDreamDesignerHandleProvider>> First = FDreamDesignerHandles::GetProvidersFor(Widget);
	const TArray<FName> Names = NamesOf(First);
	TestTrue(*FString::Printf(TEXT("The viewport is given a provider for each gradient: %s"), *FString::JoinBy(Names, TEXT(", "), [](FName InName) { return InName.ToString(); })),
		Names.Contains(BodyName) && Names.Contains(BorderName));
	TestTrue(TEXT("...after the registered providers"), Names.Num() >= 2 && Names.Last(1) == BodyName && Names.Last() == BorderName);
	const TArray<TSharedRef<IDreamDesignerHandleProvider>> Again = FDreamDesignerHandles::GetProvidersFor(Widget);
	TestTrue(TEXT("Asked again, the same providers: a drag's state stays from frame to frame"), First == Again);

	// Gone with the kind; back with it.
	FDreamDesignerHandles::UnregisterKind(Gradient);
	const TArray<FName> Without = NamesOf(FDreamDesignerHandles::GetProvidersFor(Widget));
	FDreamDesignerHandles::RegisterKind(Gradient, DreamDesignerGradientHandles::MakeKindFactory());
	TestFalse(TEXT("With its kind unregistered a tagged property has no provider"), Without.Contains(BodyName) || Without.Contains(BorderName));
	TestTrue(TEXT("...and with it registered again, it has"), NamesOf(FDreamDesignerHandles::GetProvidersFor(Widget)).Contains(BodyName));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
