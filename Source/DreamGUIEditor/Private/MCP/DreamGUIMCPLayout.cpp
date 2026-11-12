// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MCP/DreamGUIMCPLayout.h"

#include "MCP/DreamGUIMCPStage.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamWidgetBlueprint.h"
#include "Interaction/UISelectable.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DreamGUIMCP
{
	namespace
	{
		/** A file's nodes by their id path from its root: what a widget built from that file is looked up by. */
		struct FAstIndex
		{
			FString File;
			TMap<FString, const FDreamUINode*> ByPath;
			/** Ids are unique in a file (DUI3001), anonymous nodes' included (the parser names them): what finds a node a control moved into its hole. */
			TMap<FString, const FDreamUINode*> ById;
			TSharedPtr<FDreamUIAst> Owned;

			void Add(const FDreamUINode& InNode, const FString& InParentPath)
			{
				// A loop or a named slot is not a widget: its body's nodes sit under the widget that holds it.
				FString Path = InParentPath;
				if (InNode.Kind == EDreamUINodeKind::Widget)
				{
					Path = InParentPath.IsEmpty() ? InNode.Id : InParentPath + TEXT("/") + InNode.Id;
					ByPath.Add(Path, &InNode);
					if (!InNode.Id.IsEmpty() && !ById.Contains(InNode.Id))
					{
						ById.Add(InNode.Id, &InNode);
					}
				}
				for (const FDreamUINode& Child : InNode.Children)
				{
					Add(Child, Path);
				}
			}

			void Build(const FDreamUIAst& InAst, const FString& InFile)
			{
				File = InFile;
				ByPath.Reset();
				ById.Reset();
				if (InAst.bHasRoot)
				{
					Add(InAst.Root, FString());
				}
			}
		};

		/** The text-authored source of a component class, parsed once per capture. */
		const FAstIndex* FindComponentIndex(const UClass* InClass, TMap<const UClass*, TSharedPtr<FAstIndex>>& InOutCache)
		{
			if (InClass == nullptr)
			{
				return nullptr;
			}
			if (const TSharedPtr<FAstIndex>* Found = InOutCache.Find(InClass))
			{
				return Found->Get();
			}
			TSharedPtr<FAstIndex>& Slot = InOutCache.Add(InClass);
			const UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(InClass->ClassGeneratedBy);
			const FString Authored = Blueprint != nullptr ? DreamUITextAuthoring::GetAuthoredSourcePath(Blueprint) : FString();
			if (Authored.IsEmpty())
			{
				return nullptr;
			}
			const FString File = DreamUIPaths::Resolve(Authored);
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *File))
			{
				return nullptr;
			}
			TSharedPtr<FAstIndex> Index = MakeShared<FAstIndex>();
			Index->Owned = MakeShared<FDreamUIAst>();
			FDreamUIDiagnosticBag Ignored;
			FDreamUISourceFile::Parse(Text, File, *Index->Owned, Ignored, FDreamUISourceFile::MakeFileImportReader());
			Index->Build(*Index->Owned, File);
			Slot = Index;
			return Slot.Get();
		}

		FString ShortClassName(const UClass* InClass, const TCHAR* InPrefix)
		{
			FString Name = InClass != nullptr ? InClass->GetName() : FString();
			Name.RemoveFromEnd(TEXT("_C"));
			if (InPrefix != nullptr)
			{
				Name.RemoveFromStart(InPrefix);
			}
			return Name;
		}

		/** `Text` for UDreamText: the tag a file writes for a visual class, from the builder's own table. */
		FString VisualTag(const UClass* InClass)
		{
			static TMap<const UClass*, FString> Tags;
			if (Tags.Num() == 0)
			{
				TArray<TPair<FString, UClass*>> Pairs;
				FDreamUITextBuilder::GetVisualTags(Pairs);
				for (const TPair<FString, UClass*>& Pair : Pairs)
				{
					if (Pair.Value != nullptr && !Tags.Contains(Pair.Value))
					{
						Tags.Add(Pair.Value, Pair.Key);
					}
				}
			}
			for (const UClass* Class = InClass; Class != nullptr; Class = Class->GetSuperClass())
			{
				if (const FString* Found = Tags.Find(Class))
				{
					return *Found;
				}
			}
			return ShortClassName(InClass, TEXT("Dream"));
		}

		/** The anchors in words, for a node nothing else places. Y is up in DreamGUI: an anchor of 1 is the top. */
		FString DescribeAnchors(const FDreamUIAnchorData& InAnchors)
		{
			const bool bStretchX = !FMath::IsNearlyEqual(InAnchors.AnchorMin.X, InAnchors.AnchorMax.X);
			const bool bStretchY = !FMath::IsNearlyEqual(InAnchors.AnchorMin.Y, InAnchors.AnchorMax.Y);
			if (bStretchX && bStretchY)
			{
				const bool bFull = FMath::IsNearlyZero(InAnchors.AnchorMin.X) && FMath::IsNearlyZero(InAnchors.AnchorMin.Y)
					&& FMath::IsNearlyEqual(InAnchors.AnchorMax.X, 1.0) && FMath::IsNearlyEqual(InAnchors.AnchorMax.Y, 1.0);
				return bFull ? TEXT("fills its parent") : TEXT("stretches with its parent");
			}
			const auto Horizontal = [](double X) -> FString
			{
				return X <= 0.01 ? TEXT("left") : X >= 0.99 ? TEXT("right") : FMath::IsNearlyEqual(X, 0.5, 0.01) ? TEXT("centre") : FString::Printf(TEXT("%.0f%% across"), X * 100.0);
			};
			const auto Vertical = [](double Y) -> FString
			{
				return Y <= 0.01 ? TEXT("bottom") : Y >= 0.99 ? TEXT("top") : FMath::IsNearlyEqual(Y, 0.5, 0.01) ? TEXT("middle") : FString::Printf(TEXT("%.0f%% up"), Y * 100.0);
			};
			if (bStretchX)
			{
				return FString::Printf(TEXT("stretches across, held at the %s"), *Vertical(InAnchors.AnchorMin.Y));
			}
			if (bStretchY)
			{
				return FString::Printf(TEXT("stretches up and down, held at the %s"), *Horizontal(InAnchors.AnchorMin.X));
			}
			const FString H = Horizontal(InAnchors.AnchorMin.X);
			const FString V = Vertical(InAnchors.AnchorMin.Y);
			if (H == TEXT("centre") && V == TEXT("middle"))
			{
				return TEXT("centred");
			}
			return FString::Printf(TEXT("held at the %s %s"), *V, *H);
		}

		FString CollapseWhitespace(const FString& InText)
		{
			FString Out;
			Out.Reserve(InText.Len());
			bool bSpace = false;
			for (const TCHAR Char : InText)
			{
				if (FChar::IsWhitespace(Char))
				{
					bSpace = true;
					continue;
				}
				if (bSpace && !Out.IsEmpty())
				{
					Out.AppendChar(TEXT(' '));
				}
				bSpace = false;
				Out.AppendChar(Char);
			}
			return Out;
		}

		struct FCapture
		{
			FLayoutSnapshot& Out;
			const FTransform RootTransform;
			const double RootLeft;
			const double RootTop;
			const UDreamCanvas* StageCanvas;
			FAstIndex MainIndex;
			TMap<const UClass*, TSharedPtr<FAstIndex>> ComponentIndices;

			FCapture(FLayoutSnapshot& InOut, const UDreamWidget* InRoot, const UDreamCanvas* InCanvas)
				: Out(InOut)
				, RootTransform(InRoot->GetWorldTransform())
				, RootLeft(InRoot->GetLocalSpaceLeft())
				, RootTop(InRoot->GetLocalSpaceTop())
				, StageCanvas(InCanvas)
			{
			}

			/** A point of the widget's own plane, in viewport pixels; Depth is how far it stands out of the canvas plane. */
			FVector2D ToPixels(const UDreamWidget* InWidget, const FVector2D& InLocal, double& OutDepth) const
			{
				const FVector World = InWidget->GetWorldTransform().TransformPosition(FVector(0.0, InLocal.X, InLocal.Y));
				const FVector InRoot = RootTransform.InverseTransformPosition(World);
				OutDepth = InRoot.X;
				return FVector2D(InRoot.Y - RootLeft, RootTop - InRoot.Z);
			}

			/** The file a widget was written in and its node there, through the component that built it when it was one. */
			const FDreamUINode* FindNode(const FString& InPathInFile, const FString& InName, const FAstIndex* InIndex) const
			{
				if (InIndex == nullptr)
				{
					return nullptr;
				}
				if (const FDreamUINode* const* Found = InIndex->ByPath.Find(InPathInFile))
				{
					return *Found;
				}
				const FDreamUINode* const* Found = InIndex->ById.Find(InName);
				return Found != nullptr ? *Found : nullptr;
			}

			void Visit(UDreamWidget* InWidget, const int32 InParent, const int32 InDepth, const FString& InPath,
				const FAstIndex* InIndex, const FString& InPathInFile, const FBox2D& InClip, bool bInClipped)
			{
				if (!IsValid(InWidget))
				{
					return;
				}
				const int32 Index = Out.Nodes.AddDefaulted();
				FLayoutNode& Node = Out.Nodes[Index];
				Node.Parent = InParent;
				Node.Depth = InDepth;
				Node.Name = InWidget->GetDisplayName();
				Node.Path = InPath;
				if (InParent != INDEX_NONE)
				{
					Out.Nodes[InParent].Children.Add(Index);
				}

				// Where it is: the outline's four corners in viewport pixels, bounded.
				const double Left = InWidget->GetLocalSpaceLeft();
				const double Right = InWidget->GetLocalSpaceRight();
				const double Bottom = InWidget->GetLocalSpaceBottom();
				const double Top = InWidget->GetLocalSpaceTop();
				FBox2D Box(ForceInit);
				double MaxDepth = 0.0;
				for (const FVector2D Corner : { FVector2D(Left, Bottom), FVector2D(Right, Bottom), FVector2D(Left, Top), FVector2D(Right, Top) })
				{
					double Depth = 0.0;
					Box += ToPixels(InWidget, Corner, Depth);
					MaxDepth = FMath::Max(MaxDepth, FMath::Abs(Depth));
				}
				Node.X = Box.Min.X;
				Node.Y = Box.Min.Y;
				Node.W = Box.Max.X - Box.Min.X;
				Node.H = Box.Max.Y - Box.Min.Y;
				Node.b3D = MaxDepth > 0.5;

				// What of it can show: cut to every clipping ancestor's rect.
				FBox2D Visible = Box;
				if (InClip.bIsValid)
				{
					Visible.Min.X = FMath::Max(Visible.Min.X, InClip.Min.X);
					Visible.Min.Y = FMath::Max(Visible.Min.Y, InClip.Min.Y);
					Visible.Max.X = FMath::Min(Visible.Max.X, InClip.Max.X);
					Visible.Max.Y = FMath::Min(Visible.Max.Y, InClip.Max.Y);
				}
				const bool bEmpty = Visible.Max.X <= Visible.Min.X || Visible.Max.Y <= Visible.Min.Y;
				Node.bClipped = bInClipped && (bEmpty || !Visible.Equals(Box));
				Node.VisX = bEmpty ? 0.0 : Visible.Min.X;
				Node.VisY = bEmpty ? 0.0 : Visible.Min.Y;
				Node.VisW = bEmpty ? 0.0 : Visible.Max.X - Visible.Min.X;
				Node.VisH = bEmpty ? 0.0 : Visible.Max.Y - Visible.Min.Y;
				const EDreamWidgetClipping Clipping = InWidget->GetClipping();
				Node.bClipsChildren = Clipping != EDreamWidgetClipping::Inherit && Clipping != EDreamWidgetClipping::Disabled;
				FBox2D ChildClip = InClip;
				if (Node.bClipsChildren)
				{
					ChildClip = Clipping == EDreamWidgetClipping::ClipToBoundsWithoutIntersecting || !InClip.bIsValid ? Box : FBox2D(Visible.Min, Visible.Max);
					ChildClip.bIsValid = true;
				}

				Node.bActive = InWidget->GetWidgetActiveInHierarchy();
				Node.bShown = InWidget->GetRenderVisibleInHierarchy();
				Node.Opacity = InWidget->GetRenderOpacity();
				Node.Order = InWidget->GetFlattenHierarchyIndex();
				if (const UDreamCanvas* Canvas = InWidget->GetRenderCanvas(); Canvas != nullptr && Canvas != StageCanvas)
				{
					Node.Canvas = Canvas->GetWidget() != nullptr ? Canvas->GetWidget()->GetDisplayName() : Canvas->GetName();
				}
				for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
				{
					const UUISelectable* Selectable = Cast<UUISelectable>(Behaviour);
					if (Selectable != nullptr && Selectable->IsInteractable() && InWidget->GetInteractableInHierarchy())
					{
						Node.bInteractive = true;
					}
				}
				if (const UDreamLayoutContainer* Container = InWidget->GetLayoutContainer())
				{
					Node.Container = ShortClassName(Container->GetClass(), TEXT("DreamLayoutContainer"));
				}
				const UDreamWidget* ParentWidget = InWidget->GetParent();
				const UDreamLayoutContainer* ParentContainer = ParentWidget != nullptr ? ParentWidget->GetLayoutContainer() : nullptr;
				if (InParent != INDEX_NONE && ParentContainer != nullptr)
				{
					Node.PlacedBy = ShortClassName(ParentContainer->GetClass(), TEXT("DreamLayoutContainer"));
				}
				else
				{
					Node.Anchors = DescribeAnchors(InWidget->GetAnchorData());
				}

				if (const UDreamVisual* Visual = InWidget->GetVisual())
				{
					Node.Visual = VisualTag(Visual->GetClass());
					Node.Color = Visual->GetColor();
					if (const UDreamText* Text = Cast<UDreamText>(Visual))
					{
						Node.bHasText = true;
						Node.Text = Text->GetText().ToString();
						Node.FontSize = Text->GetFontSize();
						Node.bTextCut = Text->IsTextTruncated();
						const FDreamTextFit Fit = Text->MeasureFit(Node.Text, FString());
						if (Fit.bMeasured)
						{
							Node.bTextOverflows = Fit.bOverflows;
							Node.TextNeeded = Fit.Needed;
							Node.TextBox = Fit.Box;
						}
					}
				}

				// Which file wrote it: the screen's, or -- inside a component -- the component's own.
				const FDreamUINode* Authored = FindNode(InPathInFile, Node.Name, InIndex);
				Node.bAuthored = Authored != nullptr;
				if (Authored != nullptr)
				{
					Node.Type = Authored->TypeName.IsEmpty() ? TEXT("Widget") : Authored->TypeName;
					Node.Line = Authored->Location.Line;
					Node.Column = Authored->Location.Column;
					Node.File = MakeProjectRelativePath(InIndex->File);
				}
				else if (const UDreamUserWidget* Component = Cast<UDreamUserWidget>(InWidget))
				{
					Node.Type = ShortClassName(Component->GetClass(), nullptr);
				}
				else
				{
					Node.Type = !Node.Visual.IsEmpty() ? Node.Visual : TEXT("Widget");
				}

				// Below a component instance the file is the component's, from its own root.
				const FAstIndex* ChildIndex = InIndex;
				FString ChildPathBase = InPathInFile;
				if (const UDreamUserWidget* Component = Cast<UDreamUserWidget>(InWidget); Component != nullptr && InParent != INDEX_NONE)
				{
					ChildIndex = FindComponentIndex(Component->GetClass(), ComponentIndices);
					ChildPathBase = FString();
				}
				for (UDreamWidget* Child : InWidget->GetChildren())
				{
					if (!IsValid(Child))
					{
						continue;
					}
					const FString ChildName = Child->GetDisplayName();
					const FString ChildPath = InPath + TEXT("/") + ChildName;
					const FString ChildPathInFile = ChildPathBase.IsEmpty() ? ChildName : ChildPathBase + TEXT("/") + ChildName;
					Visit(Child, Index, InDepth + 1, ChildPath, ChildIndex, ChildPathInFile, ChildClip, bInClipped || Node.bClipsChildren);
				}
			}
		};

		bool Intersects(const FLayoutNode& A, const FLayoutNode& B, double InMin, double& OutW, double& OutH)
		{
			OutW = FMath::Min(A.VisX + A.VisW, B.VisX + B.VisW) - FMath::Max(A.VisX, B.VisX);
			OutH = FMath::Min(A.VisY + A.VisH, B.VisY + B.VisH) - FMath::Max(A.VisY, B.VisY);
			return OutW >= InMin && OutH >= InMin;
		}

		bool IsAncestor(const FLayoutSnapshot& InSnapshot, int32 InAncestor, int32 InNode)
		{
			for (int32 Walk = InSnapshot.Nodes[InNode].Parent; Walk != INDEX_NONE; Walk = InSnapshot.Nodes[Walk].Parent)
			{
				if (Walk == InAncestor)
				{
					return true;
				}
			}
			return false;
		}

		int32 CommonAncestor(const FLayoutSnapshot& InSnapshot, int32 InA, int32 InB, int32& OutStepsA, int32& OutStepsB)
		{
			OutStepsA = 0;
			for (int32 A = InSnapshot.Nodes[InA].Parent; A != INDEX_NONE; A = InSnapshot.Nodes[A].Parent)
			{
				++OutStepsA;
				OutStepsB = 0;
				for (int32 B = InSnapshot.Nodes[InB].Parent; B != INDEX_NONE; B = InSnapshot.Nodes[B].Parent)
				{
					++OutStepsB;
					if (A == B)
					{
						return A;
					}
				}
			}
			return INDEX_NONE;
		}

		/** Whether any ancestor is drawn as nothing; one hidden branch is one finding, not one per node in it. */
		bool IsUnderProblem(const FLayoutSnapshot& InSnapshot, int32 InNode, const TSet<int32>& InFlagged)
		{
			for (int32 Walk = InSnapshot.Nodes[InNode].Parent; Walk != INDEX_NONE; Walk = InSnapshot.Nodes[Walk].Parent)
			{
				if (InFlagged.Contains(Walk))
				{
					return true;
				}
			}
			return false;
		}

		FString Px(double InValue)
		{
			return FString::Printf(TEXT("%d"), FMath::RoundToInt(InValue));
		}

		/**
		 * How a node is named in a message: by its type and id as the file writes them. A part a control made for itself (a
		 * button's face) is named by the control the file wrote, which is the line an author can change.
		 */
		FString Who(const FLayoutSnapshot& InSnapshot, int32 InIndex)
		{
			int32 Walk = InIndex;
			while (Walk != INDEX_NONE && !InSnapshot.Nodes[Walk].bAuthored)
			{
				Walk = InSnapshot.Nodes[Walk].Parent;
			}
			const FLayoutNode& Node = InSnapshot.Nodes[Walk != INDEX_NONE ? Walk : InIndex];
			return FString::Printf(TEXT("%s %s"), *Node.Type, *Node.Name);
		}

		/** WCAG's relative luminance of an sRGB colour. */
		double RelativeLuminance(const FColor& InColor)
		{
			const auto Channel = [](uint8 Value)
			{
				const double C = Value / 255.0;
				return C <= 0.04045 ? C / 12.92 : FMath::Pow((C + 0.055) / 1.055, 2.4);
			};
			return 0.2126 * Channel(InColor.R) + 0.7152 * Channel(InColor.G) + 0.0722 * Channel(InColor.B);
		}
	}

	int32 FLayoutSnapshot::FindByPath(const FString& InPathOrName) const
	{
		FString Wanted = InPathOrName.TrimStartAndEnd();
		Wanted.RemoveFromStart(TEXT("/"));
		Wanted.RemoveFromEnd(TEXT("/"));
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (Nodes[Index].Path == Wanted)
			{
				return Index;
			}
		}
		// A path below the root, or a bare id that names one node.
		int32 Found = INDEX_NONE;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (Nodes[Index].Path.EndsWith(TEXT("/") + Wanted) || Nodes[Index].Name == Wanted)
			{
				if (Found != INDEX_NONE)
				{
					return Found; // the first in reading order
				}
				Found = Index;
			}
		}
		return Found;
	}

	void CaptureLayout(const FScreenStage& InStage, const FDreamUIAst* InAst, const FString& InSourceFile, FLayoutSnapshot& OutSnapshot)
	{
		OutSnapshot = FLayoutSnapshot();
		OutSnapshot.Viewport = InStage.GetOptions().Viewport;
		UDreamWidget* StageRoot = InStage.GetRoot();
		UDreamWidget* ContentRoot = InStage.GetContentRoot();
		if (!IsValid(StageRoot) || !IsValid(ContentRoot))
		{
			return;
		}
		FCapture Capture(OutSnapshot, StageRoot, InStage.GetCanvas());
		if (InAst != nullptr)
		{
			Capture.MainIndex.Build(*InAst, InSourceFile);
		}
		const FString RootName = ContentRoot->GetDisplayName();
		FBox2D NoClip(ForceInit);
		Capture.Visit(ContentRoot, INDEX_NONE, 0, RootName, InAst != nullptr ? &Capture.MainIndex : nullptr, RootName, NoClip, false);
	}

	void LintLayout(const FLayoutSnapshot& InSnapshot, TArray<FLayoutIssue>& OutIssues)
	{
		using namespace LayoutLint;
		const TArray<FLayoutNode>& Nodes = InSnapshot.Nodes;
		const double VW = InSnapshot.Viewport.X;
		const double VH = InSnapshot.Viewport.Y;

		// DUI8001: off the screen. What can show of a node (its rect cut to its clipping ancestors) reaching past an edge.
		TSet<int32> OffScreen;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FLayoutNode& Node = Nodes[Index];
			if (!Node.IsDrawn() || Node.VisW <= ZeroSize || Node.VisH <= ZeroSize || IsUnderProblem(InSnapshot, Index, OffScreen))
			{
				continue;
			}
			const double PastLeft = -Node.VisX;
			const double PastTop = -Node.VisY;
			const double PastRight = Node.VisX + Node.VisW - VW;
			const double PastBottom = Node.VisY + Node.VisH - VH;
			const bool bOutside = Node.VisX >= VW || Node.VisY >= VH || Node.VisX + Node.VisW <= 0.0 || Node.VisY + Node.VisH <= 0.0;
			TArray<FString> Edges;
			if (PastLeft > OffScreenTolerance) { Edges.Add(FString::Printf(TEXT("%s px past the left edge"), *Px(PastLeft))); }
			if (PastTop > OffScreenTolerance) { Edges.Add(FString::Printf(TEXT("%s px past the top"), *Px(PastTop))); }
			if (PastRight > OffScreenTolerance) { Edges.Add(FString::Printf(TEXT("%s px past the right edge"), *Px(PastRight))); }
			if (PastBottom > OffScreenTolerance) { Edges.Add(FString::Printf(TEXT("%s px past the bottom"), *Px(PastBottom))); }
			if (!bOutside && Edges.Num() == 0)
			{
				continue;
			}
			OffScreen.Add(Index);
			FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutOffScreen);
			Issue.Node = Index;
			Issue.Message = bOutside
				? FString::Printf(TEXT("%s is entirely outside the %dx%d viewport (at x=%s y=%s)"), *Who(InSnapshot, Index), InSnapshot.Viewport.X, InSnapshot.Viewport.Y, *Px(Node.X), *Px(Node.Y))
				: FString::Printf(TEXT("%s reaches off the screen: %s"), *Who(InSnapshot, Index), *FString::Join(Edges, TEXT(", ")));
			Issue.Hint = Node.PlacedBy.IsEmpty()
				? TEXT("Anchor it inside its parent (AnchorData.AnchorMin/AnchorMax) and bring AnchoredPosition and SizeDelta within it.")
				: FString::Printf(TEXT("Its parent's %s places it: give the parent more room, or make the children smaller."), *Node.PlacedBy);
			Issue.Key = FString::Printf(TEXT("8001|%s"), *Node.Path);
			Issue.Weight = bOutside ? 3.0 : 2.0;
		}

		// DUI8002: a text cut by its overflow policy or laid out past its box.
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FLayoutNode& Node = Nodes[Index];
			if (!Node.IsDrawn() || !Node.bHasText || Node.Text.TrimStartAndEnd().IsEmpty() || (!Node.bTextCut && !Node.bTextOverflows))
			{
				continue;
			}
			FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutTextOverflow);
			Issue.Node = Index;
			const FString Sizes = Node.TextNeeded.X > 0.0f
				? FString::Printf(TEXT("it needs %sx%s px and has %sx%s"), *Px(Node.TextNeeded.X), *Px(Node.TextNeeded.Y), *Px(Node.TextBox.X), *Px(Node.TextBox.Y))
				: FString();
			Issue.Message = FString::Printf(TEXT("the text of %s, %s, %s%s%s"), *Who(InSnapshot, Index), *QuoteText(Node.Text),
				Node.bTextCut ? TEXT("is cut short") : TEXT("does not fit its box"), Sizes.IsEmpty() ? TEXT("") : TEXT(": "), *Sizes);
			Issue.Hint = Node.TextBox.X < 2.0f
				? TEXT("Its box has no width: give it one (AnchorData.SizeDelta, or WrapTextAt when a container gives it its width).")
				: TEXT("Give it more room (SizeDelta, or @slot SizeRule = Auto in a box), break it with WrapTextAt, or lower FontSize.");
			Issue.Key = FString::Printf(TEXT("8002|%s"), *Node.Path);
			Issue.Weight = 2.5;
		}

		// DUI8003: of zero size, or a control too small to hit.
		TSet<int32> Zero;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FLayoutNode& Node = Nodes[Index];
			if (!Node.IsDrawn() || IsUnderProblem(InSnapshot, Index, Zero) || Node.bClipped)
			{
				continue;
			}
			const bool bZero = Node.W < ZeroSize || Node.H < ZeroSize;
			if (bZero)
			{
				// A plain widget with nothing in it may be a marker or a pivot; one that draws or holds children is not.
				const bool bMatters = !Node.Visual.IsEmpty() || Node.bInteractive || Node.Children.Num() > 0;
				if (!bMatters || (Node.bHasText && Node.Text.IsEmpty()))
				{
					continue;
				}
				Zero.Add(Index);
				FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
				Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutTooSmall);
				Issue.Node = Index;
				Issue.Severity = (!Node.Visual.IsEmpty() || Node.bInteractive) ? TEXT("warning") : TEXT("info");
				Issue.Message = FString::Printf(TEXT("%s is %sx%s px, so %s"), *Who(InSnapshot, Index), *Px(Node.W), *Px(Node.H),
					!Node.Visual.IsEmpty() ? TEXT("what it draws does not show") : Node.bInteractive ? TEXT("it cannot be pressed") : TEXT("its children are laid out in no room"));
				Issue.Hint = Node.PlacedBy.IsEmpty()
					? TEXT("Give it a size: AnchorData.SizeDelta = (w, h), or anchors that stretch it with its parent.")
					: FString::Printf(TEXT("Its parent's %s sizes it from what it wants: give it a size (SizeDelta, MinDesiredSize) or @slot SizeRule = Fill."), *Node.PlacedBy);
				Issue.Key = FString::Printf(TEXT("8003|%s"), *Node.Path);
				Issue.Weight = 2.2;
				continue;
			}
			if (Node.bInteractive && (Node.W < MinTargetSize || Node.H < MinTargetSize))
			{
				FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
				Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutTooSmall);
				Issue.Node = Index;
				Issue.Message = FString::Printf(TEXT("%s can be pressed but is only %sx%s px; a pointer needs at least %dx%d"),
					*Who(InSnapshot, Index), *Px(Node.W), *Px(Node.H), FMath::RoundToInt(MinTargetSize), FMath::RoundToInt(MinTargetSize));
				Issue.Hint = TEXT("Make the control (not only what it draws) bigger: AnchorData.SizeDelta, MinDesiredSize, or padding around its content.");
				Issue.Key = FString::Printf(TEXT("8003|%s|small"), *Node.Path);
				Issue.Weight = 1.5;
			}
		}

		// DUI8004: two controls the pointer presses, one over the other.
		TArray<int32> Interactive;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (Nodes[Index].bInteractive && Nodes[Index].IsDrawn() && Nodes[Index].VisW > ZeroSize && Nodes[Index].VisH > ZeroSize)
			{
				Interactive.Add(Index);
			}
		}
		for (int32 I = 0; I < Interactive.Num(); ++I)
		{
			for (int32 J = I + 1; J < Interactive.Num(); ++J)
			{
				const int32 A = Interactive[I];
				const int32 B = Interactive[J];
				if (IsAncestor(InSnapshot, A, B) || IsAncestor(InSnapshot, B, A))
				{
					continue; // a control inside a control: one press, the inner one's
				}
				int32 StepsA = 0;
				int32 StepsB = 0;
				if (CommonAncestor(InSnapshot, A, B, StepsA, StepsB) == INDEX_NONE || StepsA > 2 || StepsB > 2)
				{
					continue; // siblings and cousins: what one panel puts side by side
				}
				double OverlapW = 0.0;
				double OverlapH = 0.0;
				if (!Intersects(Nodes[A], Nodes[B], LayoutLint::OverlapTolerance, OverlapW, OverlapH))
				{
					continue;
				}
				const int32 LowerIndex = Nodes[A].Order <= Nodes[B].Order ? A : B;
				const int32 UpperIndex = Nodes[A].Order <= Nodes[B].Order ? B : A;
				const FLayoutNode& Lower = Nodes[LowerIndex];
				const FLayoutNode& Upper = Nodes[UpperIndex];
				FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
				Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutOverlappingControls);
				Issue.Node = UpperIndex;
				Issue.Other = LowerIndex;
				Issue.Message = FString::Printf(TEXT("%s lies over %s by %sx%s px; a press there reaches only the one on top, %s"),
					*Who(InSnapshot, UpperIndex), *Who(InSnapshot, LowerIndex), *Px(OverlapW), *Px(OverlapH), *Who(InSnapshot, UpperIndex));
				Issue.Hint = TEXT("Put them in a box (VerticalBox, HorizontalBox, Grid) with Spacing, or move them apart.");
				Issue.Key = FString::Printf(TEXT("8004|%s|%s"), *Lower.Path, *Upper.Path);
				Issue.Weight = 1.8;
			}
		}

		// DUI8005: siblings whose edges or gaps nearly, but not quite, line up.
		for (int32 Parent = 0; Parent < Nodes.Num(); ++Parent)
		{
			TArray<int32> Siblings;
			for (const int32 Child : Nodes[Parent].Children)
			{
				const FLayoutNode& Node = Nodes[Child];
				if (Node.IsDrawn() && Node.W > ZeroSize && Node.H > ZeroSize && !Node.b3D)
				{
					Siblings.Add(Child);
				}
			}
			if (Siblings.Num() < 2)
			{
				continue;
			}
			struct FEdge { const TCHAR* Name; double (*Get)(const FLayoutNode&); };
			static const FEdge Edges[] =
			{
				{ TEXT("left edges"), [](const FLayoutNode& N) { return N.X; } },
				{ TEXT("right edges"), [](const FLayoutNode& N) { return N.X + N.W; } },
				{ TEXT("tops"), [](const FLayoutNode& N) { return N.Y; } },
				{ TEXT("bottoms"), [](const FLayoutNode& N) { return N.Y + N.H; } },
				{ TEXT("centres across"), [](const FLayoutNode& N) { return N.X + N.W * 0.5; } },
				{ TEXT("centres up and down"), [](const FLayoutNode& N) { return N.Y + N.H * 0.5; } },
			};
			TSet<FString> Reported;
			for (const FEdge& Edge : Edges)
			{
				for (int32 I = 0; I < Siblings.Num(); ++I)
				{
					for (int32 J = I + 1; J < Siblings.Num(); ++J)
					{
						const FLayoutNode& A = Nodes[Siblings[I]];
						const FLayoutNode& B = Nodes[Siblings[J]];
						const double Delta = FMath::Abs(Edge.Get(A) - Edge.Get(B));
						if (Delta <= NearMissLow || Delta > NearMissHigh)
						{
							continue;
						}
						// Exactly aligned with a third sibling is the intent; then this pair's miss is the slip.
						const FString PairKey = FString::Printf(TEXT("%s|%s"), *A.Path, *B.Path);
						if (Reported.Contains(PairKey))
						{
							continue;
						}
						Reported.Add(PairKey);
						FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
						Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutNearlyAligned);
						Issue.Severity = TEXT("info");
						Issue.Node = Siblings[J];
						Issue.Other = Siblings[I];
						Issue.Message = FString::Printf(TEXT("the %s of %s and %s miss by %.0f px (%s and %s): they look meant to line up"),
							Edge.Name, *A.Name, *B.Name, Delta, *Px(Edge.Get(A)), *Px(Edge.Get(B)));
						Issue.Hint = Nodes[Parent].Container.IsEmpty()
							? TEXT("Give them the same AnchoredPosition / SizeDelta value on that axis, or put them in a box that aligns them.")
							: FString::Printf(TEXT("%s places them: give them the same size, or the same @slot alignment."), *Nodes[Parent].Container);
						Issue.Key = FString::Printf(TEXT("8005|%s|%s"), *PairKey, Edge.Name);
						Issue.Weight = 0.5;
					}
				}
			}
			// Uneven gaps in a row or a column of three or more.
			for (const bool bVertical : { true, false })
			{
				TArray<int32> Line = Siblings;
				Line.Sort([&Nodes, bVertical](int32 L, int32 R) { return bVertical ? Nodes[L].Y < Nodes[R].Y : Nodes[L].X < Nodes[R].X; });
				TArray<double> Gaps;
				bool bLine = Line.Num() >= 3;
				for (int32 I = 1; I < Line.Num() && bLine; ++I)
				{
					const FLayoutNode& Prev = Nodes[Line[I - 1]];
					const FLayoutNode& Cur = Nodes[Line[I]];
					const double Gap = bVertical ? Cur.Y - (Prev.Y + Prev.H) : Cur.X - (Prev.X + Prev.W);
					// A column overlaps on X, a row on Y: siblings scattered about are not a line.
					const double Shared = bVertical
						? FMath::Min(Prev.X + Prev.W, Cur.X + Cur.W) - FMath::Max(Prev.X, Cur.X)
						: FMath::Min(Prev.Y + Prev.H, Cur.Y + Cur.H) - FMath::Max(Prev.Y, Cur.Y);
					bLine = Gap >= 0.0 && Shared > 0.0;
					Gaps.Add(Gap);
				}
				if (!bLine)
				{
					continue;
				}
				double MinGap = TNumericLimits<double>::Max();
				double MaxGap = 0.0;
				for (const double Gap : Gaps)
				{
					MinGap = FMath::Min(MinGap, Gap);
					MaxGap = FMath::Max(MaxGap, Gap);
				}
				const double Spread = MaxGap - MinGap;
				if (Spread <= NearMissLow || Spread > NearMissHigh)
				{
					continue;
				}
				TArray<FString> GapTexts;
				for (const double Gap : Gaps)
				{
					GapTexts.Add(Px(Gap));
				}
				FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
				Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutNearlyAligned);
				Issue.Severity = TEXT("info");
				Issue.Node = Parent;
				Issue.Message = FString::Printf(TEXT("the %d children of %s stand %s with gaps of %s px: nearly, not exactly, even"),
					Line.Num(), *Who(InSnapshot, Parent), bVertical ? TEXT("one above another") : TEXT("side by side"), *FString::Join(GapTexts, TEXT(", ")));
				Issue.Hint = TEXT("Use one box with Spacing (VerticalBox / HorizontalBox) instead of positions, or make the gaps the same.");
				Issue.Key = FString::Printf(TEXT("8005|%s|gaps|%d"), *Nodes[Parent].Path, bVertical ? 1 : 0);
				Issue.Weight = 0.6;
			}
		}
	}

	void LintContrast(const FLayoutSnapshot& InSnapshot, const TArray<FColor>& InPixels, const FIntPoint InSize, TArray<FLayoutIssue>& OutIssues)
	{
		using namespace LayoutLint;
		if (InPixels.Num() != InSize.X * InSize.Y || InSize.X <= 0)
		{
			return;
		}
		const double ScaleX = static_cast<double>(InSize.X) / FMath::Max(1, InSnapshot.Viewport.X);
		const double ScaleY = static_cast<double>(InSize.Y) / FMath::Max(1, InSnapshot.Viewport.Y);
		for (int32 Index = 0; Index < InSnapshot.Nodes.Num(); ++Index)
		{
			const FLayoutNode& Node = InSnapshot.Nodes[Index];
			if (!Node.IsDrawn() || !Node.bHasText || Node.Text.TrimStartAndEnd().IsEmpty() || Node.VisW < 2.0 || Node.VisH < 2.0 || Node.b3D)
			{
				continue;
			}
			// What is behind the text: the pixels of a one-pixel ring just inside its box, where glyphs rarely reach. The
			// median of their luminance, so a stray glyph pixel does not decide it.
			const int32 X0 = FMath::Clamp(FMath::FloorToInt(Node.VisX * ScaleX), 0, InSize.X - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Node.VisY * ScaleY), 0, InSize.Y - 1);
			const int32 X1 = FMath::Clamp(FMath::CeilToInt((Node.VisX + Node.VisW) * ScaleX) - 1, 0, InSize.X - 1);
			const int32 Y1 = FMath::Clamp(FMath::CeilToInt((Node.VisY + Node.VisH) * ScaleY) - 1, 0, InSize.Y - 1);
			TArray<TPair<double, FColor>> Ring;
			for (int32 X = X0; X <= X1; ++X)
			{
				Ring.Emplace(0.0, InPixels[Y0 * InSize.X + X]);
				Ring.Emplace(0.0, InPixels[Y1 * InSize.X + X]);
			}
			for (int32 Y = Y0 + 1; Y < Y1; ++Y)
			{
				Ring.Emplace(0.0, InPixels[Y * InSize.X + X0]);
				Ring.Emplace(0.0, InPixels[Y * InSize.X + X1]);
			}
			if (Ring.Num() == 0)
			{
				continue;
			}
			for (TPair<double, FColor>& Pixel : Ring)
			{
				Pixel.Key = RelativeLuminance(Pixel.Value);
			}
			Ring.Sort([](const TPair<double, FColor>& L, const TPair<double, FColor>& R) { return L.Key < R.Key; });
			const FColor Behind = Ring[Ring.Num() / 2].Value;
			// The text as it is drawn over that: its colour at its alpha and opacity.
			const float Alpha = FMath::Clamp(Node.Color.A / 255.0f * Node.Opacity, 0.0f, 1.0f);
			const FLinearColor Mixed = FMath::Lerp(FLinearColor(Behind), FLinearColor(Node.Color), Alpha);
			const FColor Ink = Mixed.ToFColor(true);
			const double LInk = RelativeLuminance(Ink);
			const double LBehind = RelativeLuminance(Behind);
			const double Ratio = (FMath::Max(LInk, LBehind) + 0.05) / (FMath::Min(LInk, LBehind) + 0.05);
			const bool bLarge = Node.FontSize >= LargeText;
			FString Severity;
			if (Ratio < ContrastPoor)
			{
				Severity = TEXT("warning");
			}
			else if (Ratio < ContrastLow && !bLarge)
			{
				Severity = TEXT("info");
			}
			else
			{
				continue;
			}
			FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutLowContrast);
			Issue.Severity = Severity;
			Issue.Node = Index;
			Issue.Message = FString::Printf(TEXT("the text of %s, %s, stands out from what is behind it only %.1f to 1 (#%02X%02X%02X on #%02X%02X%02X); %s"),
				*Who(InSnapshot, Index), *QuoteText(Node.Text), Ratio, Ink.R, Ink.G, Ink.B, Behind.R, Behind.G, Behind.B,
				Ratio < ContrastPoor ? TEXT("it is hard to read") : TEXT("small text needs 4.5 to 1"));
			Issue.Hint = LInk > LBehind
				? TEXT("Make the text lighter or what is behind it darker (Color, the backdrop's tint).")
				: TEXT("Make the text darker or what is behind it lighter (Color, the backdrop's tint).");
			Issue.Key = FString::Printf(TEXT("8007|%s"), *Node.Path);
			Issue.Weight = Ratio < ContrastPoor ? 2.0 : 0.8;
		}
	}

	FDiagnostic MakeLayoutDiagnostic(const FLayoutSnapshot& InSnapshot, const FLayoutIssue& InIssue)
	{
		FDiagnostic Out;
		Out.Code = FormatDiagnosticCode(InIssue.Code);
		Out.Severity = InIssue.Severity;
		Out.Message = InIssue.Message;
		Out.Hint = InIssue.Hint;
		Out.Stage = DescribeDiagnosticStage(InIssue.Code);
		Out.Doc = MakeDiagnosticDocUrl(InIssue.Code);
		if (InSnapshot.Nodes.IsValidIndex(InIssue.Node))
		{
			// The line of the node, or of the nearest ancestor the file wrote (a control's own parts have none).
			int32 Walk = InIssue.Node;
			while (Walk != INDEX_NONE && !InSnapshot.Nodes[Walk].bAuthored)
			{
				Walk = InSnapshot.Nodes[Walk].Parent;
			}
			const FLayoutNode& Node = InSnapshot.Nodes[InIssue.Node];
			Out.Node = Node.Path;
			if (Walk != INDEX_NONE)
			{
				Out.File = InSnapshot.Nodes[Walk].File;
				Out.Line = InSnapshot.Nodes[Walk].Line;
				Out.Column = InSnapshot.Nodes[Walk].Column;
			}
		}
		return Out;
	}

	FString QuoteText(const FString& InText)
	{
		FString Text = CollapseWhitespace(InText);
		Text.ReplaceInline(TEXT("\""), TEXT("'"));
		if (Text.Len() > 40)
		{
			Text = Text.Left(37) + TEXT("...");
		}
		return FString::Printf(TEXT("\"%s\""), *Text);
	}

	FString FormatLayoutTree(const FLayoutSnapshot& InSnapshot, const int32 InMaxDepth, const int32 InMaxLines, int32& OutShown, int32& OutHidden)
	{
		OutShown = 0;
		OutHidden = 0;
		if (!InSnapshot.Nodes.IsValidIndex(InSnapshot.Focus))
		{
			return FString();
		}
		TArray<FString> Lines;
		const int32 BaseDepth = InSnapshot.Nodes[InSnapshot.Focus].Depth;
		TFunction<void(int32)> Emit = [&](int32 Index)
		{
			const FLayoutNode& Node = InSnapshot.Nodes[Index];
			const int32 Level = Node.Depth - BaseDepth;
			if (Level > InMaxDepth || Lines.Num() >= InMaxLines)
			{
				++OutHidden;
				for (const int32 Child : Node.Children)
				{
					Emit(Child);
				}
				return;
			}
			FString Line = FString::ChrN(Level * 2, TEXT(' '));
			Line += FString::Printf(TEXT("%s %s  x=%s y=%s w=%s h=%s"), *Node.Type, *Node.Name, *Px(Node.X), *Px(Node.Y), *Px(Node.W), *Px(Node.H));
			if (Node.bHasText)
			{
				Line += FString::Printf(TEXT("  text=%s"), *QuoteText(Node.Text));
				if (Node.FontSize > 0.0f)
				{
					Line += FString::Printf(TEXT(" size=%s"), *Px(Node.FontSize));
				}
				if (Node.bTextCut) { Line += TEXT(" CUT"); }
				else if (Node.bTextOverflows) { Line += FString::Printf(TEXT(" OVERFLOWS(needs %sx%s)"), *Px(Node.TextNeeded.X), *Px(Node.TextNeeded.Y)); }
			}
			TArray<FString> Notes;
			if (!Node.bActive) { Notes.Add(TEXT("inactive")); }
			else if (!Node.bShown) { Notes.Add(TEXT("hidden")); }
			else if (Node.Opacity <= 0.0f) { Notes.Add(TEXT("transparent")); }
			if (Node.bInteractive) { Notes.Add(TEXT("pressable")); }
			if (!Node.Container.IsEmpty()) { Notes.Add(Node.Container); }
			if (!Node.Anchors.IsEmpty() && Node.Depth > 0) { Notes.Add(Node.Anchors); }
			if (Node.bClipped) { Notes.Add(Node.VisW <= 0.0 ? TEXT("clipped away") : TEXT("partly clipped")); }
			if (Node.b3D) { Notes.Add(TEXT("turned in 3D")); }
			if (!Node.Canvas.IsEmpty()) { Notes.Add(FString::Printf(TEXT("own canvas %s"), *Node.Canvas)); }
			if (!Node.bAuthored && Node.Depth > 0) { Notes.Add(TEXT("part of its control")); }
			if (Notes.Num() > 0)
			{
				Line += TEXT("  [") + FString::Join(Notes, TEXT(", ")) + TEXT("]");
			}
			Lines.Add(MoveTemp(Line));
			++OutShown;
			for (const int32 Child : Node.Children)
			{
				Emit(Child);
			}
		};
		Emit(InSnapshot.Focus);
		return FString::Join(Lines, TEXT("\n"));
	}

	TSharedRef<FJsonObject> LayoutTreeToJson(const FLayoutSnapshot& InSnapshot, const int32 InMaxDepth)
	{
		TFunction<TSharedRef<FJsonObject>(int32, int32)> Make = [&](int32 Index, int32 Level) -> TSharedRef<FJsonObject>
		{
			const FLayoutNode& Node = InSnapshot.Nodes[Index];
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("type"), Node.Type);
			Object->SetStringField(TEXT("name"), Node.Name);
			Object->SetStringField(TEXT("path"), Node.Path);
			TSharedRef<FJsonObject> Rect = MakeShared<FJsonObject>();
			Rect->SetNumberField(TEXT("x"), FMath::RoundToInt(Node.X));
			Rect->SetNumberField(TEXT("y"), FMath::RoundToInt(Node.Y));
			Rect->SetNumberField(TEXT("w"), FMath::RoundToInt(Node.W));
			Rect->SetNumberField(TEXT("h"), FMath::RoundToInt(Node.H));
			Object->SetObjectField(TEXT("rect"), Rect);
			Object->SetBoolField(TEXT("visible"), Node.IsDrawn());
			if (Node.bClipped)
			{
				TSharedRef<FJsonObject> Visible = MakeShared<FJsonObject>();
				Visible->SetNumberField(TEXT("x"), FMath::RoundToInt(Node.VisX));
				Visible->SetNumberField(TEXT("y"), FMath::RoundToInt(Node.VisY));
				Visible->SetNumberField(TEXT("w"), FMath::RoundToInt(Node.VisW));
				Visible->SetNumberField(TEXT("h"), FMath::RoundToInt(Node.VisH));
				Object->SetObjectField(TEXT("visibleRect"), Visible);
			}
			Object->SetNumberField(TEXT("order"), Node.Order);
			if (!Node.Canvas.IsEmpty()) { Object->SetStringField(TEXT("canvas"), Node.Canvas); }
			if (Node.bInteractive) { Object->SetBoolField(TEXT("pressable"), true); }
			if (!Node.Container.IsEmpty()) { Object->SetStringField(TEXT("container"), Node.Container); }
			if (!Node.PlacedBy.IsEmpty()) { Object->SetStringField(TEXT("placedBy"), Node.PlacedBy); }
			if (!Node.Anchors.IsEmpty()) { Object->SetStringField(TEXT("anchors"), Node.Anchors); }
			if (Node.b3D) { Object->SetBoolField(TEXT("in3d"), true); }
			if (Node.bHasText)
			{
				TSharedRef<FJsonObject> Text = MakeShared<FJsonObject>();
				Text->SetStringField(TEXT("text"), CollapseWhitespace(Node.Text).Left(200));
				Text->SetNumberField(TEXT("fontSize"), Node.FontSize);
				Text->SetBoolField(TEXT("cut"), Node.bTextCut);
				Text->SetBoolField(TEXT("overflows"), Node.bTextOverflows);
				if (Node.TextNeeded.X > 0.0f)
				{
					Text->SetNumberField(TEXT("neededW"), FMath::RoundToInt(Node.TextNeeded.X));
					Text->SetNumberField(TEXT("neededH"), FMath::RoundToInt(Node.TextNeeded.Y));
				}
				Object->SetObjectField(TEXT("text"), Text);
			}
			if (Node.Line > 0)
			{
				Object->SetStringField(TEXT("file"), Node.File);
				Object->SetNumberField(TEXT("line"), Node.Line);
			}
			if (Level < InMaxDepth && Node.Children.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Children;
				for (const int32 Child : Node.Children)
				{
					Children.Add(MakeShared<FJsonValueObject>(Make(Child, Level + 1)));
				}
				Object->SetArrayField(TEXT("children"), Children);
			}
			else if (Node.Children.Num() > 0)
			{
				Object->SetNumberField(TEXT("hiddenChildren"), Node.Children.Num());
			}
			return Object;
		};
		return InSnapshot.Nodes.IsValidIndex(InSnapshot.Focus) ? Make(InSnapshot.Focus, 0) : MakeShared<FJsonObject>();
	}
}
