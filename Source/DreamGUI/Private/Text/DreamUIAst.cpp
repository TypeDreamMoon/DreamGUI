// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIAst.h"

/*
 * The two walks every consumer of the tree would otherwise write for itself.
 *
 * They live here rather than in the parser because the AST outlives it: the builder resolves styles,
 * the write-back patcher hunts for the node holding a property, and a headless syntax check counts
 * nodes -- three callers, one traversal order. Three private copies of "recurse into Children" is
 * how one of them quietly forgets that loop bodies are children too.
 */

namespace DreamUIAstLocal
{
	void VisitDepthFirst(const FDreamUINode& InNode, TFunctionRef<void(const FDreamUINode&)> InPredicate, int32 InDepth)
	{
		InPredicate(InNode);
		// The crash guard, not the policy. The parser refuses a file past DreamUIAst::MaxNestingDepth
		// with a diagnostic and a line number, so a PARSED tree can never reach this -- what can is a
		// tree assembled in code (the designer, a test) with a cycle or a runaway loop in it, and for
		// those there is nobody to report to and no line to report. Stopping the descent leaves the
		// caller with a partial walk; recursing leaves them with no editor.
		if (InDepth >= DreamUIAst::MaxNestingDepth)
		{
			return;
		}
		for (const FDreamUINode& Child : InNode.Children)
		{
			VisitDepthFirst(Child, InPredicate, InDepth + 1);
		}
	}

	/** One CollectStyleChain: the output, the styles still being expanded, and whether a cycle stopped it. */
	struct FStyleChainWalk
	{
		const FDreamUIAst& Ast;
		TArray<const FDreamUIStyle*>& Out;
		TArray<FDreamUIStyleChainProblem>* Problems;
		TArray<const FDreamUIStyle*> Expanding;
		bool bCycle = false;

		/**
		 * A base named in InOwner's clause. Its own namespace first when the owner came in under one and the base is
		 * written bare: the parser renames a library style's bases along with it, so this is the fallback for an AST
		 * assembled some other way, never a second meaning.
		 */
		const FDreamUIStyle* Resolve(const FDreamUIStyle* InOwner, const FString& InName) const
		{
			int32 Dot = INDEX_NONE;
			if (InOwner != nullptr && InOwner->Name.FindLastChar(TEXT('.'), Dot) && !InName.Contains(TEXT(".")))
			{
				if (const FDreamUIStyle* Prefixed = Ast.FindStyle(InOwner->Name.Left(Dot + 1) + InName))
				{
					return Prefixed;
				}
			}
			return Ast.FindStyle(InName);
		}

		void Note(FDreamUIStyleChainProblem::EKind InKind, const FDreamUIStyleRef& InRef, const FDreamUIStyle* InOwner, FString InThrough)
		{
			if (Problems != nullptr)
			{
				FDreamUIStyleChainProblem& Problem = Problems->AddDefaulted_GetRef();
				Problem.Kind = InKind;
				Problem.Ref = InRef;
				Problem.Owner = InOwner;
				Problem.Through = MoveTemp(InThrough);
			}
		}

		void ExpandClause(const FDreamUIStyle* InOwner, TConstArrayView<FDreamUIStyleRef> InClause, int32 InDepth)
		{
			// What THIS clause applied, entry by entry, so a later name already applied by an earlier one can say which.
			// A style applied before the clause began (by a sibling branch of an outer clause) is not this clause's
			// doing, and goes unremarked.
			const int32 ClauseStart = Out.Num();
			TArray<TPair<int32, FString>> EntryEnds;
			for (const FDreamUIStyleRef& Ref : InClause)
			{
				if (bCycle)
				{
					return;
				}
				const FDreamUIStyle* Style = Resolve(InOwner, Ref.Name);
				if (Style == nullptr)
				{
					Note(FDreamUIStyleChainProblem::EKind::Unknown, Ref, InOwner, FString());
					continue;
				}
				const int32 AppliedAt = Out.Find(Style);
				if (AppliedAt != INDEX_NONE)
				{
					if (AppliedAt >= ClauseStart)
					{
						FString Through = Ref.Name;
						for (const TPair<int32, FString>& Entry : EntryEnds)
						{
							if (AppliedAt < Entry.Key)
							{
								Through = Entry.Value;
								break;
							}
						}
						Note(FDreamUIStyleChainProblem::EKind::AlreadyApplied, Ref, InOwner, MoveTemp(Through));
					}
					continue;
				}
				// Past the nesting budget is treated as the cycle it almost certainly is: a hand-built AST can hold a
				// chain no file could, and recursing on it is the stack overflow the budget exists to rule out.
				if (Expanding.Contains(Style) || InDepth >= DreamUIAst::MaxNestingDepth)
				{
					bCycle = true;
					Note(FDreamUIStyleChainProblem::EKind::Cycle, Ref, InOwner, Style->Name);
					return;
				}
				Expanding.Push(Style);
				ExpandClause(Style, Style->BaseRefs, InDepth + 1);
				Expanding.Pop(EAllowShrinking::No);
				if (bCycle)
				{
					return;
				}
				Out.Add(Style);
				EntryEnds.Emplace(Out.Num(), Ref.Name);
			}
		}
	};
}

const FString& FDreamUINode::GetFirstStyleName() const
{
	static const FString None;
	return StyleRefs.Num() > 0 ? StyleRefs[0].Name : None;
}

bool FDreamUIAst::CollectStyleChain(TConstArrayView<FDreamUIStyleRef> InClause, TArray<const FDreamUIStyle*>& OutBaseFirst,
	TArray<FDreamUIStyleChainProblem>* OutProblems) const
{
	OutBaseFirst.Reset();
	DreamUIAstLocal::FStyleChainWalk Walk{ *this, OutBaseFirst, OutProblems };
	Walk.ExpandClause(nullptr, InClause, /*InDepth*/0);
	if (Walk.bCycle)
	{
		OutBaseFirst.Reset();
		return false;
	}
	return true;
}

const FDreamUIStyle* FDreamUIAst::FindStyle(const FString& InName) const
{
	// FString's own comparison, which is case insensitive, and that is the intended rule rather than
	// an oversight. Every name a .dui writes ends up as an FName somewhere downstream -- a node id
	// becomes a member variable, a style name is only ever matched against other names in the same
	// file -- and FName does not distinguish case. A parser that treated Card and card as two styles
	// would be stricter than the thing it feeds, which is the direction that produces "it resolved in
	// the editor and not in the build". Keywords are the opposite and are matched case sensitively:
	// they are grammar, not names.
	for (const FDreamUIStyle& Style : Styles)
	{
		if (Style.Name == InName)
		{
			return &Style;
		}
	}
	// Then the imports, so a local declaration shadows an imported one -- the same first-wins rule
	// that already resolves two locals.
	for (const FDreamUIStyle& Style : ImportedStyles)
	{
		if (Style.Name == InName)
		{
			return &Style;
		}
	}
	return nullptr;
}

const FDreamUIResource* FDreamUIAst::FindResource(const FString& InName) const
{
	for (const FDreamUIResource& Resource : Resources)
	{
		if (Resource.Name == InName)
		{
			return &Resource;
		}
	}
	for (const FDreamUIResource& Resource : ImportedResources)
	{
		if (Resource.Name == InName)
		{
			return &Resource;
		}
	}
	return nullptr;
}

const FDreamUIComponentAlias* FDreamUIAst::FindComponentAlias(const FString& InName) const
{
	for (const FDreamUIComponentAlias& Alias : ComponentAliases)
	{
		if (Alias.Alias == InName)
		{
			return &Alias;
		}
	}
	for (const FDreamUIComponentAlias& Alias : ImportedComponentAliases)
	{
		if (Alias.Alias == InName)
		{
			return &Alias;
		}
	}
	return nullptr;
}

const FDreamUIEventDecl* FDreamUIAst::FindEvent(const FString& InName) const
{
	for (const FDreamUIEventDecl& Event : Events)
	{
		if (Event.Name == InName)
		{
			return &Event;
		}
	}
	return nullptr;
}

const FDreamUIViewModelDecl* FDreamUIAst::FindViewModel(const FString& InName) const
{
	for (const FDreamUIViewModelDecl& ViewModel : ViewModels)
	{
		if (ViewModel.Name == InName)
		{
			return &ViewModel;
		}
	}
	return nullptr;
}

const FDreamUITimeline* FDreamUIAst::FindTimeline(const FString& InName) const
{
	// No import chain to fall through to, on purpose -- see the field's own note: a timeline is an
	// identity (an object on this tree, a variable on this class), not a bag of values that copies.
	for (const FDreamUITimeline& Timeline : Timelines)
	{
		if (Timeline.Name == InName)
		{
			return &Timeline;
		}
	}
	return nullptr;
}

void FDreamUIAst::ForEachNode(TFunctionRef<void(const FDreamUINode&)> InPredicate) const
{
	// bHasRoot rather than "is Root default constructed", because a parse that failed early leaves a
	// Root that is empty but not meaningfully absent, and callers that check emptiness instead would
	// each pick their own definition of empty.
	if (bHasRoot)
	{
		DreamUIAstLocal::VisitDepthFirst(Root, InPredicate, /*InDepth*/0);
	}
}
