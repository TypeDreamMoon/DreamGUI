// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetPropertyBindingExtension.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamGUIEditorModule.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"

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
			int32 Index = IsValid(Owner) ? Owner->GetAllComponents().Find(Behaviour) : INDEX_NONE;
			if (Index != INDEX_NONE)
			{
				if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(Owner))
				{
					UDreamUIBehaviour* Authored = Designer->GetPreviewHost()->FindTemplateComponentForPreview(Behaviour);
					if (Authored == nullptr)return Site;
					Index = Authored->GetWidget()->GetAllComponents().Find(Authored);
				}
			}
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
		// A `.dui` class carries its bindings in the file, and the compiler REPLACES
		// UDreamWidgetBlueprint::PropertyBindings from the `<-` lines wholesale on every compile -- so on one, the entries
		// below write the line (SetBinding and its siblings) and the compile reads it back. Only a widget the file does
		// not write (inside a nested instance) has nowhere for a line, and the control comes off its rows.
		if (!DreamUITextAuthoring::CanAuthorBindingsOn(InObject))
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

	namespace TextLocal
	{
		/** The TEMPLATE object a site names -- the copy the file describes -- found by the compiler's name for the widget. */
		const UObject* ResolveTemplateOwner(const UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite)
		{
			if (!IsValid(InBlueprint) || !IsValid(InBlueprint->WidgetTree))
			{
				return nullptr;
			}
			const UDreamWidget* Widget = InBlueprint->WidgetTree->FindWidgetByVariableName(InSite.WidgetName);
			if (!IsValid(Widget))
			{
				return nullptr;
			}
			switch (InSite.Target)
			{
			case EDreamWidgetBindingTarget::Widget:
				return Widget;
			case EDreamWidgetBindingTarget::Visual:
				return Widget->GetVisual();
			case EDreamWidgetBindingTarget::Behaviour:
				return Widget->GetAllComponents().IsValidIndex(InSite.BehaviourIndex) ? Widget->GetAllComponents()[InSite.BehaviourIndex] : nullptr;
			default:
				return nullptr;
			}
		}

		/** The edit a statement about InPropertyName on this site would be, or false with the reason it has no line. */
		bool PlanStatement(const UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName,
			EDreamUIStructuralEditKind InKind, const FString& InStatement, const FString& InMatch, FDreamUIStructuralEdit& OutEdit, FText& OutReason)
		{
			const UObject* Owner = ResolveTemplateOwner(InBlueprint, InSite);
			if (Owner == nullptr)
			{
				OutReason = LOCTEXT("NoTemplateOwner", "this object has no counterpart in the Blueprint's own hierarchy");
				return false;
			}
			if (!DreamUITextAuthoring::ResolveStatementSite(InBlueprint, Owner, InPropertyName, OutEdit, OutReason))
			{
				return false;
			}
			OutEdit.Kind = InKind;
			OutEdit.Statement = InStatement;
			OutEdit.StatementMatch = InMatch;
			return true;
		}

		/**
		 * A binding on a text-authored class is its line: written into the `.dui` inside the caller's transaction, and read
		 * back by the compile this marks for. A refusal is said with the patcher's reason, never dropped.
		 */
		bool WriteStatement(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName,
			EDreamUIStructuralEditKind InKind, const FString& InStatement, const FString& InMatch, const TCHAR* InFunction, int32 InLine,
			const FString& InOperation)
		{
			FDreamUIStructuralEdit Edit;
			FText Reason;
			if (!PlanStatement(InBlueprint, InSite, InPropertyName, InKind, InStatement, InMatch, Edit, Reason)
				|| !DreamUITextAuthoring::WriteStatementToText(InBlueprint, Edit, Reason))
			{
				DreamUITextAuthoring::ReportStructuralRefusal(InBlueprint, InFunction, InLine, InOperation, Reason);
				return false;
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
			return true;
		}
	}

	bool SetBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName, FName InFunctionName)
	{
		if (!IsValid(InBlueprint))
		{
			return false;
		}
		if (DreamUITextAuthoring::IsTextAuthored(InBlueprint))
		{
			return TextLocal::WriteStatement(InBlueprint, InSite, InPropertyName, EDreamUIStructuralEditKind::SetStatement,
				FString::Printf(TEXT("<- %s()"), *InFunctionName.ToString()), FString(), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
				FString::Printf(TEXT("bind '%s'"), *InPropertyName.ToString()));
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
		return true;
	}

	bool RemoveBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName)
	{
		if (!IsValid(InBlueprint))
		{
			return false;
		}
		if (DreamUITextAuthoring::IsTextAuthored(InBlueprint))
		{
			// The line, not the list: the compile rebuilds the list from the file, so dropping only the entry would be a
			// "remove" the next compile reverts. The line naming the bound function, or the property's only line.
			const FDreamWidgetPropertyBinding* Bound = FindBinding(InBlueprint, InSite, InPropertyName);
			const FString Match = Bound == nullptr ? FString()
				: !Bound->NotifyField.IsNone() ? Bound->NotifyField.ToString() : Bound->FunctionName.ToString();
			return TextLocal::WriteStatement(InBlueprint, InSite, InPropertyName, EDreamUIStructuralEditKind::RemoveProperty,
				FString(), Match, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, FString::Printf(TEXT("unbind '%s'"), *InPropertyName.ToString()));
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
		return true;
	}

	bool UnbindAndSetValue(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName, const FString& InValueText)
	{
		if (!DreamUITextAuthoring::IsTextAuthored(InBlueprint))
		{
			// A hand-authored class keeps its value under the binding: dropping the binding is all it takes.
			return RemoveBinding(InBlueprint, InSite, InPropertyName);
		}
		const FDreamWidgetPropertyBinding* Bound = FindBinding(InBlueprint, InSite, InPropertyName);
		const FString Match = Bound == nullptr ? FString()
			: !Bound->NotifyField.IsNone() ? Bound->NotifyField.ToString() : Bound->FunctionName.ToString();
		return TextLocal::WriteStatement(InBlueprint, InSite, InPropertyName, EDreamUIStructuralEditKind::SetStatement,
			FString::Printf(TEXT("= %s"), *InValueText.TrimStartAndEnd()), Match, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
			FString::Printf(TEXT("unbind '%s' and set it to %s"), *InPropertyName.ToString(), *InValueText.TrimStartAndEnd()));
	}

	bool UnbindAndKeepValue(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, const FProperty* InProperty,
		const UObject* InValueOwner)
	{
		if (InProperty == nullptr || !IsValid(InValueOwner) || !InValueOwner->GetClass()->IsChildOf(InProperty->GetOwnerClass()))
		{
			return false;
		}
		FString ValueText;
		if (!FDreamUITextWriteBack::PrintValueLiteral(InProperty, InProperty->ContainerPtrToValuePtr<void>(InValueOwner), ValueText))
		{
			DreamUITextAuthoring::ReportStructuralRefusal(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
				FString::Printf(TEXT("unbind '%s' and keep its value"), *InProperty->GetName()),
				LOCTEXT("NoSpelling", "its value has no spelling in a .dui; remove the binding, then set the value"));
			return false;
		}
		return UnbindAndSetValue(InBlueprint, InSite, InProperty->GetFName(), ValueText);
	}

	bool CanBindTwoWay(UObject* InObject)
	{
		if (!IsValid(InObject))
		{
			return false;
		}
		const FBindingSite Site = ResolveBindingSite(InObject);
		FDreamWidgetBlueprintEditor* Designer = Site.IsValid() ? FDreamWidgetBlueprintEditor::FindDesignerForWidget(Site.Widget) : nullptr;
		// The convention the compiler routes the reverse half to (DreamUIExpressionThunks, LowerTwoWay): one changed event,
		// one name, on every value control.
		return Designer != nullptr && DreamUITextAuthoring::IsTextAuthored(Designer->GetWidgetBlueprint())
			&& InObject->GetClass()->FindPropertyByName(TEXT("OnValueChangedBP")) != nullptr;
	}

	TArray<FString> CollectTwoWayVariables(UDreamWidgetBlueprint* InBlueprint, const UClass* InSubjectClass, const FProperty* InProperty)
	{
		TArray<FString> Result;
		UClass* Class = IsValid(InBlueprint) ? InBlueprint->SkeletonGeneratedClass.Get() : nullptr;
		if (Class == nullptr || InProperty == nullptr || InSubjectClass == nullptr)
		{
			return Result;
		}
		// The one value the changed event hands its listener: the reverse half's setter takes the variable's type and is
		// routed to that event, whose signature it has to match exactly.
		const FProperty* EventValue = nullptr;
		if (const FMulticastDelegateProperty* Event = CastField<FMulticastDelegateProperty>(InSubjectClass->FindPropertyByName(TEXT("OnValueChangedBP"))))
		{
			int32 Count = 0;
			for (TFieldIterator<FProperty> It(Event->SignatureFunction); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					EventValue = *It;
					++Count;
				}
			}
			EventValue = Count == 1 ? EventValue : nullptr;
		}
		// Both ways: the getter feeds the control, and the control's value is written back into the variable.
		auto Fits = [InProperty, EventValue](const FProperty* InCandidate)
		{
			return CanDreamWidgetBoundValueConvert(InCandidate, InProperty)
				&& (EventValue != nullptr ? EventValue->SameType(InCandidate) : CanDreamWidgetBoundValueConvert(InProperty, InCandidate));
		};
		auto IsVariable = [](const FProperty* InCandidate)
		{
			return InCandidate->HasAnyPropertyFlags(CPF_BlueprintVisible) && !InCandidate->HasAnyPropertyFlags(CPF_Parm)
				&& CastField<FMulticastDelegateProperty>(InCandidate) == nullptr && CastField<FDelegateProperty>(InCandidate) == nullptr;
		};
		// The class's OWN variables: its Blueprint variables, the `props` and `viewmodel` entries of its file. The widgets'
		// own member variables are object references to widgets, and are no value to mirror.
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const FProperty* Variable = *It;
			if (!IsVariable(Variable))
			{
				continue;
			}
			if (Fits(Variable))
			{
				// Written by a self set, which a graph may make only of a variable it can write.
				if (!Variable->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
				{
					Result.Add(Variable->GetName());
				}
				continue;
			}
			const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(Variable);
			UClass* Held = AsObject != nullptr ? AsObject->PropertyClass.Get() : nullptr;
			if (Held == nullptr || Held->IsChildOf(UDreamWidget::StaticClass()) || Held->IsChildOf(UDreamUIBehaviour::StaticClass()))
			{
				continue;
			}
			for (TFieldIterator<FProperty> Member(Held, EFieldIteratorFlags::IncludeSuper); Member; ++Member)
			{
				if (!IsVariable(*Member) || !Fits(*Member))
				{
					continue;
				}
				// Through the holder's own setter when it has one, else by a set -- not of a read-only member.
				const bool bWritable = !Member->HasAnyPropertyFlags(CPF_BlueprintReadOnly)
					|| Held->FindFunctionByName(FName(*(TEXT("Set") + Member->GetName()))) != nullptr;
				if (bWritable)
				{
					Result.Add(FString::Printf(TEXT("%s.%s"), *Variable->GetName(), *Member->GetName()));
				}
			}
		}
		Result.Sort();
		return Result;
	}

	bool SetTwoWayBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName, const FString& InVariablePath)
	{
		if (!DreamUITextAuthoring::IsTextAuthored(InBlueprint))
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d '%s <-> %s' is written in a .dui, and '%s' authors its own hierarchy."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InPropertyName.ToString(), *InVariablePath, *GetNameSafe(InBlueprint));
			return false;
		}
		return TextLocal::WriteStatement(InBlueprint, InSite, InPropertyName, EDreamUIStructuralEditKind::SetStatement,
			FString::Printf(TEXT("<-> %s"), *InVariablePath.TrimStartAndEnd()), FString(), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
			FString::Printf(TEXT("bind '%s' both ways to %s"), *InPropertyName.ToString(), *InVariablePath));
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
			// IncludeSuper on purpose: a getter inherited from a base user widget drives a property
			// exactly as well as one declared here, and hiding it would be a rule the compiler does
			// not have. What DOES have to be filtered is the kind of function -- the iterator also
			// yields native internals, delegate signatures and editor-only helpers, none of which an
			// author declared as callable, and offering them makes a binding the next compile rejects.
			for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				UFunction* Function = *It;
				if (Function == nullptr
					|| !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable)
					|| Function->HasAnyFunctionFlags(FUNC_Delegate | FUNC_EditorOnly))
				{
					continue;
				}
				const FProperty* ReturnProperty = Function->GetReturnProperty();
				// The compiler's own rule, not a copy of it: it stopped being plain SameType when two
				// plain numeric types became bindable whatever their width (see
				// CanDreamWidgetBoundValueConvert), and a panel that offers less than the compiler
				// accepts is the same drift as one that offers more -- the author's double-returning
				// getter would simply not be in the list, with nothing to say why.
				if (Function->NumParms != 1 || ReturnProperty == nullptr
					|| !CanDreamWidgetBoundValueConvert(ReturnProperty, InProperty))
				{
					continue;
				}
				Result.AddUnique(Function->GetFName());
			}
			Result.Sort(FNameLexicalLess());
			return Result;
		}


	}

	/**
	 * A new function shaped to feed this property, bound and opened.
	 *
	 * Built rather than merely offered because the alternative is telling the author to go make a
	 * function with exactly the right signature and come back -- which is the step they came here
	 * to skip.
	 */
	UEdGraph* CreateAndBindFunction(FDreamWidgetBlueprintEditor* InDesigner, UDreamWidgetBlueprint* InBlueprint,
		const FBindingSite& InSite, const FProperty* InProperty)
	{
		if (!IsValid(InBlueprint) || InProperty == nullptr)
		{
			return nullptr;
		}
		FEdGraphPinType PinType;
		if (!GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(InProperty, PinType))
		{
			return nullptr;
		}

		const FName GraphName = FBlueprintEditorUtils::FindUniqueKismetName(InBlueprint,
			FString::Printf(TEXT("Get%s_%s"), *InSite.WidgetName.ToString(), *InProperty->GetName()));

		// Before the graph is created, not after: this one does not merely record a binding, it adds a function to the
		// Blueprint and opens it. A `<-` line the file cannot take, found out later, would leave the author looking at a
		// brand new empty function that nothing will ever call.
		if (DreamUITextAuthoring::IsTextAuthored(InBlueprint))
		{
			FDreamUIStructuralEdit Planned;
			FText Reason;
			if (!TextLocal::PlanStatement(InBlueprint, InSite, InProperty->GetFName(), EDreamUIStructuralEditKind::SetStatement,
					FString::Printf(TEXT("<- %s()"), *GraphName.ToString()), FString(), Planned, Reason)
				|| !DreamUITextAuthoring::CheckStructuralEdit(InBlueprint, Planned, /*bInVerifyBuild*/true, Reason))
			{
				DreamUITextAuthoring::ReportStructuralRefusal(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
					FString::Printf(TEXT("create a binding for '%s'"), *InProperty->GetName()), Reason);
				return nullptr;
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("CreateBinding", "Create Binding"));
		InBlueprint->Modify();
		UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
			InBlueprint, GraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		if (FunctionGraph == nullptr)
		{
			return nullptr;
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
		return FunctionGraph;
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
		// A two-way binding's function is the getter the compiler generated; what the author wrote is the variable.
		auto GetBoundLabel = [Blueprint, Site, PropertyName]() -> FText
		{
			const FDreamWidgetPropertyBinding* Binding = FindBinding(Blueprint, Site, PropertyName);
			if (Binding == nullptr || Binding->FunctionName.IsNone())
			{
				return LOCTEXT("Bind", "Bind");
			}
			return Binding->NotifyField.IsNone() ? FText::FromName(Binding->FunctionName)
				: FText::Format(LOCTEXT("TwoWayLabel", "<-> {0}"), FText::FromName(Binding->NotifyField));
		};
		const bool bTextAuthored = DreamUITextAuthoring::IsTextAuthored(Blueprint);
		const bool bTwoWay = CanBindTwoWay(InObject);
		const TWeakObjectPtr<UObject> ValueOwner = InObject;
		const TWeakObjectPtr<const UClass> SubjectClass = InObject->GetClass();

		return SNew(SComboButton)
			.ToolTipText(LOCTEXT("BindTooltip", "Drive this property from a function, evaluated every frame."))
			.ContentPadding(FMargin(4.0f, 0.0f))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text_Lambda(GetBoundLabel)
			]
			.OnGetMenuContent_Lambda([InDesigner, Blueprint, Site, PropertyName, Property, GetBoundFunction, bTextAuthored, bTwoWay, ValueOwner, SubjectClass]()
			{
				FMenuBuilder MenuBuilder(/*bCloseAfterSelection*/true, nullptr);

				MenuBuilder.BeginSection(NAME_None, LOCTEXT("BindSection", "Bind"));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateBindingEntry", "Create Binding"),
					LOCTEXT("CreateBindingEntryTip", "Add a function of the right shape, bind it, and open it."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Plus")),
					FUIAction(FExecuteAction::CreateLambda([InDesigner, Blueprint, Site, Property]()
					{
						CreateAndBindFunction(InDesigner, Blueprint, Site, Property);
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

				if (bTwoWay)
				{
					// Only on a control that announces its value through OnValueChangedBP: anything else would bind one way.
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("TwoWaySection", "Two-Way Bind to Variable"));
					const TArray<FString> Variables = CollectTwoWayVariables(Blueprint, SubjectClass.Get(), Property);
					for (const FString& Variable : Variables)
					{
						MenuBuilder.AddMenuEntry(
							FText::Format(LOCTEXT("TwoWayEntry", "<-> {0}"), FText::FromString(Variable)),
							LOCTEXT("TwoWayEntryTip", "Mirror this property and the variable both ways: the variable drives the control, and the control's changes are written back into it."),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([Blueprint, Site, PropertyName, Variable]()
							{
								const FScopedTransaction Transaction(LOCTEXT("SetTwoWayBinding", "Two-Way Bind to Variable"));
								SetTwoWayBinding(Blueprint, Site, PropertyName, Variable);
							})));
					}
					if (Variables.IsEmpty())
					{
						MenuBuilder.AddMenuEntry(
							LOCTEXT("NoTwoWayVariable", "No variable of a matching type"),
							LOCTEXT("NoTwoWayVariableTip", "Declare one in the file's props, or a viewmodel with a member of this type, and compile."),
							FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
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
					if (bTextAuthored)
					{
						// The value row of a bound property is read-only -- a literal there would be overwritten by the binding.
						// This is the way to turn the binding into the value the row shows, as one line.
						MenuBuilder.AddMenuEntry(
							LOCTEXT("UnbindAndSetEntry", "Unbind and Set to This Value"),
							LOCTEXT("UnbindAndSetEntryTip", "Replace the binding's line in the file with the value this property shows now."),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Edit")),
							FUIAction(FExecuteAction::CreateLambda([Blueprint, Site, Property, ValueOwner]()
							{
								const FScopedTransaction Transaction(LOCTEXT("UnbindAndSet", "Unbind and Set Value"));
								UnbindAndKeepValue(Blueprint, Site, Property, ValueOwner.Get());
							})));
					}
					MenuBuilder.EndSection();
				}

				return MenuBuilder.MakeWidget();
			});
	}
}

#undef LOCTEXT_NAMESPACE
