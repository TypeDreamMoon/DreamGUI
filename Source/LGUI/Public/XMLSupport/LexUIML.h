// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIImageBrush.h"
#include "LexUIML.generated.h"

class ULexVisual;
class ULexUIMLBehaviour;
class ULexUIBehaviour;
class ULexUIPrefab;
class ULexUISpriteData_BaseObject;
class ULexUIFontData_BaseObject;
class ULexWidget;
class ULexCanvas;
class FXmlNode;

/**
 * Provide resources for LexUIML.
 */
UCLASS(BlueprintType)
class LGUI_API ULexUIMLResource : public UObject
{
	GENERATED_BODY()

public:
	// --- Resource getters (fall back to default if key not found) ---

	UFUNCTION(BlueprintCallable, Category = "LexUIML")
	UTexture* GetTexture(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "LexUIML")
	ULexUISpriteData_BaseObject* GetSprite(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "LexUIML")
	UMaterialInterface* GetMaterial(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "LexUIML")
	bool GetImageBrush(const FString& Key, FLexUIImageBrush& OutResult) const;

	UFUNCTION(BlueprintCallable, Category = "LexUIML")
	ULexUIFontData_BaseObject* GetFont(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "LexUIML")
	ULexUIPrefab* GetPrefab(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "LexUIML")
	TSubclassOf<ULexUIMLBehaviour> GetTemplate(const FString& Key) const;

private:
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<UTexture>> Textures;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<ULexUISpriteData_BaseObject>> Sprites;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, FLexUIImageBrush> ImageBrushes;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<ULexUIFontData_BaseObject>> Fonts;
	
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<ULexUIPrefab>> Prefabs;
	
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TSubclassOf<ULexUIMLBehaviour>> Templates;
};

struct FLexUIML_DataContainer
{
	TMap<FString, TWeakObjectPtr<UObject>> MapIdNameToObject;
};

struct FLexUIML_DeferredObjectReference
{
	TWeakObjectPtr<UObject> Target;
	FString PropertyName;
	FString Reference;
};

/**
 * Internal helper: wraps a parameterized UFUNCTION call so it can be bound
 * to a no-parameter delegate. Created and managed by BindXMLEvents.
 */
UCLASS()
class LGUI_API ULexUIMLEventBinding : public UObject
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UObject> Target;
	FName FunctionName;
	FString ParamString;

	TSharedPtr<FLexUIML_DataContainer> DataContainer;

	UFUNCTION()
	void Execute();

	/** Cached parameter buffer, built lazily on first Execute call. */
	TArray<uint8> CachedParams;
	bool bParamsCached = false;
};

/**
 * XAML parser — loads XML and builds ULexWidget hierarchies at runtime.
 *
 * Usage:
 *   ULexUIMLBehaviour* UI = FLexUIMLUtils(false).LoadFromFile(
 *       GetWorld(), Parent, UMyUIScript::StaticClass(), Resources, TEXT("D:/ui/layout.xaml"));
 *
 * XML example:
 * @code{xml}
 * <?xml version="1.0" encoding="UTF-8"?>
 * <Widget DisplayName="RootPanel"
 *   VarName="RootPanel"
 *   Pivot="0.5,0.5"
 *   AnchorMin="0,0" AnchorMax="1,1"
 *   SizeDelta="1920,1080">
 *
 *   <Image DisplayName="Icon" Src="MyIcon" VarName="MainIcon"
 *     SizeDelta="64,64"
 *     RelativeLocation.X="16" RelativeLocation.Y="16"/>
 *
 *   <Text DisplayName="Title" Font="DefaultFont"
 *     SizeDelta="200,40" Text="Hello" FontSize="24"/>
 *
 *   <Sprite DisplayName="Avatar" Src="AvatarSprite" IdName="MySprite"
 *     SizeDelta="128,128"/>
 *
 *   <Image DisplayName="OkButton" SizeDelta="160,48">
 *     <Component Class="Button" VarName="OkButton"
 *       Event:OnClick="OnOkClicked"/>
 *   </Image>
 *
 *   <Prefab:Button DisplayName="OkBtn"
 *     SizeDelta="160,48"
 *     Event:OnClick="OnOkClicked,1,test,IdName:MySprite">
 *     <Slot>
 *       <Text Font="DefaultFont" Text="OK" FontSize="16"/>
 *     </Slot>
 *   </Prefab:Button>
 *
 *   <Template:CustomPanel DisplayName="Panel"
 *     SizeDelta="300,200">
 *     <Slot:HeaderSlot>
 *       <Text Font="DefaultFont" Text="Header"/>
 *     </Slot:HeaderSlot>
 *   </Template:CustomPanel>
 * </Widget>
 * @endcode
 *
 * Attribute summary:
 *   DisplayName  — set ULexWidget::DisplayName (backward compat: Name)
 *   IdName       — unique id for event parameter reference via "IdName:xxx"
 *   VarName      — expose widget as a UPROPERTY on the Behaviour
 *   Src          — resource key for Image/Texture/Sprite
 *   Font         — font resource key for Text (alias for Src on Text)
 *   Color        — tint color: comma-separated RGBA or hex (#RRGGBBAA)
 *   Pivot / AnchorMin / AnchorMax / AnchoredPosition / SizeDelta  — bare AnchorData fields
 *   RelativeLocation.X etc. — dotted path for nested structs
 *
 * Tag summary:
 *   Widget           — plain ULexWidget (no Visual)
 *   Image            — ULexWidget + ULexImage          (Src → ImageBrushes/Textures/Sprites/Materials)
 *   Text             — ULexWidget + ULexText           (Font → Fonts)
 *   Texture          — ULexWidget + ULexTexture        (Src → Textures)
 *   Sprite           — ULexWidget + ULexSprite         (Src → Sprites)
 *   Component        — attach a ULexUIBehaviour         (Class aliases: Button, TextInput, Toggle, Slider)
 *   Slot / Slot:Name — placeholder or named slot        (binds to Behaviour slots)
 *   Prefab:Key       — instantiate ULexUIPrefab from Resources.Prefabs
 *   Template:Key     — instantiate ULexUIMLBehaviour subclass from Resources.Templates
 *
 * Event attributes (Event:OnClick, Event:OnValueChanged, etc.):
 *   Event:OnClick="FuncName"                  → Behaviour->FuncName()
 *   Event:OnClick="FuncName,123,test"         → Behaviour->FuncName(123, "test")
 *   Event:OnClick="FuncName,IdName:MySprite"  → resolves IdName to ULexWidget* param
 *
 * Component Class accepts aliases, reflected class names, and full object paths such as
 * /Script/LGUI.UIButton. Component attributes and nested property elements are imported
 * through reflection; VarName and IdName refer to the created component.
 * Object properties may use forward references: IdName:Name resolves the named object,
 * Widget:Name resolves its host widget, and Visual:Name resolves its visual.
 */
struct LGUI_API FLexUIMLUtils
{
	FLexUIMLUtils(bool InIsSubTemplate, TFunction<void(const TArray<ULexWidget*>&)> InAllWidgetsCreated);
	/**
	 * Load XML from a file path and build the widget tree.
	 * @return The root ULexWidget created, or nullptr on failure.
	 */
	ULexUIMLBehaviour* LoadFromFile(UWorld* World, ULexWidget* Parent, TSubclassOf<ULexUIMLBehaviour> Class, ULexUIMLResource* Resources, const FString& FilePath);

	/**
	 * Load XML from a string and build the widget tree.
	 * @return The root ULexWidget created, or nullptr on failure.
	 */
	ULexUIMLBehaviour* LoadFromString(UWorld* World, ULexWidget* Parent, TSubclassOf<ULexUIMLBehaviour> Class, ULexUIMLResource* Resources, const FString& XmlString);

	/** Set a single property from string via typed setters (avoids ImportText format issues). */
	static void SetPropertyValueFromString(FProperty* Property, void* ValuePtr, const FString& ValueStr, UObject* Owner);
	/** Resolve a native or reflected LexUI behaviour class. Includes common control aliases. */
	static UClass* ResolveBehaviourClass(const FString& ClassName);

private:
	/** Apply a named property from a string value using reflection. Supports "Prop.SubProp" paths. */
	bool ApplyPropertyValue(UObject* Target, const FString& PropertyName, const FString& ValueStr);
	void ResolveDeferredObjectReferences();
	UObject* ResolveObjectReference(const FString& Reference, UClass* ExpectedClass) const;
	
	ULexUIMLBehaviour* ParseWidgetElement(const FXmlNode* WidgetNode, UClass* VisualClass, ULexWidget* ParentWidget, ULexUIMLBehaviour* EventContext, UClass* ScriptClass);
	ULexUIMLBehaviour* ParsePrefabElement(const FXmlNode* PrefabNode, ULexUIPrefab* Prefab, ULexWidget* ParentWidget, ULexUIMLBehaviour* EventContext, UClass* ScriptClass);
	ULexUIMLBehaviour* ParseTemplateElement(const FXmlNode* TemplateNode, TSubclassOf<ULexUIMLBehaviour> TemplateClass, ULexWidget* ParentWidget, ULexUIMLBehaviour* EventContext, UClass* ScriptClass);
	void ParseComponentElement(const FXmlNode* ComponentNode, ULexWidget* ParentWidget, ULexUIMLBehaviour* EventContext);
	void ParsePropertyElement(const FXmlNode* PropNode, UObject* TargetObject);

	/** Create an empty placeholder widget for a <Slot> element and bind it to the Behaviour's slot properties. */
	void ParseSlotElement(const FXmlNode* SlotNode, const FString& SlotName, ULexWidget* ParentWidget, ULexUIMLBehaviour* EventContext);

	/** Process child XML elements (Prefab/Template/Widget/Property/Slot) for a parent widget. */
	void ProcessChildElements(const TArray<FXmlNode*>& Children, ULexWidget* ParentWidget, ULexUIMLBehaviour* EventContext, UClass* ScriptClass);

	/** Process children of a <Template:XXX> node, mapping them to slots on the TemplateBehaviour. */
	bool ProcessTemplateChildElements(const TArray<FXmlNode*>& Children, ULexUIMLBehaviour* TemplateBehaviour, ULexUIMLBehaviour* EventContext, UClass* ScriptClass);

	/** Apply deferred LayoutContainer/LayoutSelf sub-properties after anchor data is set. */
	void ApplyDeferredLayoutProps(ULexWidget* Widget, const TArray<TPair<FString, FString>>& DeferredContainer, const TArray<TPair<FString, FString>>& DeferredSelf);

	/** Bind VarName attribute: set a named property on EventContext to the created widget (or its Visual). */
	void BindVarName(ULexUIMLBehaviour* EventContext, const FString& VarName, ULexWidget* Widget, ULexVisual* Visual) const;
	void BindObjectName(ULexUIMLBehaviour* EventContext, const FString& VarName, const TArray<UObject*>& Candidates) const;

	/** Bind XML event attributes (OnClick, etc.) to widget components. */
	void BindXMLEvents(ULexWidget* Widget, const FXmlNode* XmlNode, UObject* EventContext, ULexUIBehaviour* ComponentFilter = nullptr);

	/** Parse <PropertyGroup> elements and populate the PropertyGroups map. */
	void ParsePropertyGroups(const TArray<FXmlNode*>& Children);

	/** Internal: recursively parse PropertyGroups, handling <Include> elements. */
	void ParsePropertyGroups_Internal(const TArray<FXmlNode*>& Children, TSet<FString>& VisitedIncludes);

	/** Apply Style attributes: expand comma-separated PropertyGroup names and write into OutAttrs. */
	void ApplyStyleAttributes(const FString& Style, TMap<FString, FString>& OutAttrs) const;

	/** Cached PropertyGroup definitions: Name → {AttrName, AttrValue}. */
	TMap<FString, TArray<TPair<FString, FString>>> PropertyGroups;

	bool bIsSubTemplate = false;
	TFunction<void(const TArray<ULexWidget*>&)> OnAllWidgetsCreated = nullptr;
	ULexUIMLResource* Resources = nullptr;
	UWorld* World = nullptr;

	TWeakObjectPtr<ULexWidget> DefaultSlot;
	TMap<FString, TWeakObjectPtr<ULexWidget>> NamedSlots;
	TSharedPtr<FLexUIML_DataContainer> DataContainer;
	TArray<ULexUIMLEventBinding*> EventBindings;
	TArray<FLexUIML_DeferredObjectReference> DeferredObjectReferences;
	
	TArray<ULexWidget*> AllWidgets;
};
