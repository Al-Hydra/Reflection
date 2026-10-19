/* Copyright Reflection Contributors 2024-2026 */
#include "Importers/Constructor/ImportMemory.h"

#include "AssetCompilingManager.h"
#include "Engine/Log.h"
#include "Engine/Package.h"
#include "HAL/PlatformMemory.h"
#include "Importers/Constructor/ImportIssues.h"
#include "PackageTools.h"
#include "RenderingThread.h"
#include "UObject/UObjectIterator.h"

namespace {
	thread_local FImportWorkScope* WorkScope = nullptr;
	FBulkImportMemory* BulkRun = nullptr;
}

FImportWorkScope::FImportWorkScope() : Previous(WorkScope) { WorkScope = this; }
FImportWorkScope::~FImportWorkScope() {
	WorkScope = Previous;
	for (int32 Index = Allocations.Num() - 1; Index >= 0; --Index) Allocations[Index]();
}
void FImportWorkScope::Register(TFunction<void()>&& Release) {
	if (WorkScope) WorkScope->Allocations.Add(MoveTemp(Release));
}
void FImportWorkScope::KeepSerializer(UObject* Object) {
	if (WorkScope) WorkScope->Serializers.Emplace(Object);
}
bool FImportWorkScope::IsActive() { return WorkScope != nullptr; }

FBulkImportMemory::FBulkImportMemory(bool bInEnabled, int32 InBatchSize, int32 InReserveMB)
	: bEnabled(bInEnabled), BatchSize(FMath::Clamp(InBatchSize, 1, 256)),
	ReserveBytes(uint64(FMath::Max(InReserveMB, 0)) * 1024 * 1024) {
	if (!bEnabled) return;
	check(IsInGameThread());
	Previous = BulkRun;
	BulkRun = this;
	CheckpointPhysical = FPlatformMemory::GetStats().UsedPhysical;
	for (TObjectIterator<UPackage> It; It; ++It) {
		ExistingPackages.Add(It->GetFName());
		if (It->IsDirty()) ExistingDirtyPackages.Add(It->GetFName());
	}
}

FBulkImportMemory::~FBulkImportMemory() {
	if (bEnabled) BulkRun = Previous;
}
bool FBulkImportMemory::IsActive() { return BulkRun != nullptr; }
void FBulkImportMemory::TrackPackage(UPackage* Package) {
	if (BulkRun && Package && Package != GetTransientPackage()) BulkRun->TouchedPackages.Add(Package);
}
void FBulkImportMemory::MarkFailedPackage(UPackage* Package) {
	if (BulkRun && Package) BulkRun->FailedPackages.Add(Package);
}
void FBulkImportMemory::RetainAsset(UObject* Asset) {
	if (!Asset) return;
	if (BulkRun) {
		TrackPackage(Asset->GetOutermost());
		BulkRun->Assets.Emplace(Asset);
	} else {
		Asset->AddToRoot();
	}
}
bool FBulkImportMemory::HasMemoryHeadroom() const {
	if (!bEnabled || ReserveBytes == 0) return true;
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	return Stats.AvailablePhysical >= ReserveBytes && Stats.AvailableVirtual >= ReserveBytes;
}
bool FBulkImportMemory::AfterAsset() {
	if (!bEnabled) return true;
	// Large textures/meshes reach this limit sooner than the asset-count limit.
	const bool bBatchGrew = FPlatformMemory::GetStats().UsedPhysical > CheckpointPhysical + 512ull * 1024 * 1024;
	if (++SinceCheckpoint >= BatchSize || bBatchGrew || !HasMemoryHeadroom()) return Checkpoint();
	return true;
}
bool FBulkImportMemory::Checkpoint() {
	if (!bEnabled) return true;
	check(IsInGameThread());
	check(!FImportWorkScope::IsActive());
	FAssetCompilingManager::Get().FinishAllCompilation();
	FlushAsyncLoading();
	FlushRenderingCommands();
	bool bSaved = true;
	int32 Saved = 0;
	for (const TWeakObjectPtr<UPackage>& Weak : TouchedPackages) {
		UPackage* Package = Weak.Get();
		if (!Package || !Package->IsDirty()) continue;
		if (FailedPackages.Contains(Package)) {
			bSaved = false;
			FImportIssues::ReportFor(Package->GetName(), Package->GetName(), TEXT("Package"), EImportIssue::Failed,
				TEXT("Batch stopped: a partially built package was kept unsaved in memory"));
			continue;
		}
		if (ExistingDirtyPackages.Contains(Package->GetFName())) {
			if (!ReportedProtectedPackages.Contains(Package->GetFName())) {
				ReportedProtectedPackages.Add(Package->GetFName());
				FImportIssues::ReportFor(Package->GetName(), Package->GetName(), TEXT("Package"), EImportIssue::Setting,
					TEXT("Left unsaved: this package had edits before the folder import"));
			}
			continue;
		}
		if (!SavePackageImmediately(Package)) {
			bSaved = false;
			FImportIssues::ReportFor(Package->GetName(), Package->GetName(), TEXT("Package"), EImportIssue::Failed,
				TEXT("Batch stopped: couldn't save this package; kept in memory"));
		} else {
			++Saved;
		}
	}

	// Release only our own references. Pre-existing roots and the editor's undo history stay intact.
	Assets.Reset();
	TArray<UPackage*> ToUnload;
	for (TObjectIterator<UPackage> It; It; ++It) {
		UPackage* Package = *It;
		if (ExistingPackages.Contains(Package->GetFName()) || Package->IsDirty() || Package->IsRooted()
			|| Package->HasAnyPackageFlags(PKG_ContainsMap | PKG_CompiledIn)
			|| !FPackageName::IsValidLongPackageName(Package->GetName())) continue;
		// Include the complete dependency closure loaded from disk by later batches,
		// not just the objects explicitly requested through Reflection's loader.
		ToUnload.Add(Package);
	}
	if (ToUnload.Num()) {
		UPackageTools::FUnloadPackageParams Params(ToUnload);
		Params.bResetTransBuffer = false;
		Params.bUnloadDirtyPackages = false;
		UPackageTools::UnloadPackages(Params);
		if (!Params.OutErrorMessage.IsEmpty()) UE_LOG(LogReflection, Warning, TEXT("Batch unload: %s"), *Params.OutErrorMessage.ToString());
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	FlushRenderingCommands();
	TouchedPackages.Reset();
	SinceCheckpoint = 0;
	++Checkpoints;
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	CheckpointPhysical = Stats.UsedPhysical;
	UE_LOG(LogReflection, Display, TEXT("Folder checkpoint %d: saved %d packages, offered %d for unload; process %.0f MiB, available RAM %.0f MiB"),
		Checkpoints, Saved, ToUnload.Num(), Stats.UsedPhysical / 1048576.0, Stats.AvailablePhysical / 1048576.0);
	if (!HasMemoryHeadroom()) {
		FImportIssues::Report(EImportIssue::Failed, TEXT("Folder import stopped at the memory reserve"),
			TEXT("Completed packages were saved. Close other applications or use smaller folders before continuing."));
		return false;
	}
	return bSaved;
}
