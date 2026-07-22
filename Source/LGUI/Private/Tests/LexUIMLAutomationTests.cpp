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
		"  <Component Class=\"Slider\" VarName=\"AmountSlider\" MinValue=\"10\" MaxValue=\"50\" Value=\"25\"/>"
		"  <Component Class=\"/Script/LGUI.UITextInput\" VarName=\"NameInput\"><Text Value=\"Alice\"/></Component>"
		"  <Component Class=\"Toggle\" VarName=\"ReadyToggle\" bIsOn=\"false\"/>"
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

		if (Behaviour->AmountSlider)
		{
			TestEqual(TEXT("Component minimum property is imported"), Behaviour->AmountSlider->GetMinValue(), 10.0f);
			TestEqual(TEXT("Component maximum property is imported"), Behaviour->AmountSlider->GetMaxValue(), 50.0f);
			TestEqual(TEXT("Component value property is imported"), Behaviour->AmountSlider->GetValue(), 25.0f);
		}
		if (Behaviour->NameInput)
		{
			TestEqual(TEXT("Nested component property is imported"), Behaviour->NameInput->GetText(), FString(TEXT("Alice")));
		}
		if (Behaviour->ReadyToggle)
		{
			TestFalse(TEXT("Boolean component property is imported"), Behaviour->ReadyToggle->GetValue());
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

#endif
