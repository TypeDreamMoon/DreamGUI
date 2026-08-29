// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "XMLSupport/DreamUIML.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamSprite.h"
#include "Core/DreamUIImageBrush.h"
#include "XmlFile.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUIAnchorData.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Misc/FileHelper.h"
#include "UObject/UObjectGlobals.h"
#include "XMLSupport/DreamUIMLBehaviour.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamLayoutSelfAspectRatio.h"
#include "Core/Components/DreamLayout.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISlider.h"
#include "Interaction/UITextInput.h"
#include "Interaction/UIToggle.h"

// ============================================================================
// UDreamUIXAMLResource
// ============================================================================

UTexture* UDreamUIMLResource::GetTexture(const FString& Key) const
{
	if (const TObjectPtr<UTexture>* Found = Textures.Find(Key))
	{
		return Found->Get();
	}
	return nullptr;
}

UDreamUISpriteData_BaseObject* UDreamUIMLResource::GetSprite(const FString& Key) const
{
	if (const TObjectPtr<UDreamUISpriteData_BaseObject>* Found = Sprites.Find(Key))
	{
		return Found->Get();
	}
	return UDreamUISpriteData::GetDefaultWhiteSolid();
}

UMaterialInterface* UDreamUIMLResource::GetMaterial(const FString& Key) const
{
	if (const TObjectPtr<UMaterialInterface>* Found = Materials.Find(Key))
	{
		return Found->Get();
	}
	return nullptr;
}

bool UDreamUIMLResource::GetImageBrush(const FString& Key, FDreamUIImageBrush& OutResult) const
{
	if (const FDreamUIImageBrush* Found = ImageBrushes.Find(Key))
	{
		OutResult = *Found;
		return true;
	}
	return false;
}

UDreamUIFontData_BaseObject* UDreamUIMLResource::GetFont(const FString& Key) const
{
	if (const TObjectPtr<UDreamUIFontData_BaseObject>* Found = Fonts.Find(Key))
	{
		return Found->Get();
	}
	return UDreamUIFontData_BaseObject::GetDefaultFont();
}

TSubclassOf<UDreamUserWidget> UDreamUIMLResource::GetWidgetClass(const FString& Key) const
{
	if (const TSubclassOf<UDreamUserWidget>* Found = WidgetClasses.Find(Key))
	{
		return Found->Get();
	}
	return nullptr;
}

TSubclassOf<UDreamUIMLBehaviour> UDreamUIMLResource::GetTemplate(const FString& Key) const
{
	if (const TSubclassOf<UDreamUIMLBehaviour>* Found = Templates.Find(Key))
	{
		return *Found;
	}
	return nullptr;
}

// ============================================================================
// UDreamUIMLEventBinding
// ============================================================================

void UDreamUIMLEventBinding::Execute()
{
	UObject* Context = Target.Get();
	if (!Context) return;

	UFunction* Func = Context->FindFunction(FunctionName);
	if (!Func) return;

	if (ParamString.IsEmpty())
	{
		Context->ProcessEvent(Func, nullptr);
		return;
	}

	// Lazy-cache parameter buffer (rebuild if Behaviour map may have changed)
	if (!bParamsCached)
	{
		CachedParams.SetNumZeroed(Func->ParmsSize);

		TArray<FProperty*> ParamProps;
		for (TFieldIterator<FProperty> It(Func); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm))
				ParamProps.Add(*It);
		}

		TArray<FString> Parts;
		ParamString.ParseIntoArray(Parts, TEXT(","));
		for (int32 i = 0; i < Parts.Num() && i < ParamProps.Num(); ++i)
		{
			FString Part = Parts[i].TrimStartAndEnd();

			// Resolve IdName:xxx at runtime using Behaviour's map
			if (Part.StartsWith(TEXT("IdName:")))
			{
				if (auto* Found = DataContainer->MapIdNameToObject.Find(Part.Mid(7)))
				{
					if (UObject* Obj = Found->Get())
					{
						Part = Obj->GetPathName();
					}
				}
			}

			FDreamUIMLUtils::SetPropertyValueFromString(
				ParamProps[i],
				ParamProps[i]->ContainerPtrToValuePtr<void>(CachedParams.GetData()),
				Part, nullptr);
		}
		bParamsCached = true;
	}

	Context->ProcessEvent(Func, CachedParams.GetData());
}

// ============================================================================
// FDreamUIXAML
// ============================================================================

static FProperty* FindBindingProperty(UObject* Object, const FString& RequestedName, FString& OutResolvedName)
{
	if (!Object) return nullptr;

	OutResolvedName = RequestedName;
	FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *OutResolvedName);
	if (!Property && RequestedName == TEXT("WidgetActive"))
	{
		OutResolvedName = TEXT("bWidgetActive");
		Property = FindFProperty<FProperty>(Object->GetClass(), *OutResolvedName);
	}
	if (!Property && RequestedName == TEXT("Value") && Object->IsA<UUIToggle>())
	{
		OutResolvedName = TEXT("bIsOn");
		Property = FindFProperty<FProperty>(Object->GetClass(), *OutResolvedName);
	}
	return Property;
}

static FString ExportBindingValue(FProperty* Property, const void* Value, UObject* Source)
{
	FString Result;
	if (Property && Value)
	{
		Property->ExportTextItem_Direct(Result, Value, nullptr, Source, PPF_None);
	}
	return Result;
}

bool UDreamUIMLBindingBehaviour::AddBinding(UObject* Source, const FString& SourceProperty, UObject* Target, const FString& TargetProperty)
{
	FString TrimmedSource = SourceProperty.TrimStartAndEnd();
	const bool bNegate = TrimmedSource.RemoveFromStart(TEXT("!"));
	FString ResolvedTargetName;
	FProperty* SourceProp = Source ? FindFProperty<FProperty>(Source->GetClass(), *TrimmedSource) : nullptr;
	FProperty* TargetProp = FindBindingProperty(Target, TargetProperty, ResolvedTargetName);
	if (!SourceProp || !TargetProp)
	{
		return false;
	}
	if (bNegate && !CastField<FBoolProperty>(SourceProp))
	{
		return false;
	}

	Bindings.Add({ Source, Target, *TrimmedSource, *ResolvedTargetName, bNegate });
	RefreshBindings();
	return true;
}

void UDreamUIMLBindingBehaviour::RefreshBindings()
{
	for (const FDreamUIML_PropertyBinding& Binding : Bindings)
	{
		UObject* Source = Binding.Source.Get();
		UObject* Target = Binding.Target.Get();
		FProperty* SourceProperty = Source ? FindFProperty<FProperty>(Source->GetClass(), Binding.SourceProperty) : nullptr;
		FProperty* TargetProperty = Target ? FindFProperty<FProperty>(Target->GetClass(), Binding.TargetProperty) : nullptr;
		if (!Source || !Target || !SourceProperty || !TargetProperty) continue;

		const void* SourceValue = SourceProperty->ContainerPtrToValuePtr<void>(Source);
		if (UDreamWidget* Widget = Cast<UDreamWidget>(Target); Widget && Binding.TargetProperty == TEXT("bWidgetActive"))
		{
			if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(SourceProperty))
			{
				const bool Value = BoolProperty->GetPropertyValue(SourceValue) != Binding.bNegateBoolean;
				Widget->SetWidgetActive(Value);
			}
			continue;
		}
		if (UDreamText* Text = Cast<UDreamText>(Target); Text && Binding.TargetProperty == TEXT("Text"))
		{
			if (const FTextProperty* TextProperty = CastField<FTextProperty>(SourceProperty))
			{
				Text->SetText(TextProperty->GetPropertyValue(SourceValue));
			}
			else if (const FStrProperty* StringProperty = CastField<FStrProperty>(SourceProperty))
			{
				Text->SetText(FText::FromString(StringProperty->GetPropertyValue(SourceValue)));
			}
			else
			{
				Text->SetText(FText::FromString(ExportBindingValue(SourceProperty, SourceValue, Source)));
			}
			continue;
		}
		if (UUITextInput* TextInput = Cast<UUITextInput>(Target); TextInput && Binding.TargetProperty == TEXT("Text"))
		{
			TextInput->SetTextWithoutNotify(ExportBindingValue(SourceProperty, SourceValue, Source));
			continue;
		}
		if (UUISlider* Slider = Cast<UUISlider>(Target); Slider && Binding.TargetProperty == TEXT("Value"))
		{
			if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(SourceProperty))
			{
				const double NumericValue = NumericProperty->IsFloatingPoint()
					? NumericProperty->GetFloatingPointPropertyValue(SourceValue)
					: static_cast<double>(NumericProperty->GetSignedIntPropertyValue(SourceValue));
				Slider->SetValueWithoutNotify(static_cast<float>(NumericValue));
			}
			continue;
		}
		if (UUIToggle* Toggle = Cast<UUIToggle>(Target); Toggle && Binding.TargetProperty == TEXT("bIsOn"))
		{
			if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(SourceProperty))
			{
				Toggle->SetValueWithoutNotify(BoolProperty->GetPropertyValue(SourceValue) != Binding.bNegateBoolean);
			}
			continue;
		}
		if (Binding.bNegateBoolean)
		{
			if (const FBoolProperty* SourceBool = CastField<FBoolProperty>(SourceProperty))
			{
				if (FBoolProperty* TargetBool = CastField<FBoolProperty>(TargetProperty))
				{
					TargetBool->SetPropertyValue_InContainer(Target, !SourceBool->GetPropertyValue(SourceValue));
					continue;
				}
			}
		}

		void* TargetValue = TargetProperty->ContainerPtrToValuePtr<void>(Target);
		if (SourceProperty->SameType(TargetProperty))
		{
			TargetProperty->CopyCompleteValue(TargetValue, SourceValue);
		}
		else
		{
			FDreamUIMLUtils::SetPropertyValueFromString(TargetProperty, TargetValue, ExportBindingValue(SourceProperty, SourceValue, Source), Target);
		}
	}
}

void UDreamUIMLBindingBehaviour::Tick(float DeltaTime)
{
	RefreshBindings();
}


static bool IsWidgetElement(const FString& Tag, UClass*& Class)
{
	if (Tag == TEXT("Widget"))
	{
		Class = nullptr;
		return true;
	}
	if (Tag == TEXT("Image"))
	{
		Class = UDreamImage::StaticClass();
		return true;
	}
	if (Tag == TEXT("Text"))
	{
		Class = UDreamText::StaticClass();
		return true;
	}
	if (Tag == TEXT("Texture"))
	{
		Class = UDreamTexture::StaticClass();
		return true;
	}
	if (Tag == TEXT("Sprite"))
	{
		Class = UDreamSprite::StaticClass();
		return true;
	}
	return false;
}

/** Check if the tag is a known Prefab name in Resources. */
static bool IsPrefabElement(const FString& Tag)
{
	return Tag == TEXT("Prefab");
}

/** Check if the tag is a known Template name. */
static bool IsTemplateElement(const FString& Tag)
{
	return Tag == TEXT("Template");
}

/** Check if the tag is a Slot element. */
static bool IsSlotElement(const FString& Tag)
{
	return Tag == TEXT("Slot");
}

/** Get the Src attribute value from a Prefab/Template XML node. */
static FString GetElementSrc(const FXmlNode* Node)
{
	return Node->GetAttribute(TEXT("Src")).TrimStartAndEnd();
}

/** Get the Name attribute value from a Slot XML node (empty = default slot). Falls back to Src. */
static FString GetSlotName(const FXmlNode* Node)
{
	FString Name = Node->GetAttribute(TEXT("Name")).TrimStartAndEnd();
	if (Name.IsEmpty()) Name = Node->GetAttribute(TEXT("Src")).TrimStartAndEnd();
	return Name;
}

bool FDreamUIMLUtils::ValidateFile(const FString& FilePath, UClass* ScriptClass, TArray<FString>& OutErrors)
{
	FString XmlString;
	if (!FFileHelper::LoadFileToString(XmlString, *FilePath))
	{
		OutErrors.Add(FString::Printf(TEXT("Could not read UIML file: %s"), *FilePath));
		return false;
	}
	return ValidateString(XmlString, ScriptClass, OutErrors);
}

bool FDreamUIMLUtils::ValidateString(const FString& XmlString, UClass* ScriptClass, TArray<FString>& OutErrors)
{
	OutErrors.Reset();
	FXmlFile XmlFile;
	if (XmlString.IsEmpty() || !XmlFile.LoadFile(XmlString, EConstructMethod::ConstructFromBuffer))
	{
		OutErrors.Add(XmlString.IsEmpty() ? TEXT("UIML markup is empty") : XmlFile.GetLastError());
		return false;
	}

	const FXmlNode* Root = XmlFile.GetRootNode();
	if (!Root)
	{
		OutErrors.Add(TEXT("UIML markup has no root element"));
		return false;
	}

	const FXmlNode* ContentRoot = Root;
	if (Root->GetTag() == TEXT("DreamUIML"))
	{
		ContentRoot = nullptr;
		for (const FXmlNode* Child : Root->GetChildrenNodes())
		{
			if (Child->GetTag() != TEXT("PropertyGroup") && Child->GetTag() != TEXT("Include"))
			{
				if (ContentRoot)
				{
					OutErrors.Add(TEXT("<DreamUIML> must contain exactly one content root"));
				}
				else
				{
					ContentRoot = Child;
				}
			}
		}
	}

	UClass* RootVisualClass = nullptr;
	if (!ContentRoot || (!IsWidgetElement(ContentRoot->GetTag(), RootVisualClass)
		&& !IsPrefabElement(ContentRoot->GetTag()) && !IsTemplateElement(ContentRoot->GetTag())))
	{
		OutErrors.Add(ContentRoot
			? FString::Printf(TEXT("Unsupported UIML root element: <%s>"), *ContentRoot->GetTag())
			: TEXT("<DreamUIML> has no content root"));
		return false;
	}

	TSet<FString> IdNames;
	TArray<TPair<FString, FString>> References;
	TFunction<void(const FXmlNode*)> ValidateNode = [&](const FXmlNode* Node)
	{
		const FString& Tag = Node->GetTag();
		const FString IdName = Node->GetAttribute(TEXT("IdName")).TrimStartAndEnd();
		if (!IdName.IsEmpty())
		{
			if (IdNames.Contains(IdName))
			{
				OutErrors.Add(FString::Printf(TEXT("Duplicate IdName '%s' on <%s>"), *IdName, *Tag));
			}
			IdNames.Add(IdName);
		}

		if (Tag == TEXT("Component"))
		{
			const FString ClassName = Node->GetAttribute(TEXT("Class")).TrimStartAndEnd();
			if (!ResolveBehaviourClass(ClassName))
			{
				OutErrors.Add(FString::Printf(TEXT("Invalid component class '%s'"), *ClassName));
			}
		}

		const FString VarName = Node->GetAttribute(TEXT("VarName")).TrimStartAndEnd();
		if (ScriptClass && !VarName.IsEmpty() && !CastField<FObjectPropertyBase>(FindFProperty<FProperty>(ScriptClass, *VarName)))
		{
			OutErrors.Add(FString::Printf(TEXT("VarName '%s' is not an object property on %s"), *VarName, *ScriptClass->GetName()));
		}

		for (const FXmlAttribute& Attr : Node->GetAttributes())
		{
			const FString& AttrName = Attr.GetTag();
			const FString& AttrValue = Attr.GetValue();
			if (ScriptClass && AttrName.StartsWith(TEXT("Event:")))
			{
				FString FunctionName;
				FString Params;
				if (!AttrValue.Split(TEXT(","), &FunctionName, &Params)) FunctionName = AttrValue;
				FunctionName.TrimStartAndEndInline();
				if (!ScriptClass->FindFunctionByName(*FunctionName))
				{
					OutErrors.Add(FString::Printf(TEXT("Event function '%s' was not found on %s"), *FunctionName, *ScriptClass->GetName()));
				}
			}
			else if (ScriptClass && AttrName.StartsWith(TEXT("Bind:")))
			{
				FString SourceProperty = AttrValue.TrimStartAndEnd();
				SourceProperty.RemoveFromStart(TEXT("!"));
				if (!FindFProperty<FProperty>(ScriptClass, *SourceProperty))
				{
					OutErrors.Add(FString::Printf(TEXT("Binding source '%s' was not found on %s"), *SourceProperty, *ScriptClass->GetName()));
				}
			}

			FString Selector;
			FString ReferenceId;
			if (AttrValue.Split(TEXT(":"), &Selector, &ReferenceId)
				&& (Selector == TEXT("IdName") || Selector == TEXT("Widget") || Selector == TEXT("Visual")))
			{
				References.Add({ ReferenceId, FString::Printf(TEXT("%s.%s"), *Tag, *AttrName) });
			}
		}

		for (const FXmlNode* Child : Node->GetChildrenNodes())
		{
			ValidateNode(Child);
		}
	};
	ValidateNode(ContentRoot);

	for (const TPair<FString, FString>& Reference : References)
	{
		if (!IdNames.Contains(Reference.Key))
		{
			OutErrors.Add(FString::Printf(TEXT("Unresolved reference '%s' at %s"), *Reference.Key, *Reference.Value));
		}
	}
	return OutErrors.IsEmpty();
}

/** Resolve a LayoutContainer class name to its UClass. */
static UClass* ResolveLayoutContainerClass(const FString& Name)
{
	if (Name == TEXT("CanvasPanel"))       return UDreamLayoutContainerCanvasPanel::StaticClass();
	if (Name == TEXT("Overlay"))           return UDreamLayoutContainerOverlay::StaticClass();
	if (Name == TEXT("StackBox"))          return UDreamLayoutContainerStackBox::StaticClass();
	if (Name == TEXT("HorizontalBox"))     return UDreamLayoutContainerHorizontalBox::StaticClass();
	if (Name == TEXT("VerticalBox"))       return UDreamLayoutContainerVerticalBox::StaticClass();
	if (Name == TEXT("WrapBox"))           return UDreamLayoutContainerWrapBox::StaticClass();
	if (Name == TEXT("GridPanel"))         return UDreamLayoutContainerGridPanel::StaticClass();
	if (Name == TEXT("UniformGridPanel"))  return UDreamLayoutContainerUniformGridPanel::StaticClass();
	if (Name == TEXT("SizeBox"))           return UDreamLayoutContainerSizeBox::StaticClass();
	if (Name == TEXT("ScaleBox"))          return UDreamLayoutContainerScaleBox::StaticClass();
	if (Name == TEXT("SafeZone"))          return UDreamLayoutContainerSafeZone::StaticClass();
	if (Name == TEXT("ScrollBox"))         return UDreamLayoutContainerScrollBox::StaticClass();
	if (Name == TEXT("WidgetSwitcher"))    return UDreamLayoutContainerWidgetSwitcher::StaticClass();
	return nullptr;
}

/** Resolve a LayoutSelf class name to its UClass. */
static UClass* ResolveLayoutSelfClass(const FString& Name)
{
	if (Name == TEXT("IgnoreLayoutContainer")) return UDreamLayoutSelf::StaticClass();
	if (Name == TEXT("AspectRatio"))           return UDreamLayoutSelfAspectRatio::StaticClass();
	return nullptr;
}

/** Handle LayoutContainer/LayoutSelf attributes. Returns true if matched (caller should continue). */
static bool TryApplyLayoutAttribute(const FString& AttrName, const FString& AttrValue, UDreamWidget* Widget,
	TArray<TPair<FString, FString>>& OutDeferredContainer, TArray<TPair<FString, FString>>& OutDeferredSelf)
{
	if (AttrName == TEXT("LayoutContainer"))
	{
		if (UClass* LayoutClass = ResolveLayoutContainerClass(AttrValue))
			Widget->CreateNewLayoutContainer(LayoutClass);
		return true;
	}
	if (AttrName.StartsWith(TEXT("LayoutContainer.")))
	{
		OutDeferredContainer.Add(TPair<FString, FString>(AttrName.Mid(16), AttrValue));
		return true;
	}
	if (AttrName == TEXT("LayoutSelf"))
	{
		if (AttrValue == TEXT("IgnoreLayoutContainer"))
		{
			Widget->SetIgnoreLayout(true);
		}
		else if (UClass* LayoutClass = ResolveLayoutSelfClass(AttrValue))
			Widget->CreateNewLayoutSelf(LayoutClass);
		return true;
	}
	if (AttrName.StartsWith(TEXT("LayoutSelf.")))
	{
		OutDeferredSelf.Add(TPair<FString, FString>(AttrName.Mid(11), AttrValue));
		return true;
	}
	return false;
}

void FDreamUIMLUtils::BindVarName(UDreamUIMLBehaviour* EventContext, const FString& VarName, UDreamWidget* Widget, UDreamVisual* Visual) const
{
	BindObjectName(EventContext, VarName, { Widget, Visual });
}

void FDreamUIMLUtils::BindObjectName(UDreamUIMLBehaviour* EventContext, const FString& VarName, const TArray<UObject*>& Candidates) const
{
	if (!EventContext || VarName.IsEmpty()) return;

	FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(FindFProperty<FProperty>(EventContext->GetClass(), *VarName));
	if (!ObjectProperty)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s].%d - VarName '%s' is not an object property on %s"),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *VarName, *EventContext->GetClass()->GetName());
		return;
	}

	for (UObject* Candidate : Candidates)
	{
		if (Candidate && Candidate->IsA(ObjectProperty->PropertyClass))
		{
			ObjectProperty->SetObjectPropertyValue_InContainer(EventContext, Candidate);
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[%s].%d - VarName '%s' expects %s, but no created object is compatible"),
		ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *VarName, *ObjectProperty->PropertyClass->GetName());
}

void FDreamUIMLUtils::ParseBindings(const FXmlNode* XmlNode, const TArray<UObject*>& TargetCandidates, UDreamUIMLBehaviour* EventContext)
{
	if (!XmlNode || !EventContext || !EventContext->GetWidget()) return;

	UDreamUIMLBindingBehaviour* BindingHost = nullptr;
	for (const FXmlAttribute& Attr : XmlNode->GetAttributes())
	{
		if (!Attr.GetTag().StartsWith(TEXT("Bind:"))) continue;

		const FString TargetPropertyName = Attr.GetTag().Mid(5);
		UObject* BindingTarget = nullptr;
		for (UObject* Candidate : TargetCandidates)
		{
			FString ResolvedName;
			if (FindBindingProperty(Candidate, TargetPropertyName, ResolvedName))
			{
				BindingTarget = Candidate;
				break;
			}
		}

		if (!BindingTarget)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - Binding target property '%s' was not found"),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *TargetPropertyName);
			continue;
		}

		if (!BindingHost)
		{
			BindingHost = EventContext->GetWidget()->GetComponent<UDreamUIMLBindingBehaviour>();
			if (!BindingHost)
			{
				BindingHost = EventContext->GetWidget()->AddComponent<UDreamUIMLBindingBehaviour>();
			}
		}
		if (!BindingHost || !BindingHost->AddBinding(EventContext, Attr.GetValue(), BindingTarget, TargetPropertyName))
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - Could not bind %s='%s' on %s"),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Attr.GetTag(), *Attr.GetValue(), *BindingTarget->GetName());
		}
	}
}

UClass* FDreamUIMLUtils::ResolveBehaviourClass(const FString& ClassName)
{
	const FString TrimmedName = ClassName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty()) return nullptr;

	if (TrimmedName == TEXT("Button")) return UUIButton::StaticClass();
	if (TrimmedName == TEXT("TextInput")) return UUITextInput::StaticClass();
	if (TrimmedName == TEXT("Toggle")) return UUIToggle::StaticClass();
	if (TrimmedName == TEXT("Slider")) return UUISlider::StaticClass();

	UClass* Result = nullptr;
	if (TrimmedName.StartsWith(TEXT("/")))
	{
		Result = UClass::TryFindTypeSlowSafe<UClass>(TrimmedName);
		if (!Result)
		{
			Result = LoadObject<UClass>(nullptr, *TrimmedName, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	}
	else
	{
		const FString ScriptPath = FString::Printf(TEXT("/Script/DreamGUI.%s"), *TrimmedName);
		Result = UClass::TryFindTypeSlowSafe<UClass>(ScriptPath);
		if (!Result)
		{
			Result = LoadObject<UClass>(nullptr, *ScriptPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	}

	if (!Result || !Result->IsChildOf(UDreamUIBehaviour::StaticClass()) || Result->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}
	return Result;
}

FDreamUIMLUtils::FDreamUIMLUtils(bool InIsSubTemplate, TFunction<void(const TArray<UDreamWidget*>&)> InAllWidgetsCreated)
{
	bIsSubTemplate = InIsSubTemplate;
	OnAllWidgetsCreated = InAllWidgetsCreated;
}

UDreamUIMLBehaviour* FDreamUIMLUtils::LoadFromFile(UWorld* InWorld, UDreamWidget* Parent, TSubclassOf<UDreamUIMLBehaviour> Class, UDreamUIMLResource* InResources, const FString& FilePath)
{
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - XML file not found: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FilePath);
		return nullptr;
	}

	FString XmlString;
	if (!FFileHelper::LoadFileToString(XmlString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Failed to read XML file: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FilePath);
		return nullptr;
	}

	return LoadFromString(InWorld, Parent, Class, InResources, XmlString);
}

UDreamUIMLBehaviour* FDreamUIMLUtils::LoadFromString(UWorld* InWorld, UDreamWidget* Parent, TSubclassOf<UDreamUIMLBehaviour> Class, UDreamUIMLResource* InResources, const FString& XmlString)
{
	TArray<FString> ValidationErrors;
	if (!ValidateString(XmlString, Class.Get(), ValidationErrors))
	{
		for (const FString& Error : ValidationErrors)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Error);
		}
		return nullptr;
	}

	FXmlFile XmlFile;
	if (!XmlFile.LoadFile(XmlString, EConstructMethod::ConstructFromBuffer))
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - XML parse error: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *XmlFile.GetLastError());
		return nullptr;
	}

	const FXmlNode* RootNode = XmlFile.GetRootNode();
	if (!RootNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - XML has no root node."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}

	this->World = InWorld;
	this->Resources = InResources;
	this->DataContainer = MakeShared<FDreamUIML_DataContainer>();

	UClass* ScriptClass = Class.Get();

	// --- Support <DreamUIML> root wrapper ---
	const FXmlNode* ContentRoot = RootNode;
	if (RootNode->GetTag() == TEXT("DreamUIML"))
	{
		ParsePropertyGroups(RootNode->GetChildrenNodes());
		// Find the first non-PropertyGroup child as the content root
		for (const FXmlNode* Child : RootNode->GetChildrenNodes())
		{
			if (Child->GetTag() != TEXT("PropertyGroup")
				&& Child->GetTag() != TEXT("Include")
				)
			{
				ContentRoot = Child;
				break;
			}
		}
		if (ContentRoot == RootNode)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - <DreamUIML> has no content root"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}
	}

	UDreamUIMLBehaviour* RootBehaviour = nullptr;
	UClass* VisualClass = nullptr;
	if (IsPrefabElement(ContentRoot->GetTag()))
	{
		if (!Resources)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - Resource is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}
		const FString PrefabName = GetElementSrc(ContentRoot);
		if (auto WidgetClass = Resources->GetWidgetClass(PrefabName))
		{
			RootBehaviour = ParsePrefabElement(ContentRoot, WidgetClass, Parent, nullptr, ScriptClass);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - Prefab '%s' not found"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *PrefabName);
			return nullptr;
		}
	}
	else if (IsTemplateElement(ContentRoot->GetTag()))
	{
		if (!Resources)
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - Resource is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return nullptr;
		}
		const FString TemplateName = GetElementSrc(ContentRoot);
		if (auto TemplateClass = Resources->GetTemplate(TemplateName))
		{
			RootBehaviour = ParseTemplateElement(ContentRoot, TemplateClass, Parent, nullptr, ScriptClass);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - Template '%s' not found"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *TemplateName);
			return nullptr;
		}
	}
	else if (IsWidgetElement(ContentRoot->GetTag(), VisualClass))
	{
		RootBehaviour = ParseWidgetElement(ContentRoot, VisualClass, Parent, nullptr, ScriptClass);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Expected root widget element, got <%s>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ContentRoot->GetTag());
	}

	if (!RootBehaviour)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Failed to parse root widget element."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}

	ResolveDeferredObjectReferences();

	for (auto& EventBinding : EventBindings)
	{
		EventBinding->DataContainer = this->DataContainer;
	}

	if (!bIsSubTemplate)
	{
		for (int i = 0; i < AllWidgets.Num(); i++)
		{
			auto& Widget = AllWidgets[i];
			if (!Widget->HasRegistered())
			{
				Widget->OnRegister();
			}
		}
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
		{
			if (DreamUIManager->HasBegunPlay())
			{
				for (int i = 0; i < AllWidgets.Num(); i++)
				{
					auto& Widget = AllWidgets[i];
					if (!Widget->HasBegunPlay())
					{
						Widget->BeginPlay();
					}
				}
			}
		}
	}
	return RootBehaviour;
}

/**
 * Bind XML event attributes (e.g. OnClick="FuncName,Param1,Param2") to the widget's components.
 * Validates function existence, parameter count, and parameter type before binding.
 */
void FDreamUIMLUtils::BindXMLEvents(UDreamWidget* Widget, const FXmlNode* XmlNode, UObject* EventContext, UDreamUIBehaviour* ComponentFilter)
{
	if (!Widget || !EventContext) return;

	const TArray<UDreamUIBehaviour*>& Components = Widget->GetAllComponents();

	for (const auto& Attr : XmlNode->GetAttributes())
	{
		const FString& AttrName = Attr.GetTag();
		if (!AttrName.StartsWith(TEXT("Event:"))) continue;

		// Strip "Event:" prefix to get event name (e.g. "Event:OnClick" → "OnClick")
		const FString EventName = AttrName.Mid(6);

		// Parse "FuncName,Param1,Param2,..."
		FString FuncName;
		FString ParamString;
		{
			const FString& Raw = Attr.GetValue();
			int32 CommaIdx;
			if (Raw.FindChar(TEXT(','), CommaIdx))
			{
				FuncName = Raw.Left(CommaIdx).TrimStartAndEnd();
				ParamString = Raw.Mid(CommaIdx + 1).TrimStartAndEnd();
			}
			else
			{
				FuncName = Raw.TrimStartAndEnd();
			}
		}

		UFunction* TargetFunc = EventContext->FindFunction(*FuncName);
		if (!TargetFunc)
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s].%d - %s='%s' but function '%s' not found on %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
				*AttrName, *FuncName, *FuncName, *EventContext->GetName());
			continue;
		}

		// Collect target function param properties
		TArray<FProperty*> TargetParams;
		for (TFieldIterator<FProperty> Pit(TargetFunc); Pit; ++Pit)
		{
			if (Pit->HasAnyPropertyFlags(CPF_Parm) && !Pit->HasAnyPropertyFlags(CPF_ReturnParm))
				TargetParams.Add(*Pit);
		}

		for (UDreamUIBehaviour* Comp : Components)
		{
			if (ComponentFilter && Comp != ComponentFilter) continue;

			for (TFieldIterator<FMulticastDelegateProperty> It(Comp->GetClass()); It; ++It)
			{
				const FString DelegateName = It->GetName();
				const bool bMatch = (DelegateName == EventName)
					|| (DelegateName.StartsWith(EventName) && DelegateName.EndsWith(TEXT("BP")));

				if (!bMatch) continue;

				// Collect delegate signature param properties
				TArray<FProperty*> DelegateParams;
				UFunction* DelegateSigFunc = It->SignatureFunction;
				if (DelegateSigFunc)
				{
					for (TFieldIterator<FProperty> Pit(DelegateSigFunc); Pit; ++Pit)
					{
						if (Pit->HasAnyPropertyFlags(CPF_Parm) && !Pit->HasAnyPropertyFlags(CPF_ReturnParm))
							DelegateParams.Add(*Pit);
					}
				}

				// Check type compatibility for directly matching param count
				bool bTypesCompatible = (TargetParams.Num() == DelegateParams.Num());
				if (bTypesCompatible && TargetParams.Num() > 0)
				{
					for (int32 i = 0; i < TargetParams.Num(); ++i)
					{
						if (!TargetParams[i]->SameType(DelegateParams[i]))
						{
							bTypesCompatible = false;
							break;
						}
					}
				}

				const bool bNeedWrapper = (!ParamString.IsEmpty())
					|| (TargetParams.Num() > DelegateParams.Num());

				if (bNeedWrapper)
				{
					auto Binding = NewObject<UDreamUIMLEventBinding>(Widget);
					Binding->Target = EventContext;
					Binding->FunctionName = *FuncName;
					Binding->ParamString = ParamString;
					EventBindings.Add(Binding);

					FScriptDelegate Delegate;
					Delegate.BindUFunction(Binding, GET_FUNCTION_NAME_CHECKED(UDreamUIMLEventBinding, Execute));
					It->AddDelegate(Delegate, Comp);

					UE_LOG(LogTemp, Log, TEXT("[%s].%d - Bound %s.%s → %s::%s (wrapper, param='%s', dlgParams=%d, tgtParams=%d)"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
						*Comp->GetName(), *DelegateName,
						*EventContext->GetName(), *FuncName,
						*ParamString, DelegateParams.Num(), TargetParams.Num());
				}
				else if (!bTypesCompatible)
				{
					UE_LOG(LogTemp, Warning, TEXT("[%s].%d - %s.%s ↔ %s::%s param types mismatch, binding skipped."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
						*Comp->GetName(), *DelegateName,
						*EventContext->GetName(), *FuncName);
				}
				else
				{
					FScriptDelegate Delegate;
					Delegate.BindUFunction(EventContext, *FuncName);
					It->AddDelegate(Delegate, Comp);

					UE_LOG(LogTemp, Log, TEXT("[%s].%d - Bound %s.%s → %s::%s (direct, params=%d)"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
						*Comp->GetName(), *DelegateName,
						*EventContext->GetName(), *FuncName,
						TargetParams.Num());
				}
				break;
			}
		}
	}
}

/** Check if AttrName is an AnchorData field and apply to the local buffer. Returns true if matched. */
static bool TryApplyAnchorDataField(const FString& AttrName, const FString& AttrValue, FDreamUIAnchorData& AnchorData, bool& bChanged, UScriptStruct* AnchorDataStruct, UObject* Owner)
{
	static const TCHAR* Fields[] = {
		TEXT("Pivot"), TEXT("AnchorMin"), TEXT("AnchorMax"),
		TEXT("AnchoredPosition"), TEXT("SizeDelta"),
	};
	for (const TCHAR* Field : Fields)
	{
		if (AttrName == Field)
		{
			FProperty* SubProp = FindFProperty<FProperty>(AnchorDataStruct, Field);
			if (SubProp)
			{
				void* SubValuePtr = SubProp->ContainerPtrToValuePtr<void>(&AnchorData);
				FDreamUIMLUtils::SetPropertyValueFromString(SubProp, SubValuePtr, AttrValue, Owner);
				bChanged = true;
			}
			return true;
		}
	}
	return false;
}

/**
 * Parse a prefab-tag node: instantiate the UDreamUIPrefab,
 * then apply attributes and children from the XML node.
 */
UDreamUIMLBehaviour* FDreamUIMLUtils::ParsePrefabElement(const FXmlNode* PrefabNode, TSubclassOf<UDreamUserWidget> WidgetClass, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass)
{
	const FString& Tag = PrefabNode->GetTag();

	// The markup's own bookkeeping wants every widget the element brought in, which the sub-prefab
	// loader used to hand over through a callback. A class hands its contents over as a tree, so they
	// are collected from it instead -- same set, gathered rather than reported.
	UDreamUserWidget* NewWidget = CreateDreamWidget(World, WidgetClass, ParentWidget);
	if (NewWidget != nullptr && NewWidget->GetWidgetTree() != nullptr)
	{
		this->AllWidgets.Add(NewWidget);
		NewWidget->GetWidgetTree()->ForEachWidget([this](UDreamWidget* Widget) { this->AllWidgets.Add(Widget); });
	}
	if (!NewWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Failed to instantiate Prefab '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Tag);
		return nullptr;
	}

	// --- Apply attributes from XML ---
	FDreamUIAnchorData AnchorData;
	UScriptStruct* AnchorDataStruct = FDreamUIAnchorData::StaticStruct();
	bool bAnchorDataChanged = false;

	TArray<TPair<FString, FString>> DeferredLayoutContainerProps;
	TArray<TPair<FString, FString>> DeferredLayoutSelfProps;

	// Build combined attribute map: Style first, node attrs override
	TMap<FString, FString> CombinedAttrs;
	ApplyStyleAttributes(PrefabNode->GetAttribute(TEXT("Style")), CombinedAttrs);
	for (const auto& Attr : PrefabNode->GetAttributes())
	{
		if (Attr.GetTag() == TEXT("Style")) continue;
		CombinedAttrs.Add(Attr.GetTag(), Attr.GetValue());
	}

	for (const auto& Pair : CombinedAttrs)
	{
		const FString& AttrName = Pair.Key;
		const FString& AttrValue = Pair.Value;

		if (AttrName == TEXT("DisplayName"))
		{
			NewWidget->SetDisplayName(AttrValue);
			continue;
		}
		if (AttrName == TEXT("VarName"))
		{
			// --- Bind VarName to script property ---
			BindVarName(EventContext, AttrValue, NewWidget, NewWidget->GetVisual());
			continue;
		}
		if (AttrName.StartsWith(TEXT("Event:")) || AttrName.StartsWith(TEXT("Bind:"))) continue;

		if (TryApplyLayoutAttribute(AttrName, AttrValue, NewWidget, DeferredLayoutContainerProps, DeferredLayoutSelfProps)) continue;

		if (TryApplyAnchorDataField(AttrName, AttrValue, AnchorData, bAnchorDataChanged, AnchorDataStruct, NewWidget)) continue;

		// Try widget property, then visual
		if (!ApplyPropertyValue(NewWidget, AttrName, AttrValue))
		{
			UDreamVisual* Vis = NewWidget->GetVisual();
			if (!Vis || !ApplyPropertyValue(Vis, AttrName, AttrValue))
			{
				UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Unknown property '%s' on Prefab <%s>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
					*AttrName, *Tag);
			}
		}
	}

	if (bAnchorDataChanged)
	{
		NewWidget->SetAnchorData(AnchorData);
	}

	ApplyDeferredLayoutProps(NewWidget, DeferredLayoutContainerProps, DeferredLayoutSelfProps);

	// --- Script behaviour (root widget only) ---
	if (ScriptClass && !EventContext)
	{
		EventContext = Cast<UDreamUIMLBehaviour>(NewWidget->AddComponent(ScriptClass));
		if (EventContext)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s].%d - Added script behaviour '%s' to root widget"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ScriptClass->GetName());
		}
	}
	ParseBindings(PrefabNode, { NewWidget, NewWidget->GetVisual() }, EventContext);

	// --- Bind XML events (OnClick="FuncName", etc.) ---
	BindXMLEvents(NewWidget, PrefabNode, EventContext);

	// Prefabs do not allow child elements
	if (PrefabNode->GetChildrenNodes().Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[%s].%d - <Prefab:XXX> does not allow child elements"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
	return EventContext;
}

UDreamUIMLBehaviour* FDreamUIMLUtils::ParseTemplateElement(const FXmlNode* TemplateNode, TSubclassOf<UDreamUIMLBehaviour> TemplateClass, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass)
{
	const FString& Tag = TemplateNode->GetTag();

	// Load the template's UIML — this instantiates the widget tree as children of ParentWidget
	UDreamUIMLBehaviour* TemplateBehaviour = UDreamUIMLBehaviour::CreateByClass(TemplateClass, World, ParentWidget, Resources, true
		, [=, this](const TArray<UDreamWidget*>& SubTemplateAllWidgets)
		{
			this->AllWidgets.Append(SubTemplateAllWidgets);
		});
	if (!TemplateBehaviour)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Failed to instantiate Template '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Tag);
		return nullptr;
	}

	UDreamWidget* RootWidget = TemplateBehaviour->GetWidget();
	if (!RootWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Template '%s' has no root widget"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Tag);
		return nullptr;
	}

	// --- Apply attributes from XML ---
	FDreamUIAnchorData AnchorData;
	UScriptStruct* AnchorDataStruct = FDreamUIAnchorData::StaticStruct();
	bool bAnchorDataChanged = false;

	TArray<TPair<FString, FString>> DeferredLayoutContainerProps;
	TArray<TPair<FString, FString>> DeferredLayoutSelfProps;

	// Build combined attribute map: Style first, node attrs override
	TMap<FString, FString> CombinedAttrs;
	ApplyStyleAttributes(TemplateNode->GetAttribute(TEXT("Style")), CombinedAttrs);
	for (const auto& Attr : TemplateNode->GetAttributes())
	{
		if (Attr.GetTag() == TEXT("Style")) continue;
		CombinedAttrs.Add(Attr.GetTag(), Attr.GetValue());
	}

	for (const auto& Pair : CombinedAttrs)
	{
		const FString& AttrName = Pair.Key;
		const FString& AttrValue = Pair.Value;

		if (AttrName == TEXT("DisplayName"))
		{
			RootWidget->SetDisplayName(AttrValue);
			continue;
		}
		if (AttrName == TEXT("VarName"))
		{
			// --- Bind VarName to script property ---
			BindVarName(EventContext, AttrValue, RootWidget, RootWidget->GetVisual());
			continue;
		}
		if (AttrName.StartsWith(TEXT("Event:")) || AttrName.StartsWith(TEXT("Bind:"))) continue;

		if (TryApplyLayoutAttribute(AttrName, AttrValue, RootWidget, DeferredLayoutContainerProps, DeferredLayoutSelfProps)) continue;

		if (TryApplyAnchorDataField(AttrName, AttrValue, AnchorData, bAnchorDataChanged, AnchorDataStruct, RootWidget)) continue;

		// Try widget property, then visual
		if (!ApplyPropertyValue(RootWidget, AttrName, AttrValue))
		{
			UDreamVisual* Vis = RootWidget->GetVisual();
			if (!Vis || !ApplyPropertyValue(Vis, AttrName, AttrValue))
			{
				UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Unknown property '%s' on Template <%s>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
					*AttrName, *Tag);
			}
		}
	}

	if (bAnchorDataChanged)
	{
		RootWidget->SetAnchorData(AnchorData);
	}

	ApplyDeferredLayoutProps(RootWidget, DeferredLayoutContainerProps, DeferredLayoutSelfProps);
	ParseBindings(TemplateNode, { RootWidget, RootWidget->GetVisual() }, EventContext);
	
	// --- Bind XML events (OnClick="FuncName", etc.) ---
	BindXMLEvents(RootWidget, TemplateNode, EventContext);

	// --- Process child elements (slot-based) ---
	if (!ProcessTemplateChildElements(TemplateNode->GetChildrenNodes(), TemplateBehaviour, EventContext, ScriptClass))
	{
		return nullptr;
	}
	return EventContext;
}

UDreamUIMLBehaviour* FDreamUIMLUtils::ParseWidgetElement(const FXmlNode* WidgetNode, UClass* VisualClass, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass)
{
	const FString& Tag = WidgetNode->GetTag();

	// Create widget
	UDreamWidget* NewWidget = NewObject<UDreamWidget>(World, UDreamWidget::StaticClass(), NAME_None, RF_Transactional);
	if (!NewWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Failed to create widget for <%s>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Tag);
		return nullptr;
	}
	AllWidgets.Add(NewWidget);
	NewWidget->SetParent(ParentWidget, false);

	// Create typed Visual if needed
	UDreamVisual* CreatedVisual = nullptr;
	if (VisualClass)
	{
		CreatedVisual = NewWidget->CreateNewVisual(VisualClass);
	}

	// --- Apply attributes ---
	// Collect AnchorData changes into a local buffer, apply once at the end.
	FDreamUIAnchorData AnchorData;
	UScriptStruct* AnchorDataStruct = FDreamUIAnchorData::StaticStruct();
	bool bAnchorDataChanged = false;

	// Deferred Layout sub-properties (may appear before LayoutContainer/LayoutSelf creation)
	TArray<TPair<FString, FString>> DeferredLayoutContainerProps;
	TArray<TPair<FString, FString>> DeferredLayoutSelfProps;

	// Build combined attribute map: Style first, node attrs override
	TMap<FString, FString> CombinedAttrs;
	ApplyStyleAttributes(WidgetNode->GetAttribute(TEXT("Style")), CombinedAttrs);
	for (const auto& Attr : WidgetNode->GetAttributes())
	{
		if (Attr.GetTag() == TEXT("Style")) continue;
		CombinedAttrs.Add(Attr.GetTag(), Attr.GetValue());
	}

	for (const auto& Pair : CombinedAttrs)
	{
		const FString& AttrName = Pair.Key;
		const FString& AttrValue = Pair.Value;

		if (AttrName == TEXT("DisplayName"))
		{
			NewWidget->SetDisplayName(AttrValue);
			continue;
		}
		if (AttrName == TEXT("IdName"))
		{
			NewWidget->SetDisplayName(AttrValue);
			DataContainer->MapIdNameToObject.Add(AttrValue, NewWidget);
			continue;
		}
		if (AttrName == TEXT("ImageBrush") || AttrName == TEXT("Font") || AttrName == TEXT("Texture") || AttrName == TEXT("Sprite") || AttrName == TEXT("VarName")
			|| AttrName.StartsWith(TEXT("Event:")) || AttrName.StartsWith(TEXT("Bind:"))) continue;

		if (TryApplyLayoutAttribute(AttrName, AttrValue, NewWidget, DeferredLayoutContainerProps, DeferredLayoutSelfProps)) continue;

		if (TryApplyAnchorDataField(AttrName, AttrValue, AnchorData, bAnchorDataChanged, AnchorDataStruct, NewWidget)) continue;

		// Try direct / dotted property (e.g. RenderOpacity, RelativeLocation.X)
		if (!ApplyPropertyValue(NewWidget, AttrName, AttrValue))
		{
			// Also try on the Visual if one was created
			if (!CreatedVisual || !ApplyPropertyValue(CreatedVisual, AttrName, AttrValue))
			{
				UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Unknown property '%s' on <%s>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
					*AttrName, *Tag);
			}
		}
	}

	// Apply accumulated AnchorData once
	if (bAnchorDataChanged)
	{
		NewWidget->SetAnchorData(AnchorData);
	}

	ApplyDeferredLayoutProps(NewWidget, DeferredLayoutContainerProps, DeferredLayoutSelfProps);

	// --- Apply resource to Visual ---
	if (CreatedVisual && Resources)
	{
		if (Tag == TEXT("Image"))
		{
			auto SrcValue = WidgetNode->GetAttribute("ImageBrush");
			UDreamImage* ImageVisual = Cast<UDreamImage>(CreatedVisual);
			if (ImageVisual && !SrcValue.IsEmpty())
			{
				FDreamUIImageBrush Brush;
				if (Resources->GetImageBrush(SrcValue, Brush))
				{
					ImageVisual->SetBrush(Brush);
				}
				else if (auto Texture = Resources->GetTexture(SrcValue))
				{
					ImageVisual->SetBrush_Texture(Texture);
				}
				else if (auto Sprite = Resources->GetSprite(SrcValue))
				{
					ImageVisual->SetBrush_DreamUISprite(Sprite);
				}
				else
				{
					ImageVisual->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultWhiteSolid());
					UE_LOG(LogTemp, Warning, TEXT("[%s].%d - ImageBrush resource '%s' not found, using default"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *SrcValue);
				}
			}
		}
		else if (Tag == TEXT("Text"))
		{
			auto FontValue = WidgetNode->GetAttribute("Font");
			UDreamText* TextVisual = Cast<UDreamText>(CreatedVisual);
			if (TextVisual && !FontValue.IsEmpty())
			{
				if (UDreamUIFontData_BaseObject* Font = Resources->GetFont(FontValue))
				{
					TextVisual->SetFont(Font);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Font resource '%s' not found, using default"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FontValue);
				}
			}
		}
		else if (Tag == TEXT("Texture"))
		{
			auto SrcValue = WidgetNode->GetAttribute("Texture");
			UDreamTexture* TextureVisual = Cast<UDreamTexture>(CreatedVisual);
			if (TextureVisual && !SrcValue.IsEmpty())
			{
				if (UTexture* Tex = Resources->GetTexture(SrcValue))
				{
					TextureVisual->SetTexture(Tex);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Texture resource '%s' not found, using default"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *SrcValue);
				}
			}
		}
		else if (Tag == TEXT("Sprite"))
		{
			auto SrcValue = WidgetNode->GetAttribute("Sprite");
			UDreamSprite* SpriteVisual = Cast<UDreamSprite>(CreatedVisual);
			if (SpriteVisual && !SrcValue.IsEmpty())
			{
				if (UDreamUISpriteData_BaseObject* Spr = Resources->GetSprite(SrcValue))
				{
					SpriteVisual->SetSprite(Spr);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Sprite resource '%s' not found, using default"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *SrcValue);
				}
			}
		}
	}

	// --- Script behaviour (root widget only) ---
	if (ScriptClass && !EventContext)
	{
		EventContext = Cast<UDreamUIMLBehaviour>(NewWidget->AddComponent(ScriptClass));
		if (EventContext)
		{
			UE_LOG(LogTemp, Log, TEXT("[%s].%d - Added script behaviour '%s' to root widget"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ScriptClass->GetName());
		}
	}

	// --- Bind VarName to script property ---
	{
		FString VarName = WidgetNode->GetAttribute(TEXT("VarName"));
		BindVarName(EventContext, VarName, NewWidget, CreatedVisual);
	}
	ParseBindings(WidgetNode, { NewWidget, CreatedVisual }, EventContext);

	// --- Bind XML events (OnClick="FuncName", etc.) ---
	BindXMLEvents(NewWidget, WidgetNode, EventContext);

	// --- Process child elements ---
	ProcessChildElements(WidgetNode->GetChildrenNodes(), NewWidget, EventContext, ScriptClass);

	return EventContext;
}

void FDreamUIMLUtils::ProcessChildElements(const TArray<FXmlNode*>& Children, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass)
{
	for (const FXmlNode* Child : Children)
	{
		const FString& ChildTag = Child->GetTag();
		UClass* VisualClass = nullptr;
		if (ChildTag == TEXT("Component"))
		{
			ParseComponentElement(Child, ParentWidget, EventContext);
		}
		else if (IsPrefabElement(ChildTag))
		{
			if (!Resources)
			{
				UE_LOG(LogTemp, Error, TEXT("[%s].%d - Resource is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
				continue;
			}
			const FString PrefabName = GetElementSrc(Child);
			if (auto ChildWidgetClass = Resources->GetWidgetClass(PrefabName))
			{
				ParsePrefabElement(Child, ChildWidgetClass, ParentWidget, EventContext, ScriptClass);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[%s].%d - Prefab '%s' not found"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *PrefabName);
				continue;
			}
		}
		else if (IsTemplateElement(ChildTag))
		{
			if (!Resources)
			{
				UE_LOG(LogTemp, Error, TEXT("[%s].%d - Resource is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
				continue;
			}
			const FString TemplateName = GetElementSrc(Child);
			if (auto ChildTemplateClass = Resources->GetTemplate(TemplateName))
			{
				ParseTemplateElement(Child, ChildTemplateClass, ParentWidget, EventContext, ScriptClass);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[%s].%d - Template '%s' not found"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *TemplateName);
				continue;
			}
		}
		else if (IsSlotElement(ChildTag))
		{
			const FString SlotName = GetSlotName(Child);
			if (bIsSubTemplate && EventContext)
			{
				ParseSlotElement(Child, SlotName, ParentWidget, EventContext);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[%s].%d - <Slot> is only valid inside a Template's own UIML"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			}
		}
		else if (IsWidgetElement(ChildTag, VisualClass))
		{
			ParseWidgetElement(Child, VisualClass, ParentWidget, EventContext, ScriptClass);
		}
		else
		{
			ParsePropertyElement(Child, ParentWidget);
		}
	}
}

void FDreamUIMLUtils::ParseComponentElement(const FXmlNode* ComponentNode, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext)
{
	if (!ComponentNode || !ParentWidget) return;

	const FString ClassName = ComponentNode->GetAttribute(TEXT("Class"));
	UClass* ComponentClass = ResolveBehaviourClass(ClassName);
	if (!ComponentClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Component class '%s' is missing, abstract, or not a DreamUI behaviour"),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ClassName);
		return;
	}

	UDreamUIBehaviour* Component = ParentWidget->AddComponent(ComponentClass);
	if (!Component)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Failed to add component '%s'"),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ComponentClass->GetName());
		return;
	}

	TMap<FString, FString> CombinedAttrs;
	ApplyStyleAttributes(ComponentNode->GetAttribute(TEXT("Style")), CombinedAttrs);
	for (const FXmlAttribute& Attr : ComponentNode->GetAttributes())
	{
		if (Attr.GetTag() != TEXT("Style"))
		{
			CombinedAttrs.Add(Attr.GetTag(), Attr.GetValue());
		}
	}

	for (const TPair<FString, FString>& Pair : CombinedAttrs)
	{
		const FString& AttrName = Pair.Key;
		if (AttrName == TEXT("Class") || AttrName == TEXT("VarName") || AttrName == TEXT("IdName")
			|| AttrName.StartsWith(TEXT("Event:")) || AttrName.StartsWith(TEXT("Bind:")))
		{
			continue;
		}
		if (!ApplyPropertyValue(Component, AttrName, Pair.Value))
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Unknown property '%s' on component %s"),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *AttrName, *ComponentClass->GetName());
		}
	}

	for (const FXmlNode* Child : ComponentNode->GetChildrenNodes())
	{
		ParsePropertyElement(Child, Component);
	}

	const FString IdName = ComponentNode->GetAttribute(TEXT("IdName")).TrimStartAndEnd();
	if (!IdName.IsEmpty())
	{
		DataContainer->MapIdNameToObject.Add(IdName, Component);
	}
	BindObjectName(EventContext, ComponentNode->GetAttribute(TEXT("VarName")), { Component });
	ParseBindings(ComponentNode, { Component }, EventContext);
	BindXMLEvents(ParentWidget, ComponentNode, EventContext, Component);
}

void FDreamUIMLUtils::ParseSlotElement(const FXmlNode* SlotNode, const FString& SlotName, UDreamWidget* ParentWidget, UDreamUIMLBehaviour* EventContext)
{
	// Create an empty placeholder widget
	UDreamWidget* Placeholder = NewObject<UDreamWidget>(World, UDreamWidget::StaticClass(), NAME_None, RF_Transactional);
	if (!Placeholder)
	{
		UE_LOG(LogTemp, Error, TEXT("[%s].%d - Failed to create placeholder for <Slot>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	Placeholder->SetParent(ParentWidget, false);

	if (SlotName.IsEmpty())
	{
		DefaultSlot = Placeholder;
		UE_LOG(LogTemp, Log, TEXT("[%s].%d - Bound default slot placeholder"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
	else
	{
		NamedSlots.Add(SlotName, Placeholder);
		UE_LOG(LogTemp, Log, TEXT("[%s].%d - Bound named slot '%s' placeholder"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *SlotName);
	}
}

bool FDreamUIMLUtils::ProcessTemplateChildElements(const TArray<FXmlNode*>& Children, UDreamUIMLBehaviour* TemplateBehaviour, UDreamUIMLBehaviour* EventContext, UClass* ScriptClass)
{
	for (const FXmlNode* Child : Children)
	{
		const FString& ChildTag = Child->GetTag();

		UClass* VisualClass = nullptr;
		if (IsSlotElement(ChildTag))
		{
			const FString SlotName = GetSlotName(Child);
			// <Slot:Name> or <Slot> — create widgets inside this slot
			const TArray<FXmlNode*>& SlotChildren = Child->GetChildrenNodes();
			for (const FXmlNode* SlotChild : SlotChildren)
			{
				const FString& SlotChildTag = SlotChild->GetTag();

				UDreamWidget* SlotParent = nullptr;
				if (SlotName.IsEmpty())
				{
					SlotParent = DefaultSlot.Get();
				}
				else
				{
					if (TWeakObjectPtr<UDreamWidget>* Found = NamedSlots.Find(SlotName))
					{
						SlotParent = Found->Get();
					}
				}

				if (!SlotParent)
				{
					UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Slot '%s' not found on template"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, SlotName.IsEmpty() ? TEXT("Default") : *SlotName);
					continue;
				}

				UClass* SlotVisualClass = nullptr;
				if (IsWidgetElement(SlotChildTag, SlotVisualClass))
				{
					ParseWidgetElement(SlotChild, SlotVisualClass, SlotParent, EventContext, ScriptClass);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Only widget elements are allowed inside <Slot>, got <%s>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *SlotChildTag);
				}
			}
		}
		else if (IsWidgetElement(ChildTag, VisualClass))
		{
			// Direct widget child → fill default slot
			UDreamWidget* DefaultSlotWidget = DefaultSlot.Get();
			if (!DefaultSlotWidget)
			{
				UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Template has no default slot, cannot place <%s>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ChildTag);
				continue;
			}
			// Parse the widget, parenting it to the slot placeholder instead of TemplateRoot
			ParseWidgetElement(Child, VisualClass, DefaultSlotWidget, EventContext, ScriptClass);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Unexpected child <%s> inside <Template:XXX>"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ChildTag);
		}
	}
	return true;
}

void FDreamUIMLUtils::ApplyDeferredLayoutProps(UDreamWidget* Widget, const TArray<TPair<FString, FString>>& DeferredContainer, const TArray<TPair<FString, FString>>& DeferredSelf)
{
	for (const auto& Pair : DeferredContainer)
	{
		if (auto* LC = Widget->GetLayoutContainer())
			ApplyPropertyValue(LC, Pair.Key, Pair.Value);
	}
	for (const auto& Pair : DeferredSelf)
	{
		if (auto* LS = Widget->GetLayoutSelf())
			ApplyPropertyValue(LS, Pair.Key, Pair.Value);
	}
}

void FDreamUIMLUtils::ParsePropertyGroups(const TArray<FXmlNode*>& Children)
{
	PropertyGroups.Empty();
	TSet<FString> VisitedIncludes;
	ParsePropertyGroups_Internal(Children, VisitedIncludes);
}

void FDreamUIMLUtils::ParsePropertyGroups_Internal(const TArray<FXmlNode*>& Children, TSet<FString>& VisitedIncludes)
{
	for (const FXmlNode* Child : Children)
	{
		const FString& Tag = Child->GetTag();
		if (Tag == TEXT("PropertyGroup"))
		{
			const FString Name = Child->GetAttribute(TEXT("Name")).TrimStartAndEnd();
			if (Name.IsEmpty()) continue;

			TArray<TPair<FString, FString>>& Group = PropertyGroups.FindOrAdd(Name);
			for (const auto& Attr : Child->GetAttributes())
			{
				const FString& AttrName = Attr.GetTag();
				if (AttrName == TEXT("Name")) continue;
				Group.Add(TPair<FString, FString>(AttrName, Attr.GetValue()));
			}
		}
		else if (Tag == TEXT("Include"))
		{
			FString Src = Child->GetAttribute(TEXT("Src")).TrimStartAndEnd();
			if (Src.IsEmpty()) continue;

			const FString FullPath = FPaths::ProjectContentDir() / Src;
			if (VisitedIncludes.Contains(FullPath)) continue;
			VisitedIncludes.Add(FullPath);

			FString XmlString;
			if (!FFileHelper::LoadFileToString(XmlString, *FullPath))
			{
				UE_LOG(LogTemp, Warning, TEXT("[%s].%d - Failed to load Include: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Src);
				continue;
			}

			FXmlFile XmlFile;
			if (!XmlFile.LoadFile(XmlString, EConstructMethod::ConstructFromBuffer)) continue;

			const FXmlNode* IncludeRoot = XmlFile.GetRootNode();
			if (!IncludeRoot || IncludeRoot->GetTag() != TEXT("DreamUIML")) continue;

			ParsePropertyGroups_Internal(IncludeRoot->GetChildrenNodes(), VisitedIncludes);
		}
	}
}

void FDreamUIMLUtils::ApplyStyleAttributes(const FString& Style, TMap<FString, FString>& OutAttrs) const
{
	TArray<FString> StyleNames;
	Style.ParseIntoArray(StyleNames, TEXT(","));
	for (const FString& StyleName : StyleNames)
	{
		const FString Trimmed = StyleName.TrimStartAndEnd();
		if (const TArray<TPair<FString, FString>>* Group = PropertyGroups.Find(Trimmed))
		{
			for (const auto& Pair : *Group)
			{
				OutAttrs.Add(Pair.Key, Pair.Value); // later styles override earlier ones
			}
		}
	}
}

void FDreamUIMLUtils::ParsePropertyElement(const FXmlNode* PropNode, UObject* TargetObject)
{
	const FString& Tag = PropNode->GetTag();

	// 1. Try text content as the property value: <RenderOpacity>0.8</RenderOpacity>
	FString ValueStr = PropNode->GetContent().TrimStartAndEnd();

	// 2. Try "Value" attribute: <RenderOpacity Value="0.8"/>
	if (ValueStr.IsEmpty())
	{
		ValueStr = PropNode->GetAttribute(TEXT("Value"));
	}

	// If we have a simple value, apply directly
	if (!ValueStr.IsEmpty())
	{
		ApplyPropertyValue(TargetObject, Tag, ValueStr);
		return;
	}

	// 3. No single value — treat attributes as struct sub-properties
	//    e.g. <RelativeLocation X="100" Y="50" Z="0"/>
	//    Each attr is applied as "Tag.AttrName" via dotted path.
	const TArray<FXmlAttribute>& Attrs = PropNode->GetAttributes();
	for (const auto& Attr : Attrs)
	{
		const FString& AttrName = Attr.GetTag();
		const FString& AttrValue = Attr.GetValue();
		if (AttrName != TEXT("Value"))
		{
			FString FullPropName = FString::Printf(TEXT("%s.%s"), *Tag, *AttrName);
			ApplyPropertyValue(TargetObject, FullPropName, AttrValue);
		}
	}
}

/** Convert comma-separated values to ImportText format for common UE structs. */
static FString ConvertStructValueForImportText(const UScriptStruct* ScriptStruct, const FString& InValue)
{
	// Already in (Key=Value,...) format? Return as-is
	if (InValue.StartsWith(TEXT("("))) return InValue;

	const FName StructName = ScriptStruct->GetFName();
	const bool bIsColorType = (StructName == NAME_Color || StructName == NAME_LinearColor);

	// Hex color: #RGB / #RGBA / #RRGGBB / #RRGGBBAA
	if (bIsColorType && InValue.StartsWith(TEXT("#")))
	{
		FString Hex = InValue.Mid(1);
		const int32 Len = Hex.Len();
		if (Len == 3 || Len == 4 || Len == 6 || Len == 8)
		{
			// Normalize to 6 or 8 hex digits
			if (Len == 3) { Hex = FString::Printf(TEXT("%c%c%c%c%c%c"), Hex[0],Hex[0],Hex[1],Hex[1],Hex[2],Hex[2]); }
			if (Len == 4) { Hex = FString::Printf(TEXT("%c%c%c%c%c%c%c%c"), Hex[0],Hex[0],Hex[1],Hex[1],Hex[2],Hex[2],Hex[3],Hex[3]); }
			if (Hex.Len() == 6) { Hex += TEXT("FF"); }
			// Now 8 hex digits: RRGGBBAA
			const uint32 R = FParse::HexDigit(Hex[0]) * 16 + FParse::HexDigit(Hex[1]);
			const uint32 G = FParse::HexDigit(Hex[2]) * 16 + FParse::HexDigit(Hex[3]);
			const uint32 B = FParse::HexDigit(Hex[4]) * 16 + FParse::HexDigit(Hex[5]);
			const uint32 A = FParse::HexDigit(Hex[6]) * 16 + FParse::HexDigit(Hex[7]);
			return FString::Printf(TEXT("(R=%u,G=%u,B=%u,A=%u)"), R, G, B, A);
		}
	}

	TArray<FString> Parts;
	InValue.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() == 0) return InValue;

	if (StructName == NAME_Vector2D && Parts.Num() >= 2)
	{
		return FString::Printf(TEXT("(X=%s,Y=%s)"), *Parts[0].TrimStartAndEnd(), *Parts[1].TrimStartAndEnd());
	}
	if (StructName == NAME_Vector && Parts.Num() >= 3)
	{
		return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"), *Parts[0].TrimStartAndEnd(), *Parts[1].TrimStartAndEnd(), *Parts[2].TrimStartAndEnd());
	}
	if (StructName == NAME_Vector4 && Parts.Num() >= 4)
	{
		return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"), *Parts[0].TrimStartAndEnd(), *Parts[1].TrimStartAndEnd(), *Parts[2].TrimStartAndEnd(), *Parts[3].TrimStartAndEnd());
	}
	if (StructName == NAME_Color && Parts.Num() >= 4)
	{
		return FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"), *Parts[0].TrimStartAndEnd(), *Parts[1].TrimStartAndEnd(), *Parts[2].TrimStartAndEnd(), *Parts[3].TrimStartAndEnd());
	}
	if (StructName == NAME_LinearColor && Parts.Num() >= 4)
	{
		return FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"), *Parts[0].TrimStartAndEnd(), *Parts[1].TrimStartAndEnd(), *Parts[2].TrimStartAndEnd(), *Parts[3].TrimStartAndEnd());
	}
	if (StructName == NAME_Rotator && Parts.Num() >= 3)
	{
		return FString::Printf(TEXT("(P=%s,Y=%s,R=%s)"), *Parts[0].TrimStartAndEnd(), *Parts[1].TrimStartAndEnd(), *Parts[2].TrimStartAndEnd());
	}
	if (StructName == NAME_Quat && Parts.Num() >= 4)
	{
		return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"), *Parts[0].TrimStartAndEnd(), *Parts[1].TrimStartAndEnd(), *Parts[2].TrimStartAndEnd(), *Parts[3].TrimStartAndEnd());
	}

	return InValue;
}

/** Set a property value from string using typed setters (avoids ImportText format issues). */
void FDreamUIMLUtils::SetPropertyValueFromString(FProperty* Property, void* ValuePtr, const FString& ValueStr, UObject* Owner)
{
	if (FNumericProperty* P = CastField<FNumericProperty>(Property)) { P->SetNumericPropertyValueFromString(ValuePtr, *ValueStr); return; }
	if (FBoolProperty*    P = CastField<FBoolProperty>(Property))    { P->SetPropertyValue(ValuePtr, ValueStr.ToBool()); return; }
	if (FStrProperty*     P = CastField<FStrProperty>(Property))     { P->SetPropertyValue(ValuePtr, ValueStr); return; }
	if (FNameProperty*    P = CastField<FNameProperty>(Property))    { P->SetPropertyValue(ValuePtr, FName(*ValueStr)); return; }
	if (FTextProperty*    P = CastField<FTextProperty>(Property))    { P->SetPropertyValue(ValuePtr, FText::FromString(ValueStr)); return; }
	if (FEnumProperty*    P = CastField<FEnumProperty>(Property))
	{
		FNumericProperty* Underlying = P->GetUnderlyingProperty();
		if (Underlying && P->GetEnum())
		{
			int64 V = P->GetEnum()->GetValueByNameString(ValueStr);
			if (V == INDEX_NONE) V = FCString::Atoi64(*ValueStr);
			Underlying->SetIntPropertyValue(ValuePtr, V);
		}
		return;
	}
	if (FStructProperty*  P = CastField<FStructProperty>(Property))
	{
		UScriptStruct* ScriptStruct = P->Struct;
		if (ScriptStruct)
		{
			const FString Formatted = ConvertStructValueForImportText(ScriptStruct, ValueStr);
			ScriptStruct->ImportText(*Formatted, ValuePtr, Owner, PPF_None, nullptr, TEXT("FDreamUIXAML"));
		}
		return;
	}

	// Last resort: ImportText
	Property->ImportText_Direct(*ValueStr, ValuePtr, Owner, PPF_None);
}

bool FDreamUIMLUtils::ApplyPropertyValue(UObject* Target, const FString& PropertyName, const FString& ValueStr)
{
	if (!Target || PropertyName.IsEmpty())
	{
		return false;
	}

	FProperty* DirectProperty = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
	if (CastField<FObjectPropertyBase>(DirectProperty)
		&& (ValueStr.StartsWith(TEXT("IdName:")) || ValueStr.StartsWith(TEXT("Widget:")) || ValueStr.StartsWith(TEXT("Visual:"))))
	{
		DeferredObjectReferences.Add({ Target, PropertyName, ValueStr });
		return true;
	}

	// Handle nested path: "AnchorData.Pivot" or "RelativeLocation.X"
	FString TopProp, SubProp;
	if (PropertyName.Split(TEXT("."), &TopProp, &SubProp))
	{
		FProperty* TopProperty = FindFProperty<FProperty>(Target->GetClass(), *TopProp);
		if (!TopProperty)
		{
			return false;
		}

		if (FStructProperty* StructProp = CastField<FStructProperty>(TopProperty))
		{
			void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Target);
			UScriptStruct* ScriptStruct = StructProp->Struct;

			// Find the sub-property within the struct
			FProperty* SubProperty = FindFProperty<FProperty>(ScriptStruct, *SubProp);
			if (!SubProperty)
			{
				return false;
			}

			// Set sub-property via typed setter
			void* SubValuePtr = SubProperty->ContainerPtrToValuePtr<void>(StructPtr);
			SetPropertyValueFromString(SubProperty, SubValuePtr, ValueStr, Target);
			return true;
		}
		return false;
	}

	// Direct property
	FProperty* Property = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
	if (!Property)
	{
		return false;
	}

	// Numeric types (float, double, int, int64, uint32, byte, etc.)
	if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		NumericProp->SetNumericPropertyValueFromString_InContainer(Target, *ValueStr);
		return true;
	}
	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		BoolProp->SetPropertyValue_InContainer(Target, ValueStr.ToBool());
		return true;
	}

	// String types
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		StrProp->SetPropertyValue_InContainer(Target, ValueStr);
		return true;
	}
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		NameProp->SetPropertyValue_InContainer(Target, FName(*ValueStr));
		return true;
	}
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		TextProp->SetPropertyValue_InContainer(Target, FText::FromString(ValueStr));
		return true;
	}

	// Struct types (FVector, FVector2D, FColor, FLinearColor, FRotator, FQuat, etc.)
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Target);
		UScriptStruct* ScriptStruct = StructProp->Struct;
		if (StructPtr && ScriptStruct)
		{
			const FString Formatted = ConvertStructValueForImportText(ScriptStruct, ValueStr);
			ScriptStruct->ImportText(*Formatted, StructPtr, Target, PPF_None, nullptr, TEXT("FDreamUIXAML"));
			return true;
		}
		return false;
	}

	// Enum types
	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum)
		{
			int64 EnumVal = ByteProp->Enum->GetValueByNameString(ValueStr);
			if (EnumVal == INDEX_NONE)
			{
				EnumVal = FCString::Atoi64(*ValueStr);
			}
			ByteProp->SetPropertyValue_InContainer(Target, static_cast<uint8>(EnumVal));
		}
		else
		{
			ByteProp->SetPropertyValue_InContainer(Target, static_cast<uint8>(FCString::Atoi(*ValueStr)));
		}
		return true;
	}
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		if (UnderlyingProp && EnumProp->GetEnum())
		{
			int64 EnumVal = EnumProp->GetEnum()->GetValueByNameString(ValueStr);
			if (EnumVal == INDEX_NONE)
			{
				EnumVal = FCString::Atoi64(*ValueStr);
			}
			void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Target);
			UnderlyingProp->SetIntPropertyValue(ValuePtr, EnumVal);
		}
		return true;
	}

	return false;
}

UObject* FDreamUIMLUtils::ResolveObjectReference(const FString& Reference, UClass* ExpectedClass) const
{
	FString Selector;
	FString IdName;
	if (!Reference.Split(TEXT(":"), &Selector, &IdName) || IdName.IsEmpty() || !ExpectedClass)
	{
		return nullptr;
	}

	const TWeakObjectPtr<UObject>* Found = DataContainer->MapIdNameToObject.Find(IdName);
	UObject* NamedObject = Found ? Found->Get() : nullptr;
	if (!NamedObject) return nullptr;

	TArray<UObject*> Candidates;
	auto AddWidgetCandidates = [&Candidates](UDreamWidget* Widget)
	{
		if (!Widget) return;
		Candidates.Add(Widget);
		Candidates.Add(Widget->GetVisual());
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			Candidates.Add(Component);
		}
	};

	UDreamWidget* HostWidget = Cast<UDreamWidget>(NamedObject);
	if (!HostWidget)
	{
		if (UDreamUIBehaviour* Behaviour = Cast<UDreamUIBehaviour>(NamedObject))
		{
			HostWidget = Behaviour->GetWidget();
		}
	}

	if (Selector == TEXT("Widget"))
	{
		Candidates.Add(HostWidget);
	}
	else if (Selector == TEXT("Visual"))
	{
		Candidates.Add(HostWidget ? HostWidget->GetVisual() : Cast<UDreamVisual>(NamedObject));
	}
	else
	{
		Candidates.Add(NamedObject);
		AddWidgetCandidates(HostWidget);
	}

	for (UObject* Candidate : Candidates)
	{
		if (Candidate && Candidate->IsA(ExpectedClass))
		{
			return Candidate;
		}
	}
	return nullptr;
}

void FDreamUIMLUtils::ResolveDeferredObjectReferences()
{
	for (const FDreamUIML_DeferredObjectReference& Pending : DeferredObjectReferences)
	{
		UObject* Target = Pending.Target.Get();
		FObjectPropertyBase* Property = Target
			? CastField<FObjectPropertyBase>(FindFProperty<FProperty>(Target->GetClass(), *Pending.PropertyName))
			: nullptr;
		UObject* Value = Property ? ResolveObjectReference(Pending.Reference, Property->PropertyClass) : nullptr;
		if (Target && Property && Value)
		{
			Property->SetObjectPropertyValue_InContainer(Target, Value);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[%s].%d - Could not resolve %s.%s='%s'"),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, Target ? *Target->GetName() : TEXT("<expired>"),
				*Pending.PropertyName, *Pending.Reference);
		}
	}
	DeferredObjectReferences.Reset();
}
