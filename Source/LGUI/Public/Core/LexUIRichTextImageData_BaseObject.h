// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexUITextData.h"
#include "LexUIRichTextImageData_BaseObject.generated.h"

/** base class for LexText to render image inside text */
UCLASS(Abstract, BlueprintType)
class LGUI_API ULexUIRichTextImageData_BaseObject :public UObject
{
	GENERATED_BODY()
public:
	DECLARE_EVENT(ULexUIRichTextImageData_BaseObject, FLGUIRichTextImageDataRefreshEvent);
	/** Called when any data change, and need UIText to refresh. */
	FLGUIRichTextImageDataRefreshEvent OnDataChange;
	/** Create or update image object. */
	virtual void CreateOrUpdateObject(class ULexWidget* parent, const TArray<FLexUIText_RichTextImageTag>& imageTagArray, TArray<TObjectPtr<class ULexWidget>>& inOutCreatedImageObjectArray, bool listImageObjectInEditorOutliner) {};
	virtual bool GetImageSize(const FName& imageTag, FIntVector2& outSize) { return false; }
};