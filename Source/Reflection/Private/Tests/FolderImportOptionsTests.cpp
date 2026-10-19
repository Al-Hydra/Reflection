/* Copyright Reflection Contributors 2024-2026 */

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && ENGINE_MAJOR_VERSION == 5

#include "Misc/AutomationTest.h"
#include "Settings/FolderImportOptions.h"
#include "Settings/ReflectionSettings.h"
#include "Settings/Runtime.h"
#include "Utilities/AssetPaths.h"
#include "Importers/Constructor/ImportReader.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Animation/Skeleton.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolderImportPathsTest, "Reflection.FolderImport.PathsAndScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolderImportPathsTest::RunTest(const FString& Parameters) {
	TGuardValue<TArray<FRRedirector>> SettingsGuard(GetMutableDefault<UReflectionSettings>()->Redirectors, TArray<FRRedirector>());
	TGuardValue<FString> ProjectGuard(GReflectionRuntime.Profile.ProjectName, TEXT("NARUTO"));
	const auto OriginalHistory = FRRedirects::History;
	FString Prefix;
	FText Error;
	TestTrue(TEXT("Content prefix accepted"), FFolderImportOptions::NormalizePrefix(TEXT("Content\\Sun/"), Prefix, Error));
	TestEqual(TEXT("Prefix normalized"), Prefix, FString(TEXT("Sun")));
	TestFalse(TEXT("Traversal rejected"), FFolderImportOptions::NormalizePrefix(TEXT("../Other"), Prefix, Error));
	TestFalse(TEXT("Empty path segments rejected"), FFolderImportOptions::NormalizePrefix(TEXT("Sun//Other"), Prefix, Error));

	FFolderImportOptions Options;
	Options.DestinationPrefix = TEXT("Sun");
	{
		FScopedFolderImportOptions Scope(Options);
		TestTrue(TEXT("Valid prefix scope"), Scope.IsValid());
		for (int32 Repeat = 0; Repeat < 2; ++Repeat) {
			FString Asset = ToEditorPackagePath(TEXT("NARUTO/Content/Textures/T_Test"));
			TestEqual(TEXT("Dependency relocated"), Asset, FString(TEXT("/Game/Sun/Textures/T_Test")));
			FRRedirects::Reverse(Asset);
			TestEqual(TEXT("Source path recovered on repeated references"), Asset, FString(TEXT("/Game/Textures/T_Test")));
		}
		FString Folder = TEXT("/Game/Characters/Custom/BodyUpper/");
		FRRedirects::Redirect(Folder);
		FString Mesh = Folder + TEXT("SK_Test");
		FRRedirects::Reverse(Mesh);
		TestEqual(TEXT("Directory redirect reverses full mesh path"), Mesh, FString(TEXT("/Game/Characters/Custom/BodyUpper/SK_Test")));
		FString AlreadyNamedSun = TEXT("/Game/Sun/T_Test");
		FRRedirects::Redirect(AlreadyNamedSun);
		TestEqual(TEXT("Original Sun folder preserved"), AlreadyNamedSun, FString(TEXT("/Game/Sun/Sun/T_Test")));
		FRRedirects::Reverse(AlreadyNamedSun);
		TestEqual(TEXT("Only added prefix removed"), AlreadyNamedSun, FString(TEXT("/Game/Sun/T_Test")));
		FString Engine = TEXT("/Engine/EngineMaterials/DefaultMaterial");
		FRRedirects::Redirect(Engine);
		TestEqual(TEXT("Engine assets keep their path"), Engine, FString(TEXT("/Engine/EngineMaterials/DefaultMaterial")));
		{
			FFolderImportOptions Nested;
			Nested.DestinationPrefix = TEXT("Moon");
			FScopedFolderImportOptions NestedScope(Nested);
			TestEqual(TEXT("Nested scope replaces prefix"), ToEditorPackagePath(TEXT("/Game/T_Test")), FString(TEXT("/Game/Moon/T_Test")));
		}
		TestEqual(TEXT("Outer scope restored"), ToEditorPackagePath(TEXT("/Game/T_Test")), FString(TEXT("/Game/Sun/T_Test")));
	}
	TestNull(TEXT("Options do not leak into later imports"), FScopedFolderImportOptions::Get());
	TestEqual(TEXT("History restored"), FRRedirects::History.Num(), OriginalHistory.Num());
	TestEqual(TEXT("Later imports use original path"), ToEditorPackagePath(TEXT("/Game/T_Test")), FString(TEXT("/Game/T_Test")));
	FRRedirects::History = OriginalHistory;

	for (const TCHAR* Folder : { TEXT("BodyLower"), TEXT("BodyUpper"), TEXT("HeadGear"), TEXT("Accessories") }) {
		const FString Relative = FString(TEXT("Characters/Custom/")) + Folder + TEXT("/Nested/SK_Test");
		TestTrue(TEXT("Source-game skeleton scope"), FFolderImportOptions::MatchesSkeletonDirectory(TEXT("NARUTO/Content/") + Relative));
		TestTrue(TEXT("Editor skeleton scope"), FFolderImportOptions::MatchesSkeletonDirectory(TEXT("/Game/") + Relative));
		FString Windows = TEXT("Content/") + Relative;
		Windows.ReplaceInline(TEXT("/"), TEXT("\\"));
		TestTrue(TEXT("Windows skeleton scope"), FFolderImportOptions::MatchesSkeletonDirectory(Windows));
	}
	TestFalse(TEXT("Folder prefix lookalike excluded"), FFolderImportOptions::MatchesSkeletonDirectory(TEXT("/Game/Characters/Custom/BodyUpperOther/SK_Test")));
	TestFalse(TEXT("Other characters excluded"), FFolderImportOptions::MatchesSkeletonDirectory(TEXT("/Game/Characters/NPC/BodyUpper/SK_Test")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolderImportOverridesTest, "Reflection.FolderImport.MaterialAndSkeletonOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolderImportOverridesTest::RunTest(const FString& Parameters) {
	UReflectionSettings* Settings = GetMutableDefault<UReflectionSettings>();
	TGuardValue<bool> SaveGuard(Settings->AssetSettings.SaveAssets, false);
	TGuardValue<TArray<FRRedirector>> RedirectGuard(Settings->Redirectors, TArray<FRRedirector>());
	const FString TestFolder = TEXT("ReflectionFolderTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UMaterial* Parent = NewObject<UMaterial>(CreatePackage(*(TEXT("/Game/Sun/") + TestFolder + TEXT("/M_Parent"))), TEXT("M_Parent"), RF_Public | RF_Standalone);
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
	FFolderImportOptions Options;
	Options.DestinationPrefix = TEXT("Sun");
	Options.bOverrideMaterialParent = true;
	Options.MaterialParent = FSoftObjectPath(Parent);
	Options.bOverrideSkeleton = true;
	Options.Skeleton = FSoftObjectPath(Skeleton);
	FScopedFolderImportOptions Scope(Options);
	TestTrue(TEXT("Selected assets resolve"), Scope.IsValid());
	TestTrue(TEXT("Selected skeleton used inside scoped folder"), Scope.GetSkeleton(TEXT("/Game/Characters/Custom/HeadGear/Hat/SK_Hat")) == Skeleton);
	TestNull(TEXT("Selected skeleton ignored outside scoped folder"), Scope.GetSkeleton(TEXT("/Game/Characters/NPC/SK_NPC")));
	TestTrue(TEXT("Chosen parent protected"), Scope.IsOverrideAsset(Parent));

	const auto Export = MakeShared<FJsonObject>();
	Export->SetStringField(TEXT("Name"), TEXT("MI_Sample"));
	Export->SetStringField(TEXT("Type"), TEXT("MaterialInstanceConstant"));
	const auto Properties = MakeShared<FJsonObject>();
	const auto MissingParent = MakeShared<FJsonObject>();
	MissingParent->SetStringField(TEXT("ObjectName"), TEXT("Material'MissingParent'"));
	MissingParent->SetStringField(TEXT("ObjectPath"), TEXT("/Game/IntentionallyMissingParent.0"));
	Properties->SetObjectField(TEXT("Parent"), MissingParent);
	const auto Scalar = MakeShared<FJsonObject>();
	Scalar->SetStringField(TEXT("ParameterName"), TEXT("Intensity"));
	Scalar->SetNumberField(TEXT("ParameterValue"), 2.5);
	Properties->SetArrayField(TEXT("ScalarParameterValues"), { MakeShared<FJsonValueObject>(Scalar) });
	Export->SetObjectField(TEXT("Properties"), Properties);
	IImporter* Importer = nullptr;
	TestTrue(TEXT("Instance imports despite unavailable source parent"), IImportReader::ReadExportsAndImport(
		{ MakeShared<FJsonValueObject>(Export) }, TEXT("/Game/") + TestFolder + TEXT("/MI_Sample"), Importer, true, false));
	UMaterialInstanceConstant* Instance = Importer ? Importer->GetTypedAsset<UMaterialInstanceConstant>() : nullptr;
	TestNotNull(TEXT("Material instance created"), Instance);
	if (Instance) {
		TestTrue(TEXT("Chosen parent assigned"), Instance->Parent == Parent);
		TestEqual(TEXT("Instance lands below prefix"), Instance->GetOutermost()->GetName(), TEXT("/Game/Sun/") + TestFolder + TEXT("/MI_Sample"));
		TestEqual(TEXT("Scalar parameter retained"), Instance->ScalarParameterValues.Num(), 1);
		if (Instance->ScalarParameterValues.Num()) TestEqual(TEXT("Scalar value retained"), Instance->ScalarParameterValues[0].ParameterValue, 2.5f);
		Instance->GetOutermost()->SetDirtyFlag(false);
	}

	const auto ParentExport = MakeShared<FJsonObject>();
	ParentExport->SetStringField(TEXT("Name"), TEXT("M_Parent"));
	ParentExport->SetStringField(TEXT("Type"), TEXT("Material"));
	ParentExport->SetObjectField(TEXT("Properties"), MakeShared<FJsonObject>());
	IImporter* ParentImporter = nullptr;
	IImportReader::ReadExportsAndImport({ MakeShared<FJsonValueObject>(ParentExport) }, TEXT("/Game/") + TestFolder + TEXT("/M_Parent"), ParentImporter, true, false);
	TestTrue(TEXT("Chosen parent preserved when listed in folder"), ParentImporter && ParentImporter->GetAsset() == Parent);
	Parent->GetOutermost()->SetDirtyFlag(false);
	return true;
}

#endif
