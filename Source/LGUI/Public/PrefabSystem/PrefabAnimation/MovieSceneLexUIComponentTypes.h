// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Layout/Margin.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "EntitySystem/MovieScenePropertyTraits.h"
#include "EntitySystem/MovieScenePropertyMetaDataTraits.h"

#include "Containers/ArrayView.h"

class LGUI_API FLexUIMaterialHandle
{
public:
	FLexUIMaterialHandle()
		: Data(nullptr)
	{}

	FLexUIMaterialHandle(void* InData)
		: Data(InData)
	{}

	friend uint32 GetTypeHash(const FLexUIMaterialHandle& In)
	{
		return GetTypeHash(In.Data);
	}
	friend bool operator==(const FLexUIMaterialHandle& A, const FLexUIMaterialHandle& B)
	{
		return A.Data == B.Data;
	}
	friend bool operator!=(const FLexUIMaterialHandle& A, const FLexUIMaterialHandle& B)
	{
		return !(A == B);
	}

	/** @return true if this handle points to valid data */
	bool IsValid() const { return Data != nullptr; }

private:
	/** Pointer to the struct data holding the material */
	void* Data;
};

namespace UE
{
namespace MovieScene
{

struct FLexUIMaterialPath
{
	FLexUIMaterialPath() = default;
	FLexUIMaterialPath(FName Name)
		: Path(Name)
	{}

	FName Path;
};

struct LGUI_API FMovieSceneLexUIComponentTypes
{
	~FMovieSceneLexUIComponentTypes();

	TComponentTypeID<FLexUIMaterialPath> LexUIMaterialPath;
	TComponentTypeID<FLexUIMaterialHandle> LexUIMaterialHandle;

	static void Destroy();

	static FMovieSceneLexUIComponentTypes* Get();

private:
	FMovieSceneLexUIComponentTypes();
};


} // namespace MovieScene
} // namespace UE
