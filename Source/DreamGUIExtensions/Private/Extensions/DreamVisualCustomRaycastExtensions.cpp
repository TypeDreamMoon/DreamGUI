// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamVisualCustomRaycastExtensions.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Utils/DreamUIUtils.h"

bool UDreamVisualCustomRaycast_VisiblePixel::Raycast(const UDreamVisual* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector& OutHitPoint, FVector& OutHitNormal)const
{
	if (auto BatchGeometry = Cast<UDreamVisualBatchMesh>(InVisual))
	{
		FVector2D HitUV; FColor HitPixel;
		if (UDreamVisualCustomRaycast::GetRaycastPixelFromUIBatchMeshVisual(BatchGeometry, InLocalSpaceRayStart, InLocalSpaceRayEnd, HitUV, HitPixel, OutHitPoint, OutHitNormal))
		{
			uint8 ChannelValue = 0;
			switch (PixelChannel)
			{
			default:
			case 0:ChannelValue = HitPixel.R; break;
			case 1:ChannelValue = HitPixel.G; break;
			case 2:ChannelValue = HitPixel.B; break;
			case 3:ChannelValue = HitPixel.A; break;
			}
			if (FDreamUIUtils::ByteToFloat01(ChannelValue) > VisibilityThreshold)
			{
				return true;
			}
		}
	}
	return false;
}

void UDreamVisualCustomRaycast_VisiblePixel::SetVisibilityThreshold(float value)
{
	VisibilityThreshold = value;
}
void UDreamVisualCustomRaycast_VisiblePixel::SetPixelChannel(uint8 value)
{
	PixelChannel = value;
}
