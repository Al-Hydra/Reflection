/* Copyright Reflection Contributors 2024-2026 */

#include "Settings/FolderImportOptions.h"

#include "Animation/Skeleton.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"

namespace {
	const FScopedFolderImportOptions* ActiveFolderOptions = nullptr;
}

bool FFolderImportOptions::NormalizePrefix(const FString& Input, FString& Output, FText& Error) {
	Output = Input.TrimStartAndEnd();
	Output.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Output.RemoveFromStart(TEXT("/"))) {}
	if (!Output.RemoveFromStart(TEXT("Game/"))) Output.RemoveFromStart(TEXT("Content/"));
	while (Output.RemoveFromEnd(TEXT("/"))) {}
	Error = FText::GetEmpty();
	if (Output.IsEmpty()) return true;

	FText Reason;
	if (Output.Contains(TEXT("//")) || Output.Contains(TEXT("."))
		|| !FPackageName::IsValidLongPackageName(TEXT("/Game/") + Output + TEXT("/ValidationAsset"), false, &Reason)) {
		Error = NSLOCTEXT("Reflection", "InvalidFolderPrefix", "Enter a folder such as Sun or Sun/Characters, without dots or invalid package characters.");
		return false;
	}
	return true;
}

bool FFolderImportOptions::MatchesSkeletonDirectory(const FString& SourcePath) {
	FString Path = SourcePath;
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	const int32 ContentAt = Path.Find(TEXT("/Content/"), ESearchCase::IgnoreCase);
	if (ContentAt != INDEX_NONE) Path = Path.Mid(ContentAt + 9);
	else if (!Path.RemoveFromStart(TEXT("/Game/"))) Path.RemoveFromStart(TEXT("Content/"));

	for (const TCHAR* Folder : { TEXT("BodyLower"), TEXT("BodyUpper"), TEXT("HeadGear"), TEXT("Accessories") }) {
		if (Path.StartsWith(FString(TEXT("Characters/Custom/")) + Folder + TEXT("/"), ESearchCase::IgnoreCase)) return true;
	}
	return false;
}

FScopedFolderImportOptions::FScopedFolderImportOptions(const FFolderImportOptions& InOptions)
	: Options(InOptions), MaterialParent(nullptr), Skeleton(nullptr) {
	FFolderImportOptions::NormalizePrefix(InOptions.DestinationPrefix, Options.DestinationPrefix, Error);
	if (Error.IsEmpty() && Options.bOverrideMaterialParent) {
		MaterialParent.Reset(Cast<UMaterial>(Options.MaterialParent.TryLoad()));
		if (!MaterialParent.IsValid()) Error = NSLOCTEXT("Reflection", "MissingFolderMaterial", "Select an existing parent material before importing.");
	}
	if (Error.IsEmpty() && Options.bOverrideSkeleton) {
		Skeleton.Reset(Cast<USkeleton>(Options.Skeleton.TryLoad()));
		if (!Skeleton.IsValid()) Error = NSLOCTEXT("Reflection", "MissingFolderSkeleton", "Select an existing skeleton before importing.");
	}
	Previous = ActiveFolderOptions;
	PreviousHistory = MoveTemp(FRRedirects::History);
	ActiveFolderOptions = this;
}

FScopedFolderImportOptions::~FScopedFolderImportOptions() {
	ActiveFolderOptions = Previous;
	FRRedirects::History = MoveTemp(PreviousHistory);
}

const FScopedFolderImportOptions* FScopedFolderImportOptions::Get() { return ActiveFolderOptions; }

bool FScopedFolderImportOptions::RedirectDestination(FString& Path, FRRedirectorPoint& Point) const {
	if (!IsValid() || Options.DestinationPrefix.IsEmpty() || !Path.StartsWith(TEXT("/Game/"))) return false;
	Point.From = TEXT("/Game/");
	Point.To = TEXT("/Game/") + Options.DestinationPrefix + TEXT("/");
	Path = Point.To + Path.Mid(Point.From.Len());
	return true;
}

void FScopedFolderImportOptions::ReverseDestination(FString& Path) const {
	if (!IsValid() || Options.DestinationPrefix.IsEmpty()) return;
	const FString Destination = TEXT("/Game/") + Options.DestinationPrefix + TEXT("/");
	if (Path.StartsWith(Destination)) Path = TEXT("/Game/") + Path.Mid(Destination.Len());
}

UMaterial* FScopedFolderImportOptions::GetMaterialParent() const { return MaterialParent.Get(); }

USkeleton* FScopedFolderImportOptions::GetSkeleton(const FString& SourcePath) const {
	return FFolderImportOptions::MatchesSkeletonDirectory(SourcePath) ? Skeleton.Get() : nullptr;
}

USkeleton* FScopedFolderImportOptions::GetAnimationSkeleton() const { return Skeleton.Get(); }

bool FScopedFolderImportOptions::IsOverrideAsset(const UObject* Asset) const {
	return Asset && (Asset == MaterialParent.Get() || Asset == Skeleton.Get());
}
