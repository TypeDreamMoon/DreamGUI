// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/*
 * Particles as DreamGUI draws them: what the reader copies off a renderer's bindings (DreamNiagaraEngineAccess.h) and the
 * mesh builder writes vertices from (DreamNiagaraMeshBuilder.h). Nothing here names a Niagara type, so the builder can be
 * fed by a test as it is by a simulation, and an engine upgrade touches the reader alone.
 *
 * Positions and vectors are in the simulation's space -- the component's for a local-space emitter, the simulation's world
 * for the rest -- and taken onto the canvas by the build space (FDreamNiagaraBuildSpace). Each default is what Niagara's own
 * renderer reads for an attribute that is not bound.
 */

/** One sprite. */
struct FDreamNiagaraSpriteParticle
{
	FVector3f Position = FVector3f::ZeroVector;
	/** Linear, and may be brighter than 1. */
	FLinearColor Color = FLinearColor::White;
	/** Width along the sprite's right, height along its up, in simulation units. */
	FVector2f Size = FVector2f(50.0f, 50.0f);
	/** Degrees, clockwise on the screen, as Niagara turns a camera-facing sprite. */
	float Rotation = 0.0f;
	/** The frame of the sprite's sheet, and with a fraction the blend towards the next. */
	float SubImageIndex = 0.0f;
	FVector3f Velocity = FVector3f::ZeroVector;
	/** The direction the sprite's top points in, for a custom alignment. */
	FVector3f Alignment = FVector3f::ZeroVector;
	/** Where in the sprite (0 to 1, from its top left) its position is. */
	FVector2f PivotOffset = FVector2f(0.5f, 0.5f);
	FVector2f UVScale = FVector2f(1.0f, 1.0f);
	FVector4f DynamicParameter = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	/** What a custom sort orders by. */
	float SortKey = 0.0f;
};

/** One point of a ribbon. */
struct FDreamNiagaraRibbonParticle
{
	FVector3f Position = FVector3f::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	float Width = 1.0f;
	/** The point's place along its ribbon: lower first. */
	float LinkOrder = 0.0f;
	/** Which ribbon the point is on. */
	int64 RibbonKey = 0;
	float NormalizedAge = 0.0f;
	FVector4f DynamicParameter = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	/** The texture coordinate along the ribbon of each UV set, where the renderer takes it from the particle. */
	float U0Override = 0.0f;
	float U1Override = 0.0f;
};

enum class EDreamNiagaraSpriteAlignment : uint8
{
	/** The sprite's rotation alone turns it. */
	Unaligned,
	/** Its top points along its velocity; its rotation is not read. */
	Velocity,
	/** Its top points along its alignment, and its rotation turns it from there. */
	Custom,
};

enum class EDreamNiagaraSortMode : uint8
{
	/** In the order the simulation holds them. */
	None,
	/** By sort key, lower drawn first (so higher on top). */
	Ascending,
	/** By sort key, higher drawn first. */
	Descending,
};

/** A sprite renderer, as the builder reads it. */
struct FDreamNiagaraSpriteSettings
{
	/** Columns and rows of the sprite sheet. */
	FIntPoint SubImageSize = FIntPoint(1, 1);
	bool bSubImageBlend = false;
	EDreamNiagaraSpriteAlignment Alignment = EDreamNiagaraSpriteAlignment::Unaligned;
	EDreamNiagaraSortMode SortMode = EDreamNiagaraSortMode::None;
};

enum class EDreamNiagaraRibbonUVDistribution : uint8
{
	/** 0 to 1 along the ribbon, by point. */
	ScaledUniformly,
	/** 0 to 1 along the ribbon, by length. */
	ScaledByLength,
	/** Once every tiling length, from the ribbon's start. */
	TiledByLength,
};

/** One of a ribbon's UV sets. */
struct FDreamNiagaraRibbonUVSettings
{
	EDreamNiagaraRibbonUVDistribution Distribution = EDreamNiagaraRibbonUVDistribution::ScaledUniformly;
	/** Simulation units per repeat, for TiledByLength. */
	float TilingLength = 100.0f;
	FVector2f Offset = FVector2f(0.0f, 0.0f);
	FVector2f Scale = FVector2f(1.0f, 1.0f);
	/** U is the particle's U override rather than the distribution's. */
	bool bUseUOverride = false;
};

/** A ribbon renderer, as the builder reads it. */
struct FDreamNiagaraRibbonSettings
{
	FDreamNiagaraRibbonUVSettings UV0;
	FDreamNiagaraRibbonUVSettings UV1;
	/** The most pieces a segment between two points is cut into (1: straight). */
	int32 MaxTessellation = 1;
	/** Every segment is cut into MaxTessellation; otherwise as many as its bend asks (TessellationAngle). */
	bool bConstantTessellation = false;
	/** Degrees of bend per piece, when the tessellation follows the bend. */
	float TessellationAngle = 15.0f;
	/** 0 is a Catmull-Rom curve through the points, 1 straight lines between them. */
	float CurveTension = 0.0f;
	/** At most this many ribbons are drawn, the ones whose keys come first; 0 draws all. */
	int32 MaxNumRibbons = 0;
	/** The points are drawn from the last to the first, so that the first is on top. */
	bool bDrawLastFirst = false;
};

/**
 * Where the particles go on the canvas, in two steps. SimToPlane takes the simulation's space to a plane laid out as
 * DreamGUI's (X out of it, Y right, Z up), one simulation unit SizeScale canvas units: there a sprite's corners are placed
 * and a ribbon's edges, facing the reader. PlaneToCanvas then takes the plane onto the canvas -- the widget's place on it
 * for a local-space emitter, nothing for one in the simulation's world, which is the canvas's. Row vectors, as FMatrix44f is.
 */
struct FDreamNiagaraBuildSpace
{
	FMatrix44f SimToPlane = FMatrix44f::Identity;
	FMatrix44f PlaneToCanvas = FMatrix44f::Identity;
	/** Canvas units per simulation unit: what a sprite's size and a ribbon's width are multiplied by. */
	float SizeScale = 1.0f;
	/** Every vertex is put on the plane (X = 0) before it goes onto the canvas, whatever its depth in the simulation. */
	bool bFlattenDepth = true;
};
