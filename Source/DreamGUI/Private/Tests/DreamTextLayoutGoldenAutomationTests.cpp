// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Engine/World.h"
#include "Tests/DreamTextTestFont.h"

/*
 * Golden comparison for the text pipeline split. The old UpdateUIText laid out and emitted vertices
 * in one pass; the new pipeline lays out into a display list and paints from it. Both are fed the
 * same component, font and parameters, and every output that anything downstream reads -- origin
 * vertices, vertex colours/UVs, triangles, caret lines, char properties, rich-text tags, inline
 * objects, preferred size, truncation -- has to agree.
 *
 * Temporary: it exists to prove the split changed nothing, and goes when the old function does.
 */
namespace DreamTextGoldenTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	struct FFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Child = nullptr;
		UDreamText* Text = nullptr;
		UDreamTextTestFont* Font = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Root->SetDisplayName(TEXT("Root"));
			Root->SetWidth(800.0f);
			Root->SetHeight(600.0f);
			Root->AddComponent<UDreamCanvas>();
			Root->OnRegister();
			Root->SetWidgetActive(true);

			Child = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Child->SetDisplayName(TEXT("Text"));
			Child->SetWidth(300.0f);
			Child->SetHeight(120.0f);
			Font = NewObject<UDreamTextTestFont>(World);
			Text = Child->CreateNewVisual<UDreamText>();
			if (!Text)return false;
			Text->SetFont(Font);
			Child->OnRegister();
			if (!Child->TrySetParent(Root, false))return false;
			return Child->GetRenderCanvas() != nullptr;
		}
	};

	/** One case: the knobs the old function read from its parameters and from the component. */
	struct FCase
	{
		FString Name;
		FString Content;
		float FontSize = 24.0f;
		FVector2f FontSpace = FVector2f::ZeroVector;
		EDreamUITextParagraphHorizontalAlign HAlign = EDreamUITextParagraphHorizontalAlign::Left;
		EDreamUITextParagraphVerticalAlign VAlign = EDreamUITextParagraphVerticalAlign::Top;
		EDreamUITextOverflowType Overflow = EDreamUITextOverflowType::HorizontalOverflow;
		ETextWrappingPolicy Wrapping = ETextWrappingPolicy::AllowPerCharacterWrapping;
		bool bKerning = true;
		EDreamUITextFontStyle Style = EDreamUITextFontStyle::None;
		bool bRichText = false;
		int32 RichFlags = 0xffffffff;
		float LineHeightPercentage = 1.0f;
		float WrapTextAt = 0.0f;
		FMargin Margin = FMargin(0.0f);
		FVector2D Pivot = FVector2D(0.5, 0.5);
		FVector2D WidgetSize = FVector2D(300.0, 120.0);
		FColor Color = FColor::White;
		float ExpandMeshSize = 0.0f;
		/** Char properties cannot be compared where the old pipeline left entries dangling (ellipsis). */
		bool bCompareCharProperties = true;
	};

	struct FOldResult
	{
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextLineProperty> Lines;
		TArray<FDreamUITextCharProperty> Chars;
		TArray<FDreamUIText_RichTextCustomTag> Tags;
		TArray<FDreamUIText_RichTextImageTag> Images;
		TArray<FDreamUIText_Emoji> Emojis;
		FVector2f PreferredSize = FVector2f::ZeroVector;
		bool bTruncated = false;
	};

	struct FNewResult
	{
		FDreamTextDisplayList DisplayList;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> Chars;
	};

	void ApplyCase(FFixture& F, const FCase& C)
	{
		F.Child->SetWidth((float)C.WidgetSize.X);
		F.Child->SetHeight((float)C.WidgetSize.Y);
		F.Child->SetPivot(C.Pivot);
		F.Text->SetText(FText::FromString(C.Content));
		F.Text->SetFontSize(C.FontSize);
		F.Text->SetFontSpace(FVector2D(C.FontSpace));
		F.Text->SetParagraphHorizontalAlignment(C.HAlign);
		F.Text->SetParagraphVerticalAlignment(C.VAlign);
		F.Text->SetOverflowType(C.Overflow);
		F.Text->SetWrappingPolicy(C.Wrapping);
		F.Text->SetUseKerning(C.bKerning);
		F.Text->SetFontStyle(C.Style);
		F.Text->SetRichText(C.bRichText);
		F.Text->SetRichTextTagFilterFlags(C.RichFlags);
		F.Text->SetLineHeightPercentage(C.LineHeightPercentage);
		F.Text->SetWrapTextAt(C.WrapTextAt);
		F.Text->SetMargin(C.Margin);
		F.Text->SetColor(C.Color);
		F.Text->SetExpandMeshSize(C.ExpandMeshSize);
	}

	void RunOld(FFixture& F, const FCase& C, FOldResult& Out)
	{
		auto Widget = F.Text->GetWidget();
		FVector2f ContentSize, ContentPivot;
		UDreamText::GetContentBox(FVector2f(Widget->GetWidth(), Widget->GetHeight()), FVector2f(Widget->GetPivot()), F.Text->GetMargin(),
			ContentSize, ContentPivot);
		const float Opacity = F.Text->GetRichText() ? Widget->GetFinalRenderOpacity() : 1.0f;
		TArray<FDreamUIText_TextProcessingElement> Processing;
		FDreamUIGeometry::UpdateUIText(
			F.Text->GetText().ToString()
			, Processing
			, ContentSize.X
			, ContentSize.Y
			, ContentPivot
			, F.Text->GetFinalColor()
			, (uint8)(Opacity * 255)
			, FVector2f(F.Text->GetFontSpace())
			, &Out.Geometry
			, F.Text->GetFontSize()
			, F.Text->GetParagraphHorizontalAlignment()
			, F.Text->GetParagraphVerticalAlignment()
			, F.Text->GetOverflowType()
			, F.Text->GetWrappingPolicy()
			, F.Text->GetUseKerning()
			, F.Text->GetFontStyle()
			, Out.PreferredSize
			, Out.bTruncated
			, Widget->GetRenderCanvas()
			, F.Text
			, Out.Lines
			, Out.Chars
			, Out.Tags
			, Out.Images
			, Out.Emojis
			, F.Text->GetFont()
			, F.Text->GetRichText()
			, F.Text->GetRichTextTagFilterFlags()
		);
	}

	void RunNew(FFixture& F, const FCase& C, FNewResult& Out)
	{
		const FDreamTextLayoutInput Input = UDreamText::MakeLayoutInput(F.Text, F.Text->GetFontSize());
		FDreamTextLayoutEngine::Layout(Input, Out.DisplayList);
		FDreamTextPainter::Paint(Out.DisplayList, UDreamText::MakePaintParams(F.Text), Out.Geometry, Out.Chars);
	}

	bool NearlyEqual(const FVector3f& A, const FVector3f& B, float Tolerance)
	{
		return A.Equals(B, Tolerance);
	}
	bool NearlyEqual(const FVector2f& A, const FVector2f& B, float Tolerance)
	{
		return A.Equals(B, Tolerance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLayoutGoldenTest,
	"DreamGUI.Text.Pipeline.GoldenAgainstLegacyUpdateUIText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextLayoutGoldenTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextGoldenTestLocal;
	FScopedGameWorld TestWorld;
	FFixture F;
	if (!TestTrue(TEXT("fixture builds and renders through a canvas"), F.Build(TestWorld.World)))return false;

	const float Tolerance = 1.0e-3f;
	TArray<FCase> Cases;
	{
		FCase C; C.Name = TEXT("single line left/top"); C.Content = TEXT("Hello, World!"); Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("single line centre/middle"); C.Content = TEXT("Hello, World!");
		C.HAlign = EDreamUITextParagraphHorizontalAlign::Center; C.VAlign = EDreamUITextParagraphVerticalAlign::Middle; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("single line right/bottom corner pivot"); C.Content = TEXT("Hello, World!");
		C.HAlign = EDreamUITextParagraphHorizontalAlign::Right; C.VAlign = EDreamUITextParagraphVerticalAlign::Bottom; C.Pivot = FVector2D(0.0, 1.0); Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("explicit line breaks, crlf"); C.Content = TEXT("first line\r\nsecond\nthird line here"); C.VAlign = EDreamUITextParagraphVerticalAlign::Middle; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("word wrap, per-character allowed"); C.Content = TEXT("The quick brown fox jumps over the lazy dog and keeps running through the field");
		C.Overflow = EDreamUITextOverflowType::VerticalOverflow; C.HAlign = EDreamUITextParagraphHorizontalAlign::Center; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("word wrap, default policy"); C.Content = TEXT("Supercalifragilisticexpialidocious words wrap only at spaces here");
		C.Overflow = EDreamUITextOverflowType::VerticalOverflow; C.Wrapping = ETextWrappingPolicy::DefaultWrapping; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("cjk per-character wrap with punctuation"); C.Content = TEXT("像雪崩来临般勇敢地往前冲，看见孤独的火焰。再走几步，风景就变了！");
		C.Overflow = EDreamUITextOverflowType::VerticalOverflow; C.WidgetSize = FVector2D(200.0, 300.0); Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("wrap at authored width, line height scaled"); C.Content = TEXT("one two three four five six seven eight nine ten eleven twelve");
		C.Overflow = EDreamUITextOverflowType::VerticalOverflow; C.WrapTextAt = 150.0f; C.LineHeightPercentage = 1.5f; C.HAlign = EDreamUITextParagraphHorizontalAlign::Center; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("font space, no kerning, bold italic"); C.Content = TEXT("Spaced out text!");
		C.FontSpace = FVector2f(3.0f, 4.0f); C.bKerning = false; C.Style = EDreamUITextFontStyle::BoldAndItalic; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("margin and expand"); C.Content = TEXT("Inset text");
		C.Margin = FMargin(10.0f, 5.0f, 20.0f, 15.0f); C.ExpandMeshSize = 2.0f; C.VAlign = EDreamUITextParagraphVerticalAlign::Middle; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("truncate"); C.Content = TEXT("This sentence is far too long to fit inside the box it was given");
		C.Overflow = EDreamUITextOverflowType::Truncate; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("ellipsis"); C.Content = TEXT("This sentence is far too long to fit inside the box it was given");
		C.Overflow = EDreamUITextOverflowType::Ellipsis; C.bCompareCharProperties = false; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("rich text styles"); C.bRichText = true;
		C.Content = TEXT("<b>Bold</b> <i>Italic</i> <u>Under</u> <s>Strike</s> <size=36>Big</size> <size=-8>small</size> <color=#ff0000>red</color> x<sup>2</sup> H<sub>2</sub>O");
		C.VAlign = EDreamUITextParagraphVerticalAlign::Middle; C.HAlign = EDreamUITextParagraphHorizontalAlign::Center; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("rich text custom tags and wrap"); C.bRichText = true;
		C.Content = TEXT("plain <shake>shaking words here</shake> and <glow>glow</glow> then more plain words to force wrapping across lines");
		C.Overflow = EDreamUITextOverflowType::VerticalOverflow; C.WidgetSize = FVector2D(220.0, 200.0); Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("rich text with image placeholders"); C.bRichText = true;
		C.Content = TEXT("smile <img=smile/> and <img=wink/> end");
		C.HAlign = EDreamUITextParagraphHorizontalAlign::Right; Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("emoji surrogates"); C.Content = TEXT("party \U0001F389 time \U0001F600!"); Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("empty"); C.Content = TEXT(""); Cases.Add(C);
	}
	{
		FCase C; C.Name = TEXT("only newlines and spaces"); C.Content = TEXT("  \n \n  "); C.Overflow = EDreamUITextOverflowType::VerticalOverflow; Cases.Add(C);
	}

	for (const FCase& C : Cases)
	{
		ApplyCase(F, C);
		FOldResult Old;
		FNewResult New;
		RunOld(F, C, Old);
		RunNew(F, C, New);

		auto Label = [&](const TCHAR* What) { return FString::Printf(TEXT("[%s] %s"), *C.Name, What); };

		// A comparison of two empty results proves nothing: every case with visible text must emit.
		if (C.Content.TrimStartAndEnd().Len() > 0)
		{
			TestTrue(*Label(TEXT("legacy pipeline emitted vertices")), Old.Geometry.OriginVertices.Num() > 0);
			AddInfo(FString::Printf(TEXT("%s: %d vertices, %d lines, %d chars, preferred %s"), *C.Name,
				Old.Geometry.OriginVertices.Num(), Old.Lines.Num(), Old.Chars.Num(), *Old.PreferredSize.ToString()));
		}
		TestEqual(*Label(TEXT("preferred size X")), New.DisplayList.PreferredSize.X, Old.PreferredSize.X, Tolerance);
		TestEqual(*Label(TEXT("preferred size Y")), New.DisplayList.PreferredSize.Y, Old.PreferredSize.Y, Tolerance);
		TestEqual(*Label(TEXT("truncated flag")), New.DisplayList.bTruncated, Old.bTruncated);

		// Geometry
		if (TestEqual(*Label(TEXT("vertex count")), New.Geometry.OriginVertices.Num(), Old.Geometry.OriginVertices.Num())
			&& TestEqual(*Label(TEXT("mesh vertex count")), New.Geometry.Vertices.Num(), Old.Geometry.Vertices.Num()))
		{
			for (int32 i = 0; i < Old.Geometry.OriginVertices.Num(); i++)
			{
				const auto& A = Old.Geometry.OriginVertices[i];
				const auto& B = New.Geometry.OriginVertices[i];
				if (!NearlyEqual(A.Position, B.Position, Tolerance))
				{
					AddError(FString::Printf(TEXT("%s: vertex %d position old %s new %s"), *Label(TEXT("origin vertex")), i, *A.Position.ToString(), *B.Position.ToString()));
					break;
				}
				if (!NearlyEqual(A.Normal, B.Normal, Tolerance) || !NearlyEqual(A.Tangent, B.Tangent, Tolerance))
				{
					AddError(FString::Printf(TEXT("%s: vertex %d normal/tangent differ"), *Label(TEXT("origin vertex")), i));
					break;
				}
			}
			for (int32 i = 0; i < Old.Geometry.Vertices.Num(); i++)
			{
				const auto& A = Old.Geometry.Vertices[i];
				const auto& B = New.Geometry.Vertices[i];
				if (A.Color != B.Color)
				{
					AddError(FString::Printf(TEXT("%s: vertex %d colour old %s new %s"), *Label(TEXT("vertex")), i, *A.Color.ToString(), *B.Color.ToString()));
					break;
				}
				bool bUVMismatch = false;
				for (int32 uv = 0; uv < 4; uv++)
				{
					// UV1.x is the widget property slot, written later by the component; both pipelines leave it alone.
					FVector2f UA = A.TextureCoordinate[uv];
					FVector2f UB = B.TextureCoordinate[uv];
					if (uv == 1) { UA.X = 0; UB.X = 0; }
					if (!NearlyEqual(UA, UB, 1.0e-5f))
					{
						AddError(FString::Printf(TEXT("%s: vertex %d uv%d old %s new %s"), *Label(TEXT("vertex")), i, uv, *UA.ToString(), *UB.ToString()));
						bUVMismatch = true;
						break;
					}
				}
				if (bUVMismatch)break;
			}
		}
		if (TestEqual(*Label(TEXT("index count")), New.Geometry.Triangles.Num(), Old.Geometry.Triangles.Num()))
		{
			for (int32 i = 0; i < Old.Geometry.Triangles.Num(); i++)
			{
				if (Old.Geometry.Triangles[i] != New.Geometry.Triangles[i])
				{
					AddError(FString::Printf(TEXT("%s: index %d old %d new %d"), *Label(TEXT("triangles")), i, (int32)Old.Geometry.Triangles[i], (int32)New.Geometry.Triangles[i]));
					break;
				}
			}
		}

		// Caret lines
		if (TestEqual(*Label(TEXT("line count")), New.DisplayList.Lines.Num(), Old.Lines.Num()))
		{
			for (int32 l = 0; l < Old.Lines.Num(); l++)
			{
				const auto& LA = Old.Lines[l].CaretPropertyList;
				const auto& LB = New.DisplayList.Lines[l].CaretPropertyList;
				if (!TestEqual(*Label(*FString::Printf(TEXT("line %d caret count"), l)), LB.Num(), LA.Num()))continue;
				for (int32 c = 0; c < LA.Num(); c++)
				{
					if (LA[c].CharIndex != LB[c].CharIndex || !NearlyEqual(LA[c].CaretPosition, LB[c].CaretPosition, Tolerance))
					{
						AddError(FString::Printf(TEXT("%s: line %d caret %d old (%d, %s) new (%d, %s)"), *Label(TEXT("carets")), l, c,
							LA[c].CharIndex, *LA[c].CaretPosition.ToString(), LB[c].CharIndex, *LB[c].CaretPosition.ToString()));
						break;
					}
				}
			}
		}

		// Char properties
		if (C.bCompareCharProperties && TestEqual(*Label(TEXT("char property count")), New.Chars.Num(), Old.Chars.Num()))
		{
			for (int32 i = 0; i < Old.Chars.Num(); i++)
			{
				const auto& A = Old.Chars[i];
				const auto& B = New.Chars[i];
				if (A.CharIndex != B.CharIndex || A.StartVertIndex != B.StartVertIndex || A.VertCount != B.VertCount
					|| A.StartTriangleIndex != B.StartTriangleIndex || A.IndicesCount != B.IndicesCount)
				{
					AddError(FString::Printf(TEXT("%s: char %d differs (old char %d v%d+%d t%d+%d, new char %d v%d+%d t%d+%d)"), *Label(TEXT("char properties")), i,
						A.CharIndex, A.StartVertIndex, A.VertCount, A.StartTriangleIndex, A.IndicesCount,
						B.CharIndex, B.StartVertIndex, B.VertCount, B.StartTriangleIndex, B.IndicesCount));
					break;
				}
			}
		}

		// Rich text side tables
		if (TestEqual(*Label(TEXT("custom tag count")), New.DisplayList.CustomTags.Num(), Old.Tags.Num()))
		{
			for (int32 i = 0; i < Old.Tags.Num(); i++)
			{
				const auto& A = Old.Tags[i];
				const auto& B = New.DisplayList.CustomTags[i];
				if (A.TagName != B.TagName || A.CharIndexStart != B.CharIndexStart || A.CharIndexEnd != B.CharIndexEnd)
				{
					AddError(FString::Printf(TEXT("%s: tag %d old %s[%d,%d] new %s[%d,%d]"), *Label(TEXT("custom tags")), i,
						*A.TagName.ToString(), A.CharIndexStart, A.CharIndexEnd, *B.TagName.ToString(), B.CharIndexStart, B.CharIndexEnd));
					break;
				}
			}
		}
		if (TestEqual(*Label(TEXT("image tag count")), New.DisplayList.Images.Num(), Old.Images.Num()))
		{
			for (int32 i = 0; i < Old.Images.Num(); i++)
			{
				const auto& A = Old.Images[i];
				const auto& B = New.DisplayList.Images[i];
				if (A.TagName != B.TagName || !A.Position.Equals(B.Position, Tolerance) || !A.Size.Equals(B.Size, Tolerance) || A.TintColor != B.TintColor)
				{
					AddError(FString::Printf(TEXT("%s: image %d differs"), *Label(TEXT("image tags")), i));
					break;
				}
			}
		}
		if (TestEqual(*Label(TEXT("emoji count")), New.DisplayList.Emojis.Num(), Old.Emojis.Num()))
		{
			for (int32 i = 0; i < Old.Emojis.Num(); i++)
			{
				const auto& A = Old.Emojis[i];
				const auto& B = New.DisplayList.Emojis[i];
				if (A.EmojiCode != B.EmojiCode || !A.Position.Equals(B.Position, Tolerance) || !A.Size.Equals(B.Size, Tolerance))
				{
					AddError(FString::Printf(TEXT("%s: emoji %d differs"), *Label(TEXT("emojis")), i));
					break;
				}
			}
		}
	}

	F.Root->DestroyWidget();
	return true;
}

#endif
