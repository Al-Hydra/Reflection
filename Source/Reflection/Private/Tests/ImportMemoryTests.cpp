/* Copyright Reflection Contributors 2024-2026 */
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && ENGINE_MAJOR_VERSION == 5
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Importers/Constructor/ImportReader.h"
#include "Importers/Constructor/ImportMemory.h"
#include "Importers/Constructor/Importer.h"
#include "Curves/CurveFloat.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Settings/FolderImportOptions.h"
#include "Settings/ReflectionSettings.h"

namespace {
	TSharedPtr<FJsonObject> CurveExport(const FString& Name) {
		auto Export = MakeShared<FJsonObject>();
		Export->SetStringField(TEXT("Name"), Name);
		Export->SetStringField(TEXT("Type"), TEXT("CurveFloat"));
		auto Properties = MakeShared<FJsonObject>();
		Properties->SetBoolField(TEXT("bIsEventCurve"), true);
		Export->SetObjectField(TEXT("Properties"), Properties);
		return Export;
	}
	FString PackageFile(const FString& Package) {
		return FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportMemorySafetyTest, "Reflection.FolderImport.MemorySafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FImportMemorySafetyTest::RunTest(const FString& Parameters) {
	if (!FParse::Param(FCommandLine::Get(), TEXT("ReflectionMemoryTests"))) { AddWarning(TEXT("Opt in with -ReflectionMemoryTests in an isolated test project.")); return true; }
	TGuardValue<TArray<FRRedirector>> Redirects(GetMutableDefault<UReflectionSettings>()->Redirectors, {});
	TGuardValue<bool> SaveGuard(GetMutableDefault<UReflectionSettings>()->AssetSettings.SaveAssets, false);
	const FString Folder = TEXT("/Game/ReflectionMemorySafety_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TStrongObjectPtr<UCurveFloat> Dirty(NewObject<UCurveFloat>(CreatePackage(*(Folder + TEXT("/Dirty"))), TEXT("Dirty"), RF_Public | RF_Standalone));
	Dirty->MarkPackageDirty();
	TStrongObjectPtr<UCurveFloat> Existing(NewObject<UCurveFloat>(CreatePackage(*(Folder + TEXT("/Existing"))), TEXT("Existing"), RF_Public | RF_Standalone));
	FImportIssues::Begin();
	{
		FBulkImportMemory Memory(true, 1, 0);
		FBulkImportMemory::RetainAsset(Dirty.Get());
		Existing->MarkPackageDirty();
		FBulkImportMemory::RetainAsset(Existing.Get());
		TestTrue(TEXT("Checkpoint succeeds with protected edits"), Memory.Checkpoint());
		TestTrue(TEXT("Pre-existing dirty package remains dirty"), Dirty->GetOutermost()->IsDirty());
		TestFalse(TEXT("Pre-existing edits are not saved"), IFileManager::Get().FileExists(*PackageFile(Folder + TEXT("/Dirty"))));
		TestTrue(TEXT("Previously clean imported package is saved"), IFileManager::Get().FileExists(*PackageFile(Folder + TEXT("/Existing"))));
		TestTrue(TEXT("Previously loaded asset stays loaded"), IsValid(Existing.Get()));

		// Save a parent in one batch, then resolve it from disk for an instance in the next.
		TWeakObjectPtr<UMaterial> Parent;
		{
			UMaterial* Material = NewObject<UMaterial>(CreatePackage(*(Folder + TEXT("/M_Parent"))), TEXT("M_Parent"), RF_Public | RF_Standalone);
			Parent = Material;
			HandleAssetCreation(Material, Material->GetOutermost(), false);
		}
		TestTrue(TEXT("Parent checkpoint succeeds"), Memory.Checkpoint());
		TestFalse(TEXT("Saved parent unloaded"), Parent.IsValid());
		{
			FImportWorkScope Work;
			auto Export = MakeShared<FJsonObject>();
			Export->SetStringField(TEXT("Name"), TEXT("MI_Test"));
			Export->SetStringField(TEXT("Type"), TEXT("MaterialInstanceConstant"));
			auto Properties = MakeShared<FJsonObject>();
			auto Reference = MakeShared<FJsonObject>();
			Reference->SetStringField(TEXT("ObjectName"), TEXT("Material'M_Parent'"));
			Reference->SetStringField(TEXT("ObjectPath"), Folder + TEXT("/M_Parent.0"));
			Properties->SetObjectField(TEXT("Parent"), Reference);
			Export->SetObjectField(TEXT("Properties"), Properties);
			IImporter* Importer = nullptr;
			IImportReader::ReadExportsAndImport({ MakeShared<FJsonValueObject>(Export) }, Folder + TEXT("/MI_Test"), Importer, true, false);
			UMaterialInstanceConstant* Instance = Importer ? Importer->GetTypedAsset<UMaterialInstanceConstant>() : nullptr;
			TestNotNull(TEXT("Instance rebuilt after parent unload"), Instance);
			TestTrue(TEXT("Parent dependency reloads correctly"), Instance && Instance->Parent && Instance->Parent->GetName() == TEXT("M_Parent"));
		}
		TestTrue(TEXT("Dependent batch saves"), Memory.Checkpoint());
		{
			TStrongObjectPtr<UMaterialInstanceConstant> Instance(LoadObjectByPath<UMaterialInstanceConstant>(Folder + TEXT("/MI_Test")));
			TestTrue(TEXT("Saved instance retains its parent"), Instance.IsValid() && Instance->Parent && Instance->Parent->GetName() == TEXT("M_Parent"));
		}
		Memory.Checkpoint();

		UCurveFloat* Failed = NewObject<UCurveFloat>(CreatePackage(*(Folder + TEXT("/Failed"))), TEXT("Failed"), RF_Public | RF_Standalone);
		Failed->MarkPackageDirty();
		FBulkImportMemory::RetainAsset(Failed);
		FBulkImportMemory::MarkFailedPackage(Failed->GetOutermost());
		TestFalse(TEXT("Partially built asset stops the batch"), Memory.Checkpoint());
		TestTrue(TEXT("Partially built asset remains in memory"), IsValid(Failed) && Failed->GetOutermost()->IsDirty());
		TestFalse(TEXT("Partial data not written"), IFileManager::Get().FileExists(*PackageFile(Folder + TEXT("/Failed"))));
		Failed->GetOutermost()->SetDirtyFlag(false);
		Failed->ClearFlags(RF_Standalone);
	}
	{
		FBulkImportMemory Memory(true, 1, MAX_int32);
		TestFalse(TEXT("Memory reserve detects insufficient headroom"), Memory.HasMemoryHeadroom());
		TestFalse(TEXT("Memory reserve stops safely after cleanup"), Memory.Checkpoint());
	}
	Dirty->GetOutermost()->SetDirtyFlag(false);
	Dirty->ClearFlags(RF_Standalone);
	Existing->ClearFlags(RF_Standalone);
	for (const TCHAR* Name : { TEXT("Existing"), TEXT("M_Parent"), TEXT("MI_Test") }) IFileManager::Get().Delete(*PackageFile(Folder + TEXT("/") + Name));
	FImportIssues::Clear();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportMemoryStressTest, "Reflection.FolderImport.MemoryStress10000",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FImportMemoryStressTest::RunTest(const FString& Parameters) {
	if (!FParse::Param(FCommandLine::Get(), TEXT("ReflectionMemoryTests"))) { AddWarning(TEXT("Opt in with -ReflectionMemoryTests in an isolated test project.")); return true; }
	TGuardValue<TArray<FRRedirector>> Redirects(GetMutableDefault<UReflectionSettings>()->Redirectors, {});
	TGuardValue<bool> SaveGuard(GetMutableDefault<UReflectionSettings>()->AssetSettings.SaveAssets, false);
	FFolderImportOptions Options;
	Options.DestinationPrefix = TEXT("ReflectionMemoryStress_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FScopedFolderImportOptions FolderOptions(Options);
	const FString Folder = TEXT("/Game/") + Options.DestinationPrefix;
	FBulkImportMemory Memory(true, 32, 0);
	TArray<TWeakObjectPtr<UObject>> BatchAssets;
	TArray<TWeakPtr<FJsonObject>> BatchJson;
	TArray<FString> Files;
	uint64 WarmMemory = 0;
	uint64 PeakMemory = 0;
	int32 Imported = 0;
	FImportIssues::Begin();
	for (int32 Index = 0; Index < 10000; ++Index) {
		const FString Name = FString::Printf(TEXT("Curve_%05d"), Index);
		{
			FImportWorkScope Work;
			auto Export = CurveExport(Name);
			// 32 KiB of source-only metadata exposes retained JSON without inflating the saved curve.
			Export->SetStringField(TEXT("SourcePayload"), FString::ChrN(16384, TEXT('x')));
			BatchJson.Add(Export);
			IImporter* Importer = nullptr;
			const bool Read = IImportReader::ReadExportsAndImport({ MakeShared<FJsonValueObject>(Export) }, TEXT("/Game/") + Name, Importer, true, false);
			UCurveFloat* Curve = Importer ? Importer->GetTypedAsset<UCurveFloat>() : nullptr;
			if (!Read || !Curve || !Curve->bIsEventCurve) { AddError(TEXT("Synthetic curve import failed")); break; }
			BatchAssets.Add(Curve);
			Files.Add(PackageFile(Folder + TEXT("/") + Name));
			++Imported;
		}
		const int32 Before = Memory.GetCheckpointCount();
		if (!Memory.AfterAsset()) { AddError(TEXT("Stress checkpoint failed")); break; }
		if (Before != Memory.GetCheckpointCount()) {
			for (const auto& Asset : BatchAssets) TestFalse(TEXT("Completed asset released"), Asset.IsValid());
			for (const auto& Json : BatchJson) TestFalse(TEXT("Source JSON released"), Json.IsValid());
			BatchAssets.Reset();
			BatchJson.Reset();
			const uint64 Used = FPlatformMemory::GetStats().UsedPhysical;
			PeakMemory = FMath::Max(PeakMemory, Used);
			if (Index == 1023) WarmMemory = Used;
		}
	}
	TestTrue(TEXT("Final partial batch saved"), Memory.Checkpoint());
	for (const auto& Asset : BatchAssets) TestFalse(TEXT("Final asset released"), Asset.IsValid());
	for (const auto& Json : BatchJson) TestFalse(TEXT("Final JSON released"), Json.IsValid());
	TestEqual(TEXT("Ten thousand actual assets imported"), Imported, 10000);
	for (const FString& File : Files) TestTrue(TEXT("Imported asset saved to disk"), IFileManager::Get().FileExists(*File));
	{
		TStrongObjectPtr<UCurveFloat> Reloaded(LoadObjectByPath<UCurveFloat>(Folder + TEXT("/Curve_09999")));
		TestTrue(TEXT("Last saved curve reloads with its data"), Reloaded.IsValid() && Reloaded->bIsEventCurve);
	}
	Memory.Checkpoint();
	const uint64 FinalMemory = FPlatformMemory::GetStats().UsedPhysical;
	AddInfo(FString::Printf(TEXT("10K stress memory: after 1024 %.1f MiB, final %.1f MiB, peak checkpoint %.1f MiB"),
		WarmMemory / 1048576.0, FinalMemory / 1048576.0, PeakMemory / 1048576.0));
	TestTrue(TEXT("Post-warmup memory growth below 256 MiB"), FinalMemory <= WarmMemory + 256ull * 1024 * 1024);
	// Delete only these individually named fixtures; never recursively remove a project folder.
	for (const FString& File : Files) IFileManager::Get().Delete(*File);
	FImportIssues::Clear();
	return true;
}
#endif
