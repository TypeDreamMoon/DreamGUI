// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamMeshModifierBase.h"
#include "DreamMeshModifierTextAnimation.generated.h"

struct FDreamMeshModifierTextAnimation_SelectResult
{
public:
	//start index
	int StartCharIndex = 0;
	//end index + 1
	int EndCharCount = 0;
	TArray<float> LerpValueArray;
};

UCLASS(ClassGroup = (DreamGUI), Abstract, BlueprintType, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamMeshModifierTextAnimation_Selector : public UObject
{
	GENERATED_BODY()
protected:
	/** 
	 * 0 means *Properties* will have no effect, 1 means *Properties* have full effect, and middle value is interplation.
	 * So we can set this "offset" property to make animation.
	 */
	UPROPERTY(EditAnywhere, Category = "Property", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float Offset = 0.5f;
	UDreamText* GetDreamText()const;
	class UDreamMeshModifierTextAnimation* GetUIEffectTextAnimation()const;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
private:
	mutable TWeakObjectPtr<class UDreamMeshModifierTextAnimation> UIEffectTextAnimation = nullptr;
public:
	virtual bool Select(UDreamText* InUIText, FDreamMeshModifierTextAnimation_SelectResult& OutSelection) PURE_VIRTUAL(UUIEffectTextAnimation_Selector::Select, return false;);
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetOffset()const { return Offset; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetOffset(float Value);
};

UCLASS(ClassGroup = (DreamGUI), Abstract, BlueprintType, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamMeshModifierTextAnimation_Property : public UObject
{
	GENERATED_BODY()
protected:
	UDreamText* GetDreamText();
	void MarkUITextPositionDirty();
public:
	virtual void Init() {};
	virtual void Deinit() {};
	virtual void ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) PURE_VIRTUAL(UUIEffectTextAnimation_Property::ApplyEffect, );
};

//per character animation control for DreamText
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName="TextAnimation")
class DREAMGUI_API UDreamMeshModifierTextAnimation : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:	
	UDreamMeshModifierTextAnimation();
protected:
	/** Selector defines the method to select characters in text */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Instanced)
		TObjectPtr<UDreamMeshModifierTextAnimation_Selector> Selector;
	/** Properties defines which property will affect and how */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Instanced)
		TArray<TObjectPtr<UDreamMeshModifierTextAnimation_Property>> Properties;
	/** This is just a agent to selector's offset property, for Sequencer access it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		mutable float SelectorOffset = 0.0f;

	UPROPERTY(Transient)TObjectPtr<UDreamText> TextObject;
	FDreamMeshModifierTextAnimation_SelectResult Selection;
	bool CheckDreamText();
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
public:
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	)override;
	UDreamText* GetDreamText();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamMeshModifierTextAnimation_Selector* GetSelector()const { return Selector; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TArray<UDreamMeshModifierTextAnimation_Property*>& GetProperties()const { return Properties; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamMeshModifierTextAnimation_Property* GetProperty(int Index)const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetSelectorOffset()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSelector(UDreamMeshModifierTextAnimation_Selector* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetProperties(const TArray<UDreamMeshModifierTextAnimation_Property*>& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetProperty(int Index, UDreamMeshModifierTextAnimation_Property* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSelectorOffset(float Value);
};
