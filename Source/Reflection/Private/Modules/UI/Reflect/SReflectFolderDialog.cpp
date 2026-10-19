/* Copyright Reflection Contributors 2024-2026 */

#include "Modules/UI/Reflect/SReflectFolderDialog.h"

#include "Engine/Compatibility.h"

#include "Modules/Cloud/Cloud.h"
#include "Modules/Cloud/Remote.h"
#include "Utilities/ContentBrowser.h"
#include "Containers/Export.h"
#include "Utilities/Dialog.h"
#include "Modules/UI/Reflect/SReflectTypeFilterDialog.h"

#include "Interfaces/IMainFrameModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/AssetCompatibility.h"
#include "Materials/Material.h"
#include "Animation/Skeleton.h"

#define LOCTEXT_NAMESPACE "Reflection.ReflectFolder"

static FFolderImportOptions LastFolderOptions;

/* Reduces whatever was pasted to the folder it lives in. */
static FString ToFolderPath(FString Path) {
	Path.TrimStartAndEndInline();
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));

	if (FString Leaf; Path.Split(TEXT("/"), nullptr, &Leaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && Leaf.Contains(TEXT("."))) {
		Path.Split(TEXT("/"), &Path, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	}

	Path.RemoveFromEnd(TEXT("/"));

	return Path;
}

EReflectFolderChoice SReflectFolderDialog::Open(TArray<FString>& OutPaths, TSet<FString>& OutAllowedTypes, const FString& InitialFolder, const bool bUseClipboard, const bool bCanGoBack, FFolderImportOptions* OutOptions) {
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "Reflect Folder"))
		.ClientSize(FVector2D(780.0f, 680.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedPtr<SReflectFolderDialog> Dialog;

	Window->SetContent(
		SAssignNew(Dialog, SReflectFolderDialog)
		.ParentWindow(Window)
		.InitialFolder(InitialFolder)
		.UseClipboard(bUseClipboard)
		.CanGoBack(bCanGoBack)
	);

	const IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	FSlateApplication::Get().AddModalWindow(Window, MainFrameModule.GetParentWindow());

	/* Asked for before the listing is looked at: what was found is not what the caller does next
	 * when the answer is to go back */
	if (Dialog->BackRequested) {
		return EReflectFolderChoice::Back;
	}

	if (!Dialog->Accepted) {
		return EReflectFolderChoice::Cancelled;
	}

	OutPaths = Dialog->Paths;
	OutAllowedTypes = Dialog->AllowedTypes;
	if (OutOptions) *OutOptions = Dialog->Options;
	LastFolderOptions = Dialog->Options;

	return OutPaths.Num() > 0 ? EReflectFolderChoice::Reflect : EReflectFolderChoice::Cancelled;
}

void SReflectFolderDialog::Construct(const FArguments& InArgs) {
	ParentWindow = InArgs._ParentWindow;
	Options = LastFolderOptions;

	ChildSlot
	[
		SNew(SVerticalBox)

		/* Folder ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(8.0f, 6.0f))
			[
				SNew(SHorizontalBox)

				/* Fills the box, so it sits on the side the folder comes in from */
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
				[
					SNew(SButton)
					.Text(LOCTEXT("UseSelected", "Use Selected"))
					.ToolTipText(this, &SReflectFolderDialog::GetSelectedFolderTooltip)
					.IsEnabled(this, &SReflectFolderDialog::HasSelectedFolder)
					.OnClicked(this, &SReflectFolderDialog::OnUseSelectedFolderClicked)
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(FolderBox, SEditableTextBox)
					.HintText(LOCTEXT("FolderHint", "/Game/Folder"))
					.ToolTipText(LOCTEXT("FolderTooltip", "A folder of the game files, in either form: /Game/Foo or the way Cloud spells it."))
					.OnTextCommitted(this, &SReflectFolderDialog::OnFolderCommitted)
				]

				/* Next to the box it acts on */
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SButton)
					.Text(LOCTEXT("Find", "Find"))
					.ToolTipText(LOCTEXT("FindTooltip", "Ask Cloud what is under that folder"))
					.IsEnabled(this, &SReflectFolderDialog::CanFind)
					.OnClicked(this, &SReflectFolderDialog::OnFindClicked)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(10.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("PrefixLabel", "Destination prefix"))
					]
					+ SHorizontalBox::Slot().FillWidth(1)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromString(Options.DestinationPrefix))
						.HintText(LOCTEXT("PrefixHint", "Sun (leave empty for the original paths)"))
						.ToolTipText(LOCTEXT("PrefixTooltip", "Place imported game assets and their dependencies under Content/<prefix>, preserving their original folders."))
						.OnTextChanged_Lambda([this](const FText& Text) { Options.DestinationPrefix = Text.ToString(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this] { return Options.bOverrideMaterialParent ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Options.bOverrideMaterialParent = State == ECheckBoxState::Checked; })
						[
							SNew(STextBlock).Text(LOCTEXT("ParentOverrideLabel", "Override instance parent"))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UMaterial::StaticClass())
						.ObjectPath_Lambda([this] { return Options.MaterialParent.ToString(); })
						.OnObjectChanged_Lambda([this](const FAssetData& Asset) { Options.MaterialParent = FSoftObjectPath(GetAssetObjectPath(Asset)); })
						.IsEnabled_Lambda([this] { return Options.bOverrideMaterialParent; })
						.AllowClear(true).DisplayThumbnail(false)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this] { return Options.bOverrideSkeleton ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Options.bOverrideSkeleton = State == ECheckBoxState::Checked; })
						[
							SNew(STextBlock).Text(LOCTEXT("SkeletonOverrideLabel", "Override mesh/animation skeleton"))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(USkeleton::StaticClass())
						.ObjectPath_Lambda([this] { return Options.Skeleton.ToString(); })
						.OnObjectChanged_Lambda([this](const FAssetData& Asset) { Options.Skeleton = FSoftObjectPath(GetAssetObjectPath(Asset)); })
						.IsEnabled_Lambda([this] { return Options.bOverrideSkeleton; })
						.AllowClear(true).DisplayThumbnail(false)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SkeletonScope", "Animations: all folders and imported dependencies. Meshes: Characters/Custom/BodyLower, BodyUpper, HeadGear, Accessories. Assigns by bone name; does not retarget motion."))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(STextBlock).Text(this, &SReflectFolderDialog::GetOptionsStatus).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this] { return Options.bSaveAndReleaseBatches ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Options.bSaveAndReleaseBatches = State == ECheckBoxState::Checked; })
					.ToolTipText(LOCTEXT("BatchSaveTooltip", "Automatically save imported packages and release completed batches. Packages with existing unsaved edits stay unsaved; packages already loaded before the run stay loaded. Overrides the general Save Assets option for this folder run."))
					[
						SNew(STextBlock).Text(LOCTEXT("BatchSave", "Save and release assets in batches (recommended for large folders)"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(SHorizontalBox).IsEnabled_Lambda([this] { return Options.bSaveAndReleaseBatches; })
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[ SNew(STextBlock).Text(LOCTEXT("BatchSize", "Assets per batch  ")) ]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SSpinBox<int32>).MinValue(1).MaxValue(256).MinDesiredWidth(70)
						.Value_Lambda([this] { return Options.BatchSize; })
						.OnValueChanged_Lambda([this](int32 Value) { Options.BatchSize = Value; })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0, 0, 0)
					[ SNew(STextBlock).Text(LOCTEXT("MemoryReserve", "Free memory reserve (MiB)  ")) ]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SSpinBox<int32>).MinValue(512).MaxValue(32768).MinDesiredWidth(90)
						.Value_Lambda([this] { return Options.MemoryReserveMB; })
						.OnValueChanged_Lambda([this](int32 Value) { Options.MemoryReserveMB = Value; })
					]
				]
			]
		]

		/* What was found ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(FMargin(8.0f, 6.0f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(2.0f))
			[
				SAssignNew(ListView, SListView<TSharedPtr<FString>>)
				.ListItemsSource(&Rows)
				.SelectionMode(ESelectionMode::None)
				.OnGenerateRow(this, &SReflectFolderDialog::GenerateRow)
			]
		]

		/* Footer ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(8.0f, 0.0f, 8.0f, 8.0f))
		[
			SNew(SHorizontalBox)

			/* The way out that is not a way back to nothing, so it sits at the far end from the
			 * button that runs and ahead of what only qualifies it */
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				SNew(SButton)
				.Text(LOCTEXT("Back", "Back"))
				.ToolTipText(LOCTEXT("BackTooltip", "Back to the reflect window. Nothing listed here comes with it."))
				.Visibility(this, &SReflectFolderDialog::GetBackVisibility)
				.OnClicked(this, &SReflectFolderDialog::OnBackClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
			[
				SNew(SButton)
				.Text(this, &SReflectFolderDialog::GetTypesText)
				.ToolTipText(LOCTEXT("TypesTooltip", "Choose which asset types the run is allowed to build. Every type by default.\n\nThis covers what the folder listed. Assets those reference still come down with them, or what did come down would be missing pieces."))
				.OnClicked(this, &SReflectFolderDialog::OnTypesClicked)
			]

			/* Under the list it counts, on the same line as the button it qualifies */
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SReflectFolderDialog::GetStatusText)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SButton)
				.Text(this, &SReflectFolderDialog::GetReflectText)
				.IsEnabled(this, &SReflectFolderDialog::CanReflect)
				.OnClicked(this, &SReflectFolderDialog::OnReflectClicked)
			]
		]
	];

	CanGoBack = InArgs._CanGoBack;

	FString SelectedFolder = InArgs._InitialFolder;

	/* Nothing was handed in, so what was copied is the best guess at what was meant. Right-clicking
	 * a folder names one and lands in the branch above, which is what keeps the clipboard out of
	 * the one way in that already knows the answer. The Content Browser's selection is not read
	 * here either: it belongs to the button that offers it, where it can be seen before it is
	 * taken. */
	if (SelectedFolder.IsEmpty() && InArgs._UseClipboard) {
		const FString Clipboard = GetClipboard().TrimStartAndEnd();

		if (IsAssetPathLike(Clipboard)) {
			/* Reduced before it goes in the box rather than inside Find, so what is shown is
			 * the folder that gets listed */
			SelectedFolder = ToFolderPath(StripObjectOuter(Clipboard));
		}
	}

	if (!SelectedFolder.IsEmpty()) {
		FolderBox->SetText(FText::FromString(SelectedFolder));

		/* The folder is already known, and the list is the point */
		Find();
	}

	FSlateApplication::Get().SetKeyboardFocus(FolderBox);
}

/* Actions ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
void SReflectFolderDialog::OnFolderCommitted(const FText& NewText, const ETextCommit::Type CommitType) {
	/* Enter in the box is the same as pressing Find, so a folder can be pasted and listed
	 * without reaching for the mouse */
	if (CommitType == ETextCommit::OnEnter) {
		Find();
	}
}

FReply SReflectFolderDialog::OnBackClicked() {
	BackRequested = true;

	if (ParentWindow.IsValid()) {
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SReflectFolderDialog::OnFindClicked() {
	Find();

	return FReply::Handled();
}

FReply SReflectFolderDialog::OnTypesClicked() {
	SReflectTypeFilterDialog::Open(AllowedTypes);

	return FReply::Handled();
}

FReply SReflectFolderDialog::OnUseSelectedFolderClicked() {
	const FString SelectedFolder = GetSelectedContentBrowserFolder();

	if (!SelectedFolder.IsEmpty()) {
		FolderBox->SetText(FText::FromString(SelectedFolder));

		/* The folder changed under it, so what the list is showing is no longer the answer */
		Rows.Reset();
		Paths.Reset();
		Searched = false;

		ListView->RequestListRefresh();
	}

	return FReply::Handled();
}

FReply SReflectFolderDialog::OnReflectClicked() {
	if (!CanReflect()) return FReply::Handled();
	Accepted = true;

	if (ParentWindow.IsValid()) {
		ParentWindow->RequestDestroyWindow();
	}

	return FReply::Handled();
}

void SReflectFolderDialog::Find() {
	const FString Folder = ToFolderPath(FolderBox->GetText().ToString());

	Rows.Reset();
	Paths.Reset();
	Searched = false;

	if (!Folder.IsEmpty()) {
		{
			const FBlockingRequestScope BlockingScope(FText::Format(
				LOCTEXT("ListingFolder", "Listing {0}"),
				FText::FromString(Folder)
			));

			Paths = Cloud::Folder::GetPathsBlocking(Folder);
		}

		for (const FString& Path : Paths) {
			Rows.Add(MakeShared<FString>(Path));
		}

		Searched = true;
	}

	ListView->RequestListRefresh();
}

/* State ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
bool SReflectFolderDialog::CanFind() const {
	return FolderBox.IsValid() && !FolderBox->GetText().ToString().TrimStartAndEnd().IsEmpty();
}

bool SReflectFolderDialog::CanReflect() const {
	FString Prefix;
	FText Error;
	return Paths.Num() > 0 && FFolderImportOptions::NormalizePrefix(Options.DestinationPrefix, Prefix, Error)
		&& (!Options.bOverrideMaterialParent || !Options.MaterialParent.IsNull())
		&& (!Options.bOverrideSkeleton || !Options.Skeleton.IsNull());
}

FText SReflectFolderDialog::GetOptionsStatus() const {
	FString Prefix;
	FText Error;
	if (!FFolderImportOptions::NormalizePrefix(Options.DestinationPrefix, Prefix, Error)) return Error;
	if (Options.bOverrideMaterialParent && Options.MaterialParent.IsNull()) return LOCTEXT("PickParent", "Select a parent material to continue.");
	if (Options.bOverrideSkeleton && Options.Skeleton.IsNull()) return LOCTEXT("PickSkeleton", "Select a skeleton to continue.");
	return FText::FromString(Prefix.IsEmpty()
		? TEXT("Destination: Content/<original path>")
		: TEXT("Destination: Content/") + Prefix + TEXT("/<original path>"));
}

EVisibility SReflectFolderDialog::GetBackVisibility() const {
	return CanGoBack ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SReflectFolderDialog::HasSelectedFolder() const {
	return !GetSelectedContentBrowserFolder().IsEmpty();
}

FText SReflectFolderDialog::GetSelectedFolderTooltip() const {
	const FString SelectedFolder = GetSelectedContentBrowserFolder();

	if (SelectedFolder.IsEmpty()) {
		return LOCTEXT("UseSelectedNone", "Select a folder in the Content Browser to use it here.");
	}

	return FText::Format(LOCTEXT("UseSelectedFmt", "Use the folder selected in the Content Browser ({0})."), FText::FromString(SelectedFolder));
}

FText SReflectFolderDialog::GetStatusText() const {
	if (!Searched) {
		return LOCTEXT("StatusIdle", "Everything under the folder comes down, subfolders included.");
	}

	if (Paths.Num() == 0) {
		return LOCTEXT("StatusEmpty", "Cloud has nothing under that folder.");
	}

	return FText::Format(LOCTEXT("StatusFoundFmt", "{0} asset(s)"), FText::AsNumber(Paths.Num()));
}

FText SReflectFolderDialog::GetTypesText() const {
	/* The count is the only sign a filter is on once the window is shut */
	if (AllowedTypes.Num() == 0) {
		return LOCTEXT("TypesAll", "All Types");
	}

	return FText::Format(LOCTEXT("TypesSomeFmt", "{0} Types"), FText::AsNumber(AllowedTypes.Num()));
}

FText SReflectFolderDialog::GetReflectText() const {
	if (Paths.Num() == 0) {
		return LOCTEXT("Reflect", "Reflect");
	}

	return FText::Format(LOCTEXT("ReflectCountFmt", "Reflect {0}"), FText::AsNumber(Paths.Num()));
}

TSharedRef<ITableRow> SReflectFolderDialog::GenerateRow(TSharedPtr<FString> Path, const TSharedRef<STableViewBase>& OwnerTable) const {
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		[
			SNew(SBox)
			.Padding(FMargin(8.0f, 2.0f))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Path.IsValid() ? *Path : FString()))
			]
		];
}

#undef LOCTEXT_NAMESPACE
