// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "ComponentTransformDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Textures/SlateIcon.h"
#include "EditorStyleSet.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "UObject/UnrealType.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/ConfigCacheIni.h"
#include "SlateOptMacros.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Editor/UnrealEdEngine.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "ScopedTransaction.h"
#include "IPropertyUtilities.h"
#include "Math/UnitConversion.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Settings/EditorProjectSettings.h"
#include "HAL/PlatformApplicationMisc.h"

#include "Core/Components/DreamWidget.h"

#include "DetailCategoryBuilder.h"
#include "Algo/Transform.h"

#define LOCTEXT_NAMESPACE "DreamGUIComponentTransformDetails"

class FScopedSwitchWorldForObject
{
public:
	FScopedSwitchWorldForObject( UObject* Object )
		: PrevWorld( NULL )
	{
		bool bRequiresPlayWorld = false;
		if( GUnrealEd->PlayWorld && !GIsPlayInEditorWorld )
		{
			UPackage* ObjectPackage = Object->GetOutermost();
			bRequiresPlayWorld = ObjectPackage->HasAnyPackageFlags(PKG_PlayInEditor);
		}

		if( bRequiresPlayWorld )
		{
			PrevWorld = SetPlayInEditorWorld( GUnrealEd->PlayWorld );
		}
	}

	~FScopedSwitchWorldForObject()
	{
		if( PrevWorld )
		{
			RestoreEditorWorld( PrevWorld );
		}
	}

private:
	UWorld* PrevWorld;
};

static USceneComponent* GetSceneComponentFromDetailsObject(UObject* InObject)
{
	AActor* Actor = Cast<AActor>(InObject);
	if (Actor)
	{
		return Actor->GetRootComponent();
	}

	return Cast<USceneComponent>(InObject);
}

FComponentTransformDetails::FComponentTransformDetails( const TArray< TWeakObjectPtr<UDreamWidget> >& InSelectedObjects, const FSelectedActorInfo& InSelectedActorInfo, IDetailLayoutBuilder& DetailBuilder )
	: FComponentTransformDetails( InSelectedObjects, InSelectedActorInfo, DetailBuilder.GetPropertyUtilities()->GetNotifyHook() )
{
}

FComponentTransformDetails::FComponentTransformDetails( const TArray< TWeakObjectPtr<UDreamWidget> >& InSelectedObjects, const FSelectedActorInfo& InSelectedActorInfo, FNotifyHook* InNotifyHook )
	: TNumericUnitTypeInterface(GetDefault<UEditorProjectAppearanceSettings>()->bDisplayUnitsOnComponentTransforms ? EUnit::Centimeters : EUnit::Unspecified)
	, SelectedActorInfo( InSelectedActorInfo )
	, SelectedObjects( InSelectedObjects )
	, NotifyHook( InNotifyHook )
	, bPreserveScaleRatio( false )
	, bEditingRotationInUI( false )
	//was left uninitialised, and OnSetTransform reads it to decide whether a committed slider counts
	, bIsSliderTransaction( false )
{
	GConfig->GetBool(TEXT("SelectionDetails"), TEXT("PreserveScaleRatio"), bPreserveScaleRatio, GEditorPerProjectIni);

}

TSharedRef<SWidget> FComponentTransformDetails::BuildTransformFieldLabel( ETransformField::Type TransformField )
{
	FText Label;
	switch( TransformField )
	{
	case ETransformField::Rotation:
		Label = LOCTEXT( "RotationLabel", "Rotation");
		break;
	case ETransformField::Scale:
		Label = LOCTEXT( "ScaleLabel", "Scale" );
		break;
	case ETransformField::Location:
	default:
		Label = LOCTEXT("LocationLabel", "Location");
		break;
	}


	TSharedRef<SWidget> NameContent =
		SNew(STextBlock)
		.Text(Label)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		;

	if(TransformField == ETransformField::Scale)
	{
		NameContent =
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				NameContent
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			[
				// Add a checkbox to toggle between preserving the ratio of x,y,z components of scale when a value is entered
				SNew(SCheckBox)
				.IsChecked(this, &FComponentTransformDetails::IsPreserveScaleRatioChecked)
				.IsEnabled(this, &FComponentTransformDetails::GetIsEnabled)
				.OnCheckStateChanged(this, &FComponentTransformDetails::OnPreserveScaleRatioToggled)
				.Style(FAppStyle::Get(), "TransparentCheckBox")
				.ToolTipText(LOCTEXT("PreserveScaleToolTip", "When locked, scales uniformly based on the current xyz scale values so the object maintains its shape in each direction when scaled"))
				[
					SNew(SImage)
					.Image(this, &FComponentTransformDetails::GetPreserveScaleRatioImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	}

	return NameContent;
}
bool FComponentTransformDetails::OnCanCopy( ETransformField::Type TransformField ) const
{
	// We can only copy values if the whole field is set.  If multiple values are defined we do not copy since we are unable to determine the value
	switch (TransformField)
	{
	case ETransformField::Location:
		return CachedLocation.IsSet();
		break;
	case ETransformField::Rotation:
		return CachedRotation.IsSet();
		break;
	case ETransformField::Scale:
		return CachedScale.IsSet();
		break;
	default:
		return false;
		break;
	}
}

void FComponentTransformDetails::OnCopy( ETransformField::Type TransformField )
{
	CacheTransform();

	FString CopyStr;
	switch (TransformField)
	{
	case ETransformField::Location:
		CopyStr = FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), CachedLocation.X.GetValue(), CachedLocation.Y.GetValue(), CachedLocation.Z.GetValue());
		break;
	case ETransformField::Rotation:
		CopyStr = FString::Printf(TEXT("(Pitch=%f,Yaw=%f,Roll=%f)"), CachedRotation.Y.GetValue(), CachedRotation.Z.GetValue(), CachedRotation.X.GetValue());
		break;
	case ETransformField::Scale:
		CopyStr = FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), CachedScale.X.GetValue(), CachedScale.Y.GetValue(), CachedScale.Z.GetValue());
		break;
	default:
		break;
	}

	if( !CopyStr.IsEmpty() )
	{
		FPlatformApplicationMisc::ClipboardCopy( *CopyStr );
	}
}

void FComponentTransformDetails::OnPaste( ETransformField::Type TransformField )
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);

	switch (TransformField)
	{
		case ETransformField::Location:
		{
			FVector Location;
			if (Location.InitFromString(PastedText))
			{
				FScopedTransaction Transaction(LOCTEXT("PasteLocation", "Paste Location"));
				OnSetTransform(ETransformField::Location, EAxisList::All, Location, true);
			}
		}
		break;
	case ETransformField::Rotation:
		{
			FRotator Rotation;
			PastedText.ReplaceInline(TEXT("Pitch="), TEXT("P="));
			PastedText.ReplaceInline(TEXT("Yaw="), TEXT("Y="));
			PastedText.ReplaceInline(TEXT("Roll="), TEXT("R="));
			if (Rotation.InitFromString(PastedText))
			{
				FScopedTransaction Transaction(LOCTEXT("PasteRotation", "Paste Rotation"));
				OnSetTransform(ETransformField::Rotation, EAxisList::All, Rotation.Euler(), true);
			}
		}
		break;
	case ETransformField::Scale:
		{
			FVector Scale;
			if (Scale.InitFromString(PastedText))
			{
				FScopedTransaction Transaction(LOCTEXT("PasteScale", "Paste Scale"));
				OnSetTransform(ETransformField::Scale, EAxisList::All, Scale, true);
			}
		}
		break;
	default:
		break;
	}
}

FUIAction FComponentTransformDetails::CreateCopyAction( ETransformField::Type TransformField ) 
{
	return
		FUIAction
		(
			FExecuteAction::CreateSP(const_cast<FComponentTransformDetails*>(this), &FComponentTransformDetails::OnCopy, TransformField ),
			FCanExecuteAction::CreateSP(const_cast<FComponentTransformDetails*>(this), &FComponentTransformDetails::OnCanCopy, TransformField )
		);
}

FUIAction FComponentTransformDetails::CreatePasteAction( ETransformField::Type TransformField ) 
{
	return 
		 FUIAction( FExecuteAction::CreateSP(const_cast<FComponentTransformDetails*>(this), &FComponentTransformDetails::OnPaste, TransformField ) );
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FComponentTransformDetails::GenerateChildContent( IDetailChildrenBuilder& ChildrenBuilder )
{
	if (SelectedObjects.Num() <= 0)return;
	const auto* Archetype = SelectedObjects[0].Get();
	if (!IsValid(Archetype))return;

	FSlateFontInfo FontInfo = IDetailLayoutBuilder::GetDetailFont();

	// Location
	{
		TSharedPtr<INumericTypeInterface<FVector::FReal>> TypeInterface;
		if( FUnitConversion::Settings().ShouldDisplayUnits() )
		{
			TypeInterface = SharedThis(this);
		}

		TSharedPtr<SNumericVectorInputBox<FVector::FReal>> LocationWidget;
		ChildrenBuilder.AddCustomRow( LOCTEXT("LocationFilter", "Location") )
		.CopyAction( CreateCopyAction( ETransformField::Location ) )
		.PasteAction( CreatePasteAction( ETransformField::Location ) )
		.OverrideResetToDefault(FResetToDefaultOverride::Create(TAttribute<bool>(this, &FComponentTransformDetails::GetLocationResetVisibility), FSimpleDelegate::CreateSP(this, &FComponentTransformDetails::OnLocationResetClicked)))
		.PropertyHandleList({ GeneratePropertyHandle(UDreamWidget::GetPropertyName_RelativeLocation(), ChildrenBuilder) })
		.NameContent()
		.VAlign(VAlign_Center)
		[
			BuildTransformFieldLabel( ETransformField::Location )
		]
		.ValueContent()
		.MinDesiredWidth(125.0f * 3.0f)
		.MaxDesiredWidth(125.0f * 3.0f)
		.VAlign( VAlign_Center )
		[
			SAssignNew(LocationWidget, SNumericVectorInputBox<FVector::FReal>)
			.X( this, &FComponentTransformDetails::GetLocationX )
			.Y( this, &FComponentTransformDetails::GetLocationY )
			.Z( this, &FComponentTransformDetails::GetLocationZ )
			.bColorAxisLabels( true )
			.IsEnabled( this, &FComponentTransformDetails::GetIsEnabled )
			.OnXChanged(this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Location, EAxisList::X, false)
			.OnYChanged(this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Location, EAxisList::Y, false)
			.OnZChanged(this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Location, EAxisList::Z, false)
			.OnXCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Location, EAxisList::X, true )
			.OnYCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Location, EAxisList::Y, true )
			.OnZCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Location, EAxisList::Z, true )
			.Font( FontInfo )
			.TypeInterface( TypeInterface )
			.AllowSpin(SelectedObjects.Num() == 1)
			.SpinDelta(1)
			.OnBeginSliderMovement(this, &FComponentTransformDetails::OnBeginLocationSlider)
			.OnEndSliderMovement(this, &FComponentTransformDetails::OnEndLocationSlider)
		];
		//Disable Y&Z for UIItem
		{
			auto Child = LocationWidget->GetChildren()->GetChildAt(0);
			auto HorizontalBox = StaticCastSharedRef<SHorizontalBox>(Child);
			HorizontalBox->GetSlot(1).GetWidget()->SetEnabled(false);
			HorizontalBox->GetSlot(2).GetWidget()->SetEnabled(false);
		}
	}
	
	// Rotation
	{
		TSharedPtr<INumericTypeInterface<FVector::FReal>> TypeInterface;
		if( FUnitConversion::Settings().ShouldDisplayUnits() )
		{
			TypeInterface = MakeShareable( new TNumericUnitTypeInterface<FVector::FReal>(EUnit::Degrees) );
		}

		ChildrenBuilder.AddCustomRow( LOCTEXT("RotationFilter", "Rotation") )
		.CopyAction( CreateCopyAction(ETransformField::Rotation) )
		.PasteAction( CreatePasteAction(ETransformField::Rotation) )
		.OverrideResetToDefault(FResetToDefaultOverride::Create(TAttribute<bool>(this, &FComponentTransformDetails::GetRotationResetVisibility), FSimpleDelegate::CreateSP(this, &FComponentTransformDetails::OnRotationResetClicked)))
		.PropertyHandleList({ GeneratePropertyHandle(UDreamWidget::GetPropertyName_RelativeRotation(), ChildrenBuilder) })
		.NameContent()
		.VAlign(VAlign_Center)
		[
			BuildTransformFieldLabel(ETransformField::Rotation)
		]
		.ValueContent()
		.MinDesiredWidth(125.0f * 3.0f)
		.MaxDesiredWidth(125.0f * 3.0f)
		.VAlign( VAlign_Center )
		[
			SNew(SNumericRotatorInputBox<FRotator::FReal>)
			.AllowSpin( SelectedObjects.Num() == 1 ) 
			.Roll( this, &FComponentTransformDetails::GetRotationX )
			.Pitch( this, &FComponentTransformDetails::GetRotationY )
			.Yaw( this, &FComponentTransformDetails::GetRotationZ )
			.bColorAxisLabels( true )
			.IsEnabled( this, &FComponentTransformDetails::GetIsEnabled )
			.OnBeginSliderMovement( this, &FComponentTransformDetails::OnBeginRotationSlider )
			.OnEndSliderMovement( this, &FComponentTransformDetails::OnEndRotationSlider )
			.OnRollChanged( this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Rotation, EAxisList::X, false )
			.OnPitchChanged( this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Rotation, EAxisList::Y, false )
			.OnYawChanged( this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Rotation, EAxisList::Z, false )
			.OnRollCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Rotation, EAxisList::X, true )
			.OnPitchCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Rotation, EAxisList::Y, true )
			.OnYawCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Rotation, EAxisList::Z, true )
			.TypeInterface(TypeInterface)
			.Font( FontInfo )
		];
	}
	
	// Scale
	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("ScaleFilter", "Scale") )
		.CopyAction( CreateCopyAction(ETransformField::Scale) )
		.PasteAction( CreatePasteAction(ETransformField::Scale) )
		.OverrideResetToDefault(FResetToDefaultOverride::Create(TAttribute<bool>(this, &FComponentTransformDetails::GetScaleResetVisibility), FSimpleDelegate::CreateSP(this, &FComponentTransformDetails::OnScaleResetClicked)))
		.PropertyHandleList({ GeneratePropertyHandle(UDreamWidget::GetPropertyName_RelativeScale(), ChildrenBuilder) })
		.NameContent()
		.VAlign(VAlign_Center)
		[
			BuildTransformFieldLabel(ETransformField::Scale)
		]
		.ValueContent()
		.MinDesiredWidth(125.0f * 3.0f)
		.MaxDesiredWidth(125.0f * 3.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SNumericVectorInputBox<FVector::FReal>)
			.X( this, &FComponentTransformDetails::GetScaleX )
			.Y( this, &FComponentTransformDetails::GetScaleY )
			.Z( this, &FComponentTransformDetails::GetScaleZ )
			.bColorAxisLabels( true )
			.IsEnabled( this, &FComponentTransformDetails::GetIsEnabled )
			.OnXChanged( this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Scale, EAxisList::X, false )
			.OnYChanged( this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Scale, EAxisList::Y, false )
			.OnZChanged( this, &FComponentTransformDetails::OnSetTransformAxis, ETextCommit::Default, ETransformField::Scale, EAxisList::Z, false )
			.OnXCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Scale, EAxisList::X, true )
			.OnYCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Scale, EAxisList::Y, true )
			.OnZCommitted( this, &FComponentTransformDetails::OnSetTransformAxis, ETransformField::Scale, EAxisList::Z, true )
			.Font( FontInfo )
			.AllowSpin( SelectedObjects.Num() == 1 )
			.SpinDelta( 0.0025f )
			.OnBeginSliderMovement( this, &FComponentTransformDetails::OnBeginScaleSlider )
			.OnEndSliderMovement(this, &FComponentTransformDetails::OnEndScaleSlider)
		];
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void FComponentTransformDetails::Tick( float DeltaTime ) 
{
	CacheTransform();
	/*if (!FixedDisplayUnits.IsSet())
	{
		CacheCommonLocationUnits();
	}*/
}

void FComponentTransformDetails::CacheCommonLocationUnits()
{
	float LargestValue = 0.f;
	if (CachedLocation.X.IsSet() && CachedLocation.X.GetValue() > LargestValue)
	{
		LargestValue = CachedLocation.X.GetValue();
	}
	if (CachedLocation.Y.IsSet() && CachedLocation.Y.GetValue() > LargestValue)
	{
		LargestValue = CachedLocation.Y.GetValue();
	}
	if (CachedLocation.Z.IsSet() && CachedLocation.Z.GetValue() > LargestValue)
	{
		LargestValue = CachedLocation.Z.GetValue();
	}

	SetupFixedDisplay(LargestValue);
}

TSharedPtr<IPropertyHandle> FComponentTransformDetails::GeneratePropertyHandle(FName PropertyName, IDetailChildrenBuilder& ChildrenBuilder)
{
	// UDreamWidget, not USceneComponent. This is a port of the engine's actor transform section and the
	// ported lookups still named the class the original one edited, so all three of these came back
	// empty: the map has no entry under a class no selected object is, and the fallback then built a
	// property node over a list of nulls, because GetSceneComponentFromDetailsObject answers null for
	// every widget. The rows carried no property handle at all.
	//
	// Try finding the property handle in the details panel's property map first.
	IDetailLayoutBuilder& LayoutBuilder = ChildrenBuilder.GetParentCategory().GetParentLayout();
	TSharedPtr<IPropertyHandle> PropertyHandle = LayoutBuilder.GetProperty(PropertyName, UDreamWidget::StaticClass());
	if (!PropertyHandle || !PropertyHandle->IsValidHandle())
	{
		// If it wasn't found, add a collapsed row which contains the property node. These three are not
		// EditAnywhere, which is fine: AddObjectPropertyData names the property explicitly and the
		// visibility filter is bypassed for a single named child.
		TArray<UObject*> Widgets;
		Widgets.Reserve(SelectedObjects.Num());
		for (const TWeakObjectPtr<UDreamWidget>& Object : SelectedObjects)
		{
			if (UDreamWidget* Widget = Object.Get())
			{
				Widgets.Add(Widget);
			}
		}
		PropertyHandle = LayoutBuilder.AddObjectPropertyData(Widgets, PropertyName);
		//CachedHandlesObjects.Append(Widgets);
	}

	//PropertyHandles.Add(PropertyHandle);
	return PropertyHandle;
}

bool FComponentTransformDetails::GetIsEnabled() const
{
	return !GEditor->HasLockedActors() || SelectedActorInfo.NumSelected == 0;
}

const FSlateBrush* FComponentTransformDetails::GetPreserveScaleRatioImage() const
{
	return bPreserveScaleRatio ? FAppStyle::GetBrush(TEXT("Icons.Lock")) : FAppStyle::GetBrush(TEXT("Icons.Unlock"));
}

ECheckBoxState FComponentTransformDetails::IsPreserveScaleRatioChecked() const
{
	return bPreserveScaleRatio ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FComponentTransformDetails::OnPreserveScaleRatioToggled( ECheckBoxState NewState )
{
	bPreserveScaleRatio = (NewState == ECheckBoxState::Checked) ? true : false;
	GConfig->SetBool(TEXT("SelectionDetails"), TEXT("PreserveScaleRatio"), bPreserveScaleRatio, GEditorPerProjectIni);
}

bool FComponentTransformDetails::GetLocationResetVisibility() const
{
	const auto* Archetype = SelectedObjects[0].Get();
	if (!IsValid(Archetype))return false;
	FVector targetLocation = FVector::ZeroVector;
	if (!IsLocationXEnable())
	{
		targetLocation.X = Archetype->GetRelativeLocation().X;
	}
	if (!IsLocationYEnable())
	{
		targetLocation.Y = Archetype->GetRelativeLocation().Y;
	}
	if (!IsLocationZEnable())
	{
		targetLocation.Z = Archetype->GetRelativeLocation().Z;
	}
	return Archetype->GetRelativeLocation() != targetLocation;
}

void FComponentTransformDetails::OnLocationResetClicked()
{
	const FText TransactionName = LOCTEXT("ResetLocation", "Reset Location");
	FScopedTransaction Transaction(TransactionName);

	UDreamWidget* Archetype = SelectedObjects[0].Get();
	if (!IsValid(Archetype))return;
	FVector targetLocation = FVector::ZeroVector;
	if (!IsLocationXEnable())
	{
		targetLocation.X = Archetype->GetRelativeLocation().X;
	}
	if (!IsLocationYEnable())
	{
		targetLocation.Y = Archetype->GetRelativeLocation().Y;
	}
	if (!IsLocationZEnable())
	{
		targetLocation.Z = Archetype->GetRelativeLocation().Z;
	}

	OnSetTransform(ETransformField::Location, EAxisList::All, targetLocation, true);
}

bool FComponentTransformDetails::GetRotationResetVisibility() const
{
	const auto* Archetype = SelectedObjects[0].Get();
	if (!IsValid(Archetype))return false;
	return Archetype->GetRelativeRotation().Euler() != FVector::ZeroVector;
}

void FComponentTransformDetails::OnRotationResetClicked()
{
	const FText TransactionName = LOCTEXT("ResetRotation", "Reset Rotation");
	FScopedTransaction Transaction(TransactionName);

	UDreamWidget* Archetype = SelectedObjects[0].Get();
	if (!IsValid(Archetype))return;

	OnSetTransform(ETransformField::Rotation, EAxisList::All, FVector::ZeroVector, true);
}

bool FComponentTransformDetails::GetScaleResetVisibility() const
{
	const auto* Archetype = SelectedObjects[0].Get();
	if (!IsValid(Archetype))return false;
	return Archetype->GetRelativeScale() != FVector::OneVector;
}

void FComponentTransformDetails::OnScaleResetClicked()
{
	const FText TransactionName = LOCTEXT("ResetScale", "Reset Scale");
	FScopedTransaction Transaction(TransactionName);

	UDreamWidget* Archetype = SelectedObjects[0].Get();
	if (!IsValid(Archetype))return;

	OnSetTransform(ETransformField::Scale, EAxisList::All, FVector(1.0f), true);
}

void FComponentTransformDetails::CacheTransform()
{
	// Nothing else ever removes from this map, and its keys can die under it -- a designer preview is
	// rebuilt whole. Weak keys make a dead entry unfindable rather than a wrong answer; this drops them
	// so the map cannot carry corpses for the life of the panel.
	for (auto It = ObjectToRelativeRotationMap.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	FVector CurLoc = FVector(EForceInit::ForceInitToZero);
	FRotator CurRot = FRotator(EForceInit::ForceInitToZero);
	FVector CurScale = FVector(EForceInit::ForceInitToZero);

	for( int32 ObjectIndex = 0; ObjectIndex < SelectedObjects.Num(); ++ObjectIndex )
	{
		TWeakObjectPtr<UDreamWidget> ObjectPtr = SelectedObjects[ObjectIndex];
		if( ObjectPtr.IsValid() )
		{
			UDreamWidget* Object = ObjectPtr.Get();
			auto SceneComponent = Object;

			FVector Loc;
			FRotator Rot;
			FVector Scale;
			if( SceneComponent )
			{
				Loc = SceneComponent->GetRelativeLocation();
				FRotator* FoundRotator = ObjectToRelativeRotationMap.Find(SceneComponent);
				Rot = (bEditingRotationInUI && !Object->IsTemplate() && FoundRotator) ? *FoundRotator : SceneComponent->GetRelativeRotation().Rotator();
				Scale = SceneComponent->GetRelativeScale();

				if( ObjectIndex == 0 )
				{
					// Cache the current values from the first actor to see if any values differ among other actors
					CurLoc = Loc;
					CurRot = Rot;
					CurScale = Scale;

					CachedLocation.Set( Loc );
					CachedRotation.Set( Rot );
					CachedScale.Set( Scale );
				}
				else if( CurLoc != Loc || CurRot != Rot || CurScale != Scale )
				{
					// Check which values differ and unset the different values
					CachedLocation.X = Loc.X == CurLoc.X && CachedLocation.X.IsSet() ? Loc.X : TOptional<FVector::FReal>();
					CachedLocation.Y = Loc.Y == CurLoc.Y && CachedLocation.Y.IsSet() ? Loc.Y : TOptional<FVector::FReal>();
					CachedLocation.Z = Loc.Z == CurLoc.Z && CachedLocation.Z.IsSet() ? Loc.Z : TOptional<FVector::FReal>();

					CachedRotation.X = Rot.Roll == CurRot.Roll && CachedRotation.X.IsSet() ? Rot.Roll : TOptional<FVector::FReal>();
					CachedRotation.Y = Rot.Pitch == CurRot.Pitch && CachedRotation.Y.IsSet() ? Rot.Pitch : TOptional<FVector::FReal>();
					CachedRotation.Z = Rot.Yaw == CurRot.Yaw && CachedRotation.Z.IsSet() ? Rot.Yaw : TOptional<FVector::FReal>();

					CachedScale.X = Scale.X == CurScale.X && CachedScale.X.IsSet() ? Scale.X : TOptional<FVector::FReal>();
					CachedScale.Y = Scale.Y == CurScale.Y && CachedScale.Y.IsSet() ? Scale.Y : TOptional<FVector::FReal>();
					CachedScale.Z = Scale.Z == CurScale.Z && CachedScale.Z.IsSet() ? Scale.Z : TOptional<FVector::FReal>();

					// If all values are unset all values are different and we can stop looking
					const bool bAllValuesDiffer = !CachedLocation.IsSet() && !CachedRotation.IsSet() && !CachedScale.IsSet();
					if( bAllValuesDiffer )
					{
						break;
					}
				}
			}
		}
	}
}

bool FComponentTransformDetails::IsLocationYEnable()const
{
	if (SelectedObjects.Num() > 0)
	{
		TWeakObjectPtr<UDreamWidget> uiItem = SelectedObjects[0];
		if (uiItem.IsValid())
		{
			if (uiItem->GetParent() == nullptr)
			{
				return true;
			}
			return false;
		}
	}
	return false;
}
bool FComponentTransformDetails::IsLocationZEnable()const
{
	if (SelectedObjects.Num() > 0)
	{
		TWeakObjectPtr<UDreamWidget> uiItem = SelectedObjects[0];
		if (uiItem.IsValid())
		{
			if (uiItem->GetParent() == nullptr)
			{
				return true;
			}
			return false;
		}
	}
	return false;
}

FVector FComponentTransformDetails::GetAxisFilteredVector(EAxisList::Type Axis, const FVector& NewValue, const FVector& OldValue)
{
	return FVector((Axis & EAxisList::X) ? NewValue.X : OldValue.X,
		(Axis & EAxisList::Y) ? NewValue.Y : OldValue.Y,
		(Axis & EAxisList::Z) ? NewValue.Z : OldValue.Z);
}

void FComponentTransformDetails::OnSetTransform(ETransformField::Type TransformField, EAxisList::Type Axis, FVector NewValue, bool bCommitted)
{
	if (!bCommitted && SelectedObjects.Num() > 1)
	{
		// Ignore interactive changes when we have more than one selected object
		return;
	}

	FText TransactionText;
	FProperty* ValueProperty = nullptr;
	FProperty* AxisProperty = nullptr;
	
	switch (TransformField)
	{
	case ETransformField::Location:
		TransactionText = LOCTEXT("OnSetLocation", "Set Location");
		// UDreamWidget's own property, not USceneComponent's. This is a port of the engine's actor
		// transform section, and the ported lookups kept pointing at the class the original edited.
		// The event built from that property could never reach the designer's mirror: its guard asks
		// whether the edited object's class owns the head property, and a UDreamWidget is not a
		// USceneComponent -- so every transform typed into this panel moved the preview and died there.
		ValueProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), UDreamWidget::GetPropertyName_RelativeLocation());
		
		// Only set axis property for single axis set
		if (Axis == EAxisList::X)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FVector>::Get(), GET_MEMBER_NAME_CHECKED(FVector, X));
		}
		else if (Axis == EAxisList::Y)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FVector>::Get(), GET_MEMBER_NAME_CHECKED(FVector, Y));
		}
		else if (Axis == EAxisList::Z)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FVector>::Get(), GET_MEMBER_NAME_CHECKED(FVector, Z));
		}
		break;
	case ETransformField::Rotation:
		TransactionText = LOCTEXT("OnSetRotation", "Set Rotation");
		ValueProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), UDreamWidget::GetPropertyName_RelativeRotation());
		
		// Only set axis property for single axis set
		if (Axis == EAxisList::X)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FRotator>::Get(), GET_MEMBER_NAME_CHECKED(FRotator, Roll));
		}
		else if (Axis == EAxisList::Y)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FRotator>::Get(), GET_MEMBER_NAME_CHECKED(FRotator, Pitch));
		}
		else if (Axis == EAxisList::Z)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FRotator>::Get(), GET_MEMBER_NAME_CHECKED(FRotator, Yaw));
		}
		break;
	case ETransformField::Scale:
		TransactionText = LOCTEXT("OnSetScale", "Set Scale");
		// Also the NAME: the widget calls it RelativeScale, so the ported RelativeScale3D found the
		// scene component's property or nothing at all.
		ValueProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), UDreamWidget::GetPropertyName_RelativeScale());

		// If keep scale is set, don't set axis property
		if (!bPreserveScaleRatio && Axis == EAxisList::X)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FVector>::Get(), GET_MEMBER_NAME_CHECKED(FVector, X));
		}
		else if (!bPreserveScaleRatio && Axis == EAxisList::Y)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FVector>::Get(), GET_MEMBER_NAME_CHECKED(FVector, Y));
		}
		else if (!bPreserveScaleRatio && Axis == EAxisList::Z)
		{
			AxisProperty = FindFProperty<FDoubleProperty>(TBaseStructure<FVector>::Get(), GET_MEMBER_NAME_CHECKED(FVector, Z));
		}
		break;
	default:
		return;
	}

	bool bBeganTransaction = false;
	TArray<UObject*> ModifiedObjects;

	FPropertyChangedEvent PropertyChangedEvent(ValueProperty, !bCommitted ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet, MakeArrayView(ModifiedObjects));
	FEditPropertyChain PropertyChain;

	if (AxisProperty)
	{
		PropertyChain.AddHead(AxisProperty);
	}
	PropertyChain.AddHead(ValueProperty);
	FPropertyChangedChainEvent PropertyChangedChainEvent(PropertyChain, PropertyChangedEvent);

	for (int32 ObjectIndex = 0; ObjectIndex < SelectedObjects.Num(); ++ObjectIndex)
	{
		TWeakObjectPtr<UDreamWidget> ObjectPtr = SelectedObjects[ObjectIndex];
		if (ObjectPtr.IsValid())
		{
			UDreamWidget* Object = ObjectPtr.Get();
			UDreamWidget* SceneComponent = Object;
			if (SceneComponent)
			{
				const bool bIsEditingTemplateObject = Object->IsTemplate();

				FVector OldComponentValue;
				FVector NewComponentValue;

				switch (TransformField)
				{
				case ETransformField::Location:
					OldComponentValue = SceneComponent->GetRelativeLocation();
					break;
				case ETransformField::Rotation:
					// Pull from the actual component or from the cache
					OldComponentValue = SceneComponent->GetRelativeRotation().Euler();
					if (bEditingRotationInUI && !bIsEditingTemplateObject && ObjectToRelativeRotationMap.Find(SceneComponent))
					{
						OldComponentValue = ObjectToRelativeRotationMap.Find(SceneComponent)->Euler();
					}
					break;
				case ETransformField::Scale:
					OldComponentValue = SceneComponent->GetRelativeScale();
					break;
				}

				// NewValue is one whole vector, recomposed once from SelectedObjects[0] with the edited axis
				// substituted, so only the edited axes may be taken from it. Writing it wholesale stamps the
				// first selection's other two axes onto every other widget, and for a parented widget that
				// lands in serialized anchor data via CalculateAnchorFromTransform.
				NewComponentValue = GetAxisFilteredVector(Axis, NewValue, OldComponentValue);

				// If we're committing during a rotation edit, or during a slider transaction, then we need
				// to force it, in order that PostEditChangeChainProperty be called -- even though the
				// slider has usually NOT changed the value here, because the interactive ticks already
				// wrote it. Without the slider half (the engine's own condition, dropped in the port) the
				// committing pass found nothing to do, ModifiedObjects stayed empty, and neither the
				// PostEditChange below nor the notify hook that mirrors onto the template ever ran: drag a
				// transform slider, watch the preview move, find the asset unchanged.
				if (OldComponentValue != NewComponentValue || (bCommitted && (bEditingRotationInUI || bIsSliderTransaction)))
				{
					if (!bBeganTransaction && bCommitted)
					{
						// Begin a transaction the first time an actors rotation is about to change.
						// NOTE: One transaction per change, not per actor
						GEditor->BeginTransaction(TransactionText);
						bBeganTransaction = true;
					}

					FScopedSwitchWorldForObject WorldSwitcher(Object);

					if (bCommitted)
					{
						if (SceneComponent->HasAnyFlags(RF_DefaultSubObject))
						{
							// Default subobjects must be included in any undo/redo operations
							SceneComponent->SetFlags(RF_Transactional);
						}

						// Have to downcast here because of function overloading and inheritance not playing nicely
						// We don't call PreEditChange for non commit changes because most classes implement the version that doesn't check the interaction type
						((UObject*)SceneComponent)->PreEditChange(PropertyChain);
					}

					if (NotifyHook)
					{
						NotifyHook->NotifyPreChange(ValueProperty);
					}

					switch (TransformField)
					{
					case ETransformField::Location:
						{
							if (!bIsEditingTemplateObject)
							{
								// Update local cache for restoring later
								ObjectToRelativeRotationMap.FindOrAdd(SceneComponent) = SceneComponent->GetRelativeRotation().Rotator();
							}

							SceneComponent->SetRelativeLocation(NewComponentValue);

							CachedLocation.Set(NewComponentValue);

							break;
						}
					case ETransformField::Rotation:
						{
							FRotator NewRotation = FRotator::MakeFromEuler(NewComponentValue);

							if (!bIsEditingTemplateObject)
							{
								// Update local cache for restoring later
								ObjectToRelativeRotationMap.FindOrAdd(SceneComponent) = NewRotation;
							}

							SceneComponent->SetRelativeRotation(NewRotation.Quaternion());
							CachedRotation.Set(NewRotation);

							break;
						}
					case ETransformField::Scale:
						{
							if (bPreserveScaleRatio)
							{
								// If we set a single axis, scale the others
								float Ratio = 0.0f;

								switch (Axis)
								{
								case EAxisList::X:
									// Account for the previous scale being zero.  Just set to the new value in that case?
									Ratio = OldComponentValue.X == 0.0f ? NewComponentValue.X : NewComponentValue.X / OldComponentValue.X;
									NewComponentValue.Y *= Ratio;
									NewComponentValue.Z *= Ratio;
									break;
								case EAxisList::Y:
									Ratio = OldComponentValue.Y == 0.0f ? NewComponentValue.Y : NewComponentValue.Y / OldComponentValue.Y;
									NewComponentValue.X *= Ratio;
									NewComponentValue.Z *= Ratio;
									break;
								case EAxisList::Z:
									Ratio = OldComponentValue.Z == 0.0f ? NewComponentValue.Z : NewComponentValue.Z / OldComponentValue.Z;
									NewComponentValue.X *= Ratio;
									NewComponentValue.Y *= Ratio;
								default:
									// Do nothing, this set multiple axis at once
									break;
								}
							}

							SceneComponent->SetRelativeScale(NewComponentValue);

							break;
						}
					}

					ModifiedObjects.Add(Object);
				}
			}
		}
	}

	if (ModifiedObjects.Num())
	{
		for (UObject* Object : ModifiedObjects)
		{
			// The widget itself is what was written, and PreEditChange above was handed the chain, so a
			// matching PostEditChangeChainProperty has to follow it. The ported loop asked
			// GetSceneComponentFromDetailsObject first and did EVERYTHING inside "if (SceneComponent)" --
			// which for a UDreamWidget is never true, so this whole tail was dead code: no PostEditChange
			// (the widget never recomputed anything from its new transform), no quaternion restore, no
			// end-of-move broadcast. The scene-component-specific work below is left where it was.
			if (UDreamWidget* Widget = Cast<UDreamWidget>(Object))
			{
				FScopedSwitchWorldForObject WorldSwitcher(Widget);

				if (bCommitted)
				{
					// We don't call PostEditChange for non commit changes because most classes implement the version that doesn't check the interaction type
					Widget->PostEditChangeChainProperty(PropertyChangedChainEvent);
				}
				else
				{
					SnapshotTransactionBuffer(Widget);
				}

				if (!Widget->IsTemplate())
				{
					if (TransformField == ETransformField::Rotation || TransformField == ETransformField::Location)
					{
						if (const FRotator* FoundRotator = ObjectToRelativeRotationMap.Find(Widget))
						{
							const FQuat OldQuat = FoundRotator->GetDenormalized().Quaternion();
							//already a quaternion here, unlike the scene component's FRotator below
							const FQuat NewQuat = Widget->GetRelativeRotation();

							if (OldQuat.Equals(NewQuat))
							{
								// Need to restore the manually set rotation as it was modified by quat
								// conversion. Through the euler face, which is the one that stores angles
								// verbatim -- a widget keeps the quaternion as its serialized truth.
								Widget->SetRelativeRotationEuler(*FoundRotator);
							}
						}
					}

					if (bCommitted)
					{
						// Broadcast when the object is done moving
						GEditor->BroadcastEndObjectMovement(*Widget);
					}
				}
			}

			USceneComponent* SceneComponent = GetSceneComponentFromDetailsObject(Object);
			USceneComponent* OldSceneComponent = SceneComponent;

			if (SceneComponent)
			{
				AActor* EditedActor = SceneComponent->GetOwner();
				FString SceneComponentPath = SceneComponent->GetPathName(EditedActor);
				
				if (bCommitted)
				{
					// This can invalidate OldSceneComponent
					// We don't call PostEditChange for non commit changes because most classes implement the version that doesn't check the interaction type
					OldSceneComponent->PostEditChangeChainProperty(PropertyChangedChainEvent);
				}
				else
				{
					SnapshotTransactionBuffer(OldSceneComponent);
				}

				SceneComponent = FindObject<USceneComponent>(EditedActor, *SceneComponentPath);

				if (EditedActor && EditedActor->GetRootComponent() == SceneComponent)
				{
					if (bCommitted)
					{
						EditedActor->PostEditChangeChainProperty(PropertyChangedChainEvent);
						SceneComponent = FindObject<USceneComponent>(EditedActor, *SceneComponentPath);
					}
					else
					{
						SnapshotTransactionBuffer(EditedActor);
					}
				}
				
				if (!Object->IsTemplate())
				{
					if (TransformField == ETransformField::Rotation || TransformField == ETransformField::Location)
					{
						FRotator* FoundRotator = ObjectToRelativeRotationMap.Find(Cast<UDreamWidget>(Object));

						if (FoundRotator)
						{
							FQuat OldQuat = FoundRotator->GetDenormalized().Quaternion();
							FQuat NewQuat = SceneComponent->GetRelativeRotation().GetDenormalized().Quaternion();

							if (OldQuat.Equals(NewQuat))
							{
								// Need to restore the manually set rotation as it was modified by quat conversion
								SceneComponent->SetRelativeRotation(*FoundRotator);
							}
						}
					}

					if (bCommitted)
					{
						// Broadcast when the actor is done moving
						GEditor->BroadcastEndObjectMovement(*SceneComponent);
						if (EditedActor && EditedActor->GetRootComponent() == SceneComponent)
						{
							GEditor->BroadcastEndObjectMovement(*EditedActor);
						}
					}
				}
			}
		}

		if (NotifyHook)
		{
			NotifyHook->NotifyPostChange(PropertyChangedEvent, ValueProperty);
		}
	}

	if (bCommitted && bBeganTransaction)
	{
		GEditor->EndTransaction();
		CacheTransform();
	}

	GUnrealEd->UpdatePivotLocationForSelection();
	GUnrealEd->SetPivotMovedIndependently(false);
	// Redraw
	GUnrealEd->RedrawLevelEditingViewports();
}

void FComponentTransformDetails::OnSetTransformAxis(FVector::FReal NewValue, ETextCommit::Type CommitInfo, ETransformField::Type TransformField, EAxisList::Type Axis, bool bCommitted)
{
	if (SelectedObjects.Num() <= 0)return;
	UDreamWidget* Archetype = SelectedObjects[0].Get();
	if (!IsValid(Archetype))return;
	switch (TransformField)
	{
	case ETransformField::Location:
	{
		FVector NewVector = GetAxisFilteredVector(Axis, FVector(NewValue), Archetype->GetRelativeLocation());
		OnSetTransform(TransformField, Axis, NewVector, bCommitted);
	}
	break;
	case ETransformField::Rotation:
	{
		FVector NewVector = GetAxisFilteredVector(Axis, FVector(NewValue), Archetype->GetRelativeRotation().Euler());
		OnSetTransform(TransformField, Axis, NewVector, bCommitted);
	}
	break;
	case ETransformField::Scale:
	{
		FVector NewVector = GetAxisFilteredVector(Axis, FVector(NewValue), Archetype->GetRelativeScale());
		OnSetTransform(TransformField, Axis, NewVector, bCommitted);
	}
	break;
	}
}

void FComponentTransformDetails::BeginSliderTransaction(FText ActorTransaction, FText ComponentTransaction) const
{
	bool bBeganTransaction = false;
	for (TWeakObjectPtr<UObject> ObjectPtr : SelectedObjects)
	{
		if (ObjectPtr.IsValid())
		{
			UObject* Object = ObjectPtr.Get();

			// Start a new transaction when a slider begins to change
			// We'll end it when the slider is released
			// NOTE: One transaction per change, not per actor
			if (!bBeganTransaction)
			{
				if (Object->IsA<AActor>())
				{
					GEditor->BeginTransaction(ActorTransaction);
				}
				else
				{
					GEditor->BeginTransaction(ComponentTransaction);
				}

				bBeganTransaction = true;
			}

			// The selection is UDreamWidgets, which are plain UObjects, so the ported
			// GetSceneComponentFromDetailsObject answered null for every one of them and this snapshot --
			// the only reason the transaction is opened here at all -- was never taken.
			FScopedSwitchWorldForObject WorldSwitcher(Object);

			if (Object->HasAnyFlags(RF_DefaultSubObject))
			{
				// Default subobjects must be included in any undo/redo operations
				Object->SetFlags(RF_Transactional);
			}

			// Call modify but not PreEdit, we don't do the proper "Edit" until it's committed
			Object->Modify();
		}
	}

	// Just in case we couldn't start a new transaction for some reason
	if (!bBeganTransaction)
	{
		GEditor->BeginTransaction(ActorTransaction);
	}
}

void FComponentTransformDetails::OnBeginRotationSlider()
{
	FText ActorTransaction = LOCTEXT("OnSetRotation", "Set Rotation");
	FText ComponentTransaction = LOCTEXT("OnSetRotation_ComponentDirect", "Modify Component(s)");
	BeginSliderTransaction(ActorTransaction, ComponentTransaction);
	
	bEditingRotationInUI = true;
	bIsSliderTransaction = true;

	for (TWeakObjectPtr<UDreamWidget> ObjectPtr : SelectedObjects)
	{
		//the widget, not GetSceneComponentFromDetailsObject, which answers null for every one of them
		if (UDreamWidget* Widget = ObjectPtr.Get())
		{
			FScopedSwitchWorldForObject WorldSwitcher(Widget);

			// Add/update cached rotation value prior to slider interaction
			ObjectToRelativeRotationMap.FindOrAdd(Widget) = Widget->GetRelativeRotation().Rotator();
		}
	}
}

void FComponentTransformDetails::OnEndRotationSlider(FVector::FReal NewValue)
{
	// Commit gets called right before this, only need to end the transaction
	bEditingRotationInUI = false;
	bIsSliderTransaction = false;
	GEditor->EndTransaction();
}

void FComponentTransformDetails::OnBeginLocationSlider()
{
	bIsSliderTransaction = true;
	FText ActorTransaction = LOCTEXT("OnSetLocation", "Set Location");
	FText ComponentTransaction = LOCTEXT("OnSetLocation_ComponentDirect", "Modify Component Location");
	BeginSliderTransaction(ActorTransaction, ComponentTransaction);
}

void FComponentTransformDetails::OnEndLocationSlider(FVector::FReal NewValue)
{
	bIsSliderTransaction = false;
	GEditor->EndTransaction();
}

void FComponentTransformDetails::OnBeginScaleSlider()
{
	// Assumption: slider isn't usable if multiple objects are selected
	//SliderScaleRatio.X = CachedScale.X.GetValue();
	//SliderScaleRatio.Y = CachedScale.Y.GetValue();
	//SliderScaleRatio.Z = CachedScale.Z.GetValue();

	bIsSliderTransaction = true;
	FText ActorTransaction = LOCTEXT("OnSetScale", "Set Scale");
	FText ComponentTransaction = LOCTEXT("OnSetScale_ComponentDirect", "Modify Component Scale");
	BeginSliderTransaction(ActorTransaction, ComponentTransaction);
}

void FComponentTransformDetails::OnEndScaleSlider(FVector::FReal NewValue)
{
	bIsSliderTransaction = false;
	GEditor->EndTransaction();
}

#undef LOCTEXT_NAMESPACE
