/* Copyright Reflection Contributors 2024-2026 */
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UPackage;

/* Owns temporary C++ allocations for one complete synchronous import, including its
 * dependencies. Returned importers/exports are valid until this scope ends. Container
 * views share exports, so each allocation is owned once, independently of those views. */
class REFLECTION_API FImportWorkScope {
public:
	FImportWorkScope();
	~FImportWorkScope();
	FImportWorkScope(const FImportWorkScope&) = delete;
	FImportWorkScope& operator=(const FImportWorkScope&) = delete;

	template<typename T, typename... Args> static T* New(Args&&... Arguments) {
		T* Value = new T(Forward<Args>(Arguments)...);
		Register([Value] { delete Value; });
		return Value;
	}
	static void KeepSerializer(UObject* Object);
	static bool IsActive();
private:
	static void Register(TFunction<void()>&& Release);
	FImportWorkScope* Previous = nullptr;
	TArray<TFunction<void()>> Allocations;
	TArray<TStrongObjectPtr<UObject>> Serializers;
};

/* Checkpoints run only after all importers and dependency requests have returned. */
class REFLECTION_API FBulkImportMemory {
public:
	explicit FBulkImportMemory(bool bEnabled, int32 InBatchSize = 32, int32 InReserveMB = 2048);
	~FBulkImportMemory();
	FBulkImportMemory(const FBulkImportMemory&) = delete;
	FBulkImportMemory& operator=(const FBulkImportMemory&) = delete;
	static bool IsActive();
	static void TrackPackage(UPackage* Package);
	static void MarkFailedPackage(UPackage* Package);
	static void RetainAsset(UObject* Asset);
	bool AfterAsset();
	bool Checkpoint();
	bool HasMemoryHeadroom() const;
	int32 GetCheckpointCount() const { return Checkpoints; }
private:
	bool bEnabled;
	int32 BatchSize;
	uint64 ReserveBytes;
	uint64 CheckpointPhysical = 0;
	int32 SinceCheckpoint = 0;
	int32 Checkpoints = 0;
	FBulkImportMemory* Previous = nullptr;
	TSet<FName> ExistingPackages;
	TSet<FName> ExistingDirtyPackages;
	TSet<FName> ReportedProtectedPackages;
	TSet<TWeakObjectPtr<UPackage>> TouchedPackages;
	TSet<TWeakObjectPtr<UPackage>> FailedPackages;
	TArray<TStrongObjectPtr<UObject>> Assets;
};
