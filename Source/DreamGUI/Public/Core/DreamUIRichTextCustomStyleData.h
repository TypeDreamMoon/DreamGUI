// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FRichTextParser.h"
#include "DreamUIRichTextCustomStyleData.generated.h"

UENUM(BlueprintType)
enum class EDreamUIRichTextCustomStyleData_SizeType :uint8
{
	KeepOrigin,
	SizeValue,
	SizeValueAsAdditional,
};
UENUM(BlueprintType)
enum class EDreamUIRichTextCustomStyleData_ColorType : uint8
{
	KeepOrigin,
	Replace,
	Multiply,
};
UENUM(BlueprintType)
enum class EDreamUIRichTextCustomStyleData_SupOrSubType : uint8
{
	KeepOrigin,
	None,
	Superscript,
	Subscript,
};

USTRUCT(BlueprintType)
struct FDreamUIRichTextCustomStyleItemData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bold = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool italic = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool underline = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool strikethrough = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIRichTextCustomStyleData_SizeType sizeType = EDreamUIRichTextCustomStyleData_SizeType::KeepOrigin;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta=(EditCondition="sizeType!=EDreamUIRichTextCustomStyleData_SizeType::KeepOrigin"))
		int size = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIRichTextCustomStyleData_ColorType colorType = EDreamUIRichTextCustomStyleData_ColorType::KeepOrigin;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "colorType!=EDreamUIRichTextCustomStyleData_ColorType::KeepOrigin"))
		FColor color = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIRichTextCustomStyleData_SupOrSubType supOrSub = EDreamUIRichTextCustomStyleData_SupOrSubType::KeepOrigin;

	void ApplyToRichTextParseResult(DreamUIRichTextParser::FRichTextParseResult& value)const;
};

/**
 * For rich text on UIText.
 * Add your own string as tag and customize your own style.
 */
UCLASS(NotBlueprintable, BlueprintType)
class DREAMGUI_API UDreamUIRichTextCustomStyleData : public UObject
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TMap<FName, FDreamUIRichTextCustomStyleItemData> DataMap;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TMap<FName, FDreamUIRichTextCustomStyleItemData>& GetDataMap()const { return DataMap; }

	DECLARE_EVENT(UDreamUIRichTextCustomStyleData, FDreamGUIRichTextCustomStyleDataRefreshEvent);
	/** Called when any data change, and need UIText to refresh. */
	FDreamGUIRichTextCustomStyleDataRefreshEvent OnDataChange;
};
