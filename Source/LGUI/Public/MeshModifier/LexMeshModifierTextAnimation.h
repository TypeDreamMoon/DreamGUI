// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexMeshModifierBase.h"
#include "LexMeshModifierTextAnimation.generated.h"

struct FLexMeshModifierTextAnimation_SelectResult
{
public:
	//start index
	int StartCharIndex = 0;
	//end index + 1
	int EndCharCount = 0;
	TArray<float> LerpValueArray;
};

UCLASS(ClassGroup = (LGUI), Abstract, BlueprintType, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexMeshModifierTextAnimation_Selector : public UObject
{
	GENERATED_BODY()
protected:
	/** 
	 * 0 means *Properties* will have no effect, 1 means *Properties* have full effect, and middle value is interplation.
	 * So we can set this "offset" property to make animation.
	 */
	UPROPERTY(EditAnywhere, Category = "Property", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float Offset = 0.5f;
	class ULexText* GetUIText()const;
	class ULexMeshModifierTextAnimation* GetUIEffectTextAnimation()const;
private:
	mutable TWeakObjectPtr<class ULexMeshModifierTextAnimation> UIEffectTextAnimation = nullptr;
public:
	virtual bool Select(class ULexText* InUIText, FLexMeshModifierTextAnimation_SelectResult& OutSelection) PURE_VIRTUAL(UUIEffectTextAnimation_Selector::Select, return false;);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetOffset()const { return Offset; }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetOffset(float Value);
};

UCLASS(ClassGroup = (LGUI), Abstract, BlueprintType, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexMeshModifierTextAnimation_Property : public UObject
{
	GENERATED_BODY()
protected:
	class ULexText* GetUIText();
	void MarkUITextPositionDirty();
public:
	virtual void Init() {};
	virtual void Deinit() {};
	virtual void ApplyProperty(class ULexText* InUIText, const FLexMeshModifierTextAnimation_SelectResult& InSelection, FLexUIGeometry* InGeometry) PURE_VIRTUAL(UUIEffectTextAnimation_Property::ApplyEffect, );
};

//per character animation control for UIText
UCLASS(ClassGroup = (LGUI), Blueprintable, DisplayName="TextAnimation")
class LGUI_API ULexMeshModifierTextAnimation : public ULexMeshModifierBase
{
	GENERATED_BODY()

public:	
	ULexMeshModifierTextAnimation();
protected:
	/** Selector defines the method to select characters in text */
	UPROPERTY(EditAnywhere, Category = "LGUI", Instanced)
		TObjectPtr<ULexMeshModifierTextAnimation_Selector> Selector;
	/** Properties defines which property will affect and how */
	UPROPERTY(EditAnywhere, Category = "LGUI", Instanced)
		TArray<TObjectPtr<ULexMeshModifierTextAnimation_Property>> Properties;
	/** This is just a agent to selector's offset property, for Sequencer access it. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		mutable float SelectorOffset = 0.0f;

	UPROPERTY(Transient)TObjectPtr<class ULexText> TextObject;
	FLexMeshModifierTextAnimation_SelectResult Selection;
	bool CheckUIText();
	virtual void BeginPlay()override;
	virtual void EndPlay()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
public:
	virtual void ModifyUIGeometry(FLexUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	)override;
	class ULexText* GetUIText();

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexMeshModifierTextAnimation_Selector* GetSelector()const { return Selector; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const TArray<ULexMeshModifierTextAnimation_Property*>& GetProperties()const { return Properties; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexMeshModifierTextAnimation_Property* GetProperty(int Index)const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetSelectorOffset()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSelector(ULexMeshModifierTextAnimation_Selector* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetProperties(const TArray<ULexMeshModifierTextAnimation_Property*>& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetProperty(int Index, ULexMeshModifierTextAnimation_Property* Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSelectorOffset(float Value);
};
