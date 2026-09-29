// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamOldAssetFixtures.h"

#include "DreamPackageLoadCheck.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "AssetToolsModule.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Controls/DreamButton.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DataFactory/DreamWidgetBlueprintFactory.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "DreamUIControlRegistry.h"
#include "DreamWidgetBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIEventTrigger.h"
#include "Interaction/UIToggle.h"
#include "Internationalization/Regex.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Serialization/BufferArchive.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace DreamOldAssetFixtures
{
	const TCHAR* const Directory = TEXT("/Game/DreamGUIFixtures");

	const TArray<FString>& PackageNames()
	{
		static const TArray<FString> Names =
		{
			FString(Directory) / TEXT("WBP_FixturePalette"),
			FString(Directory) / TEXT("WBP_FixtureBindings"),
			FString(Directory) / TEXT("WBP_FixtureAnimated"),
			FString(Directory) / TEXT("WBP_FixtureNested"),
			FString(Directory) / TEXT("L_FixtureWorld"),
		};
		return Names;
	}

	FString SnapshotFilename()
	{
		return FPaths::ProjectContentDir() / TEXT("DreamGUIFixtures/Snapshot.txt");
	}
}

namespace DreamOldAssetFixturesLocal
{
	using namespace DreamOldAssetFixtures;

	FString FilenameOf(const FString& InPackageName, bool bInIsMap)
	{
		return FPackageName::LongPackageNameToFilename(InPackageName,
			bInIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
	}

	bool SavePackageTo(UPackage* InPackage, UObject* InAsset, bool bInIsMap, TArray<FString>& OutLog)
	{
		const FString Filename = FilenameOf(InPackage->GetName(), bInIsMap);
		InAsset->SetFlags(RF_Public | RF_Standalone);
		InPackage->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_None;
		Args.Error = GWarn;
		Args.bSlowTask = false;
		const FSavePackageResultStruct Result = UPackage::Save(InPackage, InAsset, *Filename, Args);
		if (!Result.IsSuccessful())
		{
			OutLog.Add(FString::Printf(TEXT("saving %s failed (ESavePackageResult %d)"), *InPackage->GetName(), (int32)Result.Result));
			return false;
		}
		OutLog.Add(FString::Printf(TEXT("saved %s"), *Filename));
		return true;
	}

	// ---- building ------------------------------------------------------------------------------------

	/** A widget Blueprint made the way the content browser's "DreamUI Widget Blueprint" makes one: the factory, a canvas root. */
	UDreamWidgetBlueprint* NewWidgetBlueprint(const FString& InPackageName, TArray<FString>& OutLog)
	{
		UDreamWidgetBlueprintFactory* Factory = NewObject<UDreamWidgetBlueprintFactory>();
		// The root panel is the dialog's second question; ConfigureProperties is the dialog, and a
		// commandlet has no one to ask. The canvas is what a new hierarchy usually starts with.
		if (FObjectPropertyBase* RootLayout = FindFProperty<FObjectPropertyBase>(UDreamWidgetBlueprintFactory::StaticClass(), TEXT("RootLayoutClass")))
		{
			RootLayout->SetObjectPropertyValue_InContainer(Factory, UDreamLayoutContainerCanvasPanel::StaticClass());
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(AssetTools.CreateAsset(FPackageName::GetShortName(InPackageName),
			FPackageName::GetLongPackagePath(InPackageName), UDreamWidgetBlueprint::StaticClass(), Factory));
		if (Blueprint == nullptr)
		{
			OutLog.Add(FString::Printf(TEXT("the factory made no widget Blueprint at %s"), *InPackageName));
		}
		return Blueprint;
	}

	UDreamWidget* RootOf(UDreamWidgetBlueprint* InBlueprint)
	{
		return IsValid(InBlueprint) && IsValid(InBlueprint->WidgetTree) ? InBlueprint->WidgetTree->RootWidget.Get() : nullptr;
	}

	/** A plain widget under InParent, placed as the palette's "Widget" row places it. */
	UDreamWidget* AddWidget(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InParent, const TCHAR* InName, UClass* InVisualClass = nullptr)
	{
		UDreamWidget* Widget = DreamWidgetTreeEditing::CreateWidget(InBlueprint, UDreamWidget::StaticClass(), InParent, -1, InName);
		if (Widget != nullptr)
		{
			Widget->SetAnchoredPosition(FVector2D::ZeroVector);
			if (InVisualClass != nullptr)
			{
				Widget->CreateNewVisual(InVisualClass);
			}
		}
		return Widget;
	}

	/**
	 * A palette entry placed under InParent the way the palette places it -- FDreamUIEditorTools::
	 * CreateRegisteredControlAndReturn and the designer's DesignerCreateWidget, step for step, on the
	 * template tree. Null when the entry cannot be placed; OutWhy says why.
	 */
	UDreamWidget* PlaceDescriptor(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InParent, const FDreamUIControlDescriptor& InDescriptor, FString& OutWhy)
	{
		FText Invalid;
		if (!FDreamUIControlRegistry::Get().Validate(InDescriptor, Invalid))
		{
			OutWhy = Invalid.ToString();
			return nullptr;
		}
		UDreamWidget* Widget = nullptr;
		if (InDescriptor.CreationKind == EDreamUIControlCreationKind::WidgetClass)
		{
			const FString& Path = InDescriptor.WidgetClassPath;
			const UBlueprint* Control = LoadObject<UBlueprint>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
			UClass* Class = Control != nullptr ? Control->GeneratedClass.Get() : nullptr;
			if (Class == nullptr || !Class->IsChildOf(UDreamUserWidget::StaticClass()))
			{
				OutWhy = FString::Printf(TEXT("'%s' has no user widget class"), *Path);
				return nullptr;
			}
			Widget = DreamWidgetTreeEditing::CreateWidget(InBlueprint, Class, InParent, -1, Control->GetName());
			if (Widget != nullptr)
			{
				Widget->SetAnchoredPosition(FVector2D::ZeroVector);
			}
		}
		else if (InDescriptor.CreationKind == EDreamUIControlCreationKind::ControlClass)
		{
			FString Name = InDescriptor.ControlClass->GetName();
			Name.RemoveFromStart(TEXT("Dream"));
			Widget = DreamWidgetTreeEditing::CreateWidget(InBlueprint, InDescriptor.ControlClass.Get(), InParent, -1, Name);
			if (Widget != nullptr)
			{
				Widget->SetAnchoredPosition(FVector2D::ZeroVector);
				if (InDescriptor.NativeConfigure)
				{
					InDescriptor.NativeConfigure(Widget);
				}
			}
		}
		else
		{
			Widget = AddWidget(InBlueprint, InParent, *InDescriptor.Name.ToString(), InDescriptor.VisualClass.Get());
			if (Widget != nullptr)
			{
				if (InDescriptor.LayoutContainerClass.IsValid())
				{
					Widget->CreateNewLayoutContainer(InDescriptor.LayoutContainerClass.Get());
				}
				if (InDescriptor.LayoutSelfClass.IsValid())
				{
					Widget->CreateNewLayoutSelf(InDescriptor.LayoutSelfClass.Get());
				}
				if (InDescriptor.BehaviourClass.IsValid())
				{
					Widget->AddComponent(InDescriptor.BehaviourClass.Get());
				}
				if (InDescriptor.MeshModifierClass.IsValid())
				{
					if (UDreamVisualBatchMesh* Visual = Cast<UDreamVisualBatchMesh>(Widget->GetVisual()))
					{
						Visual->AddMeshModifier(InDescriptor.MeshModifierClass.Get());
					}
				}
				if (InDescriptor.NativeConfigure)
				{
					InDescriptor.NativeConfigure(Widget);
				}
			}
		}
		if (Widget == nullptr && OutWhy.IsEmpty())
		{
			OutWhy = TEXT("the tree refused it; the log says why");
		}
		return Widget;
	}

	/** What one row of an event's binding list records, as the details panel records it. */
	struct FEventBinding
	{
		UDreamWidget* HelperWidget = nullptr;
		UClass* HelperClass = nullptr;
		int32 HelperComponentIndex = INDEX_NONE;
		FName FunctionName;
		/** The stored argument, written the way the panel writes it: FBufferArchive << value. */
		TArray<uint8> ParamBuffer;
	};

	template<typename ValueType>
	TArray<uint8> BufferOf(ValueType InValue)
	{
		FBufferArchive ToBinary;
		ToBinary << InValue;
		return TArray<uint8>(ToBinary);
	}

	/**
	 * Append a row to InOwner's event InEventName. The rows are private to the event's struct, as they are
	 * to everything but the details panel, so they are written through reflection.
	 */
	bool AddEventBinding(UObject* InOwner, FName InEventName, const FEventBinding& InBinding, FString& OutWhy)
	{
		const FStructProperty* EventProperty = CastField<FStructProperty>(InOwner->GetClass()->FindPropertyByName(InEventName));
		if (EventProperty == nullptr || EventProperty->Struct != FDreamUIEventDelegate::StaticStruct())
		{
			OutWhy = FString::Printf(TEXT("%s has no event %s"), *InOwner->GetClass()->GetName(), *InEventName.ToString());
			return false;
		}
		// The parameter type the details panel lists the function under, asked the way the panel asks it.
		UFunction* Function = InBinding.HelperClass != nullptr ? InBinding.HelperClass->FindFunctionByName(InBinding.FunctionName) : nullptr;
		EDreamUIEventDelegateParameterType ParamType = EDreamUIEventDelegateParameterType::None;
		if (Function == nullptr || !UDreamUIEventDelegateParameterHelper::IsSupportedFunction(Function, ParamType))
		{
			OutWhy = FString::Printf(TEXT("%s.%s cannot be bound to an event"), *GetNameSafe(InBinding.HelperClass), *InBinding.FunctionName.ToString());
			return false;
		}

		UScriptStruct* RowStruct = FDreamUIEventDelegateData::StaticStruct();
		const FArrayProperty* ListProperty = CastField<FArrayProperty>(FDreamUIEventDelegate::StaticStruct()->FindPropertyByName(TEXT("EventList")));
		const FObjectPropertyBase* WidgetProperty = CastField<FObjectPropertyBase>(RowStruct->FindPropertyByName(TEXT("HelperWidget")));
		const FObjectPropertyBase* ClassProperty = CastField<FObjectPropertyBase>(RowStruct->FindPropertyByName(TEXT("HelperClass")));
		const FIntProperty* IndexProperty = CastField<FIntProperty>(RowStruct->FindPropertyByName(TEXT("HelperComponentIndex")));
		const FNameProperty* FunctionProperty = CastField<FNameProperty>(RowStruct->FindPropertyByName(TEXT("FunctionName")));
		const FProperty* TypeProperty = RowStruct->FindPropertyByName(TEXT("ParamType"));
		const FArrayProperty* BufferProperty = CastField<FArrayProperty>(RowStruct->FindPropertyByName(TEXT("ParamBuffer")));
		if (ListProperty == nullptr || WidgetProperty == nullptr || ClassProperty == nullptr || IndexProperty == nullptr
			|| FunctionProperty == nullptr || TypeProperty == nullptr || BufferProperty == nullptr)
		{
			OutWhy = TEXT("FDreamUIEventDelegateData is not shaped the way this writer expects");
			return false;
		}

		void* EventPtr = EventProperty->ContainerPtrToValuePtr<void>(InOwner);
		FScriptArrayHelper List(ListProperty, ListProperty->ContainerPtrToValuePtr<void>(EventPtr));
		uint8* Row = List.GetRawPtr(List.AddValue());
		WidgetProperty->SetObjectPropertyValue_InContainer(Row, InBinding.HelperWidget);
		ClassProperty->SetObjectPropertyValue_InContainer(Row, InBinding.HelperClass);
		IndexProperty->SetPropertyValue_InContainer(Row, InBinding.HelperComponentIndex);
		FunctionProperty->SetPropertyValue_InContainer(Row, InBinding.FunctionName);
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(TypeProperty))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Row), (int64)ParamType);
		}
		else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(TypeProperty))
		{
			NumericProperty->SetIntPropertyValue(NumericProperty->ContainerPtrToValuePtr<void>(Row), (int64)ParamType);
		}
		*BufferProperty->ContainerPtrToValuePtr<TArray<uint8>>(Row) = InBinding.ParamBuffer;
		return true;
	}

	/**
	 * A pure function returning the type of InPropertyName on InWidget's visual, bound to it -- what the
	 * details panel's "Create Binding" makes (DreamWidgetPropertyBindingExtension.cpp), with its result
	 * connected, so the Blueprint compiles without a warning.
	 */
	bool AddPropertyBinding(UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InWidget, FName InPropertyName, FString& OutWhy)
	{
		const UDreamVisual* Visual = InWidget != nullptr ? InWidget->GetVisual() : nullptr;
		const FProperty* Property = Visual != nullptr ? Visual->GetClass()->FindPropertyByName(InPropertyName) : nullptr;
		FEdGraphPinType PinType;
		if (Property == nullptr || !GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
		{
			OutWhy = FString::Printf(TEXT("the visual of %s has no bindable %s"), *GetNameSafe(InWidget), *InPropertyName.ToString());
			return false;
		}
		const FName WidgetName = UDreamWidgetTree::MakeWidgetVariableName(InWidget);
		const FName GraphName = FBlueprintEditorUtils::FindUniqueKismetName(InBlueprint,
			FString::Printf(TEXT("Get%s_%s"), *WidgetName.ToString(), *InPropertyName.ToString()));
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(InBlueprint, GraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(InBlueprint, Graph, /*bIsUserCreated*/true, nullptr);
		UK2Node_FunctionEntry* Entry = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			Entry = Entry != nullptr ? Entry : Cast<UK2Node_FunctionEntry>(Node);
		}
		if (Entry == nullptr)
		{
			OutWhy = TEXT("the new function graph has no entry node");
			return false;
		}
		Entry->AddExtraFlags(FUNC_BlueprintPure);
		FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*Graph);
		UK2Node_FunctionResult* Result = ResultCreator.CreateNode();
		Result->FunctionReference.SetSelfMember(GraphName);
		Result->NodePosX = Entry->NodePosX + 400;
		Result->NodePosY = Entry->NodePosY;
		ResultCreator.Finalize();
		UEdGraphPin* ReturnPin = Result->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, PinType, EGPD_Input);
		if (ReturnPin != nullptr && PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			ReturnPin->DefaultTextValue = FText::FromString(TEXT("Bound before the split"));
		}
		UEdGraphPin* Then = Entry->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* Execute = Result->FindPin(UEdGraphSchema_K2::PN_Execute);
		if (Then != nullptr && Execute != nullptr)
		{
			GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Then, Execute);
		}

		FDreamWidgetPropertyBinding& Binding = InBlueprint->PropertyBindings.AddDefaulted_GetRef();
		Binding.WidgetName = WidgetName;
		Binding.Target = EDreamWidgetBindingTarget::Visual;
		Binding.PropertyName = InPropertyName;
		Binding.FunctionName = GraphName;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
		return true;
	}

	bool CompileClean(UDreamWidgetBlueprint* InBlueprint, TArray<FString>& OutLog)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Error || Message->GetSeverity() == EMessageSeverity::Warning)
			{
				OutLog.Add(FString::Printf(TEXT("%s: %s"), *InBlueprint->GetName(), *Message->ToText().ToString()));
			}
		}
		return Results.NumErrors == 0 && Results.NumWarnings == 0;
	}

	/**
	 * Give every widget a display name no other widget in the tree has, the way the compiler's warning asks an
	 * author to: a control whose recipe builds its children under fixed names ("Content", "Viewport") leaves
	 * two widgets of that name once it is placed twice in one Blueprint. The later one takes its parent's
	 * name in front, which is how an author tells the two apart.
	 */
	int32 RenameDuplicates(UDreamWidgetBlueprint* InBlueprint)
	{
		TArray<UDreamWidget*> Widgets;
		InBlueprint->WidgetTree->ForEachWidget([&Widgets](UDreamWidget* InWidget) { Widgets.Add(InWidget); });
		TSet<FString> Seen;
		int32 Renamed = 0;
		for (UDreamWidget* Widget : Widgets)
		{
			if (!IsValid(Widget))
			{
				continue;
			}
			if (Seen.Contains(Widget->GetDisplayName()))
			{
				const UDreamWidget* Parent = Widget->GetParent();
				DreamWidgetTreeEditing::RenameWidget(InBlueprint, Widget,
					Parent != nullptr ? Parent->GetDisplayName() + TEXT("_") + Widget->GetDisplayName() : Widget->GetDisplayName());
				++Renamed;
			}
			Seen.Add(Widget->GetDisplayName());
		}
		return Renamed;
	}

	/** Every palette entry, each under the root. */
	bool FillPalette(UDreamWidgetBlueprint* InBlueprint, TArray<FString>& OutLog)
	{
		UDreamWidget* Root = RootOf(InBlueprint);
		int32 Placed = 0;
		for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
		{
			FString Why;
			if (PlaceDescriptor(InBlueprint, Root, Descriptor, Why) != nullptr)
			{
				++Placed;
			}
			else
			{
				OutLog.Add(FString::Printf(TEXT("palette entry '%s' not placed: %s"), *Descriptor.Name.ToString(), *Why));
			}
		}
		OutLog.Add(FString::Printf(TEXT("placed %d palette entries, and renamed %d widgets whose names repeated"),
			Placed, RenameDuplicates(InBlueprint)));
		return Placed > 0;
	}

	/** Event bindings to a widget, a visual and a behaviour, and a property binding. */
	bool FillBindings(UDreamWidgetBlueprint* InBlueprint, TArray<FString>& OutLog)
	{
		UDreamWidget* Root = RootOf(InBlueprint);
		UDreamWidget* Title = AddWidget(InBlueprint, Root, TEXT("Title"), UDreamText::StaticClass());
		UDreamWidget* Target = AddWidget(InBlueprint, Root, TEXT("Target"), UDreamImage::StaticClass());
		UDreamWidget* Clicker = AddWidget(InBlueprint, Root, TEXT("Clicker"), UDreamImage::StaticClass());
		UDreamWidget* Switch = AddWidget(InBlueprint, Root, TEXT("Switch"), UDreamImage::StaticClass());
		UDreamWidget* Trigger = AddWidget(InBlueprint, Root, TEXT("Trigger"), UDreamImage::StaticClass());
		if (Title == nullptr || Target == nullptr || Clicker == nullptr || Switch == nullptr || Trigger == nullptr)
		{
			OutLog.Add(TEXT("the bindings fixture's widgets were not all made"));
			return false;
		}
		if (UDreamText* Text = Cast<UDreamText>(Title->GetVisual()))
		{
			Text->SetText(FText::FromString(TEXT("Saved before the split")));
		}
		UUIButton* Button = Clicker->AddComponent<UUIButton>();
		UUIToggle* Toggle = Switch->AddComponent<UUIToggle>();
		UUIEventTrigger* Events = Trigger->AddComponent<UUIEventTrigger>();
		if (Button == nullptr || Toggle == nullptr || Events == nullptr)
		{
			OutLog.Add(TEXT("the bindings fixture's behaviours were not all added"));
			return false;
		}

		auto Bind = [&OutLog](UObject* InOwner, FName InEvent, const FEventBinding& InBinding)
		{
			FString Why;
			if (AddEventBinding(InOwner, InEvent, InBinding, Why))
			{
				return true;
			}
			OutLog.Add(Why);
			return false;
		};
		// To a widget: the click hides Target.
		FEventBinding ToWidget;
		ToWidget.HelperWidget = Target;
		ToWidget.HelperClass = UDreamWidget::StaticClass();
		ToWidget.FunctionName = TEXT("SetWidgetActive");
		ToWidget.ParamBuffer = { 0 };
		bool bOk = Bind(Button, TEXT("OnClick"), ToWidget);
		// To a visual: the toggle tints Title.
		FEventBinding ToVisual;
		ToVisual.HelperWidget = Title;
		ToVisual.HelperClass = UDreamText::StaticClass();
		ToVisual.FunctionName = TEXT("SetColor");
		ToVisual.ParamBuffer = BufferOf(FColor(255, 128, 0, 255));
		bOk &= Bind(Toggle, TEXT("OnValueChanged"), ToVisual);
		// To a behaviour, by its place among Switch's components: a click on Trigger recolours the toggle.
		FEventBinding ToBehaviour;
		ToBehaviour.HelperWidget = Switch;
		ToBehaviour.HelperClass = UUIToggle::StaticClass();
		ToBehaviour.HelperComponentIndex = Switch->GetAllComponents().Find(Toggle);
		ToBehaviour.FunctionName = TEXT("SetOnColor");
		ToBehaviour.ParamBuffer = BufferOf(FColor(0, 200, 100, 255));
		bOk &= Bind(Events, TEXT("OnPointerClick"), ToBehaviour);
		// And a second row on the same event, with no argument at all.
		FEventBinding NoArgument;
		NoArgument.HelperWidget = Title;
		NoArgument.HelperClass = UDreamWidget::StaticClass();
		NoArgument.FunctionName = TEXT("SetAsLastSibling");
		bOk &= Bind(Events, TEXT("OnPointerClick"), NoArgument);

		FString Why;
		if (!AddPropertyBinding(InBlueprint, Title, TEXT("Text"), Why))
		{
			OutLog.Add(Why);
			bOk = false;
		}
		return bOk;
	}

	/** One animation on the root driving a child's opacity and translation. */
	bool FillAnimated(UDreamWidgetBlueprint* InBlueprint, TArray<FString>& OutLog)
	{
		UDreamWidget* Root = RootOf(InBlueprint);
		UDreamWidget* Mover = AddWidget(InBlueprint, Root, TEXT("Mover"), UDreamImage::StaticClass());
		UDreamWidgetAnimationComponent* Animator = Root != nullptr ? Root->AddComponent<UDreamWidgetAnimationComponent>() : nullptr;
		UDreamWidgetAnimation* Animation = Animator != nullptr ? Animator->AddNewAnimation() : nullptr;
		UMovieScene* MovieScene = Animation != nullptr ? Animation->GetMovieScene() : nullptr;
		if (Mover == nullptr || MovieScene == nullptr)
		{
			OutLog.Add(TEXT("the animated fixture's widget or animation was not made"));
			return false;
		}
		Animation->SetDisplayNameString(TEXT("Slide"));
		const int32 Frames = 30;
		const int32 TicksPerFrame = 24000 / 30;
		MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
		MovieScene->SetDisplayRate(FFrameRate(30, 1));
		MovieScene->SetPlaybackRange(FFrameNumber(0), Frames * TicksPerFrame);
		// Possessed by display name and bound against the root, as the animation editor binds.
		const FGuid MoverGuid = MovieScene->AddPossessable(Mover->GetDisplayName(), Mover->GetClass());
		Animation->BindPossessableObject(MoverGuid, *Mover, Root);

		UMovieSceneFloatTrack* Opacity = MovieScene->AddTrack<UMovieSceneFloatTrack>(MoverGuid);
		Opacity->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
		UMovieSceneFloatSection* OpacitySection = CastChecked<UMovieSceneFloatSection>(Opacity->CreateNewSection());
		OpacitySection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(Frames * TicksPerFrame)));
		TArrayView<FMovieSceneFloatChannel*> OpacityChannels = OpacitySection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		OpacityChannels[0]->AddLinearKey(FFrameNumber(0), 0.0f);
		OpacityChannels[0]->AddLinearKey(FFrameNumber(Frames * TicksPerFrame), 1.0f);
		Opacity->AddSection(*OpacitySection);

		UMovieSceneDoubleVectorTrack* Translation = MovieScene->AddTrack<UMovieSceneDoubleVectorTrack>(MoverGuid);
		Translation->SetPropertyNameAndPath(TEXT("RenderTranslation"), TEXT("RenderTranslation"));
		Translation->SetNumChannelsUsed(3);
		UMovieSceneDoubleVectorSection* TranslationSection = CastChecked<UMovieSceneDoubleVectorSection>(Translation->CreateNewSection());
		TranslationSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(Frames * TicksPerFrame)));
		TArrayView<FMovieSceneDoubleChannel*> TranslationChannels = TranslationSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		TranslationChannels[0]->AddCubicKey(FFrameNumber(0), -120.0);
		TranslationChannels[0]->AddCubicKey(FFrameNumber(Frames * TicksPerFrame), 0.0);
		TranslationChannels[1]->AddCubicKey(FFrameNumber(0), 0.0);
		TranslationChannels[2]->AddCubicKey(FFrameNumber(0), 0.0);
		Translation->AddSection(*TranslationSection);
		return true;
	}

	/** The bindings and animated fixtures as nested user widgets, and a native control with its slot filled. */
	bool FillNested(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetBlueprint* InBindings, UDreamWidgetBlueprint* InAnimated, TArray<FString>& OutLog)
	{
		UDreamWidget* Root = RootOf(InBlueprint);
		UClass* BindingsClass = InBindings != nullptr ? InBindings->GeneratedClass.Get() : nullptr;
		UClass* AnimatedClass = InAnimated != nullptr ? InAnimated->GeneratedClass.Get() : nullptr;
		UDreamWidget* Bindings = BindingsClass != nullptr ? DreamWidgetTreeEditing::CreateWidget(InBlueprint, BindingsClass, Root, -1, TEXT("Bindings")) : nullptr;
		UDreamWidget* Animated = AnimatedClass != nullptr ? DreamWidgetTreeEditing::CreateWidget(InBlueprint, AnimatedClass, Root, -1, TEXT("Animated")) : nullptr;
		UDreamWidget* Confirm = DreamWidgetTreeEditing::CreateWidget(InBlueprint, UDreamButton::StaticClass(), Root, -1, TEXT("Confirm"));
		if (Bindings == nullptr || Animated == nullptr || Confirm == nullptr)
		{
			OutLog.Add(TEXT("the nested fixture's user widgets were not all placed"));
			return false;
		}
		TArray<FName> Slots;
		UDreamUserWidget::CollectDeclaredSlotNames(UDreamButton::StaticClass(), Slots);
		if (Slots.Num() == 0)
		{
			OutLog.Add(TEXT("UDreamButton declares no slot to fill"));
			return false;
		}
		UDreamWidget* Caption = DreamWidgetTreeEditing::CreateWidget(InBlueprint, UDreamWidget::StaticClass(), Confirm, -1, TEXT("Caption"), Slots[0]);
		if (Caption == nullptr)
		{
			OutLog.Add(FString::Printf(TEXT("nothing could be put in the button's slot '%s'"), *Slots[0].ToString()));
			return false;
		}
		if (UDreamText* Text = Cast<UDreamText>(Caption->CreateNewVisual(UDreamText::StaticClass())))
		{
			Text->SetText(FText::FromString(TEXT("Confirm")));
		}
		return true;
	}

	/** A level: the nested fixture on a world widget, the plugin's event system Blueprint, a raycaster of the level's own. */
	UWorld* MakeLevel(UPackage* InPackage, UClass* InWidgetClass, TArray<FString>& OutLog)
	{
		UWorld::InitializationValues Values;
		Values.ShouldSimulatePhysics(false).EnableTraceCollision(false).CreateNavigation(false).CreateAISystem(false).AllowAudioPlayback(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, /*bInformEngineOfWorld*/false, FName(FPackageName::GetShortName(InPackage->GetName())),
			InPackage, /*bAddToRoot*/false, ERHIFeatureLevel::Num, &Values);
		if (World == nullptr)
		{
			OutLog.Add(TEXT("no world was made for the level"));
			return nullptr;
		}

		FActorSpawnParameters Params;
		Params.Name = TEXT("FixtureWorldWidget");
		ADreamWorldWidgetActor* WorldWidget = World->SpawnActor<ADreamWorldWidgetActor>(FVector(300.0, 0.0, 150.0), FRotator(0.0, 180.0, 0.0), Params);
		if (WorldWidget == nullptr || WorldWidget->GetWidgetComponent() == nullptr)
		{
			OutLog.Add(TEXT("the world widget actor was not spawned"));
			return World;
		}
		WorldWidget->SetActorLabel(TEXT("FixtureWorldWidget"));
		WorldWidget->GetWidgetComponent()->SetWidgetClass(InWidgetClass);
		WorldWidget->GetWidgetComponent()->SetSortOrder(3);

		const TCHAR* EventSystemPath = TEXT("/DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput.DreamEventSystemActor_EnhancedInput_C");
		if (UClass* EventSystemClass = LoadObject<UClass>(nullptr, EventSystemPath))
		{
			FActorSpawnParameters EventParams;
			EventParams.Name = TEXT("FixtureEventSystem");
			if (AActor* EventSystem = World->SpawnActor<AActor>(EventSystemClass, FTransform::Identity, EventParams))
			{
				EventSystem->SetActorLabel(TEXT("FixtureEventSystem"));
			}
		}
		else
		{
			OutLog.Add(FString::Printf(TEXT("the plugin's event system Blueprint %s did not load"), EventSystemPath));
		}

		FActorSpawnParameters PointerParams;
		PointerParams.Name = TEXT("FixturePointer");
		if (AActor* Pointer = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, PointerParams))
		{
			Pointer->SetActorLabel(TEXT("FixturePointer"));
			UDreamWorldSpaceRaycaster* Raycaster = NewObject<UDreamWorldSpaceRaycaster>(Pointer, TEXT("FixtureRaycaster"), RF_Transactional);
			Raycaster->SetPointerSource(EDreamWorldPointerSource::ScreenCenter);
			Pointer->AddInstanceComponent(Raycaster);
			Raycaster->RegisterComponent();
		}
		return World;
	}

	// ---- describing ----------------------------------------------------------------------------------

	/**
	 * Properties whose loaded value is not a property of the file, and so cannot be compared with a snapshot.
	 *
	 * WidgetGuid: when the fixtures were saved, a widget a palette recipe builds (DreamUIControlRegistry's
	 * CreateChild) was created without an id and saved without one, and UDreamWidget::PostLoad backfilled a
	 * new random one on every load, so two loads of the same file disagreed about it and the snapshot left it
	 * out. Recipe parts are born with an id now, and the backfill is derived from the widget's path, but the
	 * snapshot stays as it was taken when the fixtures were saved.
	 */
	bool IsNotAPropertyOfTheFile(const FProperty* InProperty)
	{
		static const FName WidgetGuid(TEXT("WidgetGuid"));
		return InProperty->GetFName() == WidgetGuid && InProperty->GetOwnerClass() == UDreamWidget::StaticClass();
	}

	/** The properties InObject holds at a value its archetype would not have given it: "  Name=Value", in declaration order. */
	void DescribeProperties(const UObject* InObject, TArray<FString>& OutLines)
	{
		const UObject* Archetype = InObject->GetArchetype();
		for (TFieldIterator<FProperty> It(InObject->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_SkipSerialization) || IsNotAPropertyOfTheFile(Property))
			{
				continue;
			}
			const bool bComparable = Archetype != nullptr && Archetype->IsA(Property->GetOwnerClass());
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
			{
				if (bComparable && Property->Identical_InContainer(InObject, Archetype, Index, PPF_None))
				{
					continue;
				}
				FString Value;
				Property->ExportText_InContainer(Index, Value, InObject, nullptr, const_cast<UObject*>(InObject), PPF_None);
				OutLines.Add(FString::Printf(TEXT("  %s%s=%s"), *Property->GetName(),
					Property->ArrayDim > 1 ? *FString::Printf(TEXT("[%d]"), Index) : TEXT(""), *Value));
			}
		}
	}

	bool IsTransientSomewhere(const UObject* InObject)
	{
		for (const UObject* Outer = InObject; Outer != nullptr; Outer = Outer->GetOuter())
		{
			if (Outer->HasAnyFlags(RF_Transient))
			{
				return true;
			}
		}
		return false;
	}

	/** InRoot and every saved object inside it, each as "object <path> <class>" and its properties, sorted by path. */
	void DescribeObjectAndInner(const UObject* InRoot, const UPackage* InPackage, TArray<FString>& OutLines)
	{
		TArray<UObject*> Objects;
		GetObjectsWithOuter(InRoot, Objects, EGetObjectsFlags::IncludeNestedObjects);
		Objects.Add(const_cast<UObject*>(InRoot));
		Objects.RemoveAll([](const UObject* Object) { return Object == nullptr || IsTransientSomewhere(Object); });
		Objects.Sort([InPackage](const UObject& A, const UObject& B) { return A.GetPathName(InPackage) < B.GetPathName(InPackage); });
		for (const UObject* Object : Objects)
		{
			OutLines.Add(FString::Printf(TEXT("object %s %s"), *Object->GetPathName(InPackage), *Object->GetClass()->GetPathName()));
			DescribeProperties(Object, OutLines);
		}
	}

	/** Whether InClass is one of the plugin's, or content's: what a level's own furniture is not. */
	bool IsPluginOrContentClass(const UClass* InClass)
	{
		const FString Package = InClass != nullptr ? InClass->GetOutermost()->GetName() : FString();
		return DreamUI::IsRuntimeScriptPackage(FName(*Package)) || Package.StartsWith(TEXT("/DreamGUI/")) || Package.StartsWith(TEXT("/Game/"));
	}

	bool IsFixtureActor(const AActor* InActor)
	{
		if (IsPluginOrContentClass(InActor->GetClass()))
		{
			return true;
		}
		for (const UActorComponent* Component : InActor->GetComponents())
		{
			if (Component != nullptr && IsPluginOrContentClass(Component->GetClass()))
			{
				return true;
			}
		}
		return false;
	}
}

bool DreamOldAssetFixtures::WriteAll(TArray<FString>& OutLog)
{
	using namespace DreamOldAssetFixturesLocal;

	for (const FString& PackageName : PackageNames())
	{
		if (FPackageName::DoesPackageExist(PackageName))
		{
			OutLog.Add(FString::Printf(TEXT("%s exists already, and fixtures are never replaced: they are worth something only because of when they were saved. Delete the files by hand to make new ones."), *PackageName));
			return false;
		}
	}
	const TArray<FString>& Names = PackageNames();

	UDreamWidgetBlueprint* Palette = NewWidgetBlueprint(Names[0], OutLog);
	UDreamWidgetBlueprint* Bindings = NewWidgetBlueprint(Names[1], OutLog);
	UDreamWidgetBlueprint* Animated = NewWidgetBlueprint(Names[2], OutLog);
	UDreamWidgetBlueprint* Nested = NewWidgetBlueprint(Names[3], OutLog);
	if (Palette == nullptr || Bindings == nullptr || Animated == nullptr || Nested == nullptr)
	{
		return false;
	}
	bool bOk = FillPalette(Palette, OutLog) && FillBindings(Bindings, OutLog) && FillAnimated(Animated, OutLog);
	for (UDreamWidgetBlueprint* Blueprint : { Palette, Bindings, Animated })
	{
		bOk &= CompileClean(Blueprint, OutLog);
	}
	bOk = bOk && FillNested(Nested, Bindings, Animated, OutLog) && CompileClean(Nested, OutLog);
	if (!bOk)
	{
		return false;
	}
	for (UDreamWidgetBlueprint* Blueprint : { Palette, Bindings, Animated, Nested })
	{
		bOk &= SavePackageTo(Blueprint->GetOutermost(), Blueprint, /*bInIsMap*/false, OutLog);
	}

	UPackage* LevelPackage = CreatePackage(*Names[4]);
	LevelPackage->MarkAsFullyLoaded();
	UWorld* World = MakeLevel(LevelPackage, Nested->GeneratedClass.Get(), OutLog);
	if (World == nullptr)
	{
		return false;
	}
	bOk &= SavePackageTo(LevelPackage, World, /*bInIsMap*/true, OutLog);
	World->DestroyWorld(/*bInformEngineOfWorld*/false);
	return bOk;
}

bool DreamOldAssetFixtures::WriteSnapshot(TArray<FString>& OutLog)
{
	TArray<FString> Lines;
	Lines.Add(TEXT("# What the old-asset fixtures in this folder hold, as the code that saved them read them back:"));
	Lines.Add(TEXT("# every saved object, and every property it holds at a value its archetype would not give it."));
	Lines.Add(TEXT("# The DreamGUI.Compatibility tests compare the fixtures with this, its script paths carried through"));
	Lines.Add(TEXT("# the CoreRedirects. Written by the console command DreamGUI.OldAssetFixtures.Snapshot; never edited by hand."));
	Lines.Add(FString::Printf(TEXT("# %s, engine %s"), *FDateTime::UtcNow().ToIso8601(), *FEngineVersion::Current().ToString()));
	bool bClean = true;
	for (const FString& PackageName : PackageNames())
	{
		// A fixture's own Blueprints were saved compiling without a warning, and have to read back that way.
		const DreamPackageLoadCheck::FResult Loaded = DreamPackageLoadCheck::LoadAndCompile(PackageName);
		for (const FString& Problem : Loaded.Problems)
		{
			OutLog.Add(FString::Printf(TEXT("%s: %s"), *PackageName, *Problem));
		}
		for (const FString& Warning : Loaded.CompilerWarnings)
		{
			OutLog.Add(FString::Printf(TEXT("%s: %s"), *PackageName, *Warning));
		}
		for (const FString& Note : Loaded.OtherNotes)
		{
			OutLog.Add(FString::Printf(TEXT("%s, not counted: %s"), *PackageName, *Note));
		}
		bClean &= Loaded.Package != nullptr && Loaded.Problems.Num() == 0 && Loaded.CompilerWarnings.Num() == 0;
		Lines.Add(TEXT(""));
		Lines.Add(FString::Printf(TEXT("package %s"), *PackageName));
		Lines.Add(Describe(Loaded.Package));
	}
	if (!bClean)
	{
		OutLog.Add(TEXT("a fixture did not come back clean, so no snapshot was written"));
		return false;
	}
	const FString Filename = SnapshotFilename();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	if (!FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutLog.Add(FString::Printf(TEXT("could not write %s"), *Filename));
		return false;
	}
	OutLog.Add(FString::Printf(TEXT("wrote %s"), *Filename));
	return true;
}

FString DreamOldAssetFixtures::Describe(const UPackage* InPackage)
{
	using namespace DreamOldAssetFixturesLocal;

	TArray<FString> Lines;
	if (InPackage == nullptr)
	{
		return FString();
	}
	const FString AssetName = FPackageName::GetShortName(InPackage);
	UPackage* Package = const_cast<UPackage*>(InPackage);
	if (const UDreamWidgetBlueprint* Blueprint = FindObject<UDreamWidgetBlueprint>(Package, *AssetName))
	{
		Lines.Add(FString::Printf(TEXT("blueprint %s %s"), *Blueprint->GetName(), *Blueprint->GetClass()->GetPathName()));
		Lines.Add(FString::Printf(TEXT("  ParentClass=%s"), *GetPathNameSafe(Blueprint->ParentClass)));
		TArray<FString> Functions;
		for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			Functions.Add(GetNameSafe(Graph));
		}
		Functions.Sort();
		Lines.Add(FString::Printf(TEXT("  FunctionGraphs=%s"), *FString::Join(Functions, TEXT(","))));
		if (const FProperty* Bindings = Blueprint->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UDreamWidgetBlueprint, PropertyBindings)))
		{
			FString Value;
			Bindings->ExportText_InContainer(0, Value, Blueprint, nullptr, const_cast<UDreamWidgetBlueprint*>(Blueprint), PPF_None);
			Lines.Add(FString::Printf(TEXT("  PropertyBindings=%s"), *Value));
		}
		if (IsValid(Blueprint->WidgetTree))
		{
			DescribeObjectAndInner(Blueprint->WidgetTree, InPackage, Lines);
		}
	}
	else if (const UWorld* World = FindObject<UWorld>(Package, *AssetName))
	{
		Lines.Add(FString::Printf(TEXT("world %s"), *World->GetName()));
		TArray<const AActor*> Actors;
		if (World->PersistentLevel != nullptr)
		{
			for (const AActor* Actor : World->PersistentLevel->Actors)
			{
				if (IsValid(Actor) && IsFixtureActor(Actor))
				{
					Actors.Add(Actor);
				}
			}
		}
		Actors.Sort([](const AActor& A, const AActor& B) { return A.GetName() < B.GetName(); });
		for (const AActor* Actor : Actors)
		{
			DescribeObjectAndInner(Actor, InPackage, Lines);
		}
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("no widget Blueprint or world named %s"), *AssetName));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString DreamOldAssetFixtures::ApplyRedirects(const FString& InText)
{
	static const FRegexPattern ScriptPath(TEXT("/Script/[A-Za-z0-9_]+\\.[A-Za-z0-9_]+"));
	FRegexMatcher Matcher(ScriptPath, InText);
	FString Out;
	int32 Copied = 0;
	while (Matcher.FindNext())
	{
		const int32 Begin = Matcher.GetMatchBeginning();
		const int32 End = Matcher.GetMatchEnding();
		const FString Path = InText.Mid(Begin, End - Begin);
		// A class default object is named for its class, so it goes wherever its class goes.
		const FString DefaultPrefix = TEXT("Default__");
		int32 Dot = INDEX_NONE;
		Path.FindChar(TEXT('.'), Dot);
		const bool bIsDefault = Path.Mid(Dot + 1).StartsWith(DefaultPrefix);
		const FString TypePath = bIsDefault ? Path.Left(Dot + 1) + Path.Mid(Dot + 1 + DefaultPrefix.Len()) : Path;
		FString Redirected = DreamUI::ApplyTypeRedirects(ECoreRedirectFlags::Type_Class, TypePath);
		if (Redirected == TypePath)
		{
			Redirected = DreamUI::ApplyTypeRedirects(ECoreRedirectFlags::Type_Struct, TypePath);
		}
		if (Redirected == TypePath)
		{
			Redirected = DreamUI::ApplyTypeRedirects(ECoreRedirectFlags::Type_Enum, TypePath);
		}
		if (bIsDefault)
		{
			int32 NewDot = INDEX_NONE;
			Redirected.FindChar(TEXT('.'), NewDot);
			Redirected = Redirected.Left(NewDot + 1) + DefaultPrefix + Redirected.Mid(NewDot + 1);
		}
		Out += InText.Mid(Copied, Begin - Copied);
		Out += Redirected;
		Copied = End;
	}
	Out += InText.Mid(Copied);
	return Out;
}

TMap<FString, FString> DreamOldAssetFixtures::ParseSnapshot(const FString& InSnapshot)
{
	TMap<FString, FString> Blocks;
	TArray<FString> Lines;
	InSnapshot.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), /*bCullEmpty*/false);
	FString Current;
	TArray<FString> Block;
	auto Flush = [&Blocks, &Current, &Block]()
	{
		if (!Current.IsEmpty())
		{
			while (Block.Num() > 0 && Block.Last().IsEmpty())
			{
				Block.Pop();
			}
			Blocks.Add(Current, FString::Join(Block, TEXT("\n")));
		}
		Block.Reset();
	};
	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("#")))
		{
			continue;
		}
		if (Line.StartsWith(TEXT("package ")))
		{
			Flush();
			Current = Line.Mid(8).TrimStartAndEnd();
			continue;
		}
		Block.Add(Line);
	}
	Flush();
	return Blocks;
}
