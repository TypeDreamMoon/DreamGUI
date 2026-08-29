// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetPropertyBindingExtension.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintEditor.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"

#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamWidgetPropertyBindingExtension"

namespace DreamWidgetPropertyBindingExtension
{
	FBindingSite ResolveBindingSite(UObject* InObject)
	{
		FBindingSite Site;
		if (!IsValid(InObject))
		{
			return Site;
		}

		if (UDreamWidget* Widget = Cast<UDreamWidget>(InObject))
		{
			Site.Widget = Widget;
			Site.Target = EDreamWidgetBindingTarget::Widget;
		}
		else if (UDreamVisual* Visual = Cast<UDreamVisual>(InObject))
		{
			// Only the widget's OWN visual: a visual reached some other way is not something a
			// binding can name, because the name it would need belongs to the widget.
			UDreamWidget* Owner = Visual->GetWidget();
			if (IsValid(Owner) && Owner->GetVisual() == Visual)
			{
				Site.Widget = Owner;
				Site.Target = EDreamWidgetBindingTarget::Visual;
			}
		}
		else if (UDreamUIBehaviour* Behaviour = Cast<UDreamUIBehaviour>(InObject))
		{
			UDreamWidget* Owner = Behaviour->GetWidget();
			const int32 Index = IsValid(Owner) ? Owner->GetAllComponents().Find(Behaviour) : INDEX_NONE;
			if (Index != INDEX_NONE)
			{
				Site.Widget = Owner;
				Site.Target = EDreamWidgetBindingTarget::Behaviour;
				Site.BehaviourIndex = Index;
			}
		}

		if (Site.Widget != nullptr)
		{
			// The compiler's name for it, which is what a binding stores and what the runtime looks up.
			Site.WidgetName = UDreamWidgetTree::MakeWidgetVariableName(Site.Widget);
		}
		return Site;
	}

	bool IsBindable(UObject* InObject, const FProperty* InProperty)
	{
		if (!IsValid(InObject) || InProperty == nullptr)
		{
			return false;
		}
		// Bindings live on a Blueprint, so a widget in a level has nowhere to put one.
		const FBindingSite Site = ResolveBindingSite(InObject);
		if (!Site.IsValid() || FDreamWidgetBlueprintEditor::FindDesignerForWidget(Site.Widget) == nullptr)
		{
			return false;
		}
		// Editable, and settable. The second half is the same question the compiler asks.
		if (!InProperty->HasAnyPropertyFlags(CPF_Edit) || InProperty->HasAnyPropertyFlags(CPF_EditConst))
		{
			return false;
		}
		return FindDreamWidgetSetterFor(InObject->GetClass(), InProperty) != nullptr;
	}

	/** The authored binding for this site and property, if there is one. */
	FDreamWidgetPropertyBinding* FindBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName)
	{
		if (!IsValid(InBlueprint))
		{
			return nullptr;
		}
		return InBlueprint->PropertyBindings.FindByPredicate(
			[&InSite, InPropertyName](const FDreamWidgetPropertyBinding& Candidate)
			{
				return Candidate.WidgetName == InSite.WidgetName
					&& Candidate.Target == InSite.Target
					&& Candidate.BehaviourIndex == InSite.BehaviourIndex
					&& Candidate.PropertyName == InPropertyName;
			});
	}

	void SetBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName, FName InFunctionName)
	{
		if (!IsValid(InBlueprint))
		{
			return;
		}
		InBlueprint->Modify();
		if (FDreamWidgetPropertyBinding* Existing = FindBinding(InBlueprint, InSite, InPropertyName))
		{
			Existing->FunctionName = InFunctionName;
		}
		else
		{
			FDreamWidgetPropertyBinding& Binding = InBlueprint->PropertyBindings.AddDefaulted_GetRef();
			Binding.WidgetName = InSite.WidgetName;
			Binding.Target = InSite.Target;
			Binding.BehaviourIndex = InSite.BehaviourIndex;
			Binding.PropertyName = InPropertyName;
			Binding.FunctionName = InFunctionName;
		}
		// Structurally, so the compile that resolves the binding actually runs.
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
	}

	void RemoveBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName)
	{
		if (!IsValid(InBlueprint))
		{
			return;
		}
		InBlueprint->Modify();
		InBlueprint->PropertyBindings.RemoveAll(
			[&InSite, InPropertyName](const FDreamWidgetPropertyBinding& Candidate)
			{
				return Candidate.WidgetName == InSite.WidgetName
					&& Candidate.Target == InSite.Target
					&& Candidate.BehaviourIndex == InSite.BehaviourIndex
					&& Candidate.PropertyName == InPropertyName;
			});
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
	}

	namespace Local
	{
		/** Functions on the Blueprint that could feed this property: no arguments, right return type. */
		TArray<FName> CollectEligibleFunctions(UDreamWidgetBlueprint* InBlueprint, const FProperty* InProperty)
		{
			TArray<FName> Result;
			// The SKELETON class: a function added a moment ago exists there before a full compile,
			// and offering only what the last compile produced would hide the one just created.
			UClass* Class = IsValid(InBlueprint) ? InBlueprint->SkeletonGeneratedClass.Get() : nullptr;
			if (Class == nullptr || InProperty == nullptr)
			{
				return Result;
			}
			for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				UFunction* Function = *It;
				const FProperty* ReturnProperty = Function->GetReturnProperty();
				if (Function->NumParms != 1 || ReturnProperty == nullptr || !ReturnProperty->SameType(InProperty))
				{
					continue;
				}
				Result.AddUnique(Function->GetFName());
			}
			Result.Sort(FNameLexicalLess());
			return Result;
		}

		/**
		 * A new function shaped to feed this property, bound and opened.
		 *
		 * Built rather than merely offered because the alternative is telling the author to go make a
		 * function with exactly the right signature and come back -- which is the step they came here
		 * to skip.
		 */
		void CreateAndBindFunction(FDreamWidgetBlueprintEditor* InDesigner, UDreamWidgetBlueprint* InBlueprint,
			const FBindingSite& InSite, const FProperty* InProperty)
		{
			if (!IsValid(InBlueprint) || InProperty == nullptr)
			{
				return;
			}
			FEdGraphPinType PinType;
			if (!GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(InProperty, PinType))
			{
				return;
			}

			const FScopedTransaction Transaction(LOCTEXT("CreateBinding", "Create Binding"));
			InBlueprint->Modify();

			const FName GraphName = FBlueprintEditorUtils::FindUniqueKismetName(InBlueprint,
				FString::Printf(TEXT("Get%s_%s"), *InSite.WidgetName.ToString(), *InProperty->GetName()));
			UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
				InBlueprint, GraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			if (FunctionGraph == nullptr)
			{
				return;
			}
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(InBlueprint, FunctionGraph, /*bIsUserCreated*/true, nullptr);

			// The entry node exists by now; the result node does not, and the return pin lives on it.
			UK2Node_FunctionEntry* EntryNode = nullptr;
			for (UEdGraphNode* Node : FunctionGraph->Nodes)
			{
				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					EntryNode = Entry;
					break;
				}
			}
			if (EntryNode != nullptr)
			{
				// Pure: a binding is asked for a value, and giving it an exec pin invites side effects
				// on a function that runs every frame.
				EntryNode->AddExtraFlags(FUNC_BlueprintPure);
				EntryNode->MetaData.Category = LOCTEXT("BindingCategory", "Bindings");
			}

			FGraphNodeCreator<UK2Node_FunctionResult> ResultCreator(*FunctionGraph);
			UK2Node_FunctionResult* ResultNode = ResultCreator.CreateNode();
			ResultNode->FunctionReference.SetSelfMember(GraphName);
			ResultNode->NodePosX = (EntryNode != nullptr ? EntryNode->NodePosX : 0) + 400;
			ResultNode->NodePosY = (EntryNode != nullptr ? EntryNode->NodePosY : 0);
			ResultCreator.Finalize();
			ResultNode->CreateUserDefinedPin(UEdGraphSchema_K2::PN_ReturnValue, PinType, EGPD_Input);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
			SetBinding(InBlueprint, InSite, InProperty->GetFName(), GraphName);

			// Straight into it: an empty function returning a default is not what was asked for, it is
			// the place the author now has to write what was.
			if (InDesigner != nullptr)
			{
				InDesigner->OpenDocument(FunctionGraph, FDocumentTracker::OpenNewDocument);
			}
		}
	}

	TSharedPtr<SWidget> MakeBindingWidget(FDreamWidgetBlueprintEditor* InDesigner, UObject* InObject,
		TSharedPtr<IPropertyHandle> InPropertyHandle)
	{
		if (InDesigner == nullptr || !InPropertyHandle.IsValid())
		{
			return nullptr;
		}
		const FProperty* Property = InPropertyHandle->GetProperty();
		if (!IsBindable(InObject, Property))
		{
			return nullptr;
		}
		const FBindingSite Site = ResolveBindingSite(InObject);
		UDreamWidgetBlueprint* Blueprint = InDesigner->GetWidgetBlueprint();
		if (!IsValid(Blueprint) || !Site.IsValid())
		{
			return nullptr;
		}
		const FName PropertyName = Property->GetFName();

		auto GetBoundFunction = [Blueprint, Site, PropertyName]() -> FName
		{
			const FDreamWidgetPropertyBinding* Binding = FindBinding(Blueprint, Site, PropertyName);
			return Binding != nullptr ? Binding->FunctionName : NAME_None;
		};

		return SNew(SComboButton)
			.ToolTipText(LOCTEXT("BindTooltip", "Drive this property from a function, evaluated every frame."))
			.ContentPadding(FMargin(4.0f, 0.0f))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text_Lambda([GetBoundFunction]()
				{
					const FName Bound = GetBoundFunction();
					return Bound.IsNone() ? LOCTEXT("Bind", "Bind") : FText::FromName(Bound);
				})
			]
			.OnGetMenuContent_Lambda([InDesigner, Blueprint, Site, PropertyName, Property, GetBoundFunction]()
			{
				FMenuBuilder MenuBuilder(/*bCloseAfterSelection*/true, nullptr);

				MenuBuilder.BeginSection(NAME_None, LOCTEXT("BindSection", "Bind"));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateBindingEntry", "Create Binding"),
					LOCTEXT("CreateBindingEntryTip", "Add a function of the right shape, bind it, and open it."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Plus")),
					FUIAction(FExecuteAction::CreateLambda([InDesigner, Blueprint, Site, Property]()
					{
						Local::CreateAndBindFunction(InDesigner, Blueprint, Site, Property);
					})));
				MenuBuilder.EndSection();

				const TArray<FName> Functions = Local::CollectEligibleFunctions(Blueprint, Property);
				if (Functions.Num() > 0)
				{
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("ExistingSection", "Functions"));
					for (const FName FunctionName : Functions)
					{
						MenuBuilder.AddMenuEntry(
							FText::FromName(FunctionName), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([Blueprint, Site, PropertyName, FunctionName]()
							{
								const FScopedTransaction Transaction(LOCTEXT("SetBinding", "Set Binding"));
								SetBinding(Blueprint, Site, PropertyName, FunctionName);
							})));
					}
					MenuBuilder.EndSection();
				}

				if (!GetBoundFunction().IsNone())
				{
					MenuBuilder.BeginSection(NAME_None);
					MenuBuilder.AddMenuEntry(
						LOCTEXT("RemoveBindingEntry", "Remove Binding"),
						LOCTEXT("RemoveBindingEntryTip", "Leave the property at its authored value."),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Delete")),
						FUIAction(FExecuteAction::CreateLambda([Blueprint, Site, PropertyName]()
						{
							const FScopedTransaction Transaction(LOCTEXT("RemoveBinding", "Remove Binding"));
							RemoveBinding(Blueprint, Site, PropertyName);
						})));
					MenuBuilder.EndSection();
				}

				return MenuBuilder.MakeWidget();
			});
	}
}

#undef LOCTEXT_NAMESPACE
