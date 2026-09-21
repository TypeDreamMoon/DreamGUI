// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Dream2DLineRendererBase.h"
#include "Dream2DLineChildrenAsPoints.generated.h"

class UDreamWidget;

//Collect U2DLineChildrenAsPointsChild, and use child's relative location as points to draw line
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDream2DLineChildrenAsPoints : public UDream2DLineRendererBase
{
	GENERATED_BODY()

public:	
	UDream2DLineChildrenAsPoints(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay()override;
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
	/**
	 * The line's OWN rect changing, which moves every child that is anchored inside it and so
	 * moves every point this line reads.
	 *
	 * Not a child-dimension hook, because a Visual does not get one: UDreamVisual descends from
	 * UDreamWidgetSubObjectBehaviour, a separate hierarchy from the UDreamUIBehaviour components
	 * that OnChildDimensionsChanged belongs to. That is the same gap that leaves
	 * OnWidgetChildAttached unreachable here -- see RefreshChildSubscriptions.
	 */
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;

	UPROPERTY(VisibleAnywhere, Transient, Category = DreamGUI)TArray<FVector2D>
		CurrentPointArray;

	virtual void CalculatePoints()override;
	virtual const TArray<FVector2D>& GetCalcaultedPointArray()override
	{
		return CurrentPointArray;
	}

	/**
	 * The children this line is currently listening to, which is the same set CalculatePoints last
	 * read positions from. Weak because the line does not own its points -- a child can be
	 * destroyed out from under it -- and deliberately not a UPROPERTY: it is a record of live
	 * delegate registrations, which nothing should carry across a save or a duplicate.
	 */
	TArray<TWeakObjectPtr<UDreamWidget>> SubscribedChildren;

	/** Brings SubscribedChildren back in line with the widget's actual children, adding and removing delegates to match. */
	void RefreshChildSubscriptions();
	/** Drops every subscription this line holds. Symmetric with RefreshChildSubscriptions and used on the way out. */
	void ClearChildSubscriptions();
public:
	void OnChildPositionChanged();
};
