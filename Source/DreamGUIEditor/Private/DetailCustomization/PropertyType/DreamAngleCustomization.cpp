// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/PropertyType/DreamAngleCustomization.h"

#include "DetailCustomization/DreamAngleGlobalLight.h"
#include "DetailCustomization/DreamPropertyMeta.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "PropertyHandle.h"
#include "Widget/SDreamAngleWheel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamAngleCustomization"

namespace DreamAngleCustomizationLocal
{
	/** DreamGUI_Editor="AngleWheel", with the convention after a colon: "AngleWheel:TopCW". */
	const FName EditorName(TEXT("AngleWheel"));
	const FName UseGlobalName(TEXT("bUseGlobalLight"));
	const FName GlobalAngleName(TEXT("GlobalLightAngle"));

	TArray<TPair<int32, DreamAngleGlobalLight::FResolver>>& Resolvers()
	{
		static TArray<TPair<int32, DreamAngleGlobalLight::FResolver>> List;
		return List;
	}

	/** The bUseGlobalLight of the effect the angle is part of, when it has one. */
	const FBoolProperty* UseGlobalOf(const TSharedRef<IPropertyHandle>& InAngle)
	{
		const FProperty* Property = InAngle->GetProperty();
		const UStruct* Effect = Property != nullptr ? Property->GetOwnerStruct() : nullptr;
		return Effect != nullptr ? FindFProperty<FBoolProperty>(Effect, UseGlobalName) : nullptr;
	}
}

bool FDreamAngleWheelIdentifier::IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const
{
	return DreamPropertyMeta::UsesEditor(PropertyHandle.GetProperty(), DreamAngleCustomizationLocal::EditorName);
}

TSharedRef<IPropertyTypeCustomization> FDreamAngleCustomization::MakeInstance()
{
	return MakeShareable(new FDreamAngleCustomization);
}

TSharedPtr<IPropertyHandle> FDreamAngleCustomization::FindGlobalAngle(const TSharedRef<IPropertyHandle>& InAngle)
{
	using namespace DreamAngleCustomizationLocal;
	// Only an effect that can use the global light has one to turn: one with a bUseGlobalLight beside its angle.
	if (UseGlobalOf(InAngle) == nullptr)
	{
		return nullptr;
	}
	// The effect, maybe an element of an array of them, inside the style that holds the global light.
	const TSharedPtr<IPropertyHandle> Parent = InAngle->GetParentHandle();
	TSharedPtr<IPropertyHandle> Up = Parent.IsValid() ? Parent->GetParentHandle() : nullptr;
	for (int32 Depth = 0; Up.IsValid() && Depth < 4; ++Depth)
	{
		if (const TSharedPtr<IPropertyHandle> Found = Up->GetChildHandle(GlobalAngleName); Found.IsValid() && Found->IsValidHandle())
		{
			return Found;
		}
		Up = Up->GetParentHandle();
	}
	// An effect shown apart from its style: whoever shows it knows where the global light is.
	for (const TPair<int32, DreamAngleGlobalLight::FResolver>& Resolver : Resolvers())
	{
		if (const TSharedPtr<IPropertyHandle> Found = Resolver.Value ? Resolver.Value(InAngle) : nullptr; Found.IsValid() && Found->IsValidHandle())
		{
			return Found;
		}
	}
	return nullptr;
}

int32 DreamAngleGlobalLight::AddResolver(FResolver InResolver)
{
	static int32 NextId = 0;
	const int32 Id = ++NextId;
	DreamAngleCustomizationLocal::Resolvers().Emplace(Id, MoveTemp(InResolver));
	return Id;
}

void DreamAngleGlobalLight::RemoveResolver(int32 InId)
{
	DreamAngleCustomizationLocal::Resolvers().RemoveAll([InId](const TPair<int32, FResolver>& InEntry) { return InEntry.Key == InId; });
}

TSharedPtr<IPropertyHandle> DreamAngleGlobalLight::Find(const TSharedRef<IPropertyHandle>& InAngle)
{
	return FDreamAngleCustomization::FindGlobalAngle(InAngle);
}

bool FDreamAngleCustomization::UsesGlobalLight(const TSharedRef<IPropertyHandle>& InAngle)
{
	// Read beside the angle, in the effect the angle is part of, rather than through a sibling handle: an effect that is
	// the root of the view (a structure details view) has no parent handle to ask for one.
	const FBoolProperty* UseGlobal = DreamAngleCustomizationLocal::UseGlobalOf(InAngle);
	const FProperty* Property = InAngle->GetProperty();
	TArray<void*> Raw;
	InAngle->AccessRawData(Raw);
	if (UseGlobal == nullptr || Property == nullptr || Raw.Num() == 0)
	{
		return false;
	}
	for (void* Value : Raw)
	{
		const uint8* Effect = Value != nullptr ? static_cast<const uint8*>(Value) - Property->GetOffset_ForInternal() : nullptr;
		if (Effect == nullptr || !UseGlobal->GetPropertyValue_InContainer(Effect))
		{
			return false;
		}
	}
	return true;
}

TSharedPtr<IPropertyHandle> FDreamAngleCustomization::Target() const
{
	if (Global.IsValid() && Angle.IsValid() && Angle->IsValidHandle() && UsesGlobalLight(Angle.ToSharedRef()))
	{
		return Global;
	}
	return Angle;
}

TOptional<float> FDreamAngleCustomization::GetValue() const
{
	const TSharedPtr<IPropertyHandle> Handle = Target();
	float Value = 0.0f;
	if (Handle.IsValid() && Handle->IsValidHandle() && Handle->GetValue(Value) == FPropertyAccess::Success)
	{
		return Value;
	}
	return {};
}

void FDreamAngleCustomization::BeginDrag()
{
	if (!bDragging && GEditor != nullptr)
	{
		bDragging = true;
		GEditor->BeginTransaction(LOCTEXT("TurnAngle", "Turn Angle"));
	}
}

void FDreamAngleCustomization::Drag(float InDegrees)
{
	if (const TSharedPtr<IPropertyHandle> Handle = Target(); Handle.IsValid() && Handle->IsValidHandle())
	{
		Handle->SetValue(InDegrees, EPropertyValueSetFlags::InteractiveChange);
	}
}

void FDreamAngleCustomization::EndDrag(float InDegrees)
{
	if (const TSharedPtr<IPropertyHandle> Handle = Target(); Handle.IsValid() && Handle->IsValidHandle())
	{
		// The committed value: what a designer carries to its template and writes to the .dui.
		Handle->SetValue(InDegrees);
	}
	if (bDragging && GEditor != nullptr)
	{
		GEditor->EndTransaction();
	}
	bDragging = false;
}

void FDreamAngleCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace DreamAngleCustomizationLocal;
	Angle = PropertyHandle;
	Global = FindGlobalAngle(PropertyHandle);
	FString ConventionName;
	DreamPropertyMeta::UsesEditor(PropertyHandle->GetProperty(), EditorName, &ConventionName);
	const EDreamAngleConvention Convention = DreamAngleWheel::ParseConvention(ConventionName);
	const bool bSigned = !PropertyHandle->HasMetaData(TEXT("UIMin")) || FCString::Atof(*PropertyHandle->GetMetaData(TEXT("UIMin"))) < 0.0f;
	const TWeakPtr<FDreamAngleCustomization> Weak = StaticCastSharedRef<FDreamAngleCustomization>(AsShared());
	auto UsingGlobal = [Weak]()
	{
		const TSharedPtr<FDreamAngleCustomization> Self = Weak.Pin();
		return Self.IsValid() && Self->Global.IsValid() && Self->Angle.IsValid() && Self->Angle->IsValidHandle() && UsesGlobalLight(Self->Angle.ToSharedRef());
	};

	TSharedRef<SHorizontalBox> Value = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 2.0f, 6.0f, 2.0f)
		[
			SNew(SDreamAngleWheel)
			.Convention(Convention)
			.bSigned(bSigned)
			.Angle_Lambda([Weak]() { const TSharedPtr<FDreamAngleCustomization> Self = Weak.Pin(); return Self.IsValid() ? Self->GetValue() : TOptional<float>(); })
			.OnBeginDrag_Lambda([Weak]() { if (const TSharedPtr<FDreamAngleCustomization> Self = Weak.Pin()) { Self->BeginDrag(); } })
			.OnAngleChanged_Lambda([Weak](float InDegrees) { if (const TSharedPtr<FDreamAngleCustomization> Self = Weak.Pin()) { Self->Drag(InDegrees); } })
			.OnEndDrag_Lambda([Weak](float InDegrees) { if (const TSharedPtr<FDreamAngleCustomization> Self = Weak.Pin()) { Self->EndDrag(InDegrees); } })
			.ToolTipText(LOCTEXT("WheelTip", "Drag to turn; Shift snaps to 15 degrees."))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.Visibility_Lambda([UsingGlobal]() { return UsingGlobal() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				PropertyHandle->CreatePropertyValueWidget(/*bDisplayDefaultPropertyButtons*/false)
			]
		];
	if (Global.IsValid())
	{
		Value->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.Visibility_Lambda([UsingGlobal]() { return UsingGlobal() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Global->CreatePropertyValueWidget(/*bDisplayDefaultPropertyButtons*/false)
			]
		];
		Value->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("GlobalLight", "Global"))
			.ToolTipText(LOCTEXT("GlobalLightTip", "Use Global Light is on: this turns the style's global light, which every effect using it shares."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.Visibility_Lambda([UsingGlobal]() { return UsingGlobal() ? EVisibility::Visible : EVisibility::Collapsed; })
		];
	}
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(160.0f)
		[
			Value
		];
}

#undef LOCTEXT_NAMESPACE
