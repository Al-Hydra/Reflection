/* Copyright Reflection Contributors 2024-2026 */
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && ENGINE_MAJOR_VERSION == 5
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimComposite.h"
#include "Animation/BlendSpace.h"
#include "Animation/PoseAsset.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Importers/Types/Animation/AnimSequenceImporter.h"
#include "Settings/FolderImportOptions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimationSkeletonOverrideTest, "Reflection.FolderImport.AnimationSkeletonOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationSkeletonOverrideTest::RunTest(const FString& Parameters) {
	FImportWorkScope Work;
	TStrongObjectPtr<USkeleton> Selected(NewObject<USkeleton>());
	FReferenceSkeleton& Bones = const_cast<FReferenceSkeleton&>(Selected->GetReferenceSkeleton());
	{
		FReferenceSkeletonModifier Modifier(Bones, Selected.Get());
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("unused"), TEXT("unused"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hand"), TEXT("hand"), 0), FTransform::Identity);
	}
	FFolderImportOptions Options;
	Options.bOverrideSkeleton = true;
	Options.Skeleton = FSoftObjectPath(Selected.Get());
	auto Export = MakeShared<FJsonObject>();
	Export->SetStringField(TEXT("Name"), TEXT("Anim_Sample"));
	Export->SetStringField(TEXT("Type"), TEXT("AnimSequence"));
	auto Properties = MakeShared<FJsonObject>();
	auto Missing = MakeShared<FJsonObject>();
	Missing->SetStringField(TEXT("ObjectName"), TEXT("Skeleton'SourceSkeleton'"));
	Missing->SetStringField(TEXT("ObjectPath"), TEXT("/Game/IntentionallyMissingAnimationSkeleton.0"));
	Properties->SetObjectField(TEXT("Skeleton"), Missing);
	Export->SetObjectField(TEXT("Properties"), Properties);
	FUObjectExportContainer* Container = FImportWorkScope::New<FUObjectExportContainer>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueObject>(Export) });
	IAnimSequenceImporter Importer;
	Importer.Initialize(Container->Exports[0], Container);
	FImportIssues::Begin();
	{
		FScopedFolderImportOptions Scope(Options);
		TestTrue(TEXT("Animation override resolves selected skeleton"), Importer.ResolveSkeleton() == Selected.Get());
		TestNull(TEXT("Mesh directory scope stays restricted"), Scope.GetSkeleton(TEXT("/Game/Animations/Anim_Sample")));
		TestTrue(TEXT("Animations outside mesh folders use selection"), Scope.GetAnimationSkeleton() == Selected.Get());
		TArray<TStrongObjectPtr<UAnimationAsset>> Animations;
		Animations.Emplace(NewObject<UAnimSequence>());
		Animations.Emplace(NewObject<UAnimMontage>());
		Animations.Emplace(NewObject<UAnimComposite>());
		Animations.Emplace(NewObject<UBlendSpace>());
		Animations.Emplace(NewObject<UPoseAsset>());
		for (auto& Animation : Animations) {
			Importer.GetObjectSerializer()->DeserializeObjectProperties(Properties, Animation.Get());
			TestTrue(*Animation->GetClass()->GetName(), Animation->GetSkeleton() == Selected.Get());
			Importer.GetObjectSerializer()->DeserializeObjectProperties(Properties, Animation.Get());
			TestTrue(TEXT("Repeated property passes cannot restore source skeleton"), Animation->GetSkeleton() == Selected.Get());
		}
		TStrongObjectPtr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>());
		Importer.GetObjectSerializer()->DeserializeObjectProperties(MakeShared<FJsonObject>(), Mesh.Get());
		TestNull(TEXT("Generic animation override doesn't affect skeletal meshes"), Mesh->GetSkeleton());
		TestEqual(TEXT("Source skeleton wasn't downloaded or reported missing"), FImportIssues::NumIssues(), 0);

		auto Payload = MakeShared<FJsonObject>();
		Payload->SetNumberField(TEXT("numFrames"), 2);
		Payload->SetNumberField(TEXT("sequenceLength"), 1.0 / 30.0);
		auto Hand = MakeShared<FJsonObject>();
		Hand->SetStringField(TEXT("bone"), TEXT("hand"));
		TArray<TSharedPtr<FJsonValue>> Positions;
		for (double Value : { 3., 0., 0., 5., 0., 0. }) Positions.Add(MakeShared<FJsonValueNumber>(Value));
		Hand->SetArrayField(TEXT("positions"), Positions);
		auto Unknown = MakeShared<FJsonObject>();
		Unknown->SetStringField(TEXT("bone"), TEXT("missing_bone"));
		Payload->SetArrayField(TEXT("tracks"), { MakeShared<FJsonValueObject>(Hand), MakeShared<FJsonValueObject>(Unknown) });
		UAnimSequence* Sequence = CastChecked<UAnimSequence>(Animations[0].Get());
		TestEqual(TEXT("Only named bones on the selected skeleton are written"), Importer.BuildTracks(Sequence, Selected.Get(), Payload), 1);
		TArray<FName> Names;
		Sequence->GetDataModel()->GetBoneTrackNames(Names);
		TestTrue(TEXT("Tracks map by bone name rather than source index"), Names.Contains(TEXT("hand")) && !Names.Contains(TEXT("unused")));
		const FTransform LastKey = Sequence->GetDataModel()->GetBoneTrackTransform(TEXT("hand"), FFrameNumber(1));
		TestEqual(TEXT("Motion keys retained"), LastKey.GetTranslation().X, 5.0);
		TestEqual(TEXT("Missing bones reported"), FImportIssues::NumIssues(), 1);
		Payload->SetArrayField(TEXT("tracks"), { MakeShared<FJsonValueObject>(Unknown) });
		TestEqual(TEXT("Incompatible animation yields no tracks"), Importer.BuildTracks(Sequence, Selected.Get(), Payload), 0);
	}
	TestNull(TEXT("Animation override ends with folder run"), FScopedFolderImportOptions::Get());
	Options.bOverrideSkeleton = false;
	{
		FScopedFolderImportOptions Disabled(Options);
		TestNull(TEXT("Unchecked option ignores remembered skeleton"), Disabled.GetAnimationSkeleton());
		TStrongObjectPtr<UAnimMontage> Montage(NewObject<UAnimMontage>());
		Importer.GetObjectSerializer()->DeserializeObjectProperties(MakeShared<FJsonObject>(), Montage.Get());
		TestNull(TEXT("Unchecked option leaves animations unchanged"), Montage->GetSkeleton());
	}
	FImportIssues::Clear();
	return true;
}
#endif
