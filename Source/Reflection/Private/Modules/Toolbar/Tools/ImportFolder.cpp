/* Copyright Reflection Contributors 2024-2026 */

#include "Modules/Toolbar/Tools/ImportFolder.h"

#include "Importers/Constructor/ImportIssues.h"
#include "Modules/Cloud/Cloud.h"
#include "Modules/Cloud/Remote.h"
#include "Modules/Toolbar/Tools/ImportFromPath.h"
#include "Modules/UI/Reflect/SReflectFolderDialog.h"
#include "Engine/EngineUtilities.h"
#include "Utilities/Dialog.h"
#include "Settings/FolderImportOptions.h"
#include "Importers/Constructor/ImportMemory.h"

bool TToolImportFolder::Execute(const FString& InitialFolder, const bool bUseClipboard, const bool bCanGoBack) {
	if (!Cloud::Status::IsOpened()) {
		SpawnPrompt("Reflect Folder", "Cloud isn't running, so there is nowhere to fetch from.");

		return false;
	}

	/* Nothing here goes through the reflect button, so this is where the project name gets fetched */
	/* Cancelled, which is the reader saying they are done waiting */
	if (!Cloud::EnsureMetadataBlocking()) {
		return false;
	}

	/* The dialog does the listing itself: a folder is however big it is, and every asset in it is
	 * a request, so what comes down is worth seeing before it runs */
	TArray<FString> Paths;
	TSet<FString> AllowedTypes;
	FFolderImportOptions Options;

	const EReflectFolderChoice Choice = SReflectFolderDialog::Open(Paths, AllowedTypes, InitialFolder, bUseClipboard, bCanGoBack, &Options);

	if (Choice == EReflectFolderChoice::Back) {
		return true;
	}

	if (Choice != EReflectFolderChoice::Reflect) {
		return false;
	}

	const FScopedFolderImportOptions ImportOptions(Options);
	if (!ImportOptions.IsValid()) {
		SpawnPrompt("Reflect Folder", ImportOptions.GetError().ToString());
		return false;
	}

	int32 Reflected = 0;
	int32 Attempted = 0;
	int32 Filtered = 0;
	bool Cancelled = false;
	bool MemoryStopped = false;
	FBulkImportMemory Memory(Options.bSaveAndReleaseBatches, Options.BatchSize, Options.MemoryReserveMB);

	FImportIssues::Begin(); {
		/* Import opens a scope of its own per asset, which is what the dialog ends up showing.
		 * This one is here to own the Cancel button for the whole run rather than for one asset. */
		const FBlockingRequestScope BlockingScope(NSLOCTEXT("Reflection", "ReflectingFolder", "Reflecting folder"));
		if (!Memory.HasMemoryHeadroom()) MemoryStopped = !Memory.Checkpoint();

		for (const FString& Path : Paths) {
			if (MemoryStopped) break;
			bool WasFiltered = false;

			{
				// Dependencies share this lifetime; nothing is freed inside their import stack.
				FImportWorkScope Work;
				if (TToolImportFromPath::Import(Path, AllowedTypes, &WasFiltered)) {
					Reflected++;
				}
			}
			MemoryStopped = !Memory.AfterAsset();

			/* A type the filter turned down never had a chance to succeed, so counting it as
			 * attempted would report the run as failing */
			if (WasFiltered) {
				Filtered++;
			} else {
				Attempted++;
			}

			/* Cancelling only kills the request that was in flight, so the run has to notice it
			 * too or it would carry on failing its way through the rest of the folder */
			if (FBlockingRequestScope::Pump()) {
				Cancelled = true;

				break;
			}
		}
		// Also saves the final partial batch when Cancel was pressed.
		if (!MemoryStopped) MemoryStopped = !Memory.Checkpoint();
	}

	const bool Successful = !Cancelled && !MemoryStopped && Reflected == Attempted;

	AppendNotification(
		FText::FromString(MemoryStopped ? "Reflect Folder Stopped" : (Cancelled ? "Reflect Folder Cancelled" : (Successful ? "Reflected Folder" : "Reflected With Failures"))),
		FText::FromString(Filtered > 0
			? FString::Printf(TEXT("%d of %d, %d filtered out"), Reflected, Attempted, Filtered)
			: FString::Printf(TEXT("%d of %d"), Reflected, Paths.Num())),
		Successful ? 2.0f : 5.0f,
		Successful ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail,
		true,
		310.0f
	);

	FImportIssues::Finish();

	return false;
}
