// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "MeshModifier/DreamMeshModifierBase.h"
#include "Core/DreamUITextData.h"
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
class DREAMGUIEXTENSIONS_API UDreamMeshModifierTextAnimation_Selector : public UObject
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
class DREAMGUIEXTENSIONS_API UDreamMeshModifierTextAnimation_Property : public UObject
{
	GENERATED_BODY()
protected:
	UDreamText* GetDreamText();
	void MarkUITextPositionDirty();
	/**
	 * Whether a painted character's vertices are there to read among InVertexCount. A glyph still on its way from
	 * the rasterizer is counted as painted with no vertices of its own, at a StartVertIndex one past the last vertex
	 * when it is the last character; a rotation or scale averaged its "centre" from the vertex past the end.
	 */
	static bool CanReadCharVertices(const FDreamUITextCharProperty& InChar, int32 InVertexCount);
public:
	virtual void Init() {};
	virtual void Deinit() {};
	/** Applies this property to InUIText's painted characters: ApplyPropertyToCharacters over GetCharPropertyArray. */
	virtual void ApplyProperty(UDreamText* InUIText, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry);
	/**
	 * The work of ApplyProperty over a character list handed in, which is where the built-in properties do it: the
	 * per-character work is then reachable with any characters at all, a character with no vertices among them,
	 * without a text to paint them.
	 *
	 * A property moves and colours the painter's quads and does nothing else to them: the texture coordinates stay
	 * as painted, UV2.x (the quad's code and paint slot) and UV4 (its place in its gradient's box) included, so a
	 * painted gradient follows each glyph and the property's colours multiply it.
	 */
	virtual void ApplyPropertyToCharacters(const TArray<FDreamUITextCharProperty>& InCharProperties, const FDreamMeshModifierTextAnimation_SelectResult& InSelection, FDreamUIGeometry* InGeometry) {}
};

//per character animation control for DreamText
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName="TextAnimation")
class DREAMGUIEXTENSIONS_API UDreamMeshModifierTextAnimation : public UDreamMeshModifierBase
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
	/**
	 * The text this animator told that it animates the characters of (UDreamText::RegisterPerCharacterAnimation), so
	 * the text lays out a glyph per character, and so the same text is let go of again when the visual is swapped or
	 * the animator unregisters.
	 */
	TWeakObjectPtr<UDreamText> RegisteredText;
	FDreamMeshModifierTextAnimation_SelectResult Selection;
	bool CheckDreamText();
	/** Moves the per-character registration to the text the modifier finds now, letting go of the one before. */
	void SyncRegisteredText();
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
public:
	/**
	 * Positions and UVs only while a property that moves glyphs is installed: the built-in Alpha, Color and ColorRandom
	 * properties do not, every other class is taken to. A text under an animation that only fades or recolours its
	 * characters keeps drawing small sizes from coverage glyphs (UDreamText::SmallTextRaster).
	 */
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)override;
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
	/** Replaces the properties. Registered, the ones going out are wound down and the ones coming in started, as on unregister and register. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetProperties(const TArray<UDreamMeshModifierTextAnimation_Property*>& Value);
	/** Replaces one property, winding the old one down and starting the new one when registered; see SetProperties. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetProperty(int Index, UDreamMeshModifierTextAnimation_Property* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSelectorOffset(float Value);
};
