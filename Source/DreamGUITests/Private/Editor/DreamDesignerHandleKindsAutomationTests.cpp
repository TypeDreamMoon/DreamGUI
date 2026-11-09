// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Designer/DreamDesignerEffectHandles.h"
#include "Designer/DreamDesignerGradientHandles.h"
#include "Designer/DreamDesignerHandles.h"

/*
 * One kind of viewport handles at a time (FDreamDesignerHandles): the active provider is shown when the selection has its
 * handles, the first that has any when it has not, none when the active one is Off; Tab steps to the next, then Off, then
 * the first again. And the editor module's providers have names to show, and its kinds are registered.
 */
namespace DreamDesignerHandleKindsTestLocal
{
	class FNamedProvider : public IDreamDesignerHandleProvider
	{
	public:
		explicit FNamedProvider(FName InName) : Name(InName) {}
		virtual FName GetName() const override { return Name; }
		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override {}
		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override {}
		virtual FText GetTransactionName(int32 InHandleId) const override { return FText::GetEmpty(); }
		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override { return false; }

	private:
		FName Name;
	};

	/** The active provider as it was, put back when the test ends. */
	struct FScopedActiveHandles
	{
		FName Before = FDreamDesignerHandles::GetActive();
		~FScopedActiveHandles() { FDreamDesignerHandles::SetActive(Before); }
	};

	FName ShownName(TConstArrayView<TSharedRef<IDreamDesignerHandleProvider>> InAvailable)
	{
		const TSharedPtr<IDreamDesignerHandleProvider> Shown = FDreamDesignerHandles::ChooseShown(InAvailable);
		return Shown.IsValid() ? Shown->GetName() : FDreamDesignerHandles::Off;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerHandleKindsTest,
	"DreamGUI.Editor.DesignerHandles.OneKindIsShownAndTabStepsThroughThemThenNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerHandleKindsTest::RunTest(const FString&)
{
	using namespace DreamDesignerHandleKindsTestLocal;
	FScopedActiveHandles Restore;
	const TArray<TSharedRef<IDreamDesignerHandleProvider>> Available = {
		MakeShared<FNamedProvider>(TEXT("A")), MakeShared<FNamedProvider>(TEXT("B")), MakeShared<FNamedProvider>(TEXT("C")) };

	FDreamDesignerHandles::SetActive(NAME_None);
	TestEqual(TEXT("With none asked for, the first is shown"), ShownName(Available), FName(TEXT("A")));
	FDreamDesignerHandles::SetActive(TEXT("B"));
	TestEqual(TEXT("The one asked for is shown"), ShownName(Available), FName(TEXT("B")));
	TestEqual(TEXT("One the selection has no handles of gives way to the first"), ShownName(TArray<TSharedRef<IDreamDesignerHandleProvider>>({ Available[0], Available[2] })), FName(TEXT("A")));
	TestEqual(TEXT("...and stays asked for, for the next selection"), FDreamDesignerHandles::GetActive(), FName(TEXT("B")));
	FDreamDesignerHandles::SetActive(FDreamDesignerHandles::Off);
	TestEqual(TEXT("Off shows none"), ShownName(Available), FDreamDesignerHandles::Off);

	int32 Told = 0;
	const FDelegateHandle Listening = FDreamDesignerHandles::OnActiveChanged().AddLambda([&Told]() { ++Told; });
	FDreamDesignerHandles::SetActive(NAME_None);
	TArray<FName> Steps;
	for (int32 Step = 0; Step < 5; ++Step)
	{
		Steps.Add(FDreamDesignerHandles::Cycle(Available));
	}
	FDreamDesignerHandles::OnActiveChanged().Remove(Listening);
	TestTrue(*FString::Printf(TEXT("Tab steps B, C, off, A, B: %s"), *FString::JoinBy(Steps, TEXT(", "), [](FName InName) { return InName.ToString(); })),
		Steps == TArray<FName>({ TEXT("B"), TEXT("C"), FDreamDesignerHandles::Off, TEXT("A"), TEXT("B") }));
	TestEqual(TEXT("Every change is told"), Told, 6);
	TestEqual(TEXT("Tab with nothing to show changes nothing"), FDreamDesignerHandles::Cycle({}), FName(TEXT("B")));

	// The editor module's providers for every widget, each with a name to show, and the kinds its tags ask for.
	for (const TSharedRef<IDreamDesignerHandleProvider>& Provider : FDreamDesignerHandles::GetProviders())
	{
		TestFalse(*FString::Printf(TEXT("%s has a name to show"), *Provider->GetName().ToString()), Provider->GetDisplayName().IsEmpty());
	}
	for (const FName Kind : { DreamDesignerGradientHandles::KindName, DreamDesignerEffectHandles::MaskKindName, DreamDesignerEffectHandles::GlassKindName,
		DreamDesignerEffectHandles::ShadowKindName, DreamDesignerEffectHandles::LightKindName })
	{
		TestTrue(*FString::Printf(TEXT("The %s kind is registered"), *Kind.ToString()), FDreamDesignerHandles::IsKindRegistered(Kind));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
