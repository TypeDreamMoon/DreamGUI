// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"
#include "Core/Components/DreamLayoutFragment.h"

/**
 * The state a layout pass keeps, owned by one world instead of by the process: the stack of widgets whose
 * layout containers are writing their results, the pass depth behind UDreamWidget::IsLayoutWriting, and the
 * desired-size memo with its depth and its count of answers computed rather than recalled.
 *
 * That state used to be static on the classes that use it, so every world shared one copy, and the stack
 * and the memo held raw widget pointers. Now a world's manager holds its context, keyed by FObjectKey; a
 * tree in no world with a manager -- a test's, an authoring template's -- has one on its root widget, since
 * a pass never leaves the tree it started in (see UDreamWidget::GetLayoutPassContext). The context is
 * entered only through the scopes below, which give back exactly what they took, and a pass that ends with
 * anything still open says so through IsBalanced.
 *
 * Widgets are taken as UObject so this header needs nothing from the widget's.
 */
class FDreamLayoutPassContext
{
public:
	/** One pass: UpdateLayout on a tree. Passes may nest, and the context is writing while any is open. */
	class FPassScope
	{
	public:
		explicit FPassScope(FDreamLayoutPassContext& InContext)
			: Context(InContext)
		{
			++Context.PassDepth;
		}
		~FPassScope()
		{
			--Context.PassDepth;
		}
		FPassScope(const FPassScope&) = delete;
		FPassScope& operator=(const FPassScope&) = delete;

	private:
		FDreamLayoutPassContext& Context;
	};

	/**
	 * A layout container writing its results onto its children. The writer is remembered so that the dirty
	 * marks those writes raise stop below it instead of re-dirtying the container that just consumed its own;
	 * see UDreamWidget::FLayoutWriteScope.
	 */
	class FWriteScope
	{
	public:
		FWriteScope(FDreamLayoutPassContext& InContext, const UObject* InWriter)
			: Context(InContext)
			, Writer(InWriter)
		{
			Context.Writers.Push(Writer);
		}
		~FWriteScope()
		{
			// Scopes close in the order they opened; anything else is a scope that escaped its block.
			if (ensureMsgf(Context.Writers.Num() > 0 && Context.Writers.Last() == Writer, TEXT("A layout write scope closed out of order.")))
			{
				Context.Writers.Pop(EAllowShrinking::No);
			}
		}
		FWriteScope(const FWriteScope&) = delete;
		FWriteScope& operator=(const FWriteScope&) = delete;

	private:
		FDreamLayoutPassContext& Context;
		FObjectKey Writer;
	};

	/** The desired-size memo's lifetime: it answers while any memo scope is open, and forgets everything when the outermost one closes. */
	class FMemoScope
	{
	public:
		explicit FMemoScope(FDreamLayoutPassContext& InContext)
			: Context(InContext)
		{
			++Context.MemoDepth;
		}
		~FMemoScope()
		{
			if (--Context.MemoDepth == 0)
			{
				Context.DesiredSizes.Reset();
			}
		}
		FMemoScope(const FMemoScope&) = delete;
		FMemoScope& operator=(const FMemoScope&) = delete;

	private:
		FDreamLayoutPassContext& Context;
	};

	/**
	 * A desired size per widget and per pair of measure specs: one widget can be measured under several
	 * constraints in a pass, and each answer is its own.
	 *
	 * The hash folds in the two modes and ignores the values, because FDreamMeasureSpec's equality is a
	 * tolerance comparison and a hash built from a float would put two equal keys in different buckets.
	 * Equal keys therefore always hash equal; unequal keys with the same modes share a bucket, which costs a
	 * comparison and nothing else.
	 */
	struct FDesiredSizeKey
	{
		FObjectKey Widget;
		FDreamMeasureSpec WidthSpec;
		FDreamMeasureSpec HeightSpec;

		bool operator==(const FDesiredSizeKey& Other) const
		{
			return Widget == Other.Widget && WidthSpec == Other.WidthSpec && HeightSpec == Other.HeightSpec;
		}
		friend uint32 GetTypeHash(const FDesiredSizeKey& InKey)
		{
			return HashCombine(GetTypeHash(InKey.Widget),
				static_cast<uint32>(InKey.WidthSpec.Mode) * 3u + static_cast<uint32>(InKey.HeightSpec.Mode));
		}
	};

	/** Whether a pass is running: a size set now is layout output, not something anyone asked for. */
	bool IsWriting() const
	{
		return PassDepth > 0;
	}

	/** Whether InWidget's layout container is among those writing right now. */
	bool IsWriter(const UObject* InWidget) const
	{
		return Writers.Contains(FObjectKey(InWidget));
	}

	/** The memo's answer, or null outside a memo scope or when nothing was recorded. */
	const FVector2D* FindDesiredSize(const FDesiredSizeKey& InKey) const
	{
		return MemoDepth > 0 ? DesiredSizes.Find(InKey) : nullptr;
	}

	/** Records an answer. Nothing is recorded outside a memo scope, where it would outlive the pass. */
	void RecordDesiredSize(const FDesiredSizeKey& InKey, const FVector2D& InSize)
	{
		if (MemoDepth > 0)
		{
			DesiredSizes.Add(InKey, InSize);
		}
	}

	/** Drops InWidget's answers, for a caller about to write its geometry mid-pass. */
	void ForgetDesiredSizes(const UObject* InWidget)
	{
		const FObjectKey Key(InWidget);
		for (auto It = DesiredSizes.CreateIterator(); It; ++It)
		{
			if (It.Key().Widget == Key)
			{
				It.RemoveCurrent();
			}
		}
	}

	/** Drops every answer, for a change that alters which widgets take part at all. */
	void ForgetAllDesiredSizes()
	{
		DesiredSizes.Reset();
	}

	/** Whether every scope opened on this context has closed. A pass that ends otherwise leaks its state into the next. */
	bool IsBalanced() const
	{
		return PassDepth == 0 && Writers.Num() == 0 && MemoDepth == 0;
	}

	/**
	 * The raw counts behind IsBalanced and IsWriting, for a test rig that has to say which scope was left
	 * open. Numbers rather than bools, because a stray close -- a negative depth -- is the same fault in the
	 * other direction and a bool cannot show it. Reads; they change nothing.
	 */
	int32 GetPassDepth() const { return PassDepth; }
	int32 GetWriterCount() const { return Writers.Num(); }
	int32 GetMemoDepth() const { return MemoDepth; }
	int32 GetRecordedDesiredSizeCount() const { return DesiredSizes.Num(); }

	/** Counts one desired size that had to be computed because the memo could not answer it. */
	void NoteDesiredSizeComputed()
	{
		++DesiredSizeComputeCount;
	}
	/** How many desired sizes were computed rather than recalled since the last reset: what a test of the memo measures. */
	int64 GetDesiredSizeComputeCount() const { return DesiredSizeComputeCount; }
	void ResetDesiredSizeComputeCount() { DesiredSizeComputeCount = 0; }

private:
	int32 PassDepth = 0;
	TArray<FObjectKey, TInlineAllocator<8>> Writers;
	int32 MemoDepth = 0;
	TMap<FDesiredSizeKey, FVector2D> DesiredSizes;
	int64 DesiredSizeComputeCount = 0;
};
