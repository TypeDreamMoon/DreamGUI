// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIShapeBlend.generated.h"

/** How a rect block joins the shape its shape blend group makes (UDreamShapeBlendGroup). */
UENUM(BlueprintType)
enum class EDreamShapeBlendOperation : uint8
{
	/** Melts into the shape. */
	Union,
	/** Cuts itself out of the shape, smoothly. */
	Subtract,
	/** Keeps only where it and the shape meet. */
	Intersect,
};

/**
 * Rounded rects melted together by a smooth minimum, as a shader reads them (DreamUIShapeBlend.ush): a header texel, then four
 * texels a member, in a frame of the reader's choosing (units from its centre, +Y down).
 *
 *   texel 0       x the number of members, y the smoothness (units: how far apart two edges still melt), zw unused
 *   member texels 1 + 4i ..
 *     T0  the inverse of the member's 2x2: frame to member, row by row (a b; c d)
 *     T1  the member's centre in the frame xy, its half size zw
 *     T2  its corners' radii, in a rect block's order
 *     T3  x its operation (EDreamShapeBlendOperation), y its colour (ARGB bytes in a float's bits, alpha on top), zw unused
 *
 * The constants have their #defines in DreamUIShapeBlend.ush; a test holds the two to each other.
 */
namespace DreamUIShapeBlendLayout
{
	inline constexpr int32 HeaderTexels = 1;
	inline constexpr int32 MemberTexels = 4;
	inline constexpr int32 MaxMembers = 32;
	inline constexpr int32 MaxTexels = HeaderTexels + MaxMembers * MemberTexels;
}

/** A member of a shape blend, before it is packed: where it is in the frame, its size, corners, operation and colour. */
struct FDreamUIShapeBlendMember
{
	/** Its centre in the frame. */
	FVector2f Centre = FVector2f::ZeroVector;
	/** Its own x and y (+Y down) axes in the frame, a unit of the member each. */
	FVector2f AxisX = FVector2f(1.0f, 0.0f);
	FVector2f AxisY = FVector2f(0.0f, 1.0f);
	FVector2f HalfSize = FVector2f(10.0f, 10.0f);
	FVector4f CornerRadii = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	EDreamShapeBlendOperation Operation = EDreamShapeBlendOperation::Union;
	/** sRGB-encoded, as a rect block's body colour. */
	FColor Color = FColor::White;
};

/** A shape blend packed for a shader, in some frame: DreamUIShapeBlendLayout's texels. */
struct FDreamUIShapeBlendShape
{
	TArray<FVector4f> Texels;

	int32 NumMembers() const { return Texels.Num() > 0 ? FMath::RoundToInt(Texels[0].X) : 0; }
	bool IsEmpty() const { return NumMembers() == 0; }
	bool operator==(const FDreamUIShapeBlendShape& Other) const
	{
		return Texels.Num() == Other.Texels.Num() && (Texels.Num() == 0 || FMemory::Memcmp(Texels.GetData(), Other.Texels.GetData(), Texels.Num() * sizeof(FVector4f)) == 0);
	}
};

/** DreamUIShapeBlend.ush on the CPU, function for function: what a click on a shape blend is tested by. */
struct DREAMGUI_API FDreamUIShapeBlendMath
{
	/** InMembers (the first MaxMembers) in their frame, melting InSmoothness units apart. */
	static void Pack(TConstArrayView<FDreamUIShapeBlendMember> InMembers, float InSmoothness, FDreamUIShapeBlendShape& OutShape);
	/**
	 * DreamUIShapeBlend_Distance: the melted shape's signed distance at P (units, negative inside), its colour there (straight,
	 * linear, the members' blended by how much each makes the shape there), and the member nearest P whose operation adds to the
	 * shape (-1 for none).
	 */
	static float Distance(const FVector4f* InTexels, FVector2f P, FVector4f* OutColor = nullptr, int32* OutNearest = nullptr);
	/** The bounds of the melted shape in its frame: its members', grown by the smoothness. */
	static FBox2f Bounds(TConstArrayView<FDreamUIShapeBlendMember> InMembers, float InSmoothness);
};
