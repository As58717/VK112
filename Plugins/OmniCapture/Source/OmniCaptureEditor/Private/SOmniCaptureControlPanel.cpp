#include "SOmniCaptureControlPanel.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IDesktopPlatform.h"
#include "Templates/Casts.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "OmniCaptureEditorSettings.h"
#include "OmniCaptureSubsystem.h"
#include "OmniCaptureNVENCEncoder.h"
#include "OmniCaptureMuxer.h"
#include "PropertyEditorModule.h"
#include "Styling/CoreStyle.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Slate/SlateGameResources.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Types/SlateEnums.h"
#include "Engine/Texture2D.h"
#include "OmniCaptureTypes.h"
#include "RHI.h"


#define LOCTEXT_NAMESPACE "OmniCaptureControlPanel"

namespace
{
    using EOutputDirectoryMode = SOmniCaptureControlPanel::EOutputDirectoryMode;

    FText CodecToText(EOmniCaptureCodec Codec)
    {
        switch (Codec)
        {
        case EOmniCaptureCodec::HEVC: return LOCTEXT("CodecHEVC", "HEVC");
        case EOmniCaptureCodec::H264:
        default: return LOCTEXT("CodecH264", "H.264");
        }
    }

    FText FormatToText(EOmniCaptureColorFormat Format)
    {
        switch (Format)
        {
        case EOmniCaptureColorFormat::NV12: return LOCTEXT("FormatNV12", "NV12");
        case EOmniCaptureColorFormat::P010: return LOCTEXT("FormatP010", "P010");
        case EOmniCaptureColorFormat::BGRA:
        default: return LOCTEXT("FormatBGRA", "BGRA");
        }
    }

    FText OutputFormatToText(EOmniOutputFormat Format)
    {
        switch (Format)
        {
        case EOmniOutputFormat::NVENCHardware:
            return LOCTEXT("OutputFormatNVENC", "NVENC (MP4)");
        case EOmniOutputFormat::ImageSequence:
        default:
            return LOCTEXT("OutputFormatImageSequence", "Image Sequence");
        }
    }

    FText ProjectionToText(EOmniCaptureProjection Projection)
    {
        switch (Projection)
        {
        case EOmniCaptureProjection::Planar2D:
            return LOCTEXT("ProjectionPlanar", "Planar 2D");
        case EOmniCaptureProjection::Cylindrical:
            return LOCTEXT("ProjectionCylindrical", "Cylindrical");
        case EOmniCaptureProjection::FullDome:
            return LOCTEXT("ProjectionFullDome", "Full Dome");
        case EOmniCaptureProjection::SphericalMirror:
            return LOCTEXT("ProjectionSphericalMirror", "Spherical Mirror");
        case EOmniCaptureProjection::Fisheye:
            return LOCTEXT("ProjectionFisheye", "Fisheye");
        case EOmniCaptureProjection::Equirectangular:
        default:
            return LOCTEXT("ProjectionEquirect", "Equirectangular");
        }
    }

    FText FisheyeTypeToText(EOmniCaptureFisheyeType Type)
    {
        switch (Type)
        {
        case EOmniCaptureFisheyeType::Hemispherical:
            return LOCTEXT("FisheyeTypeHemi", "Hemispherical (180°)");
        case EOmniCaptureFisheyeType::OmniDirectional:
        default:
            return LOCTEXT("FisheyeTypeOmni", "Omni-directional (360°)");
        }
    }

    FText ImageFormatToText(EOmniCaptureImageFormat Format)
    {
        switch (Format)
        {
        case EOmniCaptureImageFormat::JPG:
            return LOCTEXT("ImageFormatJPG", "JPEG Sequence");
        case EOmniCaptureImageFormat::EXR:
            return LOCTEXT("ImageFormatEXR", "EXR Sequence");
        case EOmniCaptureImageFormat::BMP:
            return LOCTEXT("ImageFormatBMP", "BMP Sequence");
        case EOmniCaptureImageFormat::PNG:
        default:
            return LOCTEXT("ImageFormatPNG", "PNG Sequence");
        }
    }

    FText PNGBitDepthToText(EOmniCapturePNGBitDepth BitDepth)
    {
        switch (BitDepth)
        {
        case EOmniCapturePNGBitDepth::BitDepth8:
            return LOCTEXT("PNGBitDepth8", "8-bit Color");
        case EOmniCapturePNGBitDepth::BitDepth16:
            return LOCTEXT("PNGBitDepth16", "16-bit Color");
        case EOmniCapturePNGBitDepth::BitDepth32:
        default:
            return LOCTEXT("PNGBitDepth32", "32-bit Color");
        }
    }

    FText OutputDirectoryModeToText(EOutputDirectoryMode Mode)
    {
        switch (Mode)
        {
        case EOutputDirectoryMode::ProjectDefault:
            return LOCTEXT("OutputDirectoryModeProjectDefault", "Use Default Folder");
        case EOutputDirectoryMode::Custom:
        default:
            return LOCTEXT("OutputDirectoryModeCustom", "Use Custom Folder");
        }
    }

    int32 AlignDimensionUI(int32 Value, int32 Alignment)
    {
        const int32 SafeValue = FMath::Max(1, Value);
        if (Alignment <= 1)
        {
            return SafeValue;
        }

        const int32 Adjusted = ((SafeValue + Alignment - 1) / Alignment) * Alignment;
        return FMath::Max(Alignment, Adjusted);
    }

    FText CoverageToText(EOmniCaptureCoverage Coverage)
    {
        switch (Coverage)
        {
        case EOmniCaptureCoverage::HalfSphere:
            return LOCTEXT("Coverage180", "180°");
        case EOmniCaptureCoverage::FullSphere:
        default:
            return LOCTEXT("Coverage360", "360°");
        }
    }

    FText LayoutToText(const FOmniCaptureSettings& Settings)
    {
        if (Settings.Mode == EOmniCaptureMode::Stereo)
        {
            if (Settings.StereoLayout == EOmniCaptureStereoLayout::TopBottom)
            {
                return LOCTEXT("LayoutStereoTopBottom", "Stereo (Top-Bottom)");
            }
            return LOCTEXT("LayoutStereoSideBySide", "Stereo (Side-by-Side)");
        }

        return LOCTEXT("LayoutMono", "Mono");
    }

    TSharedRef<SMultiLineEditableTextBox> MakeReadOnlyTextBox(const FText& InText, bool bWrapText = false)
    {
        return SNew(SMultiLineEditableTextBox)
            .Text(InText)
            .IsReadOnly(true)
            .AlwaysShowScrollbars(false)
            .AutoWrapText(bWrapText)
            .AllowContextMenu(true)
            .ClearKeyboardFocusOnCommit(false)
            .RevertTextOnEscape(false)
            .SelectAllTextWhenFocused(false)
            .BackgroundColor(FLinearColor::Transparent)
            .ReadOnlyForegroundColor(FSlateColor::UseForeground());
    }
}

void SOmniCaptureControlPanel::Construct(const FArguments& InArgs)
{
    UOmniCaptureEditorSettings* Settings = GetMutableDefault<UOmniCaptureEditorSettings>();

    auto CreateDisplayText = [](TSharedPtr<SMultiLineEditableTextBox>& Target, const FText& DefaultText, bool bWrapText = false)
    {
        TSharedRef<SMultiLineEditableTextBox> Widget = MakeReadOnlyTextBox(DefaultText, bWrapText);
        Target = Widget;
        return Widget;
    };
    SettingsObject = Settings;

    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsArgs;
    DetailsArgs.bAllowSearch = true;
    DetailsArgs.bHideSelectionTip = true;
    DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    SettingsView = PropertyEditor.CreateDetailView(DetailsArgs);
    SettingsView->SetObject(Settings);

    WarningListView = SNew(SListView<TSharedPtr<FString>>)
        .ListItemsSource(&WarningItems)
        .OnGenerateRow(this, &SOmniCaptureControlPanel::GenerateWarningRow)
        .SelectionMode(ESelectionMode::None);

    DiagnosticListView = SNew(SListView<TSharedPtr<FDiagnosticListItem>>)
        .ListItemsSource(&DiagnosticItems)
        .OnGenerateRow(this, &SOmniCaptureControlPanel::GenerateDiagnosticRow)
        .SelectionMode(ESelectionMode::None);

    StereoLayoutOptions.Reset();
    StereoLayoutOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureStereoLayout>>(EOmniCaptureStereoLayout::SideBySide));
    StereoLayoutOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureStereoLayout>>(EOmniCaptureStereoLayout::TopBottom));

    OutputFormatOptions.Reset();
    OutputFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniOutputFormat>>(EOmniOutputFormat::NVENCHardware));
    OutputFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniOutputFormat>>(EOmniOutputFormat::ImageSequence));

    CodecOptions.Reset();
    CodecOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureCodec>>(EOmniCaptureCodec::HEVC));
    CodecOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureCodec>>(EOmniCaptureCodec::H264));

    ColorFormatOptions.Reset();
    ColorFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureColorFormat>>(EOmniCaptureColorFormat::NV12));
    ColorFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureColorFormat>>(EOmniCaptureColorFormat::P010));
    ColorFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureColorFormat>>(EOmniCaptureColorFormat::BGRA));

    ProjectionOptions.Reset();
    ProjectionOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureProjection>>(EOmniCaptureProjection::Equirectangular));
    ProjectionOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureProjection>>(EOmniCaptureProjection::Fisheye));
    ProjectionOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureProjection>>(EOmniCaptureProjection::Planar2D));
    ProjectionOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureProjection>>(EOmniCaptureProjection::Cylindrical));
    ProjectionOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureProjection>>(EOmniCaptureProjection::FullDome));
    ProjectionOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureProjection>>(EOmniCaptureProjection::SphericalMirror));

    FisheyeTypeOptions.Reset();
    FisheyeTypeOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureFisheyeType>>(EOmniCaptureFisheyeType::Hemispherical));
    FisheyeTypeOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureFisheyeType>>(EOmniCaptureFisheyeType::OmniDirectional));

    ImageFormatOptions.Reset();
    ImageFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureImageFormat>>(EOmniCaptureImageFormat::PNG));
    ImageFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureImageFormat>>(EOmniCaptureImageFormat::JPG));
    ImageFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureImageFormat>>(EOmniCaptureImageFormat::EXR));
    ImageFormatOptions.Add(MakeShared<TEnumOptionValue<EOmniCaptureImageFormat>>(EOmniCaptureImageFormat::BMP));

    PNGBitDepthOptions.Reset();
    PNGBitDepthOptions.Add(MakeShared<TEnumOptionValue<EOmniCapturePNGBitDepth>>(EOmniCapturePNGBitDepth::BitDepth8));
    PNGBitDepthOptions.Add(MakeShared<TEnumOptionValue<EOmniCapturePNGBitDepth>>(EOmniCapturePNGBitDepth::BitDepth16));
    PNGBitDepthOptions.Add(MakeShared<TEnumOptionValue<EOmniCapturePNGBitDepth>>(EOmniCapturePNGBitDepth::BitDepth32));

    OutputDirectoryModeOptions.Reset();
    OutputDirectoryModeOptions.Add(MakeShared<TEnumOptionValue<EOutputDirectoryMode>>(EOutputDirectoryMode::ProjectDefault));
    OutputDirectoryModeOptions.Add(MakeShared<TEnumOptionValue<EOutputDirectoryMode>>(EOutputDirectoryMode::Custom));

    RefreshFeatureAvailability(true);

    auto BuildSection = [](const FText& Title, const FText& Description, const TSharedRef<SWidget>& Content) -> TSharedRef<SWidget>
    {
        return SNew(SBorder)
            .Padding(8.0f)
            .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(Title)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.f, 2.f, 0.f, 6.f)
                [
                    SNew(STextBlock)
                    .Text(Description)
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    Content
                ]
            ];
    };

    TSharedRef<SWidget> OutputModeSection = BuildSection(
        LOCTEXT("OutputModeSectionTitle", "Output Modes"),
        LOCTEXT("OutputModeSectionDesc", "Switch between 360° and VR180 capture, choose mono or stereo output, and manage layout safety constraints."),
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ProjectionLabel", "Projection"))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f, 0.f, 0.f)
            [
                SAssignNew(ProjectionCombo, SComboBox<TEnumOptionPtr<EOmniCaptureProjection>>)
                .OptionsSource(&ProjectionOptions)
                .OnGenerateWidget(this, &SOmniCaptureControlPanel::GenerateProjectionOption)
                .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandleProjectionChanged)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return ProjectionToText(GetSettingsSnapshot().Projection);
                    })
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.f, 0.f, 12.f, 0.f)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SOmniCaptureControlPanel::GetVRModeCheckState, false)
                .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleVRModeChanged, false)
                .IsEnabled_Lambda([this]()
                {
                    return !GetSettingsSnapshot().IsPlanar();
                })
                .Content()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ModeFullSphere", "360° Full Sphere"))
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SCheckBox)
                .IsChecked(this, &SOmniCaptureControlPanel::GetVRModeCheckState, true)
                .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleVRModeChanged, true)
                .IsEnabled_Lambda([this]()
                {
                    return !GetSettingsSnapshot().IsPlanar();
                })
                .Content()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ModeVR180", "VR180 Hemisphere"))
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 6.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.f, 0.f, 12.f, 0.f)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SOmniCaptureControlPanel::GetStereoModeCheckState, false)
                .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleStereoModeChanged, false)
                .IsEnabled_Lambda([this]()
                {
                    return !GetSettingsSnapshot().IsPlanar();
                })
                .Content()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ModeMono", "Mono"))
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SCheckBox)
                .IsChecked(this, &SOmniCaptureControlPanel::GetStereoModeCheckState, true)
                .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleStereoModeChanged, true)
                .IsEnabled_Lambda([this]()
                {
                    return !GetSettingsSnapshot().IsPlanar();
                })
                .Content()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ModeStereo", "Stereo"))
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 6.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("StereoLayoutLabel", "Stereo Layout:"))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.f, 0.f, 0.f, 0.f)
            [
                SAssignNew(StereoLayoutCombo, SComboBox<TEnumOptionPtr<EOmniCaptureStereoLayout>>)
                .OptionsSource(&StereoLayoutOptions)
                .OnGenerateWidget(this, &SOmniCaptureControlPanel::GenerateStereoLayoutOption)
                .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandleStereoLayoutChanged)
                .IsEnabled_Lambda([this]()
                {
                    return GetSettingsSnapshot().IsStereo();
                })
                [
                    SNew(STextBlock)
                    .Text(this, &SOmniCaptureControlPanel::GetStereoLayoutDisplayText)
                ]
            ]
        ]
    );

    TSharedRef<SWidget> ResolutionSection = BuildSection(
        LOCTEXT("ResolutionSectionTitle", "Output Resolution"),
        LOCTEXT("ResolutionSectionDesc", "Configure either per-eye cube face resolution or planar output dimensions with integer scaling."),
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]()
            {
                return GetSettingsSnapshot().Projection == EOmniCaptureProjection::Equirectangular ? EVisibility::Visible : EVisibility::Collapsed;
            })
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SGridPanel)
                .FillColumn(1, 1.f)
                + SGridPanel::Slot(0, 0)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PerEyeWidthLabel", "Per-eye Width"))
                ]
                + SGridPanel::Slot(1, 0)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(512)
                    .MaxValue(this, &SOmniCaptureControlPanel::GetPerEyeDimensionMaxValue)
                    .Delta(64)
                    .Value(this, &SOmniCaptureControlPanel::GetPerEyeWidthValue)
                    .OnValueCommitted(this, &SOmniCaptureControlPanel::HandlePerEyeWidthCommitted)
                ]
                + SGridPanel::Slot(0, 7)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PerEyeHeightLabel", "Per-eye Height"))
                ]
                + SGridPanel::Slot(1, 7)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(512)
                    .MaxValue(this, &SOmniCaptureControlPanel::GetPerEyeDimensionMaxValue)
                    .Delta(64)
                    .Value(this, &SOmniCaptureControlPanel::GetPerEyeHeightValue)
                    .OnValueCommitted(this, &SOmniCaptureControlPanel::HandlePerEyeHeightCommitted)
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]()
            {
                return GetSettingsSnapshot().Projection == EOmniCaptureProjection::Fisheye ? EVisibility::Visible : EVisibility::Collapsed;
            })
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SGridPanel)
                .FillColumn(1, 1.f)
                + SGridPanel::Slot(0, 0)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("FisheyeWidthLabel", "Output Width"))
                ]
                + SGridPanel::Slot(1, 0)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(256)
                    .MaxValue(32768)
                    .Delta(64)
                    .Value(this, &SOmniCaptureControlPanel::GetFisheyeWidthValue)
                    .OnValueCommitted(this, &SOmniCaptureControlPanel::HandleFisheyeWidthCommitted)
                ]
                + SGridPanel::Slot(0, 1)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("FisheyeHeightLabel", "Output Height"))
                ]
                + SGridPanel::Slot(1, 1)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(256)
                    .MaxValue(32768)
                    .Delta(64)
                    .Value(this, &SOmniCaptureControlPanel::GetFisheyeHeightValue)
                    .OnValueCommitted(this, &SOmniCaptureControlPanel::HandleFisheyeHeightCommitted)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 6.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("FisheyeFOVLabel", "Field of View"))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(8.f, 0.f, 0.f, 0.f)
                [
                    SNew(SSpinBox<float>)
                    .MinValue(90.f)
                    .MaxValue(360.f)
                    .Delta(1.f)
                    .Value(this, &SOmniCaptureControlPanel::GetFisheyeFOVValue)
                    .OnValueCommitted(this, &SOmniCaptureControlPanel::HandleFisheyeFOVCommitted)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 6.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("FisheyeTypeLabel", "Fisheye Type"))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(8.f, 0.f, 0.f, 0.f)
                [
                    SAssignNew(FisheyeTypeCombo, SComboBox<TEnumOptionPtr<EOmniCaptureFisheyeType>>)
                    .OptionsSource(&FisheyeTypeOptions)
                    .OnGenerateWidget(this, &SOmniCaptureControlPanel::GenerateFisheyeTypeOption)
                    .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandleFisheyeTypeChanged)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return FisheyeTypeToText(GetSettingsSnapshot().FisheyeType);
                        })
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 6.f, 0.f, 0.f)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SOmniCaptureControlPanel::GetFisheyeConvertState)
                .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleFisheyeConvertChanged)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("FisheyeConvertLabel", "Convert to Equirectangular output"))
                    .AutoWrapText(true)
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]()
            {
                return GetSettingsSnapshot().Projection == EOmniCaptureProjection::Planar2D ? EVisibility::Visible : EVisibility::Collapsed;
            })
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SGridPanel)
                .FillColumn(1, 1.f)
                + SGridPanel::Slot(0, 0)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PlanarWidthLabel", "Output Width"))
                ]
                + SGridPanel::Slot(1, 0)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(320)
                    .MaxValue(16384)
                    .Delta(64)
                    .Value(this, &SOmniCaptureControlPanel::GetPlanarWidthValue)
                    .OnValueCommitted(this, &SOmniCaptureControlPanel::HandlePlanarWidthCommitted)
                ]
                + SGridPanel::Slot(0, 1)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PlanarHeightLabel", "Output Height"))
                ]
                + SGridPanel::Slot(1, 1)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(240)
                    .MaxValue(16384)
                    .Delta(64)
                    .Value(this, &SOmniCaptureControlPanel::GetPlanarHeightValue)
                    .OnValueCommitted(this, &SOmniCaptureControlPanel::HandlePlanarHeightCommitted)
                ]
                + SGridPanel::Slot(0, 2)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PlanarScaleLabel", "Integer Scale"))
                ]
                + SGridPanel::Slot(1, 2)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(1)
                    .MaxValue(16)
                    .Delta(1)
                    .Value(this, &SOmniCaptureControlPanel::GetPlanarScaleValue)
                    .OnValueCommitted(this, &SOmniCaptureControlPanel::HandlePlanarScaleCommitted)
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 6.f, 0.f, 0.f)
        [
            CreateDisplayText(DerivedPerEyeTextBlock, FText::GetEmpty())
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDisplayText(DerivedOutputTextBlock, FText::GetEmpty())
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDisplayText(DerivedFOVTextBlock, FText::GetEmpty())
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDisplayText(EncoderAlignmentTextBlock, FText::GetEmpty())
        ]
    );

    TSharedRef<SWidget> MetadataSection = BuildSection(
        LOCTEXT("ProjectionMetadataTitle", "Projection & Metadata"),
        LOCTEXT("ProjectionMetadataDesc", "Control hemispherical coverage safeguards and exported VR metadata so that players like YouTube, Quest, and VLC detect VR180 correctly."),
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(LOCTEXT("CoverageHint", "VR180 mode automatically clips the back hemisphere and enforces 180° fields of view."))
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 6.f, 0.f, 0.f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SOmniCaptureControlPanel::GetMetadataToggleState, EMetadataToggle::Manifest)
            .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleMetadataToggleChanged, EMetadataToggle::Manifest)
            .Content()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ManifestToggleLabel", "Generate capture manifest"))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SCheckBox)
            .IsChecked(this, &SOmniCaptureControlPanel::GetMetadataToggleState, EMetadataToggle::SpatialJson)
            .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleMetadataToggleChanged, EMetadataToggle::SpatialJson)
            .IsEnabled_Lambda([this]()
            {
                return IsSphericalMetadataSupported();
            })
            .ToolTipText_Lambda([this]()
            {
                return IsSphericalMetadataSupported()
                    ? LOCTEXT("SpatialJsonToggleTooltip", "Write a sidecar JSON file that advertises VR projection metadata.")
                    : LOCTEXT("SpatialJsonToggleTooltipDisabled", "Spatial metadata is only embedded for VR180/VR360 style captures.");
            })
            .Content()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("SpatialJsonToggleLabel", "Write spatial metadata JSON"))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SCheckBox)
            .IsChecked(this, &SOmniCaptureControlPanel::GetMetadataToggleState, EMetadataToggle::XMP)
            .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleMetadataToggleChanged, EMetadataToggle::XMP)
            .IsEnabled_Lambda([this]()
            {
                return IsSphericalMetadataSupported();
            })
            .ToolTipText_Lambda([this]()
            {
                return IsSphericalMetadataSupported()
                    ? LOCTEXT("XMPToggleTooltip", "Embed Google VR XMP metadata directly into generated media.")
                    : LOCTEXT("XMPToggleTooltipDisabled", "Planar and dome projections do not write spherical XMP metadata.");
            })
            .Content()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("XMPToggleLabel", "Embed Google VR XMP metadata"))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SCheckBox)
            .IsChecked(this, &SOmniCaptureControlPanel::GetMetadataToggleState, EMetadataToggle::FFmpeg)
            .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleMetadataToggleChanged, EMetadataToggle::FFmpeg)
            .IsEnabled_Lambda([this]()
            {
                return FeatureAvailability.FFmpeg.bAvailable && IsSphericalMetadataSupported();
            })
            .ToolTipText_Lambda([this]()
            {
                return GetFFmpegMetadataTooltip();
            })
            .Content()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("FFmpegMetadataToggleLabel", "Inject FFmpeg spherical metadata"))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(20.f, 2.f, 0.f, 0.f)
        [
            SNew(STextBlock)
            .Text(this, &SOmniCaptureControlPanel::GetFFmpegWarningText)
            .Visibility(this, &SOmniCaptureControlPanel::GetFFmpegWarningVisibility)
            .ColorAndOpacity(FSlateColor(FLinearColor::Yellow))
            .AutoWrapText(true)
        ]
    );

    TSharedRef<SWidget> PreviewSection = BuildSection(
        LOCTEXT("PreviewSectionTitle", "Preview & Debug"),
        LOCTEXT("PreviewSectionDesc", "Inspect live previews per eye and track field-of-view, layout, and encoder alignment in real time."),
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SUniformGridPanel)
            .SlotPadding(FMargin(4.f))
            + SUniformGridPanel::Slot(0, 0)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SOmniCaptureControlPanel::GetPreviewViewCheckState, EOmniCapturePreviewView::StereoComposite)
                .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandlePreviewViewChanged, EOmniCapturePreviewView::StereoComposite)
                .Content()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PreviewStereoComposite", "Stereo Composite"))
                ]
            ]
            + SUniformGridPanel::Slot(1, 0)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SOmniCaptureControlPanel::GetPreviewViewCheckState, EOmniCapturePreviewView::LeftEye)
                .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandlePreviewViewChanged, EOmniCapturePreviewView::LeftEye)
                .IsEnabled_Lambda([this]()
                {
                    return GetSettingsSnapshot().IsStereo();
                })
                .Content()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PreviewLeftEye", "Left Eye"))
                ]
            ]
            + SUniformGridPanel::Slot(2, 0)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SOmniCaptureControlPanel::GetPreviewViewCheckState, EOmniCapturePreviewView::RightEye)
                .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandlePreviewViewChanged, EOmniCapturePreviewView::RightEye)
                .IsEnabled_Lambda([this]()
                {
                    return GetSettingsSnapshot().IsStereo();
                })
                .Content()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PreviewRightEye", "Right Eye"))
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 6.f, 0.f, 0.f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("PreviewSafetyHint", "Preview toggles are disabled automatically while mono captures are active."))
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 6.f, 0.f, 0.f)
        [
            SNew(SBorder)
            .Padding(FMargin(4.f))
            .BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
            [
                SNew(SBox)
                .MinDesiredHeight(160.f)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SNew(SScaleBox)
                        .Stretch(EStretch::ScaleToFit)
                        [
                            SAssignNew(PreviewImageWidget, SImage)
                            .Visibility(EVisibility::Collapsed)
                            .ColorAndOpacity(FLinearColor::White)
                        ]
                    ]
                    + SOverlay::Slot()
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    [
                        SAssignNew(PreviewStatusText, STextBlock)
                        .Justification(ETextJustify::Center)
                        .AutoWrapText(true)
                        .WrapTextAt(320.f)
                        .Text(LOCTEXT("PreviewStatusInitializing", "Preview ready. Start recording to see frames here."))
                    ]
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 4.f, 0.f, 0.f)
        [
            SAssignNew(PreviewResolutionText, STextBlock)
            .Visibility(EVisibility::Collapsed)
            .Text(FText::GetEmpty())
        ]
    );

    TSharedRef<SWidget> EncodingSection = BuildSection(
        LOCTEXT("EncodingSectionTitle", "Encoding & Output"),
        LOCTEXT("EncodingSectionDesc", "Pick encoder targets and tune codec, color format, and bitrate for production-ready deliverables."),
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SGridPanel)
            .FillColumn(1, 1.f)
            + SGridPanel::Slot(0, 0)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("OutputFormatLabel", "Output Format"))
            ]
            + SGridPanel::Slot(1, 0)
            [
                SAssignNew(OutputFormatCombo, SComboBox<TEnumOptionPtr<EOmniOutputFormat>>)
                .OptionsSource(&OutputFormatOptions)
                .OnGenerateWidget_Lambda([this](TEnumOptionPtr<EOmniOutputFormat> InItem)
                {
                    const EOmniOutputFormat Value = InItem.IsValid() ? static_cast<EOmniOutputFormat>(InItem->GetValue()) : EOmniOutputFormat::ImageSequence;
                    const bool bEnabled = IsOutputFormatSelectable(Value);
                    return SNew(STextBlock)
                        .Text(OutputFormatToText(Value))
                        .IsEnabled(bEnabled)
                        .ToolTipText(GetOutputFormatTooltip(Value));
                })
                .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandleOutputFormatChanged)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return OutputFormatToText(GetSettingsSnapshot().OutputFormat);
                    })
                    .ToolTipText_Lambda([this]()
                    {
                        return GetOutputFormatTooltip(GetSettingsSnapshot().OutputFormat);
                    })
                ]
            ]
            + SGridPanel::Slot(0, 1)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ImageFormatLabel", "Image Format"))
                .Visibility_Lambda([this]()
                {
                    return GetSettingsSnapshot().OutputFormat == EOmniOutputFormat::ImageSequence ? EVisibility::Visible : EVisibility::Collapsed;
                })
            ]
            + SGridPanel::Slot(1, 1)
            [
                SAssignNew(ImageFormatCombo, SComboBox<TEnumOptionPtr<EOmniCaptureImageFormat>>)
                .OptionsSource(&ImageFormatOptions)
                .OnGenerateWidget(this, &SOmniCaptureControlPanel::GenerateImageFormatOption)
                .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandleImageFormatChanged)
                .Visibility_Lambda([this]()
                {
                    return GetSettingsSnapshot().OutputFormat == EOmniOutputFormat::ImageSequence ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return ImageFormatToText(GetSettingsSnapshot().ImageFormat);
                    })
                ]
            ]
            + SGridPanel::Slot(0, 2)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PNGBitDepthLabel", "PNG Color Depth"))
                .Visibility_Lambda([this]()
                {
                    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
                    const bool bShow = Snapshot.OutputFormat == EOmniOutputFormat::ImageSequence && Snapshot.ImageFormat == EOmniCaptureImageFormat::PNG;
                    return bShow ? EVisibility::Visible : EVisibility::Collapsed;
                })
            ]
            + SGridPanel::Slot(1, 2)
            [
                SAssignNew(PNGBitDepthCombo, SComboBox<TEnumOptionPtr<EOmniCapturePNGBitDepth>>)
                .OptionsSource(&PNGBitDepthOptions)
                .OnGenerateWidget(this, &SOmniCaptureControlPanel::GeneratePNGBitDepthOption)
                .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandlePNGBitDepthChanged)
                .Visibility_Lambda([this]()
                {
                    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
                    const bool bShow = Snapshot.OutputFormat == EOmniOutputFormat::ImageSequence && Snapshot.ImageFormat == EOmniCaptureImageFormat::PNG;
                    return bShow ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return PNGBitDepthToText(GetSettingsSnapshot().PNGBitDepth);
                    })
                ]
            ]
            + SGridPanel::Slot(0, 3)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("CodecLabel", "Codec"))
            ]
            + SGridPanel::Slot(1, 3)
            [
                SAssignNew(CodecCombo, SComboBox<TEnumOptionPtr<EOmniCaptureCodec>>)
                .OptionsSource(&CodecOptions)
                .OnGenerateWidget_Lambda([this](TEnumOptionPtr<EOmniCaptureCodec> InItem)
                {
                    const EOmniCaptureCodec Value = InItem.IsValid() ? static_cast<EOmniCaptureCodec>(InItem->GetValue()) : EOmniCaptureCodec::H264;
                    const bool bEnabled = IsCodecSelectable(Value);
                    return SNew(STextBlock)
                        .Text(CodecToText(Value))
                        .IsEnabled(bEnabled)
                        .ToolTipText(GetCodecTooltip(Value));
                })
                .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandleCodecChanged)
                .IsEnabled_Lambda([this]()
                {
                    return GetSettingsSnapshot().OutputFormat == EOmniOutputFormat::NVENCHardware && FeatureAvailability.NVENC.bAvailable;
                })
                .ToolTipText_Lambda([this]()
                {
                    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
                    if (Snapshot.OutputFormat != EOmniOutputFormat::NVENCHardware)
                    {
                        return LOCTEXT("CodecComboTooltipInactive", "Codec selection is only available for NVENC output.");
                    }
                    return FeatureAvailability.NVENC.bAvailable ? LOCTEXT("CodecComboTooltip", "Select the NVENC codec to encode with.") : FeatureAvailability.NVENC.Reason;
                })
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return CodecToText(GetSettingsSnapshot().Codec);
                    })
                    .ToolTipText_Lambda([this]()
                    {
                        return GetCodecTooltip(GetSettingsSnapshot().Codec);
                    })
                ]
            ]
            + SGridPanel::Slot(0, 4)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ColorFormatLabel", "Color Format"))
            ]
            + SGridPanel::Slot(1, 4)
            [
                SAssignNew(ColorFormatCombo, SComboBox<TEnumOptionPtr<EOmniCaptureColorFormat>>)
                .OptionsSource(&ColorFormatOptions)
                .OnGenerateWidget_Lambda([this](TEnumOptionPtr<EOmniCaptureColorFormat> InItem)
                {
                    const EOmniCaptureColorFormat Value = InItem.IsValid() ? static_cast<EOmniCaptureColorFormat>(InItem->GetValue()) : EOmniCaptureColorFormat::NV12;
                    const bool bEnabled = IsColorFormatSelectable(Value);
                    return SNew(STextBlock)
                        .Text(FormatToText(Value))
                        .IsEnabled(bEnabled)
                        .ToolTipText(GetColorFormatTooltip(Value));
                })
                .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandleColorFormatChanged)
                .IsEnabled_Lambda([this]()
                {
                    return GetSettingsSnapshot().OutputFormat == EOmniOutputFormat::NVENCHardware && FeatureAvailability.NVENC.bAvailable;
                })
                .ToolTipText_Lambda([this]()
                {
                    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
                    if (Snapshot.OutputFormat != EOmniOutputFormat::NVENCHardware)
                    {
                        return LOCTEXT("ColorFormatTooltipInactive", "Color format selection is only available for NVENC output.");
                    }
                    return FeatureAvailability.NVENC.bAvailable ? LOCTEXT("ColorFormatTooltip", "Choose the NVENC input color format.") : FeatureAvailability.NVENC.Reason;
                })
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return FormatToText(GetSettingsSnapshot().NVENCColorFormat);
                    })
                    .ToolTipText_Lambda([this]()
                    {
                        return GetColorFormatTooltip(GetSettingsSnapshot().NVENCColorFormat);
                    })
                ]
            ]
            + SGridPanel::Slot(0, 5)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("TargetBitrateLabel", "Target Bitrate (kbps)"))
            ]
            + SGridPanel::Slot(1, 5)
            [
                SNew(SSpinBox<int32>)
                .MinValue(1000)
                .MaxValue(1000000)
                .Delta(1000)
                .Value(this, &SOmniCaptureControlPanel::GetTargetBitrate)
                .OnValueCommitted(this, &SOmniCaptureControlPanel::HandleTargetBitrateCommitted)
            ]
            + SGridPanel::Slot(0, 6)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("MaxBitrateLabel", "Max Bitrate (kbps)"))
            ]
            + SGridPanel::Slot(1, 6)
            [
                SNew(SSpinBox<int32>)
                .MinValue(1000)
                .MaxValue(1500000)
                .Delta(1000)
                .Value(this, &SOmniCaptureControlPanel::GetMaxBitrate)
                .OnValueCommitted(this, &SOmniCaptureControlPanel::HandleMaxBitrateCommitted)
            ]
            + SGridPanel::Slot(0, 7)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("GOPLengthLabel", "GOP Length"))
            ]
            + SGridPanel::Slot(1, 7)
            [
                SNew(SSpinBox<int32>)
                .MinValue(1)
                .MaxValue(600)
                .Delta(1)
                .Value(this, &SOmniCaptureControlPanel::GetGOPLength)
                .OnValueCommitted(this, &SOmniCaptureControlPanel::HandleGOPCommitted)
            ]
            + SGridPanel::Slot(0, 8)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("BFramesLabel", "B-Frames"))
            ]
            + SGridPanel::Slot(1, 8)
            [
                SNew(SSpinBox<int32>)
                .MinValue(0)
                .MaxValue(8)
                .Delta(1)
                .Value(this, &SOmniCaptureControlPanel::GetBFrameCount)
                .OnValueCommitted(this, &SOmniCaptureControlPanel::HandleBFramesCommitted)
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 4.f, 0.f, 0.f)
        [
            SNew(STextBlock)
            .Text(this, &SOmniCaptureControlPanel::GetNVENCWarningText)
            .Visibility(this, &SOmniCaptureControlPanel::GetNVENCWarningVisibility)
            .ColorAndOpacity(FSlateColor(FLinearColor::Yellow))
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 4.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("NVENCRuntimeDirectoryLabel", "NVENC runtime directory"))
                .ToolTipText(LOCTEXT("NVENCRuntimeDirectoryTooltip", "Optional absolute path to the folder that contains the NVENC runtime (nvEncodeAPI64.dll)."))
            ]
            + SHorizontalBox::Slot()
            .Padding(8.f, 0.f, 0.f, 0.f)
            [
                SNew(SEditableTextBox)
                .Text_Lambda([this]()
                {
                    return GetNVENCRuntimeDirectoryText();
                })
                .HintText(LOCTEXT("NVENCRuntimeDirectoryHint", "Leave empty to use automatic discovery or system driver paths"))
                .OnTextCommitted(this, &SOmniCaptureControlPanel::HandleNVENCRuntimeDirectoryCommitted)
                .MinDesiredWidth(320.f)
                .ToolTipText(LOCTEXT("NVENCRuntimeDirectoryTextTooltip", "Provide a custom directory if the NVENC runtime is stored outside standard locations."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 4.f, 0.f, 0.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("NVENCDllOverrideLabel", "NVENC DLL override"))
                .ToolTipText(LOCTEXT("NVENCDllOverrideTooltip", "Optional absolute path to nvEncodeAPI64.dll or the folder that contains it."))
            ]
            + SHorizontalBox::Slot()
            .Padding(8.f, 0.f, 0.f, 0.f)
            [
                SNew(SEditableTextBox)
                .Text_Lambda([this]()
                {
                    return GetNVENCDllOverrideText();
                })
                .HintText(LOCTEXT("NVENCDllOverrideHint", "Leave empty to use system search paths"))
                .OnTextCommitted(this, &SOmniCaptureControlPanel::HandleNVENCDllOverrideCommitted)
                .MinDesiredWidth(320.f)
                .ToolTipText(LOCTEXT("NVENCDllOverrideTextTooltip", "Provide a custom nvEncodeAPI64.dll location if the driver installation is not in the default search paths."))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.f, 6.f, 0.f, 0.f)
        [
            SNew(SCheckBox)
            .IsChecked(this, &SOmniCaptureControlPanel::GetZeroCopyState)
            .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleZeroCopyChanged)
            .IsEnabled_Lambda([this]()
            {
                return GetSettingsSnapshot().OutputFormat == EOmniOutputFormat::NVENCHardware;
            })
            .ToolTipText_Lambda([this]()
            {
                const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
                if (Snapshot.OutputFormat != EOmniOutputFormat::NVENCHardware)
                {
                    return LOCTEXT("ZeroCopyTooltipInactive", "Zero-copy capture is only available when NVENC output is enabled.");
                }
                if (!FeatureAvailability.NVENC.bAvailable)
                {
                    return FeatureAvailability.NVENC.Reason;
                }
                return GetZeroCopyTooltip();
            })
            .Content()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ZeroCopyToggle", "Enable zero-copy NVENC"))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(20.f, 2.f, 0.f, 4.f)
        [
            SNew(STextBlock)
            .Text(this, &SOmniCaptureControlPanel::GetZeroCopyWarningText)
            .Visibility(this, &SOmniCaptureControlPanel::GetZeroCopyWarningVisibility)
            .ColorAndOpacity(FSlateColor(FLinearColor::Yellow))
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SCheckBox)
            .IsChecked(this, &SOmniCaptureControlPanel::GetConstantFrameRateState)
            .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleConstantFrameRateChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("CfrToggle", "Force constant frame rate"))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SCheckBox)
            .IsChecked(this, &SOmniCaptureControlPanel::GetFastStartState)
            .OnCheckStateChanged(this, &SOmniCaptureControlPanel::HandleFastStartChanged)
            .Content()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("FastStartToggle", "Enable fast-start MP4"))
            ]
        ]
    );

    TSharedRef<SWidget> AdvancedSection = BuildSection(
        LOCTEXT("AdvancedSectionTitle", "Advanced Settings"),
        LOCTEXT("AdvancedSectionDesc", "Full OmniCapture settings remain accessible for expert tuning."),
        SettingsView.ToSharedRef()
    );

    ChildSlot
    [
        SNew(SBorder)
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 0.f, 0.f, 8.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 0.f, 8.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("StartCapture", "Start Capture"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnStartCapture)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanStartCapture)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 0.f, 8.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("CaptureStill", "Capture Still"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnCaptureStill)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanCaptureStill)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 0.f, 8.f, 0.f)
                [
                    SNew(SButton)
                    .Text(this, &SOmniCaptureControlPanel::GetPauseButtonText)
                    .OnClicked(this, &SOmniCaptureControlPanel::OnTogglePause)
                    .IsEnabled(this, &SOmniCaptureControlPanel::IsPauseButtonEnabled)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("StopCapture", "Stop"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnStopCapture)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanStopCapture)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(8.f, 0.f, 0.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("OpenLastOutput", "Open Output"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnOpenLastOutput)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanOpenLastOutput)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                CreateDisplayText(StatusTextBlock, LOCTEXT("StatusIdle", "Status: Idle"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                CreateDisplayText(ActiveConfigTextBlock, LOCTEXT("ConfigInactive", "Codec: - | Format: - | Zero Copy: -"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                CreateDisplayText(LastStillTextBlock, LOCTEXT("LastStillInactive", "Last Still: -"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 4.f, 8.f, 0.f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("OutputDirectoryModeLabel", "Output Location"))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                [
                    SAssignNew(OutputDirectoryModeCombo, SComboBox<TEnumOptionPtr<EOutputDirectoryMode>>)
                    .OptionsSource(&OutputDirectoryModeOptions)
                    .OnGenerateWidget(this, &SOmniCaptureControlPanel::GenerateOutputDirectoryModeOption)
                    .OnSelectionChanged(this, &SOmniCaptureControlPanel::HandleOutputDirectoryModeChanged)
                    .InitiallySelectedItem(FindOutputDirectoryModeOption(GetCurrentOutputDirectoryMode()))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return OutputDirectoryModeToText(GetCurrentOutputDirectoryMode());
                        })
                        .ToolTipText_Lambda([this]()
                        {
                            return GetOutputDirectoryModeTooltip(GetCurrentOutputDirectoryMode());
                        })
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 4.f, 8.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("BrowseOutputDirectory", "Set Output Folder"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnBrowseOutputDirectory)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .VAlign(VAlign_Center)
                [
                    CreateDisplayText(OutputDirectoryTextBlock, LOCTEXT("OutputDirectoryInactive", "Output Folder: -"), true)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                CreateDisplayText(FrameRateTextBlock, LOCTEXT("FrameRateInactive", "Frame Rate: 0.00 FPS"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                CreateDisplayText(RingBufferTextBlock, LOCTEXT("RingBufferStats", "Ring Buffer: Pending 0 | Dropped 0 | Blocked 0"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                CreateDisplayText(AudioTextBlock, LOCTEXT("AudioStats", "Audio Drift: 0 ms"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 8.f)
            [
                SNew(SSeparator)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("WarningsHeader", "Environment & Warnings"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 4.f)
            [
                SNew(SBox)
                .HeightOverride(96.f)
                [
                    WarningListView.ToSharedRef()
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 8.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("DiagnosticsHeader", "Capture Diagnostics"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.f, 0.f, 4.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("CopyDiagnostics", "Copy Log"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnCopyDiagnostics)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanCopyDiagnostics)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ClearDiagnostics", "Clear Log"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnClearDiagnostics)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanClearDiagnostics)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 4.f)
            [
                SNew(SBox)
                .HeightOverride(140.f)
                [
                    DiagnosticListView.ToSharedRef()
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 8.f)
            [
                SNew(SSeparator)
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                .Padding(0.f, 0.f, 0.f, 8.f)
                [
                    OutputModeSection
                ]
                + SScrollBox::Slot()
                .Padding(0.f, 0.f, 0.f, 8.f)
                [
                    ResolutionSection
                ]
                + SScrollBox::Slot()
                .Padding(0.f, 0.f, 0.f, 8.f)
                [
                    MetadataSection
                ]
                + SScrollBox::Slot()
                .Padding(0.f, 0.f, 0.f, 8.f)
                [
                    PreviewSection
                ]
                + SScrollBox::Slot()
                .Padding(0.f, 0.f, 0.f, 8.f)
                [
                    EncodingSection
                ]
                + SScrollBox::Slot()
                [
                    AdvancedSection
                ]
            ]
        ]
    ];

    RefreshStatus();
    UpdateOutputDirectoryDisplay();
    RefreshConfigurationSummary();
    UpdatePreviewTextureDisplay();
    RefreshDiagnosticLog();
    ActiveTimerHandle = RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SOmniCaptureControlPanel::HandleActiveTimer));
}

FReply SOmniCaptureControlPanel::OnStartCapture()
{
    if (!SettingsObject.IsValid())
    {
        return FReply::Handled();
    }

    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->SetPendingRigTransform(GetEditorViewportCameraTransform());
        Subsystem->BeginCapture(SettingsObject->CaptureSettings);
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnCaptureStill()
{
    if (!SettingsObject.IsValid())
    {
        return FReply::Handled();
    }

    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->SetPendingRigTransform(GetEditorViewportCameraTransform());
        FString OutputPath;
        Subsystem->CapturePanoramaStill(SettingsObject->CaptureSettings, OutputPath);
    }

    RefreshStatus();
    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnStopCapture()
{
    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->EndCapture(true);
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnTogglePause()
{
    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        if (Subsystem->IsPaused())
        {
            if (Subsystem->CanResume())
            {
                Subsystem->ResumeCapture();
            }
        }
        else if (Subsystem->CanPause())
        {
            Subsystem->PauseCapture();
        }
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnOpenLastOutput()
{
    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        const FString OutputPath = Subsystem->GetLastFinalizedOutputPath();
        if (!OutputPath.IsEmpty() && FPaths::FileExists(OutputPath))
        {
            FPlatformProcess::LaunchFileInDefaultExternalApplication(*OutputPath);
        }
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnBrowseOutputDirectory()
{
    TrySelectCustomOutputDirectory();
    return FReply::Handled();
}

bool SOmniCaptureControlPanel::TrySelectCustomOutputDirectory()
{
    if (!SettingsObject.IsValid())
    {
        return false;
    }

    UOmniCaptureEditorSettings* Settings = SettingsObject.Get();
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        return false;
    }

    FString DefaultPath = Settings->CaptureSettings.OutputDirectory;
    if (DefaultPath.IsEmpty())
    {
        DefaultPath = FPaths::ProjectSavedDir() / TEXT("OmniCaptures");
    }
    DefaultPath = FPaths::ConvertRelativePathToFull(DefaultPath);

    const void* ParentWindowHandle = nullptr;
    if (FSlateApplication::IsInitialized())
    {
        ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    }

    FString ChosenDirectory;
    const bool bOpened = DesktopPlatform->OpenDirectoryDialog(
        const_cast<void*>(ParentWindowHandle),
        TEXT("Choose Capture Output Folder"),
        DefaultPath,
        ChosenDirectory
    );

    if (!bOpened)
    {
        return false;
    }

    const FString AbsoluteDirectory = FPaths::ConvertRelativePathToFull(ChosenDirectory);
    if (Settings && Settings->CaptureSettings.OutputDirectory.Equals(AbsoluteDirectory, ESearchCase::CaseSensitive))
    {
        UpdateOutputDirectoryDisplay();
        return true;
    }

    ModifyCaptureSettings([AbsoluteDirectory](FOmniCaptureSettings& CaptureSettings)
    {
        CaptureSettings.OutputDirectory = AbsoluteDirectory;
    });

    UpdateOutputDirectoryDisplay();
    return true;
}

bool SOmniCaptureControlPanel::CanStartCapture() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return !Subsystem->IsCapturing();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanStopCapture() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->IsCapturing();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanCaptureStill() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return !Subsystem->IsCapturing();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanPauseCapture() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->CanPause();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanResumeCapture() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->CanResume();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanOpenLastOutput() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        const FString OutputPath = Subsystem->GetLastFinalizedOutputPath();
        return Subsystem->HasFinalizedOutput() && !OutputPath.IsEmpty() && FPaths::FileExists(OutputPath);
    }
    return false;
}

FText SOmniCaptureControlPanel::GetPauseButtonText() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->IsPaused() ? LOCTEXT("ResumeCapture", "Resume") : LOCTEXT("PauseCapture", "Pause");
    }
    return LOCTEXT("PauseCapture", "Pause");
}

bool SOmniCaptureControlPanel::IsPauseButtonEnabled() const
{
    return CanPauseCapture() || CanResumeCapture();
}

UOmniCaptureSubsystem* SOmniCaptureControlPanel::GetSubsystem() const
{
    if (!GEditor)
    {
        return nullptr;
    }

    const FWorldContext& WorldContext = GEditor->GetEditorWorldContext();
    UWorld* World = WorldContext.World();
    return World ? World->GetSubsystem<UOmniCaptureSubsystem>() : nullptr;
}

FTransform SOmniCaptureControlPanel::GetEditorViewportCameraTransform() const
{
    FLevelEditorViewportClient* ViewportClient = nullptr;

    if (GEditor)
    {
        if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
        {
            for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
            {
                if (Candidate && Candidate->Viewport == ActiveViewport)
                {
                    ViewportClient = Candidate;
                    break;
                }
            }
        }
    }

    if (!ViewportClient)
    {
        ViewportClient = GCurrentLevelEditingViewportClient;
    }

    if (ViewportClient)
    {
        const FVector Location = ViewportClient->GetViewLocation();
        const FRotator Rotation = ViewportClient->GetViewRotation();
        return FTransform(Rotation, Location);
    }

    return FTransform::Identity;
}

EActiveTimerReturnType SOmniCaptureControlPanel::HandleActiveTimer(double InCurrentTime, float InDeltaTime)
{
    RefreshFeatureAvailability();
    RefreshStatus();
    UpdatePreviewTextureDisplay();
    RefreshDiagnosticLog();
    return EActiveTimerReturnType::Continue;
}

void SOmniCaptureControlPanel::RefreshStatus()
{
    if (!StatusTextBlock.IsValid())
    {
        return;
    }

    UOmniCaptureSubsystem* Subsystem = GetSubsystem();
    if (!Subsystem)
    {
        StatusTextBlock->SetText(LOCTEXT("StatusNoWorld", "Status: No active editor world"));
        ActiveConfigTextBlock->SetText(LOCTEXT("ConfigUnavailable", "Codec: - | Format: - | Zero Copy: -"));
        if (LastStillTextBlock.IsValid())
        {
            LastStillTextBlock->SetText(LOCTEXT("LastStillInactive", "Last Still: -"));
        }
        if (FrameRateTextBlock.IsValid())
        {
            FrameRateTextBlock->SetText(LOCTEXT("FrameRateInactive", "Frame Rate: 0.00 FPS"));
        }
        RingBufferTextBlock->SetText(FText::GetEmpty());
        AudioTextBlock->SetText(FText::GetEmpty());
        UpdateOutputDirectoryDisplay();
        RebuildWarningList(TArray<FString>());
        return;
    }

    StatusTextBlock->SetText(FText::FromString(Subsystem->GetStatusString()));

    const bool bCapturing = Subsystem->IsCapturing();
    const FOmniCaptureSettings& Settings = bCapturing ? Subsystem->GetActiveSettings() : (SettingsObject.IsValid() ? SettingsObject->CaptureSettings : FOmniCaptureSettings());

    const bool bNVENCConfigured = Settings.OutputFormat == EOmniOutputFormat::NVENCHardware;
    const bool bNVENCDetected = bNVENCConfigured && FeatureAvailability.NVENC.bAvailable;
    const bool bZeroCopyRequested = bNVENCConfigured && Settings.bZeroCopy;
    const bool bZeroCopyPossible = FeatureAvailability.ZeroCopy.bAvailable;
    const bool bZeroCopyActive = bNVENCDetected && bZeroCopyRequested && bZeroCopyPossible;

    const FIntPoint OutputSize = Settings.GetOutputResolution();
    const FText ProjectionText = ProjectionToText(Settings.Projection);
    const FText CoverageText = Settings.IsPlanar() ? LOCTEXT("CoverageNA", "N/A") : CoverageToText(Settings.Coverage);
    const FText LayoutText = LayoutToText(Settings);
    const FText OutputFormatText = OutputFormatToText(Settings.OutputFormat);
    const FText CodecText = bNVENCConfigured
        ? (bNVENCDetected ? CodecToText(Settings.Codec) : LOCTEXT("CodecUnavailable", "-"))
        : LOCTEXT("CodecNotApplicable", "N/A");
    const FText ColorFormatText = bNVENCConfigured
        ? (bNVENCDetected ? FormatToText(Settings.NVENCColorFormat) : LOCTEXT("ColorFormatUnavailable", "-"))
        : LOCTEXT("ColorFormatNotApplicable", "N/A");
    const FText ZeroCopyText = bNVENCConfigured
        ? (bNVENCDetected
            ? (bZeroCopyActive ? LOCTEXT("ZeroCopyYes", "Yes") : LOCTEXT("ZeroCopyNo", "No"))
            : LOCTEXT("ZeroCopyUnavailable", "-"))
        : LOCTEXT("ZeroCopyNotApplicable", "N/A");
    const FText ImageFormatText = Settings.OutputFormat == EOmniOutputFormat::ImageSequence ? ImageFormatToText(Settings.ImageFormat) : LOCTEXT("ImageFormatNotApplicable", "N/A");

    const FText ConfigText = FText::Format(LOCTEXT("ConfigFormat", "Output: {0} | Projection: {1} | Coverage: {2} | Layout: {3} | Resolution: {4}×{5} | Codec: {6} | Color: {7} | Zero Copy: {8} | Images: {9}"),
        OutputFormatText,
        ProjectionText,
        CoverageText,
        LayoutText,
        FText::AsNumber(OutputSize.X),
        FText::AsNumber(OutputSize.Y),
        CodecText,
        ColorFormatText,
        ZeroCopyText,
        ImageFormatText);
    ActiveConfigTextBlock->SetText(ConfigText);

    if (LastStillTextBlock.IsValid())
    {
        const FString LastStillPath = Subsystem->GetLastStillImagePath();
        LastStillTextBlock->SetText(LastStillPath.IsEmpty()
            ? LOCTEXT("LastStillInactive", "Last Still: -")
            : FText::Format(LOCTEXT("LastStillFormat", "Last Still: {0}"), FText::FromString(LastStillPath)));
    }

    if (FrameRateTextBlock.IsValid())
    {
        const double CurrentFps = Subsystem->GetCurrentFrameRate();
        FNumberFormattingOptions FpsFormat;
        FpsFormat.SetMinimumFractionalDigits(2);
        FpsFormat.SetMaximumFractionalDigits(2);
        const FText FrameText = FText::Format(LOCTEXT("FrameRateFormat", "Frame Rate: {0} FPS"), FText::AsNumber(CurrentFps, &FpsFormat));
        FrameRateTextBlock->SetText(FrameText);
        FrameRateTextBlock->SetForegroundColor(Subsystem->IsPaused() ? FSlateColor(FLinearColor::Gray) : FSlateColor::UseForeground());
    }

    const FOmniCaptureRingBufferStats RingStats = Subsystem->GetRingBufferStats();
    const FText RingText = FText::Format(LOCTEXT("RingStatsFormat", "Ring Buffer: Pending {0} | Dropped {1} | Blocked {2}"),
        FText::AsNumber(RingStats.PendingFrames),
        FText::AsNumber(RingStats.DroppedFrames),
        FText::AsNumber(RingStats.BlockedPushes));
    RingBufferTextBlock->SetText(RingText);

    const FOmniAudioSyncStats AudioStats = Subsystem->GetAudioSyncStats();
    const FString DriftString = FString::Printf(TEXT("%.2f"), AudioStats.DriftMilliseconds);
    const FString MaxString = FString::Printf(TEXT("%.2f"), AudioStats.MaxObservedDriftMilliseconds);
    const FText AudioText = FText::Format(LOCTEXT("AudioStatsFormat", "Audio Drift: {0} ms (Max {1} ms) Pending {2}"),
        FText::FromString(DriftString),
        FText::FromString(MaxString),
        FText::AsNumber(AudioStats.PendingPackets));
    AudioTextBlock->SetText(AudioText);
    AudioTextBlock->SetForegroundColor(AudioStats.bInError ? FSlateColor(FLinearColor::Red) : FSlateColor::UseForeground());

    UpdateOutputDirectoryDisplay();
    RebuildWarningList(Subsystem->GetActiveWarnings());
    RefreshConfigurationSummary();
}

void SOmniCaptureControlPanel::UpdatePreviewTextureDisplay()
{
    if (!PreviewImageWidget.IsValid() || !PreviewStatusText.IsValid())
    {
        return;
    }

    UOmniCaptureSubsystem* Subsystem = GetSubsystem();
    UTexture2D* SubsystemPreviewTexture = Subsystem ? Subsystem->GetPreviewTexture() : nullptr;
    const bool bHasValidTexture = SubsystemPreviewTexture && SubsystemPreviewTexture->GetSizeX() > 0 && SubsystemPreviewTexture->GetSizeY() > 0;
    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();

    if (bHasValidTexture)
    {
        const FVector2D NewSize(SubsystemPreviewTexture->GetSizeX(), SubsystemPreviewTexture->GetSizeY());
        const bool bTextureChanged = CachedPreviewTexture.Get() != SubsystemPreviewTexture || !CachedPreviewSize.Equals(NewSize, KINDA_SMALL_NUMBER);
        if (bTextureChanged || !PreviewBrush.IsValid())
        {
            CachedPreviewTexture = SubsystemPreviewTexture;
            CachedPreviewSize = NewSize;
            const FName BrushName = SubsystemPreviewTexture->GetFName();
            PreviewBrush = MakeShared<FSlateDynamicImageBrush>(BrushName, NewSize);
            PreviewBrush->SetResourceObject(SubsystemPreviewTexture);
            PreviewImageWidget->SetImage(PreviewBrush.Get());
        }

        PreviewImageWidget->SetVisibility(EVisibility::Visible);
        PreviewStatusText->SetVisibility(EVisibility::Collapsed);

        if (PreviewResolutionText.IsValid())
        {
            PreviewResolutionText->SetVisibility(EVisibility::Visible);
            const FText LayoutText = LayoutToText(Snapshot);
            FText ViewText;
            switch (Snapshot.PreviewVisualization)
            {
            case EOmniCapturePreviewView::LeftEye:
                ViewText = LOCTEXT("PreviewViewLeftEye", "Left Eye");
                break;
            case EOmniCapturePreviewView::RightEye:
                ViewText = LOCTEXT("PreviewViewRightEye", "Right Eye");
                break;
            default:
                ViewText = LOCTEXT("PreviewViewStereo", "Stereo Composite");
                break;
            }
            PreviewResolutionText->SetText(FText::Format(
                LOCTEXT("PreviewResolutionDisplay", "Preview: {0} × {1} • {2} • {3}"),
                FText::AsNumber(static_cast<int32>(NewSize.X)),
                FText::AsNumber(static_cast<int32>(NewSize.Y)),
                LayoutText,
                ViewText));
        }
    }
    else
    {
        CachedPreviewTexture.Reset();
        CachedPreviewSize = FVector2D::ZeroVector;

        if (PreviewBrush.IsValid())
        {
            PreviewBrush.Reset();
        }

        PreviewImageWidget->SetImage(nullptr);
        PreviewImageWidget->SetVisibility(EVisibility::Collapsed);

        FText StatusMessage;
        if (!Subsystem)
        {
            StatusMessage = LOCTEXT("PreviewStatusNoWorld", "Preview unavailable: no active editor world.");
        }
        else if (!Snapshot.bEnablePreviewWindow)
        {
            StatusMessage = LOCTEXT("PreviewStatusDisabled", "Preview disabled. Enable the preview window to see frames here.");
        }
        else if (!Subsystem->IsCapturing())
        {
            StatusMessage = LOCTEXT("PreviewStatusIdle", "Start a capture to see the live preview.");
        }
        else
        {
            StatusMessage = LOCTEXT("PreviewStatusPending", "Waiting for preview frame…");
        }

        PreviewStatusText->SetVisibility(EVisibility::Visible);
        PreviewStatusText->SetText(StatusMessage);

        if (PreviewResolutionText.IsValid())
        {
            PreviewResolutionText->SetText(FText::GetEmpty());
            PreviewResolutionText->SetVisibility(EVisibility::Collapsed);
        }
    }
}

void SOmniCaptureControlPanel::RebuildWarningList(const TArray<FString>& Warnings)
{
    WarningItems.Reset();
    for (const FString& Warning : Warnings)
    {
        WarningItems.Add(MakeShared<FString>(Warning));
    }

    if (WarningItems.Num() == 0)
    {
        WarningItems.Add(MakeShared<FString>(LOCTEXT("NoWarnings", "No warnings detected").ToString()));
    }

    if (WarningListView.IsValid())
    {
        WarningListView->RequestListRefresh();
    }
}

TSharedRef<ITableRow> SOmniCaptureControlPanel::GenerateWarningRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
        [
            MakeReadOnlyTextBox(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty(), true)
        ];
}

void SOmniCaptureControlPanel::RefreshDiagnosticLog()
{
    TArray<FOmniCaptureDiagnosticEntry> Entries;
    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->GetCaptureDiagnosticLog(Entries);
    }

    const int32 NewDiagnosticCount = Entries.Num();
    bHasDiagnostics = NewDiagnosticCount > 0;

    DiagnosticItems.Reset();

    if (bHasDiagnostics)
    {
        FNumberFormattingOptions SecondsFormat;
        SecondsFormat.SetMinimumFractionalDigits(2);
        SecondsFormat.SetMaximumFractionalDigits(2);

        for (const FOmniCaptureDiagnosticEntry& Entry : Entries)
        {
            TSharedPtr<FDiagnosticListItem> Item = MakeShared<FDiagnosticListItem>();

            if (Entry.Timestamp.GetTicks() > 0)
            {
                Item->Timestamp = FText::FromString(Entry.Timestamp.ToString(TEXT("%H:%M:%S")));
            }
            else
            {
                Item->Timestamp = LOCTEXT("DiagnosticsNoTimestamp", "--:--:--");
            }

            const FText RelativeSeconds = FText::AsNumber(Entry.SecondsSinceCaptureStart, &SecondsFormat);
            Item->RelativeTime = FText::Format(LOCTEXT("DiagnosticsRelativeFormat", "(+{0}s)"), RelativeSeconds);
            Item->AttemptIndex = Entry.AttemptIndex;

            const FText BaseStep = Entry.Step.IsEmpty()
                ? LOCTEXT("DiagnosticsDefaultStep", "Subsystem")
                : FText::FromString(Entry.Step);

            if (Entry.AttemptIndex > 0)
            {
                Item->Step = FText::Format(LOCTEXT("DiagnosticsAttemptStepFormat", "Attempt {0} · {1}"), Entry.AttemptIndex, BaseStep);
            }
            else
            {
                Item->Step = BaseStep;
            }
            Item->Message = Entry.Message.IsEmpty() ? LOCTEXT("DiagnosticsNoMessage", "No additional details.") : FText::FromString(Entry.Message);
            Item->Level = Entry.Level;

            DiagnosticItems.Add(Item);
        }
    }
    else
    {
        TSharedPtr<FDiagnosticListItem> Placeholder = MakeShared<FDiagnosticListItem>();
        Placeholder->bIsPlaceholder = true;
        Placeholder->Message = LOCTEXT("DiagnosticsPlaceholder", "No diagnostic messages captured yet.");
        DiagnosticItems.Add(Placeholder);
    }

    if (DiagnosticListView.IsValid())
    {
        DiagnosticListView->RequestListRefresh();
        if (NewDiagnosticCount > LastDiagnosticCount && NewDiagnosticCount > 0)
        {
            const int32 LastDiagnosticIndex = DiagnosticItems.FindLastByPredicate(
                [](const TSharedPtr<FDiagnosticListItem>& Item)
                {
                    return Item.IsValid() && !Item->bIsPlaceholder;
                });

            if (LastDiagnosticIndex != INDEX_NONE)
            {
                DiagnosticListView->RequestScrollIntoView(DiagnosticItems[LastDiagnosticIndex]);
            }
        }
    }

    LastDiagnosticCount = NewDiagnosticCount;
}

TSharedRef<ITableRow> SOmniCaptureControlPanel::GenerateDiagnosticRow(TSharedPtr<FDiagnosticListItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    check(Item.IsValid());

    if (Item->bIsPlaceholder)
    {
        return SNew(STableRow<TSharedPtr<FDiagnosticListItem>>, OwnerTable)
            [
                SNew(STextBlock)
                .Text(Item->Message)
                .Justification(ETextJustify::Center)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ];
    }

    const FSlateColor LevelColor = GetDiagnosticLevelColor(Item->Level);
    TSharedRef<SMultiLineEditableTextBox> MessageWidget = MakeReadOnlyTextBox(Item->Message, true);
    MessageWidget->SetForegroundColor(LevelColor);

    return SNew(STableRow<TSharedPtr<FDiagnosticListItem>>, OwnerTable)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.f, 0.f, 8.f, 0.f)
            [
                SNew(STextBlock)
                .Text(Item->Timestamp)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.f, 0.f, 8.f, 0.f)
            [
                SNew(STextBlock)
                .Text(Item->RelativeTime)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.f, 0.f, 8.f, 0.f)
            [
                SNew(STextBlock)
                .Text(Item->Step)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                [
                    MessageWidget
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Top)
                .Padding(4.f, 0.f, 0.f, 0.f)
                [
                    SNew(SButton)
                    .OnClicked_Lambda([Item]()
                    {
                        if (Item.IsValid())
                        {
                            const FString Combined = SOmniCaptureControlPanel::BuildDiagnosticEntryString(*Item);
                            FPlatformApplicationMisc::ClipboardCopy(*Combined);
                        }
                        return FReply::Handled();
                    })
                    .ToolTipText(LOCTEXT("CopyDiagnosticTooltip", "Copy diagnostic entry to clipboard"))
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("CopyDiagnosticButton", "Copy"))
                    ]
                ]
            ]
        ];
}

FString SOmniCaptureControlPanel::BuildDiagnosticEntryString(const FDiagnosticListItem& Item)
{
    return FString::Printf(TEXT("%s %s %s %s"),
        *Item.Timestamp.ToString(),
        *Item.RelativeTime.ToString(),
        *Item.Step.ToString(),
        *Item.Message.ToString());
}

FSlateColor SOmniCaptureControlPanel::GetDiagnosticLevelColor(EOmniCaptureDiagnosticLevel Level) const
{
    switch (Level)
    {
    case EOmniCaptureDiagnosticLevel::Error:
        return FSlateColor(FLinearColor::Red);
    case EOmniCaptureDiagnosticLevel::Warning:
        return FSlateColor(FLinearColor(0.96f, 0.75f, 0.05f));
    default:
        return FSlateColor::UseForeground();
    }
}

FReply SOmniCaptureControlPanel::OnCopyDiagnostics()
{
    if (!bHasDiagnostics)
    {
        return FReply::Handled();
    }

    TArray<FString> CombinedEntries;
    CombinedEntries.Reserve(DiagnosticItems.Num());

    for (const TSharedPtr<FDiagnosticListItem>& Item : DiagnosticItems)
    {
        if (!Item.IsValid() || Item->bIsPlaceholder)
        {
            continue;
        }

        CombinedEntries.Add(BuildDiagnosticEntryString(*Item));
    }

    if (CombinedEntries.Num() > 0)
    {
        const FString CombinedText = FString::Join(CombinedEntries, TEXT("\n"));
        FPlatformApplicationMisc::ClipboardCopy(*CombinedText);
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnClearDiagnostics()
{
    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->ClearCaptureDiagnosticLog();
    }

    LastDiagnosticCount = 0;
    RefreshDiagnosticLog();
    return FReply::Handled();
}

bool SOmniCaptureControlPanel::CanClearDiagnostics() const
{
    return bHasDiagnostics;
}

bool SOmniCaptureControlPanel::CanCopyDiagnostics() const
{
    return bHasDiagnostics;
}

void SOmniCaptureControlPanel::RefreshFeatureAvailability(bool bForceRefresh)
{
    const double Now = FPlatformTime::Seconds();
    if (!bForceRefresh && (Now - LastFeatureAvailabilityCheckTime) < 1.0)
    {
        return;
    }

    if (!bForceRefresh && IsAnyComboBoxOpen())
    {
        return;
    }

    LastFeatureAvailabilityCheckTime = Now;

    FFeatureAvailabilityState NewState;

    FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
    FOmniCaptureNVENCEncoder::SetRuntimeDirectoryOverride(Snapshot.GetEffectiveNVENCRuntimeDirectory());
    FOmniCaptureNVENCEncoder::SetDllOverridePath(Snapshot.NVENCDllPathOverride);
    const FOmniNVENCCapabilities Caps = FOmniCaptureNVENCEncoder::QueryCapabilities();

    if (Caps.bHardwareAvailable)
    {
        NewState.NVENC.bAvailable = true;
        NewState.NVENC.Reason = FText::Format(LOCTEXT("NVENCAvailableTooltip", "NVENC hardware encoder detected ({0})."), FText::FromString(Caps.AdapterName));
    }
    else
    {
        NewState.NVENC.bAvailable = false;

        FString ReasonLines = TEXT("NVENC hardware encoder unavailable.");
        if (!Caps.bDllPresent && !Caps.DllFailureReason.IsEmpty())
        {
            ReasonLines += FString::Printf(TEXT("\nDLL: %s"), *Caps.DllFailureReason);
            ReasonLines += TEXT("\nHint: Provide an NVENC DLL override path if the runtime is installed outside the default search paths.");
        }
        else if (!Caps.bApisReady && !Caps.ApiFailureReason.IsEmpty())
        {
            ReasonLines += FString::Printf(TEXT("\nAPI: %s"), *Caps.ApiFailureReason);
        }
        else if (!Caps.bSessionOpenable && !Caps.SessionFailureReason.IsEmpty())
        {
            ReasonLines += FString::Printf(TEXT("\nSession: %s"), *Caps.SessionFailureReason);
        }
        else if (!Caps.HardwareFailureReason.IsEmpty())
        {
            ReasonLines += FString::Printf(TEXT("\nDetail: %s"), *Caps.HardwareFailureReason);
        }

        NewState.NVENC.Reason = FText::FromString(ReasonLines);
    }

    NewState.NVENCHEVC.bAvailable = Caps.bSupportsHEVC;
    if (Caps.bSupportsHEVC)
    {
        NewState.NVENCHEVC.Reason = LOCTEXT("HEVCSupportedTooltip", "HEVC encoding is supported by the detected NVENC device.");
    }
    else
    {
        FString CodecReason = Caps.CodecFailureReason.IsEmpty() ? TEXT("This NVENC hardware does not support HEVC encoding.") : Caps.CodecFailureReason;
        NewState.NVENCHEVC.Reason = FText::FromString(CodecReason);
    }

    NewState.NVENCNV12.bAvailable = Caps.bSupportsNV12;
    if (Caps.bSupportsNV12)
    {
        NewState.NVENCNV12.Reason = LOCTEXT("NV12SupportedTooltip", "NV12 input format is supported by NVENC.");
    }
    else
    {
        FString NV12Reason = Caps.NV12FailureReason.IsEmpty() ? TEXT("NV12 input format is not available on this NVENC hardware.") : Caps.NV12FailureReason;
        NewState.NVENCNV12.Reason = FText::FromString(NV12Reason);
    }

    NewState.NVENCP010.bAvailable = Caps.bSupportsP010;
    if (Caps.bSupportsP010)
    {
        NewState.NVENCP010.Reason = LOCTEXT("P010SupportedTooltip", "10-bit P010 input is supported by NVENC.");
    }
    else
    {
        FString P010Reason = Caps.P010FailureReason.IsEmpty() ? TEXT("This NVENC hardware does not support 10-bit P010 input.") : Caps.P010FailureReason;
        NewState.NVENCP010.Reason = FText::FromString(P010Reason);
    }

    if (NewState.NVENC.bAvailable)
    {
        const bool bPreferNVENC = SettingsObject.IsValid() ? SettingsObject->bPreferNVENCWhenAvailable : true;
        if (bPreferNVENC)
        {
            UOmniCaptureSubsystem* Subsystem = GetSubsystem();
            const bool bCapturing = Subsystem && Subsystem->IsCapturing();
            if (!bCapturing && Snapshot.OutputFormat == EOmniOutputFormat::ImageSequence)
            {
                ApplyOutputFormat(EOmniOutputFormat::NVENCHardware);
                Snapshot = GetSettingsSnapshot();
            }
        }
    }

#if PLATFORM_WINDOWS
    const bool bSupportsZeroCopy = FOmniCaptureNVENCEncoder::SupportsZeroCopyRHI();
    NewState.ZeroCopy.bAvailable = bSupportsZeroCopy;
    NewState.ZeroCopy.Reason = bSupportsZeroCopy
        ? LOCTEXT("ZeroCopySupportedTooltip", "Zero-copy NVENC transfers are available on the current Direct3D RHI.")
        : LOCTEXT("ZeroCopyUnsupportedTooltip", "Zero-copy NVENC requires a Direct3D 11 or 12 RHI.");
#else
    NewState.ZeroCopy.bAvailable = false;
    NewState.ZeroCopy.Reason = LOCTEXT("ZeroCopyUnsupportedPlatformTooltip", "Zero-copy NVENC is not supported on this platform.");
#endif

    FString ResolvedFFmpeg;
    const bool bFFmpegAvailable = FOmniCaptureMuxer::IsFFmpegAvailable(Snapshot, &ResolvedFFmpeg);
    NewState.FFmpeg.bAvailable = bFFmpegAvailable;
    if (bFFmpegAvailable)
    {
        const FText BinaryText = ResolvedFFmpeg.IsEmpty() ? LOCTEXT("FFmpegDefaultBinary", "ffmpeg") : FText::FromString(ResolvedFFmpeg);
        NewState.FFmpeg.Reason = FText::Format(LOCTEXT("FFmpegAvailableTooltip", "FFmpeg available ({0})."), BinaryText);
    }
    else
    {
        if (ResolvedFFmpeg.IsEmpty())
        {
            NewState.FFmpeg.Reason = LOCTEXT("FFmpegUnavailableTooltip", "FFmpeg binary could not be located. Configure a valid path before enabling FFmpeg metadata.");
        }
        else
        {
            NewState.FFmpeg.Reason = FText::Format(LOCTEXT("FFmpegMissingTooltip", "FFmpeg binary was not found: {0}"), FText::FromString(ResolvedFFmpeg));
        }
    }

    auto ToggleChanged = [](const FFeatureToggleState& A, const FFeatureToggleState& B)
    {
        return A.bAvailable != B.bAvailable || !A.Reason.EqualTo(B.Reason);
    };

    const bool bChanged = ToggleChanged(NewState.NVENC, FeatureAvailability.NVENC)
        || ToggleChanged(NewState.NVENCHEVC, FeatureAvailability.NVENCHEVC)
        || ToggleChanged(NewState.NVENCNV12, FeatureAvailability.NVENCNV12)
        || ToggleChanged(NewState.NVENCP010, FeatureAvailability.NVENCP010)
        || ToggleChanged(NewState.ZeroCopy, FeatureAvailability.ZeroCopy)
        || ToggleChanged(NewState.FFmpeg, FeatureAvailability.FFmpeg);

    FeatureAvailability = NewState;

    if (bChanged)
    {
        if (OutputFormatCombo.IsValid())
        {
            OutputFormatCombo->RefreshOptions();
        }
        if (CodecCombo.IsValid())
        {
            CodecCombo->RefreshOptions();
        }
        if (ColorFormatCombo.IsValid())
        {
            ColorFormatCombo->RefreshOptions();
        }
    }
}

bool SOmniCaptureControlPanel::IsAnyComboBoxOpen() const
{
    const auto IsComboOpen = [](const auto& ComboPtr) -> bool
    {
        return ComboPtr.IsValid() && ComboPtr->IsOpen();
    };

    return IsComboOpen(OutputFormatCombo)
        || IsComboOpen(CodecCombo)
        || IsComboOpen(ColorFormatCombo)
        || IsComboOpen(ProjectionCombo)
        || IsComboOpen(FisheyeTypeCombo)
        || IsComboOpen(StereoLayoutCombo)
        || IsComboOpen(ImageFormatCombo)
        || IsComboOpen(PNGBitDepthCombo)
        || IsComboOpen(OutputDirectoryModeCombo);
}

bool SOmniCaptureControlPanel::IsOutputFormatSelectable(EOmniOutputFormat Format) const
{
    if (Format == EOmniOutputFormat::NVENCHardware)
    {
        return FeatureAvailability.NVENC.bAvailable;
    }
    return true;
}

FText SOmniCaptureControlPanel::GetOutputFormatTooltip(EOmniOutputFormat Format) const
{
    if (Format == EOmniOutputFormat::NVENCHardware && !FeatureAvailability.NVENC.bAvailable)
    {
        return FeatureAvailability.NVENC.Reason;
    }
    return LOCTEXT("OutputFormatTooltip", "Choose the capture output format.");
}

FText SOmniCaptureControlPanel::GetNVENCWarningText() const
{
    return FeatureAvailability.NVENC.bAvailable ? FText::GetEmpty() : FeatureAvailability.NVENC.Reason;
}

EVisibility SOmniCaptureControlPanel::GetNVENCWarningVisibility() const
{
    return FeatureAvailability.NVENC.bAvailable ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SOmniCaptureControlPanel::GetNVENCRuntimeDirectoryText() const
{
    return FText::FromString(GetSettingsSnapshot().GetEffectiveNVENCRuntimeDirectory());
}

void SOmniCaptureControlPanel::HandleNVENCRuntimeDirectoryCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
    FString CleanPath = NewText.ToString();
    CleanPath.TrimStartAndEndInline();

    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
    const bool bChanged = !Snapshot.GetEffectiveNVENCRuntimeDirectory().Equals(CleanPath, ESearchCase::CaseSensitive);

    if (bChanged)
    {
        ModifyCaptureSettings([CleanPath](FOmniCaptureSettings& Settings)
        {
            Settings.SetNVENCRuntimeDirectory(CleanPath);
        });
    }

    FOmniCaptureNVENCEncoder::SetRuntimeDirectoryOverride(CleanPath);
    FOmniCaptureNVENCEncoder::InvalidateCachedCapabilities();
    RefreshFeatureAvailability(true);
}

FText SOmniCaptureControlPanel::GetNVENCDllOverrideText() const
{
    return FText::FromString(GetSettingsSnapshot().NVENCDllPathOverride);
}

void SOmniCaptureControlPanel::HandleNVENCDllOverrideCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
    FString CleanPath = NewText.ToString();
    CleanPath.TrimStartAndEndInline();
    (void)CommitType;

    if (!CleanPath.IsEmpty())
    {
        CleanPath = FPaths::ConvertRelativePathToFull(CleanPath);
        FPaths::MakePlatformFilename(CleanPath);
    }

    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
    const bool bChanged = !Snapshot.NVENCDllPathOverride.Equals(CleanPath, ESearchCase::CaseSensitive);

    if (bChanged)
    {
        ModifyCaptureSettings([CleanPath](FOmniCaptureSettings& Settings)
        {
            Settings.NVENCDllPathOverride = CleanPath;
        });
    }

    FOmniCaptureNVENCEncoder::SetDllOverridePath(CleanPath);
    FOmniCaptureNVENCEncoder::InvalidateCachedCapabilities();
    RefreshFeatureAvailability(true);
}

bool SOmniCaptureControlPanel::IsCodecSelectable(EOmniCaptureCodec Codec) const
{
    if (Codec == EOmniCaptureCodec::HEVC)
    {
        return FeatureAvailability.NVENCHEVC.bAvailable;
    }
    return true;
}

FText SOmniCaptureControlPanel::GetCodecTooltip(EOmniCaptureCodec Codec) const
{
    if (Codec == EOmniCaptureCodec::HEVC && !FeatureAvailability.NVENCHEVC.bAvailable)
    {
        return FeatureAvailability.NVENCHEVC.Reason;
    }
    return LOCTEXT("CodecTooltipDefault", "Select the NVENC video codec.");
}

bool SOmniCaptureControlPanel::IsColorFormatSelectable(EOmniCaptureColorFormat Format) const
{
    switch (Format)
    {
    case EOmniCaptureColorFormat::NV12:
        return FeatureAvailability.NVENCNV12.bAvailable;
    case EOmniCaptureColorFormat::P010:
        return FeatureAvailability.NVENCP010.bAvailable;
    default:
        return true;
    }
}

FText SOmniCaptureControlPanel::GetColorFormatTooltip(EOmniCaptureColorFormat Format) const
{
    switch (Format)
    {
    case EOmniCaptureColorFormat::NV12:
        return FeatureAvailability.NVENCNV12.bAvailable ? LOCTEXT("ColorFormatNV12Tooltip", "NV12 8-bit input for NVENC.") : FeatureAvailability.NVENCNV12.Reason;
    case EOmniCaptureColorFormat::P010:
        return FeatureAvailability.NVENCP010.bAvailable ? LOCTEXT("ColorFormatP010Tooltip", "10-bit P010 input for NVENC.") : FeatureAvailability.NVENCP010.Reason;
    default:
        return LOCTEXT("ColorFormatBGRATooltip", "BGRA fallback input for NVENC.");
    }
}

FText SOmniCaptureControlPanel::GetZeroCopyTooltip() const
{
    return FeatureAvailability.ZeroCopy.bAvailable ? LOCTEXT("ZeroCopyTooltipDefault", "Avoid GPU to NVENC copies by enabling zero-copy transfers.") : FeatureAvailability.ZeroCopy.Reason;
}

FText SOmniCaptureControlPanel::GetZeroCopyWarningText() const
{
    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
    if (Snapshot.OutputFormat != EOmniOutputFormat::NVENCHardware)
    {
        return FText::GetEmpty();
    }
    return FeatureAvailability.ZeroCopy.bAvailable ? FText::GetEmpty() : FeatureAvailability.ZeroCopy.Reason;
}

EVisibility SOmniCaptureControlPanel::GetZeroCopyWarningVisibility() const
{
    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
    if (Snapshot.OutputFormat != EOmniOutputFormat::NVENCHardware)
    {
        return EVisibility::Collapsed;
    }
    return FeatureAvailability.ZeroCopy.bAvailable ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SOmniCaptureControlPanel::GetFFmpegMetadataTooltip() const
{
    if (!IsSphericalMetadataSupported())
    {
        return LOCTEXT("FFmpegMetadataTooltipDisabled", "FFmpeg spherical metadata is disabled for planar and dome captures.");
    }

    return FeatureAvailability.FFmpeg.bAvailable ? LOCTEXT("FFmpegMetadataTooltip", "Inject spherical metadata during FFmpeg muxing.") : FeatureAvailability.FFmpeg.Reason;
}

FText SOmniCaptureControlPanel::GetFFmpegWarningText() const
{
    if (!IsSphericalMetadataSupported())
    {
        return FText::GetEmpty();
    }

    return FeatureAvailability.FFmpeg.bAvailable ? FText::GetEmpty() : FeatureAvailability.FFmpeg.Reason;
}

EVisibility SOmniCaptureControlPanel::GetFFmpegWarningVisibility() const
{
    if (!IsSphericalMetadataSupported())
    {
        return EVisibility::Collapsed;
    }

    return FeatureAvailability.FFmpeg.bAvailable ? EVisibility::Collapsed : EVisibility::Visible;
}

SOmniCaptureControlPanel::EOutputDirectoryMode SOmniCaptureControlPanel::GetCurrentOutputDirectoryMode() const
{
    if (!SettingsObject.IsValid())
    {
        return EOutputDirectoryMode::ProjectDefault;
    }

    return SettingsObject->CaptureSettings.OutputDirectory.IsEmpty()
        ? EOutputDirectoryMode::ProjectDefault
        : EOutputDirectoryMode::Custom;
}

void SOmniCaptureControlPanel::ApplyOutputDirectoryMode(EOutputDirectoryMode Mode)
{
    if (Mode == EOutputDirectoryMode::ProjectDefault)
    {
        if (SettingsObject.IsValid() && !SettingsObject->CaptureSettings.OutputDirectory.IsEmpty())
        {
            ModifyCaptureSettings([](FOmniCaptureSettings& Settings)
            {
                Settings.OutputDirectory.Reset();
            });
        }
    }

    UpdateOutputDirectoryDisplay();
}

FText SOmniCaptureControlPanel::GetOutputDirectoryModeTooltip(EOutputDirectoryMode Mode) const
{
    switch (Mode)
    {
    case EOutputDirectoryMode::ProjectDefault:
        return LOCTEXT("OutputDirectoryModeProjectDefaultTooltip", "Save captures in the project's Saved/OmniCaptures folder.");
    case EOutputDirectoryMode::Custom:
    default:
        return LOCTEXT("OutputDirectoryModeCustomTooltip", "Save captures in a folder that you choose.");
    }
}

void SOmniCaptureControlPanel::UpdateOutputDirectoryDisplay()
{
    FString DisplayPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OmniCaptures"));
    EOutputDirectoryMode Mode = EOutputDirectoryMode::ProjectDefault;

    if (SettingsObject.IsValid())
    {
        const FString& ConfiguredPath = SettingsObject->CaptureSettings.OutputDirectory;
        if (!ConfiguredPath.IsEmpty())
        {
            DisplayPath = FPaths::ConvertRelativePathToFull(ConfiguredPath);
            Mode = EOutputDirectoryMode::Custom;
        }
        else
        {
            Mode = EOutputDirectoryMode::ProjectDefault;
        }
    }

    if (OutputDirectoryTextBlock.IsValid())
    {
        const FText ModeSuffix = (Mode == EOutputDirectoryMode::ProjectDefault)
            ? LOCTEXT("OutputDirectoryDefaultSuffix", " (Project Default)")
            : FText::GetEmpty();
        OutputDirectoryTextBlock->SetText(FText::Format(LOCTEXT("OutputDirectoryFormat", "Output Folder: {0}{1}"), FText::FromString(DisplayPath), ModeSuffix));
    }

    const bool bSkipComboUpdates = IsAnyComboBoxOpen();
    if (!bSkipComboUpdates && OutputDirectoryModeCombo.IsValid())
    {
        OutputDirectoryModeCombo->SetSelectedItem(FindOutputDirectoryModeOption(Mode));
    }
}

void SOmniCaptureControlPanel::RefreshConfigurationSummary()
{
    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();

    const FIntPoint PerEyeSize = Snapshot.GetPerEyeOutputResolution();
    const FIntPoint OutputSize = Snapshot.GetOutputResolution();
    const int32 Alignment = Snapshot.GetEncoderAlignmentRequirement();

    if (DerivedPerEyeTextBlock.IsValid())
    {
        if (Snapshot.IsPlanar())
        {
            const FIntPoint BasePlanar(FMath::Max(1, Snapshot.PlanarResolution.X), FMath::Max(1, Snapshot.PlanarResolution.Y));
            DerivedPerEyeTextBlock->SetText(FText::Format(LOCTEXT("PlanarBaseSummary", "Planar base: {0}×{1}"), FText::AsNumber(BasePlanar.X), FText::AsNumber(BasePlanar.Y)));
        }
        else
        {
            DerivedPerEyeTextBlock->SetText(FText::Format(LOCTEXT("PerEyeSummary", "Per-eye output: {0}×{1}"), FText::AsNumber(PerEyeSize.X), FText::AsNumber(PerEyeSize.Y)));
        }
    }
    if (DerivedOutputTextBlock.IsValid())
    {
        if (Snapshot.IsPlanar())
        {
            DerivedOutputTextBlock->SetText(FText::Format(LOCTEXT("PlanarOutputSummary", "Final frame: {0}×{1} (Scale ×{2})"),
                FText::AsNumber(OutputSize.X),
                FText::AsNumber(OutputSize.Y),
                FText::AsNumber(FMath::Max(1, Snapshot.PlanarIntegerScale))));
        }
        else
        {
            DerivedOutputTextBlock->SetText(FText::Format(LOCTEXT("OutputSummary", "Final frame: {0}×{1} ({2})"),
                FText::AsNumber(OutputSize.X),
                FText::AsNumber(OutputSize.Y),
                LayoutToText(Snapshot)));
        }
    }
    if (DerivedFOVTextBlock.IsValid())
    {
        if (Snapshot.IsPlanar())
        {
            DerivedFOVTextBlock->SetText(LOCTEXT("FOVSummaryPlanar", "FOV: N/A for planar projection"));
        }
        else
        {
            const FText FovText = FText::Format(LOCTEXT("FOVSummary", "FOV: {0}° horizontal × {1}° vertical"),
                FText::AsNumber(Snapshot.GetHorizontalFOVDegrees()),
                FText::AsNumber(Snapshot.GetVerticalFOVDegrees()));
            DerivedFOVTextBlock->SetText(FovText);
        }
    }
    if (EncoderAlignmentTextBlock.IsValid())
    {
        EncoderAlignmentTextBlock->SetText(FText::Format(LOCTEXT("AlignmentSummary", "Encoder alignment: {0}-pixel"), FText::AsNumber(Alignment)));
    }

    const bool bSkipComboUpdates = IsAnyComboBoxOpen();
    if (!bSkipComboUpdates && StereoLayoutCombo.IsValid())
    {
        StereoLayoutCombo->SetSelectedItem(FindStereoLayoutOption(Snapshot.StereoLayout));
    }
    if (!bSkipComboUpdates && OutputFormatCombo.IsValid())
    {
        OutputFormatCombo->SetSelectedItem(FindOutputFormatOption(Snapshot.OutputFormat));
    }
    if (!bSkipComboUpdates && CodecCombo.IsValid())
    {
        CodecCombo->SetSelectedItem(FindCodecOption(Snapshot.Codec));
    }
    if (!bSkipComboUpdates && ColorFormatCombo.IsValid())
    {
        ColorFormatCombo->SetSelectedItem(FindColorFormatOption(Snapshot.NVENCColorFormat));
    }
    if (!bSkipComboUpdates && ProjectionCombo.IsValid())
    {
        ProjectionCombo->SetSelectedItem(FindProjectionOption(Snapshot.Projection));
    }
    if (!bSkipComboUpdates && FisheyeTypeCombo.IsValid())
    {
        FisheyeTypeCombo->SetSelectedItem(FindFisheyeTypeOption(Snapshot.FisheyeType));
    }
    if (!bSkipComboUpdates && ImageFormatCombo.IsValid())
    {
        ImageFormatCombo->SetSelectedItem(FindImageFormatOption(Snapshot.ImageFormat));
    }
    if (!bSkipComboUpdates && PNGBitDepthCombo.IsValid())
    {
        PNGBitDepthCombo->SetSelectedItem(FindPNGBitDepthOption(Snapshot.PNGBitDepth));
    }
}

void SOmniCaptureControlPanel::ModifyCaptureSettings(TFunctionRef<void(FOmniCaptureSettings&)> Mutator)
{
    if (!SettingsObject.IsValid())
    {
        return;
    }

    UOmniCaptureEditorSettings* Settings = SettingsObject.Get();
    Settings->Modify();
    Settings->CaptureSettings.MigrateDeprecatedOverrides();
    Mutator(Settings->CaptureSettings);
    Settings->CaptureSettings.MigrateDeprecatedOverrides();
    Settings->SaveConfig();

    FOmniCaptureNVENCEncoder::SetRuntimeDirectoryOverride(Settings->CaptureSettings.GetEffectiveNVENCRuntimeDirectory());
    FOmniCaptureNVENCEncoder::SetDllOverridePath(Settings->CaptureSettings.NVENCDllPathOverride);

    if (SettingsView.IsValid())
    {
        SettingsView->ForceRefresh();
    }

    RefreshConfigurationSummary();
    RefreshStatus();
}

FOmniCaptureSettings SOmniCaptureControlPanel::GetSettingsSnapshot() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        if (Subsystem->IsCapturing())
        {
            FOmniCaptureSettings Snapshot = Subsystem->GetActiveSettings();
            Snapshot.MigrateDeprecatedOverrides();
            return Snapshot;
        }
    }

    FOmniCaptureSettings Snapshot = SettingsObject.IsValid() ? SettingsObject->CaptureSettings : FOmniCaptureSettings();
    Snapshot.MigrateDeprecatedOverrides();
    return Snapshot;
}

void SOmniCaptureControlPanel::ApplyVRMode(bool bVR180)
{
    ModifyCaptureSettings([bVR180](FOmniCaptureSettings& Settings)
    {
        Settings.Coverage = bVR180 ? EOmniCaptureCoverage::HalfSphere : EOmniCaptureCoverage::FullSphere;
        if (Settings.IsFisheye())
        {
            Settings.FisheyeType = bVR180 ? EOmniCaptureFisheyeType::Hemispherical : EOmniCaptureFisheyeType::OmniDirectional;
        }
    });
}

void SOmniCaptureControlPanel::ApplyStereoMode(bool bStereo)
{
    ModifyCaptureSettings([bStereo](FOmniCaptureSettings& Settings)
    {
        Settings.Mode = bStereo ? EOmniCaptureMode::Stereo : EOmniCaptureMode::Mono;
        if (!bStereo)
        {
            Settings.PreviewVisualization = EOmniCapturePreviewView::StereoComposite;
        }
    });

    if (!bStereo)
    {
        if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
        {
            Subsystem->SetPreviewVisualizationMode(EOmniCapturePreviewView::StereoComposite);
        }
    }
}

void SOmniCaptureControlPanel::ApplyStereoLayout(EOmniCaptureStereoLayout Layout)
{
    ModifyCaptureSettings([Layout](FOmniCaptureSettings& Settings)
    {
        Settings.StereoLayout = Layout;
    });
}

void SOmniCaptureControlPanel::ApplyPerEyeWidth(int32 NewWidth)
{
    ModifyCaptureSettings([NewWidth](FOmniCaptureSettings& Settings)
    {
        const int32 Alignment = Settings.GetEncoderAlignmentRequirement();
        const int32 MaxDimension = Settings.IsStereo() ? 16384 : 32768;
        const int32 ClampedWidth = FMath::Clamp(NewWidth, 1, MaxDimension);
        const int32 Base = Settings.IsVR180() ? ClampedWidth : FMath::Max(1, ClampedWidth / 2);
        Settings.Resolution = AlignDimensionUI(Base, Alignment);
    });
}

void SOmniCaptureControlPanel::ApplyPerEyeHeight(int32 NewHeight)
{
    ModifyCaptureSettings([NewHeight](FOmniCaptureSettings& Settings)
    {
        const int32 Alignment = Settings.GetEncoderAlignmentRequirement();
        const int32 MaxDimension = Settings.IsStereo() ? 16384 : 32768;
        const int32 ClampedHeight = FMath::Clamp(NewHeight, 1, MaxDimension);
        Settings.Resolution = AlignDimensionUI(ClampedHeight, Alignment);
    });
}

void SOmniCaptureControlPanel::ApplyPlanarWidth(int32 NewWidth)
{
    ModifyCaptureSettings([NewWidth](FOmniCaptureSettings& Settings)
    {
        Settings.PlanarResolution.X = FMath::Max(16, NewWidth);
    });
}

void SOmniCaptureControlPanel::ApplyPlanarHeight(int32 NewHeight)
{
    ModifyCaptureSettings([NewHeight](FOmniCaptureSettings& Settings)
    {
        Settings.PlanarResolution.Y = FMath::Max(16, NewHeight);
    });
}

void SOmniCaptureControlPanel::ApplyPlanarScale(int32 NewScale)
{
    ModifyCaptureSettings([NewScale](FOmniCaptureSettings& Settings)
    {
        Settings.PlanarIntegerScale = FMath::Clamp(NewScale, 1, 16);
    });
}

void SOmniCaptureControlPanel::ApplyFisheyeWidth(int32 NewWidth)
{
    ModifyCaptureSettings([NewWidth](FOmniCaptureSettings& Settings)
    {
        const int32 Alignment = Settings.GetEncoderAlignmentRequirement();
        const int32 Clamped = FMath::Clamp(NewWidth, 256, 32768);
        Settings.FisheyeResolution.X = AlignDimensionUI(Clamped, Alignment);
    });
}

void SOmniCaptureControlPanel::ApplyFisheyeHeight(int32 NewHeight)
{
    ModifyCaptureSettings([NewHeight](FOmniCaptureSettings& Settings)
    {
        const int32 Alignment = Settings.GetEncoderAlignmentRequirement();
        const int32 Clamped = FMath::Clamp(NewHeight, 256, 32768);
        Settings.FisheyeResolution.Y = AlignDimensionUI(Clamped, Alignment);
    });
}

void SOmniCaptureControlPanel::ApplyFisheyeFOV(float NewFov)
{
    ModifyCaptureSettings([NewFov](FOmniCaptureSettings& Settings)
    {
        Settings.FisheyeFOV = FMath::Clamp(NewFov, 90.0f, 360.0f);
    });
}

void SOmniCaptureControlPanel::ApplyFisheyeType(EOmniCaptureFisheyeType Type)
{
    ModifyCaptureSettings([Type](FOmniCaptureSettings& Settings)
    {
        Settings.FisheyeType = Type;
        Settings.Coverage = (Type == EOmniCaptureFisheyeType::Hemispherical)
            ? EOmniCaptureCoverage::HalfSphere
            : EOmniCaptureCoverage::FullSphere;
    });
}

void SOmniCaptureControlPanel::ApplyFisheyeConvert(bool bEnable)
{
    ModifyCaptureSettings([bEnable](FOmniCaptureSettings& Settings)
    {
        Settings.bFisheyeConvertToEquirect = bEnable;
    });
}

void SOmniCaptureControlPanel::ApplyProjection(EOmniCaptureProjection Projection)
{
    ModifyCaptureSettings([Projection](FOmniCaptureSettings& Settings)
    {
        Settings.Projection = Projection;
    });
}

void SOmniCaptureControlPanel::ApplyOutputFormat(EOmniOutputFormat Format)
{
    const bool bPreferNVENC = Format != EOmniOutputFormat::ImageSequence;
    ModifyCaptureSettings([this, Format, bPreferNVENC](FOmniCaptureSettings& Settings)
    {
        Settings.OutputFormat = Format;
        if (SettingsObject.IsValid())
        {
            if (UOmniCaptureEditorSettings* EditorSettings = SettingsObject.Get())
            {
                EditorSettings->bPreferNVENCWhenAvailable = bPreferNVENC;
            }
        }
    });
}

void SOmniCaptureControlPanel::ApplyCodec(EOmniCaptureCodec Codec)
{
    ModifyCaptureSettings([Codec](FOmniCaptureSettings& Settings)
    {
        Settings.Codec = Codec;
    });
}

void SOmniCaptureControlPanel::ApplyColorFormat(EOmniCaptureColorFormat Format)
{
    ModifyCaptureSettings([Format](FOmniCaptureSettings& Settings)
    {
        Settings.NVENCColorFormat = Format;
    });
}

void SOmniCaptureControlPanel::ApplyImageFormat(EOmniCaptureImageFormat Format)
{
    ModifyCaptureSettings([Format](FOmniCaptureSettings& Settings)
    {
        Settings.ImageFormat = Format;
    });
}

void SOmniCaptureControlPanel::ApplyPNGBitDepth(EOmniCapturePNGBitDepth BitDepth)
{
    ModifyCaptureSettings([BitDepth](FOmniCaptureSettings& Settings)
    {
        Settings.PNGBitDepth = BitDepth;
    });
}

void SOmniCaptureControlPanel::ApplyMetadataToggle(EMetadataToggle Toggle, bool bEnabled)
{
    if (!IsSphericalMetadataSupported() && Toggle != EMetadataToggle::Manifest && bEnabled)
    {
        return;
    }

    ModifyCaptureSettings([Toggle, bEnabled](FOmniCaptureSettings& Settings)
    {
        switch (Toggle)
        {
        case EMetadataToggle::Manifest:
            Settings.bGenerateManifest = bEnabled;
            break;
        case EMetadataToggle::SpatialJson:
            Settings.bWriteSpatialMetadata = bEnabled;
            break;
        case EMetadataToggle::XMP:
            Settings.bWriteXMPMetadata = bEnabled;
            break;
        case EMetadataToggle::FFmpeg:
            Settings.bInjectFFmpegMetadata = bEnabled;
            break;
        default:
            break;
        }
    });
}

bool SOmniCaptureControlPanel::IsSphericalMetadataSupported() const
{
    return GetSettingsSnapshot().SupportsSphericalMetadata();
}

ECheckBoxState SOmniCaptureControlPanel::GetVRModeCheckState(bool bVR180) const
{
    const bool bIsVR180 = GetSettingsSnapshot().IsVR180();
    return (bIsVR180 == bVR180) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOmniCaptureControlPanel::HandleVRModeChanged(ECheckBoxState NewState, bool bVR180)
{
    if (NewState == ECheckBoxState::Checked)
    {
        ApplyVRMode(bVR180);
    }
}

ECheckBoxState SOmniCaptureControlPanel::GetStereoModeCheckState(bool bStereo) const
{
    const bool bIsStereo = GetSettingsSnapshot().IsStereo();
    return (bIsStereo == bStereo) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOmniCaptureControlPanel::HandleStereoModeChanged(ECheckBoxState NewState, bool bStereo)
{
    if (NewState == ECheckBoxState::Checked)
    {
        ApplyStereoMode(bStereo);
    }
}

FText SOmniCaptureControlPanel::GetStereoLayoutDisplayText() const
{
    return LayoutToText(GetSettingsSnapshot());
}

void SOmniCaptureControlPanel::HandleStereoLayoutChanged(TEnumOptionPtr<EOmniCaptureStereoLayout> NewValue, ESelectInfo::Type SelectInfo)
{
    if (NewValue.IsValid())
    {
        ApplyStereoLayout(static_cast<EOmniCaptureStereoLayout>(NewValue->GetValue()));
    }
}

TSharedRef<SWidget> SOmniCaptureControlPanel::GenerateStereoLayoutOption(TEnumOptionPtr<EOmniCaptureStereoLayout> InValue) const
{
    const EOmniCaptureStereoLayout Layout = InValue.IsValid()
        ? static_cast<EOmniCaptureStereoLayout>(InValue->GetValue())
        : EOmniCaptureStereoLayout::SideBySide;
    const FText Label = (Layout == EOmniCaptureStereoLayout::TopBottom)
        ? LOCTEXT("StereoLayoutTopBottom", "Top / Bottom")
        : LOCTEXT("StereoLayoutSideBySide", "Side-by-Side");
    return SNew(STextBlock).Text(Label);
}

void SOmniCaptureControlPanel::HandleProjectionChanged(TEnumOptionPtr<EOmniCaptureProjection> NewProjection, ESelectInfo::Type SelectInfo)
{
    if (NewProjection.IsValid())
    {
        ApplyProjection(static_cast<EOmniCaptureProjection>(NewProjection->GetValue()));
    }
}

TSharedRef<SWidget> SOmniCaptureControlPanel::GenerateProjectionOption(TEnumOptionPtr<EOmniCaptureProjection> InValue) const
{
    const EOmniCaptureProjection Projection = InValue.IsValid()
        ? static_cast<EOmniCaptureProjection>(InValue->GetValue())
        : EOmniCaptureProjection::Equirectangular;
    return SNew(STextBlock).Text(ProjectionToText(Projection));
}

TSharedRef<SWidget> SOmniCaptureControlPanel::GenerateFisheyeTypeOption(TEnumOptionPtr<EOmniCaptureFisheyeType> InValue) const
{
    const EOmniCaptureFisheyeType Type = InValue.IsValid()
        ? static_cast<EOmniCaptureFisheyeType>(InValue->GetValue())
        : EOmniCaptureFisheyeType::Hemispherical;
    return SNew(STextBlock).Text(FisheyeTypeToText(Type));
}

int32 SOmniCaptureControlPanel::GetPerEyeWidthValue() const
{
    return GetSettingsSnapshot().GetPerEyeOutputResolution().X;
}

int32 SOmniCaptureControlPanel::GetPerEyeHeightValue() const
{
    return GetSettingsSnapshot().GetPerEyeOutputResolution().Y;
}

TOptional<int32> SOmniCaptureControlPanel::GetPerEyeDimensionMaxValue() const
{
    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
    const int32 MaxValue = Snapshot.IsStereo() ? 16384 : 32768;
    return TOptional<int32>(MaxValue);
}

int32 SOmniCaptureControlPanel::GetPlanarWidthValue() const
{
    return GetSettingsSnapshot().PlanarResolution.X;
}

int32 SOmniCaptureControlPanel::GetPlanarHeightValue() const
{
    return GetSettingsSnapshot().PlanarResolution.Y;
}

int32 SOmniCaptureControlPanel::GetPlanarScaleValue() const
{
    return GetSettingsSnapshot().PlanarIntegerScale;
}

int32 SOmniCaptureControlPanel::GetFisheyeWidthValue() const
{
    return GetSettingsSnapshot().FisheyeResolution.X;
}

int32 SOmniCaptureControlPanel::GetFisheyeHeightValue() const
{
    return GetSettingsSnapshot().FisheyeResolution.Y;
}

float SOmniCaptureControlPanel::GetFisheyeFOVValue() const
{
    return GetSettingsSnapshot().FisheyeFOV;
}

void SOmniCaptureControlPanel::HandlePerEyeWidthCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ApplyPerEyeWidth(FMath::Max(1, NewValue));
}

void SOmniCaptureControlPanel::HandlePerEyeHeightCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ApplyPerEyeHeight(FMath::Max(1, NewValue));
}

void SOmniCaptureControlPanel::HandlePlanarWidthCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ApplyPlanarWidth(FMath::Max(16, NewValue));
}

void SOmniCaptureControlPanel::HandlePlanarHeightCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ApplyPlanarHeight(FMath::Max(16, NewValue));
}

void SOmniCaptureControlPanel::HandlePlanarScaleCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ApplyPlanarScale(FMath::Max(1, NewValue));
}

void SOmniCaptureControlPanel::HandleFisheyeTypeChanged(TEnumOptionPtr<EOmniCaptureFisheyeType> NewValue, ESelectInfo::Type SelectInfo)
{
    if (NewValue.IsValid())
    {
        ApplyFisheyeType(static_cast<EOmniCaptureFisheyeType>(NewValue->GetValue()));
    }
}

ECheckBoxState SOmniCaptureControlPanel::GetFisheyeConvertState() const
{
    return GetSettingsSnapshot().bFisheyeConvertToEquirect ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOmniCaptureControlPanel::HandleFisheyeConvertChanged(ECheckBoxState NewState)
{
    ApplyFisheyeConvert(NewState == ECheckBoxState::Checked);
}

void SOmniCaptureControlPanel::HandleFisheyeWidthCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ApplyFisheyeWidth(FMath::Max(256, NewValue));
}

void SOmniCaptureControlPanel::HandleFisheyeHeightCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ApplyFisheyeHeight(FMath::Max(256, NewValue));
}

void SOmniCaptureControlPanel::HandleFisheyeFOVCommitted(float NewValue, ETextCommit::Type CommitType)
{
    ApplyFisheyeFOV(NewValue);
}

ECheckBoxState SOmniCaptureControlPanel::GetMetadataToggleState(EMetadataToggle Toggle) const
{
    const FOmniCaptureSettings Snapshot = GetSettingsSnapshot();
    const bool bSupportsSphericalMetadata = Snapshot.SupportsSphericalMetadata();
    bool bEnabled = false;

    switch (Toggle)
    {
    case EMetadataToggle::Manifest:
        bEnabled = Snapshot.bGenerateManifest;
        break;
    case EMetadataToggle::SpatialJson:
        bEnabled = bSupportsSphericalMetadata && Snapshot.bWriteSpatialMetadata;
        break;
    case EMetadataToggle::XMP:
        bEnabled = bSupportsSphericalMetadata && Snapshot.bWriteXMPMetadata;
        break;
    case EMetadataToggle::FFmpeg:
        bEnabled = bSupportsSphericalMetadata && Snapshot.bInjectFFmpegMetadata;
        break;
    default:
        break;
    }

    return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOmniCaptureControlPanel::HandleMetadataToggleChanged(ECheckBoxState NewState, EMetadataToggle Toggle)
{
    if (!IsSphericalMetadataSupported() && Toggle != EMetadataToggle::Manifest)
    {
        return;
    }

    ApplyMetadataToggle(Toggle, NewState == ECheckBoxState::Checked);
}

ECheckBoxState SOmniCaptureControlPanel::GetPreviewViewCheckState(EOmniCapturePreviewView View) const
{
    return GetSettingsSnapshot().PreviewVisualization == View ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOmniCaptureControlPanel::HandlePreviewViewChanged(ECheckBoxState NewState, EOmniCapturePreviewView View)
{
    if (NewState != ECheckBoxState::Checked)
    {
        return;
    }

    ModifyCaptureSettings([View](FOmniCaptureSettings& Settings)
    {
        Settings.PreviewVisualization = View;
    });

    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->SetPreviewVisualizationMode(View);
    }

    UpdatePreviewTextureDisplay();
}

TEnumOptionPtr<EOmniCaptureStereoLayout> SOmniCaptureControlPanel::FindStereoLayoutOption(EOmniCaptureStereoLayout Layout) const
{
    for (const TEnumOptionPtr<EOmniCaptureStereoLayout>& Option : StereoLayoutOptions)
    {
        if (Option.IsValid() && static_cast<EOmniCaptureStereoLayout>(Option->GetValue()) == Layout)
        {
            return Option;
        }
    }
    return StereoLayoutOptions.Num() > 0 ? StereoLayoutOptions[0] : nullptr;
}

TEnumOptionPtr<EOmniOutputFormat> SOmniCaptureControlPanel::FindOutputFormatOption(EOmniOutputFormat Format) const
{
    for (const TEnumOptionPtr<EOmniOutputFormat>& Option : OutputFormatOptions)
    {
        if (Option.IsValid() && static_cast<EOmniOutputFormat>(Option->GetValue()) == Format)
        {
            return Option;
        }
    }
    return OutputFormatOptions.Num() > 0 ? OutputFormatOptions[0] : nullptr;
}

TEnumOptionPtr<EOmniCaptureCodec> SOmniCaptureControlPanel::FindCodecOption(EOmniCaptureCodec Codec) const
{
    for (const TEnumOptionPtr<EOmniCaptureCodec>& Option : CodecOptions)
    {
        if (Option.IsValid() && static_cast<EOmniCaptureCodec>(Option->GetValue()) == Codec)
        {
            return Option;
        }
    }
    return CodecOptions.Num() > 0 ? CodecOptions[0] : nullptr;
}

TEnumOptionPtr<EOmniCaptureColorFormat> SOmniCaptureControlPanel::FindColorFormatOption(EOmniCaptureColorFormat Format) const
{
    for (const TEnumOptionPtr<EOmniCaptureColorFormat>& Option : ColorFormatOptions)
    {
        if (Option.IsValid() && static_cast<EOmniCaptureColorFormat>(Option->GetValue()) == Format)
        {
            return Option;
        }
    }
    return ColorFormatOptions.Num() > 0 ? ColorFormatOptions[0] : nullptr;
}

TEnumOptionPtr<EOmniCaptureProjection> SOmniCaptureControlPanel::FindProjectionOption(EOmniCaptureProjection Projection) const
{
    for (const TEnumOptionPtr<EOmniCaptureProjection>& Option : ProjectionOptions)
    {
        if (Option.IsValid() && static_cast<EOmniCaptureProjection>(Option->GetValue()) == Projection)
        {
            return Option;
        }
    }
    return ProjectionOptions.Num() > 0 ? ProjectionOptions[0] : nullptr;
}

TEnumOptionPtr<EOmniCaptureFisheyeType> SOmniCaptureControlPanel::FindFisheyeTypeOption(EOmniCaptureFisheyeType Type) const
{
    for (const TEnumOptionPtr<EOmniCaptureFisheyeType>& Option : FisheyeTypeOptions)
    {
        if (Option.IsValid() && static_cast<EOmniCaptureFisheyeType>(Option->GetValue()) == Type)
        {
            return Option;
        }
    }
    return FisheyeTypeOptions.Num() > 0 ? FisheyeTypeOptions[0] : nullptr;
}

TEnumOptionPtr<EOmniCaptureImageFormat> SOmniCaptureControlPanel::FindImageFormatOption(EOmniCaptureImageFormat Format) const
{
    for (const TEnumOptionPtr<EOmniCaptureImageFormat>& Option : ImageFormatOptions)
    {
        if (Option.IsValid() && static_cast<EOmniCaptureImageFormat>(Option->GetValue()) == Format)
        {
            return Option;
        }
    }
    return ImageFormatOptions.Num() > 0 ? ImageFormatOptions[0] : nullptr;
}

TEnumOptionPtr<EOmniCapturePNGBitDepth> SOmniCaptureControlPanel::FindPNGBitDepthOption(EOmniCapturePNGBitDepth BitDepth) const
{
    for (const TEnumOptionPtr<EOmniCapturePNGBitDepth>& Option : PNGBitDepthOptions)
    {
        if (Option.IsValid() && static_cast<EOmniCapturePNGBitDepth>(Option->GetValue()) == BitDepth)
        {
            return Option;
        }
    }
    return PNGBitDepthOptions.Num() > 0 ? PNGBitDepthOptions[0] : nullptr;
}

TEnumOptionPtr<EOutputDirectoryMode> SOmniCaptureControlPanel::FindOutputDirectoryModeOption(EOutputDirectoryMode Mode) const
{
    for (const TEnumOptionPtr<EOutputDirectoryMode>& Option : OutputDirectoryModeOptions)
    {
        if (Option.IsValid() && static_cast<EOutputDirectoryMode>(Option->GetValue()) == Mode)
        {
            return Option;
        }
    }
    return OutputDirectoryModeOptions.Num() > 0 ? OutputDirectoryModeOptions[0] : nullptr;
}

void SOmniCaptureControlPanel::HandleOutputFormatChanged(TEnumOptionPtr<EOmniOutputFormat> NewFormat, ESelectInfo::Type SelectInfo)
{
    if (NewFormat.IsValid())
    {
        const EOmniOutputFormat Format = static_cast<EOmniOutputFormat>(NewFormat->GetValue());
        if (!IsOutputFormatSelectable(Format))
        {
            return;
        }
        ApplyOutputFormat(Format);
    }
}

void SOmniCaptureControlPanel::HandleCodecChanged(TEnumOptionPtr<EOmniCaptureCodec> NewCodec, ESelectInfo::Type SelectInfo)
{
    if (NewCodec.IsValid())
    {
        const EOmniCaptureCodec Codec = static_cast<EOmniCaptureCodec>(NewCodec->GetValue());
        if (!IsCodecSelectable(Codec))
        {
            return;
        }
        ApplyCodec(Codec);
    }
}

void SOmniCaptureControlPanel::HandleColorFormatChanged(TEnumOptionPtr<EOmniCaptureColorFormat> NewFormat, ESelectInfo::Type SelectInfo)
{
    if (NewFormat.IsValid())
    {
        const EOmniCaptureColorFormat Format = static_cast<EOmniCaptureColorFormat>(NewFormat->GetValue());
        if (!IsColorFormatSelectable(Format))
        {
            return;
        }
        ApplyColorFormat(Format);
    }
}

void SOmniCaptureControlPanel::HandleImageFormatChanged(TEnumOptionPtr<EOmniCaptureImageFormat> NewFormat, ESelectInfo::Type SelectInfo)
{
    if (NewFormat.IsValid())
    {
        ApplyImageFormat(static_cast<EOmniCaptureImageFormat>(NewFormat->GetValue()));
    }
}

TSharedRef<SWidget> SOmniCaptureControlPanel::GenerateImageFormatOption(TEnumOptionPtr<EOmniCaptureImageFormat> InValue) const
{
    const EOmniCaptureImageFormat Format = InValue.IsValid()
        ? static_cast<EOmniCaptureImageFormat>(InValue->GetValue())
        : EOmniCaptureImageFormat::PNG;
    return SNew(STextBlock).Text(ImageFormatToText(Format));
}

void SOmniCaptureControlPanel::HandlePNGBitDepthChanged(TEnumOptionPtr<EOmniCapturePNGBitDepth> NewValue, ESelectInfo::Type SelectInfo)
{
    if (NewValue.IsValid())
    {
        ApplyPNGBitDepth(static_cast<EOmniCapturePNGBitDepth>(NewValue->GetValue()));
    }
}

TSharedRef<SWidget> SOmniCaptureControlPanel::GeneratePNGBitDepthOption(TEnumOptionPtr<EOmniCapturePNGBitDepth> InValue) const
{
    const EOmniCapturePNGBitDepth BitDepth = InValue.IsValid()
        ? static_cast<EOmniCapturePNGBitDepth>(InValue->GetValue())
        : EOmniCapturePNGBitDepth::BitDepth8;
    return SNew(STextBlock).Text(PNGBitDepthToText(BitDepth));
}

void SOmniCaptureControlPanel::HandleOutputDirectoryModeChanged(TEnumOptionPtr<EOutputDirectoryMode> NewValue, ESelectInfo::Type SelectInfo)
{
    if (!NewValue.IsValid() || SelectInfo == ESelectInfo::OnNavigation)
    {
        return;
    }

    const EOutputDirectoryMode Mode = static_cast<EOutputDirectoryMode>(NewValue->GetValue());
    if (Mode == EOutputDirectoryMode::ProjectDefault)
    {
        ApplyOutputDirectoryMode(Mode);
        return;
    }

    if (SettingsObject.IsValid() && !SettingsObject->CaptureSettings.OutputDirectory.IsEmpty())
    {
        UpdateOutputDirectoryDisplay();
        return;
    }

    if (!TrySelectCustomOutputDirectory())
    {
        ApplyOutputDirectoryMode(EOutputDirectoryMode::ProjectDefault);
    }
}

TSharedRef<SWidget> SOmniCaptureControlPanel::GenerateOutputDirectoryModeOption(TEnumOptionPtr<EOutputDirectoryMode> InValue) const
{
    const EOutputDirectoryMode Mode = InValue.IsValid()
        ? static_cast<EOutputDirectoryMode>(InValue->GetValue())
        : EOutputDirectoryMode::ProjectDefault;
    return SNew(STextBlock)
        .Text(OutputDirectoryModeToText(Mode))
        .ToolTipText(GetOutputDirectoryModeTooltip(Mode));
}

int32 SOmniCaptureControlPanel::GetTargetBitrate() const
{
    return GetSettingsSnapshot().Quality.TargetBitrateKbps;
}

int32 SOmniCaptureControlPanel::GetMaxBitrate() const
{
    return GetSettingsSnapshot().Quality.MaxBitrateKbps;
}

int32 SOmniCaptureControlPanel::GetGOPLength() const
{
    return GetSettingsSnapshot().Quality.GOPLength;
}

int32 SOmniCaptureControlPanel::GetBFrameCount() const
{
    return GetSettingsSnapshot().Quality.BFrames;
}

void SOmniCaptureControlPanel::HandleTargetBitrateCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ModifyCaptureSettings([NewValue](FOmniCaptureSettings& Settings)
    {
        Settings.Quality.TargetBitrateKbps = FMath::Clamp(NewValue, 1000, 1'500'000);
    });
}

void SOmniCaptureControlPanel::HandleMaxBitrateCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ModifyCaptureSettings([NewValue](FOmniCaptureSettings& Settings)
    {
        Settings.Quality.MaxBitrateKbps = FMath::Clamp(NewValue, 1000, 1'500'000);
    });
}

void SOmniCaptureControlPanel::HandleGOPCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ModifyCaptureSettings([NewValue](FOmniCaptureSettings& Settings)
    {
        Settings.Quality.GOPLength = FMath::Clamp(NewValue, 1, 600);
    });
}

void SOmniCaptureControlPanel::HandleBFramesCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    ModifyCaptureSettings([NewValue](FOmniCaptureSettings& Settings)
    {
        Settings.Quality.BFrames = FMath::Clamp(NewValue, 0, 8);
    });
}

ECheckBoxState SOmniCaptureControlPanel::GetZeroCopyState() const
{
    return GetSettingsSnapshot().bZeroCopy ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOmniCaptureControlPanel::HandleZeroCopyChanged(ECheckBoxState NewState)
{
    ModifyCaptureSettings([NewState](FOmniCaptureSettings& Settings)
    {
        Settings.bZeroCopy = (NewState == ECheckBoxState::Checked);
    });
}

ECheckBoxState SOmniCaptureControlPanel::GetFastStartState() const
{
    return GetSettingsSnapshot().bEnableFastStart ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOmniCaptureControlPanel::HandleFastStartChanged(ECheckBoxState NewState)
{
    ModifyCaptureSettings([NewState](FOmniCaptureSettings& Settings)
    {
        Settings.bEnableFastStart = (NewState == ECheckBoxState::Checked);
    });
}

ECheckBoxState SOmniCaptureControlPanel::GetConstantFrameRateState() const
{
    return GetSettingsSnapshot().bForceConstantFrameRate ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SOmniCaptureControlPanel::HandleConstantFrameRateChanged(ECheckBoxState NewState)
{
    ModifyCaptureSettings([NewState](FOmniCaptureSettings& Settings)
    {
        Settings.bForceConstantFrameRate = (NewState == ECheckBoxState::Checked);
    });
}

#undef LOCTEXT_NAMESPACE
