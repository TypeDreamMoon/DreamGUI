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
/** Same three states size, colour and sup/sub already had, for the four font-style flags. */
UENUM(BlueprintType)
enum class EDreamUIRichTextCustomStyleData_BoolType : uint8
{
	KeepOrigin,
	On,
	Off,
};

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIRichTextCustomStyleItemData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIRichTextCustomStyleData_BoolType boldType = EDreamUIRichTextCustomStyleData_BoolType::KeepOrigin;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIRichTextCustomStyleData_BoolType italicType = EDreamUIRichTextCustomStyleData_BoolType::KeepOrigin;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIRichTextCustomStyleData_BoolType underlineType = EDreamUIRichTextCustomStyleData_BoolType::KeepOrigin;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIRichTextCustomStyleData_BoolType strikethroughType = EDreamUIRichTextCustomStyleData_BoolType::KeepOrigin;
	/**
	 * What boldType and friends replaced. A style used to force all four flags, so a custom tag inside
	 * a <b> turned the bold off again; there was no way to say "leave it alone". These are only still
	 * here to carry an asset authored before the enums, and UpgradeLegacyBools folds them in on load.
	 */
	UPROPERTY()
		bool bold = false;
	UPROPERTY()
		bool italic = false;
	UPROPERTY()
		bool underline = false;
	UPROPERTY()
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
	/** Folds a pre-enum asset's four bools into the enums. True when it changed something. */
	bool UpgradeLegacyBools();
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
	virtual void PostLoad() override;
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
