// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamContentWidget.generated.h"

class UDreamWidget;

/** Single-child host equivalent to UContentWidget. */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamContentWidget : public UDreamUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ContentWidget", meta = (AllowPrivateAccess = true))
	TObjectPtr<UDreamWidget> Content = nullptr;
	virtual void OnRegister() override;
	virtual void OnWidgetChildAttached(UDreamWidget* Child) override;
	virtual void OnWidgetChildDetached(UDreamWidget* Child) override;
	void SynchronizeContentFromChildren();

public:
	virtual int32 GetMaxWidgetChildren() const override { return 1; }
	UFUNCTION(BlueprintPure, Category = "ContentWidget")
	UDreamWidget* GetContent()const;
	UFUNCTION(BlueprintCallable, Category = "ContentWidget")
	bool SetContent(UDreamWidget* NewContent);
	/** ContentWidget always removes its actual child; bDetach is retained for Blueprint compatibility. */
	UFUNCTION(BlueprintCallable, Category = "ContentWidget")
	void ClearContent(bool bDetach = true);
	UFUNCTION(BlueprintPure, Category = "ContentWidget")
	bool CanAcceptChild(const UDreamWidget* Child)const;
};

/** Named child attachment points equivalent to INamedSlotInterface. */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamNamedSlotHost : public UDreamUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NamedSlots", meta = (AllowPrivateAccess = true))
	TMap<FName, TObjectPtr<UDreamWidget>> NamedSlots;
	virtual void OnRegister() override;
	virtual void OnWidgetChildDetached(UDreamWidget* Child) override;
	void SynchronizeNamedSlots();

public:
	UFUNCTION(BlueprintCallable, Category = "NamedSlots")
	bool SetContentForSlot(FName SlotName, UDreamWidget* Content);
	UFUNCTION(BlueprintPure, Category = "NamedSlots")
	UDreamWidget* GetContentForSlot(FName SlotName)const;
	UFUNCTION(BlueprintCallable, Category = "NamedSlots")
	void ClearSlot(FName SlotName, bool bDetach = true);
	UFUNCTION(BlueprintPure, Category = "NamedSlots")
	TArray<FName> GetSlotNames()const;
};
