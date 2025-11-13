#include "OmniCaptureMuxer.h"
#include "OmniCaptureTypes.h"
#include "Misc/EngineVersionComparison.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    FString NormalizeFFmpegCandidatePath(const FString& InPath)
    {
        FString Trimmed = InPath;
        Trimmed.TrimStartAndEndInline();

        while (Trimmed.StartsWith(TEXT("\"")) || Trimmed.StartsWith(TEXT("'")))
        {
            Trimmed = Trimmed.RightChop(1);
        }
        while (Trimmed.EndsWith(TEXT("\"")) || Trimmed.EndsWith(TEXT("'")))
        {
            Trimmed = Trimmed.LeftChop(1);
        }

        if (Trimmed.IsEmpty())
        {
            return Trimmed;
        }

        FString Normalized = Trimmed;
        FPaths::NormalizeFilename(Normalized);

        IFileManager& FileManager = IFileManager::Get();

        auto AppendExecutable = [](const FString& Directory)
        {
            FString ExecutableName = TEXT("ffmpeg");
#if PLATFORM_WINDOWS
            ExecutableName += TEXT(".exe");
#endif
            return FPaths::Combine(Directory, ExecutableName);
        };

        const bool bIsDirectory = FileManager.DirectoryExists(*Normalized);
        if (bIsDirectory)
        {
            return AppendExecutable(Normalized);
        }

        if (FileManager.FileExists(*Normalized))
        {
            return Normalized;
        }

        const FString AbsolutePath = FPaths::ConvertRelativePathToFull(Normalized);
        if (FileManager.DirectoryExists(*AbsolutePath))
        {
            return AppendExecutable(AbsolutePath);
        }

        if (FileManager.FileExists(*AbsolutePath))
        {
            return AbsolutePath;
        }

        return Trimmed;
    }

    static bool IsImageSequenceFormat(EOmniOutputFormat Format)
    {
#if UE_VERSION_OLDER_THAN(5, 6, 0)
        return Format == EOmniOutputFormat::PNGSequence || Format == EOmniOutputFormat::ImageSequence;
#else
        return Format == EOmniOutputFormat::ImageSequence;
#endif
    }

    const TCHAR* ToCoverageString(EOmniCaptureCoverage Coverage)
    {
        return Coverage == EOmniCaptureCoverage::HalfSphere ? TEXT("VR180") : TEXT("VR360");
    }

    const TCHAR* ToLayoutString(const FOmniCaptureSettings& Settings)
    {
        if (Settings.Mode == EOmniCaptureMode::Stereo)
        {
            return Settings.StereoLayout == EOmniCaptureStereoLayout::TopBottom
                ? TEXT("StereoTopBottom")
                : TEXT("StereoSideBySide");
        }

        return TEXT("Mono");
    }
}

FString FOmniCaptureMuxer::ResolveFFmpegBinary(const FOmniCaptureSettings& Settings)
{
    if (!Settings.PreferredFFmpegPath.IsEmpty())
    {
        return NormalizeFFmpegCandidatePath(Settings.PreferredFFmpegPath);
    }
    FString EnvPath = FPlatformMisc::GetEnvironmentVariable(TEXT("OMNICAPTURE_FFMPEG"));
    if (!EnvPath.IsEmpty())
    {
        return NormalizeFFmpegCandidatePath(EnvPath);
    }
    return TEXT("ffmpeg");
}

bool FOmniCaptureMuxer::IsFFmpegAvailable(const FOmniCaptureSettings& Settings, FString* OutResolvedPath)
{
    const FString Resolved = ResolveFFmpegBinary(Settings);
    if (OutResolvedPath)
    {
        *OutResolvedPath = Resolved;
    }
    if (Resolved.IsEmpty())
    {
        return false;
    }
    if (Resolved.Equals(TEXT("ffmpeg"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    if (FPaths::FileExists(Resolved))
    {
        return true;
    }

    const FString AbsoluteResolved = FPaths::ConvertRelativePathToFull(Resolved);
    return FPaths::FileExists(AbsoluteResolved);
}

void FOmniCaptureMuxer::Initialize(const FOmniCaptureSettings& Settings, const FString& InOutputDirectory)
{
    OutputDirectory = InOutputDirectory.IsEmpty() ? (FPaths::ProjectSavedDir() / TEXT("OmniCaptures")) : InOutputDirectory;
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    BaseFileName = Settings.OutputFileName.IsEmpty() ? TEXT("OmniCapture") : Settings.OutputFileName;
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    CachedFFmpegPath = ResolveFFmpegBinary(Settings);
}

void FOmniCaptureMuxer::BeginRealtimeSession(const FOmniCaptureSettings& Settings)
{
    AudioStats = FOmniAudioSyncStats();
    LastVideoTimestamp = 0.0;
    LastAudioTimestamp = 0.0;
    DriftWarningThresholdMs = Settings.bForceConstantFrameRate ? 20.0 : 35.0;
    bRealtimeSessionActive = true;
}

void FOmniCaptureMuxer::EndRealtimeSession()
{
    bRealtimeSessionActive = false;
    AudioStats = FOmniAudioSyncStats();
    LastVideoTimestamp = 0.0;
    LastAudioTimestamp = 0.0;
}

void FOmniCaptureMuxer::PushFrame(const FOmniCaptureFrame& Frame)
{
    if (!bRealtimeSessionActive)
    {
        return;
    }

    LastVideoTimestamp = Frame.Metadata.Timecode;
    AudioStats.LatestVideoTimestamp = LastVideoTimestamp;

    int32 PacketCount = 0;
    double LatestAudioTime = LastAudioTimestamp;

    for (const FOmniAudioPacket& Packet : Frame.AudioPackets)
    {
        const double Duration = (Packet.SampleRate > 0 && Packet.NumChannels > 0)
            ? static_cast<double>(Packet.PCM16.Num()) / (static_cast<double>(Packet.SampleRate) * FMath::Max(Packet.NumChannels, 1))
            : 0.0;
        LatestAudioTime = FMath::Max(LatestAudioTime, Packet.Timestamp + Duration);
        ++PacketCount;
    }

    LastAudioTimestamp = LatestAudioTime;
    AudioStats.LatestAudioTimestamp = LastAudioTimestamp;
    AudioStats.PendingPackets = PacketCount;
    AudioStats.DriftMilliseconds = (LastAudioTimestamp - LastVideoTimestamp) * 1000.0;
    AudioStats.MaxObservedDriftMilliseconds = FMath::Max(AudioStats.MaxObservedDriftMilliseconds, FMath::Abs(AudioStats.DriftMilliseconds));
    AudioStats.bInError = FMath::Abs(AudioStats.DriftMilliseconds) > DriftWarningThresholdMs;
}

bool FOmniCaptureMuxer::FinalizeCapture(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath, int32 DroppedFrames)
{
    bool bSuccess = true;

    if (Settings.bGenerateManifest)
    {
        FString ManifestPath;
        if (WriteManifest(Settings, Frames, AudioPath, VideoPath, DroppedFrames, ManifestPath))
        {
            UE_LOG(LogTemp, Log, TEXT("OmniCapture manifest written to %s"), *ManifestPath);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to write OmniCapture manifest for %s"), *BaseFileName);
            bSuccess = false;
        }
    }

    if (!WriteSpatialMetadata(Settings))
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to write VR spatial metadata sidecars for %s"), *BaseFileName);
        bSuccess = false;
    }

    const bool bMuxed = TryInvokeFFmpeg(Settings, Frames, AudioPath, VideoPath);
    return bSuccess && bMuxed;
}

bool FOmniCaptureMuxer::WriteManifest(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath, int32 DroppedFrames, FString& OutManifestPath) const
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("fileBase"), BaseFileName);
    Root->SetStringField(TEXT("directory"), OutputDirectory);
    const TCHAR* OutputFormatString = TEXT("NVENC");
    if (IsImageSequenceFormat(Settings.OutputFormat))
    {
        OutputFormatString = TEXT("ImageSequence");
    }
    Root->SetStringField(TEXT("outputFormat"), OutputFormatString);
    Root->SetStringField(TEXT("mode"), Settings.Mode == EOmniCaptureMode::Stereo ? TEXT("Stereo") : TEXT("Mono"));
    Root->SetStringField(TEXT("coverage"), ToCoverageString(Settings.Coverage));
    Root->SetStringField(TEXT("gamma"), Settings.Gamma == EOmniCaptureGamma::Linear ? TEXT("Linear") : TEXT("sRGB"));
    Root->SetNumberField(TEXT("resolution"), Settings.Resolution);
    Root->SetNumberField(TEXT("frameCount"), Frames.Num());
    Root->SetNumberField(TEXT("frameRate"), CalculateFrameRate(Frames));
    Root->SetNumberField(TEXT("droppedFrames"), DroppedFrames);
    Root->SetStringField(TEXT("stereoLayout"), Settings.StereoLayout == EOmniCaptureStereoLayout::TopBottom ? TEXT("TopBottom") : TEXT("SideBySide"));
    const FIntPoint OutputSize = Settings.GetOutputResolution();
    Root->SetNumberField(TEXT("outputWidth"), OutputSize.X);
    Root->SetNumberField(TEXT("outputHeight"), OutputSize.Y);
    Root->SetStringField(TEXT("outputLayout"), ToLayoutString(Settings));
    Root->SetNumberField(TEXT("longitudeSpanRadians"), Settings.GetLongitudeSpanRadians());
    Root->SetNumberField(TEXT("latitudeSpanRadians"), Settings.GetLatitudeSpanRadians());
    Root->SetBoolField(TEXT("isStereo"), Settings.IsStereo());
    Root->SetBoolField(TEXT("isVR180"), Settings.IsVR180());
    Root->SetNumberField(TEXT("horizontalFOVDegrees"), Settings.GetHorizontalFOVDegrees());
    Root->SetNumberField(TEXT("verticalFOVDegrees"), Settings.GetVerticalFOVDegrees());
    Root->SetStringField(TEXT("stereoMode"), Settings.GetStereoModeMetadataTag());
    Root->SetNumberField(TEXT("encoderAlignment"), Settings.GetEncoderAlignmentRequirement());
    const FIntPoint EyeSize = Settings.GetPerEyeOutputResolution();
    Root->SetNumberField(TEXT("perEyeWidth"), EyeSize.X);
    Root->SetNumberField(TEXT("perEyeHeight"), EyeSize.Y);

    if (Settings.AuxiliaryPasses.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> AuxLayers;
        for (EOmniCaptureAuxiliaryPassType Pass : Settings.AuxiliaryPasses)
        {
            if (Pass == EOmniCaptureAuxiliaryPassType::None)
            {
                continue;
            }

            AuxLayers.Add(MakeShared<FJsonValueString>(GetAuxiliaryLayerName(Pass).ToString()));
        }

        if (AuxLayers.Num() > 0)
        {
            Root->SetArrayField(TEXT("auxiliaryLayers"), AuxLayers);
        }
    }

    const bool bHalfSphere = Settings.IsVR180();
    const int32 FullPanoWidth = bHalfSphere ? OutputSize.X * 2 : OutputSize.X;
    const int32 FullPanoHeight = OutputSize.Y;
    const int32 CroppedLeft = bHalfSphere ? (FullPanoWidth - OutputSize.X) / 2 : 0;
    const int32 CroppedTop = 0;

    TSharedRef<FJsonObject> GPano = MakeShared<FJsonObject>();
    GPano->SetStringField(TEXT("projectionType"), TEXT("equirectangular"));
    GPano->SetStringField(TEXT("stereoMode"), Settings.GetStereoModeMetadataTag());
    GPano->SetNumberField(TEXT("fullPanoWidthPixels"), FullPanoWidth);
    GPano->SetNumberField(TEXT("fullPanoHeightPixels"), FullPanoHeight);
    GPano->SetNumberField(TEXT("croppedAreaImageWidthPixels"), OutputSize.X);
    GPano->SetNumberField(TEXT("croppedAreaImageHeightPixels"), OutputSize.Y);
    GPano->SetNumberField(TEXT("croppedAreaLeftPixels"), CroppedLeft);
    GPano->SetNumberField(TEXT("croppedAreaTopPixels"), CroppedTop);
    GPano->SetNumberField(TEXT("initialHorizontalFOVDegrees"), Settings.GetHorizontalFOVDegrees());
    GPano->SetNumberField(TEXT("initialVerticalFOVDegrees"), Settings.GetVerticalFOVDegrees());
    GPano->SetNumberField(TEXT("initialViewHeadingDegrees"), 0.0);
    GPano->SetNumberField(TEXT("initialViewPitchDegrees"), 0.0);
    GPano->SetNumberField(TEXT("initialViewRollDegrees"), 0.0);
    Root->SetObjectField(TEXT("gpano"), GPano);

    switch (Settings.ColorSpace)
    {
    case EOmniCaptureColorSpace::BT2020:
        Root->SetStringField(TEXT("colorSpace"), TEXT("BT.2020"));
        break;
    case EOmniCaptureColorSpace::HDR10:
        Root->SetStringField(TEXT("colorSpace"), TEXT("HDR10"));
        break;
    default:
        Root->SetStringField(TEXT("colorSpace"), TEXT("BT.709"));
        break;
    }

    Root->SetStringField(TEXT("audio"), AudioPath);
    const FString FinalVideo = OutputDirectory / (BaseFileName + TEXT(".mp4"));
    Root->SetStringField(TEXT("videoFile"), FinalVideo);
    if (!VideoPath.IsEmpty())
    {
        Root->SetStringField(TEXT("nvencBitstream"), VideoPath);
    }
    Root->SetBoolField(TEXT("zeroCopy"), Settings.bZeroCopy);
    if (const UEnum* InteropEnum = StaticEnum<EOmniCaptureNVENCD3D12Interop>())
    {
        Root->SetStringField(TEXT("d3d12Interop"), InteropEnum->GetNameStringByValue(static_cast<int64>(Settings.D3D12InteropMode)));
    }
    Root->SetStringField(TEXT("codec"), Settings.Codec == EOmniCaptureCodec::HEVC ? TEXT("HEVC") : TEXT("H264"));

    switch (Settings.NVENCColorFormat)
    {
    case EOmniCaptureColorFormat::NV12:
        Root->SetStringField(TEXT("nvencColorFormat"), TEXT("NV12"));
        break;
    case EOmniCaptureColorFormat::P010:
        Root->SetStringField(TEXT("nvencColorFormat"), TEXT("P010"));
        break;
    case EOmniCaptureColorFormat::BGRA:
        Root->SetStringField(TEXT("nvencColorFormat"), TEXT("BGRA"));
        break;
    }

    TArray<TSharedPtr<FJsonValue>> FrameArray;
    FrameArray.Reserve(Frames.Num());
    for (const FOmniCaptureFrameMetadata& Metadata : Frames)
    {
        TSharedRef<FJsonObject> FrameObject = MakeShared<FJsonObject>();
        FrameObject->SetNumberField(TEXT("index"), Metadata.FrameIndex);
        FrameObject->SetNumberField(TEXT("timecode"), Metadata.Timecode);
        FrameObject->SetBoolField(TEXT("keyFrame"), Metadata.bKeyFrame);
        FrameArray.Add(MakeShared<FJsonValueObject>(FrameObject));
    }
    Root->SetArrayField(TEXT("frames"), FrameArray);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        return false;
    }

    OutManifestPath = OutputDirectory / (BaseFileName + TEXT("_Manifest.json"));
    return FFileHelper::SaveStringToFile(OutputString, *OutManifestPath);
}

bool FOmniCaptureMuxer::WriteSpatialMetadata(const FOmniCaptureSettings& Settings) const
{
    if (!Settings.bWriteSpatialMetadata && !Settings.bWriteXMPMetadata)
    {
        return true;
    }

    if (!Settings.SupportsSphericalMetadata())
    {
        return true;
    }

    const FIntPoint OutputSize = Settings.GetOutputResolution();
    if (OutputSize.X <= 0 || OutputSize.Y <= 0)
    {
        return false;
    }

    const FString StereoMode = Settings.GetStereoModeMetadataTag();
    const bool bHalfSphere = Settings.IsVR180();
    const FIntPoint EyeSize = Settings.GetPerEyeOutputResolution();
    const int32 FullPanoWidth = bHalfSphere ? OutputSize.X * 2 : OutputSize.X;
    const int32 FullPanoHeight = OutputSize.Y;
    const int32 CroppedLeft = bHalfSphere ? (FullPanoWidth - OutputSize.X) / 2 : 0;
    const int32 CroppedTop = 0;

    TSharedRef<FJsonObject> SpatialRoot = MakeShared<FJsonObject>();
    SpatialRoot->SetStringField(TEXT("projection"), bHalfSphere ? TEXT("VR180") : TEXT("VR360"));
    SpatialRoot->SetStringField(TEXT("stereoMode"), StereoMode);
    SpatialRoot->SetBoolField(TEXT("isStereo"), Settings.IsStereo());
    SpatialRoot->SetNumberField(TEXT("frameWidth"), OutputSize.X);
    SpatialRoot->SetNumberField(TEXT("frameHeight"), OutputSize.Y);
    SpatialRoot->SetNumberField(TEXT("perEyeWidth"), EyeSize.X);
    SpatialRoot->SetNumberField(TEXT("perEyeHeight"), EyeSize.Y);
    SpatialRoot->SetNumberField(TEXT("fullPanoWidth"), FullPanoWidth);
    SpatialRoot->SetNumberField(TEXT("fullPanoHeight"), FullPanoHeight);
    SpatialRoot->SetNumberField(TEXT("croppedLeft"), CroppedLeft);
    SpatialRoot->SetNumberField(TEXT("croppedTop"), CroppedTop);
    SpatialRoot->SetNumberField(TEXT("horizontalFOVDegrees"), Settings.GetHorizontalFOVDegrees());
    SpatialRoot->SetNumberField(TEXT("verticalFOVDegrees"), Settings.GetVerticalFOVDegrees());

    bool bSuccess = true;

    if (Settings.bWriteSpatialMetadata)
    {
        FString SpatialJson;
        TSharedRef<TJsonWriter<>> SpatialWriter = TJsonWriterFactory<>::Create(&SpatialJson);
        if (!FJsonSerializer::Serialize(SpatialRoot, SpatialWriter))
        {
            bSuccess = false;
        }
        else
        {
            const FString SpatialPath = OutputDirectory / (BaseFileName + TEXT("_SpatialMetadata.json"));
            if (!FFileHelper::SaveStringToFile(SpatialJson, *SpatialPath))
            {
                bSuccess = false;
            }
        }
    }

    if (!Settings.bWriteXMPMetadata)
    {
        return bSuccess;
    }

    const FString XMPString = FString::Printf(
        TEXT("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")
        TEXT("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n")
        TEXT(" <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n")
        TEXT("  <rdf:Description rdf:about=\"\"\n")
        TEXT("    xmlns:GPano=\"http://ns.google.com/photos/1.0/panorama/\"\n")
        TEXT("    GPano:ProjectionType=\"equirectangular\"\n")
        TEXT("    GPano:StereoMode=\"%s\"\n")
        TEXT("    GPano:StitchingSoftware=\"OmniCapture\"\n")
        TEXT("    GPano:CroppedAreaImageWidthPixels=\"%d\"\n")
        TEXT("    GPano:CroppedAreaImageHeightPixels=\"%d\"\n")
        TEXT("    GPano:CroppedAreaLeftPixels=\"%d\"\n")
        TEXT("    GPano:CroppedAreaTopPixels=\"%d\"\n")
        TEXT("    GPano:FullPanoWidthPixels=\"%d\"\n")
        TEXT("    GPano:FullPanoHeightPixels=\"%d\"\n")
        TEXT("    GPano:InitialViewHeadingDegrees=\"0\"\n")
        TEXT("    GPano:InitialViewPitchDegrees=\"0\"\n")
        TEXT("    GPano:InitialViewRollDegrees=\"0\"\n")
        TEXT("    GPano:InitialHorizontalFOVDegrees=\"%.2f\"\n")
        TEXT("    GPano:InitialVerticalFOVDegrees=\"%.2f\"/>\n")
        TEXT(" </rdf:RDF>\n")
        TEXT("</x:xmpmeta>\n"),
        *StereoMode,
        OutputSize.X,
        OutputSize.Y,
        CroppedLeft,
        CroppedTop,
        FullPanoWidth,
        FullPanoHeight,
        static_cast<double>(Settings.GetHorizontalFOVDegrees()),
        static_cast<double>(Settings.GetVerticalFOVDegrees()));

    const FString XMPPath = OutputDirectory / (BaseFileName + TEXT("_VRMetadata.xmp"));
    if (!FFileHelper::SaveStringToFile(XMPString, *XMPPath))
    {
        bSuccess = false;
    }

    return bSuccess;
}

bool FOmniCaptureMuxer::TryInvokeFFmpeg(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath) const
{
    if (Frames.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No frames captured; skipping FFmpeg mux."));
        return false;
    }

    const bool bImageSequenceOutput = IsImageSequenceFormat(Settings.OutputFormat);

    const FString Binary = CachedFFmpegPath.IsEmpty() ? BuildFFmpegBinaryPath() : CachedFFmpegPath;
    if (Binary.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("FFmpeg not configured. Skipping automatic muxing."));
        return bImageSequenceOutput;
    }
    if (!Binary.Equals(TEXT("ffmpeg"), ESearchCase::IgnoreCase) && !FPaths::FileExists(Binary))
    {
        UE_LOG(LogTemp, Warning, TEXT("FFmpeg binary %s was not found on disk."), *Binary);
        return bImageSequenceOutput;
    }

    const double FrameRate = CalculateFrameRate(Frames);
    const double EffectiveFrameRate = FrameRate <= 0.0 ? 30.0 : FrameRate;

    FString ColorSpaceArg = TEXT("bt709");
    FString ColorPrimariesArg = TEXT("bt709");
    FString ColorTransferArg = TEXT("bt709");
    FString PixelFormatArg = TEXT("yuv420p");

    switch (Settings.ColorSpace)
    {
    case EOmniCaptureColorSpace::BT2020:
        ColorSpaceArg = TEXT("bt2020nc");
        ColorPrimariesArg = TEXT("bt2020");
        ColorTransferArg = TEXT("bt2020-10");
        PixelFormatArg = TEXT("yuv420p10le");
        break;
    case EOmniCaptureColorSpace::HDR10:
        ColorSpaceArg = TEXT("bt2020nc");
        ColorPrimariesArg = TEXT("bt2020");
        ColorTransferArg = TEXT("smpte2084");
        PixelFormatArg = TEXT("yuv420p10le");
        break;
    default:
        break;
    }

    FString OutputFile = OutputDirectory / (BaseFileName + TEXT(".mp4"));
    FString CommandLine;

    if (bImageSequenceOutput)
    {
        const FString Extension = Settings.GetImageFileExtension();
        FString Pattern = OutputDirectory / FString::Printf(TEXT("%s_%%06d%s"), *BaseFileName, *Extension);
        CommandLine = FString::Printf(TEXT("-y -framerate %.3f -i \"%s\""), EffectiveFrameRate, *Pattern);
    }
    else if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        const FString BitstreamPath = !VideoPath.IsEmpty() ? VideoPath : (OutputDirectory / (BaseFileName + TEXT(".h264")));
        if (!FPaths::FileExists(BitstreamPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("NVENC bitstream %s not found; skipping FFmpeg mux."), *BitstreamPath);
            return false;
        }
        CommandLine = FString::Printf(TEXT("-y -framerate %.3f -i \"%s\""), EffectiveFrameRate, *BitstreamPath);
    }
    else
    {
        return false;
    }

    if (!AudioPath.IsEmpty() && FPaths::FileExists(AudioPath))
    {
        CommandLine += FString::Printf(TEXT(" -i \"%s\" -c:a aac -b:a 192k"), *AudioPath);
    }
    else
    {
        CommandLine += TEXT(" -an");
        if (!AudioPath.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("Audio file %s was not found; muxed output will be silent."), *AudioPath);
        }
    }

    const FString StereoModeTag = Settings.GetStereoModeMetadataTag();
    const TCHAR* StereoMode = *StereoModeTag;
    const bool bHalfSphere = Settings.IsVR180();
    const FIntPoint OutputSize = Settings.GetOutputResolution();
    const int32 FullPanoWidth = bHalfSphere ? OutputSize.X * 2 : OutputSize.X;
    const int32 FullPanoHeight = OutputSize.Y;
    const int32 CroppedLeft = bHalfSphere ? (FullPanoWidth - OutputSize.X) / 2 : 0;
    const int32 CroppedTop = 0;
    const TCHAR* ViewTag = bHalfSphere ? TEXT("VR180") : TEXT("VR360");

    if (IsImageSequenceFormat(Settings.OutputFormat))
    {
        const TCHAR* CodecName = Settings.Codec == EOmniCaptureCodec::HEVC ? TEXT("libx265") : TEXT("libx264");
        CommandLine += FString::Printf(TEXT(" -c:v %s -pix_fmt %s"), CodecName, *PixelFormatArg);
    }
    else if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        CommandLine += TEXT(" -c:v copy");
    }

    if (Settings.bInjectFFmpegMetadata && Settings.SupportsSphericalMetadata())
    {
        FString MetadataArgs = FString::Printf(TEXT(" -metadata:s:v:0 spherical_video=1 -metadata:s:v:0 projection=equirectangular -metadata:s:v:0 stereo_mode=%s"), StereoMode);
        MetadataArgs += TEXT(" -metadata:s:v:0 spatial_audio=0 -metadata:s:v:0 stitching_software=OmniCapture");
        MetadataArgs += TEXT(" -metadata:s:v:0 projection_pose_yaw_degrees=0 -metadata:s:v:0 projection_pose_pitch_degrees=0 -metadata:s:v:0 projection_pose_roll_degrees=0");
        if (bHalfSphere)
        {
            MetadataArgs += TEXT(" -metadata:s:v:0 bound_left=-90 -metadata:s:v:0 bound_right=90 -metadata:s:v:0 bound_top=90 -metadata:s:v:0 bound_bottom=-90");
        }
        else
        {
            MetadataArgs += TEXT(" -metadata:s:v:0 bound_left=-180 -metadata:s:v:0 bound_right=180 -metadata:s:v:0 bound_top=90 -metadata:s:v:0 bound_bottom=-90");
        }
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 view=%s"), ViewTag);
        MetadataArgs += TEXT(" -metadata:s:v:0 spherical=1");
        MetadataArgs += TEXT(" -metadata:s:v:0 gpano:ProjectionType=equirectangular");
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:StereoMode=%s"), StereoMode);
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:FullPanoWidthPixels=%d"), FullPanoWidth);
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:FullPanoHeightPixels=%d"), FullPanoHeight);
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:CroppedAreaImageWidthPixels=%d"), OutputSize.X);
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:CroppedAreaImageHeightPixels=%d"), OutputSize.Y);
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:CroppedAreaLeftPixels=%d"), CroppedLeft);
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:CroppedAreaTopPixels=%d"), CroppedTop);
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:InitialHorizontalFOVDegrees=%.2f"), static_cast<double>(Settings.GetHorizontalFOVDegrees()));
        MetadataArgs += FString::Printf(TEXT(" -metadata:s:v:0 gpano:InitialVerticalFOVDegrees=%.2f"), static_cast<double>(Settings.GetVerticalFOVDegrees()));
        CommandLine += MetadataArgs;
    }
    CommandLine += FString::Printf(TEXT(" -colorspace %s -color_primaries %s -color_trc %s"), *ColorSpaceArg, *ColorPrimariesArg, *ColorTransferArg);

    if (Settings.bForceConstantFrameRate)
    {
        CommandLine += TEXT(" -vsync cfr");
    }
    if (Settings.bEnableFastStart)
    {
        CommandLine += TEXT(" -movflags +faststart");
    }

    CommandLine += FString::Printf(TEXT(" -shortest \"%s\""), *OutputFile);

    UE_LOG(LogTemp, Log, TEXT("Invoking FFmpeg: %s %s"), *Binary, *CommandLine);

    FProcHandle ProcHandle = FPlatformProcess::CreateProc(*Binary, *CommandLine, true, true, true, nullptr, 0, *OutputDirectory, nullptr);
    if (!ProcHandle.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to launch FFmpeg process."));
        return false;
    }

    FPlatformProcess::WaitForProc(ProcHandle);
    int32 ReturnCode = 0;
    FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
    if (ReturnCode != 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FFmpeg returned non-zero exit code %d"), ReturnCode);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("FFmpeg muxing complete: %s"), *OutputFile);
    return true;
}

FString FOmniCaptureMuxer::BuildFFmpegBinaryPath() const
{
    return ResolveFFmpegBinary(FOmniCaptureSettings());
}

double FOmniCaptureMuxer::CalculateFrameRate(const TArray<FOmniCaptureFrameMetadata>& Frames) const
{
    if (Frames.Num() < 2)
    {
        return 30.0;
    }
    double Duration = Frames.Last().Timecode - Frames[0].Timecode;
    if (Duration <= 0.0)
    {
        return 30.0;
    }
    return static_cast<double>(Frames.Num() - 1) / Duration;
}
