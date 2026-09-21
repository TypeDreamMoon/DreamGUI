// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetNavigation.h"

#include "Core/DreamUIManager.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/UISelectable.h"

namespace
{
	/**
	 * Every registered navigation component, world-agnostic and filtered at use.
	 *
	 * A registry of its own rather than a second array on UDreamUIManagerWorldSubsystem: the manager's
	 * selectable array is typed to UUISelectable and widening it would reach into a file half the
	 * plugin edits. Entries are weak and the scan checks the world, which is the same treatment the
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

// ---------------------------------------------------------------- UDreamWidgetNavigation

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
	return Widget->GetRenderVisibleInHierarchy() && Widget->GetInteractableInHierarchy();
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
	default:
		// Both are scan answers, and the scan is the caller's to run: it needs the boundary rule and
		// the confining scope, neither of which is this component's business.
		return nullptr;
	}
}

bool UDreamWidgetNavigation::OnNavigate_Implementation(EDreamUINavigationDirection InDirection, TScriptInterface<IDreamNavigationInterface>& OutResult)
{
	if (InDirection == EDreamUINavigationDirection::None)
	{
		return false;
	}

	bool bHandled = false;
	UDreamWidget* Target = ResolveTarget(InDirection, bHandled);
	if (bHandled)
	{
		UDreamUIBehaviour* TargetBehaviour = DreamUINavigationScan::FindNavigationBehaviour(Target);
		OutResult = DreamUINavigationScan::CanNavigateTo(TargetBehaviour) ? TargetBehaviour : nullptr;
		return true;
	}

	// Escape and Wrap: run the scan. The boundary rule on the surrounding navigation area decides what
	// happens at the edge, exactly as it does for a selectable -- with one addition, that a direction
	// explicitly set to Wrap wraps whether or not the area asked for it.
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return false;
	}
	const FVector Direction = DreamUINavigationScan::GetWorldDirection(Widget, InDirection);
	if (Direction.IsNearlyZero())
	{
		// Next and Prev have no direction of their own: they are right-then-down and left-then-up.
		UDreamUIBehaviour* Found = DreamUINavigationScan::ScanSequential(this, InDirection);
		OutResult = Found != this ? Found : nullptr;
		return true;
	}
	UDreamUIBehaviour* Found = DreamUINavigationScan::ScanDirectional(this, Direction, nullptr, nullptr);
	if (Found == this && GetNavigationData(InDirection).Rule == EDreamUINavigationRule::Wrap)
	{
		Found = DreamUINavigationScan::ScanWrap(this, Direction, nullptr, nullptr);
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
		if (Selectable == nullptr && Component->GetClass()->ImplementsInterface(UDreamNavigationInterface::StaticClass()))
		{
			Selectable = Component;
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
		for (const TWeakObjectPtr<UUISelectable>& Selectable : Manager->GetAllSelectableArray())
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

UDreamUIBehaviour* DreamUINavigationScan::ScanSequential(UDreamUIBehaviour* InSelf, EDreamUINavigationDirection InDirection)
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

	UDreamUIBehaviour* Found = ScanDirectional(InSelf, GetWorldDirection(Widget, First), nullptr, nullptr);
	if (Found != InSelf)
	{
		return Found;
	}
	return ScanDirectional(InSelf, GetWorldDirection(Widget, Second), nullptr, nullptr);
}
