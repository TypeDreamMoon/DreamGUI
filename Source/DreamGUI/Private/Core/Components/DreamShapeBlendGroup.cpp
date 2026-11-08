// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"

UDreamShapeBlendGroup::UDreamShapeBlendGroup()
{
	DeclareTransformChangedUnused(UDreamShapeBlendGroup::StaticClass());
}

void UDreamShapeBlendGroup::SetSmoothness(float Value)
{
	Value = FMath::Max(Value, 0.0f);
	if (Smoothness != Value)
	{
		Smoothness = Value;
		Refresh();
	}
}

void UDreamShapeBlendGroup::SetBlendEnabled(bool Value)
{
	if (bBlendEnabled != Value)
	{
		bBlendEnabled = Value;
		Refresh();
	}
}

bool UDreamShapeBlendGroup::IsActive() const
{
	return bBlendEnabled && bIsRegisteredWithWidget && Leader.IsValid();
}

void UDreamShapeBlendGroup::GetMembers(TArray<UDreamRectBlock*>& OutMembers) const
{
	OutMembers.Reset();
	for (const TWeakObjectPtr<UDreamRectBlock>& Member : Members)
	{
		if (UDreamRectBlock* Block = Member.Get())
		{
			OutMembers.Add(Block);
		}
	}
}

UDreamShapeBlendGroup* UDreamShapeBlendGroup::FindFor(const UDreamRectBlock* InMember)
{
	const UDreamWidget* Widget = InMember != nullptr ? InMember->GetWidget() : nullptr;
	const UDreamWidget* Parent = Widget != nullptr ? Widget->GetParent() : nullptr;
	UDreamShapeBlendGroup* Group = Parent != nullptr ? Parent->GetComponent<UDreamShapeBlendGroup>() : nullptr;
	return Group != nullptr && Group->IsActive() ? Group : nullptr;
}

void UDreamShapeBlendGroup::GatherMembers(const UDreamWidget* InFrame, TArray<FDreamUIShapeBlendMember>& OutMembers, TArray<UDreamRectBlock*>* OutBlocks) const
{
	OutMembers.Reset();
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr || InFrame == nullptr)
	{
		return;
	}
	const FMatrix ToFrame = InFrame->GetInverseWorldMatrix();
	const FVector2D FrameCentre = InFrame->GetLocalSpaceCenter();
	for (UDreamWidget* Child : Widget->GetChildren())
	{
		if (!IsValid(Child) || !Child->GetWidgetActiveInHierarchy())
		{
			continue;
		}
		UDreamRectBlock* Block = Cast<UDreamRectBlock>(Child->GetVisual());
		if (Block == nullptr)
		{
			continue;
		}
		// The member's rect into the frame: its centre, and its own right and down axes, +Y down in both.
		const FMatrix ToFrameFromMember = Child->GetWorldMatrix() * ToFrame;
		const FVector2D Centre = Child->GetLocalSpaceCenter();
		const FVector Origin = ToFrameFromMember.TransformPosition(FVector(0.0, Centre.X, Centre.Y));
		const FVector Right = ToFrameFromMember.TransformVector(FVector(0.0, 1.0, 0.0));
		const FVector Up = ToFrameFromMember.TransformVector(FVector(0.0, 0.0, 1.0));
		FDreamUIShapeBlendMember& Member = OutMembers.AddDefaulted_GetRef();
		Member.Centre = FVector2f(static_cast<float>(Origin.Y - FrameCentre.X), static_cast<float>(FrameCentre.Y - Origin.Z));
		Member.AxisX = FVector2f(static_cast<float>(Right.Y), static_cast<float>(-Right.Z));
		Member.AxisY = FVector2f(static_cast<float>(-Up.Y), static_cast<float>(Up.Z));
		Member.HalfSize = FVector2f(Child->GetWidth() * 0.5f, Child->GetHeight() * 0.5f);
		Member.CornerRadii = Block->GetResolvedCornerRadius();
		Member.Operation = Block->GetShapeBlendOperation();
		Member.Color = Block->GetBodyColor();
		if (OutBlocks != nullptr)
		{
			OutBlocks->Add(Block);
		}
		if (OutMembers.Num() >= DreamUIShapeBlendLayout::MaxMembers)
		{
			break;
		}
	}
}

void UDreamShapeBlendGroup::MakeShapeInFrame(const UDreamWidget* InFrame, FDreamUIShapeBlendShape& OutShape) const
{
	OutShape.Texels.Reset();
	if (!IsActive())
	{
		return;
	}
	TArray<FDreamUIShapeBlendMember> Gathered;
	GatherMembers(InFrame, Gathered, nullptr);
	if (Gathered.Num() > 0)
	{
		FDreamUIShapeBlendMath::Pack(Gathered, Smoothness, OutShape);
	}
}

bool UDreamShapeBlendGroup::HitTest(const UDreamRectBlock* InMember, const FVector& InWorldPoint) const
{
	const UDreamRectBlock* Lead = Leader.Get();
	if (!IsActive() || Lead == nullptr || LeaderShape.IsEmpty())
	{
		return false;
	}
	const int32 Index = Members.IndexOfByKey(InMember);
	const UDreamWidget* Frame = Lead->GetWidget();
	if (Index == INDEX_NONE || Frame == nullptr)
	{
		return false;
	}
	const FVector Local = Frame->GetInverseWorldMatrix().TransformPosition(InWorldPoint);
	const FVector2D Centre = Frame->GetLocalSpaceCenter();
	const FVector2f P(static_cast<float>(Local.Y - Centre.X), static_cast<float>(Centre.Y - Local.Z));
	int32 Nearest = -1;
	const float Distance = FDreamUIShapeBlendMath::Distance(LeaderShape.Texels.GetData(), P, nullptr, &Nearest);
	return Distance <= 0.0f && Nearest == Index;
}

void UDreamShapeBlendGroup::Refresh()
{
	UDreamWidget* Widget = GetWidget();
	TArray<UDreamRectBlock*> Blocks;
	TArray<FDreamUIShapeBlendMember> Gathered;
	UDreamRectBlock* NewLeader = nullptr;
	if (bBlendEnabled && bIsRegisteredWithWidget && Widget != nullptr)
	{
		// The first member draws the shape, measured in its own frame.
		for (UDreamWidget* Child : Widget->GetChildren())
		{
			if (IsValid(Child) && Child->GetWidgetActiveInHierarchy() && Cast<UDreamRectBlock>(Child->GetVisual()) != nullptr)
			{
				NewLeader = Cast<UDreamRectBlock>(Child->GetVisual());
				break;
			}
		}
		if (NewLeader != nullptr)
		{
			GatherMembers(NewLeader->GetWidget(), Gathered, &Blocks);
		}
	}
	// Members that left are drawn on their own again.
	for (const TWeakObjectPtr<UDreamRectBlock>& Old : Members)
	{
		UDreamRectBlock* Block = Old.Get();
		if (Block != nullptr && !Blocks.Contains(Block))
		{
			Block->SetShapeBlend(nullptr, false, nullptr, FBox2f(ForceInit));
		}
	}
	const bool bLeaderChanged = Leader.Get() != NewLeader;
	Members.Reset();
	for (UDreamRectBlock* Block : Blocks)
	{
		Members.Add(Block);
	}
	Leader = NewLeader;
	FDreamUIShapeBlendShape Shape;
	if (Gathered.Num() > 0)
	{
		FDreamUIShapeBlendMath::Pack(Gathered, Smoothness, Shape);
	}
	const FBox2f Bounds = FDreamUIShapeBlendMath::Bounds(Gathered, Smoothness);
	const bool bChanged = bLeaderChanged || !(Shape == LeaderShape) || !(Bounds == LeaderBounds);
	LeaderShape = MoveTemp(Shape);
	LeaderBounds = Bounds;
	if (!bChanged)
	{
		return;
	}
	for (UDreamRectBlock* Block : Blocks)
	{
		const bool bLeader = Block == NewLeader;
		Block->SetShapeBlend(this, bLeader, bLeader ? &LeaderShape : nullptr, bLeader ? LeaderBounds : FBox2f(ForceInit));
	}
	// A layer style on this widget is drawn by the leader once there is one, apart while there is not.
	if (UDreamLayerStyle* Style = UDreamLayerStyle::Find(Widget))
	{
		Style->RefreshPath();
	}
}

void UDreamShapeBlendGroup::Release()
{
	for (const TWeakObjectPtr<UDreamRectBlock>& Member : Members)
	{
		if (UDreamRectBlock* Block = Member.Get())
		{
			Block->SetShapeBlend(nullptr, false, nullptr, FBox2f(ForceInit));
		}
	}
	Members.Reset();
	Leader.Reset();
	LeaderShape.Texels.Reset();
	LeaderBounds = FBox2f(ForceInit);
}

void UDreamShapeBlendGroup::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	// A world that is not playing ticks no behaviour: the editor's tick moves the shape there, as the designer's previews do.
	UWorld* World = GetWorld();
	if (World != nullptr && !World->IsGameWorld())
	{
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World))
		{
			EditorTickHandle = Manager->GetEditorTickDelegate().AddWeakLambda(this, [this](float) { Refresh(); });
		}
	}
#endif
	Refresh();
}

void UDreamShapeBlendGroup::OnUnregister()
{
#if WITH_EDITOR
	if (EditorTickHandle.IsValid())
	{
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			Manager->GetEditorTickDelegate().Remove(EditorTickHandle);
		}
		EditorTickHandle.Reset();
	}
#endif
	Release();
	UDreamWidget* Widget = GetWidget();
	Super::OnUnregister();
	if (UDreamLayerStyle* Style = UDreamLayerStyle::Find(Widget))
	{
		Style->RefreshPath();
	}
}

void UDreamShapeBlendGroup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Refresh();
}

void UDreamShapeBlendGroup::OnWidgetChildAttached(UDreamWidget* Child)
{
	Super::OnWidgetChildAttached(Child);
	Refresh();
}

void UDreamShapeBlendGroup::OnWidgetChildDetached(UDreamWidget* Child)
{
	Super::OnWidgetChildDetached(Child);
	Refresh();
}

#if WITH_EDITOR
void UDreamShapeBlendGroup::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Smoothness = FMath::Max(Smoothness, 0.0f);
	Refresh();
}
#endif
