#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && ENGINE_MAJOR_VERSION == 5
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Importers/Types/Animation/AnimSequenceImporter.h"
#include "Settings/FolderImportOptions.h"
#include "Settings/ReflectionSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationMorphCurvesTest, "Reflection.FolderImport.AnimationMorphCurves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationMorphCurvesTest::RunTest(const FString& Parameters) {
	TGuardValue<bool> SaveGuard(GetMutableDefault<UReflectionSettings>()->AssetSettings.SaveAssets, false);
	FBulkImportMemory Memory(true, 32, 0);
	FImportWorkScope Work;
	TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>());
	{
		FReferenceSkeletonModifier Modifier(const_cast<FReferenceSkeleton&>(Skeleton->GetReferenceSkeleton()), Skeleton.Get());
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
	}
	FFolderImportOptions Options;
	Options.bOverrideSkeleton = true;
	Options.Skeleton = FSoftObjectPath(Skeleton.Get());
	FScopedFolderImportOptions Scope(Options);
	FImportIssues::Begin();
	for (int32 Scenario = 0; Scenario < 4; ++Scenario) {
		const FString Name = TEXT("MorphTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		auto Export = MakeShared<FJsonObject>();
		Export->SetStringField(TEXT("Name"), Name);
		Export->SetStringField(TEXT("Type"), TEXT("AnimSequence"));
		auto Properties = MakeShared<FJsonObject>();
		Properties->SetNumberField(TEXT("NumFrames"), 1);
		Export->SetObjectField(TEXT("Properties"), Properties);
		auto Key = MakeShared<FJsonObject>();
		Key->SetStringField(TEXT("InterpMode"), TEXT("RCIM_Linear"));
		Key->SetNumberField(TEXT("ArriveTangent"), 0);
		Key->SetNumberField(TEXT("LeaveTangent"), 0);
		Key->SetNumberField(TEXT("Value"), 1);
		if (Scenario == 1) Key->SetStringField(TEXT("Time"), TEXT("NaN"));
		else Key->SetNumberField(TEXT("Time"), 0);
		auto Curve = MakeShared<FJsonObject>();
		Curve->SetStringField(TEXT("CurveName"), Scenario == 2 ? TEXT("None") : TEXT("Smile"));
		Curve->SetNumberField(TEXT("CurveTypeFlags"), 20);
		auto FloatCurve = MakeShared<FJsonObject>();
		FloatCurve->SetArrayField(TEXT("Keys"), { MakeShared<FJsonValueObject>(Key) });
		Curve->SetObjectField(TEXT("FloatCurve"), FloatCurve);
		auto Curves = MakeShared<FJsonObject>();
		Curves->SetArrayField(TEXT("FloatCurves"), Scenario == 3 ? TArray<TSharedPtr<FJsonValue>>() : TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueObject>(Curve) });
		Export->SetObjectField(TEXT("CompressedCurveData"), Curves);
		auto Container = FImportWorkScope::New<FUObjectExportContainer>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueObject>(Export) });
		IAnimSequenceImporter Importer;
		Importer.Initialize(Container->Exports[0], Container);
		Importer.SetPackage(CreatePackage(*(TEXT("/Game/") + Name)));
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("tracks"), {});
		Payload->SetNumberField(TEXT("numFrames"), 1);
		Payload->SetNumberField(TEXT("sequenceLength"), 1.0 / 30.0);
		const bool Imported = Importer.ImportPayload(Skeleton.Get(), Payload);
		TestEqual(TEXT("Named morph-only poses import; unnamed/empty data fail"), Imported, Scenario < 2);
		UAnimSequence* Sequence = Importer.GetTypedAsset<UAnimSequence>();
		TestNotNull(TEXT("Sequence constructed for data validation"), Sequence);
		if (Sequence) {
			TestTrue(TEXT("Uses selected skeleton"), Sequence->GetSkeleton() == Skeleton.Get());
			TestTrue(TEXT("Curve-only timeline initialized"), Sequence->GetPlayLength() > 0);
			if (Scenario < 2) {
				const FFloatCurve* Written = Sequence->GetDataModel()->FindFloatCurve(FAnimationCurveIdentifier(TEXT("Smile"), ERawCurveTrackTypes::RCT_Float));
				TestNotNull(TEXT("Morph curve retained without any bone tracks"), Written);
				if (Written) {
					TestEqual(TEXT("Morph value preserved"), Written->FloatCurve.Eval(0), 1.0f);
					TestTrue(TEXT("Curve time is finite"), FMath::IsFinite(Written->FloatCurve.Keys[0].Time));
					TestEqual(TEXT("Curve flags preserved"), Written->GetCurveTypeFlags(), 20);
				}
			}
			Sequence->GetOutermost()->SetDirtyFlag(false);
			Sequence->ClearFlags(RF_Standalone);
		}
	}
	bool ReportedMissingName = false;
	for (const auto& Asset : FImportIssues::GetAssets()) for (const auto& Issue : Asset->Issues) {
		ReportedMissingName |= Issue->Summary.Contains(TEXT("no exported name"));
	}
	TestTrue(TEXT("Missing morph name appears in error report"), ReportedMissingName);
	FImportIssues::Clear();
	return true;
}
#endif
