// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"
#include "RHI/DreamPixelProbe.h"

#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIAdjustment.h"
#include "Core/DreamUIStylize.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "DreamNiagaraSystem.h"
#include "DreamUICaptureLibrary.h"
#include "DreamWidgetBlueprint.h"
#include "Extensions/DreamMaskTransition.h"
#include "Extensions/DreamPath.h"
#include "Extensions/Effects/DreamAdjustmentLayer.h"
#include "Extensions/Effects/DreamBackgroundBlur.h"
#include "Extensions/Effects/DreamGlass.h"
#include "Extensions/Effects/DreamHoloFoil.h"
#include "Extensions/Effects/DreamPointerLight.h"
#include "Extensions/Effects/DreamStylizeEffect.h"
#include "Extensions/Effects/DreamTiltCard.h"
#include "MeshModifier/DreamMeshModifierJelly.h"
#include "MeshModifier/DreamMeshModifierTextOnPath.h"
#include "MeshModifier/DreamMeshModifierWarp.h"
#include "NiagaraSystem.h"

#include "Slate/SceneViewport.h"

/*
 * Every layer effect, drawn in the designer.
 *
 * Each case puts two holders side by side on a designer's canvas with the same content -- a backdrop of coloured bars
 * and a block over it -- and gives one of them the effect. The designer's own viewport is drawn by a real RHI and read
 * back, and the two holders are held to each other: where the effect is drawn they differ, and by enough pixels to see.
 *
 * Then the effect is turned off the way the details panel would turn it off: one property of the PREVIEW object written
 * through the same edit notifications a property row sends, and mirrored to the template as the designer mirrors a
 * details edit. Drawn again without a compile in between, the two holders match: an edit shows the moment it is made.
 *
 * A pointer-driven effect at rest (a tilt card nobody is pointing at, a jelly nobody has pressed) draws nothing extra,
 * and its case says so rather than asking for a difference.
 */
namespace DreamDesignerLayerEffectsPixelTestLocal
{
	using namespace DreamTests;

	/** How far a channel has to move for a pixel to count as changed: past antialiasing and dithering. */
	constexpr int32 ChangedBy = 24;
	/** How long the first draw may take: the effects' shaders compile on first use. */
	constexpr double DrawnSeconds = 120.0;
	const FVector2D HolderSize(400.0, 300.0);
	const FVector2D HolderCentre(330.0, 0.0);
	const FColor BackdropColour(40, 60, 90, 255);
	const FColor SubjectColour(220, 140, 80, 255);
	const TCHAR* const NiagaraSystemPath = TEXT("/DreamGUI/Tests/Niagara/NS_DreamUITest_Particles.NS_DreamUITest_Particles");

	struct FPicture
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		bool IsSet() const { return Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y; }
	};

	bool Read(FDreamDesignerDriver& InDriver, FPicture& OutPicture)
	{
		const TSharedPtr<FSceneViewport> Viewport = InDriver.SceneViewport();
		return Viewport.IsValid() && UDreamUICaptureLibrary::ReadViewportPixels(Viewport.Get(), OutPicture.Pixels, OutPicture.Size)
			&& OutPicture.IsSet();
	}

	/** One holder's pixels, from InMin, InSize across. */
	TArray<FColor> Crop(const FPicture& InPicture, FIntPoint InMin, FIntPoint InSize)
	{
		TArray<FColor> Out;
		Out.Reserve(InSize.X * InSize.Y);
		for (int32 Y = 0; Y < InSize.Y; ++Y)
		{
			for (int32 X = 0; X < InSize.X; ++X)
			{
				const int32 PX = FMath::Clamp(InMin.X + X, 0, InPicture.Size.X - 1);
				const int32 PY = FMath::Clamp(InMin.Y + Y, 0, InPicture.Size.Y - 1);
				Out.Add(InPicture.Pixels[PY * InPicture.Size.X + PX]);
			}
		}
		return Out;
	}

	/** Every widget under InRoot that is actually there, InRoot not counted, each one before the widgets it holds. */
	void CollectLiveUnder(const UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets)
	{
		for (UDreamWidget* Child : LiveChildrenOf(InRoot))
		{
			OutWidgets.Add(Child);
			CollectLiveUnder(Child, OutWidgets);
		}
	}

	bool Differs(const FColor& InA, const FColor& InB)
	{
		return FMath::Abs(InA.R - InB.R) > ChangedBy || FMath::Abs(InA.G - InB.G) > ChangedBy || FMath::Abs(InA.B - InB.B) > ChangedBy;
	}

	/**
	 * The pixels of InLeft that no pixel of InRight within one pixel matches. The two pictures are the same content at two places
	 * the designer projects to different fractions of a pixel, so every edge is antialiased a little differently in each; a
	 * neighbour that matches is that, and an effect changes far more than a pixel's reach.
	 */
	int32 CountDifferent(const TArray<FColor>& InLeft, const TArray<FColor>& InRight, FIntPoint InSize)
	{
		int32 Count = 0;
		for (int32 Y = 0; Y < InSize.Y; ++Y)
		{
			for (int32 X = 0; X < InSize.X; ++X)
			{
				const FColor& A = InLeft[Y * InSize.X + X];
				bool bMatched = false;
				for (int32 DY = -1; DY <= 1 && !bMatched; ++DY)
				{
					for (int32 DX = -1; DX <= 1 && !bMatched; ++DX)
					{
						const int32 NX = FMath::Clamp(X + DX, 0, InSize.X - 1);
						const int32 NY = FMath::Clamp(Y + DY, 0, InSize.Y - 1);
						bMatched = !Differs(A, InRight[NY * InSize.X + NX]);
					}
				}
				Count += bMatched ? 0 : 1;
			}
		}
		return Count;
	}

	/** What a case builds in a holder, with the effect or without it. */
	struct FBuild
	{
		UDreamWidgetBlueprint* Blueprint = nullptr;
		UDreamWidget* Holder = nullptr;
		bool bWith = false;
		/** The widget whose effect the case turns off, in the template; the case sets it. */
		UDreamWidget* Subject = nullptr;

		UDreamWidget* Widget(const TCHAR* InName, FVector2D InSize, FVector2D InCentre, UDreamWidget* InParent = nullptr) const
		{
			UDreamWidget* Made = DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(), InParent ? InParent : Holder, -1,
				FString::Printf(TEXT("%s%s"), InName, bWith ? TEXT("With") : TEXT("Without")));
			if (Made)
			{
				Made->SetWidth(InSize.X);
				Made->SetHeight(InSize.Y);
				Made->SetAnchoredPosition(InCentre);
			}
			return Made;
		}

		UDreamRectBlock* Block(const TCHAR* InName, FVector2D InSize, FVector2D InCentre, FColor InColour, UDreamWidget* InParent = nullptr, float InCorner = 0.0f) const
		{
			UDreamWidget* Made = Widget(InName, InSize, InCentre, InParent);
			UDreamRectBlock* Visual = Made ? Made->CreateNewVisual<UDreamRectBlock>() : nullptr;
			if (Visual)
			{
				Visual->SetBodyColor(InColour);
				Visual->SetCornerRadius(FVector4(InCorner, InCorner, InCorner, InCorner));
			}
			return Visual;
		}

		/** The backdrop every case draws over: a field and three bars, so that what reads the backdrop has something to read. */
		void Backdrop() const
		{
			Block(TEXT("Field"), HolderSize, FVector2D::ZeroVector, BackdropColour);
			Block(TEXT("BarRed"), FVector2D(40.0, HolderSize.Y), FVector2D(-110.0, 0.0), FColor(230, 70, 60, 255));
			Block(TEXT("BarGreen"), FVector2D(40.0, HolderSize.Y), FVector2D(0.0, 0.0), FColor(70, 210, 110, 255));
			Block(TEXT("BarYellow"), FVector2D(40.0, HolderSize.Y), FVector2D(110.0, 0.0), FColor(240, 220, 70, 255));
		}

		UDreamRectBlock* SubjectBlock(UDreamWidget* InParent = nullptr) const
		{
			return Block(TEXT("Subject"), FVector2D(170.0, 110.0), FVector2D::ZeroVector, SubjectColour, InParent, 14.0f);
		}
	};

	/** What a case expects to see of its effect in the designer at rest. */
	enum class EExpect : uint8
	{
		/** The holder with the effect differs from the one without. */
		Drawn,
		/** It needs a pointer, a press or time the designer does not give it, so the two match. */
		AtRest,
	};

	struct FCase
	{
		FString Name;
		EExpect Expect = EExpect::Drawn;
		/** Pixels that must differ between the holders, at the designer's scale. */
		int32 MinChanged = 150;
		TFunction<void(FBuild&)> Build;
		/** The property of the subject's preview (or its visual, or one of its components) the details panel turns the effect off with. */
		TFunction<UObject*(UDreamWidget* /*InPreviewSubject*/)> OffObject;
		FString OffPath;
		FString OffValue;
	};

	template<typename T>
	UObject* ComponentOf(UDreamWidget* InWidget)
	{
		return IsValid(InWidget) ? InWidget->GetComponent<T>() : nullptr;
	}

	UObject* VisualOf(UDreamWidget* InWidget)
	{
		return IsValid(InWidget) ? InWidget->GetVisual() : nullptr;
	}

	UObject* WidgetItself(UDreamWidget* InWidget)
	{
		return InWidget;
	}

	TArray<FCase> MakeCases()
	{
		TArray<FCase> Cases;
		auto Add = [&Cases](const TCHAR* InName, TFunction<void(FBuild&)> InBuild, TFunction<UObject*(UDreamWidget*)> InOffObject,
			const TCHAR* InOffPath, const TCHAR* InOffValue, EExpect InExpect = EExpect::Drawn, int32 InMinChanged = 150)
		{
			FCase Case;
			Case.Name = InName;
			Case.Build = MoveTemp(InBuild);
			Case.OffObject = MoveTemp(InOffObject);
			Case.OffPath = InOffPath;
			Case.OffValue = InOffValue;
			Case.Expect = InExpect;
			Case.MinChanged = InMinChanged;
			Cases.Add(MoveTemp(Case));
		};

		Add(TEXT("LayerStyle"), [](FBuild& B)
		{
			B.Backdrop();
			UDreamRectBlock* Subject = B.SubjectBlock();
			B.Subject = Subject ? Subject->GetWidget() : nullptr;
			if (B.bWith && B.Subject)
			{
				FDreamLayerEffects Style;
				FDreamStyleDropShadow& Shadow = Style.DropShadows.AddDefaulted_GetRef();
				Shadow.Distance = 14.0f;
				Shadow.Size = 10.0f;
				Shadow.Opacity = 0.9f;
				FDreamStyleStroke& Stroke = Style.Strokes.AddDefaulted_GetRef();
				Stroke.Size = 5.0f;
				Stroke.Color = FColor::White;
				B.Subject->AddComponent<UDreamLayerStyle>()->SetStyle(Style);
			}
		}, &ComponentOf<UDreamLayerStyle>, TEXT("Style.bEnabled"), TEXT("False"));

		Add(TEXT("LayerGroup"), [](FBuild& B)
		{
			B.Backdrop();
			B.Subject = B.Widget(TEXT("Group"), FVector2D(260.0, 180.0), FVector2D::ZeroVector);
			B.Block(TEXT("Under"), FVector2D(150.0, 100.0), FVector2D(-35.0, -20.0), SubjectColour, B.Subject);
			B.Block(TEXT("Over"), FVector2D(150.0, 100.0), FVector2D(35.0, 20.0), FColor(90, 120, 230, 255), B.Subject);
			if (B.bWith && B.Subject)
			{
				UDreamLayerGroup* Group = B.Subject->AddComponent<UDreamLayerGroup>();
				Group->SetIsolation(EDreamLayerGroupIsolation::Isolated);
				Group->SetGroupOpacity(0.35f);
			}
		}, &ComponentOf<UDreamLayerGroup>, TEXT("bEnabled"), TEXT("False"));

		Add(TEXT("ShapeBlendGroup"), [](FBuild& B)
		{
			B.Backdrop();
			B.Subject = B.Widget(TEXT("Shapes"), FVector2D(300.0, 200.0), FVector2D::ZeroVector);
			B.Block(TEXT("Left"), FVector2D(110.0, 110.0), FVector2D(-62.0, 0.0), SubjectColour, B.Subject, 55.0f);
			B.Block(TEXT("Right"), FVector2D(110.0, 110.0), FVector2D(62.0, 0.0), SubjectColour, B.Subject, 55.0f);
			if (B.bWith && B.Subject)
			{
				B.Subject->AddComponent<UDreamShapeBlendGroup>()->SetSmoothness(48.0f);
			}
		}, &ComponentOf<UDreamShapeBlendGroup>, TEXT("bBlendEnabled"), TEXT("False"), EExpect::Drawn, 60);

		Add(TEXT("ClipMask"), [](FBuild& B)
		{
			B.Backdrop();
			B.Subject = B.Widget(TEXT("Masked"), FVector2D(200.0, 140.0), FVector2D::ZeroVector);
			B.Block(TEXT("Content"), FVector2D(200.0, 140.0), FVector2D::ZeroVector, SubjectColour, B.Subject);
			if (B.Subject)
			{
				B.Subject->SetClipping(EDreamWidgetClipping::ClipToBounds);
				if (B.bWith)
				{
					FDreamClipMask Mask;
					Mask.Shape = EDreamClipMaskShape::Ellipse;
					B.Subject->SetClipMask(Mask);
				}
			}
		}, &WidgetItself, TEXT("ClipMask.Shape"), TEXT("Rect"));

		Add(TEXT("ClipToBelow"), [](FBuild& B)
		{
			B.Backdrop();
			B.Block(TEXT("Below"), FVector2D(120.0, 120.0), FVector2D::ZeroVector, FColor(250, 250, 250, 255), nullptr, 60.0f);
			UDreamRectBlock* Above = B.Block(TEXT("Above"), FVector2D(260.0, 70.0), FVector2D::ZeroVector, FColor(200, 40, 160, 255));
			B.Subject = Above ? Above->GetWidget() : nullptr;
			if (B.bWith && B.Subject)
			{
				B.Subject->SetClipToBelow(true);
			}
		}, &WidgetItself, TEXT("bClipToBelow"), TEXT("False"));

		Add(TEXT("BlendMode"), [](FBuild& B)
		{
			B.Backdrop();
			UDreamRectBlock* Subject = B.SubjectBlock();
			B.Subject = Subject ? Subject->GetWidget() : nullptr;
			if (B.bWith && Subject)
			{
				Subject->SetBlendMode(EDreamUIBlendMode::Difference);
			}
		}, &VisualOf, TEXT("BlendMode"), TEXT("Alpha"));

		Add(TEXT("Glass"), [](FBuild& B)
		{
			B.Backdrop();
			if (B.bWith)
			{
				B.Subject = B.Widget(TEXT("Glass"), FVector2D(220.0, 150.0), FVector2D::ZeroVector);
				if (B.Subject)
				{
					B.Subject->CreateNewVisual<UDreamGlass>();
				}
			}
		}, &VisualOf, TEXT("bEnabled"), TEXT("False"));

		Add(TEXT("BackgroundBlur"), [](FBuild& B)
		{
			B.Backdrop();
			if (B.bWith)
			{
				B.Subject = B.Widget(TEXT("Blur"), FVector2D(220.0, 150.0), FVector2D::ZeroVector);
				if (B.Subject)
				{
					B.Subject->CreateNewVisual<UDreamBackgroundBlur>();
				}
			}
		}, &VisualOf, TEXT("bEnabled"), TEXT("False"));

		Add(TEXT("AdjustmentLayer"), [](FBuild& B)
		{
			B.Backdrop();
			if (B.bWith)
			{
				B.Subject = B.Widget(TEXT("Adjust"), FVector2D(220.0, 150.0), FVector2D::ZeroVector);
				if (UDreamAdjustmentLayer* Layer = B.Subject ? B.Subject->CreateNewVisual<UDreamAdjustmentLayer>() : nullptr)
				{
					FDreamAdjustment Invert;
					Invert.Type = EDreamAdjustmentType::Invert;
					Layer->SetAdjustments({ Invert });
				}
			}
		}, &VisualOf, TEXT("bEnabled"), TEXT("False"));

		Add(TEXT("Stylize"), [](FBuild& B)
		{
			B.Backdrop();
			if (B.bWith)
			{
				B.Subject = B.Widget(TEXT("Stylize"), FVector2D(220.0, 150.0), FVector2D::ZeroVector);
				if (UDreamStylizeEffect* Effect = B.Subject ? B.Subject->CreateNewVisual<UDreamStylizeEffect>() : nullptr)
				{
					// No flicker: a flickering screen never holds still to be compared.
					FDreamStylizeLayer Crt;
					Crt.Type = EDreamStylizeType::CRT;
					Crt.FlickerStrength = 0.0f;
					Effect->SetLayers({ Crt });
				}
			}
		}, &VisualOf, TEXT("bEnabled"), TEXT("False"));

		Add(TEXT("MaskTransition"), [](FBuild& B)
		{
			B.Backdrop();
			UDreamRectBlock* Subject = B.SubjectBlock();
			B.Subject = Subject ? Subject->GetWidget() : nullptr;
			if (B.bWith && B.Subject)
			{
				B.Subject->AddComponent<UDreamMaskTransition>()->SetProgress(0.5f);
			}
		}, &ComponentOf<UDreamMaskTransition>, TEXT("bEnabled"), TEXT("False"));

		Add(TEXT("Warp"), [](FBuild& B)
		{
			B.Backdrop();
			UDreamRectBlock* Subject = B.SubjectBlock();
			B.Subject = Subject ? Subject->GetWidget() : nullptr;
			if (B.bWith && Subject)
			{
				if (UDreamMeshModifierWarp* Warp = Cast<UDreamMeshModifierWarp>(Subject->AddMeshModifier(UDreamMeshModifierWarp::StaticClass())))
				{
					Warp->SetStyle(EDreamWarpStyle::Arc);
					Warp->SetBend(60.0f);
				}
			}
		}, &ComponentOf<UDreamMeshModifierWarp>, TEXT("bEnable"), TEXT("False"));

		Add(TEXT("TextOnPath"), [](FBuild& B)
		{
			B.Backdrop();
			UDreamWidget* Holder = B.Widget(TEXT("Label"), FVector2D(300.0, 60.0), FVector2D(0.0, 40.0));
			if (UDreamText* Text = Holder ? Holder->CreateNewVisual<UDreamText>() : nullptr)
			{
				Text->SetText(FText::FromString(TEXT("TEXT ON A CIRCLE")));
				Text->SetFontSize(32.0f);
				Text->SetColor(FColor::White);
				B.Subject = Holder;
				if (B.bWith)
				{
					Text->AddMeshModifier(UDreamMeshModifierTextOnPath::StaticClass());
				}
			}
		}, nullptr, TEXT(""), TEXT(""));

		Add(TEXT("HoloFoil"), [](FBuild& B)
		{
			B.Backdrop();
			UDreamRectBlock* Subject = B.SubjectBlock();
			B.Subject = Subject ? Subject->GetWidget() : nullptr;
			if (B.bWith && B.Subject)
			{
				B.Subject->AddComponent<UDreamHoloFoil>();
			}
		}, &ComponentOf<UDreamHoloFoil>, TEXT("bEnabled"), TEXT("False"));

		Add(TEXT("PointerLight"), [](FBuild& B)
		{
			B.Backdrop();
			// It lights bevels and reveals borders: a block with a border to reveal.
			UDreamRectBlock* Subject = B.SubjectBlock();
			B.Subject = Subject ? Subject->GetWidget() : nullptr;
			if (Subject)
			{
				Subject->SetEnableBorder(true);
				Subject->SetBorderWidth(8.0f);
				Subject->SetBorderColor(FColor(30, 30, 40, 255));
			}
			if (B.bWith && B.Subject)
			{
				B.Subject->AddComponent<UDreamPointerLight>();
			}
		}, &ComponentOf<UDreamPointerLight>, TEXT("bEnabled"), TEXT("False"));

		Add(TEXT("TiltCard"), [](FBuild& B)
		{
			B.Backdrop();
			UDreamRectBlock* Subject = B.SubjectBlock();
			B.Subject = Subject ? Subject->GetWidget() : nullptr;
			if (B.bWith && B.Subject)
			{
				B.Subject->AddComponent<UDreamTiltCard>();
			}
		}, nullptr, TEXT(""), TEXT(""), EExpect::AtRest);

		Add(TEXT("Jelly"), [](FBuild& B)
		{
			B.Backdrop();
			UDreamRectBlock* Subject = B.SubjectBlock();
			B.Subject = Subject ? Subject->GetWidget() : nullptr;
			if (B.bWith && Subject)
			{
				Subject->AddMeshModifier(UDreamMeshModifierJelly::StaticClass());
			}
		}, nullptr, TEXT(""), TEXT(""), EExpect::AtRest);

		Add(TEXT("Path"), [](FBuild& B)
		{
			B.Backdrop();
			if (B.bWith)
			{
				B.Subject = B.Widget(TEXT("Path"), FVector2D(260.0, 120.0), FVector2D::ZeroVector);
				if (UDreamPath* Path = B.Subject ? B.Subject->CreateNewVisual<UDreamPath>() : nullptr)
				{
					Path->SetColor(FColor::White);
				}
			}
		}, &VisualOf, TEXT("bDrawLine"), TEXT("False"), EExpect::Drawn, 60);

		Add(TEXT("Niagara"), [](FBuild& B)
		{
			B.Backdrop();
			if (B.bWith)
			{
				B.Subject = B.Widget(TEXT("Particles"), FVector2D(260.0, 200.0), FVector2D::ZeroVector);
				if (UDreamNiagaraSystem* System = B.Subject ? B.Subject->CreateNewVisual<UDreamNiagaraSystem>() : nullptr)
				{
					System->SetSystem(LoadObject<UNiagaraSystem>(nullptr, NiagaraSystemPath));
				}
			}
		}, &WidgetItself, TEXT("bWidgetActive"), TEXT("False"), EExpect::Drawn, 40);

		return Cases;
	}

	/**
	 * Write InValueText to the property at InPath (dots into structs) of InObject the way a property row does: the designer told
	 * first, the edit notifications around the write, the top-level property written through its setter, then the designer told
	 * again so that the template takes the value too.
	 */
	bool EditAsTheDetailsPanelDoes(UObject* InObject, const FString& InPath, const FString& InValueText, FString& OutWhyNot)
	{
		TArray<FString> Parts;
		InPath.ParseIntoArray(Parts, TEXT("."));
		if (!IsValid(InObject) || Parts.Num() == 0)
		{
			OutWhyNot = TEXT("no object or no path");
			return false;
		}
		TArray<FProperty*> Chain;
		const UStruct* Scope = InObject->GetClass();
		for (const FString& Part : Parts)
		{
			FProperty* Property = Scope ? FindFProperty<FProperty>(Scope, *Part) : nullptr;
			if (Property == nullptr)
			{
				OutWhyNot = FString::Printf(TEXT("%s has no %s"), Scope ? *Scope->GetName() : TEXT("(nothing)"), *Part);
				return false;
			}
			Chain.Add(Property);
			const FStructProperty* AsStruct = CastField<FStructProperty>(Property);
			Scope = AsStruct ? AsStruct->Struct : nullptr;
		}
		FEditPropertyChain EditChain;
		for (FProperty* Property : Chain)
		{
			EditChain.AddTail(Property);
		}
		EditChain.SetActiveMemberPropertyNode(Chain[0]);
		EditChain.SetActivePropertyNode(Chain.Last());

		UDreamWidget* OwnerWidget = Cast<UDreamWidget>(InObject);
		OwnerWidget = OwnerWidget ? OwnerWidget : InObject->GetTypedOuter<UDreamWidget>();
		FDreamWidgetBlueprintEditor* Designer = OwnerWidget ? FDreamWidgetBlueprintEditor::FindDesignerForWidget(OwnerWidget) : nullptr;
		TArray<UObject*> Edited = { InObject };
		if (Designer)
		{
			Designer->MigrateDetailsChangeToTemplate(Edited, EditChain, /*bIsModify*/true);
		}
		InObject->PreEditChange(EditChain);
		// The whole top-level value, edited in a copy and put back through the property's setter where it has one.
		FProperty* Top = Chain[0];
		void* Copy = FMemory::Malloc(Top->GetSize(), Top->GetMinAlignment());
		Top->InitializeValue(Copy);
		Top->GetValue_InContainer(InObject, Copy);
		void* Leaf = Copy;
		for (int32 Index = 1; Index < Chain.Num(); ++Index)
		{
			Leaf = Chain[Index]->ContainerPtrToValuePtr<void>(Leaf);
		}
		const bool bImported = Chain.Last()->ImportText_Direct(*InValueText, Leaf, InObject, PPF_None) != nullptr;
		if (bImported)
		{
			Top->SetValue_InContainer(InObject, Copy);
		}
		Top->DestroyValue(Copy);
		FMemory::Free(Copy);
		if (!bImported)
		{
			OutWhyNot = FString::Printf(TEXT("'%s' is not a value of %s"), *InValueText, *InPath);
			return false;
		}
		FPropertyChangedEvent Event(Chain.Last(), EPropertyChangeType::ValueSet, MakeArrayView(Edited));
		FPropertyChangedChainEvent ChainEvent(EditChain, Event);
		InObject->PostEditChangeChainProperty(ChainEvent);
		if (Designer)
		{
			Designer->MigrateDetailsChangeToTemplate(Edited, EditChain, /*bIsModify*/false);
		}
		return true;
	}

	/** The pixels of each holder, cropped to one size. */
	bool ReadHolders(FDreamDesignerDriver& InDriver, const FDesignerLatentRef& InState, TArray<FColor>& OutWith, TArray<FColor>& OutWithout,
		FIntPoint& OutSize, FPicture& OutPicture)
	{
		const TOptional<FBox2D> With = InDriver.WidgetPixelRect(InDriver.PreviewFor(InState->Get(TEXT("With"))));
		const TOptional<FBox2D> Without = InDriver.WidgetPixelRect(InDriver.PreviewFor(InState->Get(TEXT("Without"))));
		if (!With.IsSet() || !Without.IsSet() || !Read(InDriver, OutPicture))
		{
			return false;
		}
		OutSize = FIntPoint(
			FMath::FloorToInt32(FMath::Min(With->GetSize().X, Without->GetSize().X)) - 2,
			FMath::FloorToInt32(FMath::Min(With->GetSize().Y, Without->GetSize().Y)) - 2);
		if (OutSize.X <= 8 || OutSize.Y <= 8)
		{
			return false;
		}
		OutWith = Crop(OutPicture, FIntPoint(FMath::RoundToInt32(With->Min.X) + 1, FMath::RoundToInt32(With->Min.Y) + 1), OutSize);
		OutWithout = Crop(OutPicture, FIntPoint(FMath::RoundToInt32(Without->Min.X) + 1, FMath::RoundToInt32(Without->Min.Y) + 1), OutSize);
		return true;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FDreamDesignerLayerEffectsPixelTest,
	"DreamGUI.Designer.RHI.LayerEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

void FDreamDesignerLayerEffectsPixelTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const DreamDesignerLayerEffectsPixelTestLocal::FCase& Case : DreamDesignerLayerEffectsPixelTestLocal::MakeCases())
	{
		OutBeautifiedNames.Add(Case.Name);
		OutTestCommands.Add(Case.Name);
	}
}

bool FDreamDesignerLayerEffectsPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerLayerEffectsPixelTestLocal;
	TSharedPtr<FCase> Case;
	for (FCase& Candidate : MakeCases())
	{
		if (Candidate.Name == Parameters)
		{
			Case = MakeShared<FCase>(MoveTemp(Candidate));
		}
	}
	if (!TestTrue(FString::Printf(TEXT("There is a case called %s"), *Parameters), Case.IsValid()))
	{
		return false;
	}

	const FDesignerLatentRef State = OpenLatentDesigner(*this, *FString::Printf(TEXT("DesignerEffect%s"), *Case->Name), /*bGiveRootAPanel*/ false);
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Case]()
	{
		UDreamWidgetBlueprint* Blueprint = State->Asset.Blueprint;
		UDreamWidget* Root = DesignerTemplateRoot(Blueprint);
		for (const bool bWith : { true, false })
		{
			FBuild Build;
			Build.Blueprint = Blueprint;
			Build.bWith = bWith;
			Build.Holder = DreamWidgetTreeEditing::CreateWidget(Blueprint, UDreamWidget::StaticClass(), Root, -1, bWith ? TEXT("With") : TEXT("Without"));
			if (!TestNotNull(TEXT("A holder was made"), Build.Holder))
			{
				State->bAlive = false;
				return;
			}
			Build.Holder->SetWidth(HolderSize.X);
			Build.Holder->SetHeight(HolderSize.Y);
			Build.Holder->SetAnchoredPosition(bWith ? -HolderCentre : HolderCentre);
			Case->Build(Build);
			State->Made.Add(bWith ? TEXT("With") : TEXT("Without"), Build.Holder);
			if (Build.Subject)
			{
				State->Made.Add(bWith ? TEXT("SubjectWith") : TEXT("SubjectWithout"), Build.Subject);
			}
		}
		State->Driver->Compile();
		SelectNothingInDesigner(*State->Driver);
		State->Driver->MoveTo(FIntPoint(State->Driver->ViewportPixelSize().X - 4, State->Driver->ViewportPixelSize().Y - 4));
	});
	// Drawn until the picture holds still: shaders compile on first use, and a particle system needs a moment to show.
	TSharedRef<int32> LastChanged = MakeShared<int32>(-1);
	TSharedRef<int32> StableFor = MakeShared<int32>(0);
	EnqueueDesignerUntil(State, this, [State, LastChanged, StableFor]()
	{
		TArray<FColor> With, Without;
		FIntPoint Size;
		FPicture Picture;
		if (!ReadHolders(*State->Driver, State, With, Without, Size, Picture))
		{
			return false;
		}
		const int32 Changed = CountDifferent(With, Without, Size);
		*StableFor = FMath::Abs(Changed - *LastChanged) <= FMath::Max(4, Changed / 50) ? *StableFor + 1 : 0;
		*LastChanged = Changed;
		return *StableFor >= 6;
	}, DrawnSeconds, TEXT("the two holders drawn and holding still"), /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Case]()
	{
		TArray<FColor> With, Without;
		FIntPoint Size;
		FPicture Picture;
		if (!TestTrue(TEXT("Both holders are on screen and the viewport reads back"), ReadHolders(*State->Driver, State, With, Without, Size, Picture)))
		{
			State->bAlive = false;
			return;
		}
		FDreamPixelProbe::SaveCapture(Picture.Pixels, Picture.Size, FString::Printf(TEXT("Designer_Effect_%s"), *Case->Name));
		const int32 Changed = CountDifferent(With, Without, Size);
		AddInfo(FString::Printf(TEXT("%s: %d of %d pixels differ between the holders"), *Case->Name, Changed, Size.X * Size.Y));
		if (Case->Expect == EExpect::Drawn)
		{
			TestTrue(FString::Printf(TEXT("%s is drawn in the designer (%d pixels differ, %d wanted)"), *Case->Name, Changed, Case->MinChanged),
				Changed >= Case->MinChanged);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s at rest draws nothing extra (%d pixels differ)"), *Case->Name, Changed), Changed < Case->MinChanged);
		}
		if (Case->Expect != EExpect::Drawn || !Case->OffObject || Changed < Case->MinChanged)
		{
			State->bAlive = false;
			return;
		}
		UDreamWidget* PreviewSubject = State->Driver->PreviewFor(State->Get(TEXT("SubjectWith")));
		UObject* Target = Case->OffObject(PreviewSubject);
		FString WhyNot;
		if (!TestNotNull(TEXT("The preview object the effect is turned off on"), Target)
			|| !TestTrue(FString::Printf(TEXT("%s.%s set to %s as a property row would (%s)"), Target ? *Target->GetClass()->GetName() : TEXT("?"), *Case->OffPath, *Case->OffValue, *WhyNot),
				EditAsTheDetailsPanelDoes(Target, Case->OffPath, Case->OffValue, WhyNot)))
		{
			State->bAlive = false;
		}
	});
	EnqueueDesignerFrames(State, 6, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Case]()
	{
		TArray<FColor> With, Without;
		FIntPoint Size;
		FPicture Picture;
		if (!TestTrue(TEXT("The viewport reads back after the edit"), ReadHolders(*State->Driver, State, With, Without, Size, Picture)))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Picture.Pixels, Picture.Size, FString::Printf(TEXT("Designer_Effect_%s_Off"), *Case->Name));
		const int32 Changed = CountDifferent(With, Without, Size);
		TestTrue(FString::Printf(TEXT("%s turned off in the details shows at once, without a compile (%d pixels still differ)"), *Case->Name, Changed),
			Changed < FMath::Max(Case->MinChanged / 3, 20));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FDreamDesignerPaletteEffectsPixelTest,
	"DreamGUI.Designer.RHI.PaletteEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

void FDreamDesignerPaletteEffectsPixelTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const TCHAR* Row : { TEXT("LayerStyle"), TEXT("LayerGroup"), TEXT("ShapeBlendGroup"), TEXT("MaskTransition"), TEXT("HoloFoil"),
		TEXT("PointerLight"), TEXT("TiltCard"), TEXT("WarpModifier"), TEXT("JellyModifier"), TEXT("TextOnPathModifier"), TEXT("Glass"),
		TEXT("AdjustmentLayer"), TEXT("Stylize"), TEXT("Path") })
	{
		OutBeautifiedNames.Add(Row);
		OutTestCommands.Add(Row);
	}
}

/*
 * A palette row dropped on a designer's canvas over coloured bars, as an author drops it: the pixels of the widget it made
 * are not the pixels that were there before. An effect that arrives at its class defaults often draws nothing -- the row
 * is what gives it something to show, and this is what holds it to that.
 */
bool FDreamDesignerPaletteEffectsPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerLayerEffectsPixelTestLocal;
	const FName Row(*Parameters);
	const FDesignerLatentRef State = OpenLatentDesigner(*this, *FString::Printf(TEXT("DesignerPalette%s"), *Parameters), /*bGiveRootAPanel*/ false);
	TSharedRef<FPicture> Before = MakeShared<FPicture>();
	EnqueueDesignerFrames(State, 2, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State]()
	{
		FBuild Build;
		Build.Blueprint = State->Asset.Blueprint;
		Build.Holder = DreamWidgetTreeEditing::CreateWidget(Build.Blueprint, UDreamWidget::StaticClass(), DesignerTemplateRoot(Build.Blueprint), -1, TEXT("Backdrop"));
		if (!TestNotNull(TEXT("A backdrop was made"), Build.Holder))
		{
			State->bAlive = false;
			return;
		}
		const FVector2D Extent(HolderSize.X * 2.0, HolderSize.Y * 1.5);
		Build.Holder->SetWidth(Extent.X);
		Build.Holder->SetHeight(Extent.Y);
		Build.Block(TEXT("Field"), Extent, FVector2D::ZeroVector, BackdropColour);
		const FColor Colours[] = { FColor(230, 70, 60, 255), FColor(70, 210, 110, 255), FColor(240, 220, 70, 255) };
		for (int32 Bar = 0; Bar < 7; ++Bar)
		{
			Build.Block(*FString::Printf(TEXT("Bar%d"), Bar), FVector2D(36.0, Extent.Y), FVector2D(-300.0 + Bar * 100.0, 0.0), Colours[Bar % 3]);
		}
		State->Made.Add(TEXT("Backdrop"), Build.Holder);
		State->Driver->Compile();
		SelectNothingInDesigner(*State->Driver);
		State->Driver->MoveTo(FIntPoint(State->Driver->ViewportPixelSize().X - 4, State->Driver->ViewportPixelSize().Y - 4));
	});
	EnqueueDesignerFrames(State, 30, /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Before, Row]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		if (!TestTrue(TEXT("The viewport reads back before the drop"), Read(Driver, *Before)))
		{
			State->bAlive = false;
			return;
		}
		// The centre is over the backdrop, a group with no panel, and a drop on a group lands in it -- so the new widget is looked
		// for in the whole tree, not only among the root's children.
		UDreamWidget* Root = DesignerTemplateRoot(State->Asset.Blueprint);
		TArray<UDreamWidget*> Existing;
		CollectLiveUnder(Root, Existing);
		if (!TestTrue(FString::Printf(TEXT("The %s row's drop is taken"), *Row.ToString()), Driver.DropFromPalette(Row, Driver.ViewportCentrePixel())))
		{
			State->bAlive = false;
			return;
		}
		TArray<UDreamWidget*> After;
		CollectLiveUnder(Root, After);
		for (UDreamWidget* Widget : After)
		{
			if (!Existing.Contains(Widget))
			{
				State->Made.Add(TEXT("Dropped"), Widget);
				break;
			}
		}
		if (!TestNotNull(TEXT("The drop made a widget"), State->Get(TEXT("Dropped"))))
		{
			State->bAlive = false;
			return;
		}
		SelectNothingInDesigner(Driver);
		Driver.MoveTo(FIntPoint(Driver.ViewportPixelSize().X - 4, Driver.ViewportPixelSize().Y - 4));
	});
	TSharedRef<int32> Changed = MakeShared<int32>(0);
	EnqueueDesignerUntil(State, this, [State, Before, Changed]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(State->Get(TEXT("Dropped"))));
		FPicture Now;
		if (!Rect.IsSet() || !Read(Driver, Now) || Now.Size != Before->Size)
		{
			return false;
		}
		const FIntPoint Min(FMath::Max(0, FMath::RoundToInt32(Rect->Min.X)), FMath::Max(0, FMath::RoundToInt32(Rect->Min.Y)));
		const FIntPoint Size(FMath::Min(Now.Size.X - Min.X, FMath::RoundToInt32(Rect->GetSize().X)), FMath::Min(Now.Size.Y - Min.Y, FMath::RoundToInt32(Rect->GetSize().Y)));
		if (Size.X <= 0 || Size.Y <= 0)
		{
			return false;
		}
		*Changed = CountDifferent(Crop(*Before, Min, Size), Crop(Now, Min, Size), Size);
		return *Changed >= 150;
	}, DrawnSeconds, TEXT("the dropped effect drawn over the bars"), /*bInDraw*/ true);
	EnqueueDesignerAction(State, [this, State, Changed, Row]()
	{
		FPicture Now;
		if (Read(*State->Driver, Now))
		{
			FDreamPixelProbe::SaveCapture(Now.Pixels, Now.Size, FString::Printf(TEXT("Designer_Palette_%s"), *Row.ToString()));
		}
		const UDreamWidget* Dropped = State->Get(TEXT("Dropped"));
		const UDreamWidget* Parent = Dropped != nullptr ? Dropped->GetParent() : nullptr;
		AddInfo(FString::Printf(TEXT("%s: landed in %s, %d pixels of its rect changed"), *Row.ToString(),
			Parent != nullptr ? *Parent->GetName() : TEXT("nothing"), *Changed));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

#endif
