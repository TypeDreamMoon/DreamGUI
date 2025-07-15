// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/LexWidget.h"
#include "Components/ActorComponent.h"
#include "Core/LGUILifeCycleBehaviour.h"
#include "LGUILifeCycleUIBehaviour.generated.h"

class ULexWidget;
/**
 * Base class for LGUI's life cycle behviour related component, which is special made for UI, contains some easy-to-use event/functions.
 * This type of component should be attached to an actor which have UIItem as RootComponent, so the UI's callback will execute (OnUIXXX).
 */
UCLASS(ClassGroup = (LGUI), Abstract, Blueprintable, HideCategories=(Activation), DisplayName = "LGUI LifeCycle UI Behaviour")
class LGUI_API ULGUILifeCycleUIBehaviour : public ULGUILifeCycleBehaviour
{
	GENERATED_BODY()	
public:	
	ULGUILifeCycleUIBehaviour();
protected:
	friend class ULexWidget;

	virtual void OnRegister()override;
	virtual void OnUnregister()override;
public:
	
	UFUNCTION(BlueprintCallable, Category = "LGUILifeCycleBehaviour", meta = (DisplayName="Get Root UI Component"))
		ULexWidget* GetRootUIComponent() const;
private:
	enum class ECallbackFunctionType :int32
	{
		OnIsEnabledChanged,
		OnTransformChanged,
		OnDimensionsChanged,
		OnChildDimensionsChanged,
		OnAttachmentChanged,
		OnSiblingIndexChanged,
		OnRenderVisibilityChanged,
		OnLayoutVisibilityChanged,
		OnHitTestVisibilityChanged,
		COUNT,
	};
	/** Some UI callback functions want to execute before Awake, but most behaviours should executed inside or after Awake. So use this array to cache these callbacks and execute when Awake called. */
	TArray<TFunction<void()>> CallbacksBeforeAwake;
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	friend class ULexWidget;
	/** This owner's RootComponent cast to UIItem */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexWidget> RootUIComp = nullptr;
	/** Check and get RootUIItem */
	UFUNCTION(BlueprintCallable, Category = LGUI)
	bool CheckRootUIComponent() const;

	virtual void Call_Awake()override;
	
	/** Called when RootUIComp IsEnabled state is changed */
	virtual void OnIsEnabledChanged(bool IsEnabled);
	virtual void OnTransformChanged();
	/** Called when RootUIComp->AnchorData is changed or scale is changed. */
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged);
	virtual void OnChildDimensionsChanged(ULexWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged);
	/** Called when RootUIComp attach to a new parent */
	virtual void OnAttachmentChanged();
	virtual void OnSiblingIndexChanged();
	virtual void OnRenderVisibilityChanged();
	virtual void OnLayoutVisibilityChanged();
	virtual void OnHitTestVisibilityChanged();

	void Call_OnIsEnabledChanged(bool IsEnabled);
	void Call_OnTransformChanged();
	void Call_OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged);
	void Call_OnChildDimensionsChanged(ULexWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged);
	void Call_OnAttachmentChanged();
	void Call_OnSiblingIndexChanged();
	void Call_OnRenderVisibilityChanged();
	void Call_OnLayoutVisibilityChanged();
	void Call_OnHitTestVisibilityChanged();

	/** Called when RootUIComp IsActiveInHierarchy state is changed */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnIsEnabledChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnIsEnabledChanged(bool IsEnabled);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnTransformChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnTransformChanged();
	/** Called when RootUIComp->AnchorData is changed  or scale is changed. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnDimensionsChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnChildDimensionsChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnChildDimensionsChanged(ULexWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged);
	/** Called when RootUIComp attach to a new parent */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnAttachmentChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnAttachmentChanged();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnSiblingIndexChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnSiblingIndexChanged();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnRenderVisibilityChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnRenderVisibilityChanged();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnLayoutVisibilityChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnLayoutVisibilityChanged();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnHitTestVisibilityChanged"), Category = "LGUILifeCycleBehaviour") void ReceiveOnHitTestVisibilityChanged();
};