// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "LexUIMLTestTypes.h"
#include "UObject/UnrealType.h"
#include "XMLSupport/LexUIML.h"
#include "XMLSupport/LexUIMLBehaviour.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIMLSourcePathTest,
	"LGUI.UIML.SourcePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIMLSourcePathTest::RunTest(const FString& Parameters)
{
	const FString RelativePath = TEXT("UI/UIML/Test.xaml");
	FString ExpectedPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), RelativePath));
	FPaths::NormalizeFilename(ExpectedPath);
	TestEqual(TEXT("Relative UIML paths resolve from Content"), ULexUIMLBehaviour::ResolveUIMLPath(RelativePath), ExpectedPath);

	FString AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UIML/Test.xaml")));
	FPaths::NormalizeFilename(AbsolutePath);
	TestEqual(TEXT("Absolute UIML paths are preserved"), ULexUIMLBehaviour::ResolveUIMLPath(AbsolutePath), AbsolutePath);
	TestTrue(TEXT("Blank UIML paths remain blank"), ULexUIMLBehaviour::ResolveUIMLPath(TEXT("   ")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIMLDeclarativeComponentsTest,
	"LGUI.UIML.DeclarativeComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIMLDeclarativeComponentsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::None, false);
	TestNotNull(TEXT("Test world created"), World);
	if (!World) return false;

	const FString Markup = TEXT(
		"<Image DisplayName=\"Root\">"
		"  <Component Class=\"Button\" VarName=\"ActionButton\" IdName=\"SubmitAction\" Event:OnClick=\"HandleClick\"/>"
		"  <Component Class=\"Slider\" VarName=\"AmountSlider\" MinValue=\"10\" MaxValue=\"50\" Value=\"25\" Fill=\"IdName:FillBar\"/>"
		"  <Component Class=\"/Script/LGUI.UITextInput\" VarName=\"NameInput\" Text=\"Alice\" TextVisual=\"Visual:InputText\"/>"
		"  <Component Class=\"Toggle\" VarName=\"ReadyToggle\" bIsOn=\"false\"/>"
		"  <Image IdName=\"FillBar\"/>"
		"  <Text IdName=\"InputText\"/>"
		"  <Text VarName=\"StatusLabel\" Bind:Text=\"StatusText\"/>"
		"  <Widget VarName=\"VisibilityPanel\" Bind:WidgetActive=\"bPanelVisible\"/>"
		"</Image>");

	FLexUIMLUtils Parser(false, nullptr);
	ULexUIMLBehaviour* Parsed = Parser.LoadFromString(World, nullptr, ULexUIMLTestBehaviour::StaticClass(), nullptr, Markup);
	ULexUIMLTestBehaviour* Behaviour = Cast<ULexUIMLTestBehaviour>(Parsed);
	TestNotNull(TEXT("Declarative markup creates the script behaviour"), Behaviour);
	if (Behaviour)
	{
		TestNotNull(TEXT("Button alias creates and binds UUIButton"), Behaviour->ActionButton.Get());
		TestNotNull(TEXT("Slider alias creates and binds UUISlider"), Behaviour->AmountSlider.Get());
		TestNotNull(TEXT("Full class path creates and binds UUITextInput"), Behaviour->NameInput.Get());
		TestNotNull(TEXT("Toggle alias creates and binds UUIToggle"), Behaviour->ReadyToggle.Get());
		TestNotNull(TEXT("Text visual binds through VarName"), Behaviour->StatusLabel.Get());
		TestNotNull(TEXT("Visibility widget binds through VarName"), Behaviour->VisibilityPanel.Get());

		if (Behaviour->AmountSlider)
		{
			TestEqual(TEXT("Component minimum property is imported"), Behaviour->AmountSlider->GetMinValue(), 10.0f);
			TestEqual(TEXT("Component maximum property is imported"), Behaviour->AmountSlider->GetMaxValue(), 50.0f);
			TestEqual(TEXT("Component value property is imported"), Behaviour->AmountSlider->GetValue(), 25.0f);
			TestNotNull(TEXT("Forward IdName reference resolves a widget"), Behaviour->AmountSlider->GetFill());
		}
		if (Behaviour->NameInput)
		{
			TestEqual(TEXT("Component string property is imported"), Behaviour->NameInput->GetText(), FString(TEXT("Alice")));
			TestNotNull(TEXT("Forward Visual reference resolves a visual"), Behaviour->NameInput->GetTextComponent());
		}
		if (Behaviour->ReadyToggle)
		{
			TestFalse(TEXT("Boolean component property is imported"), Behaviour->ReadyToggle->GetValue());
		}
		if (Behaviour->StatusLabel && Behaviour->VisibilityPanel)
		{
			TestEqual(TEXT("Text binding applies its initial value"), Behaviour->StatusLabel->GetText().ToString(), FString(TEXT("Waiting")));
			TestTrue(TEXT("WidgetActive binding applies its initial value"), Behaviour->VisibilityPanel->GetWidgetActive());

			Behaviour->StatusText = FText::FromString(TEXT("Ready"));
			Behaviour->bPanelVisible = false;
			ULexUIMLBindingBehaviour* BindingHost = Behaviour->GetWidget()->GetComponent<ULexUIMLBindingBehaviour>();
			TestNotNull(TEXT("Bindings create a runtime binding host"), BindingHost);
			if (BindingHost)
			{
				BindingHost->RefreshBindings();
				TestEqual(TEXT("Text binding refreshes changed source data"), Behaviour->StatusLabel->GetText().ToString(), FString(TEXT("Ready")));
				TestFalse(TEXT("WidgetActive binding refreshes changed source data"), Behaviour->VisibilityPanel->GetWidgetActive());
			}
		}
		if (Behaviour->ActionButton)
		{
			FMulticastDelegateProperty* ClickProperty = FindFProperty<FMulticastDelegateProperty>(UUIButton::StaticClass(), TEXT("OnClickBP"));
			TestNotNull(TEXT("Button click delegate is reflected"), ClickProperty);
			if (ClickProperty)
			{
				FMulticastScriptDelegate* ClickDelegate = ClickProperty->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Behaviour->ActionButton);
				ClickDelegate->ProcessDelegate<UObject>(nullptr);
				TestEqual(TEXT("Component event invokes the script function"), Behaviour->ClickCount, 1);
			}
		}
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexUIMLValidationTest,
	"LGUI.UIML.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexUIMLValidationTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const FString ValidMarkup = TEXT(
		"<Widget>"
		"  <Image IdName=\"Target\"><Component Class=\"Button\" Event:OnClick=\"HandleClick\"/></Image>"
		"  <Text VarName=\"StatusLabel\" Bind:Text=\"StatusText\"/>"
		"</Widget>");
	TestTrue(TEXT("Valid markup passes semantic validation"),
		FLexUIMLUtils::ValidateString(ValidMarkup, ULexUIMLTestBehaviour::StaticClass(), Errors));
	TestEqual(TEXT("Valid markup reports no errors"), Errors.Num(), 0);

	const FString InvalidMarkup = TEXT(
		"<Widget>"
		"  <Image IdName=\"Duplicate\"/>"
		"  <Text IdName=\"Duplicate\" VarName=\"MissingVar\" Bind:Text=\"MissingSource\"/>"
		"  <Component Class=\"MissingControl\" Event:OnClick=\"MissingFunction\" Fill=\"IdName:MissingId\"/>"
		"</Widget>");
	TestFalse(TEXT("Invalid markup fails semantic validation"),
		FLexUIMLUtils::ValidateString(InvalidMarkup, ULexUIMLTestBehaviour::StaticClass(), Errors));
	TestTrue(TEXT("Invalid markup reports all independent failures"), Errors.Num() >= 6);
	return true;
}

#endif
