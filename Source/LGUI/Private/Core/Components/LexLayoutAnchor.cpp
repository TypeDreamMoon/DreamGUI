// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayoutAnchor.h"
#include "LGUI.h"
#include "LTweenManager.h"
#include "Core/LGUISettings.h"
#include "Core/Components/LexWidget.h"

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

ULexLayoutAnchor::ULexLayoutAnchor()
{
}

TSubclassOf<ULexLayoutSlot> ULexLayoutAnchor::GetSlotClass() const
{
	return ULexLayoutAnchorSlot::StaticClass();
}

#if WITH_EDITOR
void ULexLayoutAnchor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}
bool ULexLayoutAnchor::CanEditChange(const FProperty* InProperty) const
{
	return UObject::CanEditChange(InProperty);
}
#endif

void ULexLayoutAnchor::OnUpdateLayout()
{
	
}

ULexLayoutAnchorSlot::ULexLayoutAnchorSlot()
{
}

#if WITH_EDITOR
void ULexLayoutAnchorSlot::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CalculateTransformFromAnchor();
}

bool ULexLayoutAnchorSlot::CanEditChange(const FProperty* InProperty) const
{
	return Super::CanEditChange(InProperty);
}
#endif

void ULexLayoutAnchorSlot::PostInitProperties()
{
	Super::PostInitProperties();
}

void ULexLayoutAnchorSlot::BeginDestroy()
{
	Super::BeginDestroy();
}

void ULexLayoutAnchorSlot::OnParentTransformChanged()
{
	
}

void ULexLayoutAnchorSlot::OnParentDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	bool ChildWidthChange = false, ChildHeightChange = false;
	if (InWidthChange && IsHorizontalStretched())
	{
		ChildWidthChange = true;
	}
	if (InHeightChange && IsVerticalStretched())
	{
		ChildHeightChange = true;
	}
	GetWidget()->MarkDimensionChanged(false, ChildWidthChange, ChildHeightChange);
}

void ULexLayoutAnchorSlot::CalculateTransformFromLayout()
{
	CalculateTransformFromAnchor();
}

void ULexLayoutAnchorSlot::CalculateAnchorFromTransform()
{
	if (!bCanSetAnchorFromTransform)return;
	auto Widget = GetWidget();
	auto TempRelativeLocation = Widget->GetRelativeLocation();
	FVector2D CalculatedAnchoredPosition;
	if (auto UIParent = Widget->GetUIParent())
	{
		//just a reverse operation from CalculateTransformFromAnchor
		float LocalLeftPoint =
			UIParent->GetLocalSpaceLeft()
			+ (UIParent->GetWidth() * this->AnchorMin.X);

		float LocalBottomPoint =
			UIParent->GetLocalSpaceBottom()
			+ (UIParent->GetHeight() * this->AnchorMin.Y);

		CalculatedAnchoredPosition.X = TempRelativeLocation.Y
			- LocalLeftPoint
			- +(UIParent->GetWidth() * (this->AnchorMax.X - this->AnchorMin.X)) * Widget->GetPivot().X;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z
			- LocalBottomPoint
			- (UIParent->GetHeight() * (this->AnchorMax.Y - this->AnchorMin.Y)) * Widget->GetPivot().Y;
	}
	else
	{
		CalculatedAnchoredPosition.X = TempRelativeLocation.Y;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z;
	}
	auto CompScale3D = Widget->GetComponentScale();
	auto CompScale2D = FVector2f(CompScale3D.Y, CompScale3D.Z);

	bAnchorLeftCached = false;
	bAnchorRightCached = false;
	bAnchorBottomCached = false;
	bAnchorTopCached = false;

	bool AnchorChanged = !AnchoredPosition.Equals(CalculatedAnchoredPosition);
	bool ScaleChanged = !PrevScale2D.Equals(CompScale2D);
	if (AnchorChanged || ScaleChanged)
	{
		AnchoredPosition = CalculatedAnchoredPosition;
		PrevScale2D = CompScale2D;
	}
}
void ULexLayoutAnchorSlot::CalculateTransformFromAnchor()
{
	bool HorizontalPositionChanged = false, VerticalPositionChanged = false;
	CalculateTransformFromAnchor(HorizontalPositionChanged, VerticalPositionChanged);
}
void ULexLayoutAnchorSlot::CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged)
{
	bCanSetAnchorFromTransform = false;
	auto Widget = GetWidget();
	FVector ResultLocation = Widget->GetRelativeLocation();
	if (auto UIParent = Widget->GetUIParent())
	{
		float LocalLeftPoint = //this left point anchor position in parent's space
			UIParent->GetLocalSpaceLeft()//parent's left position
			+ (UIParent->GetWidth() * this->AnchorMin.X);//add anchor offset
		float LocalLeftPivotPoint = //to pivot point, with anchor offset
			LocalLeftPoint
			+ (UIParent->GetWidth() * (this->AnchorMax.X - this->AnchorMin.X))//parent anchor width (width without SizeDelta)
				* Widget->GetPivot().X
			+ this->AnchoredPosition.X;

		float LocalBottomPoint = //this bottom point anchor position in parent's space
			UIParent->GetLocalSpaceBottom()//parent's bottom position
			+ (UIParent->GetHeight() * this->AnchorMin.Y);//add anchor offset
		float LocalBottomPivotPoint = //to pivot point, with anchor offset
			LocalBottomPoint
			+ (UIParent->GetHeight() * (this->AnchorMax.Y - this->AnchorMin.Y))//parent anchor width (width without SizeDelta)
				* Widget->GetPivot().Y
			+ this->AnchoredPosition.Y;

		ResultLocation.Y = LocalLeftPivotPoint;
		ResultLocation.Z = LocalBottomPivotPoint;
	}
	else
	{
		ResultLocation.Y = this->AnchoredPosition.X;
		ResultLocation.Z = this->AnchoredPosition.Y;
	}

	auto OriginRelativeLocation = Widget->GetRelativeLocation();
	double Tolerance = 0.0f;
	if (FMath::Abs(OriginRelativeLocation.Y - ResultLocation.Y) > Tolerance)
	{
		OutHorizontalPositionChanged = true;
	}
	if (FMath::Abs(OriginRelativeLocation.Z - ResultLocation.Z) > Tolerance)
	{
		OutVerticalPositionChanged = true;
	}
	if (OutHorizontalPositionChanged || OutVerticalPositionChanged)
	{
		Widget->GetRelativeLocation_DirectMutable() = ResultLocation;
		Widget->UpdateComponentToWorld();
	}
	bCanSetAnchorFromTransform = true;
}

float ULexLayoutAnchorSlot::GetWidth() const
{
	if (!bWidthCached)
	{
		bWidthCached = true;
		auto Widget = GetWidget();
		if (auto UIParent = Widget->GetUIParent())
		{
			if (IsHorizontalStretched())
			{
				CacheWidth = SizeDelta.X + UIParent->GetWidth() * (AnchorMax.X - AnchorMin.X);
			}
			else
			{
				CacheWidth = SizeDelta.X;
			}
		}
		else
		{
			CacheWidth = SizeDelta.X;
		}
	}
	return CacheWidth;
}
float ULexLayoutAnchorSlot::GetHeight() const
{
	if (!bHeightCached)
	{
		bHeightCached = true;
		auto Widget = GetWidget();
		if (auto UIParent = Widget->GetUIParent())
		{
			if (IsVerticalStretched())
			{
				CacheHeight = SizeDelta.Y + UIParent->GetHeight() * (AnchorMax.Y - AnchorMin.Y);
			}
			else
			{
				CacheHeight = SizeDelta.Y;
			}
		}
		else
		{
			CacheHeight = SizeDelta.Y;
		}
	}
	return CacheHeight;
}

void ULexLayoutAnchorSlot::SetAnchorMin(FVector2D Value)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (!AnchorMin.Equals(Value, 0.0f))
		{
			auto CurrentLeft = this->GetAnchorLeft();
			auto CurrentBottom = this->GetAnchorBottom();

			AnchorMin = Value;
			
			//SetAnchorLeft
			{
				auto CurrentRight = this->GetAnchorRight();
				CacheWidth = UIParent->GetWidth() * (this->AnchorMax.X - this->AnchorMin.X) - CurrentRight - CurrentLeft;
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorMax.X - AnchorMin.X));
					SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, Widget->GetPivot().X);
			}

			//SetAnchorBottom
			{
				auto CurrentTop = this->GetAnchorTop();
				CacheHeight = UIParent->GetHeight() * (this->AnchorMax.Y - this->AnchorMin.Y) - CurrentTop - CurrentBottom;
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorMax.Y - AnchorMin.Y));
					SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, Widget->GetPivot().Y);
			}

			CalculateTransformFromAnchor();
			Widget->SetSize(FVector2D(CacheWidth, CacheHeight));
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}
void ULexLayoutAnchorSlot::SetAnchorMax(FVector2D Value)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (!AnchorMax.Equals(Value, 0.0f))
		{
			auto CurrentRight = this->GetAnchorRight();
			auto CurrentTop = this->GetAnchorTop();

			AnchorMax = Value;

			//SetAnchorRight
			{
				auto CurrentLeft = this->GetAnchorLeft();
				CacheWidth = UIParent->GetWidth() * (this->AnchorMax.X - this->AnchorMin.X) - CurrentRight - CurrentLeft;
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorMax.X - AnchorMin.X));
					SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, Widget->GetPivot().X);
			}
			//SetAnchorTop
			{
				auto CurrentBottom = this->GetAnchorBottom();
				CacheHeight = UIParent->GetHeight() * (this->AnchorMax.Y - this->AnchorMin.Y) - CurrentTop - CurrentBottom;
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorMax.Y - AnchorMin.Y));
					SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, Widget->GetPivot().Y);
			}

			CalculateTransformFromAnchor();
			Widget->SetSize(FVector2D(CacheWidth, CacheHeight));
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}

void ULexLayoutAnchorSlot::SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (!AnchorMin.Equals(MinValue, 0.0f) || !AnchorMax.Equals(MaxValue, 0.0f))
		{
			auto PrevRelativeLocation = Widget->GetRelativeLocation();
			auto PrevWidth = Widget->GetWidth();
			auto PrevHeight = Widget->GetHeight();
			this->SetAnchorMin(MinValue);
			this->SetAnchorMax(MaxValue);
			if (bKeepSize)
			{
				Widget->SetWidth(PrevWidth);
				Widget->SetHeight(PrevHeight);
			}
			if (bKeepRelativeLocation)
			{
				Widget->SetRelativeLocation(PrevRelativeLocation);
			}
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}

void ULexLayoutAnchorSlot::SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (AnchorMin.X != Value.X || AnchorMax.X != Value.Y)
		{
			auto CurrentLeft = this->GetAnchorLeft();
			auto CurrentRight = this->GetAnchorRight();

			if (bKeepSize)
			{
				CacheWidth = Widget->GetWidth();
			}
			auto PrevRelativeLocation = Widget->GetRelativeLocation();

			AnchorMin.X = Value.X;
			AnchorMax.X = Value.Y;

			//SetAnchorLeft & SetAnchorRight
			{
				if (!bKeepSize)//recalculate size on new anchor if not keep size
				{
					CacheWidth = UIParent->GetWidth() * (this->AnchorMax.X - this->AnchorMin.X) - CurrentRight - CurrentLeft;
				}
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorMax.X - AnchorMin.X));
					SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, Widget->GetPivot().X);
			}
			if (bKeepRelativeLocation)
			{
				Widget->SetRelativeLocation(PrevRelativeLocation);
			}
			else
			{
				CalculateTransformFromAnchor();
			}
			if (!bKeepSize)
			{
				Widget->SetSize(FVector2D(CacheWidth, CacheHeight));
			}
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}
void ULexLayoutAnchorSlot::SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (AnchorMin.Y != Value.X || AnchorMax.Y != Value.Y)
		{
			auto CurrentBottom = this->GetAnchorBottom();
			auto CurrentTop = this->GetAnchorTop();

			if (bKeepSize)
			{
				CacheHeight = Widget->GetHeight();
			}
			auto PrevRelativeLocation = Widget->GetRelativeLocation();

			AnchorMin.Y = Value.X;
			AnchorMax.Y = Value.Y;

			//SetAnchorBottom && SetAnchorTop
			{
				if (!bKeepSize)//recalculate size on new anchor if not keep size
				{
					CacheHeight = UIParent->GetHeight() * (this->AnchorMax.Y - this->AnchorMin.Y) - CurrentTop - CurrentBottom;
				}
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorMax.Y - AnchorMin.Y));
					SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, Widget->GetPivot().Y);
			}
			if (bKeepRelativeLocation)
			{
				Widget->SetRelativeLocation(PrevRelativeLocation);
			}
			else
			{
				CalculateTransformFromAnchor();
			}
			if (!bKeepSize)
			{
				Widget->SetSize(FVector2D(CacheWidth, CacheHeight));
			}
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}

void ULexLayoutAnchorSlot::SetAnchoredPosition(FVector2D Value)
{
	if (!AnchoredPosition.Equals(Value, 0.0f))
	{
		AnchoredPosition = Value;
		CalculateTransformFromAnchor();
	}
}

void ULexLayoutAnchorSlot::SetHorizontalAnchoredPosition(float Value)
{
	if (AnchoredPosition.X != Value)
	{
		AnchoredPosition.X = Value;
		CalculateTransformFromAnchor();
	}
}
void ULexLayoutAnchorSlot::SetVerticalAnchoredPosition(float Value)
{
	if (AnchoredPosition.Y != Value)
	{
		AnchoredPosition.Y = Value;
		CalculateTransformFromAnchor();
	}
}

void ULexLayoutAnchorSlot::SetSizeDelta(FVector2D Value)
{
	if (!SizeDelta.Equals(Value, 0.0f))
	{
		SizeDelta = Value;
		bWidthCached = false;
		bHeightCached = false;
		CalculateTransformFromAnchor();
		auto Widget = GetWidget();
		Widget->SetSize(FVector2D(GetWidth(), GetHeight()));
	}
}

float ULexLayoutAnchorSlot::GetAnchorLeft()const
{
	if (!bAnchorLeftCached)
	{
		bAnchorLeftCached = true;
		auto Widget = GetWidget();
		if (auto UIParent = Widget->GetUIParent())
		{
			CacheAnchorLeft =
				Widget->GetLocalSpaceLeft()//local space left
				+ Widget->GetRelativeLocation().Y//convert to parent space
				-
				(UIParent->GetLocalSpaceLeft()//parent space left
					+ UIParent->GetWidth() * this->AnchorMin.X)//to parent anchor min point
				;
		}
		else
		{
			CacheAnchorLeft = Widget->GetLocalSpaceLeft();//local space left
		}
	}
	return CacheAnchorLeft;
}
float ULexLayoutAnchorSlot::GetAnchorTop()const
{
	if (!bAnchorTopCached)
	{
		bAnchorTopCached = true;
		auto Widget = GetWidget();
		if (auto UIParent = Widget->GetUIParent())
		{
			CacheAnchorTop =
				-(
					Widget->GetLocalSpaceTop()
					+ Widget->GetRelativeLocation().Z
					-
					(UIParent->GetLocalSpaceTop()
						- UIParent->GetHeight() * (1.0f - this->AnchorMax.Y))
					)
				;
		}
		else
		{
			CacheAnchorTop = Widget->GetLocalSpaceTop();
		}
	}
	return CacheAnchorTop;
}
float ULexLayoutAnchorSlot::GetAnchorRight()const
{
	if (!bAnchorRightCached)
	{
		bAnchorRightCached = true;
		auto Widget = GetWidget();
		if (auto UIParent = Widget->GetUIParent())
		{
			CacheAnchorRight =
				-(
					Widget->GetLocalSpaceRight()
					+ Widget->GetRelativeLocation().Y
					-
					(UIParent->GetLocalSpaceRight()
						- UIParent->GetWidth() * (1.0f - this->AnchorMax.X))
					)
				;
		}
		else
		{
			CacheAnchorRight = Widget->GetLocalSpaceRight();
		}
	}
	return CacheAnchorRight;
}
float ULexLayoutAnchorSlot::GetAnchorBottom()const
{
	if (!bAnchorBottomCached)
	{
		bAnchorBottomCached = true;
		auto Widget = GetWidget();
		if (auto UIParent = Widget->GetUIParent())
		{
			CacheAnchorBottom =
				Widget->GetLocalSpaceBottom()
				+ Widget->GetRelativeLocation().Z
				-
				(UIParent->GetLocalSpaceBottom()
					+ UIParent->GetHeight() * this->AnchorMin.Y)
				;
		}
		else
		{
			CacheAnchorBottom = Widget->GetLocalSpaceBottom();
		}
	}
	return CacheAnchorBottom;
}

void ULexLayoutAnchorSlot::SetAnchorLeft(float Value)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (CacheAnchorLeft != Value || !bAnchorLeftCached)
		{
			bAnchorLeftCached = true;
			CacheAnchorLeft = Value;
			auto CurrentRight = this->GetAnchorRight();
			CacheWidth = UIParent->GetWidth() * (this->AnchorMax.X - this->AnchorMin.X) - CurrentRight - Value;
			//SetWdith
			{
				if (IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorMax.X - AnchorMin.X));
					SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					SizeDelta.X = CacheWidth;
				}
			}
			this->AnchoredPosition.X = FMath::Lerp(Value, -CurrentRight, Widget->GetPivot().X);
			CalculateTransformFromAnchor();
			Widget->SetWidth(CacheWidth);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexLayoutAnchorSlot::SetAnchorTop(float Value)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (CacheAnchorTop != Value || !bAnchorTopCached)
		{
			bAnchorTopCached = true;
			CacheAnchorTop = Value;
			auto CurrentBottom = this->GetAnchorBottom();
			CacheHeight = UIParent->GetHeight() * (this->AnchorMax.Y - this->AnchorMin.Y) - Value - CurrentBottom;
			//SetHeight
			{
				if (IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorMax.Y - AnchorMin.Y));
					SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -Value, Widget->GetPivot().Y);
			CalculateTransformFromAnchor();
			Widget->SetHeight(CacheHeight);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexLayoutAnchorSlot::SetAnchorRight(float Value)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (CacheAnchorRight != Value || !bAnchorRightCached)
		{
			bAnchorRightCached = true;
			CacheAnchorRight = Value;
			auto CurrentLeft = this->GetAnchorLeft();
			CacheWidth = UIParent->GetWidth() * (this->AnchorMax.X - this->AnchorMin.X) - Value - CurrentLeft;
			//SetWdith
			{
				if (IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorMax.X - AnchorMin.X));
					SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					SizeDelta.X = CacheWidth;
				}
			}
			this->AnchoredPosition.X = FMath::Lerp(CurrentLeft, -Value, Widget->GetPivot().X);
			CalculateTransformFromAnchor();
			Widget->SetWidth(CacheWidth);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexLayoutAnchorSlot::SetAnchorBottom(float Value)
{
	auto Widget = GetWidget();
	if (auto UIParent = Widget->GetUIParent())
	{
		if (CacheAnchorBottom != Value || !bAnchorBottomCached)
		{
			bAnchorBottomCached = true;
			CacheAnchorBottom = Value;
			auto CurrentTop = this->GetAnchorTop();
			CacheHeight = UIParent->GetHeight() * (this->AnchorMax.Y - this->AnchorMin.Y) - CurrentTop - Value;
			//SetHeight
			{
				if (IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorMax.Y - AnchorMin.Y));
					SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchoredPosition.Y = FMath::Lerp(Value, -CurrentTop, Widget->GetPivot().Y);
			CalculateTransformFromAnchor();
			Widget->SetHeight(CacheHeight);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

ULTweener* ULexLayoutAnchorSlot::HorizontalAnchoredPositionTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::GetHorizontalAnchoredPosition), FLTweenFloatSetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::SetHorizontalAnchoredPosition), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexLayoutAnchorSlot::VerticalAnchoredPositionTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::GetVerticalAnchoredPosition), FLTweenFloatSetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::SetVerticalAnchoredPosition), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexLayoutAnchorSlot::AnchoredPositionTo(FVector2D endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenVector2DGetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::GetAnchoredPosition), FLTweenVector2DSetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::SetAnchoredPosition), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}

ULTweener* ULexLayoutAnchorSlot::AnchorLeftTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::GetAnchorLeft), FLTweenFloatSetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::SetAnchorLeft), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexLayoutAnchorSlot::AnchorRightTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::GetAnchorRight), FLTweenFloatSetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::SetAnchorRight), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexLayoutAnchorSlot::AnchorTopTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::GetAnchorTop), FLTweenFloatSetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::SetAnchorTop), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
ULTweener* ULexLayoutAnchorSlot::AnchorBottomTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::GetAnchorBottom), FLTweenFloatSetterFunction::CreateUObject(this, &ULexLayoutAnchorSlot::SetAnchorBottom), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (GetWidget()->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif