// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIImageBrush.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamUIML.generated.h"

class UDreamVisual;
class UDreamUIMLBehaviour;
class UDreamUIBehaviour;
class UDreamUIPrefab;
class UDreamUISpriteData_BaseObject;
class UDreamUIFontData_BaseObject;
class UDreamWidget;
class UDreamCanvas;
class FXmlNode;

/**
 * Provide resources for DreamUIML.
 */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIMLResource : public UObject
{
	GENERATED_BODY()

public:
	// --- Resource getters (fall back to default if key not found) ---

	UFUNCTION(BlueprintCallable, Category = "DreamUIML")
	UTexture* GetTexture(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "DreamUIML")
	UDreamUISpriteData_BaseObject* GetSprite(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "DreamUIML")
	UMaterialInterface* GetMaterial(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "DreamUIML")
	bool GetImageBrush(const FString& Key, FDreamUIImageBrush& OutResult) const;

	UFUNCTION(BlueprintCallable, Category = "DreamUIML")
	UDreamUIFontData_BaseObject* GetFont(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "DreamUIML")
	UDreamUIPrefab* GetPrefab(const FString& Key) const;

	UFUNCTION(BlueprintCallable, Category = "DreamUIML")
	TSubclassOf<UDreamUIMLBehaviour> GetTemplate(const FString& Key) const;

private:
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<UTexture>> Textures;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<UDreamUISpriteData_BaseObject>> Sprites;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, FDreamUIImageBrush> ImageBrushes;

	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<UDreamUIFontData_BaseObject>> Fonts;
	
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TObjectPtr<UDreamUIPrefab>> Prefabs;
	
	UPROPERTY(EditAnywhere, meta = (AllowPrivateAccess = true))
	TMap<FString, TSubclassOf<UDreamUIMLBehaviour>> Templates;
};

struct FDreamUIML_DataContainer
{
	TMap<FString, TWeakObjectPtr<UObject>> MapIdNameToObject;
};

struct FDreamUIML_DeferredObjectReference
{
	TWeakObjectPtr<UObject> Target;
	FString PropertyName;
	FString Reference;
};

struct FDreamUIML_PropertyBinding
{
	TWeakObjectPtr<UObject> Source;
	TWeakObjectPtr<UObject> Target;
	FName SourceProperty;
	FName TargetProperty;
	bool bNegateBoolean = false;
};

/** Runtime host for one-way UIML property bindings. */
UCLASS(Transient)
class DREAMGUI_API UDreamUIMLBindingBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	bool AddBinding(UObject* Source, const FString& SourceProperty, UObject* Target, const FString& TargetProperty);
	void RefreshBindings();

protected:
	virtual void Tick(float DeltaTime) override;

private:
	TArray<FDreamUIML_PropertyBinding> Bindings;
};

/**
 * Internal helper: wraps a parameterized UFUNCTION call so it can be bound
 * to a no-parameter delegate. Created and managed by BindXMLEvents.
 */
UCLASS()
class DREAMGUI_API UDreamUIMLEventBinding : public UObject
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UObject> Target;
	FName FunctionName;
	FString ParamString;

	TSharedPtr<FDreamUIML_DataContainer> DataContainer;

	UFUNCTION()
	void Execute();

	/** Cached parameter buffer, built lazily on first Execute call. */
	TArray<uint8> CachedParams;
	bool bParamsCached = false;
};

/**
 * XAML parser — loads XML and builds UDreamWidget hierarchies at runtime.
 *
 * Usage:
 *   UDreamUIMLBehaviour* UI = FDreamUIMLUtils(false).LoadFromFile(
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
 *   DisplayName  — set UDreamWidget::DisplayName (backward compat: Name)
 *   IdName       — unique id for event parameter reference via "IdName:xxx"
 *   VarName      — expose widget as a UPROPERTY on the Behaviour
 *   Src          — resource key for Image/Texture/Sprite
 *   Font         — font resource key for Text (alias for Src on Text)
 *   Color        — tint color: comma-separated RGBA or hex (#RRGGBBAA)
 *   Pivot / AnchorMin / AnchorMax / AnchoredPosition / SizeDelta  — bare AnchorData fields
 *   RelativeLocation.X etc. — dotted path for nested structs
 *
 * Tag summary:
 *   Widget           — plain UDreamWidget (no Visual)
 *   Image            — UDreamWidget + UDreamImage          (Src → ImageBrushes/Textures/Sprites/Materials)
 *   Text             — UDreamWidget + UDreamText           (Font → Fonts)
 *   Texture          — UDreamWidget + UDreamTexture        (Src → Textures)
 *   Sprite           — UDreamWidget + UDreamSprite         (Src → Sprites)
 *   Component        — attach a UDreamUIBehaviour         (Class aliases: Button, TextInput, Toggle, Slider)
 *   Slot / Slot:Name — placeholder or named slot        (binds to Behaviour slots)
 *   Prefab:Key       — instantiate UDreamUIPrefab from Resources.Prefabs
 *   Template:Key     — instantiate UDreamUIMLBehaviour subclass from Resources.Templates
 *
 * Event attributes (Event:OnClick, Event:OnValueChanged, etc.):
 *   Event:OnClick="FuncName"                  → Behaviour->FuncName()
 *   Event:OnClick="FuncName,123,test"         → Behaviour->FuncName(123, "test")
 *   Event:OnClick="FuncName,IdName:MySprite"  → resolves IdName to UDreamWidget* param
 *
 * Component Class accepts aliases, reflected class names, and full object paths such as
 * /Script/DreamGUI.UIButton. Component attributes and nested property elements are imported
 * through reflection; VarName and IdName refer to the created component.
 * Object properties may use forward references: IdName:Name resolves the named object,
 * Widget:Name resolves its host widget, and Visual:Name resolves its visual.
 * One-way bindings use Bind:TargetProperty="SourceProperty". Prefix a boolean source
 * with ! to invert it, for example Bind:WidgetActive="!bLoading".
 */
struct DREAMGUI_API FDreamUIMLUtils
{
	FDreamUIMLUtils(bool InIsSubTemplate, TFunction<void(const TArray<UDreamWidget*>&)> InAllWidgetsCreated);
	/**
	 * Load XML from a file path and build the widget tree.
	 * @return The root UDreamWidget created, or nullptr on failure.
	 */
	UDreamUIMLBehaviour* LoadFromFile(UWorld* World, UDreamWidget* Parent, TSubclassOf<UDreamUIMLBehaviour> Class, UDreamUIMLResource* Resources, const FString& FilePath);

	/**
	 * Load XML from a string and build the widget tree.
	 * @return The root UDreamWidget created, or nullptr on failure.
	 */
	UDreamUIMLBehaviour* LoadFromString(UWorld* World, UDreamWidget* Parent, TSubclassOf<UDreamUIMLBehaviour> Class, UDreamUIMLResource* Resources, const FString& XmlString);
	/** Validate markup without creating objects. Returns false and fills OutErrors for semantic or XML errors. */
	static bool ValidateString(const FString& XmlString, UClass* ScriptClass, TArray<FString>& OutErrors);
	/** Load and validate a UIML file without creating objects. */
	static bool ValidateFile(const FString& FilePath, UClass* ScriptClass, TArray<FString>& OutErrors);

	/** Set a single property from string via typed setters (avoids ImportText format issues). */
	static void SetPropertyValueFromString(FProperty* Property, void* ValuePtr, const FString& ValueStr, UObject* Owner);
	/** Resolve a native or reflected DreamUI behaviour class. Includes common control aliases. */
	static UClass* ResolveBehaviourClass(const FString& ClassName);

private:
	/** Apply a named property from a string value using reflection. Supports "Prop.SubProp" paths. */
	bool ApplyPropertyValue(UObject* Target, const FString& PropertyName, const FString& ValueStr);
	void ResolveDeferredObjectReferences();
	UObject* ResolveObjectReference(const FString& Reference, UClass* ExpectedClass) const;
	void ParseBindings(const FXmlNode* XmlNode, const TArray<UObject*>& TargetCandidates, UDreamUIMLBehaviour* EventContext);
	
	UDreamUIMLBehaviour* ParseWidgetElement(const FXmlNode* WidgetNode, UClass* VisualClass, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass);
	UDreamUIMLBehaviour* ParsePrefabElement(const FXmlNode* PrefabNode, UDreamUIPrefab* Prefab, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass);
	UDreamUIMLBehaviour* ParseTemplateElement(const FXmlNode* TemplateNode, TSubclassOf<UDreamUIMLBehaviour> TemplateClass, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass);
	void ParseComponentElement(const FXmlNode* ComponentNode, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext);
	void ParsePropertyElement(const FXmlNode* PropNode, UObject* TargetObject);

	/** Create an empty placeholder widget for a <Slot> element and bind it to the Behaviour's slot properties. */
	void ParseSlotElement(const FXmlNode* SlotNode, const FString& SlotName, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext);

	/** Process child XML elements (Prefab/Template/Widget/Property/Slot) for a parent widget. */
	void ProcessChildElements(const TArray<FXmlNode*>& Children, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass);

	/** Process children of a <Template:XXX> node, mapping them to slots on the TemplateBehaviour. */
	bool ProcessTemplateChildElements(const TArray<FXmlNode*>& Children, UDreamUIMLBehaviour* TemplateBehaviour, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass);

	/** Apply deferred LayoutContainer/LayoutSelf sub-properties after anchor data is set. */
	void ApplyDeferredLayoutProps(UDreamWidget* Widget, const TArray<TPair<FString, FString>>& DeferredContainer, const TArray<TPair<FString, FString>>& DeferredSelf);

	/** Bind VarName attribute: set a named property on EventContext to the created widget (or its Visual). */
	void BindVarName(UDreamUIMLBehaviour* EventContext, const FString& VarName, UDreamWidget* Widget, UDreamVisual* Visual) const;
	void BindObjectName(UDreamUIMLBehaviour* EventContext, const FString& VarName, const TArray<UObject*>& Candidates) const;

	/** Bind XML event attributes (OnClick, etc.) to widget components. */
	void BindXMLEvents(UDreamWidget* Widget, const FXmlNode* XmlNode, UObject* EventContext, UDreamUIBehaviour* ComponentFilter = nullptr);

	/** Parse <PropertyGroup> elements and populate the PropertyGroups map. */
	void ParsePropertyGroups(const TArray<FXmlNode*>& Children);

	/** Internal: recursively parse PropertyGroups, handling <Include> elements. */
	void ParsePropertyGroups_Internal(const TArray<FXmlNode*>& Children, TSet<FString>& VisitedIncludes);

	/** Apply Style attributes: expand comma-separated PropertyGroup names and write into OutAttrs. */
	void ApplyStyleAttributes(const FString& Style, TMap<FString, FString>& OutAttrs) const;

	/** Cached PropertyGroup definitions: Name → {AttrName, AttrValue}. */
	TMap<FString, TArray<TPair<FString, FString>>> PropertyGroups;

	bool bIsSubTemplate = false;
	TFunction<void(const TArray<UDreamWidget*>&)> OnAllWidgetsCreated = nullptr;
	UDreamUIMLResource* Resources = nullptr;
	UWorld* World = nullptr;

	TWeakObjectPtr<UDreamWidget> DefaultSlot;
	TMap<FString, TWeakObjectPtr<UDreamWidget>> NamedSlots;
	TSharedPtr<FDreamUIML_DataContainer> DataContainer;
	TArray<UDreamUIMLEventBinding*> EventBindings;
	TArray<FDreamUIML_DeferredObjectReference> DeferredObjectReferences;
	
	TArray<UDreamWidget*> AllWidgets;
};
