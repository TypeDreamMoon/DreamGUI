// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Extensions/DreamUMGWidget.h"
#include "Core/DreamUIWorldContext.h"
#include "Blueprint/UserWidget.h"
#include "DreamTweenBPLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/SViewport.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteInfo.h"
#include "Input/HittestGrid.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Core/DreamUICustomMeshSource.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"

#define LOCTEXT_NAMESPACE "UIWidget"

UDreamUMGWidget::UDreamUMGWidget(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	
}

bool UDreamUMGWidget::SupportDrawCallBatching()const
{
	if (IsValid(CustomMesh))
	{
		return CustomMesh->SupportDrawcallBatching();
	}
	else
	{
		return true;
	}
}
void UDreamUMGWidget::OnBeforeCreateOrUpdateGeometry()
{

}
UTexture* UDreamUMGWidget::GetTextureToCreateGeometry()
{
	return RenderTarget;
}
void UDreamUMGWidget::OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (IsValid(CustomMesh))
	{
		CustomMesh->UIGeo = &InGeo;
		CustomMesh->OnFillMesh(this, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
	}
	else
	{
		auto Widget = GetWidget();
		static FDreamUISpriteInfo SpriteInfo;
		FDreamUIGeometry::UpdateUIRectSimpleVertex(&InGeo,
			Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), SpriteInfo, Widget->GetRenderCanvas(), this, GetFinalColor(),
			InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged
		);
	}
}



void UDreamUMGWidget::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(TickMode != ETickMode::Disabled);
	InitWidget();
}

void UDreamUMGWidget::EndPlay()
{
	Super::EndPlay();

	UDreamTweenBPLibrary::KillAllTweensOnTarget(this, this);
	ReleaseResources();
}

void UDreamUMGWidget::OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	// If the InLevel is null, it's a signal that the entire world is about to disappear, so
	// go ahead and remove this widget from the viewport, it could be holding onto too many
	// dangerous actor references that won't carry over into the next world.
	if (InLevel == nullptr && InWorld == GetWorld())
	{
		ReleaseResources();
	}
}

void UDreamUMGWidget::OnRegister()
{
	Super::OnRegister();

#if !UE_SERVER
	FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &ThisClass::OnLevelRemovedFromWorld);

	if (!IsRunningDedicatedServer())
	{
		const bool bIsGameWorld = DreamUI::IsGameWorld(this);

		if (!WidgetRenderer && !GUsingNullRHI)
		{
			WidgetRenderer = new FWidgetRenderer(bApplyGammaCorrection);
		}

#if WITH_EDITOR
		if (!bIsGameWorld)
		{
			InitWidget();
		}
#endif
	}
#endif // !UE_SERVER

#if WITH_EDITOR
	if (!DreamUI::IsGameWorld(this))
	{
		
	}
#endif
}

void UDreamUMGWidget::SetWindowFocusable(bool bInWindowFocusable)
{
	bWindowFocusable = bInWindowFocusable;
	if (SlateWindow.IsValid())
	{
		SlateWindow->SetIsFocusable(bWindowFocusable);
	}
};

EVisibility UDreamUMGWidget::ConvertWindowVisibilityToVisibility(EWindowVisibility visibility)
{
	switch (visibility)
	{
	case EWindowVisibility::Visible:
		return EVisibility::Visible;
	case EWindowVisibility::SelfHitTestInvisible:
		return EVisibility::SelfHitTestInvisible;
	default:
		checkNoEntry();
		return EVisibility::SelfHitTestInvisible;
	}
}

void UDreamUMGWidget::OnWidgetVisibilityChanged(ESlateVisibility InVisibility)
{
	ensure(TickMode != ETickMode::Enabled);
	ensure(UmgWidget);
	ensure(bOnWidgetVisibilityChangedRegistered);

	if (InVisibility != ESlateVisibility::Collapsed && InVisibility != ESlateVisibility::Hidden)
	{
		if (ShouldReenableComponentTickWhenWidgetBecomesVisible())
		{
			SetComponentTickEnabled(true);
		}

		if (bOnWidgetVisibilityChangedRegistered)
		{
			UmgWidget->OnNativeVisibilityChanged.RemoveAll(this);
			bOnWidgetVisibilityChangedRegistered = false;
		}
	}
}

void UDreamUMGWidget::SetWindowVisibility(EWindowVisibility InVisibility)
{
	WindowVisibility = InVisibility;
	if (SlateWindow.IsValid())
	{
		SlateWindow->SetVisibility(ConvertWindowVisibilityToVisibility(WindowVisibility));
	}

	if (IsWidgetVisible())
	{
		if (ShouldReenableComponentTickWhenWidgetBecomesVisible())
		{
			SetComponentTickEnabled(true);
		}

		if (bOnWidgetVisibilityChangedRegistered)
		{
			if (UmgWidget)
			{
				UmgWidget->OnNativeVisibilityChanged.RemoveAll(this);
			}
			bOnWidgetVisibilityChangedRegistered = false;
		}
	}
}

void UDreamUMGWidget::SetTickMode(ETickMode InTickMode)
{
	TickMode = InTickMode;
	SetComponentTickEnabled(InTickMode != ETickMode::Disabled);
}

bool UDreamUMGWidget::IsWidgetVisible() const
{
	//  If we are in World Space, if the component or the SlateWindow is not visible the Widget is not visible.
	if ((!GetWidget()->GetRenderVisibleInHierarchy() || !SlateWindow.IsValid() || !SlateWindow->GetVisibility().IsVisible()))
	{
		return false;
	}

	// If we have a UUserWidget check its visibility
	if (UmgWidget)
	{
		return UmgWidget->IsVisible();
	}

	// If we use a SlateWidget check its visibility
	return SlateWidget.IsValid() && SlateWidget->GetVisibility().IsVisible();
}

void UDreamUMGWidget::OnUnregister()
{
#if !UE_SERVER
	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
#endif

#if WITH_EDITOR
	if (!DreamUI::IsGameWorld(this))
	{
		ReleaseResources();
	}
#endif

	Super::OnUnregister();
}

void UDreamUMGWidget::BeginDestroy()
{
	ReleaseResources();
	Super::BeginDestroy();
}

void UDreamUMGWidget::ReleaseResources()
{
	if (UmgWidget)
	{
		if (bOnWidgetVisibilityChangedRegistered)
		{
			UmgWidget->OnNativeVisibilityChanged.RemoveAll(this);
			bOnWidgetVisibilityChangedRegistered = false;
		}
		UmgWidget = nullptr;
	}

	if (SlateWidget.IsValid())
	{
		SlateWidget.Reset();
	}

	if (WidgetRenderer)
	{
		BeginCleanup(WidgetRenderer);
		WidgetRenderer = nullptr;
	}

	UnregisterWindow();
}

void UDreamUMGWidget::RegisterWindow()
{
	if (SlateWindow.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().RegisterVirtualWindow(SlateWindow.ToSharedRef());
		}

		if (UmgWidget && !UmgWidget->IsDesignTime())
		{
			if (UWorld* LocalWorld = GetWorld())
			{
				if (LocalWorld->IsGameWorld())
				{
					if (UGameInstance* GameInstance = LocalWorld->GetGameInstance())
					{
						if (UGameViewportClient* GameViewportClient = GameInstance->GetGameViewportClient())
						{
							SlateWindow->AssignParentWidget(GameViewportClient->GetGameViewportWidget());
						}
					}
				}
			}
		}
	}
}

void UDreamUMGWidget::UnregisterWindow()
{
	if (SlateWindow.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterVirtualWindow(SlateWindow.ToSharedRef());
		}

		SlateWindow.Reset();
	}
}

void UDreamUMGWidget::TickComponent(float DeltaTime)
{
	if (IsRunningDedicatedServer())
	{
		SetTickMode(ETickMode::Disabled);
		return;
	}


#if !UE_SERVER
	if (!IsRunningDedicatedServer())
	{
		UpdateWidget();

		// There is no Widget set and we already rendered an empty widget. No need to continue.
		if (UmgWidget == nullptr && !SlateWidget.IsValid() && bRenderCleared)
		{
			return;
		}

		// We have a Widget, it's invisible and we are in automatic or disabled TickMode, we disable ticking and register a callback to know if visibility changes.
		if (UmgWidget && TickMode != ETickMode::Enabled && !IsWidgetVisible())
		{
			SetComponentTickEnabled(false);
			if (!bOnWidgetVisibilityChangedRegistered)
			{
				UmgWidget->OnNativeVisibilityChanged.AddUObject(this, &UDreamUMGWidget::OnWidgetVisibilityChanged);
				bOnWidgetVisibilityChangedRegistered = true;
			}
		}

		// Tick Mode is Disabled, we stop here and Disable the Component Tick
		if (TickMode == ETickMode::Disabled && !bRedrawRequested)
		{
			SetComponentTickEnabled(false);
			return;
		}

		if (ShouldDrawWidget())
		{
			// Calculate the actual delta time since we last drew, this handles the case where we're ticking when
			// the world is paused, this also takes care of the case where the widget component is rendering at
			// a different rate than the rest of the world.
			const float DeltaTimeFromLastDraw = LastWidgetRenderTime == 0 ? 0 : (GetCurrentTime() - LastWidgetRenderTime);
			DrawWidgetToRenderTarget(DeltaTimeFromLastDraw);

			// We draw an empty widget.
			if (UmgWidget == nullptr && !SlateWidget.IsValid())
			{
				bRenderCleared = true;
			}
		}

	}
#endif // !UE_SERVER
}

void UDreamUMGWidget::SetComponentTickEnabled(bool bEnable)
{
	if (bIsTickEnabled == bEnable)return;
	if (GetWorld())
	{
		if (bEnable)
		{
#if WITH_EDITOR
			if (!DreamUI::IsGameWorld(this))
			{
				if (auto DreamUIManagerObject = UDreamUIManagerObject::GetInstance(true))
				{
					EditorTickHandle = DreamUIManagerObject->GetEditorTickDelegate().AddUObject(this, &UDreamUMGWidget::TickComponent);
				}
			}
			else
#endif
			{
				Tweener = UDreamTweenBPLibrary::UpdateCall(this, FDreamTweenUpdateDelegate::CreateUObject(this, &UDreamUMGWidget::TickComponent));
			}
		}
		else
		{
#if WITH_EDITOR
			if (!DreamUI::IsGameWorld(this))
			{
				if (EditorTickHandle.IsValid())
				{
					if (auto DreamUIManagerObject = UDreamUIManagerObject::GetInstance(false))
					{
						DreamUIManagerObject->GetEditorTickDelegate().Remove(EditorTickHandle);
					}
				}
			}
			else
#endif
			{
				if (Tweener.IsValid())
					UDreamTweenBPLibrary::KillIfIsTweening(this, Tweener.Get());
			}
		}
		bIsTickEnabled = bEnable;
	}
}

bool UDreamUMGWidget::ShouldReenableComponentTickWhenWidgetBecomesVisible() const
{
	return (TickMode != ETickMode::Disabled) || bRedrawRequested;
}

bool UDreamUMGWidget::ShouldDrawWidget() const
{
	const float RenderTimeThreshold = .5f;
	if (auto RenderCanvas = GetWidget()->GetRenderCanvas())
	{
		// If we don't tick when off-screen, don't bother ticking if it hasn't been rendered recently
		if (TickWhenOffscreen || GetWorld()->TimeSince(GetWorld()->LastRenderTime) <= RenderTimeThreshold)
		{
			if ((GetCurrentTime() - LastWidgetRenderTime) >= RedrawTime)
			{
				return bManuallyRedraw ? bRedrawRequested : true;
			}
		}
	}

	return false;
}

void UDreamUMGWidget::DrawWidgetToRenderTarget(float DeltaTime)
{
	if (GUsingNullRHI)
	{
		return;
	}

	if (!SlateWindow.IsValid())
	{
		return;
	}

	if (!WidgetRenderer)
	{
		return;
	}

	auto Widget = GetWidget();
	auto DrawSize = FIntPoint(Widget->GetWidth() * ResolutionScale, Widget->GetHeight() * ResolutionScale);
	static const int32 MaxAllowedDrawSize = GetMax2DTextureDimension();
	if (DrawSize.X <= 0 || DrawSize.Y <= 0)
	{
		return;
	}
	DrawSize.X = FMath::Min(DrawSize.X, MaxAllowedDrawSize);
	DrawSize.Y = FMath::Min(DrawSize.Y, MaxAllowedDrawSize);

	const FIntPoint PreviousDrawSize = CurrentDrawSize;
	CurrentDrawSize = DrawSize;

	const float DrawScale = 1.0f;

	SlateWindow->SlatePrepass(DrawScale);

	WidgetRenderer->SetIsPrepassNeeded(true);

	UpdateRenderTarget(CurrentDrawSize);

	// The render target could be null if the current draw size is zero
	if (RenderTarget)
	{
		bRedrawRequested = false;

		WidgetRenderer->DrawWindow(
			RenderTarget,
			SlateWindow->GetHittestGrid(),
			SlateWindow.ToSharedRef(),
			DrawScale,
			CurrentDrawSize,
			DeltaTime);

		LastWidgetRenderTime = GetCurrentTime();

		if (TickMode == ETickMode::Disabled && bIsTickEnabled)
		{
			SetComponentTickEnabled(false);
		}
	}
}

double UDreamUMGWidget::GetCurrentTime() const
{
	return (TimingPolicy == EWidgetTimingPolicy::RealTime) ? FApp::GetCurrentTime() : static_cast<double>(GetWorld()->GetTimeSeconds());
}

#if WITH_EDITOR

bool UDreamUMGWidget::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();
	}

	return Super::CanEditChange(InProperty);
}

void UDreamUMGWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName WidgetClassName("WidgetClass");
		static FName bWindowFocusableName(TEXT("bWindowFocusable"));
		static FName WindowVisibilityName(TEXT("WindowVisibility"));

		auto PropertyName = Property->GetFName();

		if (PropertyName == WidgetClassName)
		{
			UmgWidget = nullptr;

			UpdateWidget();
		}
		else if (PropertyName == bWindowFocusableName)
		{
			SetWindowFocusable(bWindowFocusable);
		}
		else if (PropertyName == WindowVisibilityName)
		{
			SetWindowVisibility(WindowVisibility);
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUMGWidget, CustomMesh))
		{
			if (IsValid(CustomMesh))//custom mesh use geometry raycast to get precise uv
			{
				this->SetRaycastType(EDreamVisualRaycastType::Mesh);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

void UDreamUMGWidget::InitWidget()
{
	if (IsRunningDedicatedServer())
	{
		SetTickMode(ETickMode::Disabled);
		return;
	}

	// Don't do any work if Slate is not initialized
	if (FSlateApplication::IsInitialized())
	{
		if (UWorld* World = GetWorld())
		{
			if (WidgetClass && UmgWidget == nullptr && !World->bIsTearingDown)
			{
				UmgWidget = CreateWidget(World, WidgetClass);
				SetTickMode(TickMode);
			}
		}
	}
}

void UDreamUMGWidget::SetManuallyRedraw(bool bUseManualRedraw)
{
	bManuallyRedraw = bUseManualRedraw;
}

UUserWidget* UDreamUMGWidget::GetUMGWidget() const
{
	return UmgWidget;
}

void UDreamUMGWidget::SetWidget(UUserWidget* InWidget)
{
	if (InWidget != nullptr)
	{
		SetSlateWidget(nullptr);
	}

	UmgWidget = InWidget;

	UpdateWidget();
}

void UDreamUMGWidget::SetSlateWidget(const TSharedPtr<SWidget>& InSlateWidget)
{
	if (UmgWidget != nullptr)
	{
		SetWidget(nullptr);
	}

	if (SlateWidget.IsValid())
	{
		SlateWidget.Reset();
	}

	SlateWidget = InSlateWidget;

	UpdateWidget();
}

void UDreamUMGWidget::UpdateWidget()
{
	// Don't do any work if Slate is not initialized
	if (FSlateApplication::IsInitialized() && IsValid(this))
	{
		// Look for a UMG widget set
		TSharedPtr<SWidget> NewSlateWidget;
		if (UmgWidget)
		{
			NewSlateWidget = UmgWidget->TakeWidget();
		}

		// Create the SlateWindow if it doesn't exists
		bool bNeededNewWindow = false;
		if (!SlateWindow.IsValid())
		{
			SlateWindow = SNew(SVirtualWindow).Size(CurrentDrawSize);
			SlateWindow->SetIsFocusable(bWindowFocusable);
			SlateWindow->SetVisibility(ConvertWindowVisibilityToVisibility(WindowVisibility));
			RegisterWindow();

			bNeededNewWindow = true;
		}

		SlateWindow->Resize(CurrentDrawSize);

		// Add the UMG or SlateWidget to the Component
		bool bWidgetChanged = false;

		// We Get here if we have a UMG Widget
		if (NewSlateWidget.IsValid())
		{
			if (NewSlateWidget != CurrentSlateWidget || bNeededNewWindow)
			{
				CurrentSlateWidget = NewSlateWidget;
				SlateWindow->SetContent(NewSlateWidget.ToSharedRef());
				bRenderCleared = false;
				bWidgetChanged = true;
			}
		}
		// If we don't have one, we look for a Slate Widget
		else if (SlateWidget.IsValid())
		{
			if (SlateWidget != CurrentSlateWidget || bNeededNewWindow)
			{
				CurrentSlateWidget = SlateWidget;
				SlateWindow->SetContent(SlateWidget.ToSharedRef());
				bRenderCleared = false;
				bWidgetChanged = true;
			}
		}
		else
		{
			if (CurrentSlateWidget != SNullWidget::NullWidget)
			{
				CurrentSlateWidget = SNullWidget::NullWidget;
				bRenderCleared = false;
				bWidgetChanged = true;
			}
			SlateWindow->SetContent(SNullWidget::NullWidget);
		}

		if (bNeededNewWindow || bWidgetChanged)
		{
			SetComponentTickEnabled(true);
		}
	}
}

void UDreamUMGWidget::UpdateRenderTarget(FIntPoint DesiredRenderTargetSize)
{
	bool bClearColorChanged = false;

	FLinearColor ActualBackgroundColor = BackgroundColor;

	if (DesiredRenderTargetSize.X != 0 && DesiredRenderTargetSize.Y != 0)
	{
		const EPixelFormat requestedFormat = FSlateApplication::Get().GetRenderer()->GetSlateRecommendedColorFormat();

		if (RenderTarget == nullptr)
		{
			RenderTarget = NewObject<UTextureRenderTarget2D>(this);
			RenderTarget->ClearColor = ActualBackgroundColor;
			RenderTarget->AddressX = TextureAddress::TA_Clamp;
			RenderTarget->AddressY = TextureAddress::TA_Clamp;
			RenderTarget->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, requestedFormat, false);

			MarkTextureDirty();
		}
		else
		{
			bClearColorChanged = (RenderTarget->ClearColor != ActualBackgroundColor);

			// Update the clear color or format
			if (bClearColorChanged || RenderTarget->SizeX != DesiredRenderTargetSize.X || RenderTarget->SizeY != DesiredRenderTargetSize.Y)
			{
				RenderTarget->ClearColor = ActualBackgroundColor;
				RenderTarget->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, PF_B8G8R8A8, false);
				RenderTarget->UpdateResourceImmediate();
			}
		}
	}
}

void UDreamUMGWidget::GetLocalHitLocation(int32 InHitFaceIndex, const FVector& InWorldHitLocation, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutLocalWidgetHitLocation) const
{
	if (IsValid(CustomMesh))
	{
		FVector2D HitUV;
		if (CustomMesh->GetHitUV(this, InHitFaceIndex, InWorldHitLocation, InLineStart, InLineEnd, HitUV))
		{
			OutLocalWidgetHitLocation = HitUV * CurrentDrawSize;
		}
	}
	else
	{
		auto Widget = GetWidget();
		// Find the hit location on the component
		FVector ComponentHitLocation = Widget->GetWorldTransform().InverseTransformPosition(InWorldHitLocation);

		// Convert the 3D position of component space, into the 2D equivalent
		auto LocationRelativeToLeftBottom = FVector2D(ComponentHitLocation.Y, ComponentHitLocation.Z) - Widget->GetLocalSpaceLeftBottomPoint();
		auto Location01 = LocationRelativeToLeftBottom / FVector2D(Widget->GetWidth(), Widget->GetHeight());
		Location01.Y = 1.0f - Location01.Y;

		OutLocalWidgetHitLocation = Location01 * CurrentDrawSize;
	}
}

UUserWidget* UDreamUMGWidget::GetUserWidgetObject() const
{
	return UmgWidget;
}

UTextureRenderTarget2D* UDreamUMGWidget::GetRenderTarget() const
{
	return RenderTarget;
}

const TSharedPtr<SWidget>& UDreamUMGWidget::GetSlateWidget() const
{
	return SlateWidget;
}


TArray<FWidgetAndPointer> UDreamUMGWidget::GetHitWidgetPath(FVector2D WidgetSpaceHitCoordinate, bool bIgnoreEnabledStatus, float CursorRadius /*= 0.0f*/)
{
	const FVector2D& LocalHitLocation = WidgetSpaceHitCoordinate;
	const FVirtualPointerPosition VirtualMouseCoordinate(LocalHitLocation, LastLocalHitLocation);

	// Cache the location of the hit
	LastLocalHitLocation = LocalHitLocation;

	TArray<FWidgetAndPointer> ArrangedWidgets;
	if (SlateWindow.IsValid())
	{
		// @todo slate - widget components would need to be associated with a user for this to be anthing valid
		const int32 UserIndex = INDEX_NONE;
		ArrangedWidgets = SlateWindow->GetHittestGrid().GetBubblePath(LocalHitLocation, CursorRadius, bIgnoreEnabledStatus, UserIndex);

		for (FWidgetAndPointer& ArrangedWidget : ArrangedWidgets)
		{
			ArrangedWidget.SetPointerPosition(VirtualMouseCoordinate);
		}
	}

	return ArrangedWidgets;
}

TSharedPtr<SWindow> UDreamUMGWidget::GetSlateWindow() const
{
	return SlateWindow;
}

FVector2D UDreamUMGWidget::GetCurrentDrawSize() const
{
	return CurrentDrawSize;
}

void UDreamUMGWidget::RequestRenderUpdate()
{
	bRedrawRequested = true;
	if (TickMode == ETickMode::Disabled)
	{
		SetComponentTickEnabled(true);
	}
}

void UDreamUMGWidget::SetBackgroundColor(const FLinearColor NewBackgroundColor)
{
	if (NewBackgroundColor != this->BackgroundColor)
	{
		this->BackgroundColor = NewBackgroundColor;
		this->MarkColorDirty();
	}
}

TSharedPtr< SWindow > UDreamUMGWidget::GetVirtualWindow() const
{
	return StaticCastSharedPtr<SWindow>(SlateWindow);
}

void UDreamUMGWidget::SetWidgetClass(TSubclassOf<UUserWidget> InWidgetClass)
{
	if (WidgetClass != InWidgetClass)
	{
		WidgetClass = InWidgetClass;

		if (FSlateApplication::IsInitialized())
		{
			if (WidgetClass)
			{
				UUserWidget* NewWidget = CreateWidget(GetWorld(), WidgetClass);
				SetWidget(NewWidget);
			}
			else
			{
				SetWidget(nullptr);
			}
		}
	}
}

/*
 * The hosted widget is the only thing here that knows how big it wants to be, and UMG already
 * measured it: Slate caches a desired size on every widget during prepass, and this component runs
 * one on the virtual window each time it redraws. So the answer exists and costs a weak-pointer pin
 * plus a field read -- no TakeWidget(), which would BUILD the Slate tree, and nothing that would
 * make a measure pass construct anything.
 *
 * Three states have to stay distinguishable, and only one of them is a size:
 *
 *   - Nothing hosted. No world means InitWidget never created the UUserWidget, which is the state of
 *     every one of these in a headless test and in a Blueprint authoring tree. No opinion.
 *   - Hosted but never prepassed. SWidget::GetDesiredSize returns zero when bDesiredSizeSet is
 *     false, and zero here is indistinguishable from a widget that genuinely wants no room. Taking
 *     it at face value is how a UMG panel would collapse on the frame before its first draw. No
 *     opinion.
 *   - Measured. Convert and answer.
 *
 * The conversion is the counter-intuitive bit. ResolutionScale does NOT scale the element on
 * screen -- the quad is always the widget's rect. It scales the render target, so the virtual
 * window is laid out at rect * ResolutionScale Slate units for the same UI rectangle. At scale 2 a
 * widget wanting 100 Slate units is asking for 50 UI units, so the desired size has to be divided
 * back out or a supersampled widget measures twice as large as it draws.
 */
FVector2f UDreamUMGWidget::MeasureHostedWidget() const
{
	// SlateWidget is set only by SetSlateWidget, and SetWidget/SetSlateWidget clear each other, so
	// at most one of these two is ever live.
	TSharedPtr<SWidget> Hosted = SlateWidget;
	if (!Hosted.IsValid() && IsValid(UmgWidget))
	{
		// The already-built Slate tree if there is one. Not TakeWidget(), which builds it.
		Hosted = UmgWidget->GetCachedWidget();
	}
	if (!Hosted.IsValid())
	{
		return FVector2f(-1.0f, -1.0f);
	}

	const UE::Slate::FDeprecateVector2DResult Desired = Hosted->GetDesiredSize();
	const float Scale = ResolutionScale > 0.0f ? ResolutionScale : 1.0f;
	return FVector2f(Desired.X > 0.0f ? Desired.X / Scale : -1.0f,
		Desired.Y > 0.0f ? Desired.Y / Scale : -1.0f);
}

float UDreamUMGWidget::GetPreferredWidth() const
{
	return MeasureHostedWidget().X;
}

float UDreamUMGWidget::GetPreferredHeight() const
{
	return MeasureHostedWidget().Y;
}

#undef LOCTEXT_NAMESPACE

// A custom mesh that supplies its OWN source, which is why this one is spellable where the bare
// UDreamCustomMesh is not: the class it hosts is a property, and a property is something a .dui
// can write.
DECLARE_DREAM_GUI_VISUAL("UMGWidget", UDreamUMGWidget)
