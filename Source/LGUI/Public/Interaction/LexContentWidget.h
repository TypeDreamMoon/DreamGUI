// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIBehaviour.h"
#include "LexContentWidget.generated.h"

class ULexWidget;

/** Single-child host equivalent to UContentWidget. */
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexContentWidget : public ULexUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ContentWidget", meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexWidget> Content = nullptr;
	virtual void OnRegister() override;

public:
	UFUNCTION(BlueprintPure, Category = "ContentWidget")
	ULexWidget* GetContent()const { return Content; }
	UFUNCTION(BlueprintCallable, Category = "ContentWidget")
	bool SetContent(ULexWidget* NewContent);
	UFUNCTION(BlueprintCallable, Category = "ContentWidget")
	void ClearContent(bool bDetach = true);
	UFUNCTION(BlueprintPure, Category = "ContentWidget")
	bool CanAcceptChild(const ULexWidget* Child)const;
};

/** Named child attachment points equivalent to INamedSlotInterface. */
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexNamedSlotHost : public ULexUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NamedSlots", meta = (AllowPrivateAccess = true))
	TMap<FName, TObjectPtr<ULexWidget>> NamedSlots;

public:
	UFUNCTION(BlueprintCallable, Category = "NamedSlots")
	bool SetContentForSlot(FName SlotName, ULexWidget* Content);
	UFUNCTION(BlueprintPure, Category = "NamedSlots")
	ULexWidget* GetContentForSlot(FName SlotName)const;
	UFUNCTION(BlueprintCallable, Category = "NamedSlots")
	void ClearSlot(FName SlotName, bool bDetach = true);
	UFUNCTION(BlueprintPure, Category = "NamedSlots")
	TArray<FName> GetSlotNames()const;
};
