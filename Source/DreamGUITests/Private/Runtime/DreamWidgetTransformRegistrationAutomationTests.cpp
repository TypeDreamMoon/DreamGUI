// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUserWidget.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "Driver/DreamDriverRig.h"
#include "DreamScopedWorld.h"

/*
 * WHERE A HIERARCHY STANDS ONCE IT IS REGISTERED.
 *
 * A widget's world transform is a cache. UDreamWidget::UpdateObjectToWorldTransform composes it from
 * the widget's own transform and its parent's cached one -- or, for a root, the scene component its
 * canvas follows -- and it is composed again only when something asks: a transform setter whose value
 * changed, an attach through TrySetParent, a canvas attaching to a scene component. The trees the
 * runtime builds for itself are not attached that way. CreateDreamWidget, DuplicateDreamWidgetHierarchy,
 * class instancing and every control that assembles parts of its own hang a subtree up with
 * SetParentBeforeRegister, which moves pointers and composes nothing, and then hand the finished tree to
 * RegisterDreamWidgetHierarchy. Whatever in that tree is never moved afterwards keeps what it was
 * composed against while it was being built: a control's parts were composed against a control that had
 * no parent yet, and a copy's cache is not copied at all -- only what the widget serializes is.
 *
 * A screen-space root sits at the world origin, so there the stale cache and the right one are usually
 * the same transform, which is why no screen ever showed it. A world-space panel is not at the origin:
 * a button made at its default place on a panel standing in the level stayed at the origin, and the
 * driver's world-space tests met it as "no pixel". These tests pin the rule underneath, with plain
 * widgets and the runtime's own creation roads, in numbers rather than pixels: once a hierarchy is
 * registered, every cached world transform in it is the one its chain composes to -- for a subtree
 * registered under a parent, and for a hierarchy registered as a root.
 *
 * The geometry is the widgets' defaults throughout -- centred anchors and pivot, 100 x 100 -- so a
 * child's world location is its parent's plus its anchored position, laid along the widget plane's Y
 * (across) and Z (up).
 *
 * The last test is the tween helper every widget, visual and behaviour tween goes through, handed no
 * widget: it has to choose a clock without one rather than dereference it.
 */
namespace DreamWidgetTransformRegistrationTestLocal
{
	using DreamTests::FScopedGameWorld;

	/** Where the panels and the originals stand: well off the origin, so standing AT the origin is a miss. */
	const FVector2D ParentPosition(300.0, 200.0);
	/** Where a child stands within its parent. */
	const FVector2D ChildOffset(40.0, 30.0);
	/** Every location here is a sum of a few exact authored numbers, and a real miss is hundreds of units. */
	constexpr double LocationTolerance = 0.01;

	/** The world location of something anchored at InPosition inside a parent standing at InParentAt. */
	FVector AnchoredIn(const FVector& InParentAt, const FVector2D& InPosition)
	{
		return InParentAt + FVector(0.0, InPosition.X, InPosition.Y);
	}

	UDreamWidget* MakePlainWidget(UWorld* InWorld, const TCHAR* InName)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transactional);
		Widget->SetDisplayName(InName);
		return Widget;
	}

	/**
	 * Whether InWidget's cached world transform is the one its chain composes to right now.
	 * GetLayoutWorldTransform walks the chain afresh on every call and caches nothing, which makes it the
	 * reference; the two differ only by render transforms, and nothing here has one.
	 */
	bool IsComposedFromItsChain(const UDreamWidget* InWidget)
	{
		return InWidget != nullptr
			&& InWidget->GetWorldTransform().Equals(InWidget->GetLayoutWorldTransform(), LocationTolerance);
	}

	/** Where InWidget is and where it should be, so a red says which half of the claim failed. */
	FString DescribeMiss(const UDreamWidget* InWidget, const FVector& InExpected)
	{
		return FString::Printf(TEXT("(it is at %s, it should be at %s)"),
			InWidget != nullptr ? *InWidget->GetWorldLocation().ToString() : TEXT("nowhere"), *InExpected.ToString());
	}

	void DestroyIfAlive(UDreamWidget* InWidget)
	{
		if (IsValid(InWidget))
		{
			InWidget->DestroyWidget();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetRegistrationRootPlacementTest,
	"DreamGUI.Widget.Registration.AHierarchyCopiedAsARootStandsWhereTheOriginalStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetRegistrationRootPlacementTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetTransformRegistrationTestLocal;
	FScopedGameWorld TestWorld;
	if (!TestNotNull(TEXT("A world to build in"), TestWorld.World))
	{
		return false;
	}
	UWorld* World = TestWorld.World;

	// A live original: a root and one child, placed by the anchor setters, each of which composes the
	// world transform as it goes -- so the original is right whatever registration does or does not do.
	UDreamWidget* Original = MakePlainWidget(World, TEXT("Original"));
	UDreamWidget* OriginalChild = MakePlainWidget(World, TEXT("OriginalChild"));
	OriginalChild->SetParentBeforeRegister(Original);
	RegisterDreamWidgetHierarchy(Original);
	Original->SetAnchoredPosition(ParentPosition);
	OriginalChild->SetAnchoredPosition(ChildOffset);
	UDreamWidget* Copy = nullptr;
	ON_SCOPE_EXIT
	{
		DestroyIfAlive(Copy);
		DestroyIfAlive(Original);
	};

	const FVector OriginalAt(0.0, ParentPosition.X, ParentPosition.Y);
	const FVector OriginalChildAt = AnchoredIn(OriginalAt, ChildOffset);
	if (!TestTrue(FString::Printf(TEXT("The original stands where its anchors put it %s"), *DescribeMiss(Original, OriginalAt)),
			Original->GetWorldLocation().Equals(OriginalAt, LocationTolerance))
		|| !TestTrue(FString::Printf(TEXT("... and so does its child %s"), *DescribeMiss(OriginalChild, OriginalChildAt)),
			OriginalChild->GetWorldLocation().Equals(OriginalChildAt, LocationTolerance)))
	{
		return false;
	}

	// Copied with no parent to go to, so the copy is a hierarchy root and is registered as one.
	// Duplication brings across what a widget serializes -- its relative location among it -- and not
	// the cached world transform, so every widget in the copy starts out composed against nothing.
	Copy = DuplicateDreamWidgetHierarchy(World, Original, nullptr);
	if (!TestNotNull(TEXT("The original was copied"), Copy))
	{
		return false;
	}
	UDreamWidget* CopyChild = Copy->GetChildrenCount() == 1 ? Copy->GetChildren()[0] : nullptr;
	if (!TestNotNull(TEXT("... child and all"), CopyChild))
	{
		return false;
	}
	TestNull(TEXT("The copy is a root"), Copy->GetParent());
	TestTrue(TEXT("... and a registered one, child included"), Copy->HasRegistered() && CopyChild->HasRegistered());
	TestTrue(TEXT("The copy carries the original's relative location"),
		Copy->GetRelativeLocation().Equals(Original->GetRelativeLocation(), LocationTolerance));
	TestTrue(TEXT("... and its child carries the original child's"),
		CopyChild->GetRelativeLocation().Equals(OriginalChild->GetRelativeLocation(), LocationTolerance));

	// THE CLAIM. Registering the root composed the whole copy from its own transforms, so it stands
	// exactly where the original does. Nothing else would have: neither widget in the copy is ever moved.
	TestTrue(FString::Printf(TEXT("The copy stands where the original stands %s"), *DescribeMiss(Copy, OriginalAt)),
		Copy->GetWorldLocation().Equals(OriginalAt, LocationTolerance));
	TestTrue(FString::Printf(TEXT("The copy's child stands where the original's child stands %s"), *DescribeMiss(CopyChild, OriginalChildAt)),
		CopyChild->GetWorldLocation().Equals(OriginalChildAt, LocationTolerance));
	TestTrue(TEXT("Every cached world transform in the copy is the one its chain composes to"),
		IsComposedFromItsChain(Copy) && IsComposedFromItsChain(CopyChild));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetRegistrationParentedPlacementTest,
	"DreamGUI.Widget.Registration.AControlMadeAtItsDefaultPlaceUnderAPanelStandsOnThatPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetRegistrationParentedPlacementTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetTransformRegistrationTestLocal;
	FScopedGameWorld TestWorld;
	if (!TestNotNull(TEXT("A world to build in"), TestWorld.World))
	{
		return false;
	}
	UWorld* World = TestWorld.World;

	// A panel off the origin, placed by its setter, which composes its world transform on the spot.
	UDreamWidget* Panel = MakePlainWidget(World, TEXT("Panel"));
	Panel->OnRegister();
	Panel->SetAnchoredPosition(ParentPosition);
	ON_SCOPE_EXIT
	{
		DestroyIfAlive(Panel);
	};
	const FVector PanelAt(0.0, ParentPosition.X, ParentPosition.Y);
	if (!TestTrue(FString::Printf(TEXT("The panel stands where its anchors put it %s"), *DescribeMiss(Panel, PanelAt)),
		Panel->GetWorldLocation().Equals(PanelAt, LocationTolerance)))
	{
		return false;
	}

	// The factory every control comes out of, with a part built the way a control builds its own: under
	// the control, while the control is being made -- so the part is composed against the control as it
	// stands at that moment, which is nowhere yet.
	UDreamWidget* Part = nullptr;
	UDreamUserWidget* Control = CreateDreamWidget(World, UDreamUserWidget::StaticClass(), Panel,
		[World, &Part](UDreamUserWidget* InBuilt)
		{
			Part = MakePlainWidget(World, TEXT("Part"));
			Part->SetParentBeforeRegister(InBuilt);
			Part->SetAnchoredPosition(ChildOffset);
		});
	if (!TestNotNull(TEXT("The control was made"), Control) || !TestNotNull(TEXT("... with its part"), Part))
	{
		return false;
	}
	TestTrue(TEXT("The control hangs from the panel"), Control->GetParent() == Panel);
	TestTrue(TEXT("... at its default place"), Control->GetAnchoredPosition().IsNearlyZero());
	TestTrue(TEXT("The part hangs from the control"), Part->GetParent() == Control);

	// THE CLAIM. Registration composed the new subtree against the panel it was made under. Nothing else
	// would have: at its default place the control never moves, so no setter ever asks.
	TestTrue(FString::Printf(TEXT("A control at its default place stands on its panel %s"), *DescribeMiss(Control, PanelAt)),
		Control->GetWorldLocation().Equals(PanelAt, LocationTolerance));
	const FVector PartAt = AnchoredIn(PanelAt, ChildOffset);
	TestTrue(FString::Printf(TEXT("... and its part stands on the panel too, where its anchors put it %s"), *DescribeMiss(Part, PartAt)),
		Part->GetWorldLocation().Equals(PartAt, LocationTolerance));
	TestTrue(TEXT("Every cached world transform in the control is the one its chain composes to"),
		IsComposedFromItsChain(Control) && IsComposedFromItsChain(Part));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetTweenClockWithoutWidgetTest,
	"DreamGUI.Widget.TweenClock.ATweenWithNoWidgetBehindItTakesTheWorldSpaceClockInsteadOfCrashing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetTweenClockWithoutWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetTransformRegistrationTestLocal;
	// A world a game instance owns, so the tween manager exists and every tween below is a real one. In a
	// world without it no tween is made at all, and the helper is never reached.
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("The rig's world has a tween manager"), UDreamTweenManager::GetDreamTweenInstance(Rig.GetWorld())))
	{
		return false;
	}
	const UDreamUISettings* Settings = GetDefault<UDreamUISettings>();
	const bool bWorldSpacePause = Settings->bWorldSpaceUIAffectByGamePause;
	const bool bWorldSpaceDilation = Settings->bWorldSpaceUIAffectByTimeDilation;

	// 1. The helper handed no widget, as Blueprint can hand it one. The tween's clock is first set to the
	//    opposite of the world-space pair, so only the helper writing that pair can make the two checks
	//    below hold -- a helper that merely stopped crashing by returning early would leave the opposite.
	UDreamWidget* Target = Rig.MakeWidget(TEXT("TweenTarget"), nullptr, FVector2D(100.0, 100.0));
	UDreamTweener* Direct = Target != nullptr ? Target->RenderOpacityTo(0.5f, 10.0f) : nullptr;
	if (!TestNotNull(TEXT("A real tween to choose the clock of"), Direct))
	{
		return false;
	}
	Direct->SetAffectByGamePause(!bWorldSpacePause)->SetAffectByTimeDilation(!bWorldSpaceDilation);
	UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(nullptr, Direct);
	TestTrue(TEXT("With no widget the tween pauses as world-space UI is set to"),
		Direct->GetAffectByGamePause() == bWorldSpacePause);
	TestTrue(TEXT("... and follows time dilation as world-space UI is set to"),
		Direct->GetAffectByTimeDilation() == bWorldSpaceDilation);
	Direct->Kill();

	// 2. The road nobody has to write by hand: a visual no widget hosts, but which lives in the world
	//    through its outer chain -- all the tween manager asks of it. Its own tween verb hands the helper
	//    its GetWidget(), which is null. Reaching the checks below at all is half of what is pinned.
	UDreamVisualEmpty* Loose = NewObject<UDreamVisualEmpty>(Rig.GetHostActor());
	if (!TestNotNull(TEXT("A visual outside any widget"), Loose))
	{
		return false;
	}
	TestNull(TEXT("... which has no widget"), Loose->GetWidget());
	UDreamTweener* FromVisual = Loose->ColorTo(FColor::Red, 10.0f);
	if (TestNotNull(TEXT("The visual's colour tween is a real one, made and handed back"), FromVisual))
	{
		TestTrue(TEXT("... on world-space UI's pause setting"),
			FromVisual->GetAffectByGamePause() == bWorldSpacePause);
		TestTrue(TEXT("... and its time-dilation setting"),
			FromVisual->GetAffectByTimeDilation() == bWorldSpaceDilation);
		// Before any frame runs: its setter marks the colour dirty through the widget the visual has not got.
		FromVisual->Kill();
	}
	return true;
}

#endif
