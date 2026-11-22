// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

/*
 * What DreamGUI needs to build on every engine it supports: 5.5 and later.
 *
 * The code is written against the newest engine. Where an older one spells an API differently, or does not have it,
 * the difference is answered here, once, and the call sites stay as the newest engine wants them:
 *
 *  - an API the older engines lack under the same name is supplied under that name (a "polyfill"), guarded so that it
 *    only exists where the engine does not provide it -- deleting the block is all it takes to drop that engine;
 *  - an API whose shape changed, and that cannot be supplied under the engine's own name (a static member of an
 *    engine class, an overload of an engine method), is reached through a small function in DreamGUICompat.
 *
 * Every guard names the first engine version that has the newer API, read from the engine's own release tags.
 *
 * The helpers are templates wherever they touch an engine type, so this header pulls in nothing heavy: it is included
 * by public headers that half the plugin includes.
 *
 * Reflected declarations (UCLASS, USTRUCT, UFUNCTION, UPROPERTY) cannot be guarded this way -- UnrealHeaderTool only
 * honours WITH_EDITOR and WITH_EDITORONLY_DATA around them -- so nothing reflected may depend on an API that 5.5 lacks.
 * Plain C++ members of a reflected class can be guarded: UnrealHeaderTool reads past an #if it does not know.
 */

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UObjectHash.h"

/** True on engine Major.Minor or newer. */
#define DREAMGUI_ENGINE_AT_LEAST(Major, Minor) \
	((ENGINE_MAJOR_VERSION > (Major)) || (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))

static_assert(DREAMGUI_ENGINE_AT_LEAST(5, 5), "DreamGUI needs Unreal Engine 5.5 or later.");

// ---------------------------------------------------------------------------------------------------------------------
// PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS (5.6): nothing is marked experimental before, so there is nothing to silence.
// ---------------------------------------------------------------------------------------------------------------------
#ifndef PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
#define PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
#define PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS
#endif

// ---------------------------------------------------------------------------------------------------------------------
// FDelegateUserObjectConst (5.6): a delegate's user object was a plain const void* before.
// ---------------------------------------------------------------------------------------------------------------------
#if !DREAMGUI_ENGINE_AT_LEAST(5, 6)
using FDelegateUserObjectConst = const void*;
#endif

// ---------------------------------------------------------------------------------------------------------------------
// EGetObjectsFlags (5.8): the object hash functions took a bool bIncludeNestedObjects before.
// ---------------------------------------------------------------------------------------------------------------------
#if !DREAMGUI_ENGINE_AT_LEAST(5, 8)
enum class EGetObjectsFlags : uint8
{
	None = 0,
	IncludeNestedObjects = 1 << 0,
};
ENUM_CLASS_FLAGS(EGetObjectsFlags)

inline void GetObjectsWithOuter(const UObjectBase* Outer, TArray<UObject*>& Results, EGetObjectsFlags Flags
	, EObjectFlags ExclusionFlags = RF_NoFlags, EInternalObjectFlags ExclusionInternalFlags = EInternalObjectFlags::None)
{
	GetObjectsWithOuter(Outer, Results, EnumHasAnyFlags(Flags, EGetObjectsFlags::IncludeNestedObjects), ExclusionFlags, ExclusionInternalFlags);
}

inline void ForEachObjectWithOuter(const UObjectBase* Outer, TFunctionRef<void(UObject*)> Operation, EGetObjectsFlags Flags
	, EObjectFlags ExclusionFlags = RF_NoFlags, EInternalObjectFlags ExclusionInternalFlags = EInternalObjectFlags::None)
{
	ForEachObjectWithOuter(Outer, Operation, EnumHasAnyFlags(Flags, EGetObjectsFlags::IncludeNestedObjects), ExclusionFlags, ExclusionInternalFlags);
}

inline void ForEachObjectWithPackage(const UPackage* Outer, TFunctionRef<bool(UObject*)> Operation, EGetObjectsFlags Flags
	, EObjectFlags ExclusionFlags = RF_NoFlags, EInternalObjectFlags ExclusionInternalFlags = EInternalObjectFlags::None)
{
	ForEachObjectWithPackage(Outer, Operation, EnumHasAnyFlags(Flags, EGetObjectsFlags::IncludeNestedObjects), ExclusionFlags, ExclusionInternalFlags);
}

inline void GetObjectsWithPackage(const UPackage* Outer, TArray<UObject*>& Results, EGetObjectsFlags Flags
	, EObjectFlags ExclusionFlags = RF_NoFlags, EInternalObjectFlags ExclusionInternalFlags = EInternalObjectFlags::None)
{
	GetObjectsWithPackage(Outer, Results, EnumHasAnyFlags(Flags, EGetObjectsFlags::IncludeNestedObjects), ExclusionFlags, ExclusionInternalFlags);
}
#endif

namespace DreamGUICompat
{
	/** FCoreDelegates::GetOnPostEngineInit() (5.8); the public static OnPostEngineInit before. */
	inline FSimpleMulticastDelegate& OnPostEngineInit()
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 8)
		return FCoreDelegates::GetOnPostEngineInit();
#else
		return FCoreDelegates::OnPostEngineInit;
#endif
	}

	/** FViewMatrices::GetWorldToClip() (5.8); GetViewProjectionMatrix() before. */
	template <typename TViewMatrices>
	const auto& WorldToClip(const TViewMatrices& InMatrices)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 8)
		return InMatrices.GetWorldToClip();
#else
		return InMatrices.GetViewProjectionMatrix();
#endif
	}

	/** FViewMatrices::GetClipToWorld() (5.8); GetInvViewProjectionMatrix() before. */
	template <typename TViewMatrices>
	const auto& ClipToWorld(const TViewMatrices& InMatrices)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 8)
		return InMatrices.GetClipToWorld();
#else
		return InMatrices.GetInvViewProjectionMatrix();
#endif
	}

	/** UPackage::GetMetaData(): an FMetaData from 5.6 on, a UMetaData object before; both answer the same queries. */
	template <typename TPackage>
	auto& PackageMetaData(TPackage* InPackage)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 6)
		return InPackage->GetMetaData();
#else
		return *InPackage->GetMetaData();
#endif
	}

	/** FSceneView::GetCullingFrustum() (5.6); the public CullingFrustum member before. */
	template <typename TSceneView>
	const auto& CullingFrustum(const TSceneView& InView)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 6)
		return InView.GetCullingFrustum();
#else
		return InView.CullingFrustum;
#endif
	}

	/** IMovieScenePlayer::GetEvaluationState() (5.6); the public State member before. */
	template <typename TPlayer>
	auto* EvaluationState(TPlayer& InPlayer)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 6)
		return InPlayer.GetEvaluationState();
#else
		return &InPlayer.State;
#endif
	}

	/** IDetailLayoutBuilder::GetDetailsViewSharedPtr() (5.6); a raw pointer to the same widget before. */
	template <typename TDetailBuilder>
	auto GetDetailsViewSharedPtr(TDetailBuilder& InBuilder)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 6)
		return InBuilder.GetDetailsViewSharedPtr();
#else
		auto* const View = InBuilder.GetDetailsView();
		using TView = std::remove_pointer_t<decltype(View)>;
		return View != nullptr ? TSharedPtr<TView>(StaticCastSharedRef<TView>(View->AsShared())) : TSharedPtr<TView>();
#endif
	}

	/** FText::AsLocalizable_Advanced with a translator's note (5.8); a text carries no note before, so it is dropped. */
	template <typename TString>
	FText AsLocalizableWithNote(const FTextKey& InNamespace, const FTextKey& InKey, TString&& InString, FStringView InNote)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 8)
		return FText::AsLocalizable_Advanced(InNamespace, InKey, Forward<TString>(InString), InNote);
#else
		return FText::AsLocalizable_Advanced(InNamespace, InKey, Forward<TString>(InString));
#endif
	}

	/** UEditorEngine::HasSelectedMovementLockedItems() (5.8); HasLockedActors() before. */
	template <typename TEditorEngine>
	bool HasSelectedMovementLockedItems(TEditorEngine* InEditor)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 8)
		return InEditor->HasSelectedMovementLockedItems();
#else
		return InEditor->HasLockedActors();
#endif
	}

	/*
	 * The material queries -- relevance, resource, used textures -- are asked by shader platform from 5.7 on and by
	 * feature level before. Each helper takes the shader platform and, on an older engine, asks with the highest feature
	 * level that platform supports (GetMaxSupportedFeatureLevel), which is what the engine itself did before the change.
	 */

	/** UMaterialInterface::GetRelevance_Concurrent. */
	template <typename TMaterial, typename TShaderPlatform>
	auto GetRelevance_Concurrent(const TMaterial* InMaterial, TShaderPlatform InShaderPlatform)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 7)
		return InMaterial->GetRelevance_Concurrent(InShaderPlatform);
#else
		return InMaterial->GetRelevance_Concurrent(GetMaxSupportedFeatureLevel(InShaderPlatform));
#endif
	}

	/** UMeshComponent::GetMaterialRelevance. */
	template <typename TComponent, typename TShaderPlatform>
	auto GetMaterialRelevance(const TComponent* InComponent, TShaderPlatform InShaderPlatform)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 7)
		return InComponent->GetMaterialRelevance(InShaderPlatform);
#else
		return InComponent->GetMaterialRelevance(GetMaxSupportedFeatureLevel(InShaderPlatform));
#endif
	}

	/** UMaterialInterface::GetMaterialResource (const and not). */
	template <typename TMaterial, typename TShaderPlatform>
	auto GetMaterialResource(TMaterial* InMaterial, TShaderPlatform InShaderPlatform)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 7)
		return InMaterial->GetMaterialResource(InShaderPlatform);
#else
		return InMaterial->GetMaterialResource(GetMaxSupportedFeatureLevel(InShaderPlatform));
#endif
	}
}
