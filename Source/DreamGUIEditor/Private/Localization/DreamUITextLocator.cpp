// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Localization/DreamUITextLocator.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamUIWidgetPicking.h"
#include "Designer/DreamWidgetDesignerViewportClient.h"
#include "Designer/SDreamWidgetDesignerViewport.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Kismet/GameplayStatics.h"
#include "SceneView.h"

#if WITH_DREAMLOC
#include "DreamLocTextLocator.h"
#include "Features/IModularFeatures.h"
#endif

UDreamText* DreamUITextLocator::FindTextAlongRay(const UWorld* InWorld, TConstArrayView<UDreamWidget*> InWidgets, const FVector& InLineStart, const FVector& InLineEnd)
{
	TArray<FDreamUIWidgetPickHit> Hits;
	DreamUIWidgetPicking::RaycastWidgetRects(InWorld, InWidgets, InLineStart, InLineEnd, Hits);
	for (const FDreamUIWidgetPickHit& Hit : Hits)
	{
		if (UDreamText* Text = Hit.Widget != nullptr ? Cast<UDreamText>(Hit.Widget->GetVisual()) : nullptr)
		{
			return Text;
		}
	}
	return nullptr;
}

void DreamUITextLocator::CollectAll(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets)
{
	if (!IsValid(InRoot))
	{
		return;
	}
	OutWidgets.Add(InRoot);
	for (UDreamWidget* Child : InRoot->GetChildren())
	{
		CollectAll(Child, OutWidgets);
	}
}

bool DreamUITextLocator::Describe(const UDreamText* InText, FString& OutNamespace, FString& OutKey, FString& OutWhyNot)
{
	OutNamespace.Reset();
	OutKey.Reset();
	OutWhyNot.Reset();
	if (InText == nullptr)
	{
		return false;
	}
	const FText& Text = InText->GetText();
	const TOptional<FString> Key = FTextInspector::GetKey(Text);
	if (Key.IsSet() && !Key->IsEmpty())
	{
		OutNamespace = TextNamespaceUtil::StripPackageNamespace(FTextInspector::GetNamespace(Text).Get(FString()));
		OutKey = Key.GetValue();
		return true;
	}
	OutWhyNot = Text.IsEmpty() ? TEXT("An empty DreamText: nothing to translate")
		: Text.IsInitializedFromString() || Text.IsCultureInvariant() ? TEXT("A DreamText that is not localizable (@noloc, or set from a string in code)")
		: TEXT("A DreamText built from other texts (a format, a number): the texts it is built from have the keys");
	return false;
}

void DreamUITextLocator::GetWorldCorners(const UDreamWidget* InWidget, FVector OutCorners[4])
{
	// A widget's rect lives in its plane, local Y across and Z up.
	const float Left = InWidget->GetLocalSpaceLeft();
	const float Bottom = InWidget->GetLocalSpaceBottom();
	const float Right = Left + InWidget->GetWidth();
	const float Top = Bottom + InWidget->GetHeight();
	const FTransform& Transform = InWidget->GetWorldTransform();
	OutCorners[0] = Transform.TransformPosition(FVector(0.0, Left, Bottom));
	OutCorners[1] = Transform.TransformPosition(FVector(0.0, Right, Bottom));
	OutCorners[2] = Transform.TransformPosition(FVector(0.0, Right, Top));
	OutCorners[3] = Transform.TransformPosition(FVector(0.0, Left, Top));
}

#if WITH_DREAMLOC
namespace DreamUITextLocatorLocal
{
	const FName ViewportType(TEXT("SViewport"));
	const FName DesignerViewportType(TEXT("SDreamWidgetDesignerViewport"));

	/** The widgets of InWorld's root canvases that pass InWanted, nested instances' included. */
	TArray<UDreamWidget*> WidgetsOf(UWorld* InWorld, TFunctionRef<bool(const UDreamCanvas&)> InWanted)
	{
		TArray<UDreamWidget*> Out;
		UDreamUIManagerWorldSubsystem* Manager = InWorld != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(InWorld) : nullptr;
		if (Manager == nullptr)
		{
			return Out;
		}
		for (const TWeakObjectPtr<UDreamCanvas>& Weak : Manager->GetAllCanvasArray())
		{
			UDreamCanvas* Canvas = Weak.Get();
			if (Canvas != nullptr && Canvas->IsRootCanvas() && InWanted(*Canvas))
			{
				DreamUITextLocator::CollectAll(Canvas->GetWidget(), Out);
			}
		}
		return Out;
	}

	/** A text found: its key, and its rect on the desktop from where each corner lands in the viewport. */
	bool Fill(const UDreamText* InText, TFunctionRef<FVector2D(const FVector&)> InToViewport, const FVector2D& InViewportOrigin, FDreamLocLocatedText& OutText)
	{
		DreamUITextLocator::Describe(InText, OutText.Namespace, OutText.Key, OutText.WhyNot);
		const UDreamWidget* Shown = InText->GetWidget();
		if (Shown == nullptr)
		{
			return true;
		}
		FVector Corners[4];
		DreamUITextLocator::GetWorldCorners(Shown, Corners);
		OutText.Rect = FBox2D(ForceInit);
		for (const FVector& Corner : Corners)
		{
			OutText.Rect += InViewportOrigin + InToViewport(Corner);
		}
		return true;
	}

	/** Screen-space canvases: their own view, a ray through the point of the part of the viewport they fill. */
	bool LocateOnScreenSpace(UWorld* InWorld, const FVector2D& InPixel, const FVector2D& InOrigin, FDreamLocLocatedText& OutText)
	{
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld);
		if (Manager == nullptr)
		{
			return false;
		}
		for (const TWeakObjectPtr<UDreamCanvas>& Weak : Manager->GetAllCanvasArray())
		{
			UDreamCanvas* Canvas = Weak.Get();
			if (Canvas == nullptr || !Canvas->IsRootCanvas() || !Canvas->IsRenderToScreenSpace() || !Canvas->ContainsViewportPoint(InPixel))
			{
				continue;
			}
			const FMatrix ViewProjection = Canvas->GetViewProjectionMatrix();
			const FIntRect Rect = Canvas->GetViewportRect();
			const FVector2D Size = FVector2D(Rect.Size());
			if (Size.X <= 0.0 || Size.Y <= 0.0)
			{
				continue;
			}
			// As UDreamScreenSpaceRaycaster makes its ray: the point as -1..1 across the canvas's part, both ends deprojected.
			const FVector2D Point01 = (InPixel - FVector2D(Rect.Min)) / Size;
			const double X = (Point01.X - 0.5) * 2.0;
			const double Y = ((1.0 - Point01.Y) - 0.5) * 2.0;
			const FMatrix Inverse = ViewProjection.InverseFast();
			const FVector4 Near = Inverse.TransformFVector4(FVector4(X, Y, 1.0, 1.0));
			const FVector4 Far = Inverse.TransformFVector4(FVector4(X, Y, 0.5, 1.0));
			if (FMath::IsNearlyZero(Near.W) || FMath::IsNearlyZero(Far.W))
			{
				continue;
			}
			const FVector Start = FVector(Near) / Near.W;
			const FVector Direction = (FVector(Far) / Far.W - Start).GetSafeNormal();
			TArray<UDreamWidget*> Widgets;
			DreamUITextLocator::CollectAll(Canvas->GetWidget(), Widgets);
			if (const UDreamText* Text = DreamUITextLocator::FindTextAlongRay(InWorld, Widgets, Start, Start + Direction * 1000000.0))
			{
				return Fill(Text, [&ViewProjection, &Rect, &Size](const FVector& InWorldPoint)
				{
					const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(InWorldPoint, 1.0));
					const double W = FMath::IsNearlyZero(Clip.W) ? 1.0 : Clip.W;
					return FVector2D(Rect.Min) + FVector2D((Clip.X / W * 0.5 + 0.5) * Size.X, (1.0 - (Clip.Y / W * 0.5 + 0.5)) * Size.Y);
				}, InOrigin, OutText);
			}
		}
		return false;
	}

	class FLocator : public IDreamLocTextLocator
	{
	public:
		virtual bool LocateText(TConstArrayView<TSharedRef<SWidget>> InWidgets, const FVector2D& InScreenPosition, FDreamLocLocatedText& OutText) override
		{
			TSharedPtr<SWidget> Viewport;
			for (int32 Index = InWidgets.Num() - 1; Index >= 0 && !Viewport.IsValid(); --Index)
			{
				if (InWidgets[Index]->GetType() == ViewportType)
				{
					Viewport = InWidgets[Index];
				}
			}
			if (!Viewport.IsValid())
			{
				return false;
			}
			// Desktop pixels to the viewport's: its tick-space geometry is on the desktop.
			const FGeometry Geometry = Viewport->GetTickSpaceGeometry();
			const FVector2D Origin = FVector2D(Geometry.GetAbsolutePosition());
			const FVector2D Pixel = InScreenPosition - Origin;

			// The designer: its own pick ray.
			for (const TSharedRef<SWidget>& Widget : InWidgets)
			{
				if (Widget->GetType() != DesignerViewportType)
				{
					continue;
				}
				const TSharedPtr<FDreamWidgetDesignerViewportClient> Client = StaticCastSharedRef<SDreamWidgetDesignerViewport>(Widget)->GetViewportClient();
				FVector Start;
				FVector End;
				if (!Client.IsValid() || Client->Viewport == nullptr || !Client->ComputePickRay(FMath::RoundToInt(Pixel.X), FMath::RoundToInt(Pixel.Y), Start, End))
				{
					return false;
				}
				UWorld* World = Client->GetWorld();
				const TArray<UDreamWidget*> Widgets = WidgetsOf(World, [](const UDreamCanvas&) { return true; });
				const UDreamText* Text = DreamUITextLocator::FindTextAlongRay(World, Widgets, Start, End);
				if (Text == nullptr)
				{
					return false;
				}
				FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Client->Viewport, Client->GetScene(), Client->EngineShowFlags));
				const FSceneView* View = Client->CalcSceneView(&ViewFamily);
				return Fill(Text, [View](const FVector& InWorldPoint)
				{
					FVector2D Out = FVector2D::ZeroVector;
					if (View != nullptr)
					{
						View->WorldToPixel(InWorldPoint, Out);
					}
					return Out;
				}, Origin, OutText);
			}

			// A game's viewport (PIE): its screen-space canvases first, drawn over the world; then the first player's view.
			if (GEngine == nullptr)
			{
				return false;
			}
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.GameViewport == nullptr || Context.GameViewport->GetGameViewportWidget() != Viewport)
				{
					continue;
				}
				UWorld* World = Context.World();
				if (World == nullptr)
				{
					return false;
				}
				if (LocateOnScreenSpace(World, Pixel, Origin, OutText))
				{
					return true;
				}
				APlayerController* Player = World->GetFirstPlayerController();
				FVector Start;
				FVector Direction;
				if (Player == nullptr || !UGameplayStatics::DeprojectScreenToWorld(Player, Pixel, Start, Direction))
				{
					return false;
				}
				const TArray<UDreamWidget*> Widgets = WidgetsOf(World, [](const UDreamCanvas& InCanvas) { return !InCanvas.IsRenderToScreenSpace(); });
				const UDreamText* Text = DreamUITextLocator::FindTextAlongRay(World, Widgets, Start, Start + Direction * 1000000.0);
				if (Text == nullptr)
				{
					return false;
				}
				return Fill(Text, [Player](const FVector& InWorldPoint)
				{
					FVector2D Out = FVector2D::ZeroVector;
					Player->ProjectWorldLocationToScreen(InWorldPoint, Out);
					return Out;
				}, Origin, OutText);
			}
			return false;
		}
	};

	TUniquePtr<FLocator> Locator;
}
#endif

void DreamUITextLocator::RegisterWithDreamLoc()
{
#if WITH_DREAMLOC
	using namespace DreamUITextLocatorLocal;
	if (!Locator.IsValid())
	{
		Locator = MakeUnique<FLocator>();
		IModularFeatures::Get().RegisterModularFeature(IDreamLocTextLocator::GetModularFeatureName(), Locator.Get());
	}
#endif
}

void DreamUITextLocator::UnregisterFromDreamLoc()
{
#if WITH_DREAMLOC
	using namespace DreamUITextLocatorLocal;
	if (Locator.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(IDreamLocTextLocator::GetModularFeatureName(), Locator.Get());
		Locator.Reset();
	}
#endif
}
