// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIShapeBlend.h"
#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUIClipMask.h"
#include "Core/Components/DreamRectBlock.h"

using namespace DreamUIShapeBlendLayout;

void FDreamUIShapeBlendMath::Pack(TConstArrayView<FDreamUIShapeBlendMember> InMembers, float InSmoothness, FDreamUIShapeBlendShape& OutShape)
{
	const int32 Count = FMath::Min(InMembers.Num(), MaxMembers);
	OutShape.Texels.Reset(HeaderTexels + Count * MemberTexels);
	OutShape.Texels.Add(FVector4f(static_cast<float>(Count), FMath::Max(InSmoothness, 0.0f), 0.0f, 0.0f));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FDreamUIShapeBlendMember& Member = InMembers[Index];
		// The frame to the member: the inverse of the 2x2 whose columns are the member's axes in the frame.
		const float A = Member.AxisX.X, B = Member.AxisY.X, C = Member.AxisX.Y, D = Member.AxisY.Y;
		const float Det = A * D - B * C;
		const float InvDet = FMath::Abs(Det) > 1e-8f ? 1.0f / Det : 0.0f;
		OutShape.Texels.Add(FVector4f(D * InvDet, -B * InvDet, -C * InvDet, A * InvDet));
		OutShape.Texels.Add(FVector4f(Member.Centre.X, Member.Centre.Y, FMath::Max(Member.HalfSize.X, 0.0f), FMath::Max(Member.HalfSize.Y, 0.0f)));
		OutShape.Texels.Add(Member.CornerRadii);
		const uint32 Bits = UDreamRectBlock::PackColorForDataTexture(Member.Color);
		OutShape.Texels.Add(FVector4f(static_cast<float>(static_cast<uint8>(Member.Operation)), *reinterpret_cast<const float*>(&Bits), 0.0f, 0.0f));
	}
}

float FDreamUIShapeBlendMath::Distance(const FVector4f* InTexels, FVector2f P, FVector4f* OutColor, int32* OutNearest)
{
	const int32 Count = FMath::Min(FMath::RoundToInt(InTexels[0].X), MaxMembers);
	const float K = FMath::Max(InTexels[0].Y, 1e-4f);
	float D = 1e6f;
	FVector4f Color(0.0f, 0.0f, 0.0f, 0.0f);
	float NearestDistance = 1e30f;
	int32 Nearest = -1;
	for (int32 Member = 0; Member < Count; ++Member)
	{
		const FVector4f* T = InTexels + HeaderTexels + Member * MemberTexels;
		const FVector2f Offset = P - FVector2f(T[1].X, T[1].Y);
		const FVector2f Local(T[0].X * Offset.X + T[0].Y * Offset.Y, T[0].Z * Offset.X + T[0].W * Offset.Y);
		const float Di = FDreamUIClipMaskMath::SdRoundRect(Local, FVector2f(T[1].Z, T[1].W), T[2]);
		const uint32 Bits = *reinterpret_cast<const uint32*>(&T[3].Y);
		const FVector3f Encoded(static_cast<float>((Bits >> 16) & 0xff) / 255.0f, static_cast<float>((Bits >> 8) & 0xff) / 255.0f, static_cast<float>(Bits & 0xff) / 255.0f);
		const FVector3f Linear = FDreamUIBlendMath::ToLinear(Encoded);
		const FVector4f Ci(Linear.X, Linear.Y, Linear.Z, static_cast<float>((Bits >> 24) & 0xff) / 255.0f);
		const int32 Operation = FMath::RoundToInt(T[3].X);
		if (Operation != static_cast<int32>(EDreamShapeBlendOperation::Subtract) && Di < NearestDistance)
		{
			NearestDistance = Di;
			Nearest = Member;
		}
		if (Member == 0)
		{
			D = Operation == static_cast<int32>(EDreamShapeBlendOperation::Subtract) ? 1e6f : Di;
			Color = Ci;
			continue;
		}
		if (Operation == static_cast<int32>(EDreamShapeBlendOperation::Subtract))
		{
			const float H = FMath::Clamp(0.5f + 0.5f * (Di + D) / K, 0.0f, 1.0f);
			D = FMath::Lerp(-Di, D, H) + K * H * (1.0f - H);
		}
		else if (Operation == static_cast<int32>(EDreamShapeBlendOperation::Intersect))
		{
			const float H = FMath::Clamp(0.5f + 0.5f * (D - Di) / K, 0.0f, 1.0f);
			D = FMath::Lerp(Di, D, H) + K * H * (1.0f - H);
			Color = Ci + (Color - Ci) * H;
		}
		else
		{
			const float H = FMath::Clamp(0.5f + 0.5f * (Di - D) / K, 0.0f, 1.0f);
			D = FMath::Lerp(Di, D, H) - K * H * (1.0f - H);
			Color = Ci + (Color - Ci) * H;
		}
	}
	if (OutColor != nullptr)
	{
		*OutColor = Color;
	}
	if (OutNearest != nullptr)
	{
		*OutNearest = Nearest;
	}
	return D;
}

FBox2f FDreamUIShapeBlendMath::Bounds(TConstArrayView<FDreamUIShapeBlendMember> InMembers, float InSmoothness)
{
	FBox2f Box(ForceInit);
	for (const FDreamUIShapeBlendMember& Member : InMembers)
	{
		if (Member.Operation == EDreamShapeBlendOperation::Subtract)
		{
			continue;
		}
		const FVector2f Reach(FMath::Abs(Member.AxisX.X) * Member.HalfSize.X + FMath::Abs(Member.AxisY.X) * Member.HalfSize.Y,
			FMath::Abs(Member.AxisX.Y) * Member.HalfSize.X + FMath::Abs(Member.AxisY.Y) * Member.HalfSize.Y);
		Box += Member.Centre - Reach;
		Box += Member.Centre + Reach;
	}
	return Box.bIsValid ? Box.ExpandBy(FMath::Max(InSmoothness, 0.0f)) : Box;
}
