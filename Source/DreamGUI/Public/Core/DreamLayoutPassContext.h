// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

/**
 * The state a layout pass keeps, owned by one world instead of by the process.
 *
 * Today that state is static on the classes that use it: the stack of widgets whose layout containers are
 * writing their results (UDreamWidget::LayoutWriterStack), the pass depth behind
 * UDreamWidget::IsLayoutWriting, and the desired-size memo with its depth (UDreamPanelLayoutBase). Every
 * world shares that one copy, and the stack and the memo hold raw widget pointers. This is the shape it
 * moves into: one context per world, held by that world's manager, keyed by FObjectKey, and entered only
 * through the scopes below, which give back exactly what they took. A pass that ends with anything still
 * open says so through IsBalanced.
 *
 * Widgets are taken as UObject so this header needs nothing from the widget's.
 *
 * Nothing uses this yet: the scopes on UDreamWidget and UDreamPanelLayoutBase still write the static state.
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

	/** A desired size per widget and per measure spec, the spec folded into a number by whoever measures. */
	struct FDesiredSizeKey
	{
		FObjectKey Widget;
		uint64 Spec = 0;

		bool operator==(const FDesiredSizeKey& Other) const
		{
			return Widget == Other.Widget && Spec == Other.Spec;
		}
		friend uint32 GetTypeHash(const FDesiredSizeKey& InKey)
		{
			return HashCombine(GetTypeHash(InKey.Widget), ::GetTypeHash(InKey.Spec));
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

private:
	int32 PassDepth = 0;
	TArray<FObjectKey, TInlineAllocator<8>> Writers;
	int32 MemoDepth = 0;
	TMap<FDesiredSizeKey, FVector2D> DesiredSizes;
};
