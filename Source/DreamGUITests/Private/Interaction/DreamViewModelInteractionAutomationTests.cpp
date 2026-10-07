// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamProgressBar.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamSpinBox.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "DreamViewModelRuntimeTestTypes.h"
#include "DreamViewModelTestTypes.h"
#include "DreamWidgetBlueprint.h"
#include "Event/DreamEventSystem.h"

#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * A view model reached through the controls bound to it, by a player's input.
 *
 * The view-model suites (DreamGUI.ViewModel.Runtime, DreamGUI.Text.ViewModel.Compile) drive the binding from the view
 * model's side, or call the generated setter themselves. These compile a real .dui with a `viewmodels` entry, put the
 * class on the rig's screen through the runtime's own factory, hand it a view model as a host does, and then act only
 * on the controls: a slider bound both ways dragged with the pointer, a spin box bound both ways typed into, a button
 * routed to a view model function clicked. What each changes in the view model is asserted, and so is the other control
 * bound to the same member, which only a change announced by the view model can have reached -- UMG's MVVM gives a
 * two-way binding the same round trip (the control writes the view model, the view model's field notification updates
 * every other binding of it).
 *
 * A text bound to the slider's member would need a converter, which a .dui view model does not have (a view model
 * offers the display-ready member instead); a progress bar's Percent shows the same member as it is.
 */
namespace DreamViewModelInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
		}
		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*FilePath, false, true, true);
		}
		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}
		FString FilePath;
	};

	/** A .dui-backed Blueprint in /Temp, its package rooted for the test, pointed at InFilePath and compiled. */
	struct FScopedTextBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FCompilerResultsLog Results;

		FScopedTextBlueprint(const TCHAR* InName, const FString& InFilePath)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			// The parent the view-model suites compile their .dui fixtures on, from this module: the test view models
			// are this module's too, and a Blueprint calls an editor module's functions only when its parent class is an
			// editor module's (UK2Node_CallFunction::ValidateNodeDuringCompilation) -- on a runtime parent, Heal and
			// SetVolume would be "editor functions in a runtime Blueprint". A game's view models are in its runtime code.
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTestViewModelWidgetBase::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults == nullptr)
			{
				Blueprint = nullptr;
				return;
			}
			Defaults->SourceFile.FilePath = InFilePath;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		}
		~FScopedTextBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}
		UClass* GetClass() const { return Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr; }
		FString Messages() const
		{
			FString All;
			for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
			{
				All += Message->ToText().ToString() + TEXT(" | ");
			}
			return All;
		}
	};

	/**
	 * The compiled class on the rig's screen, given InViewModel as Player the way a host hands one over. Null, having
	 * said why, when the file did not compile clean or the instance did not take the view model.
	 */
	UDreamUserWidget* PlaceCompiled(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FScopedTextBlueprint& InFixture, UObject* InViewModel)
	{
		UClass* Class = InFixture.GetClass();
		if (!InTest.TestNotNull(TEXT("The .dui compiled into a class"), Class)
			|| !InTest.TestEqual(*FString::Printf(TEXT("...clean, saw [%s]"), *InFixture.Messages()), InFixture.Results.NumErrors, 0))
		{
			return nullptr;
		}
		UDreamUserWidget* Instance = Cast<UDreamUserWidget>(InRig.MakeControl(Class, TEXT("ViewModelScreen"), nullptr, FVector2D(600.0, 400.0)));
		if (!InTest.TestNotNull(TEXT("The class is placed on the rig's screen"), Instance)
			|| !InTest.TestTrue(TEXT("...and takes the view model"), Instance->SetViewModel(TEXT("Player"), InViewModel)))
		{
			return nullptr;
		}
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(2);
		return Instance;
	}

	template<class T>
	T* PartNamed(const UDreamUserWidget* InInstance, const TCHAR* InName)
	{
		return InInstance != nullptr ? Cast<T>(InInstance->GetWidgetFromName(FName(InName))) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewModelSliderInteractionTest,
	"DreamGUI.ViewModel.Interaction.DraggingASliderBoundBothWaysWritesTheViewModelAndTheBarBoundToTheSameMemberFollows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamViewModelSliderInteractionTest, "DreamGUI.ViewModel.Interaction.DraggingASliderBoundBothWaysWritesTheViewModelAndTheBarBoundToTheSameMemberFollows", "[Pointer][Animated]")

/*
 * `Value <-> Player.Volume` on a slider and `Percent <- Player.Volume` on a bar. The slider's handle dragged three
 * quarters along: the view model's Volume is three quarters, written through its own SetVolume, and the bar shows it.
 * Then the other way: the view model set from code moves the slider's value back with it.
 */
bool FDreamViewModelSliderInteractionTest::RunTest(const FString& Parameters)
{
	using namespace DreamViewModelInteractionTestLocal;
	FScopedDuiFile File(TEXT("ViewModelSliderInteraction.dui"));
	TestTrue(TEXT("The file was written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_ViewModelSliderInteraction"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("CanvasPanel Root {"),
		TEXT("    Native.Slider Vol { AnchorData.SizeDelta = (400, 40)  AnchorData.AnchoredPosition = (0, 60)  Value <-> Player.Volume }"),
		TEXT("    Native.ProgressBar Meter { AnchorData.SizeDelta = (400, 20)  AnchorData.AnchoredPosition = (0, -60)  Percent <- Player.Volume }"),
		TEXT("}") }));
	// Before the rig, so the rig takes its instance of the class down before the class's package is let go.
	FScopedTextBlueprint Fixture(TEXT("BP_ViewModelSliderInteraction"), File.FilePath);
	TStrongObjectPtr<UDreamTestPlayerVM> Player(NewObject<UDreamTestPlayerVM>(GetTransientPackage()));
	Player->Volume = 0.0f;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUserWidget* Instance = PlaceCompiled(*this, Rig, Fixture, Player.Get());
	UDreamSlider* Slider = PartNamed<UDreamSlider>(Instance, TEXT("Vol"));
	UDreamProgressBar* Meter = PartNamed<UDreamProgressBar>(Instance, TEXT("Meter"));
	if (!TestTrue(TEXT("The slider and the bar are there, the slider with its handle"), Slider != nullptr && Meter != nullptr
		&& Slider->HandleNode != nullptr && Slider->HandleAreaNode != nullptr))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	const TOptional<FBox2D> Travel = Driver->Find(FDreamBy::Widget(Slider->HandleAreaNode.Get()))->GetPixelRect();
	const TOptional<FVector2D> Grip = Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()))->GetCentrePixel();
	if (!TestTrue(TEXT("The slider's travel and handle are on screen"), Travel.IsSet() && Grip.IsSet() && Travel->Max.X - Travel->Min.X > 100.0))
	{
		return false;
	}
	const float OnePixel = static_cast<float>(1.0 / (Travel->Max.X - Travel->Min.X));
	const int32 SetterCallsBefore = Player->SetVolumeCount;
	const double TargetX = Travel->Min.X + 0.75 * (Travel->Max.X - Travel->Min.X);

	TestTrue(TEXT("Dragging the slider's handle completes"), Driver->Find(FDreamBy::Widget(Slider->HandleNode.Get()))->DragBy(FVector2D(TargetX - Grip->X, 0.0)));

	TestNearlyEqual(TEXT("The slider is three quarters"), Slider->GetValue(), 0.75f, OnePixel * 1.5f);
	TestNearlyEqual(TEXT("...and so is the view model's Volume"), Player->Volume, Slider->GetValue(), 0.0001f);
	TestTrue(FString::Printf(TEXT("...written through the view model's own SetVolume (%d calls)"), Player->SetVolumeCount - SetterCallsBefore),
		Player->SetVolumeCount > SetterCallsBefore);
	TestNearlyEqual(TEXT("The bar bound to the same member shows it"), Meter->GetPercent(), Player->Volume, 0.0001f);

	Player->SetVolume(0.2f);
	Rig.PumpFrames(1);
	TestNearlyEqual(TEXT("A change made on the view model moves the slider back with it"), Slider->GetValue(), 0.2f, 0.0001f);
	TestNearlyEqual(TEXT("...and the bar"), Meter->GetPercent(), 0.2f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewModelSpinBoxInteractionTest,
	"DreamGUI.ViewModel.Interaction.ANumberTypedIntoASpinBoxBoundBothWaysReachesTheViewModelWhenItIsCommitted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamViewModelSpinBoxInteractionTest, "DreamGUI.ViewModel.Interaction.ANumberTypedIntoASpinBoxBoundBothWaysReachesTheViewModelWhenItIsCommitted", "[Pointer][Text][Animated]")

/*
 * `Value <-> Player.Brightness` on a spin box, Brightness having no setter -- so the write-back writes the member and
 * announces it -- and `Percent <- Player.Brightness` on a bar. The field clicked, its text selected with Ctrl+A, 0.25
 * typed and Enter pressed: the view model's Brightness is 0.25 and the bar shows it, as an editable value bound both ways
 * in UMG reaches its view model on commit.
 */
bool FDreamViewModelSpinBoxInteractionTest::RunTest(const FString& Parameters)
{
	using namespace DreamViewModelInteractionTestLocal;
	FScopedDuiFile File(TEXT("ViewModelSpinBoxInteraction.dui"));
	TestTrue(TEXT("The file was written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_ViewModelSpinBoxInteraction"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("CanvasPanel Root {"),
		TEXT("    Native.SpinBox Amount { AnchorData.SizeDelta = (300, 40)  AnchorData.AnchoredPosition = (0, 60)  Value <-> Player.Brightness }"),
		TEXT("    Native.ProgressBar Meter { AnchorData.SizeDelta = (300, 20)  AnchorData.AnchoredPosition = (0, -60)  Percent <- Player.Brightness }"),
		TEXT("}") }));
	FScopedTextBlueprint Fixture(TEXT("BP_ViewModelSpinBoxInteraction"), File.FilePath);
	TStrongObjectPtr<UDreamTestPlayerVM> Player(NewObject<UDreamTestPlayerVM>(GetTransientPackage()));
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUserWidget* Instance = PlaceCompiled(*this, Rig, Fixture, Player.Get());
	UDreamSpinBox* SpinBox = PartNamed<UDreamSpinBox>(Instance, TEXT("Amount"));
	UDreamProgressBar* Meter = PartNamed<UDreamProgressBar>(Instance, TEXT("Meter"));
	if (!TestTrue(TEXT("The spin box, with its field, and the bar are there"), SpinBox != nullptr && SpinBox->FieldNode != nullptr && Meter != nullptr))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The spin box starts at the view model's Brightness"), SpinBox->GetValue(), Player->Brightness, 0.0001f);
	FDreamDriverRef Driver = Rig.Driver();

	// A click that does not travel is not a scrub: it gives the field the keyboard, as SSpinBox enters its text mode.
	TestTrue(TEXT("Clicking the field completes"), Driver->Find(FDreamBy::Widget(SpinBox->FieldNode.Get()))->Click());
	TestTrue(TEXT("Ctrl+A, the number and Enter complete"),
		Driver->Sequence().Key(EKeys::A, EDreamDriverModifierKeys::Ctrl).Type(TEXT("0.25")).Key(EKeys::Enter).Perform());

	TestNearlyEqual(TEXT("The typed number is the spin box's value"), SpinBox->GetValue(), 0.25f, 0.0001f);
	TestNearlyEqual(TEXT("...and the view model's Brightness"), Player->Brightness, 0.25f, 0.0001f);
	TestNearlyEqual(TEXT("...which the bar bound to the same member shows"), Meter->GetPercent(), 0.25f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamViewModelButtonRouteInteractionTest,
	"DreamGUI.ViewModel.Interaction.AClickOnAButtonRoutedToTheViewModelCallsItOnceAndTheBarBoundToWhatItChangedFollows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamViewModelButtonRouteInteractionTest, "DreamGUI.ViewModel.Interaction.AClickOnAButtonRoutedToTheViewModelCallsItOnceAndTheBarBoundToWhatItChangedFollows", "[Pointer][Animated]")

/*
 * `OnClicked += Player.Heal(25)` on a button and `Percent <- Player.GetHealthPercent()` on a bar, the player at half
 * health. Each click heals by the routed amount, once, and the bar follows the FieldNotify function the heal announces:
 * a half, three quarters, full. A command bound to a view model in UMG (an MVVM event binding) is called once a click.
 */
bool FDreamViewModelButtonRouteInteractionTest::RunTest(const FString& Parameters)
{
	using namespace DreamViewModelInteractionTestLocal;
	FScopedDuiFile File(TEXT("ViewModelButtonRouteInteraction.dui"));
	TestTrue(TEXT("The file was written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_ViewModelButtonRouteInteraction"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("CanvasPanel Root {"),
		TEXT("    Native.Button Heal { AnchorData.SizeDelta = (200, 60)  AnchorData.AnchoredPosition = (0, 60)  OnClicked += Player.Heal(25) }"),
		TEXT("    Native.ProgressBar Life { AnchorData.SizeDelta = (300, 20)  AnchorData.AnchoredPosition = (0, -60)  Percent <- Player.GetHealthPercent() }"),
		TEXT("}") }));
	FScopedTextBlueprint Fixture(TEXT("BP_ViewModelButtonRouteInteraction"), File.FilePath);
	TStrongObjectPtr<UDreamTestPlayerVM> Player(NewObject<UDreamTestPlayerVM>(GetTransientPackage()));
	// Written directly: nobody listens yet, and handing the view model over evaluates every binding once.
	Player->Health = 50.0f;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUserWidget* Instance = PlaceCompiled(*this, Rig, Fixture, Player.Get());
	UDreamButton* Heal = PartNamed<UDreamButton>(Instance, TEXT("Heal"));
	UDreamProgressBar* Life = PartNamed<UDreamProgressBar>(Instance, TEXT("Life"));
	if (!TestTrue(TEXT("The button and the bar are there"), Heal != nullptr && Life != nullptr))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The bar starts at half"), Life->GetPercent(), 0.5f, 0.0001f);
	FDreamElementRef Button = Rig.Driver()->Find(FDreamBy::Widget(Heal));

	TestTrue(TEXT("The first click completes"), Button->Click());
	TestNearlyEqual(TEXT("One click healed by the routed amount, once"), Player->Health, 75.0f, 0.001f);
	TestNearlyEqual(TEXT("...with the routed argument"), Player->LastHealAmount, 25.0f, 0.001f);
	TestNearlyEqual(TEXT("...and the bar followed: three quarters"), Life->GetPercent(), 0.75f, 0.0001f);

	TestTrue(TEXT("The second click completes"), Button->Click());
	TestNearlyEqual(TEXT("A second click healed once more"), Player->Health, 100.0f, 0.001f);
	TestNearlyEqual(TEXT("...and the bar is full"), Life->GetPercent(), 1.0f, 0.0001f);
	return true;
}

#endif
