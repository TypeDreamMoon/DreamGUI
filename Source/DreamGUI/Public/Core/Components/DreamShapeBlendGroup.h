// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIShapeBlend.h"
#include "DreamShapeBlendGroup.generated.h"

class UDreamRectBlock;

/**
 * Melts the rect blocks among the widget's children into one shape: their rounded rects joined by a smooth minimum, so that
 * two of them close together flow into each other, each by its own operation (UDreamRectBlock's ShapeBlendOperation: a union,
 * a cut, an intersection), the colour across the shape each one's as much as it makes the shape there. A blob of a menu, a
 * tab indicator that stretches as it moves, a loader of drops: move the children and the shape follows.
 *
 * The children are no longer drawn each on its own: the first of them draws the whole shape, in one quad over all of them,
 * with the built-in shader or its material, batched as any rect block is. Its body is the members' colours; a layer style on
 * this widget (UDreamLayerStyle) is drawn round the melted shape, in the same pixels. A click lands on the member nearest it,
 * wherever the melted shape covers it -- the same formula on the CPU.
 *
 * Glass can take its shape from a group (MakeShapeInFrame).
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "Dream Shape Blend Group")
class DREAMGUI_API UDreamShapeBlendGroup : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UDreamShapeBlendGroup();

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Shape Blend")
	float GetSmoothness() const { return Smoothness; }
	/** How far apart, in units, two edges still melt into each other: 0 joins them with a hard corner. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Shape Blend")
	void SetSmoothness(float Value);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Shape Blend")
	bool GetBlendEnabled() const { return bBlendEnabled; }
	/** Off, the children are drawn each on its own again. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Shape Blend")
	void SetBlendEnabled(bool Value);

	/** Whether the children are melted now: on, registered, with a rect block among them. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Shape Blend")
	bool IsActive() const;
	/** The rect block that draws the shape: the first of the members. */
	UDreamRectBlock* GetLeader() const { return Leader.Get(); }
	/** The rect blocks melted, in the order they are folded in. */
	void GetMembers(TArray<UDreamRectBlock*>& OutMembers) const;

	/**
	 * The melted shape in InFrame's space -- units from InFrame's rect's centre, +Y down, as a rect block measures its own
	 * shape -- for a shader to read (DreamUIShapeBlend.ush): what glass takes for its shape. Empty when the group is not active.
	 */
	void MakeShapeInFrame(const class UDreamWidget* InFrame, FDreamUIShapeBlendShape& OutShape) const;
	/** Whether InWorldPoint, on the members' plane, is on the melted shape where InMember is the member nearest it. */
	bool HitTest(const UDreamRectBlock* InMember, const FVector& InWorldPoint) const;

	/** The members gathered again and the shape packed, now: what every frame does, for a test or a tool that cannot wait. */
	void Refresh();

	/** The group InWidget is a member of (its parent's), when that is active; else null. */
	static UDreamShapeBlendGroup* FindFor(const UDreamRectBlock* InMember);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnWidgetChildAttached(UDreamWidget* Child) override;
	virtual void OnWidgetChildDetached(UDreamWidget* Child) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** How far apart, in units, two edges still melt into each other. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Shape Blend", meta = (ClampMin = "0", UIMax = "128"))
	float Smoothness = 16.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Shape Blend")
	bool bBlendEnabled = true;

private:
	/** The members in InFrame's space, packed or not. */
	void GatherMembers(const class UDreamWidget* InFrame, TArray<FDreamUIShapeBlendMember>& OutMembers, TArray<UDreamRectBlock*>* OutBlocks) const;
	/** Every member told it is in no group any more. */
	void Release();

	TArray<TWeakObjectPtr<UDreamRectBlock>> Members;
	TWeakObjectPtr<UDreamRectBlock> Leader;
	FDreamUIShapeBlendShape LeaderShape;
	FBox2f LeaderBounds = FBox2f(ForceInit);
	FDelegateHandle EditorTickHandle;
};
