/* Copyright Reflection Contributors 2024-2026 */

#pragma once

#include "CoreMinimal.h"
#include "Settings/Redirector.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

class UMaterial;
class USkeleton;

struct REFLECTION_API FFolderImportOptions {
	bool bSaveAndReleaseBatches = true;
	int32 BatchSize = 32;
	int32 MemoryReserveMB = 2048;
	FString DestinationPrefix;
	bool bOverrideMaterialParent = false;
	FSoftObjectPath MaterialParent;
	bool bOverrideSkeleton = false;
	FSoftObjectPath Skeleton;

	static bool NormalizePrefix(const FString& Input, FString& Output, FText& Error);
	static bool MatchesSkeletonDirectory(const FString& SourcePath);
};

/* Applies to this synchronous folder run and its dependency imports, then restores prior state. */
class REFLECTION_API FScopedFolderImportOptions {
public:
	explicit FScopedFolderImportOptions(const FFolderImportOptions& InOptions);
	~FScopedFolderImportOptions();
	FScopedFolderImportOptions(const FScopedFolderImportOptions&) = delete;
	FScopedFolderImportOptions& operator=(const FScopedFolderImportOptions&) = delete;

	static const FScopedFolderImportOptions* Get();
	bool IsValid() const { return Error.IsEmpty(); }
	const FText& GetError() const { return Error; }
	bool RedirectDestination(FString& Path, FRRedirectorPoint& Point) const;
	void ReverseDestination(FString& Path) const;
	UMaterial* GetMaterialParent() const;
	USkeleton* GetSkeleton(const FString& SourcePath) const;
	/* Animation assets use the selected skeleton throughout the folder run. */
	USkeleton* GetAnimationSkeleton() const;
	bool IsOverrideAsset(const UObject* Asset) const;

private:
	FFolderImportOptions Options;
	FText Error;
	TStrongObjectPtr<UMaterial> MaterialParent;
	TStrongObjectPtr<USkeleton> Skeleton;
	const FScopedFolderImportOptions* Previous = nullptr;
	TMap<FString, TArray<FRRedirectorPoint>> PreviousHistory;
};
