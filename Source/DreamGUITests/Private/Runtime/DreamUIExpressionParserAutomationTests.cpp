// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"

/*
 * The right side of `<-` as an expression. The load-bearing property is BACKWARDS COMPATIBILITY:
 * a bare `Func()` must keep travelling as BindingFunction, byte for byte, because everything
 * downstream of the parser was built against that shape -- expressions ride the new
 * BindingExpression field and only the compiler's thunk pass ever lowers them.
 */

namespace DreamUIExpressionParserTestLocal
{
	FString Wrap(const FString& InBindingLine)
	{
		return FString::Join(TArray<FString>{
			TEXT("class /Game/UI/WBP_ExprFixture"),
			TEXT("Widget Root {"),
			TEXT("    ") + InBindingLine,
			TEXT("}")}, TEXT("\n"));
	}

	/** Parse one wrapped binding line; null when the parse failed. */
	const FDreamUIProperty* ParseBinding(const FString& InBindingLine, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (!FDreamUISourceFile::Parse(Wrap(InBindingLine), TEXT("Expr.dui"), OutAst, OutDiagnostics))
		{
			return nullptr;
		}
		return OutAst.Root.Properties.Num() > 0 ? &OutAst.Root.Properties[0] : nullptr;
	}

	bool Reported(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
		{
			return InDiagnostic.Code == InCode;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIExpressionBareCallCompatTest,
	"DreamGUI.Text.Expression.ABareCallStillTravelsAsTheOneName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIExpressionBareCallCompatTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionParserTestLocal;
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const FDreamUIProperty* Property = ParseBinding(TEXT("Text <- GetTitle()"), Ast, Diagnostics);
	if (!TestTrue(TEXT("Parses"), Property != nullptr)) { return false; }
	TestEqual(TEXT("The name rides BindingFunction, exactly as before"), Property->BindingFunction, FString(TEXT("GetTitle")));
	TestFalse(TEXT("No expression is carried for the bare shape"), Property->BindingExpression.IsSet());
	TestTrue(TEXT("It still counts as a binding"), Property->IsBinding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIExpressionShapesTest,
	"DreamGUI.Text.Expression.OperatorsParseWithCFamilyPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIExpressionShapesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionParserTestLocal;
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseBinding(TEXT("bWidgetActive <- !IsLoading()"), Ast, Diagnostics);
		if (!TestTrue(TEXT("'!Call()' parses"), Property != nullptr && Property->BindingExpression.IsSet())) { return false; }
		const FDreamUIExpression& Root = Property->BindingExpression.GetValue();
		TestTrue(TEXT("Root is unary '!'"), Root.Kind == FDreamUIExpression::EKind::Unary && Root.Symbol == TEXT("!"));
		TestTrue(TEXT("...over the call"), Root.Operands.Num() == 1
			&& Root.Operands[0].Kind == FDreamUIExpression::EKind::Call
			&& Root.Operands[0].Symbol == TEXT("IsLoading"));
		TestTrue(TEXT("A binding with an expression is still a binding"), Property->IsBinding());
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseBinding(TEXT("FontSize <- Base() + Step() * Count()"), Ast, Diagnostics);
		if (!TestTrue(TEXT("Arithmetic parses"), Property != nullptr && Property->BindingExpression.IsSet())) { return false; }
		const FDreamUIExpression& Root = Property->BindingExpression.GetValue();
		// '*' binds tighter than '+': the tree is Base + (Step * Count).
		TestTrue(TEXT("Root is '+'"), Root.Kind == FDreamUIExpression::EKind::Binary && Root.Symbol == TEXT("+"));
		TestTrue(TEXT("Right child is the '*'"), Root.Operands.Num() == 2
			&& Root.Operands[1].Kind == FDreamUIExpression::EKind::Binary && Root.Operands[1].Symbol == TEXT("*"));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		// The spaced spelling: '<' then '-1'. Written '<-1' this is an arrow, and that is documented.
		const FDreamUIProperty* Property = ParseBinding(TEXT("bWidgetActive <- Count() < -1"), Ast, Diagnostics);
		if (!TestTrue(TEXT("'a < -1' parses with the space"), Property != nullptr && Property->BindingExpression.IsSet())) { return false; }
		const FDreamUIExpression& Root = Property->BindingExpression.GetValue();
		TestTrue(TEXT("Root is '<'"), Root.Kind == FDreamUIExpression::EKind::Binary && Root.Symbol == TEXT("<"));
		TestTrue(TEXT("The right side is the negative literal"), Root.Operands.Num() == 2
			&& Root.Operands[1].Kind == FDreamUIExpression::EKind::Literal
			&& Root.Operands[1].LiteralRaw == TEXT("-1"));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseBinding(TEXT("Text <- Format(GetName(), 3)"), Ast, Diagnostics);
		if (!TestTrue(TEXT("Calls take argument lists now"), Property != nullptr && Property->BindingExpression.IsSet())) { return false; }
		const FDreamUIExpression& Root = Property->BindingExpression.GetValue();
		TestTrue(TEXT("Two arguments"), Root.Kind == FDreamUIExpression::EKind::Call && Root.Operands.Num() == 2);
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const FDreamUIProperty* Property = ParseBinding(TEXT("Value <- Volume"), Ast, Diagnostics);
		if (!TestTrue(TEXT("A bare identifier is a variable source"), Property != nullptr && Property->BindingExpression.IsSet())) { return false; }
		TestTrue(TEXT("...as a VariableRef"), Property->BindingExpression.GetValue().Kind == FDreamUIExpression::EKind::VariableRef);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIExpressionErrorsTest,
	"DreamGUI.Text.Expression.MalformedExpressionsReportDUI2011",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIExpressionErrorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionParserTestLocal;
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseBinding(TEXT("Value <- 1 +"), Ast, Diagnostics);
		TestTrue(TEXT("A dangling operator is 2011"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedBindingExpression));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseBinding(TEXT("Value <- A() B()"), Ast, Diagnostics);
		TestTrue(TEXT("Trailing junk after the expression is 2011"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedBindingExpression));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		ParseBinding(TEXT("Value <- (A() && B()"), Ast, Diagnostics);
		TestTrue(TEXT("An unclosed paren is 2011"), Reported(Diagnostics, EDreamUIDiagnosticCode::MalformedBindingExpression));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIExpressionMinusRegressionTest,
	"DreamGUI.Text.Expression.NegativeLiteralsOutsideExpressionsStillLex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIExpressionMinusRegressionTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIExpressionParserTestLocal;
	// The '-' rule grew a subtraction meaning; every spelling that used to be a negative number has
	// to stay one. Tuples and plain assignments are the two that carried them.
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	const FString Source = FString::Join(TArray<FString>{
		TEXT("class /Game/UI/WBP_MinusFixture"),
		TEXT("Widget Root {"),
		TEXT("    AnchorData.SizeDelta = (400, -240)"),
		TEXT("    RenderOpacity = -0.5"),
		TEXT("}")}, TEXT("\n"));
	TestTrue(TEXT("Negative literals parse where they always did"),
		FDreamUISourceFile::Parse(Source, TEXT("Minus.dui"), Ast, Diagnostics));
	TestEqual(TEXT("No diagnostics at all"), Diagnostics.Diagnostics.Num(), 0);
	return true;
}

#endif
