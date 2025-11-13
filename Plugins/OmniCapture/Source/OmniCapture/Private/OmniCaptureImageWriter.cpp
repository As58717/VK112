#include "OmniCaptureImageWriter.h"


#include "Async/Async.h"
#include "Async/Future.h"
#include "HAL/FileManager.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "ImageWriteQueue.h"
#include "ImageWriteTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Containers/StringConv.h"
#include "Internationalization/Internationalization.h"
#include "Math/Vector2D.h"
#include "OmniCaptureVersion.h"

#include <exception>

#ifndef WITH_OMNICAPTURE_OPENEXR
#define WITH_OMNICAPTURE_OPENEXR 0
#endif

THIRD_PARTY_INCLUDES_START
#include "png.h"
THIRD_PARTY_INCLUDES_END

#if WITH_OMNICAPTURE_OPENEXR
THIRD_PARTY_INCLUDES_START
#include "OpenEXR/ImfMultiPartOutputFile.h"
#include "OpenEXR/ImfOutputFile.h"
#include "OpenEXR/ImfOutputPart.h"
#include "OpenEXR/ImfChannelList.h"
#include "OpenEXR/ImfFrameBuffer.h"
#include "OpenEXR/ImfStringAttribute.h"
#include "OpenEXR/ImfCompression.h"
#include "OpenEXR/ImfNamespace.h"
#include "Imath/half.h"
THIRD_PARTY_INCLUDES_END
#endif

namespace
{
    constexpr int32 DefaultJpegQuality = 85;

#if WITH_OMNICAPTURE_OPENEXR
    OPENEXR_IMF_NAMESPACE::Compression ToOpenExrCompression(EOmniCaptureEXRCompression Compression)
    {
        using namespace OPENEXR_IMF_NAMESPACE;
        switch (Compression)
        {
        case EOmniCaptureEXRCompression::None:
            return NO_COMPRESSION;
        case EOmniCaptureEXRCompression::Zips:
            return ZIPS_COMPRESSION;
        case EOmniCaptureEXRCompression::Piz:
            return PIZ_COMPRESSION;
        case EOmniCaptureEXRCompression::Pxr24:
            return PXR24_COMPRESSION;
        case EOmniCaptureEXRCompression::Dwaa:
            return DWAA_COMPRESSION;
        case EOmniCaptureEXRCompression::Dwab:
            return DWAB_COMPRESSION;
        case EOmniCaptureEXRCompression::Rle:
            return RLE_COMPRESSION;
        case EOmniCaptureEXRCompression::Zip:
        default:
            return ZIP_COMPRESSION;
        }
    }

    const TCHAR* GetChannelSuffix(int32 ChannelIndex)
    {
        switch (ChannelIndex)
        {
        case 0: return TEXT("R");
        case 1: return TEXT("G");
        case 2: return TEXT("B");
        case 3: return TEXT("A");
        default: return TEXT("X");
        }
    }

    struct FPreparedExrLayer
    {
        std::string Name;
        OPENEXR_IMF_NAMESPACE::PixelType PixelType = OPENEXR_IMF_NAMESPACE::PixelType::HALF;
        int32 ChannelCount = 4;
        TArray<float> FloatBuffer;
        TArray<IMATH_NAMESPACE::half> HalfBuffer;

        const char* GetBasePointer() const
        {
            if (PixelType == OPENEXR_IMF_NAMESPACE::PixelType::FLOAT)
            {
                return reinterpret_cast<const char*>(FloatBuffer.GetData());
            }

            return reinterpret_cast<const char*>(HalfBuffer.GetData());
        }

        int32 GetComponentSize() const
        {
            return PixelType == OPENEXR_IMF_NAMESPACE::PixelType::FLOAT ? sizeof(float) : sizeof(IMATH_NAMESPACE::half);
        }
    };
#endif

    TSharedPtr<IImageWrapper> CreateImageWrapper(EImageFormat Format)
    {
        IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        return ImageWrapperModule.CreateImageWrapper(Format);
    }

    bool WritePNGWithImageWrapper(const FString& FilePath, const FIntPoint& Size, const void* RawData, int64 RawSizeInBytes, ERGBFormat Format, int32 BitDepth)
    {
        if (!RawData || RawSizeInBytes <= 0 || Size.X <= 0 || Size.Y <= 0)
        {
            return false;
        }

        const TSharedPtr<IImageWrapper> ImageWrapper = CreateImageWrapper(EImageFormat::PNG);
        if (!ImageWrapper.IsValid())
        {
            return false;
        }

        if (!ImageWrapper->SetRaw(static_cast<const uint8*>(RawData), RawSizeInBytes, Size.X, Size.Y, Format, BitDepth))
        {
            return false;
        }

        const TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(0);
        if (CompressedData.Num() == 0)
        {
            return false;
        }

        IFileManager::Get().Delete(*FilePath, false, true, false);
        return FFileHelper::SaveArrayToFile(CompressedData, *FilePath);
    }

    FString NormalizeFilePath(const FString& InPath)
    {
        FString Normalized = InPath;
        FPaths::MakeStandardFilename(Normalized);
        return Normalized;
    }

    int32 GetChannelCountForFormat(ERGBFormat Format)
    {
        switch (Format)
        {
        case ERGBFormat::Gray:
        case ERGBFormat::GrayF:
            return 1;
        case ERGBFormat::RGBA:
        case ERGBFormat::BGRA:
        case ERGBFormat::RGBAF:
            return 4;
        default:
            return 0;
        }
    }

    int32 GetPngColorType(ERGBFormat Format)
    {
        switch (Format)
        {
        case ERGBFormat::Gray:
        case ERGBFormat::GrayF:
            return PNG_COLOR_TYPE_GRAY;
        case ERGBFormat::RGBA:
        case ERGBFormat::BGRA:
        case ERGBFormat::RGBAF:
            return PNG_COLOR_TYPE_RGBA;
        default:
            return -1;
        }
    }

    void PngWriteDataCallback(png_structp PngPtr, png_bytep Data, png_size_t Length)
    {
        FArchive* Archive = static_cast<FArchive*>(png_get_io_ptr(PngPtr));
        if (!Archive)
        {
            png_error(PngPtr, "Invalid archive writer");
            return;
        }

        Archive->Serialize(Data, Length);
        if (Archive->IsError())
        {
            png_error(PngPtr, "Failed to write PNG data");
        }
    }

    void PngFlushCallback(png_structp PngPtr)
    {
        FArchive* Archive = static_cast<FArchive*>(png_get_io_ptr(PngPtr));
        if (Archive)
        {
            Archive->Flush();
        }
    }
}

FOmniCaptureImageWriter::FOmniCaptureImageWriter()
{
    bStopRequested.Store(false);
}
FOmniCaptureImageWriter::~FOmniCaptureImageWriter() { Flush(); }

void FOmniCaptureImageWriter::Initialize(const FOmniCaptureSettings& Settings, const FString& InOutputDirectory)
{
    OutputDirectory = InOutputDirectory.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("OmniCaptures") : InOutputDirectory;
    SequenceBaseName = Settings.OutputFileName;
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    TargetFormat = Settings.ImageFormat;
    TargetPNGBitDepth = Settings.PNGBitDepth;
    MaxPendingTasks = FMath::Max(1, Settings.MaxPendingImageTasks);
    bPackEXRAuxiliaryLayers = Settings.bPackEXRAuxiliaryLayers;
    bUseEXRMultiPart = Settings.bUseEXRMultiPart;
    TargetEXRCompression = Settings.EXRCompression;
    bStopRequested.Store(false);
    bInitialized = true;
}

void FOmniCaptureImageWriter::EnqueueFrame(TUniquePtr<FOmniCaptureFrame>&& Frame, const FString& FrameFileName)
{
    if (!bInitialized || !Frame.IsValid() || IsStopRequested())
    {
        return;
    }

    PruneCompletedTasks();
    WaitForAvailableTaskSlot();

    if (IsStopRequested())
    {
        return;
    }

    FString TargetPath = NormalizeFilePath(OutputDirectory / FrameFileName);
    FOmniCaptureFrameMetadata Metadata = Frame->Metadata;
    bool bIsLinear = Frame->bLinearColor;

    TUniquePtr<FImagePixelData> PixelData = MoveTemp(Frame->PixelData);
    TMap<FName, FOmniCaptureLayerPayload> AuxiliaryLayers = MoveTemp(Frame->AuxiliaryLayers);
    if (!PixelData.IsValid())
    {
        return;
    }

    const EOmniCapturePixelPrecision PixelPrecision = Frame->PixelPrecision;
    const EOmniCapturePixelDataType PixelDataType = Frame->PixelDataType;
    const FString LayerDirectory = FPaths::GetPath(TargetPath);
    const FString LayerBaseName = FPaths::GetBaseFilename(TargetPath);
    const FString LayerExtension = FPaths::GetExtension(TargetPath, true);

    TFuture<bool> Future = Async(EAsyncExecution::ThreadPool, [this, FilePath = MoveTemp(TargetPath), Format = TargetFormat, bIsLinear, PixelPrecision, PixelDataType, PixelData = MoveTemp(PixelData), AuxiliaryLayers = MoveTemp(AuxiliaryLayers), LayerDirectory, LayerBaseName, LayerExtension]() mutable
    {
        if (Format == EOmniCaptureImageFormat::EXR)
        {
            return WriteEXRFrame(FilePath, bIsLinear, MoveTemp(PixelData), PixelPrecision, PixelDataType, MoveTemp(AuxiliaryLayers), LayerDirectory, LayerBaseName, LayerExtension);
        }

        bool bResult = WritePixelDataToDisk(MoveTemp(PixelData), FilePath, Format, bIsLinear, PixelPrecision, PixelDataType);

        for (TPair<FName, FOmniCaptureLayerPayload>& Pair : AuxiliaryLayers)
        {
            if (!Pair.Value.PixelData.IsValid())
            {
                continue;
            }

            const FString LayerFileName = FString::Printf(TEXT("%s_%s%s"), *LayerBaseName, *Pair.Key.ToString(), *LayerExtension);
            const FString LayerPath = FPaths::Combine(LayerDirectory, LayerFileName);
            const bool bLayerLinear = Pair.Value.bLinear;
            const EOmniCapturePixelPrecision LayerPrecision = (Pair.Value.Precision == EOmniCapturePixelPrecision::Unknown) ? PixelPrecision : Pair.Value.Precision;
            EOmniCapturePixelDataType LayerType = Pair.Value.PixelDataType;
            if (LayerType == EOmniCapturePixelDataType::Unknown)
            {
                if (bLayerLinear)
                {
                    LayerType = (LayerPrecision == EOmniCapturePixelPrecision::FullFloat)
                        ? EOmniCapturePixelDataType::LinearColorFloat32
                        : EOmniCapturePixelDataType::LinearColorFloat16;
                }
                else
                {
                    LayerType = EOmniCapturePixelDataType::Color8;
                }
            }
            bResult &= WritePixelDataToDisk(MoveTemp(Pair.Value.PixelData), LayerPath, Format, bLayerLinear, LayerPrecision, LayerType);
        }

        return bResult;
    });

    TrackPendingTask(MoveTemp(Future));
    PruneCompletedTasks();
    EnforcePendingTaskLimit();

    {
        FScopeLock Lock(&MetadataCS);
        CapturedMetadata.Add(Metadata);
    }
}

void FOmniCaptureImageWriter::Flush()
{
    RequestStop();
    PruneCompletedTasks();
    WaitForAllTasks();
    bInitialized = false;
}

TArray<FOmniCaptureFrameMetadata> FOmniCaptureImageWriter::ConsumeCapturedFrames()
{
    FScopeLock Lock(&MetadataCS);
    TArray<FOmniCaptureFrameMetadata> Result = MoveTemp(CapturedMetadata);
    CapturedMetadata.Reset();
    return Result;
}

bool FOmniCaptureImageWriter::WritePixelDataToDisk(TUniquePtr<FImagePixelData> PixelData, const FString& FilePath, EOmniCaptureImageFormat Format, bool bIsLinear, EOmniCapturePixelPrecision PixelPrecision, EOmniCapturePixelDataType PixelDataType) const
{
    if (!PixelData.IsValid())
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    EOmniCapturePixelDataType EffectiveType = PixelDataType;

    if (Format != EOmniCaptureImageFormat::EXR)
    {
        if (EffectiveType == EOmniCapturePixelDataType::ScalarFloat32)
        {
            const TImagePixelData<float>* ScalarData = static_cast<const TImagePixelData<float>*>(PixelData.Get());
            const FIntPoint Size = ScalarData->GetSize();
            const int32 PixelCount = Size.X * Size.Y;
            TUniquePtr<TImagePixelData<FLinearColor>> Expanded = MakeUnique<TImagePixelData<FLinearColor>>(Size);
            Expanded->Pixels.SetNum(PixelCount);
            for (int32 Index = 0; Index < PixelCount; ++Index)
            {
                const float Value = ScalarData->Pixels[Index];
                Expanded->Pixels[Index] = FLinearColor(Value, Value, Value, Value);
            }

            PixelData = MoveTemp(Expanded);
            PixelPrecision = EOmniCapturePixelPrecision::FullFloat;
            bIsLinear = true;
            EffectiveType = EOmniCapturePixelDataType::LinearColorFloat32;
        }
        else if (EffectiveType == EOmniCapturePixelDataType::Vector2Float32)
        {
            const TImagePixelData<FVector2f>* VectorData = static_cast<const TImagePixelData<FVector2f>*>(PixelData.Get());
            const FIntPoint Size = VectorData->GetSize();
            const int32 PixelCount = Size.X * Size.Y;
            TUniquePtr<TImagePixelData<FLinearColor>> Expanded = MakeUnique<TImagePixelData<FLinearColor>>(Size);
            Expanded->Pixels.SetNum(PixelCount);
            for (int32 Index = 0; Index < PixelCount; ++Index)
            {
                const FVector2f& Value = VectorData->Pixels[Index];
                Expanded->Pixels[Index] = FLinearColor(Value.X, Value.Y, 0.0f, 0.0f);
            }

            PixelData = MoveTemp(Expanded);
            PixelPrecision = EOmniCapturePixelPrecision::FullFloat;
            bIsLinear = true;
            EffectiveType = EOmniCapturePixelDataType::LinearColorFloat32;
        }
    }

    bool bWriteSuccessful = false;

    const auto RequireType = [&](EOmniCapturePixelDataType ExpectedType) -> bool
    {
        if (EffectiveType != ExpectedType)
        {
            UE_LOG(LogTemp, Warning, TEXT("Pixel data type mismatch while writing '%s' (expected %d, got %d)"), *FilePath, static_cast<int32>(ExpectedType), static_cast<int32>(EffectiveType));
            return false;
        }
        return true;
    };

    switch (Format)
    {
    case EOmniCaptureImageFormat::JPG:
        if (bIsLinear)
        {
            if (PixelPrecision == EOmniCapturePixelPrecision::FullFloat)
            {
                if (RequireType(EOmniCapturePixelDataType::LinearColorFloat32))
                {
                    const TImagePixelData<FLinearColor>* FloatData = static_cast<const TImagePixelData<FLinearColor>*>(PixelData.Get());
                    bWriteSuccessful = WriteJPEGFromLinearFloat32(*FloatData, FilePath);
                }
            }
            else
            {
                if (RequireType(EOmniCapturePixelDataType::LinearColorFloat16))
                {
                    const TImagePixelData<FFloat16Color>* FloatData = static_cast<const TImagePixelData<FFloat16Color>*>(PixelData.Get());
                    bWriteSuccessful = WriteJPEGFromLinear(*FloatData, FilePath);
                }
            }
        }
        else
        {
            if (RequireType(EOmniCapturePixelDataType::Color8))
            {
                const TImagePixelData<FColor>* ColorData = static_cast<const TImagePixelData<FColor>*>(PixelData.Get());
                bWriteSuccessful = WriteJPEG(*ColorData, FilePath);
            }
        }
        break;
    case EOmniCaptureImageFormat::EXR:
        if (bIsLinear)
        {
            bWriteSuccessful = WriteEXR(MoveTemp(PixelData), FilePath, PixelPrecision, EffectiveType);
        }
        else
        {
            if (RequireType(EOmniCapturePixelDataType::Color8))
            {
                const TImagePixelData<FColor>* SRGBData = static_cast<const TImagePixelData<FColor>*>(PixelData.Get());
                bWriteSuccessful = WriteEXRFromColor(*SRGBData, FilePath);
            }
        }
        break;
    case EOmniCaptureImageFormat::BMP:
        if (bIsLinear)
        {
            if (PixelPrecision == EOmniCapturePixelPrecision::FullFloat)
            {
                if (RequireType(EOmniCapturePixelDataType::LinearColorFloat32))
                {
                    const TImagePixelData<FLinearColor>* LinearBMP = static_cast<const TImagePixelData<FLinearColor>*>(PixelData.Get());
                    bWriteSuccessful = WriteBMPFromLinearFloat32(*LinearBMP, FilePath);
                }
            }
            else
            {
                if (RequireType(EOmniCapturePixelDataType::LinearColorFloat16))
                {
                    const TImagePixelData<FFloat16Color>* LinearBMP = static_cast<const TImagePixelData<FFloat16Color>*>(PixelData.Get());
                    bWriteSuccessful = WriteBMPFromLinear(*LinearBMP, FilePath);
                }
            }
        }
        else
        {
            if (RequireType(EOmniCapturePixelDataType::Color8))
            {
                const TImagePixelData<FColor>* BmpColor = static_cast<const TImagePixelData<FColor>*>(PixelData.Get());
                bWriteSuccessful = WriteBMP(*BmpColor, FilePath);
            }
        }
        break;
    case EOmniCaptureImageFormat::PNG:
    default:
        if (bIsLinear)
        {
            if (PixelPrecision == EOmniCapturePixelPrecision::FullFloat)
            {
                if (RequireType(EOmniCapturePixelDataType::LinearColorFloat32))
                {
                    const TImagePixelData<FLinearColor>* LinearData = static_cast<const TImagePixelData<FLinearColor>*>(PixelData.Get());
                    bWriteSuccessful = WritePNGFromLinearFloat32(*LinearData, FilePath);
                }
            }
            else
            {
                if (RequireType(EOmniCapturePixelDataType::LinearColorFloat16))
                {
                    const TImagePixelData<FFloat16Color>* LinearData = static_cast<const TImagePixelData<FFloat16Color>*>(PixelData.Get());
                    bWriteSuccessful = WritePNGFromLinear(*LinearData, FilePath);
                }
            }
        }
        else
        {
            if (RequireType(EOmniCapturePixelDataType::Color8))
            {
                const TImagePixelData<FColor>* PngData = static_cast<const TImagePixelData<FColor>*>(PixelData.Get());
                bWriteSuccessful = WritePNG(*PngData, FilePath);
            }
        }
        break;
    }

    if (!bWriteSuccessful)
    {
        UE_LOG(LogTemp, Warning, TEXT("Unsupported pixel data type for OmniCapture image export (%s)"), *FilePath);
    }

    return bWriteSuccessful;
}

bool FOmniCaptureImageWriter::WritePNGRaw(const FString& FilePath, const FIntPoint& Size, const void* RawData, int64 RawSizeInBytes, ERGBFormat Format, int32 BitDepth) const
{
    const int32 Channels = GetChannelCountForFormat(Format);
    if (Channels <= 0 || Size.X <= 0 || Size.Y <= 0)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    const int32 BytesPerChannel = BitDepth / 8;
    if (BytesPerChannel <= 0)
    {
        return false;
    }

    const int64 BytesPerRow = static_cast<int64>(Size.X) * Channels * BytesPerChannel;
    if (BytesPerRow <= 0)
    {
        return false;
    }

    if (RawSizeInBytes < BytesPerRow * Size.Y)
    {
        return false;
    }

    const uint8* BasePtr = static_cast<const uint8*>(RawData);
    auto PrepareRows = [BasePtr, BytesPerRow](int32 RowStart, int32 RowCount, int64, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)
    {
        (void)TempBuffer;
        for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
        {
            RowPointers[RowIndex] = const_cast<uint8*>(BasePtr + (static_cast<int64>(RowStart + RowIndex) * BytesPerRow));
        }
    };

    if (WritePNGWithRowSource(FilePath, Size, Format, BitDepth, PrepareRows))
    {
        return true;
    }

    if (BitDepth == 8)
    {
        return WritePNGWithImageWrapper(FilePath, Size, RawData, RawSizeInBytes, Format, BitDepth);
    }

    return false;
}

bool FOmniCaptureImageWriter::WritePNGWithRowSource(const FString& FilePath, const FIntPoint& Size, ERGBFormat Format, int32 BitDepth, TFunctionRef<void(int32 RowStart, int32 RowCount, int64 BytesPerRow, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)> PrepareRows) const
{
#if WITH_LIBPNG
    const int32 Channels = GetChannelCountForFormat(Format);
    if (Channels <= 0 || BitDepth <= 0 || Size.X <= 0 || Size.Y <= 0)
    {
        return false;
    }

    const int32 ColorType = GetPngColorType(Format);
    if (ColorType < 0)
    {
        return false;
    }

    const int32 BytesPerChannel = BitDepth / 8;
    if (BytesPerChannel <= 0)
    {
        return false;
    }

    const int64 BytesPerRow = static_cast<int64>(Size.X) * Channels * BytesPerChannel;
    if (BytesPerRow <= 0)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    IFileManager::Get().Delete(*FilePath, false, true, false);
    TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(*FilePath));
    if (!Archive.IsValid())
    {
        return false;
    }

    png_structp PngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!PngPtr)
    {
        Archive->Close();
        return false;
    }

    png_infop InfoPtr = png_create_info_struct(PngPtr);
    if (!InfoPtr)
    {
        png_destroy_write_struct(&PngPtr, nullptr);
        Archive->Close();
        return false;
    }

    if (setjmp(png_jmpbuf(PngPtr)))
    {
        png_destroy_write_struct(&PngPtr, &InfoPtr);
        Archive->Close();
        IFileManager::Get().Delete(*FilePath, false, true, true);
        return false;
    }

    png_set_write_fn(PngPtr, Archive.Get(), PngWriteDataCallback, PngFlushCallback);
    png_set_IHDR(PngPtr, InfoPtr, Size.X, Size.Y, BitDepth, ColorType, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    if (BitDepth == 16)
    {
        png_set_swap(PngPtr);
    }

    if (Format == ERGBFormat::BGRA)
    {
        png_set_bgr(PngPtr);
    }

    png_write_info(PngPtr, InfoPtr);

    const int64 DesiredChunkBytes = 64ll * 1024ll * 1024ll;
    const int64 SafeRowSize = FMath::Max<int64>(BytesPerRow, 1);
    const int32 MaxRowsPerChunk = FMath::Max<int32>(1, static_cast<int32>(FMath::Min<int64>(Size.Y, DesiredChunkBytes / SafeRowSize)));

    TArray64<uint8> TempBuffer;
    TArray<uint8*> RowPointers;
    RowPointers.Reserve(MaxRowsPerChunk);

    int32 RowIndex = 0;
    while (RowIndex < Size.Y)
    {
        if (IsStopRequested())
        {
            png_destroy_write_struct(&PngPtr, &InfoPtr);
            Archive->Close();
            IFileManager::Get().Delete(*FilePath, false, true, true);
            return false;
        }

        const int32 RowsThisPass = FMath::Min(MaxRowsPerChunk, Size.Y - RowIndex);
        RowPointers.SetNum(RowsThisPass, EAllowShrinking::No);
        PrepareRows(RowIndex, RowsThisPass, BytesPerRow, TempBuffer, RowPointers);
        png_write_rows(PngPtr, reinterpret_cast<png_bytep*>(RowPointers.GetData()), RowsThisPass);
        RowIndex += RowsThisPass;
    }

    png_write_end(PngPtr, InfoPtr);
    png_destroy_write_struct(&PngPtr, &InfoPtr);

    Archive->Close();
    return !Archive->IsError();
#else
    return false;
#endif
}

bool FOmniCaptureImageWriter::WritePNG(const TImagePixelData<FColor>& PixelData, const FString& FilePath) const
{
    const FIntPoint Size = PixelData.GetSize();
    const TArray64<FColor>& Pixels = PixelData.Pixels;
    if (Pixels.Num() != Size.X * Size.Y)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    if (TargetPNGBitDepth == EOmniCapturePNGBitDepth::BitDepth16)
    {
        auto PrepareRows = [&Pixels, &Size](int32 RowStart, int32 RowCount, int64 BytesPerRow, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)
        {
            const int64 RequiredSize = BytesPerRow * RowCount;
            TempBuffer.SetNum(RequiredSize, EAllowShrinking::No);

            for (int32 Row = 0; Row < RowCount; ++Row)
            {
                uint8* RowData = TempBuffer.GetData() + BytesPerRow * Row;
                RowPointers[Row] = RowData;
                uint16* Dest = reinterpret_cast<uint16*>(RowData);
                const int64 PixelRowStart = static_cast<int64>(RowStart + Row) * Size.X;
                for (int32 Column = 0; Column < Size.X; ++Column)
                {
                    const FColor& Pixel = Pixels[PixelRowStart + Column];
                    *Dest++ = static_cast<uint16>(Pixel.B) * 257u;
                    *Dest++ = static_cast<uint16>(Pixel.G) * 257u;
                    *Dest++ = static_cast<uint16>(Pixel.R) * 257u;
                    *Dest++ = static_cast<uint16>(Pixel.A) * 257u;
                }
            }
        };

        return WritePNGWithRowSource(FilePath, Size, ERGBFormat::BGRA, 16, PrepareRows);
    }

    if (TargetPNGBitDepth == EOmniCapturePNGBitDepth::BitDepth8)
    {
        auto PrepareRows = [&Pixels, &Size](int32 RowStart, int32 RowCount, int64 BytesPerRow, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)
        {
            const int64 RequiredSize = BytesPerRow * RowCount;
            TempBuffer.SetNum(RequiredSize, EAllowShrinking::No);

            for (int32 Row = 0; Row < RowCount; ++Row)
            {
                uint8* RowData = TempBuffer.GetData() + BytesPerRow * Row;
                RowPointers[Row] = RowData;
                const int64 PixelRowStart = static_cast<int64>(RowStart + Row) * Size.X;
                const uint8* Source = reinterpret_cast<const uint8*>(Pixels.GetData() + PixelRowStart);
                FMemory::Memcpy(RowData, Source, BytesPerRow);
            }
        };

        return WritePNGWithRowSource(FilePath, Size, ERGBFormat::BGRA, 8, PrepareRows);
    }

    return WritePNGRaw(FilePath, Size, Pixels.GetData(), Pixels.Num() * sizeof(FColor), ERGBFormat::BGRA, 8);
}

bool FOmniCaptureImageWriter::WriteBMP(const TImagePixelData<FColor>& PixelData, const FString& FilePath) const
{
    const TSharedPtr<IImageWrapper> ImageWrapper = CreateImageWrapper(EImageFormat::BMP);
    if (!ImageWrapper.IsValid())
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    const FIntPoint Size = PixelData.GetSize();
    const TArray64<FColor>& Pixels = PixelData.Pixels;
    if (Pixels.Num() != Size.X * Size.Y)
    {
        return false;
    }

    if (!ImageWrapper->SetRaw(reinterpret_cast<const uint8*>(Pixels.GetData()), Pixels.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8))
    {
        return false;
    }

    const TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(0);
    if (CompressedData.Num() == 0)
    {
        return false;
    }

    IFileManager::Get().Delete(*FilePath, false, true, false);
    return FFileHelper::SaveArrayToFile(CompressedData, *FilePath);
}

bool FOmniCaptureImageWriter::WritePNGFromLinear(const TImagePixelData<FFloat16Color>& PixelData, const FString& FilePath) const
{
    const FIntPoint Size = PixelData.GetSize();
    const int32 ExpectedCount = Size.X * Size.Y;
    if (PixelData.Pixels.Num() != ExpectedCount)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    if (TargetPNGBitDepth == EOmniCapturePNGBitDepth::BitDepth16)
    {
        auto PrepareRows = [&PixelData, &Size](int32 RowStart, int32 RowCount, int64 BytesPerRow, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)
        {
            const int64 RequiredSize = BytesPerRow * RowCount;
            TempBuffer.SetNum(RequiredSize, EAllowShrinking::No);

            const auto ToUInt16 = [](float Value) -> uint16
            {
                const float Clamped = FMath::Clamp(Value, 0.0f, 1.0f);
                return static_cast<uint16>(FMath::RoundToInt(Clamped * 65535.0f));
            };

            for (int32 Row = 0; Row < RowCount; ++Row)
            {
                uint8* RowData = TempBuffer.GetData() + BytesPerRow * Row;
                RowPointers[Row] = RowData;
                uint16* Dest = reinterpret_cast<uint16*>(RowData);
                const int64 PixelRowStart = static_cast<int64>(RowStart + Row) * Size.X;
                for (int32 Column = 0; Column < Size.X; ++Column)
                {
                    const FFloat16Color& Pixel = PixelData.Pixels[PixelRowStart + Column];
                    *Dest++ = ToUInt16(Pixel.B.GetFloat());
                    *Dest++ = ToUInt16(Pixel.G.GetFloat());
                    *Dest++ = ToUInt16(Pixel.R.GetFloat());
                    *Dest++ = ToUInt16(Pixel.A.GetFloat());
                }
            }
        };

        return WritePNGWithRowSource(FilePath, Size, ERGBFormat::BGRA, 16, PrepareRows);
    }

    const int64 PixelCount = static_cast<int64>(Size.X) * Size.Y;
    const int64 BytesPerRow = static_cast<int64>(Size.X) * 4;
    TArray64<uint8> ConvertedPixels;
    ConvertedPixels.SetNum(PixelCount * 4, EAllowShrinking::No);

    for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const FFloat16Color& Pixel = PixelData.Pixels[PixelIndex];
        const FLinearColor Linear(
            Pixel.R.GetFloat(),
            Pixel.G.GetFloat(),
            Pixel.B.GetFloat(),
            Pixel.A.GetFloat());
        const FColor Converted = Linear.ToFColor(true);
        const int64 Offset = PixelIndex * 4;
        ConvertedPixels[Offset + 0] = Converted.B;
        ConvertedPixels[Offset + 1] = Converted.G;
        ConvertedPixels[Offset + 2] = Converted.R;
        ConvertedPixels[Offset + 3] = Converted.A;
    }

    uint8* ConvertedBasePtr = ConvertedPixels.GetData();
    auto PrepareRows = [ConvertedBasePtr, BytesPerRow](int32 RowStart, int32 RowCount, int64, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)
    {
        (void)TempBuffer;
        for (int32 Row = 0; Row < RowCount; ++Row)
        {
            RowPointers[Row] = ConvertedBasePtr + BytesPerRow * (RowStart + Row);
        }
    };

    if (WritePNGWithRowSource(FilePath, Size, ERGBFormat::BGRA, 8, PrepareRows))
    {
        return true;
    }

    return WritePNGWithImageWrapper(FilePath, Size, ConvertedPixels.GetData(), ConvertedPixels.Num(), ERGBFormat::BGRA, 8);
}

bool FOmniCaptureImageWriter::WritePNGFromLinearFloat32(const TImagePixelData<FLinearColor>& PixelData, const FString& FilePath) const
{
    const FIntPoint Size = PixelData.GetSize();
    const int32 ExpectedCount = Size.X * Size.Y;
    if (PixelData.Pixels.Num() != ExpectedCount)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    if (TargetPNGBitDepth == EOmniCapturePNGBitDepth::BitDepth16)
    {
        auto PrepareRows = [&PixelData, &Size](int32 RowStart, int32 RowCount, int64 BytesPerRow, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)
        {
            const int64 RequiredSize = BytesPerRow * RowCount;
            TempBuffer.SetNum(RequiredSize, EAllowShrinking::No);

            const auto ToUInt16 = [](float Value) -> uint16
            {
                const float Clamped = FMath::Clamp(Value, 0.0f, 1.0f);
                return static_cast<uint16>(FMath::RoundToInt(Clamped * 65535.0f));
            };

            for (int32 Row = 0; Row < RowCount; ++Row)
            {
                uint8* RowData = TempBuffer.GetData() + BytesPerRow * Row;
                RowPointers[Row] = RowData;
                uint16* Dest = reinterpret_cast<uint16*>(RowData);
                const int64 PixelRowStart = static_cast<int64>(RowStart + Row) * Size.X;
                for (int32 Column = 0; Column < Size.X; ++Column)
                {
                    const FLinearColor& Pixel = PixelData.Pixels[PixelRowStart + Column];
                    *Dest++ = ToUInt16(Pixel.B);
                    *Dest++ = ToUInt16(Pixel.G);
                    *Dest++ = ToUInt16(Pixel.R);
                    *Dest++ = ToUInt16(Pixel.A);
                }
            }
        };

        return WritePNGWithRowSource(FilePath, Size, ERGBFormat::BGRA, 16, PrepareRows);
    }

    auto PrepareRows8Bit = [&PixelData, &Size](int32 RowStart, int32 RowCount, int64 BytesPerRow, TArray64<uint8>& TempBuffer, TArray<uint8*>& RowPointers)
    {
        const int64 RequiredSize = BytesPerRow * RowCount;
        TempBuffer.SetNum(RequiredSize, EAllowShrinking::No);

        for (int32 Row = 0; Row < RowCount; ++Row)
        {
            uint8* RowData = TempBuffer.GetData() + BytesPerRow * Row;
            RowPointers[Row] = RowData;
            const int64 PixelRowStart = static_cast<int64>(RowStart + Row) * Size.X;
            for (int32 Column = 0; Column < Size.X; ++Column)
            {
                const FLinearColor& Pixel = PixelData.Pixels[PixelRowStart + Column];
                const FColor Converted = Pixel.ToFColor(true);
                const int64 Offset = static_cast<int64>(Column) * 4;
                RowData[Offset + 0] = Converted.B;
                RowData[Offset + 1] = Converted.G;
                RowData[Offset + 2] = Converted.R;
                RowData[Offset + 3] = Converted.A;
            }
        }
    };

    return WritePNGWithRowSource(FilePath, Size, ERGBFormat::BGRA, 8, PrepareRows8Bit);
}

bool FOmniCaptureImageWriter::WriteBMPFromLinear(const TImagePixelData<FFloat16Color>& PixelData, const FString& FilePath) const
{
    const FIntPoint Size = PixelData.GetSize();
    const int32 ExpectedCount = Size.X * Size.Y;
    if (PixelData.Pixels.Num() != ExpectedCount)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    TArray<FColor> Converted;
    Converted.SetNum(ExpectedCount);
    for (int32 Index = 0; Index < ExpectedCount; ++Index)
    {
        const FFloat16Color& Pixel = PixelData.Pixels[Index];
        const FLinearColor Linear(
            Pixel.R.GetFloat(),
            Pixel.G.GetFloat(),
            Pixel.B.GetFloat(),
            Pixel.A.GetFloat());
        Converted[Index] = Linear.ToFColor(true);
    }

    TUniquePtr<TImagePixelData<FColor>> TempData = MakeUnique<TImagePixelData<FColor>>(Size);
    TempData->Pixels = MoveTemp(Converted);
    return WriteBMP(*TempData, FilePath);
}

bool FOmniCaptureImageWriter::WriteBMPFromLinearFloat32(const TImagePixelData<FLinearColor>& PixelData, const FString& FilePath) const
{
    const FIntPoint Size = PixelData.GetSize();
    const int32 ExpectedCount = Size.X * Size.Y;
    if (PixelData.Pixels.Num() != ExpectedCount)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    TUniquePtr<TImagePixelData<FColor>> TempData = MakeUnique<TImagePixelData<FColor>>(Size);
    TempData->Pixels.SetNum(ExpectedCount);

    for (int32 Index = 0; Index < ExpectedCount; ++Index)
    {
        TempData->Pixels[Index] = PixelData.Pixels[Index].ToFColor(true);
    }

    return WriteBMP(*TempData, FilePath);
}

bool FOmniCaptureImageWriter::WriteJPEG(const TImagePixelData<FColor>& PixelData, const FString& FilePath) const
{
    const TSharedPtr<IImageWrapper> ImageWrapper = CreateImageWrapper(EImageFormat::JPEG);
    if (!ImageWrapper.IsValid())
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    const FIntPoint Size = PixelData.GetSize();
    const TArray64<FColor>& Pixels = PixelData.Pixels;
    if (Pixels.Num() != Size.X * Size.Y)
    {
        return false;
    }

    if (!ImageWrapper->SetRaw(reinterpret_cast<const uint8*>(Pixels.GetData()), Pixels.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8))
    {
        return false;
    }

    const TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(DefaultJpegQuality);
    if (CompressedData.Num() == 0)
    {
        return false;
    }

    IFileManager::Get().Delete(*FilePath, false, true, false);
    return FFileHelper::SaveArrayToFile(CompressedData, *FilePath);
}

bool FOmniCaptureImageWriter::WriteJPEGFromLinear(const TImagePixelData<FFloat16Color>& PixelData, const FString& FilePath) const
{
    const FIntPoint Size = PixelData.GetSize();
    const int32 ExpectedCount = Size.X * Size.Y;
    if (PixelData.Pixels.Num() != ExpectedCount)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    TArray<FColor> Converted;
    Converted.SetNum(ExpectedCount);
    for (int32 Index = 0; Index < ExpectedCount; ++Index)
    {
        const FFloat16Color& Pixel = PixelData.Pixels[Index];
        const FLinearColor Linear(
            Pixel.R.GetFloat(),
            Pixel.G.GetFloat(),
            Pixel.B.GetFloat(),
            Pixel.A.GetFloat());
        Converted[Index] = Linear.ToFColor(true);
    }

    TUniquePtr<TImagePixelData<FColor>> TempData = MakeUnique<TImagePixelData<FColor>>(Size);
    TempData->Pixels = MoveTemp(Converted);
    return WriteJPEG(*TempData, FilePath);
}

bool FOmniCaptureImageWriter::WriteJPEGFromLinearFloat32(const TImagePixelData<FLinearColor>& PixelData, const FString& FilePath) const
{
    const FIntPoint Size = PixelData.GetSize();
    const int32 ExpectedCount = Size.X * Size.Y;
    if (PixelData.Pixels.Num() != ExpectedCount)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    TUniquePtr<TImagePixelData<FColor>> TempData = MakeUnique<TImagePixelData<FColor>>(Size);
    TempData->Pixels.SetNum(ExpectedCount);

    for (int32 Index = 0; Index < ExpectedCount; ++Index)
    {
        TempData->Pixels[Index] = PixelData.Pixels[Index].ToFColor(true);
    }

    return WriteJPEG(*TempData, FilePath);
}

bool FOmniCaptureImageWriter::WriteEXRFrame(const FString& FilePath, bool bIsLinear, TUniquePtr<FImagePixelData> PixelData, EOmniCapturePixelPrecision PixelPrecision, EOmniCapturePixelDataType PixelDataType, TMap<FName, FOmniCaptureLayerPayload>&& AuxiliaryLayers, const FString& LayerDirectory, const FString& LayerBaseName, const FString& LayerExtension) const
{
    if (!PixelData.IsValid())
    {
        return false;
    }

    TArray<FExrLayerRequest> Layers;
    Layers.Reserve(1 + AuxiliaryLayers.Num());

    FExrLayerRequest& BeautyLayer = Layers.Emplace_GetRef();
    BeautyLayer.Name = TEXT("Beauty");
    BeautyLayer.PixelData = MoveTemp(PixelData);
    BeautyLayer.bLinear = bIsLinear;
    BeautyLayer.Precision = PixelPrecision;
    BeautyLayer.PixelDataType = PixelDataType;
    if (BeautyLayer.PixelDataType == EOmniCapturePixelDataType::Unknown)
    {
        BeautyLayer.PixelDataType = (BeautyLayer.Precision == EOmniCapturePixelPrecision::FullFloat)
            ? EOmniCapturePixelDataType::LinearColorFloat32
            : EOmniCapturePixelDataType::LinearColorFloat16;
    }

    for (TPair<FName, FOmniCaptureLayerPayload>& Pair : AuxiliaryLayers)
    {
        if (!Pair.Value.PixelData.IsValid())
        {
            continue;
        }

        FExrLayerRequest& Request = Layers.Emplace_GetRef();
        Request.Name = Pair.Key.ToString();
        Request.PixelData = MoveTemp(Pair.Value.PixelData);
        Request.bLinear = Pair.Value.bLinear;
        Request.Precision = (Pair.Value.Precision == EOmniCapturePixelPrecision::Unknown) ? PixelPrecision : Pair.Value.Precision;
        Request.PixelDataType = Pair.Value.PixelDataType;
        if (Request.PixelDataType == EOmniCapturePixelDataType::Unknown)
        {
            switch (Request.Precision)
            {
            case EOmniCapturePixelPrecision::FullFloat:
                Request.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat32;
                break;
            case EOmniCapturePixelPrecision::HalfFloat:
                Request.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat16;
                break;
            default:
                Request.PixelDataType = EOmniCapturePixelDataType::Color8;
                break;
            }
        }
    }

    if (bPackEXRAuxiliaryLayers && Layers.Num() > 1)
    {
#if WITH_OMNICAPTURE_OPENEXR
        if (WriteCombinedEXR(FilePath, Layers))
        {
            return true;
        }

        UE_LOG(LogTemp, Warning, TEXT("Falling back to per-layer EXR output for %s"), *FilePath);
#else
        UE_LOG(LogTemp, Warning, TEXT("Combined EXR output is disabled because OpenEXR support was not found. Writing individual layers instead for %s."), *FilePath);
#endif
    }

    bool bResult = true;
    if (Layers.Num() > 0)
    {
        bResult = WriteEXR(MoveTemp(Layers[0].PixelData), FilePath, Layers[0].Precision, Layers[0].PixelDataType);
    }

    for (int32 Index = 1; Index < Layers.Num(); ++Index)
    {
        if (!Layers[Index].PixelData.IsValid())
        {
            continue;
        }

        const FString LayerFileName = FString::Printf(TEXT("%s_%s%s"), *LayerBaseName, *Layers[Index].Name, *LayerExtension);
        const FString LayerPath = FPaths::Combine(LayerDirectory, LayerFileName);
        bResult &= WriteEXR(MoveTemp(Layers[Index].PixelData), LayerPath, Layers[Index].Precision, Layers[Index].PixelDataType);
    }

    return bResult;
}

#if WITH_OMNICAPTURE_OPENEXR
bool FOmniCaptureImageWriter::WriteCombinedEXR(const FString& FilePath, TArray<FExrLayerRequest>& Layers) const
{
    if (Layers.Num() == 0)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    const FIntPoint ExpectedSize = Layers[0].PixelData.IsValid() ? Layers[0].PixelData->GetSize() : FIntPoint::ZeroValue;
    if (ExpectedSize.X <= 0 || ExpectedSize.Y <= 0)
    {
        return false;
    }

    const int64 PixelCount = static_cast<int64>(ExpectedSize.X) * ExpectedSize.Y;
    if (PixelCount <= 0)
    {
        return false;
    }

    TArray<FPreparedExrLayer> PreparedLayers;
    PreparedLayers.Reserve(Layers.Num());

    for (FExrLayerRequest& Layer : Layers)
    {
        if (!Layer.PixelData.IsValid())
        {
            return false;
        }

        if (Layer.PixelData->GetSize() != ExpectedSize)
        {
            UE_LOG(LogTemp, Warning, TEXT("Skipping EXR layer '%s' due to mismatched resolution"), *Layer.Name);
            return false;
        }

        FPreparedExrLayer& Prepared = PreparedLayers.Emplace_GetRef();
        FTCHARToUTF8 NameUtf8(*Layer.Name);
        Prepared.Name = std::string(NameUtf8.Length() > 0 ? NameUtf8.Get() : "");
        Prepared.ChannelCount = 4;

        const FImagePixelData* PixelData = Layer.PixelData.Get();
        EOmniCapturePixelPrecision Precision = Layer.Precision;
        if (Precision == EOmniCapturePixelPrecision::Unknown)
        {
            Precision = EOmniCapturePixelPrecision::HalfFloat;
        }

        switch (Layer.PixelDataType)
        {
        case EOmniCapturePixelDataType::LinearColorFloat32:
        {
            const TImagePixelData<FLinearColor>* Float32Data = static_cast<const TImagePixelData<FLinearColor>*>(PixelData);
            Prepared.PixelType = OPENEXR_IMF_NAMESPACE::PixelType::FLOAT;
            Prepared.FloatBuffer.SetNum(PixelCount * 4);

            for (int64 Index = 0; Index < PixelCount; ++Index)
            {
                const FLinearColor& Src = Float32Data->Pixels[Index];
                const int64 Base = Index * 4;
                Prepared.FloatBuffer[Base + 0] = Src.R;
                Prepared.FloatBuffer[Base + 1] = Src.G;
                Prepared.FloatBuffer[Base + 2] = Src.B;
                Prepared.FloatBuffer[Base + 3] = Src.A;
            }
            break;
        }
        case EOmniCapturePixelDataType::LinearColorFloat16:
        {
            const TImagePixelData<FFloat16Color>* Float16Data = static_cast<const TImagePixelData<FFloat16Color>*>(PixelData);
            Prepared.PixelType = OPENEXR_IMF_NAMESPACE::PixelType::HALF;
            Prepared.HalfBuffer.SetNum(PixelCount * 4);

            for (int64 Index = 0; Index < PixelCount; ++Index)
            {
                const FFloat16Color& Src = Float16Data->Pixels[Index];
                const int64 Base = Index * 4;
                Prepared.HalfBuffer[Base + 0] = IMATH_NAMESPACE::half(Src.R.GetFloat());
                Prepared.HalfBuffer[Base + 1] = IMATH_NAMESPACE::half(Src.G.GetFloat());
                Prepared.HalfBuffer[Base + 2] = IMATH_NAMESPACE::half(Src.B.GetFloat());
                Prepared.HalfBuffer[Base + 3] = IMATH_NAMESPACE::half(Src.A.GetFloat());
            }
            break;
        }
        case EOmniCapturePixelDataType::Color8:
        {
            const TImagePixelData<FColor>* ColorData = static_cast<const TImagePixelData<FColor>*>(PixelData);
            Prepared.PixelType = OPENEXR_IMF_NAMESPACE::PixelType::FLOAT;
            Prepared.FloatBuffer.SetNum(PixelCount * 4);

            for (int64 Index = 0; Index < PixelCount; ++Index)
            {
                const FLinearColor Src = ColorData->Pixels[Index].ReinterpretAsLinear();
                const int64 Base = Index * 4;
                Prepared.FloatBuffer[Base + 0] = Src.R;
                Prepared.FloatBuffer[Base + 1] = Src.G;
                Prepared.FloatBuffer[Base + 2] = Src.B;
                Prepared.FloatBuffer[Base + 3] = Src.A;
            }
            break;
        }
        default:
            UE_LOG(LogTemp, Warning, TEXT("Unsupported pixel payload for EXR layer '%s'"), *Layer.Name);
            return false;
        }
    }

    IFileManager::Get().Delete(*FilePath, false, true, false);

    bool bSucceeded = false;

    try
    {
        if (bUseEXRMultiPart)
        {
            TArray<OPENEXR_IMF_NAMESPACE::Header> Headers;
            TArray<OPENEXR_IMF_NAMESPACE::FrameBuffer> FrameBuffers;
            Headers.Reserve(PreparedLayers.Num());
            FrameBuffers.Reserve(PreparedLayers.Num());

            for (const FPreparedExrLayer& Prepared : PreparedLayers)
            {
                OPENEXR_IMF_NAMESPACE::Header Header(ExpectedSize.X, ExpectedSize.Y);
                Header.compression() = ToOpenExrCompression(TargetEXRCompression);
                if (!Prepared.Name.empty())
                {
                    Header.setName(Prepared.Name.c_str());
                }

                OPENEXR_IMF_NAMESPACE::FrameBuffer Buffer;
                for (int32 ChannelIndex = 0; ChannelIndex < Prepared.ChannelCount; ++ChannelIndex)
                {
                    const TCHAR* ChannelSuffix = GetChannelSuffix(ChannelIndex);
                    FTCHARToUTF8 ChannelUtf8(ChannelSuffix);
                    Header.channels().insert(ChannelUtf8.Get(), OPENEXR_IMF_NAMESPACE::Channel(Prepared.PixelType));

                    const char* BasePtr = Prepared.GetBasePointer();
                    const int32 ComponentSize = Prepared.GetComponentSize();
                    const size_t PixelStride = static_cast<size_t>(ComponentSize) * Prepared.ChannelCount;
                    const size_t RowStride = PixelStride * ExpectedSize.X;
                    const size_t ChannelOffset = static_cast<size_t>(ComponentSize) * ChannelIndex;

                    Buffer.insert(ChannelUtf8.Get(), OPENEXR_IMF_NAMESPACE::Slice(Prepared.PixelType, const_cast<char*>(BasePtr) + ChannelOffset, PixelStride, RowStride));
                }

                Headers.Add(Header);
                FrameBuffers.Add(Buffer);
            }

            OPENEXR_IMF_NAMESPACE::MultiPartOutputFile OutputFile(TCHAR_TO_UTF8(*FilePath), Headers.GetData(), Headers.Num());
            for (int32 PartIndex = 0; PartIndex < Headers.Num(); ++PartIndex)
            {
                OPENEXR_IMF_NAMESPACE::OutputPart Part(OutputFile, PartIndex);
                Part.setFrameBuffer(FrameBuffers[PartIndex]);
                Part.writePixels(ExpectedSize.Y);
            }
        }
        else
        {
            OPENEXR_IMF_NAMESPACE::Header Header(ExpectedSize.X, ExpectedSize.Y);
            Header.compression() = ToOpenExrCompression(TargetEXRCompression);
            OPENEXR_IMF_NAMESPACE::FrameBuffer FrameBuffer;

            for (const FPreparedExrLayer& Prepared : PreparedLayers)
            {
                const std::string Prefix = Prepared.Name.empty() ? std::string() : Prepared.Name + ".";
                for (int32 ChannelIndex = 0; ChannelIndex < Prepared.ChannelCount; ++ChannelIndex)
                {
                    const TCHAR* ChannelSuffix = GetChannelSuffix(ChannelIndex);
                    FTCHARToUTF8 ChannelUtf8(ChannelSuffix);
                    const std::string ChannelName = Prefix + ChannelUtf8.Get();

                    Header.channels().insert(ChannelName.c_str(), OPENEXR_IMF_NAMESPACE::Channel(Prepared.PixelType));

                    const char* BasePtr = Prepared.GetBasePointer();
                    const int32 ComponentSize = Prepared.GetComponentSize();
                    const size_t PixelStride = static_cast<size_t>(ComponentSize) * Prepared.ChannelCount;
                    const size_t RowStride = PixelStride * ExpectedSize.X;
                    const size_t ChannelOffset = static_cast<size_t>(ComponentSize) * ChannelIndex;

                    FrameBuffer.insert(ChannelName.c_str(), OPENEXR_IMF_NAMESPACE::Slice(Prepared.PixelType, const_cast<char*>(BasePtr) + ChannelOffset, PixelStride, RowStride));
                }
            }

            OPENEXR_IMF_NAMESPACE::OutputFile OutputFile(TCHAR_TO_UTF8(*FilePath), Header);
            OutputFile.setFrameBuffer(FrameBuffer);
            OutputFile.writePixels(ExpectedSize.Y);
        }

        bSucceeded = true;
    }
    catch (const std::exception& Exception)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to write multi-layer EXR '%s': %s"), *FilePath, UTF8_TO_TCHAR(Exception.what()));
    }

    if (bSucceeded)
    {
        for (FExrLayerRequest& Layer : Layers)
        {
            Layer.PixelData.Reset();
        }
    }

    return bSucceeded;
}
#endif // WITH_OMNICAPTURE_OPENEXR

#if !WITH_OMNICAPTURE_OPENEXR
bool FOmniCaptureImageWriter::WriteCombinedEXR(const FString& FilePath, TArray<FExrLayerRequest>& Layers) const
{
    UE_LOG(LogTemp, Verbose, TEXT("Skipping combined EXR output for %s because OpenEXR support is unavailable."), *FilePath);
    return false;
}
#endif

bool FOmniCaptureImageWriter::WriteEXR(TUniquePtr<FImagePixelData> PixelData, const FString& FilePath, EOmniCapturePixelPrecision PixelPrecision, EOmniCapturePixelDataType PixelDataType) const
{
    if (!PixelData.IsValid())
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    const EOmniCapturePixelDataType EffectiveType = PixelDataType;

    EOmniCapturePixelPrecision EffectivePrecision = PixelPrecision;
    if (EffectivePrecision == EOmniCapturePixelPrecision::Unknown)
    {
        EffectivePrecision = EOmniCapturePixelPrecision::HalfFloat;
    }

    EImagePixelType PixelType = EImagePixelType::Float16;
    switch (EffectivePrecision)
    {
    case EOmniCapturePixelPrecision::FullFloat:
        PixelType = EImagePixelType::Float32;
        break;
    case EOmniCapturePixelPrecision::HalfFloat:
        PixelType = EImagePixelType::Float16;
        break;
    default:
        return false;
    }

    if (EffectivePrecision == EOmniCapturePixelPrecision::FullFloat && EffectiveType != EOmniCapturePixelDataType::LinearColorFloat32)
    {
        UE_LOG(LogTemp, Warning, TEXT("WriteEXR expected 32-bit linear color data for '%s'."), *FilePath);
    }
    else if (EffectivePrecision == EOmniCapturePixelPrecision::HalfFloat && EffectiveType != EOmniCapturePixelDataType::LinearColorFloat16)
    {
        UE_LOG(LogTemp, Warning, TEXT("WriteEXR expected 16-bit linear color data for '%s'."), *FilePath);
    }

    IFileManager::Get().Delete(*FilePath, false, true, false);
    return WriteEXRInternal(MoveTemp(PixelData), FilePath, PixelType);
}

bool FOmniCaptureImageWriter::WriteEXRFromColor(const TImagePixelData<FColor>& PixelData, const FString& FilePath) const
{
    const FIntPoint Size = PixelData.GetSize();
    const int32 ExpectedCount = Size.X * Size.Y;
    if (PixelData.Pixels.Num() != ExpectedCount)
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

    TArray<FFloat16Color> Converted;
    Converted.SetNum(ExpectedCount);
    for (int32 Index = 0; Index < ExpectedCount; ++Index)
    {
        Converted[Index] = FFloat16Color(PixelData.Pixels[Index].ReinterpretAsLinear());
    }

    TUniquePtr<TImagePixelData<FFloat16Color>> TempData = MakeUnique<TImagePixelData<FFloat16Color>>(Size);
    TempData->Pixels = MoveTemp(Converted);
    return WriteEXR(MoveTemp(TempData), FilePath, EOmniCapturePixelPrecision::HalfFloat, EOmniCapturePixelDataType::LinearColorFloat16);
}

bool FOmniCaptureImageWriter::WriteEXRInternal(TUniquePtr<FImagePixelData> PixelData, const FString& FilePath, EImagePixelType PixelType) const
{
    if (!PixelData.IsValid())
    {
        return false;
    }

    if (IsStopRequested())
    {
        return false;
    }

#if OMNICAPTURE_UE_VERSION_AT_LEAST(5, 5, 0)
#if WITH_OMNICAPTURE_OPENEXR
    TArray<FExrLayerRequest> Layers;
    FExrLayerRequest& Layer = Layers.Emplace_GetRef();
    Layer.PixelData = MoveTemp(PixelData);
    Layer.bLinear = true;
    Layer.Precision = (PixelType == EImagePixelType::Float32)
        ? EOmniCapturePixelPrecision::FullFloat
        : EOmniCapturePixelPrecision::HalfFloat;

    return WriteCombinedEXR(FilePath, Layers);
#else
    UE_LOG(LogTemp, Warning, TEXT("EXR writing is unavailable: OpenEXR support is required when building against UE 5.5 or newer."));
    return false;
#endif // WITH_OMNICAPTURE_OPENEXR
#else
    IImageWriteQueueModule& ImageWriteModule = FModuleManager::LoadModuleChecked<IImageWriteQueueModule>(TEXT("ImageWriteQueue"));
    IImageWriteQueue& WriteQueue = ImageWriteModule.GetWriteQueue();

    TPromise<bool> CompletionPromise;
    TFuture<bool> CompletionFuture = CompletionPromise.GetFuture();

    TUniquePtr<FImageWriteTask> Task = WriteQueue.CreateTask(EImageWriteTaskType::HighPriority);
    Task->Format = EImageFormat::EXR;
    Task->Filename = FilePath;
    Task->PixelData = MoveTemp(PixelData);
    Task->PixelType = PixelType;
    Task->CompressionQuality = static_cast<int32>(EImageCompressionQuality::Default);
    Task->bOverwriteFile = true;
    Task->OnCompleted = FOnImageWriteTaskCompleted::CreateLambda([Promise = MoveTemp(CompletionPromise)](bool bSuccess) mutable
    {
        Promise.SetValue(bSuccess);
    });

    WriteQueue.Enqueue(MoveTemp(Task));
    return CompletionFuture.Get();
#endif // OMNICAPTURE_UE_VERSION_AT_LEAST(5, 5, 0)
}

void FOmniCaptureImageWriter::RequestStop()
{
    bStopRequested.Store(true);
}

bool FOmniCaptureImageWriter::IsStopRequested() const
{
    return bStopRequested.Load();
}

void FOmniCaptureImageWriter::WaitForAvailableTaskSlot()
{
    if (MaxPendingTasks <= 0)
    {
        return;
    }

    while (!IsStopRequested())
    {
        TFuture<bool> TaskToWait;
        {
            FScopeLock Lock(&PendingTasksCS);
            if (PendingTasks.Num() < MaxPendingTasks)
            {
                break;
            }

            TaskToWait = MoveTemp(PendingTasks[0]);
            PendingTasks.RemoveAt(0, 1, EAllowShrinking::No);
        }

        if (TaskToWait.IsValid())
        {
            const bool bResult = TaskToWait.Get();
            if (!bResult)
            {
                UE_LOG(LogTemp, Warning, TEXT("OmniCapture image write task failed"));
            }
        }
    }
}

void FOmniCaptureImageWriter::TrackPendingTask(TFuture<bool>&& TaskFuture)
{
    FScopeLock Lock(&PendingTasksCS);
    PendingTasks.Add(MoveTemp(TaskFuture));
}

void FOmniCaptureImageWriter::PruneCompletedTasks()
{
    FScopeLock Lock(&PendingTasksCS);
    for (int32 Index = PendingTasks.Num() - 1; Index >= 0; --Index)
    {
        if (PendingTasks[Index].IsReady())
        {
            const bool bResult = PendingTasks[Index].Get();
            if (!bResult)
            {
                UE_LOG(LogTemp, Warning, TEXT("OmniCapture image write task failed"));
            }
            PendingTasks.RemoveAtSwap(Index, 1, EAllowShrinking::No);
        }
    }
}

void FOmniCaptureImageWriter::EnforcePendingTaskLimit()
{
    if (MaxPendingTasks <= 0)
    {
        return;
    }

    while (true)
    {
        TFuture<bool> TaskToWait;
        {
            FScopeLock Lock(&PendingTasksCS);
            if (PendingTasks.Num() <= MaxPendingTasks)
            {
                break;
            }

            TaskToWait = MoveTemp(PendingTasks[0]);
            PendingTasks.RemoveAt(0, 1, EAllowShrinking::No);
        }

        if (TaskToWait.IsValid())
        {
            const bool bResult = TaskToWait.Get();
            if (!bResult)
            {
                UE_LOG(LogTemp, Warning, TEXT("OmniCapture image write task failed"));
            }
        }
    }
}

void FOmniCaptureImageWriter::WaitForAllTasks()
{
    TArray<TFuture<bool>> TasksToWait;
    {
        FScopeLock Lock(&PendingTasksCS);
        TasksToWait = MoveTemp(PendingTasks);
        PendingTasks.Reset();
    }

    for (TFuture<bool>& Task : TasksToWait)
    {
        const bool bResult = Task.Get();
        if (!bResult)
        {
            UE_LOG(LogTemp, Warning, TEXT("OmniCapture image write task failed"));
        }
    }
}
