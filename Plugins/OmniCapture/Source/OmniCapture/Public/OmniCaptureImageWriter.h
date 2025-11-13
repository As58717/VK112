#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"
#include "Async/Future.h"
#include "Templates/Function.h"
#include "ImageWriteTypes.h"

class OMNICAPTURE_API FOmniCaptureImageWriter
{
public:
    FOmniCaptureImageWriter();
    ~FOmniCaptureImageWriter();

    void Initialize(const FOmniCaptureSettings& Settings, const FString& InOutputDirectory);
    void EnqueueFrame(TUniquePtr<FOmniCaptureFrame>&& Frame, const FString& FrameFileName);
    void Flush();
    const TArray<FOmniCaptureFrameMetadata>& GetCapturedFrames() const { return CapturedMetadata; }
    TArray<FOmniCaptureFrameMetadata> ConsumeCapturedFrames();

private:
    struct FExrLayerRequest
    {
        FString Name;
        TUniquePtr<FImagePixelData> PixelData;
        bool bLinear = false;
        EOmniCapturePixelPrecision Precision = EOmniCapturePixelPrecision::Unknown;
        EOmniCapturePixelDataType PixelDataType = EOmniCapturePixelDataType::Unknown;
    };

    bool WritePixelDataToDisk(TUniquePtr<FImagePixelData> PixelData, const FString& FilePath, EOmniCaptureImageFormat Format, bool bIsLinear, EOmniCapturePixelPrecision PixelPrecision, EOmniCapturePixelDataType PixelDataType) const;
    bool WritePNGRaw(const FString& FilePath, const FIntPoint& Size, const void* RawData, int64 RawSizeInBytes, ERGBFormat Format, int32 BitDepth) const;
    bool WritePNGWithRowSource(const FString& FilePath, const FIntPoint& Size, ERGBFormat Format, int32 BitDepth, TFunctionRef<void(int32 RowStart, int32 RowCount, int64 BytesPerRow, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)> PrepareRows) const;
    bool WritePNG(const TImagePixelData<FColor>& PixelData, const FString& FilePath) const;
    bool WritePNGFromLinear(const TImagePixelData<FFloat16Color>& PixelData, const FString& FilePath) const;
    bool WritePNGFromLinearFloat32(const TImagePixelData<FLinearColor>& PixelData, const FString& FilePath) const;
    bool WriteBMP(const TImagePixelData<FColor>& PixelData, const FString& FilePath) const;
    bool WriteBMPFromLinear(const TImagePixelData<FFloat16Color>& PixelData, const FString& FilePath) const;
    bool WriteBMPFromLinearFloat32(const TImagePixelData<FLinearColor>& PixelData, const FString& FilePath) const;
    bool WriteJPEG(const TImagePixelData<FColor>& PixelData, const FString& FilePath) const;
    bool WriteJPEGFromLinear(const TImagePixelData<FFloat16Color>& PixelData, const FString& FilePath) const;
    bool WriteJPEGFromLinearFloat32(const TImagePixelData<FLinearColor>& PixelData, const FString& FilePath) const;
    bool WriteEXR(TUniquePtr<FImagePixelData> PixelData, const FString& FilePath, EOmniCapturePixelPrecision PixelPrecision, EOmniCapturePixelDataType PixelDataType) const;
    bool WriteEXRFromColor(const TImagePixelData<FColor>& PixelData, const FString& FilePath) const;
    bool WriteEXRInternal(TUniquePtr<FImagePixelData> PixelData, const FString& FilePath, EImagePixelType PixelType) const;
    bool WriteEXRFrame(const FString& FilePath, bool bIsLinear, TUniquePtr<FImagePixelData> PixelData, EOmniCapturePixelPrecision PixelPrecision, EOmniCapturePixelDataType PixelDataType, TMap<FName, FOmniCaptureLayerPayload>&& AuxiliaryLayers, const FString& LayerDirectory, const FString& LayerBaseName, const FString& LayerExtension) const;
    bool WriteCombinedEXR(const FString& FilePath, TArray<FExrLayerRequest>& Layers) const;
    void RequestStop();
    bool IsStopRequested() const;
    void WaitForAvailableTaskSlot();
    void TrackPendingTask(TFuture<bool>&& TaskFuture);
    void PruneCompletedTasks();
    void EnforcePendingTaskLimit();
    void WaitForAllTasks();

    bool bInitialized = false;
    FString OutputDirectory;
    FString SequenceBaseName;
    EOmniCaptureImageFormat TargetFormat = EOmniCaptureImageFormat::PNG;
    EOmniCapturePNGBitDepth TargetPNGBitDepth = EOmniCapturePNGBitDepth::BitDepth32;
    int32 MaxPendingTasks = 8;
    bool bPackEXRAuxiliaryLayers = true;
    bool bUseEXRMultiPart = false;
    EOmniCaptureEXRCompression TargetEXRCompression = EOmniCaptureEXRCompression::Zip;

    TArray<FOmniCaptureFrameMetadata> CapturedMetadata;
    FCriticalSection MetadataCS;

    TArray<TFuture<bool>> PendingTasks;
    FCriticalSection PendingTasksCS;
    TAtomic<bool> bStopRequested;
};

