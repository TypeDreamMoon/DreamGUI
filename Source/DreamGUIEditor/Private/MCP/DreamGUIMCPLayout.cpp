// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MCP/DreamGUIMCPLayout.h"

#include "MCP/DreamGUIMCPStage.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
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
			const FMatrix ViewProjection;
			FAstIndex MainIndex;
			TMap<const UClass*, TSharedPtr<FAstIndex>> ComponentIndices;

			FCapture(FLayoutSnapshot& InOut, const UDreamWidget* InRoot, const UDreamCanvas* InCanvas)
				: Out(InOut)
				, RootTransform(InRoot->GetWorldTransform())
				, RootLeft(InRoot->GetLocalSpaceLeft())
				, RootTop(InRoot->GetLocalSpaceTop())
				, StageCanvas(InCanvas)
				, ViewProjection(InCanvas != nullptr ? InCanvas->GetViewProjectionMatrix() : FMatrix::Identity)
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

			/**
			 * A point of the widget's own plane where the canvas draws it, in viewport pixels from the top left: through
			 * the widget's drawn matrix (perspective and shear folded in) and the canvas's view projection, the way the
			 * canvas renders it. False when the point lies behind the eye.
			 */
			bool Project(const UDreamWidget* InWidget, const FVector2D& InLocal, FVector2D& OutPixels) const
			{
				const FVector World = InWidget->GetWorldMatrix().TransformPosition(FVector(0.0, InLocal.X, InLocal.Y));
				const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(World, 1.0));
				if (Clip.W <= UE_KINDA_SMALL_NUMBER)
				{
					return false;
				}
				OutPixels.X = (Clip.X / Clip.W * 0.5 + 0.5) * Out.Viewport.X;
				OutPixels.Y = (0.5 - Clip.Y / Clip.W * 0.5) * Out.Viewport.Y;
				return true;
			}

			/** A box of the widget's own plane in viewport pixels: projected the way its rect was, bounded. */
			FBox2D ToViewport(const UDreamWidget* InWidget, const FVector2D& InMin, const FVector2D& InMax, bool bInProjected) const
			{
				FBox2D Result(ForceInit);
				for (const FVector2D Corner : { InMin, FVector2D(InMax.X, InMin.Y), FVector2D(InMin.X, InMax.Y), InMax })
				{
					FVector2D Pixels;
					double Depth = 0.0;
					if (!bInProjected || !Project(InWidget, Corner, Pixels))
					{
						Pixels = ToPixels(InWidget, Corner, Depth);
					}
					Result += Pixels;
				}
				return Result;
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
				Node.bBleed = InWidget->HasTag(TEXT("Bleed"));
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
				Node.b3D = MaxDepth > 0.5 || InWidget->HasPerspectiveApplied() || InWidget->HasShearApplied();
				if (Node.b3D)
				{
					// Turned out of the canvas plane (or sheared, or inside a perspective): where it is DRAWN, which is the
					// outline through the matrix the geometry uses, projected by the canvas's own camera.
					FBox2D Drawn(ForceInit);
					bool bAllProjected = StageCanvas != nullptr;
					for (const FVector2D Corner : { FVector2D(Left, Bottom), FVector2D(Right, Bottom), FVector2D(Left, Top), FVector2D(Right, Top) })
					{
						FVector2D OnScreen;
						bAllProjected = bAllProjected && Project(InWidget, Corner, OnScreen);
						Drawn += OnScreen;
					}
					if (bAllProjected)
					{
						Box = Drawn;
					}
					else
					{
						Node.bApproximate = true; // a corner behind the eye: the outline seen straight on is all there is
					}
				}
				Node.X = Box.Min.X;
				Node.Y = Box.Min.Y;
				Node.W = Box.Max.X - Box.Min.X;
				Node.H = Box.Max.Y - Box.Min.Y;

				// Which way it faces: its plane's normal in the screen's frame, and, turned in 3D, its edges as drawn -- the
				// farther of two opposite edges is the shorter one on the screen, which is what a person sees.
				Node.Normal = RootTransform.InverseTransformVectorNoScale(InWidget->GetWorldTransform().TransformVectorNoScale(FVector::ForwardVector)).GetSafeNormal();
				if (Node.b3D && !Node.bApproximate && StageCanvas != nullptr)
				{
					const double MidX = (Left + Right) * 0.5;
					const double MidY = (Bottom + Top) * 0.5;
					FVector2D BL, BR, TL, TR, ML, MR, MT, MB, MC;
					if (Project(InWidget, FVector2D(Left, Bottom), BL) && Project(InWidget, FVector2D(Right, Bottom), BR) && Project(InWidget, FVector2D(Left, Top), TL)
						&& Project(InWidget, FVector2D(Right, Top), TR) && Project(InWidget, FVector2D(Left, MidY), ML) && Project(InWidget, FVector2D(Right, MidY), MR)
						&& Project(InWidget, FVector2D(MidX, Top), MT) && Project(InWidget, FVector2D(MidX, Bottom), MB) && Project(InWidget, FVector2D(MidX, MidY), MC))
					{
						// The widget's own edges -- left, right, top, bottom -- then which side of the screen each one faces: a
						// node rolled a quarter turn has its top edge on the screen's right.
						double EdgeDepth[4] = { 0.0, 0.0, 0.0, 0.0 };
						ToPixels(InWidget, FVector2D(Left, MidY), EdgeDepth[0]);
						ToPixels(InWidget, FVector2D(Right, MidY), EdgeDepth[1]);
						ToPixels(InWidget, FVector2D(MidX, Top), EdgeDepth[2]);
						ToPixels(InWidget, FVector2D(MidX, Bottom), EdgeDepth[3]);
						const double EdgeLength[4] = { FVector2D::Distance(TL, BL), FVector2D::Distance(TR, BR), FVector2D::Distance(TL, TR), FVector2D::Distance(BL, BR) };
						int32 Map[4] = { 0, 1, 2, 3 };
						const FVector2D Across = MR - ML;
						if (FMath::Abs(Across.X) >= FMath::Abs(Across.Y))
						{
							if (Across.X < 0.0) { Swap(Map[0], Map[1]); }
							if (MT.Y > MB.Y) { Swap(Map[2], Map[3]); }
						}
						else
						{
							Map[0] = MT.X <= MB.X ? 2 : 3;
							Map[1] = Map[0] == 2 ? 3 : 2;
							Map[2] = ML.Y <= MR.Y ? 0 : 1;
							Map[3] = Map[2] == 0 ? 1 : 0;
						}
						for (int32 Side = 0; Side < 4; ++Side)
						{
							Node.Facing.Length[Side] = EdgeLength[Map[Side]];
							Node.Facing.Depth[Side] = EdgeDepth[Map[Side]];
						}
						Node.Facing.Centre = MC;
						Node.Facing.bValid = true;
					}
				}

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
						const EDreamUITextParagraphHorizontalAlign HAlign = Text->GetParagraphHorizontalAlignment();
						Node.TextSpillX = HAlign == EDreamUITextParagraphHorizontalAlign::Center ? 0 : HAlign == EDreamUITextParagraphHorizontalAlign::Right ? -1 : 1;
						const EDreamUITextParagraphVerticalAlign VAlign = Text->GetParagraphVerticalAlignment();
						Node.TextSpillY = VAlign == EDreamUITextParagraphVerticalAlign::Middle ? 0 : VAlign == EDreamUITextParagraphVerticalAlign::Bottom ? -1 : 1;
					}
				}

				// What it draws, in its own plane and in viewport pixels: the geometry's bounds when the visual has built some,
				// else the rect. A rect block's body is its rect; its shadow or glow counts in Drawn when it shows.
				Node.World = InWidget->GetWorldTransform();
				const double RootScale = FMath::Abs(RootTransform.GetScale3D().Y);
				Node.PxPerUnit = RootScale > UE_SMALL_NUMBER ? FMath::Abs(Node.World.GetScale3D().Y) / RootScale : 1.0;
				Node.LocalRect = FBox2D(FVector2D(Left, Bottom), FVector2D(Right, Top));
				Node.LocalBody = Node.LocalRect;
				Node.LocalDrawn = Node.LocalRect;
				Node.LocalLine = Node.LocalRect;
				Node.Body = Box;
				Node.Drawn = Box;
				if (Node.bHasText && Node.TextNeeded.X > 0.0f && Node.TextNeeded.Y > 0.0f)
				{
					const double LineW = FMath::Min<double>(Node.TextNeeded.X, Right - Left);
					const double LineH = FMath::Min<double>(Node.TextNeeded.Y, Top - Bottom);
					const double LineX = Node.TextSpillX > 0 ? Left : Node.TextSpillX < 0 ? Right - LineW : (Left + Right - LineW) * 0.5;
					const double LineY = Node.TextSpillY > 0 ? Top - LineH : Node.TextSpillY < 0 ? Bottom : (Bottom + Top - LineH) * 0.5;
					Node.LocalLine = FBox2D(FVector2D(LineX, LineY), FVector2D(LineX + LineW, LineY + LineH));
					Node.LocalBody = Node.LocalLine;
					Node.LocalDrawn = Node.LocalLine;
				}
				const UDreamRectBlock* Block = Cast<UDreamRectBlock>(InWidget->GetVisual());
				if (Block != nullptr)
				{
					Node.bHollow = !Block->GetEnableBody() || Block->GetBodyColor().A * Node.Color.A / 255 < LayoutLint::CoversTextAlpha;
				}
				if (const UDreamVisual* Visual = Cast<UDreamVisualBatchMesh>(InWidget->GetVisual()))
				{
					FVector2D GeoMin, GeoMax;
					Visual->GetGeometryBoundsInLocalSpace(GeoMin, GeoMax);
					const double Reach = 4.0 * FMath::Max(Right - Left, Top - Bottom) + 256.0;
					const bool bGeometry = GeoMax.X - GeoMin.X > 0.01 && GeoMax.Y - GeoMin.Y > 0.01 && GeoMin.X > Left - Reach && GeoMax.X < Right + Reach
						&& GeoMin.Y > Bottom - Reach && GeoMax.Y < Top + Reach;
					if (bGeometry)
					{
						const FBox2D Local(GeoMin, GeoMax);
						const FBox2D Geometry = ToViewport(InWidget, GeoMin, GeoMax, Node.b3D && !Node.bApproximate && StageCanvas != nullptr);
						if (Block != nullptr)
						{
							if (Block->GetEnableOuterShadow() && Block->GetOuterShadowColor().A >= LayoutLint::CoversTextAlpha)
							{
								Node.Drawn = Geometry;
								Node.LocalDrawn = Local;
							}
						}
						else
						{
							Node.Body = Geometry;
							Node.Drawn = Geometry;
							Node.LocalBody = Local;
							Node.LocalDrawn = Local;
							Node.bBodyMeasured = true;
							const FDreamUIGeometry* Shape = Cast<UDreamVisualBatchMesh>(Visual)->GetGeometry();
							if (Shape != nullptr && Shape->OriginVertices.Num() <= LayoutLint::ShapePoints)
							{
								for (const FDreamUIOriginVertexData& Vertex : Shape->OriginVertices)
								{
									Node.LocalShape.Emplace(Vertex.Position.Y, Vertex.Position.Z);
								}
							}
						}
					}
				}
				if (InClip.bIsValid)
				{
					for (FBox2D* Cut : { &Node.Body, &Node.Drawn })
					{
						Cut->Min.X = FMath::Max(Cut->Min.X, InClip.Min.X);
						Cut->Min.Y = FMath::Max(Cut->Min.Y, InClip.Min.Y);
						Cut->Max.X = FMath::Min(Cut->Max.X, InClip.Max.X);
						Cut->Max.Y = FMath::Min(Cut->Max.Y, InClip.Max.Y);
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

		/** A size the way a message says it: whole pixels, unless rounding would make it read as the very threshold it misses. */
		FString PxBelow(double InValue, double InThreshold)
		{
			if (InValue >= InThreshold || FMath::RoundToInt(InValue) < FMath::RoundToInt(InThreshold))
			{
				return FString::Printf(TEXT("%d"), FMath::RoundToInt(InValue));
			}
			const double Tenths = FMath::FloorToDouble(InValue * 10.0) / 10.0;
			return Tenths < InThreshold ? FString::Printf(TEXT("%.1f"), Tenths) : FString::Printf(TEXT("%.2f"), FMath::FloorToDouble(InValue * 100.0) / 100.0);
		}

		/** Tagged Bleed, or inside something that is: a decoration meant to go past its frame or over text. */
		bool IsBleed(const FLayoutSnapshot& InSnapshot, int32 InIndex)
		{
			for (int32 Walk = InIndex; Walk != INDEX_NONE; Walk = InSnapshot.Nodes[Walk].Parent)
			{
				if (InSnapshot.Nodes[Walk].bBleed)
				{
					return true;
				}
			}
			return false;
		}

		/** A node that draws a box what is inside it sits in: a visible visual other than text, of some size. */
		bool IsFrame(const FLayoutNode& InNode)
		{
			return InNode.IsDrawn() && !InNode.Visual.IsEmpty() && InNode.Visual != TEXT("Text") && InNode.Visual != TEXT("Empty")
				&& InNode.Color.A >= 13 && InNode.W >= 24.0 && InNode.H >= 24.0 && !InNode.bApproximate;
		}

		/** Where a text's glyphs are: what it needs, placed in its box by its alignment; the box when it was not measured. */
		FBox2D LineBox(const FLayoutNode& InNode)
		{
			double W = InNode.W;
			double H = InNode.H;
			if (InNode.TextNeeded.X > 0.0f && InNode.TextNeeded.Y > 0.0f)
			{
				W = FMath::Min<double>(InNode.TextNeeded.X, InNode.W);
				H = FMath::Min<double>(InNode.TextNeeded.Y, InNode.H);
			}
			const double X = InNode.TextSpillX > 0 ? InNode.X : InNode.TextSpillX < 0 ? InNode.X + InNode.W - W : InNode.X + (InNode.W - W) * 0.5;
			const double Y = InNode.TextSpillY > 0 ? InNode.Y : InNode.TextSpillY < 0 ? InNode.Y + InNode.H - H : InNode.Y + (InNode.H - H) * 0.5;
			return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
		}

		FBox2D RectOf(const FLayoutNode& InNode)
		{
			return FBox2D(FVector2D(InNode.X, InNode.Y), FVector2D(InNode.X + InNode.W, InNode.Y + InNode.H));
		}

		enum class EPart : uint8 { Rect, Body, Drawn, Line };

		/**
		 * A part of a node in the plane of another (pixels at that node's scale, Y down): through the two widgets' world
		 * transforms, so two nodes on one card turned in 3D compare as the card has them. In viewport pixels when either
		 * was not captured with its plane.
		 */
		FBox2D InPlane(const FLayoutNode& InNode, EPart InPart, const FLayoutNode& InPlaneOf);

		/** What a node draws, without a shadow: a text's glyphs (measured, or placed by its alignment), a shape's outline. */
		FBox2D BodyOf(const FLayoutNode& InNode)
		{
			const FBox2D Body = InNode.Body.bIsValid ? InNode.Body : RectOf(InNode);
			if (InNode.bHasText && !InNode.bBodyMeasured)
			{
				const FBox2D Line = LineBox(InNode);
				return FBox2D(FVector2D(FMath::Max(Line.Min.X, Body.Min.X), FMath::Max(Line.Min.Y, Body.Min.Y)),
					FVector2D(FMath::Min(Line.Max.X, Body.Max.X), FMath::Min(Line.Max.Y, Body.Max.Y)));
			}
			return Body;
		}

		/** What a node draws with its shadow or glow. */
		FBox2D DrawnOf(const FLayoutNode& InNode)
		{
			return InNode.Drawn.bIsValid ? InNode.Drawn : BodyOf(InNode);
		}

		double Area(const FBox2D& InBox)
		{
			return FMath::Max(0.0, InBox.Max.X - InBox.Min.X) * FMath::Max(0.0, InBox.Max.Y - InBox.Min.Y);
		}

		FBox2D Intersect(const FBox2D& InA, const FBox2D& InB)
		{
			return FBox2D(FVector2D(FMath::Max(InA.Min.X, InB.Min.X), FMath::Max(InA.Min.Y, InB.Min.Y)),
				FVector2D(FMath::Min(InA.Max.X, InB.Max.X), FMath::Min(InA.Max.Y, InB.Max.Y)));
		}

		FBox2D InPlane(const FLayoutNode& InNode, EPart InPart, const FLayoutNode& InPlaneOf)
		{
			const FBox2D Local = InPart == EPart::Rect ? InNode.LocalRect : InPart == EPart::Body ? InNode.LocalBody : InPart == EPart::Drawn ? InNode.LocalDrawn : InNode.LocalLine;
			if (!Local.bIsValid || !InPlaneOf.LocalRect.bIsValid)
			{
				return InPart == EPart::Rect ? RectOf(InNode) : InPart == EPart::Body ? BodyOf(InNode) : InPart == EPart::Drawn ? DrawnOf(InNode) : LineBox(InNode);
			}
			FBox2D Result(ForceInit);
			const auto Add = [&](const FVector2D& InPoint)
			{
				const FVector There = InPlaneOf.World.InverseTransformPosition(InNode.World.TransformPosition(FVector(0.0, InPoint.X, InPoint.Y)));
				Result += FVector2D(There.Y * InPlaneOf.PxPerUnit, -There.Z * InPlaneOf.PxPerUnit);
			};
			if (InPart != EPart::Rect && InPart != EPart::Line && InNode.LocalShape.Num() > 0 && Local == InNode.LocalBody)
			{
				for (const FVector2D& Point : InNode.LocalShape)
				{
					Add(Point);
				}
				return Result;
			}
			for (const FVector2D Corner : { Local.Min, FVector2D(Local.Max.X, Local.Min.Y), FVector2D(Local.Min.X, Local.Max.Y), Local.Max })
			{
				Add(Corner);
			}
			return Result;
		}

		/**
		 * How much of a box what a node draws covers, in the plane of another, the bound of that part, and the area of all
		 * it draws there: a shape kept point by point by its convex outline (a hexagon's corners are not its box), anything
		 * else by its body's four corners (a thin bar turned 40 degrees is not its box either).
		 */
		double CoveredBy(const FLayoutNode& InNode, const FLayoutNode& InPlaneOf, const FBox2D& InBox, FBox2D& OutPart, double& OutWhole)
		{
			OutPart = FBox2D(ForceInit);
			OutWhole = 0.0;
			if (!InNode.LocalBody.bIsValid || !InPlaneOf.LocalRect.bIsValid)
			{
				OutPart = Intersect(BodyOf(InNode), InBox);
				OutWhole = Area(BodyOf(InNode));
				return Area(OutPart);
			}
			TArray<FVector2D> Local = InNode.LocalShape;
			if (Local.Num() < 3)
			{
				const FBox2D& Body = InNode.LocalBody;
				Local = { Body.Min, FVector2D(Body.Max.X, Body.Min.Y), Body.Max, FVector2D(Body.Min.X, Body.Max.Y) };
			}
			TArray<FVector2D> Points;
			for (const FVector2D& Point : Local)
			{
				const FVector There = InPlaneOf.World.InverseTransformPosition(InNode.World.TransformPosition(FVector(0.0, Point.X, Point.Y)));
				Points.Emplace(There.Y * InPlaneOf.PxPerUnit, -There.Z * InPlaneOf.PxPerUnit);
			}
			// The convex outline (monotone chain), counter-clockwise.
			Points.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
			const auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
			TArray<FVector2D> Hull;
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const int32 Start = Hull.Num();
				for (int32 I = 0; I < Points.Num(); ++I)
				{
					const FVector2D& P = Points[Pass == 0 ? I : Points.Num() - 1 - I];
					while (Hull.Num() >= Start + 2 && Cross(Hull[Hull.Num() - 2], Hull.Last(), P) <= 0.0)
					{
						Hull.Pop();
					}
					Hull.Add(P);
				}
				Hull.Pop();
			}
			if (Hull.Num() < 3)
			{
				return 0.0;
			}
			const auto PolygonArea = [](const TArray<FVector2D>& InPoly)
			{
				double Twice = 0.0;
				for (int32 I = 0; I < InPoly.Num(); ++I)
				{
					Twice += InPoly[I].X * InPoly[(I + 1) % InPoly.Num()].Y - InPoly[(I + 1) % InPoly.Num()].X * InPoly[I].Y;
				}
				return FMath::Abs(Twice) * 0.5;
			};
			OutWhole = PolygonArea(Hull);
			// Cut to the box, one edge of it at a time.
			TArray<FVector2D> Poly = MoveTemp(Hull);
			for (int32 Edge = 0; Edge < 4 && Poly.Num() > 0; ++Edge)
			{
				const auto Inside = [&](const FVector2D& P)
				{
					return Edge == 0 ? P.X >= InBox.Min.X : Edge == 1 ? P.X <= InBox.Max.X : Edge == 2 ? P.Y >= InBox.Min.Y : P.Y <= InBox.Max.Y;
				};
				const auto Cut = [&](const FVector2D& A, const FVector2D& B)
				{
					const double T = Edge == 0 ? (InBox.Min.X - A.X) / (B.X - A.X) : Edge == 1 ? (InBox.Max.X - A.X) / (B.X - A.X)
						: Edge == 2 ? (InBox.Min.Y - A.Y) / (B.Y - A.Y) : (InBox.Max.Y - A.Y) / (B.Y - A.Y);
					return A + (B - A) * T;
				};
				TArray<FVector2D> Kept;
				for (int32 I = 0; I < Poly.Num(); ++I)
				{
					const FVector2D& A = Poly[I];
					const FVector2D& B = Poly[(I + 1) % Poly.Num()];
					if (Inside(A))
					{
						Kept.Add(A);
						if (!Inside(B)) { Kept.Add(Cut(A, B)); }
					}
					else if (Inside(B))
					{
						Kept.Add(Cut(A, B));
					}
				}
				Poly = MoveTemp(Kept);
			}
			for (const FVector2D& Point : Poly)
			{
				OutPart += Point;
			}
			return PolygonArea(Poly);
		}

		/** The branch a node belongs to, seen from another: its ancestor (or itself) whose parent holds both. */
		int32 BranchOf(const FLayoutSnapshot& InSnapshot, int32 InNode, int32 InFrom)
		{
			int32 Walk = InNode;
			while (InSnapshot.Nodes[Walk].Parent != INDEX_NONE && !IsAncestor(InSnapshot, InSnapshot.Nodes[Walk].Parent, InFrom))
			{
				Walk = InSnapshot.Nodes[Walk].Parent;
			}
			return Walk;
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

		// DUI8001 for text: a node inside the screen whose text, laid out past its box, spills off it -- upward from a
		// bottom-aligned box, both ways from a centred one -- where nothing clips it.
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FLayoutNode& Node = Nodes[Index];
			if (!Node.bHasText || !Node.bTextOverflows || Node.bTextCut || !Node.IsDrawn() || OffScreen.Contains(Index) || IsUnderProblem(InSnapshot, Index, OffScreen))
			{
				continue;
			}
			bool bClippedByAncestor = false;
			for (int32 Walk = Node.Parent; Walk != INDEX_NONE && !bClippedByAncestor; Walk = Nodes[Walk].Parent)
			{
				bClippedByAncestor = Nodes[Walk].bClipsChildren;
			}
			if (bClippedByAncestor)
			{
				continue;
			}
			const double DrawnW = FMath::Max<double>(Node.W, Node.TextNeeded.X);
			const double DrawnH = FMath::Max<double>(Node.H, Node.TextNeeded.Y);
			const double DrawnX = Node.TextSpillX > 0 ? Node.X : Node.TextSpillX < 0 ? Node.X + Node.W - DrawnW : Node.X + (Node.W - DrawnW) * 0.5;
			const double DrawnY = Node.TextSpillY > 0 ? Node.Y : Node.TextSpillY < 0 ? Node.Y + Node.H - DrawnH : Node.Y + (Node.H - DrawnH) * 0.5;
			TArray<FString> Edges;
			if (-DrawnX > OffScreenTolerance) { Edges.Add(FString::Printf(TEXT("%s px past the left edge"), *Px(-DrawnX))); }
			if (-DrawnY > OffScreenTolerance) { Edges.Add(FString::Printf(TEXT("%s px past the top"), *Px(-DrawnY))); }
			if (DrawnX + DrawnW - VW > OffScreenTolerance) { Edges.Add(FString::Printf(TEXT("%s px past the right edge"), *Px(DrawnX + DrawnW - VW))); }
			if (DrawnY + DrawnH - VH > OffScreenTolerance) { Edges.Add(FString::Printf(TEXT("%s px past the bottom"), *Px(DrawnY + DrawnH - VH))); }
			if (Edges.Num() == 0)
			{
				continue;
			}
			FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutOffScreen);
			Issue.Node = Index;
			Issue.Message = FString::Printf(TEXT("the text of %s, %s, spills out of its box and off the screen: %s"), *Who(InSnapshot, Index), *QuoteText(Node.Text),
				*FString::Join(Edges, TEXT(", ")));
			Issue.Hint = TEXT("Give the text room for what it needs (DUI8002 says how much), or move the box away from the edge.");
			Issue.Key = FString::Printf(TEXT("8001|%s|text"), *Node.Path);
			Issue.Weight = 1.9;
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
			// A hair under the minimum is the minimum: layout works in fractions of a pixel.
			if (Node.bInteractive && (Node.W < MinTargetSize - 0.01 || Node.H < MinTargetSize - 0.01))
			{
				FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
				Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutTooSmall);
				Issue.Node = Index;
				Issue.Message = FString::Printf(TEXT("%s can be pressed but is only %sx%s px; a pointer needs at least %dx%d"),
					*Who(InSnapshot, Index), *PxBelow(Node.W, MinTargetSize), *PxBelow(Node.H, MinTargetSize), FMath::RoundToInt(MinTargetSize), FMath::RoundToInt(MinTargetSize));
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
				if (Nodes[A].Canvas != Nodes[B].Canvas)
				{
					continue; // another canvas draws, and is pressed, in an order of its own
				}
				// Wherever they sit in the tree -- buttons are nearly always in groups of their own -- what matters is where
				// they end up on the screen.
				double OverlapW = 0.0;
				double OverlapH = 0.0;
				if (!Intersects(Nodes[A], Nodes[B], LayoutLint::OverlapTolerance, OverlapW, OverlapH))
				{
					continue;
				}
				// A small control wholly on a much larger one (a button on a pressable card, a dialog on a click-away
				// backdrop) is layered on purpose: the press reaching the one on top is the point.
				const double AreaA = Nodes[A].VisW * Nodes[A].VisH;
				const double AreaB = Nodes[B].VisW * Nodes[B].VisH;
				const double OverlapArea = OverlapW * OverlapH;
				const double Smaller = FMath::Min(AreaA, AreaB);
				const double Larger = FMath::Max(AreaA, AreaB);
				if (Smaller > 0.0 && OverlapArea >= Smaller * LayoutLint::LayeredShare && Larger >= Smaller * LayoutLint::LayeredAreaRatio)
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
			// Inside a box no bigger than an icon, the plain visuals -- no text, nothing to press -- are the parts of a
			// drawing (a blade and its guard, a coin and its rim), offset from one another on purpose, and some of them
			// turned, whose rects are only the bounds of what they draw. Their edges a pixel or three apart are the
			// drawing, not a slip, and are not reported. Texts and controls in the same box still are.
			const bool bGlyphBox = FMath::Max(Nodes[Parent].W, Nodes[Parent].H) <= GlyphBoxSize;
			TArray<int32> Siblings;
			for (const int32 Child : Nodes[Parent].Children)
			{
				const FLayoutNode& Node = Nodes[Child];
				const bool bDrawingPart = bGlyphBox && !Node.Visual.IsEmpty() && !Node.bHasText && !Node.bInteractive;
				if (Node.IsDrawn() && Node.W > ZeroSize && Node.H > ZeroSize && !Node.b3D && !bDrawingPart)
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
		// The box a node sits in: the nearest ancestor that draws one, or a backplate -- an earlier child of an ancestor that
		// draws a box filling it (an overlay of a plate and its content); of several, the last, the face drawn over a
		// shadow or a glow. A clipping ancestor on the way cuts the node instead, and a box the size of the screen is the
		// screen's business (DUI8001).
		const double ViewportArea = FMath::Max(1.0, VW * VH);
		const auto IsBackplateOf = [&Nodes](int32 InChild, int32 InOwner)
		{
			const FLayoutNode& Child = Nodes[InChild];
			const FLayoutNode& Owner = Nodes[InOwner];
			const double OwnerArea = Owner.W * Owner.H;
			if (!IsFrame(Child) || Child.bHollow || OwnerArea <= 0.0)
			{
				return false;
			}
			const FBox2D OwnerBox = InPlane(Owner, EPart::Rect, Owner);
			const FBox2D ChildBox = InPlane(Child, EPart::Body, Owner); // what it draws: a hexagon in a square slot is no plate
			return Area(Intersect(ChildBox, OwnerBox)) >= 0.9 * Area(OwnerBox) && Area(ChildBox) <= 1.25 * Area(OwnerBox);
		};
		const auto FindFrame = [&](int32 InIndex) -> int32
		{
			for (int32 Child = InIndex, Walk = Nodes[InIndex].Parent; Walk != INDEX_NONE; Child = Walk, Walk = Nodes[Walk].Parent)
			{
				int32 Frame = INDEX_NONE;
				for (int32 Index = 0; Index < Nodes[Walk].Children.Num() && Nodes[Walk].Children[Index] != Child; ++Index)
				{
					Frame = IsBackplateOf(Nodes[Walk].Children[Index], Walk) ? Nodes[Walk].Children[Index] : Frame;
				}
				Frame = Frame == INDEX_NONE && IsFrame(Nodes[Walk]) ? Walk : Frame;
				if (Frame != INDEX_NONE)
				{
					const bool bCut = Nodes[Frame].bClipsChildren || Nodes[Walk].bClipsChildren;
					return Nodes[Frame].W * Nodes[Frame].H >= ScrimShare * ViewportArea || bCut ? INDEX_NONE : Frame;
				}
				if (Nodes[Walk].bClipsChildren)
				{
					return INDEX_NONE;
				}
			}
			return INDEX_NONE;
		};

		// DUI8008: a node drawn past the panel or card that frames it -- what it draws, not its rect: a text's glyphs, a
		// shape's outline, a rect block's body (its shadow is meant to spread). Tagged Bleed (or inside something that
		// is), it goes past on purpose.
		TSet<int32> Outside;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FLayoutNode& Node = Nodes[Index];
			if (!Node.IsDrawn() || !Node.bAuthored || Node.Visual.IsEmpty() || Node.Visual == TEXT("Empty") || Node.bApproximate || Node.VisW <= ZeroSize
				|| Node.VisH <= ZeroSize || OffScreen.Contains(Index) || IsUnderProblem(InSnapshot, Index, Outside) || IsBleed(InSnapshot, Index))
			{
				continue;
			}
			const int32 Frame = FindFrame(Index);
			if (Frame == INDEX_NONE || IsBleed(InSnapshot, Frame))
			{
				continue;
			}
			const FBox2D FrameBox = InPlane(Nodes[Frame], EPart::Body, Nodes[Frame]);
			const FBox2D Drawn = InPlane(Node, EPart::Body, Nodes[Frame]);
			const double DW = Drawn.Max.X - Drawn.Min.X;
			const double DH = Drawn.Max.Y - Drawn.Min.Y;
			if (DW <= ZeroSize || DH <= ZeroSize)
			{
				continue;
			}
			if (IsFrame(Node) && DW >= PeerPlateShare * (FrameBox.Max.X - FrameBox.Min.X) && DH >= PeerPlateShare * (FrameBox.Max.Y - FrameBox.Min.Y)
				&& Area(Intersect(Drawn, FrameBox)) >= PeerPlateShare * PeerPlateShare * Area(FrameBox))
			{
				continue; // a plate over a plate: a face over its shadow, a glow behind a card -- layers of one panel
			}
			const auto Past = [](double InAmount, double InSize)
			{
				return InAmount > OutsideFrameTolerance && InAmount >= OutsideFrameShare * InSize;
			};
			TArray<FString> Edges;
			const double PastLeft = FrameBox.Min.X - Drawn.Min.X;
			const double PastTop = FrameBox.Min.Y - Drawn.Min.Y;
			const double PastRight = Drawn.Max.X - FrameBox.Max.X;
			const double PastBottom = Drawn.Max.Y - FrameBox.Max.Y;
			if (Past(PastLeft, DW)) { Edges.Add(FString::Printf(TEXT("%s px past its left edge"), *Px(PastLeft))); }
			if (Past(PastTop, DH)) { Edges.Add(FString::Printf(TEXT("%s px above its top"), *Px(PastTop))); }
			if (Past(PastRight, DW)) { Edges.Add(FString::Printf(TEXT("%s px past its right edge"), *Px(PastRight))); }
			if (Past(PastBottom, DH)) { Edges.Add(FString::Printf(TEXT("%s px below its bottom"), *Px(PastBottom))); }
			if (Edges.Num() == 0)
			{
				continue;
			}
			Outside.Add(Index);
			FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutOutsideFrame);
			Issue.Node = Index;
			Issue.Other = Frame;
			Issue.Message = FString::Printf(TEXT("%s draws outside %s, the panel it sits in: %s"), *Who(InSnapshot, Index), *Who(InSnapshot, Frame),
				*FString::Join(Edges, TEXT(", ")));
			Issue.Hint = TEXT("Keep it inside: make it smaller, move it in, or give the panel room. A glow, a shadow or a bleed meant to go past the panel: write @tag Bleed on it.");
			Issue.Key = FString::Printf(TEXT("8008|%s"), *Node.Path);
			Issue.Weight = 1.7;
		}

		// DUI8009: a visual that takes no pointer drawn over a text (after it, in its canvas): over a fair share of its
		// glyphs, or -- a small decoration -- mostly inside the line the text takes. What is drawn before the text is
		// under it, its backplate, and never counts; nor does a scrim over most of the screen.
		for (int32 TextIndex = 0; TextIndex < Nodes.Num(); ++TextIndex)
		{
			const FLayoutNode& TextNode = Nodes[TextIndex];
			if (!TextNode.IsDrawn() || !TextNode.bHasText || TextNode.Text.TrimStartAndEnd().IsEmpty() || TextNode.VisW < 2.0 || TextNode.VisH < 2.0
				|| TextNode.bApproximate)
			{
				continue;
			}
			const FBox2D Glyphs = InPlane(TextNode, EPart::Body, TextNode);
			const FBox2D Line = InPlane(TextNode, EPart::Line, TextNode);
			// A veil over the whole card the text is on (a dimmed choice, a disabled panel) is about the card, not the text.
			const int32 TextFrame = FindFrame(TextIndex);
			const FBox2D TextFrameBox = TextFrame != INDEX_NONE ? InPlane(Nodes[TextFrame], EPart::Rect, TextNode) : FBox2D(ForceInit);
			const double GlyphArea = Area(Glyphs);
			if (GlyphArea < 4.0)
			{
				continue;
			}
			bool bCovered = false;
			for (int32 Index = 0; Index < Nodes.Num() && !bCovered; ++Index)
			{
				const FLayoutNode& Over = Nodes[Index];
				if (Over.Order <= TextNode.Order || !Over.IsDrawn() || Over.Visual.IsEmpty() || Over.Visual == TEXT("Text") || Over.Visual == TEXT("Empty")
					|| Over.bInteractive || Over.Canvas != TextNode.Canvas || Over.bApproximate || Over.VisW <= ZeroSize || Over.VisH <= ZeroSize
					|| Over.bHollow || (Over.Color.A < CoversTextAlpha && Over.Drawn.Equals(Over.Body)))
				{
					continue;
				}
				if (IsAncestor(InSnapshot, TextIndex, Index) || IsAncestor(InSnapshot, Index, TextIndex) || IsBleed(InSnapshot, Index))
				{
					continue;
				}
				// A control's own parts around its own label are the control's business.
				if (!Over.bAuthored)
				{
					int32 Owner = Over.Parent;
					while (Owner != INDEX_NONE && !Nodes[Owner].bAuthored)
					{
						Owner = Nodes[Owner].Parent;
					}
					if (Owner != INDEX_NONE && (Owner == TextIndex || IsAncestor(InSnapshot, Owner, TextIndex)))
					{
						continue;
					}
				}
				if (Area(DrawnOf(Over)) >= ScrimShare * ViewportArea)
				{
					continue; // a scrim, a vignette, scan lines: over everything, about no text in particular
				}
				const FBox2D OverBox = InPlane(Over, EPart::Drawn, TextNode);
				const double OverArea = Area(OverBox);
				if (TextFrameBox.bIsValid && Area(Intersect(OverBox, TextFrameBox)) >= 0.9 * Area(TextFrameBox))
				{
					continue;
				}
				const FBox2D Covered = Intersect(OverBox, Glyphs);
				const double CoveredArea = Area(Covered);
				const double InLine = Area(Intersect(OverBox, Line));
				// A sliver along an edge is two things touching, not one over the other.
				const auto Thick = [](const FBox2D& InBox) { return FMath::Min(InBox.Max.X - InBox.Min.X, InBox.Max.Y - InBox.Min.Y) >= CoversTextThickness; };
				const bool bOverGlyphs = CoveredArea >= CoversTextShare * GlyphArea && Thick(Covered);
				const bool bInLine = OverArea <= Area(Line) && InLine >= CrowdsTextShare * OverArea && Thick(Intersect(OverBox, Line));
				if (!bOverGlyphs && !bInLine)
				{
					continue;
				}
				const FBox2D Shown = bOverGlyphs ? Covered : Intersect(OverBox, Line);
				FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
				Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutCoversText);
				Issue.Node = Index;
				Issue.Other = TextIndex;
				Issue.Message = bOverGlyphs
					? FString::Printf(TEXT("%s is drawn over the text of %s, %s, hiding %d%% of its letters (%sx%s px)"), *Who(InSnapshot, Index),
						*Who(InSnapshot, TextIndex), *QuoteText(TextNode.Text), FMath::RoundToInt(100.0 * CoveredArea / GlyphArea),
						*Px(Shown.Max.X - Shown.Min.X), *Px(Shown.Max.Y - Shown.Min.Y))
					: FString::Printf(TEXT("%s is drawn over the line of %s, %s: %d%% of it (%sx%s px) lies on the text"), *Who(InSnapshot, Index),
						*Who(InSnapshot, TextIndex), *QuoteText(TextNode.Text), FMath::RoundToInt(100.0 * InLine / OverArea),
						*Px(Shown.Max.X - Shown.Min.X), *Px(Shown.Max.Y - Shown.Min.Y));
				Issue.Hint = TEXT("Move it off the text, or put it before the text in the file (or in a lower layer) so it is drawn under it. A decoration meant to lie over text: write @tag Bleed on it.");
				Issue.Key = FString::Printf(TEXT("8009|%s|%s"), *Over.Path, *TextNode.Path);
				Issue.Weight = 1.75;
				bCovered = true; // one finding a text: the first thing drawn over it
			}
			if (bCovered || IsBleed(InSnapshot, TextIndex))
			{
				continue;
			}

			// A text laid across the edge of a shape drawn under it that is not its plate: some of its letters on the
			// shape, the rest off it, so it reads over two backgrounds at once (a label slid onto the next badge). What is
			// drawn before a text is its plate when it holds the text, or when the text's own plate is drawn over it; a
			// backplate of a panel the text is in, its frame and its ancestors never count.
			// What the text is on: an ancestor that draws, or a plate an ancestor holds under it, that holds the whole text.
			// A shape drawn before that is behind the panel the text is on -- see-through or not, frosted glass over a busy
			// backdrop -- not next to the text.
			const auto HoldsText = [&](int32 InIndex)
			{
				const FLayoutNode& Plate = Nodes[InIndex];
				return Plate.IsDrawn() && !Plate.Visual.IsEmpty() && Plate.Visual != TEXT("Text") && Plate.Visual != TEXT("Empty") && Plate.Color.A >= 13
					&& Area(Intersect(InPlane(Plate, EPart::Body, TextNode), Glyphs)) >= (1.0 - CoversTextShare) * GlyphArea;
			};
			int32 PlateOrder = INDEX_NONE;
			for (int32 Child = TextIndex, Walk = TextNode.Parent; Walk != INDEX_NONE; Child = Walk, Walk = Nodes[Walk].Parent)
			{
				if (HoldsText(Walk))
				{
					PlateOrder = FMath::Max(PlateOrder, Nodes[Walk].Order);
				}
				for (const int32 Sibling : Nodes[Walk].Children)
				{
					if (Sibling == Child)
					{
						break;
					}
					if (HoldsText(Sibling))
					{
						PlateOrder = FMath::Max(PlateOrder, Nodes[Sibling].Order);
					}
				}
			}
			for (int32 Index = 0; Index < Nodes.Num(); ++Index)
			{
				const FLayoutNode& Under = Nodes[Index];
				if (Under.Order >= TextNode.Order || !Under.IsDrawn() || Under.Visual.IsEmpty() || Under.Visual == TEXT("Text") || Under.Visual == TEXT("Empty")
					|| Under.Visual == TEXT("Ring") || Under.Visual == TEXT("Line") || Under.bInteractive || Under.Canvas != TextNode.Canvas || Under.bApproximate
					|| Under.VisW <= ZeroSize || Under.VisH <= ZeroSize || Under.bHollow || Under.Color.A < CoversTextAlpha)
				{
					continue; // a ring or a line is an outline: its box is not what it draws
				}
				if (Index == TextFrame || IsAncestor(InSnapshot, Index, TextIndex) || IsAncestor(InSnapshot, TextIndex, Index) || IsBleed(InSnapshot, Index))
				{
					continue;
				}
				if (Under.Parent != INDEX_NONE && IsAncestor(InSnapshot, Under.Parent, TextIndex) && IsBackplateOf(Index, Under.Parent))
				{
					continue; // the plate of a panel the text is in
				}
				if (Under.Order < PlateOrder)
				{
					continue; // the panel or plate the text is on lies between them
				}
				if (!Under.bAuthored)
				{
					int32 Owner = Under.Parent;
					while (Owner != INDEX_NONE && !Nodes[Owner].bAuthored)
					{
						Owner = Nodes[Owner].Parent;
					}
					if (Owner != INDEX_NONE && (Owner == TextIndex || IsAncestor(InSnapshot, Owner, TextIndex)))
					{
						continue;
					}
				}
				if (Area(BodyOf(Under)) >= ScrimShare * ViewportArea)
				{
					continue;
				}
				// On the screen first, cut to what clips each of them: a shape a frame crops short of the text is not under it.
				const FBox2D OnScreen = Intersect(BodyOf(Under), BodyOf(TextNode));
				if (FMath::Min(OnScreen.Max.X - OnScreen.Min.X, OnScreen.Max.Y - OnScreen.Min.Y) < CoversTextThickness)
				{
					continue;
				}
				FBox2D Part;
				double Whole = 0.0;
				const double CoveredArea = CoveredBy(Under, TextNode, Glyphs, Part, Whole);
				// The shape as drawn in the text's plane, no more than what shows of it on the screen.
				const double Share = FMath::Min(CoveredArea / GlyphArea, Area(OnScreen) / FMath::Max(1.0, Area(BodyOf(TextNode))));
				if (Whole > UnderShapeRatio * GlyphArea)
				{
					continue; // a backdrop shape, a glow, a stripe the text is laid over: not one thing the text is on the edge of
				}
				if (Share < CoversTextShare || Share > 1.0 - CoversTextShare || !Part.bIsValid
					|| FMath::Min(Part.Max.X - Part.Min.X, Part.Max.Y - Part.Min.Y) < CoversTextThickness)
				{
					continue; // clear of it, a sliver, or on it all but an edge: then it is the text's plate
				}
				const int32 Branch = BranchOf(InSnapshot, Index, TextIndex);
				const FString Belongs = Branch != Index ? FString::Printf(TEXT(" (in %s)"), *Who(InSnapshot, Branch)) : FString();
				FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
				Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutCoversText);
				Issue.Node = TextIndex;
				Issue.Other = Index;
				Issue.Message = FString::Printf(TEXT("the text of %s, %s, lies across the edge of %s%s, drawn under it: %d%% of its letters are on that shape and the rest off it"),
					*Who(InSnapshot, TextIndex), *QuoteText(TextNode.Text), *Who(InSnapshot, Index), *Belongs, FMath::RoundToInt(100.0 * Share));
				Issue.Hint = TEXT("Move the text clear of the shape, or give the text a plate of its own. A text meant to lie across it: write @tag Bleed on the shape.");
				Issue.Key = FString::Printf(TEXT("8009|under|%s|%s"), *Under.Path, *TextNode.Path);
				Issue.Weight = 1.7;
				break;
			}
		}

		// DUI8010: a panel whose content keeps to one side, or leaves a wide band empty across its middle. An info, and a
		// cautious one: only sizeable panels that are not the screen's backdrop, with text and at least three things in
		// them, judged by what they draw (a text's glyphs, not its box).
		TSet<int32> Unbalanced;
		for (int32 Owner = 0; Owner < Nodes.Num(); ++Owner)
		{
			if (!Nodes[Owner].IsDrawn() || Nodes[Owner].Children.Num() == 0 || IsUnderProblem(InSnapshot, Owner, Unbalanced))
			{
				continue;
			}
			int32 Panel = INDEX_NONE;
			for (int32 Child : Nodes[Owner].Children)
			{
				Panel = IsBackplateOf(Child, Owner) ? Child : Panel;
			}
			Panel = Panel == INDEX_NONE && IsFrame(Nodes[Owner]) ? Owner : Panel;
			if (Panel == INDEX_NONE)
			{
				continue;
			}
			const FLayoutNode& PanelNode = Nodes[Panel];
			if (PanelNode.W < UnbalancedMinSide || PanelNode.H < UnbalancedMinSide * 0.5 || PanelNode.W * PanelNode.H < UnbalancedMinShare * ViewportArea
				|| PanelNode.W * PanelNode.H >= ScrimShare * ViewportArea || IsBleed(InSnapshot, Panel))
			{
				continue;
			}
			const FBox2D PanelBox = InPlane(PanelNode, EPart::Rect, PanelNode);
			const double PanelW = PanelBox.Max.X - PanelBox.Min.X;
			const double PanelH = PanelBox.Max.Y - PanelBox.Min.Y;
			const double PanelArea = Area(PanelBox);
			TArray<FBox2D> Items;
			bool bHasText = false;
			for (int32 Index = Owner + 1; Index < Nodes.Num() && Nodes[Index].Depth > Nodes[Owner].Depth; ++Index)
			{
				const FLayoutNode& Item = Nodes[Index];
				if (Index == Panel || !Item.IsDrawn() || Item.Visual.IsEmpty() || Item.Visual == TEXT("Empty") || Item.VisW <= ZeroSize || Item.VisH <= ZeroSize)
				{
					continue;
				}
				const FBox2D Box = Intersect(InPlane(Item, EPart::Body, PanelNode), PanelBox);
				const double ItemArea = Area(Box);
				if (ItemArea <= 0.0 || ItemArea >= 0.85 * PanelArea)
				{
					continue; // a backdrop, a border or a sheen over the whole panel is the panel, not its content
				}
				if (FMath::Min(Box.Max.X - Box.Min.X, Box.Max.Y - Box.Min.Y) < UnbalancedThinItem)
				{
					continue; // a rule, a divider, an accent bar along an edge: they frame the content, they are not it
				}
				bHasText |= Item.bHasText && !Item.Text.TrimStartAndEnd().IsEmpty();
				Items.Add(Box);
			}
			if (Items.Num() < 3 || !bHasText)
			{
				continue;
			}
			FBox2D Content = Items[0];
			for (const FBox2D& Box : Items)
			{
				Content += Box;
			}
			struct FFinding { double Share = 0.0; FString Where; FString Dir; };
			FFinding Best;
			const auto Consider = [&Best](double InShare, FString InWhere, const TCHAR* InDir)
			{
				if (InShare > Best.Share)
				{
					Best.Share = InShare;
					Best.Where = MoveTemp(InWhere);
					Best.Dir = InDir;
				}
			};
			// How much of the panel's height (width) lies in the gaps between its items: rows spread thin, or a hole.
			const auto GapsBetween = [&](bool bVertical)
			{
				TArray<TPair<double, double>> Spans;
				for (const FBox2D& Box : Items)
				{
					Spans.Emplace(bVertical ? Box.Min.Y : Box.Min.X, bVertical ? Box.Max.Y : Box.Max.X);
				}
				Spans.Sort([](const TPair<double, double>& A, const TPair<double, double>& B) { return A.Key < B.Key; });
				double Covered = 0.0;
				double From = Spans[0].Key;
				double To = Spans[0].Value;
				for (const TPair<double, double>& Span : Spans)
				{
					if (Span.Key > To)
					{
						Covered += To - From;
						From = Span.Key;
						To = Span.Value;
					}
					else
					{
						To = FMath::Max(To, Span.Value);
					}
				}
				Covered += To - From;
				const double Span = bVertical ? Content.Max.Y - Content.Min.Y : Content.Max.X - Content.Min.X;
				return FMath::Max(0.0, Span - Covered) / (bVertical ? PanelH : PanelW);
			};
			const double GapsY = GapsBetween(true);
			const double GapsX = GapsBetween(false);
			const double EmptyX = GapsX + (Content.Min.X - PanelBox.Min.X + PanelBox.Max.X - Content.Max.X) / PanelW;
			const double EmptyY = GapsY + (Content.Min.Y - PanelBox.Min.Y + PanelBox.Max.Y - Content.Max.Y) / PanelH;
			const bool bTall = PanelH > UnbalancedElongated * PanelW;
			const bool bWide = PanelW > UnbalancedElongated * PanelH;

			// One side empty, the other tight: content pushed to the other side. A narrower band counts where the rest
			// of that axis is mostly empty too.
			const double Left = Content.Min.X - PanelBox.Min.X;
			const double Right = PanelBox.Max.X - Content.Max.X;
			const double Top = Content.Min.Y - PanelBox.Min.Y;
			const double Bottom = PanelBox.Max.Y - Content.Max.Y;
			const auto Side = [&](double InEmpty, double InOpposite, double InSpan, const TCHAR* InEmptySide, const TCHAR* InKeepsTo, const TCHAR* InDir)
			{
				const double Share = InEmpty / InSpan;
				const double AxisEmpty = (InDir[0] == TEXT('l') || InDir[0] == TEXT('r')) ? EmptyX : EmptyY; // left, right: across
				if ((Share >= UnbalancedEmptyShare || (Share >= UnbalancedSideShare && AxisEmpty >= UnbalancedSparse)) && InOpposite <= InEmpty * UnbalancedOppositeRatio)
				{
					Consider(Share, FString::Printf(TEXT("its content keeps to the %s and the %s %d%% of it is empty"), InKeepsTo, InEmptySide,
						FMath::RoundToInt(100.0 * Share)), InDir);
				}
			};
			Side(Right, Left, PanelW, TEXT("right"), TEXT("left"), TEXT("left"));
			Side(Left, Right, PanelW, TEXT("left"), TEXT("right"), TEXT("right"));
			Side(Bottom, Top, PanelH, TEXT("bottom"), TEXT("top"), TEXT("top"));
			Side(Top, Bottom, PanelH, TEXT("top"), TEXT("bottom"), TEXT("bottom"));
			// A wide empty band across the middle: the items' extents on one axis, merged, and the widest gap between them.
			const auto MiddleGap = [&](bool bVertical)
			{
				TArray<TPair<double, double>> Spans;
				for (const FBox2D& Box : Items)
				{
					Spans.Emplace(bVertical ? Box.Min.Y : Box.Min.X, bVertical ? Box.Max.Y : Box.Max.X);
				}
				Spans.Sort([](const TPair<double, double>& A, const TPair<double, double>& B) { return A.Key < B.Key; });
				double Reach = Spans[0].Value;
				double Widest = 0.0;
				double From = 0.0;
				for (const TPair<double, double>& Span : Spans)
				{
					if (Span.Key - Reach > Widest)
					{
						Widest = Span.Key - Reach;
						From = Reach;
					}
					Reach = FMath::Max(Reach, Span.Value);
				}
				const double PanelSpan = bVertical ? PanelH : PanelW;
				const double Share = Widest / PanelSpan;
				if (Share >= UnbalancedEmptyShare)
				{
					const double Origin = bVertical ? PanelBox.Min.Y : PanelBox.Min.X;
					Consider(Share, FString::Printf(TEXT("a band %s px %s across its middle (%s to %s px from its %s) holds nothing, %d%% of its %s"), *Px(Widest),
						bVertical ? TEXT("high") : TEXT("wide"), *Px(From - Origin), *Px(From + Widest - Origin), bVertical ? TEXT("top") : TEXT("left"),
						FMath::RoundToInt(100.0 * Share), bVertical ? TEXT("height") : TEXT("width")), bVertical ? TEXT("middle-y") : TEXT("middle-x"));
				}
			};
			// Along a bar or a sidebar the middle is where it is meant to be empty: items at both ends, or spread out.
			if (!bTall)
			{
				MiddleGap(true);
				if (GapsY >= UnbalancedVerySparse)
				{
					Consider(GapsY, FString::Printf(TEXT("its rows are spread thin: the gaps between them take %d%% of its height"),
						FMath::RoundToInt(100.0 * GapsY)), TEXT("sparse-y"));
				}
			}
			if (!bWide)
			{
				MiddleGap(false);
				if (GapsX >= UnbalancedVerySparse)
				{
					Consider(GapsX, FString::Printf(TEXT("its columns are spread thin: the gaps between them take %d%% of its width"),
						FMath::RoundToInt(100.0 * GapsX)), TEXT("sparse-x"));
				}
			}
			if (Best.Share <= 0.0)
			{
				continue;
			}
			Unbalanced.Add(Owner);
			FLayoutIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Code = static_cast<int32>(EDreamUIDiagnosticCode::LayoutUnbalanced);
			Issue.Severity = TEXT("info");
			Issue.Node = Owner;
			Issue.Other = Panel;
			Issue.Message = FString::Printf(TEXT("%s (%sx%s px) looks unbalanced: %s"), *Who(InSnapshot, Owner), *Px(PanelNode.W), *Px(PanelNode.H), *Best.Where);
			Issue.Hint = TEXT("Let the content fill the panel (a box with Fill slots, or anchors that stretch), spread it out, or make the panel smaller.");
			Issue.Key = FString::Printf(TEXT("8010|%s|%s"), *Nodes[Owner].Path, *Best.Dir);
			Issue.Weight = 0.5;
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
			if (!Node.bAuthored && Ratio < LayoutLint::HiddenByControl)
			{
				// A control's own part painted the colour of what is behind it is put away on purpose: the tick of a
				// toggle that is off, a caret that blinked out.
				continue;
			}
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

	void DescribeFaces(const FLayoutSnapshot& InSnapshot, TArray<FLayoutFace>& OutFaces)
	{
		using namespace LayoutFacing;
		OutFaces.Reset();
		const TArray<FLayoutNode>& Nodes = InSnapshot.Nodes;
		const double VW = FMath::Max(1, InSnapshot.Viewport.X);
		// The plane each node sits in: the nearest listed face above it, or the screen's. Nodes come parent first.
		TArray<FVector> PlaneOf;
		PlaneOf.Init(FVector::ForwardVector, Nodes.Num());
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FLayoutNode& Node = Nodes[Index];
			const FVector Around = Node.Parent != INDEX_NONE ? PlaneOf[Node.Parent] : FVector::ForwardVector;
			PlaneOf[Index] = Around;
			if (!Node.Facing.bValid || !Node.IsDrawn() || Node.W < MinSide || Node.H < MinSide)
			{
				continue;
			}
			const double Lean = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Node.Normal, Around), -1.0, 1.0)));
			if (Lean < MinAngle)
			{
				continue; // on the face it sits in: part of it
			}
			PlaneOf[Index] = Node.Normal;

			// How far it leans from the screen's plane, about the vertical axis (turn) and the horizontal one (tilt).
			const FVector N = Node.Normal;
			const double TurnDegrees = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(N.Y), FMath::Abs(N.X)));
			const double TiltDegrees = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(N.Z), FMath::Abs(N.X)));
			const FLayoutFacing& F = Node.Facing;
			// Of two opposite edges, the one drawn shorter is the farther; drawn alike (no perspective), the deeper one.
			const auto Farther = [&F](int32 A, int32 B)
			{
				const double LA = F.Length[A];
				const double LB = F.Length[B];
				if (FMath::Abs(LA - LB) > SameLength * FMath::Max(LA, LB))
				{
					return LA < LB ? A : B;
				}
				return F.Depth[A] >= F.Depth[B] ? A : B;
			};

			FLayoutFace& Face = OutFaces.AddDefaulted_GetRef();
			Face.Node = Index;
			Face.Name = Who(InSnapshot, Index);
			const double FromMiddle = F.Centre.X - VW * 0.5;
			Face.Side = FromMiddle < -CentreBand * VW ? TEXT("left") : FromMiddle > CentreBand * VW ? TEXT("right") : TEXT("middle");
			Face.bBack = N.X < 0.0;
			TArray<FString> Words;
			TArray<FString> Short;
			if (Face.bBack)
			{
				Words.Add(TEXT("its back is toward the viewer (turned past 90 degrees), drawn mirrored"));
				Short.Add(TEXT("shows its back"));
			}
			if (TurnDegrees >= MinAngle)
			{
				Face.Turn = FMath::RoundToInt(TurnDegrees);
				Face.TurnFarther = Farther(FLayoutFacing::Left, FLayoutFacing::Right) == FLayoutFacing::Left ? TEXT("left") : TEXT("right");
				// A face looks the way of its farther edge: the right edge pushed back turns its front to the right.
				if (Face.Side == TEXT("middle"))
				{
					Words.Add(FString::Printf(TEXT("turned %d degrees: faces %s (%s edge farther)"), Face.Turn, *Face.TurnFarther, *Face.TurnFarther));
					Short.Add(FString::Printf(TEXT("faces %s"), *Face.TurnFarther));
				}
				else
				{
					Face.TowardCentre = (Face.Side == TEXT("left")) == (Face.TurnFarther == TEXT("right")) ? 1 : -1;
					Words.Add(FString::Printf(TEXT("turned %d degrees: faces %s the screen centre (%s edge farther)"), Face.Turn,
						Face.TowardCentre > 0 ? TEXT("toward") : TEXT("away from"), *Face.TurnFarther));
					Short.Add(Face.TowardCentre > 0 ? TEXT("faces toward the centre") : TEXT("faces away from the centre"));
				}
			}
			if (TiltDegrees >= MinAngle)
			{
				Face.Tilt = FMath::RoundToInt(TiltDegrees);
				const int32 Far = Farther(FLayoutFacing::Top, FLayoutFacing::Bottom);
				const int32 Near = Far == FLayoutFacing::Top ? FLayoutFacing::Bottom : FLayoutFacing::Top;
				Face.TiltFarther = Far == FLayoutFacing::Top ? TEXT("top") : TEXT("bottom");
				Face.bNarrower = F.Length[Far] < F.Length[Near] * (1.0 - SameLength);
				const bool bBackward = Far == FLayoutFacing::Top;
				Words.Add(FString::Printf(TEXT("tilted %s %d degrees: %s edge farther%s"), bBackward ? TEXT("back") : TEXT("forward"), Face.Tilt,
					*Face.TiltFarther, Face.bNarrower ? TEXT(" and narrower") : TEXT("")));
				Short.Add(bBackward ? TEXT("tilted back") : TEXT("tilted forward"));
			}
			if (Words.Num() == 0)
			{
				// Leans from the face it sits in, yet square to the screen: a card turned back flat on a turned panel.
				Words.Add(TEXT("flat to the screen, on a face that is turned"));
				Short.Add(TEXT("flat to the screen"));
			}
			Face.Words = FString::Join(Words, TEXT("; "));
			Face.Short = FString::Join(Short, TEXT(", "));
		}
	}

	FString FormatFaces(const FLayoutSnapshot& InSnapshot, const TArray<FLayoutFace>& InFaces)
	{
		if (InFaces.Num() == 0)
		{
			return FString();
		}
		TArray<FString> Lines;
		Lines.Add(TEXT("Turned in 3D, as drawn (one line a face; the farther edge is drawn shorter):"));
		for (int32 Index = 0; Index < InFaces.Num() && Index < LayoutFacing::MaxFaces; ++Index)
		{
			const FLayoutFace& Face = InFaces[Index];
			const TCHAR* Where = Face.Side == TEXT("left") ? TEXT("on the left") : Face.Side == TEXT("right") ? TEXT("on the right") : TEXT("in the middle");
			Lines.Add(FString::Printf(TEXT("  %s, %s: %s"), *Face.Name, Where, *Face.Words));
		}
		if (InFaces.Num() > LayoutFacing::MaxFaces)
		{
			Lines.Add(FString::Printf(TEXT("  ...and %d more (in data.faces)"), InFaces.Num() - LayoutFacing::MaxFaces));
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	FString SummarizeFaces(const TArray<FLayoutFace>& InFaces)
	{
		if (InFaces.Num() == 0)
		{
			return FString();
		}
		constexpr int32 Named = 4;
		TArray<FString> Parts;
		for (int32 Index = 0; Index < InFaces.Num() && Index < Named; ++Index)
		{
			FString Id = InFaces[Index].Name;
			int32 Space = INDEX_NONE;
			if (Id.FindChar(TEXT(' '), Space))
			{
				Id.RightChopInline(Space + 1); // the id alone: `RosterLeft`, not `Widget RosterLeft`
			}
			Parts.Add(FString::Printf(TEXT("%s %s"), *Id, *InFaces[Index].Short));
		}
		FString Sentence = FString::Join(Parts, TEXT(", "));
		if (InFaces.Num() > Named)
		{
			Sentence += FString::Printf(TEXT(", and %d more"), InFaces.Num() - Named);
		}
		return FString::Printf(TEXT(" In 3D: %s."), *Sentence);
	}

	TArray<TSharedPtr<FJsonValue>> FacesToJson(const FLayoutSnapshot& InSnapshot, const TArray<FLayoutFace>& InFaces)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FLayoutFace& Face : InFaces)
		{
			const FLayoutNode& Node = InSnapshot.Nodes[Face.Node];
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Face.Name);
			Object->SetStringField(TEXT("path"), Node.Path);
			int32 Walk = Face.Node;
			while (Walk != INDEX_NONE && !InSnapshot.Nodes[Walk].bAuthored)
			{
				Walk = InSnapshot.Nodes[Walk].Parent;
			}
			if (Walk != INDEX_NONE && InSnapshot.Nodes[Walk].Line > 0)
			{
				Object->SetStringField(TEXT("file"), InSnapshot.Nodes[Walk].File);
				Object->SetNumberField(TEXT("line"), InSnapshot.Nodes[Walk].Line);
			}
			TSharedRef<FJsonObject> Centre = MakeShared<FJsonObject>();
			Centre->SetNumberField(TEXT("x"), FMath::RoundToInt(Node.Facing.Centre.X));
			Centre->SetNumberField(TEXT("y"), FMath::RoundToInt(Node.Facing.Centre.Y));
			Object->SetObjectField(TEXT("centre"), Centre);
			Object->SetStringField(TEXT("side"), Face.Side);
			if (Face.Turn > 0)
			{
				TSharedRef<FJsonObject> Turn = MakeShared<FJsonObject>();
				Turn->SetNumberField(TEXT("degrees"), Face.Turn);
				Turn->SetStringField(TEXT("fartherEdge"), Face.TurnFarther);
				Turn->SetStringField(TEXT("faces"), Face.TurnFarther);
				if (Face.TowardCentre != 0)
				{
					Turn->SetBoolField(TEXT("towardCentre"), Face.TowardCentre > 0);
				}
				Object->SetObjectField(TEXT("turn"), Turn);
			}
			if (Face.Tilt > 0)
			{
				TSharedRef<FJsonObject> Tilt = MakeShared<FJsonObject>();
				Tilt->SetNumberField(TEXT("degrees"), Face.Tilt);
				Tilt->SetStringField(TEXT("fartherEdge"), Face.TiltFarther);
				Tilt->SetStringField(TEXT("leans"), Face.TiltFarther == TEXT("top") ? TEXT("back") : TEXT("forward"));
				Tilt->SetBoolField(TEXT("fartherEdgeNarrower"), Face.bNarrower);
				Object->SetObjectField(TEXT("tilt"), Tilt);
			}
			if (Face.bBack)
			{
				Object->SetBoolField(TEXT("back"), true);
			}
			Object->SetStringField(TEXT("text"), Face.Words);
			Out.Add(MakeShared<FJsonValueObject>(Object));
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
			if (Node.b3D) { Notes.Add(Node.bApproximate ? TEXT("turned in 3D, rect approximate: part of it is behind the eye") : TEXT("turned in 3D, rect as drawn in perspective")); }
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
			if (Node.bApproximate) { Object->SetBoolField(TEXT("approximate"), true); }
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
