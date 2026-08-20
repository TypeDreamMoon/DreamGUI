// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DreamWidgetSubObjectBehaviour.generated.h"

class UDreamWidget;
UCLASS(BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamWidgetSubObjectBehaviour : public UObject
{
	GENERATED_BODY()

public:
	void Call_OnRegister();
	void Call_OnUnregister();
	virtual void BeginPlay(){};
	virtual void EndPlay(){};
	bool IsRegistered()const{return bIsRegistered;}

	virtual void PostInitProperties() override;

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	UDreamWidget* GetWidget()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	FString GetPathDisplayName(const UObject* StopOuter = nullptr) const;
protected:
	virtual void OnRegister(){};
	virtual void OnUnregister(){};
private:
	bool bIsRegistered = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = DreamGUI, Getter=GetWidget, meta = (AllowPrivateAccess = true), DisplayName=Widget)
	mutable TObjectPtr<UDreamWidget> OwnerWidget = nullptr;
};
