// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetNavigation.h"

#include "Core/DreamUIManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUIPopupLayer.h"

namespace
{
	/**
	 * Every registered navigation component, world-agnostic and filtered at use.
	 *
	 * A registry of its own rather than the manager's selectable array, which holds the input system's
	 * selectables. Entries are weak and the scan checks the world, which is the same treatment the
	 * selectable array gets from the code that reads it.
	 */
	TArray<TWeakObjectPtr<UDreamWidgetNavigation>> GAllNavigationComponents;

	/** The widget a display name refers to, searched from InRoot's own tree. */
	UDreamWidget* FindWidgetByDisplayName(UDreamWidget* InFrom, FName InName)
	{
		if (!IsValid(InFrom) || InName.IsNone())
		{
			return nullptr;
		}
		// Search from the top of this widget's own tree, so a name can point sideways and upwards and
		// not only at a descendant -- an explicit link across a row is the ordinary case.
		UDreamWidget* Root = InFrom;
		while (UDreamWidget* Parent = Root->GetParent())
		{
			Root = Parent;
		}
		TArray<UDreamWidget*> Pending;
		Pending.Add(Root);
		while (Pending.Num() > 0)
		{
			UDreamWidget* Widget = Pending.Pop(EAllowShrinking::No);
			if (!IsValid(Widget))
			{
				continue;
			}
			if (Widget->GetDisplayName() == InName.ToString())
			{
				return Widget;
			}
			for (UDreamWidget* Child : Widget->GetChildren())
			{
				Pending.Add(Child);
			}
		}
		return nullptr;
	}
}

namespace DreamWidgetNavigationLocal
{
	/** How many nested navigation areas a single Escape move may climb out of before it gives up. */
	constexpr int32 MaxNavigationEscapeDepth = 8;

	/** The input system's answer to "what keeps navigation from here inside it"; see DreamUINavigationScan::SetConfiningWidgetResolver. */
	DreamUINavigationScan::FConfiningWidgetResolver GDreamConfiningWidgetResolver = nullptr;

	/**
	 * Another component on InWidget that takes navigation and can be navigated to now -- the selectable beside a
	 * navigation component -- or null. A UDreamWidgetNavigation is never the answer: two of them on one widget is not a
	 * case either component speaks for.
	 */
	UDreamUIBehaviour* FindOtherNavigationComponent(UDreamWidget* InWidget, const UDreamUIBehaviour* InSelf)
	{
		for (UDreamUIBehaviour* Component : InWidget->GetAllComponents())
		{
			if (!IsValid(Component) || Component == InSelf || Component->IsA<UDreamWidgetNavigation>())
			{
				continue;
			}
			if (DreamUINavigationScan::CanNavigateTo(Component))
			{
				return Component;
			}
		}
		return nullptr;
	}

	/**
	 * The far end of the legacy Next/Prev sequence from InSelf, the way InDirection wraps: the opposite sequence walked
	 * until it stops, as ScanWrap walks a direction backwards.
	 */
	UDreamUIBehaviour* ScanSequentialWrap(UDreamUIBehaviour* InSelf, EDreamUINavigationDirection InDirection,
		UDreamWidget* InParent, const UDreamWidget* InRestrictNode)
	{
		const EDreamUINavigationDirection Backwards = InDirection == EDreamUINavigationDirection::Next
			? EDreamUINavigationDirection::Prev : EDreamUINavigationDirection::Next;
		UDreamUIBehaviour* Walker = InSelf;
		TSet<UDreamUIBehaviour*> Visited;
		Visited.Add(InSelf);
		for (;;)
		{
			UDreamUIBehaviour* Back = DreamUINavigationScan::ScanSequential(Walker, Backwards, InParent, InRestrictNode);
			if (Back == nullptr || Back == Walker || Visited.Contains(Back))
			{
				break;//at the far end, or round a cycle a strange layout built
			}
			Visited.Add(Back);
			Walker = Back;
		}
		return Walker;
	}
}

// ---------------------------------------------------------------- UDreamWidgetNavigation

UDreamWidgetNavigation::UDreamWidgetNavigation()
{
	// Asked where to go when navigation arrives; where it stands is read then.
	DeclareTickUnused(StaticClass());
	DeclareTransformChangedUnused(StaticClass());
}

void UDreamWidgetNavigation::OnRegister()
{
	Super::OnRegister();
	// Focus is the precondition for navigation, and a widget whose only navigation opinion lives here
	// would otherwise be refused focus by UDreamWidget::SetFocus. UUISelectable::OnRegister does
	// exactly this for the same reason.
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->SetIsFocusable(true);
	}
	GAllNavigationComponents.AddUnique(this);
}

void UDreamWidgetNavigation::OnUnregister()
{
	GAllNavigationComponents.Remove(this);
	Super::OnUnregister();
}

const TArray<TWeakObjectPtr<UDreamWidgetNavigation>>& UDreamWidgetNavigation::GetAllNavigationComponents()
{
	return GAllNavigationComponents;
}

FDreamWidgetNavigationData& UDreamWidgetNavigation::GetNavigationData(EDreamUINavigationDirection InDirection)
{
	switch (InDirection)
	{
	case EDreamUINavigationDirection::Down:  return Down;
	case EDreamUINavigationDirection::Left:  return Left;
	case EDreamUINavigationDirection::Right: return Right;
	case EDreamUINavigationDirection::Next:  return Next;
	case EDreamUINavigationDirection::Prev:  return Previous;
	case EDreamUINavigationDirection::Up:
	default:
		return Up;
	}
}

const FDreamWidgetNavigationData& UDreamWidgetNavigation::GetNavigationData(EDreamUINavigationDirection InDirection) const
{
	return const_cast<UDreamWidgetNavigation*>(this)->GetNavigationData(InDirection);
}

bool UDreamWidgetNavigation::HasRuleFor(EDreamUINavigationDirection InDirection) const
{
	if (InDirection == EDreamUINavigationDirection::None)
	{
		return false;
	}
	return GetNavigationData(InDirection).Rule != EDreamUINavigationRule::Escape;
}

bool UDreamWidgetNavigation::HasAnyRule() const
{
	return Up.Rule != EDreamUINavigationRule::Escape
		|| Down.Rule != EDreamUINavigationRule::Escape
		|| Left.Rule != EDreamUINavigationRule::Escape
		|| Right.Rule != EDreamUINavigationRule::Escape
		|| Next.Rule != EDreamUINavigationRule::Escape
		|| Previous.Rule != EDreamUINavigationRule::Escape;
}

void UDreamWidgetNavigation::SetRule(EDreamUINavigationDirection InDirection, EDreamUINavigationRule InRule)
{
	if (InDirection == EDreamUINavigationDirection::None)
	{
		return;
	}
	GetNavigationData(InDirection).Rule = InRule;
}

void UDreamWidgetNavigation::SetAllNavigationRules(EDreamUINavigationRule InRule, FName InWidgetToFocus)
{
	// None is excluded on purpose: GetNavigationData answers the Up block for it, so including it
	// would write Up twice and make the list read as though there were seven directions.
	static const EDreamUINavigationDirection AllDirections[] = {
		EDreamUINavigationDirection::Left,
		EDreamUINavigationDirection::Right,
		EDreamUINavigationDirection::Up,
		EDreamUINavigationDirection::Down,
		EDreamUINavigationDirection::Next,
		EDreamUINavigationDirection::Prev,
	};
	for (const EDreamUINavigationDirection Direction : AllDirections)
	{
		FDreamWidgetNavigationData& Data = GetNavigationData(Direction);
		Data.Rule = InRule;
		Data.WidgetToFocus = InWidgetToFocus;
	}
}

void UDreamWidgetNavigation::SetExplicitTarget(EDreamUINavigationDirection InDirection, UDreamWidget* InTarget)
{
	if (InDirection == EDreamUINavigationDirection::None)
	{
		return;
	}
	FDreamWidgetNavigationData& Data = GetNavigationData(InDirection);
	Data.Widget = InTarget;
	// Clearing the target puts the direction back to "no opinion" rather than leaving an Explicit rule
	// pointing at nothing, which would read as Stop and is never what clearing a link means.
	Data.Rule = IsValid(InTarget) ? EDreamUINavigationRule::Explicit : EDreamUINavigationRule::Escape;
}

void UDreamWidgetNavigation::SetCustomDelegate(EDreamUINavigationDirection InDirection, const FDreamCustomWidgetNavigationDelegate& InDelegate)
{
	if (InDirection == EDreamUINavigationDirection::None)
	{
		return;
	}
	FDreamWidgetNavigationData& Data = GetNavigationData(InDirection);
	Data.CustomDelegate = InDelegate;
	Data.Rule = InDelegate.IsBound() ? EDreamUINavigationRule::Custom : EDreamUINavigationRule::Escape;
}

void UDreamWidgetNavigation::SetNavigationRuleCustomBoundary(EDreamUINavigationDirection InDirection,
	const FDreamCustomWidgetNavigationDelegate& InDelegate)
{
	if (InDirection == EDreamUINavigationDirection::None)
	{
		return;
	}
	FDreamWidgetNavigationData& Data = GetNavigationData(InDirection);
	Data.CustomDelegate = InDelegate;
	Data.Rule = InDelegate.IsBound() ? EDreamUINavigationRule::CustomBoundary : EDreamUINavigationRule::Escape;
}

bool UDreamWidgetNavigation::CanNavigateHere_Implementation() const
{
	if (!bCanNavigateHere)
	{
		return false;
	}
	const UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return false;
	}
	// Focusable too: SetFocus refuses a widget that is not, and a move landing where focus cannot be is a move that strands
	// the player. OnRegister makes the widget focusable, so only a widget turned unfocusable afterwards is refused.
	return Widget->GetIsFocusable() && Widget->GetRenderVisibleInHierarchy() && Widget->GetInteractableInHierarchy();
}

UDreamWidget* UDreamWidgetNavigation::ResolveTarget(EDreamUINavigationDirection InDirection, bool& bOutHandled)
{
	bOutHandled = false;
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget) || InDirection == EDreamUINavigationDirection::None)
	{
		return nullptr;
	}
	FDreamWidgetNavigationData& Data = GetNavigationData(InDirection);
	switch (Data.Rule)
	{
	case EDreamUINavigationRule::Stop:
		// Handled, and the answer is "nobody": focus stays exactly where it is.
		bOutHandled = true;
		return nullptr;

	case EDreamUINavigationRule::Explicit:
	{
		bOutHandled = true;
		UDreamWidget* Target = Data.Widget.Get();
		if (!IsValid(Target))
		{
			Target = FindWidgetByDisplayName(Widget, Data.WidgetToFocus);
		}
		return Target;
	}

	case EDreamUINavigationRule::Custom:
	{
		bOutHandled = true;
		// An unbound Custom is an authoring mistake rather than a rule, and answering "nowhere" for it
		// would pin focus in place with nothing to explain why.
		if (!Data.CustomDelegate.IsBound())
		{
			return nullptr;
		}
		return Data.CustomDelegate.Execute(InDirection);
	}

	case EDreamUINavigationRule::Wrap:
	case EDreamUINavigationRule::Escape:
	case EDreamUINavigationRule::CustomBoundary:
	default:
		// All three are scan answers, and the scan is the caller's to run: it needs the boundary rule
		// and the confining scope, neither of which is this component's business. CustomBoundary
		// belongs here rather than beside Custom precisely because it must let the scan go first --
		// its delegate is the edge case, and only the scan knows whether this move is one.
		return nullptr;
	}
}

UDreamWidget* UDreamWidgetNavigation::AskBoundaryDelegate(EDreamUINavigationDirection InDirection)
{
	const FDreamWidgetNavigationData& Data = GetNavigationData(InDirection);
	if (Data.Rule != EDreamUINavigationRule::CustomBoundary || !Data.CustomDelegate.IsBound())
	{
		return nullptr;
	}
	return Data.CustomDelegate.Execute(InDirection);
}

bool UDreamWidgetNavigation::OnNavigate_Implementation(EDreamUINavigationDirection InDirection, TScriptInterface<IDreamNavigationInterface>& OutResult)
{
	if (InDirection == EDreamUINavigationDirection::None)
	{
		return false;
	}
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return false;
	}

	// No opinion about this direction, and a selectable beside us that has one: it answers. The pipeline hands every move
	// to this component once any direction has a rule (FindNavigationBehaviour), and a direction left at Escape here is
	// still the selectable's -- its explicit links, its None -- as UUISelectable::FindNavigableOn reads the two from its side.
	if (!HasRuleFor(InDirection))
	{
		if (UDreamUIBehaviour* Other = DreamWidgetNavigationLocal::FindOtherNavigationComponent(Widget, this))
		{
			return IDreamNavigationInterface::Execute_OnNavigate(Other, InDirection, OutResult);
		}
	}

	bool bHandled = false;
	UDreamWidget* Target = ResolveTarget(InDirection, bHandled);
	if (bHandled)
	{
		UDreamUIBehaviour* TargetBehaviour = DreamUINavigationScan::FindNavigationBehaviour(Target);
		OutResult = DreamUINavigationScan::CanNavigateTo(TargetBehaviour) ? TargetBehaviour : nullptr;
		return true;
	}

	// Escape, Wrap and CustomBoundary: run the scan, kept where a selectable's is kept -- inside the open popup or the
	// confining screen that holds this widget, else its root canvas, and inside its navigation area, whose boundary rule
	// decides at the area's edge. These scans used to pass no limit at all, so a navigation-only widget walked out of a
	// dialog, a menu and its own player's screen. A direction explicitly set to Wrap wraps whether or not the area asked.
	UDreamWidget* Parent = DreamUINavigationScan::FindScanParent(Widget);
	const UDreamWidget* Area = Widget->GetRestrictNavigationAreaWidget();
	const bool bWrapRule = GetNavigationData(InDirection).Rule == EDreamUINavigationRule::Wrap;
	const FVector Direction = DreamUINavigationScan::GetWorldDirection(Widget, InDirection);
	UDreamUIBehaviour* Found = nullptr;
	if (Direction.IsNearlyZero())
	{
		// Next and Prev have no direction of their own: here they are the legacy right-then-down and left-then-up, and a
		// Wrap rule goes round to the far end of that sequence.
		Found = DreamUINavigationScan::ScanSequential(this, InDirection, Parent, Area);
		if (Found == this && bWrapRule)
		{
			Found = DreamWidgetNavigationLocal::ScanSequentialWrap(this, InDirection, Parent, Area);
		}
	}
	else
	{
		Found = DreamUINavigationScan::ScanWithinArea(this, Direction, Parent, Area);
		if (Found == this && bWrapRule)
		{
			Found = DreamUINavigationScan::ScanWrap(this, Direction, Parent, Area);
		}
	}
	if (Found == this)
	{
		// Nothing in that direction inside the area: this move leaves it, which is the one case
		// CustomBoundary asks about. Asked AFTER the scan and after Wrap, so a delegate never
		// pre-empts a neighbour that was there all along.
		if (UDreamWidget* FromDelegate = AskBoundaryDelegate(InDirection))
		{
			OutResult = DreamUINavigationScan::FindNavigationBehaviour(FromDelegate);
			return true;
		}
	}
	OutResult = Found != this ? Found : nullptr;
	return true;
}

// ---------------------------------------------------------------- DreamUINavigationScan

UDreamUIBehaviour* DreamUINavigationScan::FindNavigationBehaviour(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	UDreamUIBehaviour* Selectable = nullptr;
	UDreamUIBehaviour* SecondSelectable = nullptr;
	UDreamWidgetNavigation* Navigation = nullptr;
	for (UDreamUIBehaviour* Component : InWidget->GetAllComponents())
	{
		if (!IsValid(Component))
		{
			continue;
		}
		if (UDreamWidgetNavigation* AsNavigation = Cast<UDreamWidgetNavigation>(Component))
		{
			Navigation = AsNavigation;
			continue;
		}
		if (Component->GetClass()->ImplementsInterface(UDreamNavigationInterface::StaticClass()))
		{
			if (Selectable == nullptr)
			{
				Selectable = Component;
			}
			else if (SecondSelectable == nullptr)
			{
				SecondSelectable = Component;
			}
		}
	}
	// More than one other taker -- a user widget's event bridge, which refuses navigation until its widget opts in, and a
	// selectable an author added beside it: the first that can be navigated to now, rather than whichever sits first.
	if (SecondSelectable != nullptr && !CanNavigateTo(Selectable))
	{
		for (UDreamUIBehaviour* Component : InWidget->GetAllComponents())
		{
			if (IsValid(Component) && Component != Selectable && !Component->IsA<UDreamWidgetNavigation>()
				&& Component->GetClass()->ImplementsInterface(UDreamNavigationInterface::StaticClass()) && CanNavigateTo(Component))
			{
				Selectable = Component;
				break;
			}
		}
	}
	// A navigation component with no rule at all is a participant but not an authority: a widget that
	// has both gets its selectable's behaviour, and the rules only take over where one was authored.
	return Navigation != nullptr && (Selectable == nullptr || Navigation->HasAnyRule()) ? Navigation : Selectable;
}

bool DreamUINavigationScan::CanNavigateTo(UDreamUIBehaviour* InBehaviour)
{
	if (!IsValid(InBehaviour))
	{
		return false;
	}
	if (!InBehaviour->GetClass()->ImplementsInterface(UDreamNavigationInterface::StaticClass()))
	{
		return false;
	}
	return IDreamNavigationInterface::Execute_CanNavigateHere(InBehaviour);
}

void DreamUINavigationScan::SetConfiningWidgetResolver(FConfiningWidgetResolver InResolver)
{
	DreamWidgetNavigationLocal::GDreamConfiningWidgetResolver = InResolver;
}

UDreamWidget* DreamUINavigationScan::FindConfiningWidget(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	if (DreamWidgetNavigationLocal::GDreamConfiningWidgetResolver != nullptr)
	{
		return DreamWidgetNavigationLocal::GDreamConfiningWidgetResolver(InWidget);
	}
	// No input system in this process to ask about screens: the popups, which the core keeps, still hold the pad in.
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(InWidget);
	return Popups != nullptr ? Popups->FindPopupContaining(InWidget, INDEX_NONE) : nullptr;
}

UDreamWidget* DreamUINavigationScan::FindScanParent(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	if (UDreamWidget* Confining = FindConfiningWidget(InWidget))
	{
		return Confining;
	}
	// The selectable's canvas limit (UUISelectable::FindNavigableIn): a screen's controls are reached from that screen only.
	if (InWidget->IsScreenSpaceOverlayUI() || InWidget->IsRenderTargetUI())
	{
		if (const UDreamCanvas* RootCanvas = InWidget->GetRootCanvas())
		{
			return RootCanvas->GetWidget();
		}
	}
	return nullptr;
}

UDreamUIBehaviour* DreamUINavigationScan::ScanWithinArea(UDreamUIBehaviour* InSelf, const FVector& InDirection,
	UDreamWidget* InParent, const UDreamWidget* InRestrictNode, int32 InEscapeDepth)
{
	UDreamUIBehaviour* Found = ScanDirectional(InSelf, InDirection, InParent, InRestrictNode);
	if (Found != InSelf)
	{
		return Found;//the scan moved, so the edge was never reached
	}
	// Nothing that way. Whether that is the end of the story is the area's decision, and with no area
	// around us there is nobody to ask -- stopping is the only thing "the edge of everything" can mean.
	if (!IsValid(InRestrictNode))
	{
		return InSelf;
	}
	switch (InRestrictNode->GetNavigationBoundaryRule())
	{
	case EDreamUINavigationBoundaryRule::Wrap:
		return ScanWrap(InSelf, InDirection, InParent, InRestrictNode);
	case EDreamUINavigationBoundaryRule::Escape:
		{
			// One area out, and only if there is one: past the outermost area the move has genuinely
			// left everything that could restrict it, and the plain scan already covered that ground.
			if (InEscapeDepth >= DreamWidgetNavigationLocal::MaxNavigationEscapeDepth)
			{
				return InSelf;
			}
			const UDreamWidget* AreaParent = InRestrictNode->GetParent();
			const UDreamWidget* Enclosing = IsValid(AreaParent) ? AreaParent->GetRestrictNavigationAreaWidget() : nullptr;
			if (Enclosing == nullptr)
			{
				return ScanDirectional(InSelf, InDirection, InParent, nullptr);
			}
			return ScanWithinArea(InSelf, InDirection, InParent, Enclosing, InEscapeDepth + 1);
		}
	case EDreamUINavigationBoundaryRule::Stop:
	default:
		return InSelf;
	}
}

UDreamUIBehaviour* DreamUINavigationScan::ScanDirectional(UDreamUIBehaviour* InSelf, const FVector& InDirection,
	UDreamWidget* InParent, const UDreamWidget* InRestrictNode)
{
	if (!IsValid(InSelf))
	{
		return nullptr;
	}
	UDreamWidget* ThisWidget = InSelf->GetWidget();
	if (!IsValid(ThisWidget))
	{
		return InSelf;
	}

	auto GetPointOnRectEdge = [](UDreamWidget* Rect, FVector2D Dir)
	{
		if (Dir != FVector2D::ZeroVector)
		{
			Dir /= FMath::Max(FMath::Abs(Dir.X), FMath::Abs(Dir.Y));
		}
		const FVector2D Center = Rect->GetLocalSpaceCenter();
		Dir = Center + FVector2D(Rect->GetWidth() * Dir.X * 0.5f, Rect->GetHeight() * Dir.Y * 0.5f);
		return FVector(0, Dir.X, Dir.Y);
	};

	const FVector LocalDir = ThisWidget->GetWorldTransform().InverseTransformVectorNoScale(InDirection);
	const FVector LocalPos = GetPointOnRectEdge(ThisWidget, FVector2D(LocalDir.Y, LocalDir.Z));
	const FVector Pos = ThisWidget->GetWorldTransform().TransformPosition(LocalPos);

	float MaxScore = -MAX_flt;
	UDreamUIBehaviour* BestPick = InSelf;

	// One pass per candidate widget, whichever pool it came from, so a widget carrying both a
	// selectable and a navigation component is considered once and by one behaviour.
	TSet<const UDreamWidget*> Considered;
	Considered.Add(ThisWidget);

	auto Consider = [&](UDreamUIBehaviour* Candidate)
	{
		if (!IsValid(Candidate) || Candidate == InSelf)
		{
			return;
		}
		UDreamWidget* CandidateWidget = Candidate->GetWidget();
		if (!IsValid(CandidateWidget))
		{
			return;
		}
		bool bAlreadySeen = false;
		Considered.Add(CandidateWidget, &bAlreadySeen);
		if (bAlreadySeen)
		{
			return;
		}
		// The behaviour that would actually receive the move, not necessarily the one we found it by.
		UDreamUIBehaviour* Receiver = FindNavigationBehaviour(CandidateWidget);
		if (Receiver == nullptr || Receiver == InSelf || !CanNavigateTo(Receiver))
		{
			return;
		}
		if (IsValid(InParent) && !CandidateWidget->IsChildOf(InParent))
		{
			return;
		}
		if (!CandidateWidget->GetInteractableInHierarchy())
		{
			return;
		}
		if (CandidateWidget->IsWorldSpaceUI() != ThisWidget->IsWorldSpaceUI())
		{
			return;
		}
		if (InRestrictNode != nullptr && !CandidateWidget->IsChildOf(InRestrictNode))
		{
			return;
		}
#if WITH_EDITOR
		if (InSelf->GetWorld() != Candidate->GetWorld())
		{
			return;
		}
#endif
		const FVector2D LocalCenter2D = CandidateWidget->GetLocalSpaceCenter();
		const FVector CandidateCenter(0, LocalCenter2D.X, LocalCenter2D.Y);
		const FVector CenterInWorld = CandidateWidget->GetWorldTransform().TransformPosition(CandidateCenter);
		// Clipped away is not the same as out of reach: a row scrolled off the end of a list is one
		// scroll from being on screen, and anything else hidden has no way back and stays skipped.
		if (!CandidateWidget->IsPointVisibleOnClip(CenterInWorld)
			&& !FDreamUINavigationScroll::IsReachableByScrolling(CandidateWidget))
		{
			return;
		}
		const FVector ToCandidate = CenterInWorld - Pos;
		const float Dot = FVector::DotProduct(InDirection, ToCandidate);
		if (Dot <= 0.0f)
		{
			return;//behind us
		}
		const float Score = Dot / ToCandidate.SizeSquared();
		if (Score > MaxScore)
		{
			MaxScore = Score;
			BestPick = Receiver;
		}
	};

	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InSelf->GetWorld()))
	{
		for (const TWeakObjectPtr<UDreamUIBehaviour>& Selectable : Manager->GetAllSelectableArray())
		{
			Consider(Selectable.Get());
		}
	}
	for (const TWeakObjectPtr<UDreamWidgetNavigation>& Navigation : GAllNavigationComponents)
	{
		Consider(Navigation.Get());
	}
	return BestPick;
}

FVector DreamUINavigationScan::GetWorldDirection(const UDreamWidget* InWidget, EDreamUINavigationDirection InDirection)
{
	if (!IsValid(InWidget))
	{
		return FVector::ZeroVector;
	}
	switch (InDirection)
	{
	case EDreamUINavigationDirection::Left:  return -InWidget->GetRightVector();
	case EDreamUINavigationDirection::Right: return InWidget->GetRightVector();
	case EDreamUINavigationDirection::Up:    return InWidget->GetUpVector();
	case EDreamUINavigationDirection::Down:  return -InWidget->GetUpVector();
	default:
		return FVector::ZeroVector;
	}
}

UDreamUIBehaviour* DreamUINavigationScan::ScanWrap(UDreamUIBehaviour* InSelf, const FVector& InDirection,
	UDreamWidget* InParent, const UDreamWidget* InRestrictNode)
{
	UDreamUIBehaviour* Walker = InSelf;
	TSet<UDreamUIBehaviour*> Visited;
	Visited.Add(InSelf);
	const FVector Backwards = -InDirection;
	for (;;)
	{
		UDreamUIBehaviour* Back = ScanDirectional(Walker, Backwards, InParent, InRestrictNode);
		if (Back == nullptr || Back == Walker || Visited.Contains(Back))
		{
			break;//at the far edge, or round a cycle a strange layout built
		}
		Visited.Add(Back);
		Walker = Back;
	}
	return Walker;
}

UDreamUIBehaviour* DreamUINavigationScan::ScanSequential(UDreamUIBehaviour* InSelf, EDreamUINavigationDirection InDirection,
	UDreamWidget* InParent, const UDreamWidget* InRestrictNode)
{
	if (!IsValid(InSelf))
	{
		return nullptr;
	}
	UDreamWidget* Widget = InSelf->GetWidget();
	if (!IsValid(Widget))
	{
		return InSelf;
	}
	const bool bForward = InDirection == EDreamUINavigationDirection::Next;
	const EDreamUINavigationDirection First = bForward ? EDreamUINavigationDirection::Right : EDreamUINavigationDirection::Left;
	const EDreamUINavigationDirection Second = bForward ? EDreamUINavigationDirection::Down : EDreamUINavigationDirection::Up;

	UDreamUIBehaviour* Found = ScanDirectional(InSelf, GetWorldDirection(Widget, First), InParent, InRestrictNode);
	if (Found != InSelf)
	{
		return Found;
	}
	return ScanDirectional(InSelf, GetWorldDirection(Widget, Second), InParent, InRestrictNode);
}

UDreamUIBehaviour* DreamUINavigationScan::ScanSequential(UDreamUIBehaviour* InSelf, EDreamUINavigationDirection InDirection)
{
	const UDreamWidget* Widget = IsValid(InSelf) ? InSelf->GetWidget() : nullptr;
	return ScanSequential(InSelf, InDirection, FindScanParent(Widget),
		IsValid(Widget) ? Widget->GetRestrictNavigationAreaWidget() : nullptr);
}
