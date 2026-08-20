// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUITextData.h"
#include "DreamUIRichTextImageData_BaseObject.generated.h"

/** base class for DreamText to render image inside text */
UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamUIRichTextImageData_BaseObject :public UObject
{
	GENERATED_BODY()
public:
	DECLARE_EVENT(UDreamUIRichTextImageData_BaseObject, FDreamGUIRichTextImageDataRefreshEvent);
	/** Called when any data change, and need UIText to refresh. */
	FDreamGUIRichTextImageDataRefreshEvent OnDataChange;
	/** Create or update image object. */
	virtual void CreateOrUpdateObject(class UDreamWidget* parent, const TArray<FDreamUIText_RichTextImageTag>& imageTagArray, TArray<TObjectPtr<class UDreamWidget>>& inOutCreatedImageObjectArray) {};
	virtual bool GetImageSize(const FName& imageTag, FIntVector2& outSize) { return false; }
};