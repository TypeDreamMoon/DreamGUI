// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityIDs.h"

class DREAMGUI_API FDreamUIMaterialHandle
{
public:
	FDreamUIMaterialHandle()
		: Data(nullptr)
	{}

	FDreamUIMaterialHandle(void* InData)
		: Data(InData)
	{}

	friend uint32 GetTypeHash(const FDreamUIMaterialHandle& In)
	{
		return GetTypeHash(In.Data);
	}
	friend bool operator==(const FDreamUIMaterialHandle& A, const FDreamUIMaterialHandle& B)
	{
		return A.Data == B.Data;
	}
	friend bool operator!=(const FDreamUIMaterialHandle& A, const FDreamUIMaterialHandle& B)
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

struct FDreamUIMaterialPath
{
	FDreamUIMaterialPath() = default;
	FDreamUIMaterialPath(FName Name)
		: Path(Name)
	{}

	FName Path;
};

struct DREAMGUI_API FMovieSceneDreamUIComponentTypes
{
	~FMovieSceneDreamUIComponentTypes();

	TComponentTypeID<FDreamUIMaterialPath> DreamUIMaterialPath;
	TComponentTypeID<FDreamUIMaterialHandle> DreamUIMaterialHandle;

	static void Destroy();

	static FMovieSceneDreamUIComponentTypes* Get();

private:
	FMovieSceneDreamUIComponentTypes();
};


} // namespace MovieScene
} // namespace UE
