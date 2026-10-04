// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/PropertyType/DreamGradientCustomization.h"

#include "AssetToolsModule.h"
#include "Core/DreamGradientAsset.h"
#include "Core/DreamGUISettings.h"
#include "DataFactory/DreamGradientAssetFactory.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IAssetTools.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "Misc/Char.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "SColorGradientEditor.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widget/SDreamGradientPreview.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamGradientCustomization"

namespace DreamGradientCustomizationLocal
{
	FName GetChannelName(int32 InChannel)
	{
		static const FName Names[4] = { FName(TEXT("R")), FName(TEXT("G")), FName(TEXT("B")), FName(TEXT("A")) };
		return Names[FMath::Clamp(InChannel, 0, 3)];
	}

	/** The stops left to right, in InInterpolation: what the stop editor's strip, and its new marks, are coloured from. */
	FDreamGradient MakeStrip(const TArray<FDreamGradientStop>& InStops, EDreamPaintInterpolation InInterpolation)
	{
		FDreamGradient Strip;
		Strip.Type = EDreamPaintType::Linear;
		// To the right: across the unit square the gradient's position is the strip's u.
		Strip.Angle = 90.0f;
		Strip.Spread = EDreamPaintSpread::Pad;
		Strip.Scale = 1.0f;
		Strip.Offset = 0.0f;
		Strip.Interpolation = InInterpolation;
		Strip.Stops = InStops;
		return Strip;
	}

	/** The strip's colour at InPosition along the gradient: straight alpha, linear light. */
	FLinearColor EvaluateStrip(const FDreamGradient& InStrip, float InPosition)
	{
		return InStrip.Evaluate(FVector2f(InPosition, 0.5f), 1.0f);
	}

	/** A key's colour as a stop holds it: sRGB-encoded with straight alpha. */
	FColor ToStopColor(const FLinearColor& InLinear)
	{
		FColor Color = FLinearColor(InLinear.R, InLinear.G, InLinear.B, 1.0f).ToFColorSRGB();
		Color.A = (uint8)FMath::Clamp(FMath::RoundToInt(InLinear.A * 255.0f), 0, 255);
		return Color;
	}

	/**
	 * Whether InKey holds what SColorGradientEditor gives a mark it adds with a plain click: the curve's value there
	 * before the key existed, its straight line between the keys around it (all of them linear), the end values past them.
	 */
	bool IsStraightLineValue(const FRichCurve& InCurve, FKeyHandle InKey)
	{
		const int32 KeyIndex = InCurve.GetIndexSafe(InKey);
		const TArray<FRichCurveKey>& Keys = InCurve.GetConstRefOfKeys();
		if (!Keys.IsValidIndex(KeyIndex))
		{
			return false;
		}
		const float Time = Keys[KeyIndex].Time;
		const FRichCurveKey* Before = nullptr;
		const FRichCurveKey* After = nullptr;
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			if (Index == KeyIndex)
			{
				continue;
			}
			if (Keys[Index].Time <= Time)
			{
				Before = &Keys[Index];
			}
			else if (After == nullptr)
			{
				After = &Keys[Index];
			}
		}
		// What the editor asks an empty curve for.
		float Expected = 1.0f;
		if (Before != nullptr && After != nullptr)
		{
			const float Span = After->Time - Before->Time;
			Expected = Span > 0.0f ? FMath::Lerp(Before->Value, After->Value, (Time - Before->Time) / Span) : Before->Value;
		}
		else if (Before != nullptr)
		{
			Expected = Before->Value;
		}
		else if (After != nullptr)
		{
			Expected = After->Value;
		}
		return FMath::IsNearlyEqual(Keys[KeyIndex].Value, Expected, 1.0e-4f);
	}

	void DeleteKeyIfValid(FRichCurve& InCurve, FKeyHandle InKey)
	{
		// FRichCurve::DeleteKey checks the handle rather than testing it.
		if (InCurve.IsKeyHandleValid(InKey))
		{
			InCurve.DeleteKey(InKey);
		}
	}

	void SetKeyTimeIfDifferent(FRichCurve& InCurve, FKeyHandle InKey, float InTime)
	{
		if (InCurve.IsKeyHandleValid(InKey) && InCurve.GetKeyTime(InKey) != InTime)
		{
			InCurve.SetKeyTime(InKey, InTime);
		}
	}

	bool IsGradientHandle(const TSharedRef<IPropertyHandle>& InHandle)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(InHandle->GetProperty());
		return StructProperty != nullptr && StructProperty->Struct == FDreamGradient::StaticStruct();
	}

	/**
	 * The first gradient InHandle edits. bOutMultipleValues: whether the others differ from it. The editing rows show the
	 * first and write to all; the CSS row says the selection disagrees.
	 */
	bool ReadFirstGradient(const TSharedPtr<IPropertyHandle>& InHandle, FDreamGradient& OutGradient, bool* bOutMultipleValues = nullptr)
	{
		if (bOutMultipleValues != nullptr)
		{
			*bOutMultipleValues = false;
		}
		if (!InHandle.IsValid() || !InHandle->IsValidHandle())
		{
			return false;
		}
		TArray<const void*> RawData;
		InHandle->AccessRawData(RawData);
		const FDreamGradient* First = nullptr;
		for (const void* Raw : RawData)
		{
			if (Raw == nullptr)
			{
				continue;
			}
			const FDreamGradient* Gradient = static_cast<const FDreamGradient*>(Raw);
			if (First == nullptr)
			{
				First = Gradient;
			}
			else if (*Gradient != *First)
			{
				if (bOutMultipleValues != nullptr)
				{
					*bOutMultipleValues = true;
				}
				break;
			}
		}
		if (First == nullptr)
		{
			return false;
		}
		OutGradient = *First;
		return true;
	}

	bool CanEdit(const TSharedRef<IPropertyHandle>& InHandle)
	{
		return InHandle->IsValidHandle() && InHandle->IsEditable();
	}

	/**
	 * Whether the rows that write the gradient without being its property rows -- the CSS box, the stop editor, the header's
	 * Paste -- may write it. A custom row is not greyed out with the gradient's own row: a child row inherits its parent's
	 * edit condition, not the details view's read-only verdict on it, and the view asks FIsCustomRowReadOnly with nothing
	 * but two names. So the designer's text-authoring gate is asked here what it answers for the gradient's own row: the same
	 * chain of properties (FPropertyAndParent's), for each object. Outside a text-authored widget it is always yes.
	 */
	bool IsWritableFromCustomRows(const TSharedRef<IPropertyHandle>& InHandle)
	{
		const FProperty* Leaf = InHandle->IsValidHandle() ? InHandle->GetProperty() : nullptr;
		if (Leaf == nullptr)
		{
			return false;
		}
		TArray<const FProperty*> Parents;
		for (TSharedPtr<IPropertyHandle> Parent = InHandle->GetParentHandle(); Parent.IsValid(); Parent = Parent->GetParentHandle())
		{
			if (const FProperty* ParentProperty = Parent->GetProperty())
			{
				Parents.Add(ParentProperty);
			}
		}
		TArray<UObject*> Outers;
		InHandle->GetOuterObjects(Outers);
		for (const UObject* Outer : Outers)
		{
			if (Outer != nullptr && DreamUITextAuthoring::IsPropertyReadOnly(Outer, Leaf, Parents))
			{
				return false;
			}
		}
		return true;
	}

	TAttribute<bool> MakeCustomRowEnabled(const TSharedRef<IPropertyHandle>& InHandle)
	{
		return TAttribute<bool>::CreateLambda([InHandle]() { return IsWritableFromCustomRows(InHandle); });
	}

	/** Every gradient InHandle edits as InEdit leaves it; whether any of them changed. */
	bool ComputeEdits(const TSharedRef<IPropertyHandle>& InHandle, TFunctionRef<void(FDreamGradient&)> InEdit, TArray<FDreamGradient>& OutValues)
	{
		TArray<const void*> RawData;
		InHandle->AccessRawData(RawData);
		OutValues.Reset(RawData.Num());
		bool bChanged = false;
		for (const void* Raw : RawData)
		{
			if (Raw == nullptr)
			{
				OutValues.AddDefaulted();
				continue;
			}
			const FDreamGradient& Current = *static_cast<const FDreamGradient*>(Raw);
			FDreamGradient& Edited = OutValues.Add_GetRef(Current);
			InEdit(Edited);
			bChanged |= Edited != Current;
		}
		return bChanged;
	}

	/**
	 * Writes InValues into InHandle's gradients the way the property system announces an edit: PreEditChange first (each
	 * object recorded into the open transaction, the notify hook's own snapshot), PostEditChange after. Directly rather
	 * than through the handle's text import, which prints floats with six decimals: every position, angle and scale of the
	 * gradient would move a little on each edit.
	 */
	void StoreEdits(const TSharedRef<IPropertyHandle>& InHandle, const TArray<FDreamGradient>& InValues, EPropertyChangeType::Type InChangeType)
	{
		InHandle->NotifyPreChange();
		// Asked again after the pre-change pass, which runs other code: the addresses are the property node's to give.
		TArray<void*> RawData;
		InHandle->AccessRawData(RawData);
		for (int32 Index = 0; Index < RawData.Num() && Index < InValues.Num(); ++Index)
		{
			if (RawData[Index] != nullptr)
			{
				*static_cast<FDreamGradient*>(RawData[Index]) = InValues[Index];
			}
		}
		InHandle->NotifyPostChange(InChangeType);
		if (InChangeType != EPropertyChangeType::Interactive)
		{
			InHandle->NotifyFinishedChangingProperties();
		}
	}

	/**
	 * The stop editor's writes. Each is one undo step -- except a drag: the editor writes every step of one as an
	 * interactive change, and the drag is a single step from its first write to the committed write that ends it. The step
	 * stays open across frames and closes there, as the property system's own interactive edits do; and the committed write
	 * goes out even when the drag's last step already wrote its value, because it is the one the details view's notify hook
	 * copies onto a designer template (it skips interactive changes). Owned by the stop curves, so by the editor widget.
	 */
	class FStopsWriter
	{
	public:
		explicit FStopsWriter(const TSharedRef<IPropertyHandle>& InHandle)
			: Handle(InHandle)
		{
		}

		~FStopsWriter()
		{
			CloseDrag();
		}

		void Write(const TArray<FDreamGradientStop>& InStops, bool bInInteractive)
		{
			if (!CanEdit(Handle))
			{
				CloseDrag();
				return;
			}
			TArray<FDreamGradient> NewValues;
			const bool bChanged = ComputeEdits(Handle, [&InStops](FDreamGradient& InOutGradient) { InOutGradient.Stops = InStops; }, NewValues);
			const bool bEndsDrag = bDragOpen && !bInInteractive;
			if (!bChanged && !bEndsDrag)
			{
				return;
			}
			if (bInInteractive && !bDragOpen && GEditor != nullptr)
			{
				GEditor->BeginTransaction(LOCTEXT("MoveGradientStop", "Move Gradient Stop"));
				bDragOpen = true;
			}
			// A step of its own, unless it belongs to a drag's.
			const FScopedTransaction Transaction(LOCTEXT("EditGradientStops", "Edit Gradient Stops"), /*bShouldActuallyTransact*/ !bInInteractive && !bDragOpen);
			StoreEdits(Handle, NewValues, bInInteractive ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet);
			if (!bInInteractive)
			{
				CloseDrag();
			}
		}

	private:
		void CloseDrag()
		{
			if (bDragOpen)
			{
				bDragOpen = false;
				if (GEditor != nullptr)
				{
					GEditor->EndTransaction();
				}
			}
		}

		TSharedRef<IPropertyHandle> Handle;
		bool bDragOpen = false;
	};

	void NotifyUser(const FText& InMessage, bool bInSucceeded)
	{
		FNotificationInfo Info(InMessage);
		Info.ExpireDuration = 4.0f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bInSucceeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	FText GetSummaryText(const TSharedRef<IPropertyHandle>& InHandle)
	{
		bool bMultipleValues = false;
		FDreamGradient Gradient;
		if (!ReadFirstGradient(InHandle, Gradient, &bMultipleValues))
		{
			return FText::GetEmpty();
		}
		return bMultipleValues ? LOCTEXT("MultipleValues", "Multiple Values") : FText::FromString(Gradient.ToCss());
	}

	void CopyCss(const TSharedRef<IPropertyHandle>& InHandle)
	{
		FDreamGradient Gradient;
		if (ReadFirstGradient(InHandle, Gradient))
		{
			FPlatformApplicationMisc::ClipboardCopy(*Gradient.ToCss());
		}
	}

	void PasteFromClipboard(const TSharedRef<IPropertyHandle>& InHandle)
	{
		FString Clipboard;
		FPlatformApplicationMisc::ClipboardPaste(Clipboard);
		Clipboard.TrimStartAndEndInline();
		FText Error;
		if (DreamGradientDetails::ApplyCss(InHandle, Clipboard, Error))
		{
			return;
		}
		// Not CSS: the property system's own spelling of a struct, which a plain paste of the row has always taken.
		if (Clipboard.StartsWith(TEXT("(")) && InHandle->SetValueFromFormattedString(Clipboard) == FPropertyAccess::Success)
		{
			return;
		}
		NotifyUser(FText::Format(LOCTEXT("PasteNotAGradient", "The clipboard does not hold a gradient: {0}"), Error), false);
	}

	void ApplyPreset(const TSharedRef<IPropertyHandle>& InHandle, const FDreamGradient& InPreset, const FText& InPresetName)
	{
		DreamGradientDetails::EditGradients(InHandle, FText::Format(LOCTEXT("ApplyPreset", "Apply Gradient Preset {0}"), InPresetName),
			[&InPreset](FDreamGradient& InOutGradient) { InOutGradient = InPreset; });
	}

	/** Where "Save as Gradient Asset" offers to put the asset: beside the asset being edited, else the project's content. */
	FString GetDefaultAssetPath(const TSharedRef<IPropertyHandle>& InHandle)
	{
		TArray<UObject*> Outers;
		InHandle->GetOuterObjects(Outers);
		for (const UObject* Outer : Outers)
		{
			const UPackage* Package = Outer != nullptr ? Outer->GetPackage() : nullptr;
			const FString PackageName = Package != nullptr ? Package->GetName() : FString();
			// A designer preview's package, or a transient object's, is no place for an asset.
			if (!PackageName.IsEmpty() && !PackageName.StartsWith(TEXT("/Temp/")) && !PackageName.StartsWith(TEXT("/Engine/Transient"))
				&& FPackageName::IsValidLongPackageName(PackageName))
			{
				return FPackageName::GetLongPackagePath(PackageName);
			}
		}
		return TEXT("/Game");
	}

	void SaveAsGradientAsset(const TSharedRef<IPropertyHandle>& InHandle)
	{
		FDreamGradient Gradient;
		if (!ReadFirstGradient(InHandle, Gradient))
		{
			return;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		// Held for the dialog: nothing else references a factory made here.
		const TStrongObjectPtr<UDreamGradientAssetFactory> Factory(NewObject<UDreamGradientAssetFactory>());
		Factory->InitialGradient = Gradient;
		const FString PackagePath = GetDefaultAssetPath(InHandle);
		FString UniquePackageName;
		FString UniqueAssetName;
		AssetTools.CreateUniqueAssetName(PackagePath / TEXT("DG_Gradient"), FString(), UniquePackageName, UniqueAssetName);
		if (const UObject* Created = AssetTools.CreateAssetWithDialog(UniqueAssetName, PackagePath, UDreamGradientAsset::StaticClass(), Factory.Get()))
		{
			NotifyUser(FText::Format(LOCTEXT("SavedGradientAsset", "Saved the gradient as {0}. Set a paint's Preset to it to keep following it as it changes."),
				FText::FromString(Created->GetPathName())), true);
		}
	}

	/** A menu entry's face: the preset's swatch and its name. */
	TSharedRef<SWidget> MakePresetEntryWidget(const FText& InLabel, const FDreamGradient& InPreset)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 1.0f, 6.0f, 1.0f)
			[
				SNew(SBox)
				.WidthOverride(56.0f)
				.HeightOverride(14.0f)
				[
					SNew(SDreamGradientPreview)
					.Gradient(InPreset)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InLabel)
			];
	}

	TSharedRef<SWidget> MakeProjectPresetNameBox(const TSharedRef<IPropertyHandle>& InHandle)
	{
		return SNew(SBox)
			.WidthOverride(240.0f)
			.Padding(FMargin(8.0f, 2.0f))
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("PresetNameHint", "Preset name, then Enter"))
				.ToolTipText(LOCTEXT("PresetName_ToolTip", "Keep this gradient in the project settings under this name, as CSS (Project Settings > Plugins > Dream GUI > Gradient Presets): a rich text's <gradient=Name> finds it, and this menu offers it. A preset of the same name is replaced."))
				.OnVerifyTextChanged_Lambda([](const FText& InText, FText& OutError)
				{
					return InText.IsEmpty() || DreamGradientDetails::IsValidPresetName(InText.ToString(), &OutError);
				})
				.OnTextCommitted_Lambda([InHandle](const FText& InText, ETextCommit::Type InCommitType)
				{
					FDreamGradient Gradient;
					if (InCommitType != ETextCommit::OnEnter || InText.IsEmpty() || !ReadFirstGradient(InHandle, Gradient))
					{
						return;
					}
					const FString Name = InText.ToString().TrimStartAndEnd();
					const bool bReplaces = UDreamGUISettings::Get()->GradientPresets.Contains(FName(*Name));
					FText Error;
					if (!DreamGradientDetails::SaveAsProjectPreset(Gradient, Name, Error))
					{
						NotifyUser(Error, false);
						return;
					}
					NotifyUser(FText::Format(bReplaces
						? LOCTEXT("ReplacedProjectPreset", "Replaced the project gradient preset {0}.")
						: LOCTEXT("SavedProjectPreset", "Saved the project gradient preset {0}."), FText::FromString(Name)), true);
					FSlateApplication::Get().DismissAllMenus();
				})
			];
	}

	TSharedRef<SWidget> MakePresetMenu(const TSharedRef<IPropertyHandle>& InHandle)
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);

		MenuBuilder.BeginSection(TEXT("DreamGradientProjectPresets"), LOCTEXT("ProjectPresets", "Project Presets"));
		{
			const TMap<FName, FString>& Presets = UDreamGUISettings::Get()->GradientPresets;
			TArray<FName> Names;
			Presets.GetKeys(Names);
			Names.Sort(FNameLexicalLess());
			if (Names.Num() == 0)
			{
				MenuBuilder.AddWidget(
					SNew(SBox)
					.Padding(FMargin(8.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoProjectPresets", "None yet: save one below, or add them in Project Settings > Plugins > Dream GUI."))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					],
					FText::GetEmpty(), true);
			}
			for (const FName Name : Names)
			{
				const FString& Css = Presets.FindChecked(Name);
				FDreamGradient Preset;
				FString Error;
				const bool bReads = FDreamGradient::ParseCss(Css, Preset, &Error);
				const FText Label = FText::FromName(Name);
				MenuBuilder.AddMenuEntry(
					FUIAction(
						FExecuteAction::CreateLambda([InHandle, Preset, Label]() { ApplyPreset(InHandle, Preset, Label); }),
						FCanExecuteAction::CreateLambda([InHandle, bReads]() { return bReads && CanEdit(InHandle); })),
					MakePresetEntryWidget(Label, Preset),
					NAME_None,
					bReads
						? FText::FromString(Css)
						: FText::Format(LOCTEXT("UnreadablePreset", "\"{0}\" does not read as a gradient: {1}"), FText::FromString(Css), FText::FromString(Error)));
			}
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection(TEXT("DreamGradientAssets"), LOCTEXT("GradientAssets", "Gradient Assets"));
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("PickGradientAsset", "Gradient Asset"),
				LOCTEXT("PickGradientAsset_ToolTip", "Copy the gradient of one of the project's Dream Gradient assets. To keep following an asset as it is edited, set the paint's Preset to it instead."),
				FNewMenuDelegate::CreateLambda([InHandle](FMenuBuilder& SubMenuBuilder)
				{
					const TArray<const UClass*> AllowedClasses{ UDreamGradientAsset::StaticClass() };
					SubMenuBuilder.AddWidget(
						PropertyCustomizationHelpers::MakeAssetPickerWithMenu(FAssetData(), /*AllowClear*/ false, AllowedClasses,
							TArray<UFactory*>(), FOnShouldFilterAsset(),
							FOnAssetSelected::CreateLambda([InHandle](const FAssetData& InAssetData)
							{
								if (const UDreamGradientAsset* Asset = Cast<UDreamGradientAsset>(InAssetData.GetAsset()))
								{
									ApplyPreset(InHandle, Asset->GetGradient(), FText::FromName(InAssetData.AssetName));
								}
							}),
							FSimpleDelegate::CreateLambda([]() { FSlateApplication::Get().DismissAllMenus(); })),
						FText::GetEmpty(), true);
				}));
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection(TEXT("DreamGradientSaveAsPreset"), LOCTEXT("SaveAsPreset", "Save As Preset"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("SaveAsGradientAsset", "Save as Gradient Asset..."),
				LOCTEXT("SaveAsGradientAsset_ToolTip", "Keep this gradient as a Dream Gradient asset, which paints can name as their Preset."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([InHandle]() { SaveAsGradientAsset(InHandle); }),
					FCanExecuteAction::CreateLambda([InHandle]()
					{
						FDreamGradient Gradient;
						return ReadFirstGradient(InHandle, Gradient);
					})));
			MenuBuilder.AddWidget(MakeProjectPresetNameBox(InHandle), LOCTEXT("SaveAsProjectPreset", "Save as Project Preset"));
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	/** Shown when a gradient of the selection uses the field: a selection that disagrees shows what any of it uses. */
	TAttribute<EVisibility> MakeFieldVisibility(const TSharedRef<IPropertyHandle>& InGradientHandle, FName InField)
	{
		return TAttribute<EVisibility>::CreateLambda([InGradientHandle, InField]()
		{
			if (!InGradientHandle->IsValidHandle())
			{
				return EVisibility::Collapsed;
			}
			TArray<const void*> RawData;
			InGradientHandle->AccessRawData(RawData);
			bool bAnyGradient = false;
			for (const void* Raw : RawData)
			{
				if (Raw == nullptr)
				{
					continue;
				}
				bAnyGradient = true;
				const FDreamGradient& Gradient = *static_cast<const FDreamGradient*>(Raw);
				if (DreamGradientDetails::IsFieldUsed(InField, Gradient.Type, Gradient.Size))
				{
					return EVisibility::Visible;
				}
			}
			return bAnyGradient ? EVisibility::Collapsed : EVisibility::Visible;
		});
	}

	void AddCssRow(IDetailChildrenBuilder& ChildBuilder, const TSharedRef<IPropertyHandle>& InHandle)
	{
		// The box's own commit reports a parse error on the box; it holds itself only weakly, through this.
		const TSharedRef<TWeakPtr<SEditableTextBox>> BoxHolder = MakeShared<TWeakPtr<SEditableTextBox>>();
		const TSharedRef<SEditableTextBox> Box = SNew(SEditableTextBox)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([InHandle]()
			{
				bool bMultipleValues = false;
				FDreamGradient Gradient;
				return ReadFirstGradient(InHandle, Gradient, &bMultipleValues) && !bMultipleValues ? FText::FromString(Gradient.ToCss()) : FText::GetEmpty();
			})
			.HintText_Lambda([InHandle]()
			{
				bool bMultipleValues = false;
				FDreamGradient Gradient;
				ReadFirstGradient(InHandle, Gradient, &bMultipleValues);
				return bMultipleValues ? LOCTEXT("MultipleValues", "Multiple Values") : LOCTEXT("CssHint", "linear-gradient(180deg, #FFF3B0, #9C6A12)");
			})
			.ToolTipText(LOCTEXT("Css_ToolTip", "The gradient as CSS: linear-, radial- and conic-gradient and their repeating- forms, with stops, angles, positions and `in oklab` or `in srgb-linear`; diamond-, corners- and reflecting- for what CSS has no word for. Type or paste one and press Enter. The same spelling a .dui file and a project preset use."))
			.SelectAllTextWhenFocused(true)
			.ClearKeyboardFocusOnCommit(false)
			.RevertTextOnEscape(true)
			.IsReadOnly_Lambda([InHandle]() { return !CanEdit(InHandle); })
			.OnTextCommitted_Lambda([InHandle, BoxHolder](const FText& InText, ETextCommit::Type InCommitType)
			{
				const TSharedPtr<SEditableTextBox> CommittedBox = BoxHolder->Pin();
				const FString Css = InText.ToString().TrimStartAndEnd();
				// Leaving an empty box (a selection that disagrees shows one) is not an edit.
				FText Error;
				const bool bApplied = InCommitType == ETextCommit::OnCleared || Css.IsEmpty() || DreamGradientDetails::ApplyCss(InHandle, Css, Error);
				if (CommittedBox.IsValid())
				{
					CommittedBox->SetError(bApplied ? FText::GetEmpty() : Error);
				}
			});
		*BoxHolder = Box;

		ChildBuilder.AddCustomRow(LOCTEXT("CssFilter", "CSS"))
		.RowTag(DreamGradientDetails::CssRowTag)
		.IsEnabled(MakeCustomRowEnabled(InHandle))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("Css", "CSS"))
			.ToolTipText(LOCTEXT("CssName_ToolTip", "The whole gradient in one line, to copy and paste."))
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(600.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Box
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(2.0f, 0.0f))
				.ToolTipText(LOCTEXT("CopyCss_ToolTip", "Copy the gradient as CSS."))
				.IsEnabled_Lambda([InHandle]()
				{
					bool bMultipleValues = false;
					FDreamGradient Gradient;
					return ReadFirstGradient(InHandle, Gradient, &bMultipleValues) && !bMultipleValues;
				})
				.OnClicked_Lambda([InHandle]()
				{
					CopyCss(InHandle);
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("GenericCommands.Copy"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(2.0f, 0.0f))
				.ToolTipText(LOCTEXT("PasteCss_ToolTip", "Paste a gradient written as CSS."))
				.IsEnabled_Lambda([InHandle]() { return CanEdit(InHandle); })
				.OnClicked_Lambda([InHandle]()
				{
					PasteFromClipboard(InHandle);
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("GenericCommands.Paste"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
	}

	void AddStopsRow(IDetailChildrenBuilder& ChildBuilder, const TSharedRef<IPropertyHandle>& InGradientHandle, const TSharedRef<IPropertyHandle>& InStopsHandle)
	{
		const TSharedRef<FStopsWriter> Writer = MakeShared<FStopsWriter>(InGradientHandle);
		FDreamGradientStopCurves::FBinding Binding;
		Binding.Read = [InGradientHandle](FDreamGradient& OutGradient) { return ReadFirstGradient(InGradientHandle, OutGradient); };
		Binding.Write = [Writer](const TArray<FDreamGradientStop>& InStops, bool bInInteractive) { Writer->Write(InStops, bInInteractive); };
		Binding.GetOwners = [InGradientHandle]()
		{
			TArray<UObject*> Outers;
			if (InGradientHandle->IsValidHandle())
			{
				InGradientHandle->GetOuterObjects(Outers);
			}
			return TArray<const UObject*>(Outers);
		};
		const TSharedRef<FDreamGradientStopCurves> StopCurves = MakeShared<FDreamGradientStopCurves>(MoveTemp(Binding));
		StopCurves->Refresh();

		const TSharedRef<SColorGradientEditor> GradientEditor = SNew(SColorGradientEditor)
			// The editor keeps a raw pointer to its curves: these attributes are what hold them, for as long as it lives.
			.ViewMinInput_Lambda([StopCurves]() { return StopCurves->GetViewMinInput(); })
			.ViewMaxInput_Lambda([StopCurves]() { return StopCurves->GetViewMaxInput(); })
			.IsEditingEnabled_Lambda([InGradientHandle]() { return CanEdit(InGradientHandle); })
			.ClampStopsToViewRange(true)
			.WithAlphaChannel(true)
			.DesiredSizeOverride(TOptional<FVector2D>(FVector2D(250.0, 64.0)));
		GradientEditor->SetCurveOwner(&StopCurves.Get());

		ChildBuilder.AddCustomRow(LOCTEXT("StopsFilter", "Stops"))
		.RowTag(DreamGradientDetails::StopsRowTag)
		.IsEnabled(MakeCustomRowEnabled(InGradientHandle))
		.NameContent()
		[
			InStopsHandle->CreatePropertyNameWidget(LOCTEXT("Stops", "Stops"),
				LOCTEXT("Stops_ToolTip", "Colour marks above, opacity marks below; each pair is one stop and moves as one. Click to add a stop, drag to move it, double-click to pick its colour, right-click to type its position or remove it."))
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(1000.0f)
		[
			GradientEditor
		];
	}
}

//------------------------------------------------------------------------------------------------------------------------
// FDreamGradientStopCurves

FDreamGradientStopCurves::FDreamGradientStopCurves(FBinding InBinding)
	: Binding(MoveTemp(InBinding))
{
	Strip = DreamGradientCustomizationLocal::MakeStrip(TArray<FDreamGradientStop>(), EDreamPaintInterpolation::SRGB);
}

void FDreamGradientStopCurves::Refresh()
{
	bSynced = false;
	SyncFromSource();
}

void FDreamGradientStopCurves::SyncFromSource() const
{
	FDreamGradient Source;
	if (!Binding.Read || !Binding.Read(Source))
	{
		return;
	}
	if (bSynced && Source.Stops == SyncedStops)
	{
		// The colour space is not in the keys: changing it only redraws.
		Strip.Interpolation = Source.Interpolation;
		return;
	}
	Rebuild(Source);
}

void FDreamGradientStopCurves::Rebuild(const FDreamGradient& InSource) const
{
	for (FRichCurve& Curve : Curves)
	{
		Curve.Reset();
	}
	Stops.Reset();
	float Effective = 0.0f;
	float LastKeyTime = 0.0f;
	for (int32 Index = 0; Index < InSource.Stops.Num(); ++Index)
	{
		const FDreamGradientStop& Stop = InSource.Stops[Index];
		// Where CSS puts it: never ahead of the stop before it.
		Effective = Index == 0 ? Stop.Position : FMath::Max(Stop.Position, Effective);
		const float KeyTime = Index == 0 ? Effective : FMath::Max(Effective, LastKeyTime + MinKeySpacing);
		const FLinearColor Linear(Stop.Color);
		FStopKeys& Keys = Stops.AddDefaulted_GetRef();
		Keys.Red = Curves[0].AddKey(KeyTime, Linear.R);
		Keys.Green = Curves[1].AddKey(KeyTime, Linear.G);
		Keys.Blue = Curves[2].AddKey(KeyTime, Linear.B);
		Keys.Alpha = Curves[3].AddKey(KeyTime, Linear.A);
		Keys.Position = Effective;
		Keys.KeyTime = KeyTime;
		Keys.Color = Stop.Color;
		Keys.KeyColor = Linear;
		LastKeyTime = KeyTime;
	}
	SyncedStops = InSource.Stops;
	Strip = DreamGradientCustomizationLocal::MakeStrip(InSource.Stops, InSource.Interpolation);
	bSynced = true;
	UpdateViewRange();
}

bool FDreamGradientStopCurves::IsHeld(FKeyHandle InHandle, int32 InChannel) const
{
	for (const FStopKeys& Keys : Stops)
	{
		const FKeyHandle Held = InChannel == 0 ? Keys.Red : InChannel == 1 ? Keys.Green : InChannel == 2 ? Keys.Blue : Keys.Alpha;
		if (Held == InHandle)
		{
			return true;
		}
	}
	return false;
}

FKeyHandle FDreamGradientStopCurves::FindLooseKey(int32 InChannel, float InTime) const
{
	const FRichCurve& Curve = Curves[InChannel];
	for (auto It = Curve.GetKeyHandleIterator(); It; ++It)
	{
		if (Curve.GetKeyTime(*It) == InTime && !IsHeld(*It, InChannel))
		{
			return *It;
		}
	}
	return FKeyHandle::Invalid();
}

void FDreamGradientStopCurves::Reconcile()
{
	using namespace DreamGradientCustomizationLocal;
	FRichCurve& RedCurve = Curves[0];
	FRichCurve& GreenCurve = Curves[1];
	FRichCurve& BlueCurve = Curves[2];
	FRichCurve& AlphaCurve = Curves[3];
	// The gradient as it was before this edit: what a new mark is coloured from.
	const FDreamGradient Before = Strip;
	const bool bHadStops = Before.Stops.Num() > 0;

	// A stop whose colour mark or alpha mark the editor deleted goes whole.
	for (int32 Index = Stops.Num() - 1; Index >= 0; --Index)
	{
		const FStopKeys& Keys = Stops[Index];
		const bool bHasColor = RedCurve.IsKeyHandleValid(Keys.Red) && GreenCurve.IsKeyHandleValid(Keys.Green) && BlueCurve.IsKeyHandleValid(Keys.Blue);
		if (bHasColor && AlphaCurve.IsKeyHandleValid(Keys.Alpha))
		{
			continue;
		}
		DeleteKeyIfValid(RedCurve, Keys.Red);
		DeleteKeyIfValid(GreenCurve, Keys.Green);
		DeleteKeyIfValid(BlueCurve, Keys.Blue);
		DeleteKeyIfValid(AlphaCurve, Keys.Alpha);
		Stops.RemoveAt(Index);
	}

	// Marks the editor added get their other half.
	TArray<FKeyHandle> NewColorKeys;
	for (auto It = RedCurve.GetKeyHandleIterator(); It; ++It)
	{
		if (!IsHeld(*It, 0))
		{
			NewColorKeys.Add(*It);
		}
	}
	TArray<FKeyHandle> NewAlphaKeys;
	for (auto It = AlphaCurve.GetKeyHandleIterator(); It; ++It)
	{
		if (!IsHeld(*It, 3))
		{
			NewAlphaKeys.Add(*It);
		}
	}
	for (const FKeyHandle RedHandle : NewColorKeys)
	{
		const float Time = RedCurve.GetKeyTime(RedHandle);
		const FKeyHandle GreenHandle = FindLooseKey(1, Time);
		const FKeyHandle BlueHandle = FindLooseKey(2, Time);
		if (!GreenCurve.IsKeyHandleValid(GreenHandle) || !BlueCurve.IsKeyHandleValid(BlueHandle))
		{
			RedCurve.DeleteKey(RedHandle);
			continue;
		}
		// No alpha key here yet, so the alpha curve gives the gradient's alpha here: alpha mixes on a straight line in
		// every colour space.
		FLinearColor Color(RedCurve.GetKeyValue(RedHandle), GreenCurve.GetKeyValue(GreenHandle), BlueCurve.GetKeyValue(BlueHandle), AlphaCurve.Eval(Time, 1.0f));
		// The editor colours a mark it adds with the curves' straight line between its neighbours -- linear light, not the
		// gradient's colour space -- unless it was told a colour (Ctrl+click white, Alt+click black). The straight line
		// becomes what the gradient shows there, so adding a stop changes nothing.
		if (bHadStops && IsStraightLineValue(RedCurve, RedHandle) && IsStraightLineValue(GreenCurve, GreenHandle) && IsStraightLineValue(BlueCurve, BlueHandle))
		{
			const FLinearColor Shown = EvaluateStrip(Before, Time);
			Color.R = Shown.R;
			Color.G = Shown.G;
			Color.B = Shown.B;
			RedCurve.SetKeyValue(RedHandle, Color.R);
			GreenCurve.SetKeyValue(GreenHandle, Color.G);
			BlueCurve.SetKeyValue(BlueHandle, Color.B);
		}
		FStopKeys& Keys = Stops.AddDefaulted_GetRef();
		Keys.Red = RedHandle;
		Keys.Green = GreenHandle;
		Keys.Blue = BlueHandle;
		Keys.Alpha = AlphaCurve.AddKey(Time, Color.A);
		Keys.Position = Time;
		Keys.KeyTime = Time;
		Keys.KeyColor = Color;
		Keys.Color = ToStopColor(Color);
	}
	for (const FKeyHandle AlphaHandle : NewAlphaKeys)
	{
		const float Time = AlphaCurve.GetKeyTime(AlphaHandle);
		// The colour the gradient shows here: a new alpha mark changes the alpha alone.
		FLinearColor Color = bHadStops ? EvaluateStrip(Before, Time) : FLinearColor::White;
		Color.A = AlphaCurve.GetKeyValue(AlphaHandle);
		FStopKeys& Keys = Stops.AddDefaulted_GetRef();
		Keys.Red = RedCurve.AddKey(Time, Color.R);
		Keys.Green = GreenCurve.AddKey(Time, Color.G);
		Keys.Blue = BlueCurve.AddKey(Time, Color.B);
		Keys.Alpha = AlphaHandle;
		Keys.Position = Time;
		Keys.KeyTime = Time;
		Keys.KeyColor = Color;
		Keys.Color = ToStopColor(Color);
	}
	// A key no stop holds is a stray an editor operation left behind.
	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		TArray<FKeyHandle> Loose;
		for (auto It = Curves[Channel].GetKeyHandleIterator(); It; ++It)
		{
			if (!IsHeld(*It, Channel))
			{
				Loose.Add(*It);
			}
		}
		for (const FKeyHandle Handle : Loose)
		{
			Curves[Channel].DeleteKey(Handle);
		}
	}

	// A stop the editor moved: the half it moved takes the other along. A colour or an alpha it changed is the stop's.
	for (FStopKeys& Keys : Stops)
	{
		const float ColorTime = RedCurve.GetKeyTime(Keys.Red);
		const float AlphaTime = AlphaCurve.GetKeyTime(Keys.Alpha);
		const float Time = ColorTime == Keys.KeyTime && AlphaTime != Keys.KeyTime ? AlphaTime : ColorTime;
		SetKeyTimeIfDifferent(RedCurve, Keys.Red, Time);
		SetKeyTimeIfDifferent(GreenCurve, Keys.Green, Time);
		SetKeyTimeIfDifferent(BlueCurve, Keys.Blue, Time);
		SetKeyTimeIfDifferent(AlphaCurve, Keys.Alpha, Time);
		if (Time != Keys.KeyTime)
		{
			Keys.Position = Time;
			Keys.KeyTime = Time;
		}
		const FLinearColor KeyColor(RedCurve.GetKeyValue(Keys.Red), GreenCurve.GetKeyValue(Keys.Green), BlueCurve.GetKeyValue(Keys.Blue), AlphaCurve.GetKeyValue(Keys.Alpha));
		if (KeyColor.R != Keys.KeyColor.R || KeyColor.G != Keys.KeyColor.G || KeyColor.B != Keys.KeyColor.B)
		{
			const FColor Encoded = ToStopColor(KeyColor);
			Keys.Color.R = Encoded.R;
			Keys.Color.G = Encoded.G;
			Keys.Color.B = Encoded.B;
		}
		if (KeyColor.A != Keys.KeyColor.A)
		{
			Keys.Color.A = ToStopColor(KeyColor).A;
		}
		Keys.KeyColor = KeyColor;
	}
	// Not in the middle of a drag: the dragged mark's keys must stay where the editor put them, or it lets go of the mark.
	if (!bNextChangeInteractive)
	{
		SeparateCoincidentKeys();
	}
}

void FDreamGradientStopCurves::SeparateCoincidentKeys()
{
	TArray<int32> Order;
	Order.Reserve(Stops.Num());
	for (int32 Index = 0; Index < Stops.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Order.StableSort([this](int32 A, int32 B) { return Stops[A].KeyTime < Stops[B].KeyTime; });
	for (int32 OrderIndex = 1; OrderIndex < Order.Num(); ++OrderIndex)
	{
		const float Earliest = Stops[Order[OrderIndex - 1]].KeyTime + MinKeySpacing;
		FStopKeys& Keys = Stops[Order[OrderIndex]];
		if (Keys.KeyTime >= Earliest)
		{
			continue;
		}
		// The keys only: the position the stop was given stays.
		Keys.KeyTime = Earliest;
		Curves[0].SetKeyTime(Keys.Red, Earliest);
		Curves[1].SetKeyTime(Keys.Green, Earliest);
		Curves[2].SetKeyTime(Keys.Blue, Earliest);
		Curves[3].SetKeyTime(Keys.Alpha, Earliest);
	}
}

void FDreamGradientStopCurves::CollectStops(TArray<FDreamGradientStop>& OutStops) const
{
	TArray<int32> Order;
	Order.Reserve(Stops.Num());
	for (int32 Index = 0; Index < Stops.Num(); ++Index)
	{
		Order.Add(Index);
	}
	// In the order they sit on the curves, which is their order along the gradient; the list's own order for a tie.
	Order.StableSort([this](int32 A, int32 B) { return Stops[A].KeyTime < Stops[B].KeyTime; });
	OutStops.Reset(Order.Num());
	for (const int32 Index : Order)
	{
		OutStops.Add(FDreamGradientStop(Stops[Index].Position, Stops[Index].Color));
	}
}

void FDreamGradientStopCurves::UpdateViewRange() const
{
	ViewMinInput = 0.0f;
	ViewMaxInput = 1.0f;
	for (const FStopKeys& Keys : Stops)
	{
		ViewMinInput = FMath::Min(ViewMinInput, Keys.KeyTime);
		ViewMaxInput = FMath::Max(ViewMaxInput, Keys.KeyTime);
	}
}

TArray<FRichCurveEditInfoConst> FDreamGradientStopCurves::GetCurves() const
{
	SyncFromSource();
	TArray<FRichCurveEditInfoConst> Result;
	Result.Reserve(4);
	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		Result.Add(FRichCurveEditInfoConst(&Curves[Channel], DreamGradientCustomizationLocal::GetChannelName(Channel)));
	}
	return Result;
}

void FDreamGradientStopCurves::GetCurves(TAdderReserverRef<FRichCurveEditInfoConst> OutCurves) const
{
	SyncFromSource();
	OutCurves.Reserve(4);
	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		OutCurves.Add(FRichCurveEditInfoConst(&Curves[Channel], DreamGradientCustomizationLocal::GetChannelName(Channel)));
	}
}

TArray<FRichCurveEditInfo> FDreamGradientStopCurves::GetCurves()
{
	SyncFromSource();
	TArray<FRichCurveEditInfo> Result;
	Result.Reserve(4);
	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		Result.Add(FRichCurveEditInfo(&Curves[Channel], DreamGradientCustomizationLocal::GetChannelName(Channel)));
	}
	return Result;
}

void FDreamGradientStopCurves::ModifyOwner()
{
	// Nothing to do here: the write after the edit announces it to the property system, which records the objects into
	// the transaction the editor opened before calling this.
}

TArray<const UObject*> FDreamGradientStopCurves::GetOwners() const
{
	return Binding.GetOwners ? Binding.GetOwners() : TArray<const UObject*>();
}

void FDreamGradientStopCurves::MakeTransactional()
{
}

void FDreamGradientStopCurves::OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos)
{
	Reconcile();
	TArray<FDreamGradientStop> NewStops;
	CollectStops(NewStops);
	// What the source holds once the write below lands, so the next look does not take the edit for someone else's. A
	// write that does not land (the row went read-only) leaves the source different, and the curves are rebuilt from it.
	SyncedStops = NewStops;
	Strip.Stops = NewStops;
	bSynced = true;
	if (!bNextChangeInteractive)
	{
		UpdateViewRange();
	}
	if (Binding.Write)
	{
		Binding.Write(NewStops, bNextChangeInteractive);
	}
}

void FDreamGradientStopCurves::SetOnCurveChangedIsInteractive(bool bInIsInteractive)
{
	bNextChangeInteractive = bInIsInteractive;
}

FLinearColor FDreamGradientStopCurves::GetLinearColorValue(float InTime) const
{
	// The editor colours each mark from the colour at its time: a mark's own keys answer there, either of a hard edge's
	// two included.
	for (const FStopKeys& Keys : Stops)
	{
		if (Keys.KeyTime == InTime)
		{
			return FLinearColor(Curves[0].GetKeyValue(Keys.Red), Curves[1].GetKeyValue(Keys.Green), Curves[2].GetKeyValue(Keys.Blue), Curves[3].GetKeyValue(Keys.Alpha));
		}
	}
	// Everywhere else the gradient as its stops paint it, in their colour space.
	return DreamGradientCustomizationLocal::EvaluateStrip(Strip, InTime);
}

bool FDreamGradientStopCurves::HasAnyAlphaKeys() const
{
	// The editor asks this first when it paints: the look at the source that catches an undo or a paste before it draws.
	SyncFromSource();
	return Curves[3].GetNumKeys() > 0;
}

bool FDreamGradientStopCurves::IsValidCurve(FRichCurveEditInfo CurveInfo)
{
	for (const FRichCurve& Curve : Curves)
	{
		if (CurveInfo.CurveToEdit == &Curve)
		{
			return true;
		}
	}
	return false;
}

//------------------------------------------------------------------------------------------------------------------------
// FDreamGradientCustomization

TSharedRef<IPropertyTypeCustomization> FDreamGradientCustomization::MakeInstance()
{
	return MakeShareable(new FDreamGradientCustomization());
}

void FDreamGradientCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace DreamGradientCustomizationLocal;
	if (!IsGradientHandle(PropertyHandle))
	{
		HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			PropertyHandle->CreatePropertyValueWidget()
		];
		return;
	}

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	.MaxDesiredWidth(600.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 2.0f, 4.0f, 2.0f)
		[
			SNew(SDreamGradientPreview)
			.DesiredSize(FVector2D(160.0, 18.0))
			.Gradient_Lambda([PropertyHandle]()
			{
				FDreamGradient Gradient;
				ReadFirstGradient(PropertyHandle, Gradient);
				return Gradient;
			})
			.ToolTipText_Lambda([PropertyHandle]() { return GetSummaryText(PropertyHandle); })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
			.HasDownArrow(true)
			.ContentPadding(FMargin(2.0f, 0.0f))
			.ToolTipText(LOCTEXT("Presets_ToolTip", "Paint with a preset -- one of the project's gradient assets, or of its presets in Project Settings > Plugins > Dream GUI -- or keep this gradient as one."))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("Presets", "Presets"))
			]
			.OnGetMenuContent_Lambda([PropertyHandle]() { return MakePresetMenu(PropertyHandle); })
		]
	]
	// The row's Copy and Paste speak CSS, the spelling a .dui and a project preset take; a paste of the property system's
	// own struct text still reads.
	.CopyAction(FUIAction(
		FExecuteAction::CreateLambda([PropertyHandle]() { CopyCss(PropertyHandle); }),
		FCanExecuteAction::CreateLambda([PropertyHandle]()
		{
			bool bMultipleValues = false;
			FDreamGradient Gradient;
			return ReadFirstGradient(PropertyHandle, Gradient, &bMultipleValues) && !bMultipleValues;
		})))
	.PasteAction(FUIAction(
		FExecuteAction::CreateLambda([PropertyHandle]() { PasteFromClipboard(PropertyHandle); }),
		FCanExecuteAction::CreateLambda([PropertyHandle]() { return CanEdit(PropertyHandle) && IsWritableFromCustomRows(PropertyHandle); })));
}

void FDreamGradientCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace DreamGradientCustomizationLocal;
	// The fields in their declaration order.
	TArray<TPair<FName, TSharedRef<IPropertyHandle>>> Fields;
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(ChildIndex);
		if (Child.IsValid() && Child->GetProperty() != nullptr)
		{
			Fields.Emplace(Child->GetProperty()->GetFName(), Child.ToSharedRef());
		}
	}
	const auto FindField = [&Fields](FName InName) -> TSharedPtr<IPropertyHandle>
	{
		for (const TPair<FName, TSharedRef<IPropertyHandle>>& Field : Fields)
		{
			if (Field.Key == InName)
			{
				return Field.Value;
			}
		}
		return nullptr;
	};
	const FName TypeName = GET_MEMBER_NAME_CHECKED(FDreamGradient, Type);
	const FName StopsName = GET_MEMBER_NAME_CHECKED(FDreamGradient, Stops);
	const FName InterpolationName = GET_MEMBER_NAME_CHECKED(FDreamGradient, Interpolation);
	const TSharedPtr<IPropertyHandle> TypeHandle = FindField(TypeName);
	const TSharedPtr<IPropertyHandle> StopsHandle = FindField(StopsName);
	const TSharedPtr<IPropertyHandle> InterpolationHandle = FindField(InterpolationName);
	if (!IsGradientHandle(PropertyHandle) || !TypeHandle.IsValid() || !StopsHandle.IsValid() || !InterpolationHandle.IsValid())
	{
		// Whatever the struct has, as the default layout would show it.
		for (const TPair<FName, TSharedRef<IPropertyHandle>>& Field : Fields)
		{
			ChildBuilder.AddProperty(Field.Value);
		}
		return;
	}

	AddCssRow(ChildBuilder, PropertyHandle);
	ChildBuilder.AddProperty(TypeHandle.ToSharedRef());
	AddStopsRow(ChildBuilder, PropertyHandle, StopsHandle.ToSharedRef());
	// The same stops as a list, for typing exact positions and colours.
	ChildBuilder.AddProperty(StopsHandle.ToSharedRef())
		.DisplayName(LOCTEXT("StopList", "Stop List"))
		.ToolTip(LOCTEXT("StopList_ToolTip", "The stops one by one: positions as fractions of the gradient's length, colours sRGB with straight alpha. The first 16 are drawn."));
	ChildBuilder.AddProperty(InterpolationHandle.ToSharedRef()).Visibility(MakeFieldVisibility(PropertyHandle, InterpolationName));
	for (const TPair<FName, TSharedRef<IPropertyHandle>>& Field : Fields)
	{
		if (Field.Key != TypeName && Field.Key != StopsName && Field.Key != InterpolationName)
		{
			ChildBuilder.AddProperty(Field.Value).Visibility(MakeFieldVisibility(PropertyHandle, Field.Key));
		}
	}
}

//------------------------------------------------------------------------------------------------------------------------
// DreamGradientDetails

bool DreamGradientDetails::IsFieldUsed(FName InField, EDreamPaintType InType, EDreamPaintRadialSize InSize)
{
	const bool bLinear = InType == EDreamPaintType::Linear;
	const bool bRadial = InType == EDreamPaintType::Radial;
	const bool bConic = InType == EDreamPaintType::Conic;
	const bool bDiamond = InType == EDreamPaintType::Diamond;
	if (InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Type) || InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Stops))
	{
		// Kept in sight whatever the type: a type that paints nothing still holds its stops.
		return true;
	}
	if (InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Interpolation))
	{
		return InType != EDreamPaintType::None;
	}
	if (InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Angle))
	{
		// A Linear's direction, a Conic's start, a Diamond's turn.
		return bLinear || bConic || bDiamond;
	}
	if (InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Center))
	{
		// A Linear runs through the box's middle.
		return bRadial || bConic || bDiamond;
	}
	if (InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Shape))
	{
		// A Diamond takes the ellipse's radii whatever its shape.
		return bRadial;
	}
	if (InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Size))
	{
		return bRadial || bDiamond;
	}
	if (InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Radius))
	{
		return (bRadial || bDiamond) && InSize == EDreamPaintRadialSize::Explicit;
	}
	if (InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Spread)
		|| InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Scale)
		|| InField == GET_MEMBER_NAME_CHECKED(FDreamGradient, Offset))
	{
		// Along the gradient's line: Corners has none.
		return bLinear || bRadial || bConic || bDiamond;
	}
	// A field this does not know is shown.
	return true;
}

bool DreamGradientDetails::EditGradients(const TSharedRef<IPropertyHandle>& InGradientHandle, const FText& InDescription, TFunctionRef<void(FDreamGradient&)> InEdit)
{
	using namespace DreamGradientCustomizationLocal;
	if (!CanEdit(InGradientHandle) || !IsGradientHandle(InGradientHandle))
	{
		return false;
	}
	TArray<FDreamGradient> NewValues;
	if (!ComputeEdits(InGradientHandle, InEdit, NewValues))
	{
		return false;
	}
	const FScopedTransaction Transaction(InDescription);
	StoreEdits(InGradientHandle, NewValues, EPropertyChangeType::ValueSet);
	return true;
}

bool DreamGradientDetails::ApplyCss(const TSharedRef<IPropertyHandle>& InGradientHandle, const FString& InCss, FText& OutError)
{
	FDreamGradient Parsed;
	FString Error;
	if (!FDreamGradient::ParseCss(InCss.TrimStartAndEnd(), Parsed, &Error))
	{
		OutError = Error.IsEmpty() ? LOCTEXT("CssUnreadable", "This is not a gradient CSS can write.") : FText::FromString(Error);
		return false;
	}
	EditGradients(InGradientHandle, LOCTEXT("SetGradientFromCss", "Set Gradient from CSS"), [&Parsed](FDreamGradient& InOutGradient) { InOutGradient = Parsed; });
	return true;
}

bool DreamGradientDetails::IsValidPresetName(const FString& InName, FText* OutError)
{
	FText Error;
	if (InName.IsEmpty())
	{
		Error = LOCTEXT("PresetNameEmpty", "A preset needs a name.");
	}
	else if (InName.Len() >= NAME_SIZE)
	{
		Error = LOCTEXT("PresetNameTooLong", "That name is too long.");
	}
	else
	{
		for (const TCHAR Character : InName)
		{
			if (FChar::IsWhitespace(Character) || Character == TEXT('<') || Character == TEXT('>') || Character == TEXT('=')
				|| Character == TEXT('/') || Character == TEXT('"'))
			{
				Error = LOCTEXT("PresetNameCharacters", "A preset's name is what <gradient=Name> says: no spaces, and none of < > = / \".");
				break;
			}
		}
	}
	if (OutError != nullptr)
	{
		*OutError = Error;
	}
	return Error.IsEmpty();
}

bool DreamGradientDetails::SaveAsProjectPreset(const FDreamGradient& InGradient, const FString& InName, FText& OutError)
{
	const FString Name = InName.TrimStartAndEnd();
	if (!IsValidPresetName(Name, &OutError))
	{
		return false;
	}
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	FProperty* PresetsProperty = FindFProperty<FProperty>(UDreamGUISettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UDreamGUISettings, GradientPresets));
	if (Settings == nullptr || PresetsProperty == nullptr)
	{
		OutError = LOCTEXT("NoPresetSettings", "The project settings have no gradient presets.");
		return false;
	}
	// The way the Project Settings page writes it, so whatever keeps the presets read hears of the change.
	Settings->PreEditChange(PresetsProperty);
	Settings->GradientPresets.Add(FName(*Name), InGradient.ToCss());
	FPropertyChangedEvent ChangedEvent(PresetsProperty, EPropertyChangeType::ValueSet);
	Settings->PostEditChangeProperty(ChangedEvent);
	if (!Settings->TryUpdateDefaultConfigFile())
	{
		OutError = LOCTEXT("PresetNotSaved", "The preset is in use, but the project's DreamGUI config file could not be written: it will be gone next session.");
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
