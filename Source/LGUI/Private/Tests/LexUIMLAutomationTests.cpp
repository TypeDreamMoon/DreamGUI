#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
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

#endif
