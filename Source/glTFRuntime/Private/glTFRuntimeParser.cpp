// Copyright 2020-2023, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "Async/Async.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Engine/Texture2D.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Animation/Skeleton.h"
#include "Materials/Material.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
#include "MaterialDomain.h"
#else
#include "MaterialShared.h"
#endif
#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
#include "RenderMath.h"
#else
#include "RenderUtils.h"
#endif

#include "glTFRuntimeAssetUserData.h"

DEFINE_LOG_CATEGORY(LogGLTFRuntime);

FglTFRuntimeOnPreLoadedPrimitive FglTFRuntimeParser::OnPreLoadedPrimitive;
FglTFRuntimeOnLoadedPrimitive FglTFRuntimeParser::OnLoadedPrimitive;
FglTFRuntimeOnLoadedRefSkeleton FglTFRuntimeParser::OnLoadedRefSkeleton;
FglTFRuntimeOnCreatedPoseTracks FglTFRuntimeParser::OnCreatedPoseTracks;
FglTFRuntimeOnTextureImageIndex FglTFRuntimeParser::OnTextureImageIndex;
FglTFRuntimeOnTextureMips FglTFRuntimeParser::OnTextureMips;
FglTFRuntimeOnTextureFilterMips FglTFRuntimeParser::OnTextureFilterMips;
FglTFRuntimeOnTexturePixels FglTFRuntimeParser::OnTexturePixels;
FglTFRuntimeOnLoadedTexturePixels FglTFRuntimeParser::OnLoadedTexturePixels;
FglTFRuntimeOnFinalizedStaticMesh FglTFRuntimeParser::OnFinalizedStaticMesh;
FglTFRuntimeOnPreCreatedStaticMesh FglTFRuntimeParser::OnPreCreatedStaticMesh;
FglTFRuntimeOnPostCreatedStaticMesh FglTFRuntimeParser::OnPostCreatedStaticMesh;
FglTFRuntimeOnPreCreatedSkeletalMesh FglTFRuntimeParser::OnPreCreatedSkeletalMesh;

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromFilename(const FString& Filename, const FglTFRuntimeConfig& LoaderConfig)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromFilename, FColor::Magenta);

	FString TruePath = Filename;

	if (LoaderConfig.bSearchContentDir)
	{
		TruePath = FPaths::Combine(FPaths::ProjectContentDir(), Filename);
	}

	if (!FPaths::FileExists(TruePath))
	{
		bool bAssetFound = false;
		for (const FString& PluginName : LoaderConfig.ContentPluginsToScan)
		{
			TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
			if (Plugin)
			{
				TruePath = FPaths::Combine(Plugin->GetContentDir(), Filename);
				if (FPaths::FileExists(TruePath))
				{
					bAssetFound = true;
					break;
				}
			}
		}

		if (!bAssetFound)
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to open file %s"), *Filename);
			return nullptr;
		}
	}

	TArray64<uint8> Content;
	if (!FFileHelper::LoadFileToArray(Content, *TruePath))
	{
		UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to load file %s"), *Filename);
		return nullptr;
	}

	TSharedPtr<FglTFRuntimeParser> Parser = FromData(Content.GetData(), Content.Num(), LoaderConfig);

	if (Parser)
	{
		if (LoaderConfig.bAllowExternalFiles)
		{
			// allows to load external files
			Parser->BaseDirectory = FPaths::GetPath(TruePath);
		}
		Parser->BaseFilename = FPaths::GetBaseFilename(TruePath);
	}

	return Parser;
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromRawDataAndArchive(const uint8* DataPtr, int64 DataNum, TSharedPtr<FglTFRuntimeArchive> InArchive, const FglTFRuntimeConfig& LoaderConfig)
{
	// this must be defined here as we are dealing with raw C pointers
	TArray64<uint8> ArchiveEntryPointData;

	if (InArchive)
	{
		FString Filename = LoaderConfig.ArchiveEntryPoint;

		if (Filename.IsEmpty())
		{
			TArray<FString> Extensions;
			LoaderConfig.ArchiveAutoEntryPointExtensions.ParseIntoArray(Extensions, TEXT(" "), true);
			for (const FString& Extension : Extensions)
			{
				Filename = InArchive->GetFirstFilenameByExtension(Extension);
				if (!Filename.IsEmpty())
				{
					break;
				}
			}
		}

		if (!LoaderConfig.bAsBlob && Filename.IsEmpty())
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to find entry point from Archive."), *Filename);
			return nullptr;
		}

		if (!LoaderConfig.bAsBlob && !InArchive->GetFileContent(Filename, ArchiveEntryPointData))
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to get %s from Archive."), *Filename);
			return nullptr;
		}

		if (ArchiveEntryPointData.Num() > 0)
		{
			DataPtr = ArchiveEntryPointData.GetData();
			DataNum = ArchiveEntryPointData.Num();
		}
		else
		{
			DataPtr = nullptr;
			DataNum = 0;
		}
	}

	if (LoaderConfig.bAsBlob)
	{
		TSharedPtr<FglTFRuntimeParser> NewParser = MakeShared<FglTFRuntimeParser>(MakeShared<FJsonObject>(), LoaderConfig.GetMatrix(), LoaderConfig.SceneScale);
		if (NewParser)
		{
			NewParser->AsBlob.Append(DataPtr, DataNum);
			NewParser->Archive = InArchive;
			NewParser->AssetUserDataClasses = LoaderConfig.AssetUserDataClasses;
		}
		return NewParser;
	}

	// detect binary format
	if (DataNum > 20)
	{
		if (DataPtr[0] == 0x67 &&
			DataPtr[1] == 0x6C &&
			DataPtr[2] == 0x54 &&
			DataPtr[3] == 0x46)
		{
			return FromBinary(DataPtr, DataNum, LoaderConfig, InArchive);
		}
	}

	if (DataNum > 0 && DataNum <= INT32_MAX)
	{
		FString JsonData;
		FFileHelper::BufferToString(JsonData, DataPtr, (int32)DataNum);
		return FromString(JsonData, LoaderConfig, InArchive);
	}

	return nullptr;
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromData(const uint8* DataPtr, int64 DataNum, const FglTFRuntimeConfig& LoaderConfig)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromData, FColor::Magenta);

	// required for Gzip and LZ4;
	TArray64<uint8> UncompressedData;

	// Gzip Compressed ? 10 bytes header and 8 bytes footer
	if (DataNum > 18 && DataPtr[0] == 0x1F && DataPtr[1] == 0x8B && DataPtr[2] == 0x08)
	{
		uint32* GzipOriginalSize = (uint32*)(&DataPtr[DataNum - 4]);
		int64 StartOfBuffer = 10;
		// FEXTRA
		if (DataPtr[3] & 0x04)
		{
			uint16* FExtraXLen = (uint16*)(&DataPtr[StartOfBuffer]);
			if (StartOfBuffer + 2 >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FEXTRA header."));
				return nullptr;
			}
			StartOfBuffer += 2 + *FExtraXLen;
			if (StartOfBuffer >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FEXTRA XLEN."));
				return nullptr;
			}
		}

		// FNAME
		if (DataPtr[3] & 0x08)
		{
			while (DataPtr[StartOfBuffer] != 0)
			{
				StartOfBuffer++;
				if (StartOfBuffer >= DataNum)
				{
					UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FNAME header."));
					return nullptr;
				}
			}
			if (++StartOfBuffer >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FNAME header."));
				return nullptr;
			}
		}

		// FCOMMENT
		if (DataPtr[3] & 0x10)
		{
			while (DataPtr[StartOfBuffer] != 0)
			{
				StartOfBuffer++;
				if (StartOfBuffer >= DataNum)
				{
					UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FCOMMENT header."));
					return nullptr;
				}
			}
			if (++StartOfBuffer >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FCOMMENT header."));
				return nullptr;
			}
		}

		// FHCRC
		if (DataPtr[3] & 0x02)
		{
			if (StartOfBuffer + 2 >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FHCRC header."));
				return nullptr;
			}
			StartOfBuffer += 2;
		}

		UncompressedData.AddUninitialized(*GzipOriginalSize);
		if (!FCompression::UncompressMemory(NAME_Zlib, UncompressedData.GetData(), *GzipOriginalSize, &DataPtr[StartOfBuffer], DataNum - StartOfBuffer - 8, COMPRESS_NoFlags, -15))
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to uncompress Gzip data."));
			return nullptr;
		}

		DataPtr = UncompressedData.GetData();
		DataNum = *GzipOriginalSize;
	}
	// LZ4 ? magic number(4) + 3 (Note: Unreal includes the classic LZ4 c library, unfortunately it is exposed in a pretty annoying way, so I have reimplemented the decoding process as it is way more fun than messing around with the build system)
	else if (DataNum > 7 && DataPtr[0] == 0x04 && DataPtr[1] == 0x22 && DataPtr[2] == 0x4D && DataPtr[3] == 0x18)
	{
		const uint8* LZ4FLG = DataPtr + 4;
		const uint8* LZ4BD = DataPtr + 5;

		const bool bLZ4ThreadSafe = ((*LZ4FLG >> 5) & 0x01) == 1;
		const bool bLZ4BlockHasChecksum = ((*LZ4FLG >> 4) & 0x01) == 1;
		const bool bLZ4BlockHasContentSize = ((*LZ4FLG >> 3) & 0x01) == 1;
		const bool bLZ4BlockHasDictID = (*LZ4FLG & 0x01) == 1;

		int64 ReserveBlockSize = 0;
		if (((*LZ4BD >> 7) & 0x01) == 1)
		{
			ReserveBlockSize = 4 * 1024 * 1024;
		}
		else if (((*LZ4BD >> 6) & 0x01) == 1)
		{
			ReserveBlockSize = 1024 * 1024;
		}
		else if (((*LZ4BD >> 5) & 0x01) == 1)
		{
			ReserveBlockSize = 256 * 1024;
		}
		else if (((*LZ4BD >> 4) & 0x01) == 1)
		{
			ReserveBlockSize = 64 * 1024;
		}

		int64 LZ4Offset = 7 + (bLZ4BlockHasContentSize ? 8 : 0) + (bLZ4BlockHasDictID ? 4 : 0);

		TArray<TPair<const uint8*, int64>> LZ4Blocks;

		while (LZ4Offset < DataNum)
		{
			if (LZ4Offset + 4 >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid LZ4 Block at index %d."), LZ4Blocks.Num());
				return nullptr;
			}

			const uint32* BlockSize = reinterpret_cast<const uint32*>(DataPtr + LZ4Offset);

			const uint32 TrueBlockSize = *BlockSize & 0x7FFFFFFF;

			if (TrueBlockSize == 0)
			{
				break;
			}

			if (LZ4Offset + 4 + TrueBlockSize >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid LZ4 Block at index %d."), LZ4Blocks.Num());
				return nullptr;
			}

			LZ4Blocks.Add(TPair<const uint8*, int64>(DataPtr + LZ4Offset + 4, *BlockSize));

			LZ4Offset += 4 + TrueBlockSize + (bLZ4BlockHasChecksum ? 4 : 0);

		}

		if (bLZ4BlockHasContentSize)
		{
			const uint64* LZ4ContentSize = reinterpret_cast<const uint64*>(DataPtr + 4 + 2);
			// 64GB seems a pretty reasonable limit...
			UncompressedData.Reserve(FMath::Min<int64>(*LZ4ContentSize, 64LLU * 1024 * 1024 * 1024));
		}
		else
		{
			// let's reserve double of the "theoretical" compressed size
			UncompressedData.Reserve(ReserveBlockSize * LZ4Blocks.Num() * 2);
		}

		auto LZ4Decompress = [](const uint8* BlockData, const int64 BlockSize, TArray64<uint8>& Output) -> bool
			{
				const uint32 TrueBlockSize = BlockSize & 0x7FFFFFFF;
				// uncompressed block?
				if (((BlockSize >> 31) & 0x01) == 1)
				{
					Output.Append(BlockData, TrueBlockSize);
					return true;
				}

				int64 Offset = 0;
				while (Offset < TrueBlockSize)
				{
					const uint8 Token = BlockData[Offset++];
					int64 Length = Token >> 4;

					if (Length > 0)
					{
						int64 TempLength = Length + 240;
						while (TempLength == 255)
						{
							if (Offset + 1 >= TrueBlockSize)
							{
								return false;
							}
							TempLength = BlockData[Offset++];
							Length += TempLength;
						}

						if (Offset + Length > TrueBlockSize)
						{
							return false;
						}

						while (Length > 0)
						{
							Output.Add(BlockData[Offset++]);
							Length--;
						}

						if (Offset == TrueBlockSize)
						{
							return true;
						}
					}

					if (Offset + 2 >= TrueBlockSize)
					{
						return false;
					}

					uint16 CopyOffset = static_cast<uint16>(BlockData[Offset++]);
					CopyOffset |= (static_cast<uint16>(BlockData[Offset++]) << 8);

					if (CopyOffset == 0)
					{
						return false;
					}

					int64 MatchOffset = Output.Num() - CopyOffset;
					if (MatchOffset < 0)
					{
						return false;
					}

					int64 MatchLength = Token & 0x0F;
					int64 TempMatchLength = MatchLength + 240;
					while (TempMatchLength == 255)
					{
						if (Offset + 1 > TrueBlockSize)
						{
							return false;
						}
						TempMatchLength = BlockData[Offset++];
						MatchLength += TempMatchLength;
					}

					MatchLength += 4;

					while (MatchLength > 0)
					{
						if (!Output.IsValidIndex(MatchOffset))
						{
							return false;
						}
						const uint8 Byte = Output[MatchOffset++];
						Output.Add(Byte);
						MatchLength--;
					}
				}

				return true;
			};

		// can we decompress the blocks in parallel?
		if (bLZ4ThreadSafe)
		{
			TArray<TPair<TArray64<uint8>, bool>> BlocksStates;
			BlocksStates.AddDefaulted(LZ4Blocks.Num());

			ParallelFor(LZ4Blocks.Num(), [&](const int32 BlockIndex)
				{
					TPair<TArray64<uint8>, bool>& BlockState = BlocksStates[BlockIndex];
					BlockState.Key.Reserve(ReserveBlockSize);
					BlockState.Value = LZ4Decompress(LZ4Blocks[BlockIndex].Key, LZ4Blocks[BlockIndex].Value, BlockState.Key);
				});

			for (int32 BlockIndex = 0; BlockIndex < LZ4Blocks.Num(); BlockIndex++)
			{
				const TPair<TArray64<uint8>, bool>& BlockState = BlocksStates[BlockIndex];
				if (!BlockState.Value)
				{
					UE_LOG(LogGLTFRuntime, Error, TEXT("LZ4 parallel decompression error @Block %d"), BlockIndex);
					return nullptr;
				}

				UncompressedData.Append(BlockState.Key);
			}
		}
		else
		{
			for (int32 BlockIndex = 0; BlockIndex < LZ4Blocks.Num(); BlockIndex++)
			{
				if (!LZ4Decompress(LZ4Blocks[BlockIndex].Key, LZ4Blocks[BlockIndex].Value, UncompressedData))
				{
					UE_LOG(LogGLTFRuntime, Error, TEXT("LZ4 decompression error @Block %d"), BlockIndex);
					return nullptr;
				}
			}
		}

		DataPtr = UncompressedData.GetData();
		DataNum = UncompressedData.Num();
	}

	TSharedPtr<FglTFRuntimeArchive> Archive = nullptr;

	// Zip archive ?
	if (DataNum > 4 && DataPtr[0] == 0x50 && DataPtr[1] == 0x4b && DataPtr[2] == 0x03 && DataPtr[3] == 0x04)
	{
		TSharedPtr<FglTFRuntimeArchiveZip> ZipFile = MakeShared<FglTFRuntimeArchiveZip>();
		if (!LoaderConfig.EncryptionKey.IsEmpty())
		{
			ZipFile->SetPassword(LoaderConfig.EncryptionKey);
		}

		if (!ZipFile->FromData(DataPtr, DataNum))
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to parse Zip archive."));
			return nullptr;
		}

		Archive = ZipFile;
	}
	// tar ?
	else if (DataNum % 512 == 0 && DataNum >= 10240 && DataPtr[257] == 'u' && DataPtr[258] == 's' && DataPtr[259] == 't' && DataPtr[260] == 'a' && DataPtr[261] == 'r')
	{
		TMap<FString, TArray64<uint8>> TarMap;

		auto GetTarString = [](const uint8* StringDataPtr, const int32 StringLen) -> FString
			{
				TArray<uint8> StringData;
				StringData.Reserve(StringLen + 1);
				for (int32 StringByteIndex = 0; StringByteIndex < StringLen; StringByteIndex++)
				{
					if (StringDataPtr[StringByteIndex] == 0)
					{
						break;
					}

					StringData.Add(StringDataPtr[StringByteIndex]);
				}

				StringData.Add(0);

				return FString(UTF8_TO_TCHAR(StringData.GetData()));
			};

		TSharedPtr<FglTFRuntimeArchiveMap> TarArchive = MakeShared<FglTFRuntimeArchiveMap>();

		// given the block nature of tar files, it is more memory efficient to extract the files directly
		int64 ByteIndex = 0;
		bool bTarParsingFailed = false;
		int64 TarPrefixLen = 0;
		while (ByteIndex < DataNum)
		{
			const uint8* Block = DataPtr + ByteIndex;
			ByteIndex += 512;

			// end of archive ?
			if (Block[0] == 0)
			{
				// no entry ? assume error
				if (TarMap.Num() < 1)
				{
					bTarParsingFailed = true;
				}
				break;
			}

			if (Block[257] != 'u' || Block[258] != 's' || Block[259] != 't' || Block[260] != 'a' || Block[261] != 'r')
			{
				bTarParsingFailed = true;
				break;
			}

			const FString BlockSizeString = GetTarString(Block + 124, 12);
			int64 FileSize = 0;
			for (const TCHAR& BlockSizeChar : BlockSizeString)
			{
				FileSize *= 8;
				FileSize += static_cast<char>(BlockSizeChar) - '0';
			}

			const int64 BlockSize = Align(FileSize, 512);

			if (ByteIndex + BlockSize > DataNum)
			{
				bTarParsingFailed = true;
				break;
			}

			// first entry ?
			if (ByteIndex == 512)
			{
				const FString TarPrefix = GetTarString(Block, 100);
				TarPrefixLen = TarPrefix.Len();
			}

			if (Block[156] == 0 || Block[156] == '0' || Block[156] == '7')
			{
				const FString TarFilename = GetTarString(Block, 100);
				if (TarFilename.Len() <= TarPrefixLen)
				{
					bTarParsingFailed = true;
					break;
				}
				TArray64<uint8>& TarFileContent = TarMap.Add(TarFilename.RightChop(TarPrefixLen));
				TarFileContent.AddUninitialized(FileSize);
				FMemory::Memcpy(TarFileContent.GetData(), DataPtr + ByteIndex, FileSize);
			}

			ByteIndex += BlockSize;
		}

		if (!bTarParsingFailed)
		{
			TarArchive->FromMap(TarMap);
			Archive = TarArchive;
		}
	}

	return FromRawDataAndArchive(DataPtr, DataNum, Archive, LoaderConfig);
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromMap(const TMap<FString, TArray64<uint8>> Map, const FglTFRuntimeConfig& LoaderConfig)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromMap, FColor::Magenta);

	TSharedPtr<FglTFRuntimeArchiveMap> ArchiveMap = MakeShared<FglTFRuntimeArchiveMap>();

	ArchiveMap->FromMap(Map);

	return FromRawDataAndArchive(nullptr, 0, ArchiveMap, LoaderConfig);
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromString(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig, TSharedPtr<FglTFRuntimeArchive> InArchive)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromString, FColor::Magenta);

	TSharedPtr<FJsonValue> RootValue;

	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonData);
	if (!FJsonSerializer::Deserialize(JsonReader, RootValue))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonObject = RootValue->AsObject();
	if (!JsonObject)
		return nullptr;

	TSharedPtr<FglTFRuntimeParser> Parser = MakeShared<FglTFRuntimeParser>(JsonObject.ToSharedRef(), LoaderConfig.GetMatrix(), LoaderConfig.SceneScale);

	if (Parser)
	{
		if (LoaderConfig.bAllowExternalFiles && !LoaderConfig.OverrideBaseDirectory.IsEmpty())
		{
			if (LoaderConfig.bOverrideBaseDirectoryFromContentDir)
			{
				Parser->BaseDirectory = FPaths::Combine(FPaths::ProjectContentDir(), LoaderConfig.OverrideBaseDirectory);
			}
			else
			{
				Parser->BaseDirectory = LoaderConfig.OverrideBaseDirectory;
			}
		}
		Parser->DefaultPrefixForUnnamedNodes = LoaderConfig.PrefixForUnnamedNodes;
		Parser->Archive = InArchive;
		Parser->AssetUserDataClasses = LoaderConfig.AssetUserDataClasses;
	}

	return Parser;
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromBinary(const uint8* DataPtr, int64 DataNum, const FglTFRuntimeConfig& LoaderConfig, TSharedPtr<FglTFRuntimeArchive> InArchive)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromBinary, FColor::Magenta);

	FString JsonData;
	TArray64<uint8> BinaryBuffer;

	bool bJsonFound = false;
	bool bBinaryFound = false;
	int64 BlobIndex = 12;

	while (BlobIndex < DataNum)
	{
		if (BlobIndex + 8 > DataNum)
		{
			return nullptr;
		}

		uint32* ChunkLength = (uint32*)&DataPtr[BlobIndex];
		uint32* ChunkType = (uint32*)&DataPtr[BlobIndex + 4];

		BlobIndex += 8;

		if ((BlobIndex + *ChunkLength) > DataNum)
		{
			return nullptr;
		}

		if (*ChunkType == 0x4E4F534A && !bJsonFound)
		{
			bJsonFound = true;
			FFileHelper::BufferToString(JsonData, &DataPtr[BlobIndex], *ChunkLength);
		}

		else if (*ChunkType == 0x004E4942 && !bBinaryFound)
		{
			bBinaryFound = true;
			BinaryBuffer.Append(&DataPtr[BlobIndex], *ChunkLength);
		}

		BlobIndex += *ChunkLength;
	}

	if (!bJsonFound)
	{
		return nullptr;
	}

	TSharedPtr<FglTFRuntimeParser> Parser = FromString(JsonData, LoaderConfig, InArchive);

	if (Parser)
	{
		if (bBinaryFound)
		{
			Parser->SetBinaryBuffer(BinaryBuffer);
		}
	}

	return Parser;
}

void FglTFRuntimeParser::LoadAndFillBaseMaterials()
{
	UMaterialInterface* OpaqueMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeBase"));
	if (OpaqueMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::Opaque, OpaqueMaterial);
	}

	UMaterialInterface* TranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTranslucent_Inst"));
	if (TranslucentMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::Translucent, TranslucentMaterial);
	}

	UMaterialInterface* TwoSidedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTwoSided_Inst"));
	if (TwoSidedMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSided, TwoSidedMaterial);
	}

	UMaterialInterface* TwoSidedTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTwoSidedTranslucent_Inst"));
	if (TwoSidedTranslucentMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedTranslucent, TwoSidedTranslucentMaterial);
	}

	UMaterialInterface* MaskedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeMasked_Inst"));
	if (MaskedMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::Masked, MaskedMaterial);
	}

	UMaterialInterface* TwoSidedMaskedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTwoSidedMasked_Inst"));
	if (TwoSidedMaskedMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedMasked, TwoSidedMaskedMaterial);
	}

	// KHR_materials_pbrSpecularGlossiness
	UMaterialInterface* SGOpaqueMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntime_SG_Base"));
	if (SGOpaqueMaterial)
	{
		SpecularGlossinessMaterialsMap.Add(EglTFRuntimeMaterialType::Opaque, SGOpaqueMaterial);
	}

	UMaterialInterface* SGTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntime_SG_Translucent_Inst"));
	if (SGTranslucentMaterial)
	{
		SpecularGlossinessMaterialsMap.Add(EglTFRuntimeMaterialType::Translucent, SGTranslucentMaterial);
	}

	UMaterialInterface* SGTwoSidedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntime_SG_TwoSided_Inst"));
	if (SGTwoSidedMaterial)
	{
		SpecularGlossinessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSided, SGTwoSidedMaterial);
	}

	UMaterialInterface* SGTwoSidedTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntime_SG_TwoSidedTranslucent_Inst"));
	if (SGTwoSidedTranslucentMaterial)
	{
		SpecularGlossinessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedTranslucent, SGTwoSidedTranslucentMaterial);
	}


	// KHR_materials_unlit 
	UMaterialInterface* UnlitOpaqueMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_Unlit_glTFRuntimeBase"));
	if (UnlitOpaqueMaterial)
	{
		UnlitMaterialsMap.Add(EglTFRuntimeMaterialType::Opaque, UnlitOpaqueMaterial);
	}

	UMaterialInterface* UnlitTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_Unlit_glTFRuntimeTranslucent_Inst"));
	if (UnlitTranslucentMaterial)
	{
		UnlitMaterialsMap.Add(EglTFRuntimeMaterialType::Translucent, UnlitTranslucentMaterial);
	}

	UMaterialInterface* UnlitTwoSidedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_Unlit_glTFRuntimeTwoSided_Inst"));
	if (UnlitTwoSidedMaterial)
	{
		UnlitMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSided, UnlitTwoSidedMaterial);
	}

	UMaterialInterface* UnlitTwoSidedTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_Unlit_glTFRuntimeTwoSidedTranslucent_Inst"));
	if (UnlitTwoSidedTranslucentMaterial)
	{
		UnlitMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedTranslucent, UnlitTwoSidedTranslucentMaterial);
	}

	UMaterialInterface* UnlitMaskedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_Unlit_glTFRuntimeMasked_Inst"));
	if (UnlitMaskedMaterial)
	{
		UnlitMaterialsMap.Add(EglTFRuntimeMaterialType::Masked, UnlitMaskedMaterial);
	}

	UMaterialInterface* UnlitTwoSidedMaskedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_Unlit_glTFRuntimeTwoSidedMasked_Inst"));
	if (UnlitTwoSidedMaskedMaterial)
	{
		UnlitMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedMasked, UnlitTwoSidedMaskedMaterial);
	}

	// KHR_materials_transmission
	UMaterialInterface* TrasmissionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_Transmission_glTFRuntimeBase"));
	if (TranslucentMaterial)
	{
		TransmissionMaterialsMap.Add(EglTFRuntimeMaterialType::Opaque, TrasmissionMaterial);
		TransmissionMaterialsMap.Add(EglTFRuntimeMaterialType::Masked, TrasmissionMaterial);
		TransmissionMaterialsMap.Add(EglTFRuntimeMaterialType::Translucent, TrasmissionMaterial);
	}

	UMaterialInterface* TrasmissionTwoSidedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_Transmission_glTFRuntimeTwoSided_Inst"));
	if (TrasmissionTwoSidedMaterial)
	{
		TransmissionMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSided, TrasmissionTwoSidedMaterial);
		TransmissionMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedMasked, TrasmissionTwoSidedMaterial);
		TransmissionMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedTranslucent, TrasmissionTwoSidedMaterial);
	}

	// KHR_materials_transmission
	UMaterialInterface* ClearCoatMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_ClearCoat_glTFRuntimeBase"));
	if (ClearCoatMaterial)
	{
		ClearCoatMaterialsMap.Add(EglTFRuntimeMaterialType::Opaque, ClearCoatMaterial);
	}

	UMaterialInterface* ClearCoatTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_ClearCoat_glTFRuntimeTranslucent_Inst"));
	if (ClearCoatTranslucentMaterial)
	{
		ClearCoatMaterialsMap.Add(EglTFRuntimeMaterialType::Masked, ClearCoatTranslucentMaterial);
		ClearCoatMaterialsMap.Add(EglTFRuntimeMaterialType::Translucent, ClearCoatTranslucentMaterial);
	}

	UMaterialInterface* ClearCoatTwoSidedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_ClearCoat_glTFRuntimeTwoSided_Inst"));
	if (ClearCoatTwoSidedMaterial)
	{
		ClearCoatMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSided, ClearCoatTwoSidedMaterial);
	}

	UMaterialInterface* ClearCoatTwoSidedTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_ClearCoat_glTFRuntimeTwoSidedTranslucent_Inst"));
	if (ClearCoatTwoSidedTranslucentMaterial)
	{
		ClearCoatMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedMasked, ClearCoatTwoSidedTranslucentMaterial);
		ClearCoatMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedTranslucent, ClearCoatTwoSidedTranslucentMaterial);
	}
}


FglTFRuntimeParser::FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject, const FMatrix& InSceneBasis, float InSceneScale) : Root(JsonObject), SceneBasis(InSceneBasis), SceneScale(InSceneScale)
{
	bAllNodesCached = false;
	DownloadTime = 0;

	if (IsInGameThread())
	{
		LoadAndFillBaseMaterials();
	}
	else
	{
		FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([this]()
			{
				LoadAndFillBaseMaterials();
			}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
	}

	JsonObject->TryGetStringArrayField(TEXT("extensionsUsed"), ExtensionsUsed);
	JsonObject->TryGetStringArrayField(TEXT("extensionsRequired"), ExtensionsRequired);

	if (ExtensionsUsed.Contains("KHR_materials_variants"))
	{
		TArray<TSharedRef<FJsonObject>> MaterialsVariantsObjects = GetJsonObjectArrayFromRootExtension("KHR_materials_variants", "variants");
		for (TSharedRef<FJsonObject> MaterialsVariantsObject : MaterialsVariantsObjects)
		{
			FString VariantName;
			if (MaterialsVariantsObject->TryGetStringField(TEXT("name"), VariantName))
			{
				MaterialsVariants.Add(VariantName);
			}
			else
			{
				MaterialsVariants.Add("");
			}
		}
	}

	if (ExtensionsRequired.Contains("KHR_draco_mesh_compression"))
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("glTFRuntimeDraco")))
		{
			UE_LOG(LogGLTFRuntime, Warning, TEXT("KHR_draco_mesh_compression required but glTFRuntimeDraco module is not loaded"));
		}
	}
}

bool FglTFRuntimeParser::LoadNodes()
{
	if (bAllNodesCached)
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonNodes;

	// no nodes ?
	if (!Root->TryGetArrayField(TEXT("nodes"), JsonNodes))
	{
		return false;
	}

	// first round for getting all nodes
	for (int32 Index = 0; Index < JsonNodes->Num(); Index++)
	{
		TSharedPtr<FJsonObject> JsonNodeObject = (*JsonNodes)[Index]->AsObject();
		if (!JsonNodeObject)
		{
			return false;
		}

		FglTFRuntimeNode Node;
		if (!LoadNode_Internal(Index, JsonNodeObject.ToSharedRef(), JsonNodes->Num(), Node))
		{
			return false;
		}

		AllNodesCache.Add(Node);
	}

	for (FglTFRuntimeNode& Node : AllNodesCache)
	{
		FixNodeParent(Node);
	}

	bAllNodesCached = true;

	return true;
}

void FglTFRuntimeParser::FixNodeParent(FglTFRuntimeNode& Node)
{
	for (int32 Index : Node.ChildrenIndices)
	{
		AllNodesCache[Index].ParentIndex = Node.Index;
		FixNodeParent(AllNodesCache[Index]);
	}
}

bool FglTFRuntimeParser::LoadNodesRecursive(const int32 NodeIndex, TArray<FglTFRuntimeNode>& Nodes)
{
	FglTFRuntimeNode Node;
	if (!LoadNode(NodeIndex, Node))
	{
		AddError("LoadNodesRecursive()", FString::Printf(TEXT("Unable to load node %d"), NodeIndex));
		return false;
	}

	Nodes.Add(Node);

	for (int32 ChildIndex : Node.ChildrenIndices)
	{
		if (!LoadNodesRecursive(ChildIndex, Nodes))
		{
			return false;
		}
	}

	return true;
}

int32 FglTFRuntimeParser::GetNumMeshes() const
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Root->TryGetArrayField(TEXT("meshes"), JsonArray))
	{
		return JsonArray->Num();
	}
	return 0;
}

int32 FglTFRuntimeParser::GetNumImages() const
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Root->TryGetArrayField(TEXT("images"), JsonArray))
	{
		return JsonArray->Num();
	}
	return 0;
}

int32 FglTFRuntimeParser::GetNumAnimations() const
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Root->TryGetArrayField(TEXT("animations"), JsonArray))
	{
		return JsonArray->Num();
	}
	return 0;
}

bool FglTFRuntimeParser::LoadScenes(TArray<FglTFRuntimeScene>& Scenes)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonScenes;
	// no scenes ?
	if (!Root->TryGetArrayField(TEXT("scenes"), JsonScenes))
	{
		return false;
	}

	for (int32 Index = 0; Index < JsonScenes->Num(); Index++)
	{
		FglTFRuntimeScene Scene;
		if (!LoadScene(Index, Scene))
		{
			return false;
		}
		Scenes.Add(Scene);
	}

	return true;
}

bool FglTFRuntimeParser::CheckJsonIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 Index, TArray<TSharedRef<FJsonValue>>& JsonItems) const
{
	if (Index < 0)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (!JsonObject->TryGetArrayField(FieldName, JsonArray))
	{
		return false;
	}

	if (Index >= JsonArray->Num())
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& JsonItem : (*JsonArray))
	{
		JsonItems.Add(JsonItem.ToSharedRef());
	}

	return true;
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetJsonObjectFromIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 Index) const
{
	TArray<TSharedRef<FJsonValue>> JsonArray;
	if (!CheckJsonIndex(JsonObject, FieldName, Index, JsonArray))
	{
		return nullptr;
	}

	return JsonArray[Index]->AsObject();
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetJsonObjectFromExtensionIndex(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName, const int32 Index)
{
	if (Index < 0)
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (!JsonObject->TryGetObjectField(TEXT("extensions"), JsonExtensionsObject))
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
	if (!(*JsonExtensionsObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
	{
		return nullptr;
	}

	return GetJsonObjectFromIndex(JsonExtensionObject->ToSharedRef(), FieldName, Index);
}

TArray<TSharedRef<FJsonObject>> FglTFRuntimeParser::GetJsonObjectArrayFromExtension(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName)
{
	TArray<TSharedRef<FJsonObject>> Objects;

	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (JsonObject->TryGetObjectField(TEXT("extensions"), JsonExtensionsObject))
	{
		const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
		if ((*JsonExtensionsObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items;
			if ((*JsonExtensionObject)->TryGetArrayField(FieldName, Items))
			{
				for (const TSharedPtr<FJsonValue>& Item : (*Items))
				{
					const TSharedPtr<FJsonObject>* Object = nullptr;
					if (Item->TryGetObject(Object))
					{
						Objects.Add(Object->ToSharedRef());
					}
				}
			}
		}
	}

	return Objects;
}

TArray<TSharedRef<FJsonObject>> FglTFRuntimeParser::GetJsonObjectArrayOfObjects(TSharedRef<FJsonObject> JsonObject, const FString& FieldName)
{
	TArray<TSharedRef<FJsonObject>> Items;

	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!JsonObject->TryGetArrayField(FieldName, JsonArray))
	{
		return Items;
	}

	for (const TSharedPtr<FJsonValue>& JsonItem : *JsonArray)
	{
		const TSharedPtr<FJsonObject>* JsonItemObject = nullptr;
		if (JsonItem->TryGetObject(JsonItemObject))
		{
			Items.Add(JsonItemObject->ToSharedRef());
		}
	}

	return Items;
}

FVector4 FglTFRuntimeParser::GetJsonObjectVector4(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const FVector4 DefaultValue)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!JsonObject->TryGetArrayField(FieldName, JsonArray))
	{
		return DefaultValue;
	}

	FVector4 NewValue = DefaultValue;
	for (int32 Index = 0; Index < 4; Index++)
	{
		if (JsonArray->IsValidIndex(Index))
		{
			TSharedPtr<FJsonValue> Item = (*JsonArray)[Index];
			if (Item)
			{
				double Value = 0;
				if (Item->TryGetNumber(Value))
				{
					NewValue[Index] = Value;
				}
			}
		}
		else
		{
			break;
		}
	}

	return NewValue;
}

FString FglTFRuntimeParser::GetJsonObjectString(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const FString& DefaultValue) const
{
	FString Value;
	if (!JsonObject->TryGetStringField(FieldName, Value))
	{
		return DefaultValue;
	}
	return Value;
}

double FglTFRuntimeParser::GetJsonObjectNumber(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const double DefaultValue)
{
	double Value;
	if (!JsonObject->TryGetNumberField(FieldName, Value))
	{
		return DefaultValue;
	}
	return Value;
}

bool FglTFRuntimeParser::GetJsonObjectBool(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const bool DefaultValue)
{
	bool Value;
	if (!JsonObject->TryGetBoolField(FieldName, Value))
	{
		return DefaultValue;
	}
	return Value;
}

int32 FglTFRuntimeParser::GetJsonObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 DefaultValue)
{
	int64 Value;
	if (!JsonObject->TryGetNumberField(FieldName, Value))
	{
		return DefaultValue;
	}
	return (int32)Value;
}

int32 FglTFRuntimeParser::GetJsonExtensionObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName, const int32 DefaultValue)
{
	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (!JsonObject->TryGetObjectField(TEXT("extensions"), JsonExtensionsObject))
	{
		return DefaultValue;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
	if (!(*JsonExtensionsObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
	{
		return DefaultValue;
	}

	return GetJsonObjectIndex(JsonExtensionObject->ToSharedRef(), FieldName, DefaultValue);
}

double FglTFRuntimeParser::GetJsonExtensionObjectNumber(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName, const double DefaultValue)
{
	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (!JsonObject->TryGetObjectField(TEXT("extensions"), JsonExtensionsObject))
	{
		return DefaultValue;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
	if (!(*JsonExtensionsObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
	{
		return DefaultValue;
	}

	return GetJsonObjectNumber(JsonExtensionObject->ToSharedRef(), FieldName, DefaultValue);
}

TArray<int32> FglTFRuntimeParser::GetJsonExtensionObjectIndices(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName)
{
	TArray<int32> Indices;
	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (!JsonObject->TryGetObjectField(TEXT("extensions"), JsonExtensionsObject))
	{
		return Indices;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
	if (!(*JsonExtensionsObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
	{
		return Indices;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (!(*JsonExtensionObject)->TryGetArrayField(FieldName, JsonArray))
	{
		return Indices;
	}

	for (TSharedPtr<FJsonValue> JsonItem : *JsonArray)
	{
		int32 Index;
		if (!JsonItem->TryGetNumber(Index))
		{
			return Indices;
		}
		Indices.Add(Index);
	}

	return Indices;
}

TArray<double> FglTFRuntimeParser::GetJsonExtensionObjectNumbers(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName)
{
	TArray<double> Numbers;
	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (!JsonObject->TryGetObjectField(TEXT("extensions"), JsonExtensionsObject))
	{
		return Numbers;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
	if (!(*JsonExtensionsObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
	{
		return Numbers;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (!(*JsonExtensionObject)->TryGetArrayField(FieldName, JsonArray))
	{
		return Numbers;
	}

	for (TSharedPtr<FJsonValue> JsonItem : *JsonArray)
	{
		double Value;
		if (!JsonItem->TryGetNumber(Value))
		{
			return Numbers;
		}
		Numbers.Add(Value);
	}

	return Numbers;
}

bool FglTFRuntimeParser::LoadScene(int32 SceneIndex, FglTFRuntimeScene& Scene)
{
	TSharedPtr<FJsonObject> JsonSceneObject = GetJsonObjectFromRootIndex("scenes", SceneIndex);
	if (!JsonSceneObject)
	{
		return false;
	}

	Scene.Index = SceneIndex;
	Scene.Name = GetJsonObjectString(JsonSceneObject.ToSharedRef(), "name", FString::FromInt(Scene.Index));

	const TArray<TSharedPtr<FJsonValue>>* JsonSceneNodes;
	if (JsonSceneObject->TryGetArrayField(TEXT("nodes"), JsonSceneNodes))
	{
		for (TSharedPtr<FJsonValue> JsonSceneNode : *JsonSceneNodes)
		{
			int64 NodeIndex;
			if (!JsonSceneNode->TryGetNumber(NodeIndex))
			{
				return false;
			}

			FglTFRuntimeNode SceneNode;
			if (!LoadNode(NodeIndex, SceneNode))
			{
				return false;
			}
			Scene.RootNodesIndices.Add(SceneNode.Index);
		}
	}

	return true;
}

bool FglTFRuntimeParser::GetAllNodes(TArray<FglTFRuntimeNode>& Nodes)
{
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
		{
			return false;
		}
	}

	Nodes = AllNodesCache;

	return true;
}

int32 FglTFRuntimeParser::AddFakeRootNode(const FString& BaseName)
{
	TArray<int32> OrphanNodes;
	TArray<FglTFRuntimeNode> AllNodes;

	if (!GetAllNodes(AllNodes))
	{
		return INDEX_NONE;
	}

	for (int32 NodeIndex = 0; NodeIndex < AllNodes.Num(); NodeIndex++)
	{
		FglTFRuntimeNode Node;
		if (!LoadNode(NodeIndex, Node))
		{
			return INDEX_NONE;
		}

		if (Node.ParentIndex <= INDEX_NONE)
		{
			OrphanNodes.Add(NodeIndex);
			AllNodesCache[NodeIndex].ParentIndex = AllNodes.Num();
		}
	}

	FglTFRuntimeNode NewNode;
	NewNode.Name = BaseName;
	NewNode.Index = AllNodes.Num();
	NewNode.ChildrenIndices = OrphanNodes;

	AllNodesCache.Add(NewNode);

	return NewNode.Index;
}

bool FglTFRuntimeParser::LoadNode(const int32 Index, FglTFRuntimeNode& Node)
{
	// a bit hacky, but allows zero-copy for cached values
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
		{
			return false;
		}
	}

	if (Index >= AllNodesCache.Num())
	{
		return false;
	}

	Node = AllNodesCache[Index];
	return true;
}

bool FglTFRuntimeParser::LoadNodeByName(const FString& Name, FglTFRuntimeNode& Node)
{
	// a bit hacky, but allows zero-copy for cached values
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
		{
			return false;
		}
	}

	for (FglTFRuntimeNode& NodeRef : AllNodesCache)
	{
		if (NodeRef.Name == Name)
		{
			Node = NodeRef;
			return true;
		}
	}

	return false;
}

bool FglTFRuntimeParser::LoadJointByName(const int64 RootBoneIndex, const FString& Name, FglTFRuntimeNode& Node)
{
	// a bit hacky, but allows zero-copy for cached values
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
		{
			return false;
		}
	}

	if (!LoadNode(RootBoneIndex, Node))
	{
		return false;
	}

	if (Node.Name == Name)
	{
		return true;
	}

	for (int32 Index : Node.ChildrenIndices)
	{
		FglTFRuntimeNode ChildNode;
		if (LoadJointByName(Index, Name, ChildNode))
		{
			Node = ChildNode;
			return true;
		}
	}

	return false;
}


void FglTFRuntimeParser::AddError(const FString& ErrorContext, const FString& ErrorMessage)
{
	FString FullMessage = ErrorContext + ": " + ErrorMessage;
	Errors.Add(FullMessage);
	UE_LOG(LogGLTFRuntime, Error, TEXT("%s"), *FullMessage);
	if (OnError.IsBound())
	{
		OnError.Broadcast(ErrorContext, ErrorMessage);
	}
}

bool FglTFRuntimeParser::HasErrors() const
{
	return Errors.Num() > 0;
}

const TArray<FString>& FglTFRuntimeParser::GetErrors() const
{
	return Errors;
}

void FglTFRuntimeParser::ClearErrors()
{
	Errors.Empty();
}

bool FglTFRuntimeParser::FillJsonMatrix(const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues, FMatrix& Matrix)
{
	if (JsonMatrixValues->Num() != 16)
		return false;

	for (int32 i = 0; i < 16; i++)
	{
		double Value;
		if (!(*JsonMatrixValues)[i]->TryGetNumber(Value))
		{
			return false;
		}

		Matrix.M[i / 4][i % 4] = Value;
	}

	return true;
}

bool FglTFRuntimeParser::LoadNode_Internal(int32 Index, TSharedRef<FJsonObject> JsonNodeObject, int32 NodesCount, FglTFRuntimeNode& Node)
{
	Node.Index = Index;
	Node.Name = GetJsonObjectString(JsonNodeObject, "name", DefaultPrefixForUnnamedNodes + FString::FromInt(Node.Index));

	Node.MeshIndex = GetJsonObjectIndex(JsonNodeObject, "mesh", INDEX_NONE);

	Node.SkinIndex = GetJsonObjectIndex(JsonNodeObject, "skin", INDEX_NONE);

	Node.CameraIndex = GetJsonObjectIndex(JsonNodeObject, "camera", INDEX_NONE);

	FMatrix Matrix = FMatrix::Identity;

	const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues;
	if (JsonNodeObject->TryGetArrayField(TEXT("matrix"), JsonMatrixValues))
	{
		if (!FillJsonMatrix(JsonMatrixValues, Matrix))
		{
			return false;
		}
	}

	FVector MatrixScaleToReapply = FVector::OneVector;
	bool bMatrixScaleNeedsToBeReapplied = false;

	const TArray<TSharedPtr<FJsonValue>>* JsonScaleValues;
	if (JsonNodeObject->TryGetArrayField(TEXT("scale"), JsonScaleValues))
	{
		FVector MatrixScale;
		if (!GetJsonVector<3>(JsonScaleValues, MatrixScale))
		{
			return false;
		}

		if (MatrixScale.IsNearlyZero())
		{
			bMatrixScaleNeedsToBeReapplied = true;
			MatrixScaleToReapply = MatrixScale;
		}

		Matrix *= FScaleMatrix(MatrixScale);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonRotationValues;
	if (JsonNodeObject->TryGetArrayField(TEXT("rotation"), JsonRotationValues))
	{
		FVector4 Vector;
		if (!GetJsonVector<4>(JsonRotationValues, Vector))
		{
			return false;
		}
		FQuat Quat = { Vector.X, Vector.Y, Vector.Z, Vector.W };
		Matrix *= FQuatRotationMatrix(Quat);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTranslationValues;
	if (JsonNodeObject->TryGetArrayField(TEXT("translation"), JsonTranslationValues))
	{
		FVector Translation;
		if (!GetJsonVector<3>(JsonTranslationValues, Translation))
		{
			return false;
		}

		Matrix *= FTranslationMatrix(Translation);
	}

	Matrix.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
	Node.Transform = FTransform(SceneBasis.Inverse() * Matrix * SceneBasis);
	// this is a hack for allowing very small scaling factors (common in quantized meshes)
	// it is required as the FTransform ctor generates 0 scaling for small numbers
	if (bMatrixScaleNeedsToBeReapplied)
	{
		Node.Transform.SetScale3D(MatrixScaleToReapply);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonChildren;
	if (JsonNodeObject->TryGetArrayField(TEXT("children"), JsonChildren))
	{
		for (int32 i = 0; i < JsonChildren->Num(); i++)
		{
			int64 ChildIndex;
			if (!(*JsonChildren)[i]->TryGetNumber(ChildIndex))
			{
				return false;
			}

			if (ChildIndex >= NodesCount)
			{
				return false;
			}

			Node.ChildrenIndices.Add(ChildIndex);
		}
	}

	return true;
}

FTransform FglTFRuntimeParser::RawMatrixToRebasedTransform(const FMatrix& Matrix) const
{
	FMatrix MatrixCopy = Matrix;
	MatrixCopy.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
	return FTransform(SceneBasis.Inverse() * MatrixCopy * SceneBasis);
}

bool FglTFRuntimeParser::LoadAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, float& Duration, FString& Name, TFunctionRef<void(const FglTFRuntimeNode& Node, const FString& Path, const FglTFRuntimeAnimationCurve& Curve)> Callback, TFunctionRef<bool(const FglTFRuntimeNode& Node)> NodeFilter, const TArray<FglTFRuntimePathItem>& OverrideTrackNameFromExtension)
{
	Name = GetJsonObjectString(JsonAnimationObject, "name", "");

	const TArray<TSharedPtr<FJsonValue>>* JsonSamplers;
	if (!JsonAnimationObject->TryGetArrayField(TEXT("samplers"), JsonSamplers))
	{
		return false;
	}

	Duration = 0.f;

	TArray<FglTFRuntimeAnimationCurve> Samplers;

	for (int32 SamplerIndex = 0; SamplerIndex < JsonSamplers->Num(); SamplerIndex++)
	{
		TSharedPtr<FJsonObject> JsonSamplerObject = (*JsonSamplers)[SamplerIndex]->AsObject();
		if (!JsonSamplerObject)
		{
			return false;
		}

		FglTFRuntimeAnimationCurve AnimationCurve;

		if (!BuildFromAccessorField(JsonSamplerObject.ToSharedRef(), "input", AnimationCurve.Timeline, { 5126 }, INDEX_NONE, false, nullptr))
		{
			AddError("LoadAnimation_Internal()", FString::Printf(TEXT("Unable to retrieve \"input\" from sampler %d"), SamplerIndex));
			return false;
		}

		if (!BuildFromAccessorField(JsonSamplerObject.ToSharedRef(), "output", AnimationCurve.Values, { 1, 3, 4 }, { 5126, 5120, 5121, 5122, 5123 }, INDEX_NONE, true, nullptr))
		{
			AddError("LoadAnimation_Internal()", FString::Printf(TEXT("Unable to retrieve \"output\" from sampler %d"), SamplerIndex));
			return false;
		}

		FString SamplerInterpolation;
		if (!JsonSamplerObject->TryGetStringField(TEXT("interpolation"), SamplerInterpolation))
		{
			SamplerInterpolation = "LINEAR";
		}

		// get animation valid duration
		for (float Time : AnimationCurve.Timeline)
		{
			if (Time > Duration)
			{
				Duration = Time;
			}
		}

		// extract tangents and value (unfortunately Unreal does not support Cubic Splines for skeletal animations)
		if (SamplerInterpolation == "CUBICSPLINE")
		{
			TArray<FVector4> CubicValues;
			for (int32 TimeIndex = 0; TimeIndex < AnimationCurve.Timeline.Num(); TimeIndex++)
			{
				// gather A, V and B
				FVector4 InTangent = AnimationCurve.Values[TimeIndex * 3];
				FVector4 Value = AnimationCurve.Values[TimeIndex * 3 + 1];
				FVector4 OutTangent = AnimationCurve.Values[TimeIndex * 3 + 2];

				AnimationCurve.InTangents.Add(InTangent);
				AnimationCurve.OutTangents.Add(OutTangent);
				CubicValues.Add(Value);
			}

			AnimationCurve.Values = CubicValues;
		}

		AnimationCurve.bStep = SamplerInterpolation == "STEP";

		Samplers.Add(AnimationCurve);
	}


	const TArray<TSharedPtr<FJsonValue>>* JsonChannels;
	if (!JsonAnimationObject->TryGetArrayField(TEXT("channels"), JsonChannels))
	{
		return false;
	}

	for (int32 ChannelIndex = 0; ChannelIndex < JsonChannels->Num(); ChannelIndex++)
	{
		TSharedPtr<FJsonObject> JsonChannelObject = (*JsonChannels)[ChannelIndex]->AsObject();
		if (!JsonChannelObject)
			return false;

		int32 Sampler;
		if (!JsonChannelObject->TryGetNumberField(TEXT("sampler"), Sampler))
		{
			return false;
		}

		if (Sampler >= Samplers.Num())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* JsonTargetObject;
		if (!JsonChannelObject->TryGetObjectField(TEXT("target"), JsonTargetObject))
		{
			return false;
		}

		FglTFRuntimeNode Node;
		if (OverrideTrackNameFromExtension.Num() > 0)
		{
			const TSharedPtr<FJsonObject>* JsonTargetExtensions;
			if ((*JsonTargetObject)->TryGetObjectField(TEXT("extensions"), JsonTargetExtensions))
			{
				TSharedPtr<FJsonValue> JsonTrackName = GetJSONObjectFromRelativePath(JsonTargetExtensions->ToSharedRef(), OverrideTrackNameFromExtension);
				if (JsonTrackName)
				{
					JsonTrackName->TryGetString(Node.Name);
				}
			}
		}

		if (Node.Name.IsEmpty())
		{
			int64 NodeIndex;
			if (!(*JsonTargetObject)->TryGetNumberField(TEXT("node"), NodeIndex))
			{
				return false;
			}

			if (!LoadNode(NodeIndex, Node))
			{
				return false;
			}
		}

		if (!NodeFilter(Node))
		{
			continue;
		}

		FString Path;
		if (!(*JsonTargetObject)->TryGetStringField(TEXT("path"), Path))
		{
			return false;
		}

		Callback(Node, Path, Samplers[Sampler]);
	}

	return true;
}

TArray<FString> FglTFRuntimeParser::GetCamerasNames()
{
	TArray<FString> CamerasNames;
	const TArray<TSharedPtr<FJsonValue>>* JsonCameras;
	if (!Root->TryGetArrayField(TEXT("cameras"), JsonCameras))
	{
		return CamerasNames;
	}

	for (TSharedPtr<FJsonValue> JsonCamera : *JsonCameras)
	{
		TSharedPtr<FJsonObject> JsonCameraObject = JsonCamera->AsObject();
		if (!JsonCameraObject)
		{
			continue;
		}

		FString CameraName;
		if (!JsonCameraObject->TryGetStringField(TEXT("name"), CameraName))
		{
			continue;
		}

		CamerasNames.Add(CameraName);
	}

	return CamerasNames;
}

UglTFRuntimeAnimationCurve* FglTFRuntimeParser::LoadNodeAnimationCurve(const int32 NodeIndex)
{
	FglTFRuntimeNode Node;
	if (!LoadNode(NodeIndex, Node))
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField(TEXT("animations"), JsonAnimations))
	{
		return nullptr;
	}

	UglTFRuntimeAnimationCurve* AnimationCurve = NewObject<UglTFRuntimeAnimationCurve>(GetTransientPackage(), NAME_None, RF_Public);

	FTransform OriginalTransform = FTransform(SceneBasis * Node.Transform.ToMatrixWithScale() * SceneBasis.Inverse());

	AnimationCurve->SetDefaultValues(OriginalTransform.GetLocation(), OriginalTransform.GetRotation(), OriginalTransform.GetRotation().Rotator(), OriginalTransform.GetScale3D());

	bool bAnimationFound = false;

	auto Callback = [&](const FglTFRuntimeNode& Node, const FString& Path, const FglTFRuntimeAnimationCurve& Curve)
		{
			if (Path == "translation")
			{
				if (Curve.Timeline.Num() != Curve.Values.Num())
				{
					AddError("LoadNodeAnimationCurve()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for translation on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
					return;
				}
				for (int32 TimeIndex = 0; TimeIndex < Curve.Timeline.Num(); TimeIndex++)
				{
					AnimationCurve->AddLocationValue(Curve.Timeline[TimeIndex], Curve.Values[TimeIndex] * SceneScale, Curve.bStep ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear);
				}
			}
			else if (Path == "rotation")
			{
				if (Curve.Timeline.Num() != Curve.Values.Num())
				{
					AddError("LoadNodeAnimationCurve()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for rotation on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
					return;
				}
				for (int32 TimeIndex = 0; TimeIndex < Curve.Timeline.Num(); TimeIndex++)
				{
					const FVector4 RotationValue = Curve.Values[TimeIndex];
					const FQuat Quat = FQuat(RotationValue.X, RotationValue.Y, RotationValue.Z, RotationValue.W);
					AnimationCurve->AddQuatValue(Curve.Timeline[TimeIndex], Quat, Curve.bStep ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear);
					AnimationCurve->AddRotatorValue(Curve.Timeline[TimeIndex], Quat.Rotator(), Curve.bStep ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear);
					const FMatrix RotationMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(Quat) * SceneBasis;
					AnimationCurve->AddConvertedQuaternion(Curve.Timeline[TimeIndex], RotationMatrix.ToQuat().GetNormalized(), Curve.bStep);

				}
			}
			else if (Path == "scale")
			{
				if (Curve.Timeline.Num() != Curve.Values.Num())
				{
					AddError("LoadNodeAnimationCurve()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for scale on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
					return;
				}
				for (int32 TimeIndex = 0; TimeIndex < Curve.Timeline.Num(); TimeIndex++)
				{
					AnimationCurve->AddScaleValue(Curve.Timeline[TimeIndex], Curve.Values[TimeIndex], Curve.bStep ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear);
				}
			}
			bAnimationFound = true;
		};

	for (int32 JsonAnimationIndex = 0; JsonAnimationIndex < JsonAnimations->Num(); JsonAnimationIndex++)
	{
		TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[JsonAnimationIndex]->AsObject();
		if (!JsonAnimationObject)
		{
			return nullptr;
		}
		float Duration;
		FString Name;
		if (!LoadAnimation_Internal(JsonAnimationObject.ToSharedRef(), Duration, Name, Callback, [&](const FglTFRuntimeNode& Node) -> bool { return Node.Index == NodeIndex; }, {}))
		{
			return nullptr;
		}
		// stop at the first found animation
		if (bAnimationFound)
		{
			AnimationCurve->glTFCurveAnimationIndex = JsonAnimationIndex;
			AnimationCurve->glTFCurveAnimationName = Name;
			AnimationCurve->glTFCurveAnimationDuration = Duration;
			AnimationCurve->BasisMatrix = SceneBasis;
			return AnimationCurve;
		}
	}

	return nullptr;
}

TArray<UglTFRuntimeAnimationCurve*> FglTFRuntimeParser::LoadAllNodeAnimationCurves(const int32 NodeIndex)
{
	TArray<UglTFRuntimeAnimationCurve*> AnimationCurves;

	FglTFRuntimeNode Node;
	if (!LoadNode(NodeIndex, Node))
	{
		return AnimationCurves;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField(TEXT("animations"), JsonAnimations))
	{
		return AnimationCurves;
	}

	UglTFRuntimeAnimationCurve* AnimationCurve = nullptr;

	FTransform OriginalTransform = FTransform(SceneBasis * Node.Transform.ToMatrixWithScale() * SceneBasis.Inverse());

	bool bAnimationFound = false;

	auto Callback = [&](const FglTFRuntimeNode& Node, const FString& Path, const FglTFRuntimeAnimationCurve& Curve)
		{
			if (Path == "translation")
			{
				if (Curve.Timeline.Num() != Curve.Values.Num())
				{
					AddError("LoadAllNodeAnimationCurves()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for translation on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
					return;
				}
				for (int32 TimeIndex = 0; TimeIndex < Curve.Timeline.Num(); TimeIndex++)
				{
					AnimationCurve->AddLocationValue(Curve.Timeline[TimeIndex], Curve.Values[TimeIndex] * SceneScale, Curve.bStep ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear);
				}
			}
			else if (Path == "rotation")
			{
				if (Curve.Timeline.Num() != Curve.Values.Num())
				{
					AddError("LoadAllNodeAnimationCurves()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for rotation on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
					return;
				}
				for (int32 TimeIndex = 0; TimeIndex < Curve.Timeline.Num(); TimeIndex++)
				{
					const FVector4 RotationValue = Curve.Values[TimeIndex];
					const FQuat Quat = FQuat(RotationValue.X, RotationValue.Y, RotationValue.Z, RotationValue.W);
					AnimationCurve->AddQuatValue(Curve.Timeline[TimeIndex], Quat, Curve.bStep ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear);
					AnimationCurve->AddRotatorValue(Curve.Timeline[TimeIndex], Quat.Rotator(), Curve.bStep ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear);
					const FMatrix RotationMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(Quat) * SceneBasis;
					AnimationCurve->AddConvertedQuaternion(Curve.Timeline[TimeIndex], RotationMatrix.ToQuat().GetNormalized(), Curve.bStep);
				}
			}
			else if (Path == "scale")
			{
				if (Curve.Timeline.Num() != Curve.Values.Num())
				{
					AddError("LoadAllNodeAnimationCurves()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for scale on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
					return;
				}
				for (int32 TimeIndex = 0; TimeIndex < Curve.Timeline.Num(); TimeIndex++)
				{
					AnimationCurve->AddScaleValue(Curve.Timeline[TimeIndex], Curve.Values[TimeIndex], Curve.bStep ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear);
				}
			}
			bAnimationFound = true;
		};

	for (int32 JsonAnimationIndex = 0; JsonAnimationIndex < JsonAnimations->Num(); JsonAnimationIndex++)
	{
		TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[JsonAnimationIndex]->AsObject();
		if (!JsonAnimationObject)
		{
			continue;
		}

		float Duration;
		FString Name;
		bAnimationFound = false;
		AnimationCurve = NewObject<UglTFRuntimeAnimationCurve>(GetTransientPackage(), NAME_None, RF_Public);
		AnimationCurve->SetDefaultValues(OriginalTransform.GetLocation(), OriginalTransform.GetRotation(), OriginalTransform.GetRotation().Rotator(), OriginalTransform.GetScale3D());
		if (!LoadAnimation_Internal(JsonAnimationObject.ToSharedRef(), Duration, Name, Callback, [&](const FglTFRuntimeNode& Node) -> bool { return Node.Index == NodeIndex; }, {}))
		{
			continue;
		}
		// stop at the first found animation
		if (bAnimationFound)
		{
			AnimationCurve->glTFCurveAnimationIndex = JsonAnimationIndex;
			AnimationCurve->glTFCurveAnimationName = Name;
			AnimationCurve->glTFCurveAnimationDuration = Duration;
			AnimationCurve->BasisMatrix = SceneBasis;
			AnimationCurves.Add(AnimationCurve);
		}
	}

	return AnimationCurves;
}

bool FglTFRuntimeParser::HasRoot(int32 Index, int32 RootIndex)
{
	if (Index == RootIndex)
	{
		return true;
	}

	FglTFRuntimeNode Node;
	if (!LoadNode(Index, Node))
	{
		return false;
	}

	while (Node.ParentIndex != INDEX_NONE)
	{
		if (!LoadNode(Node.ParentIndex, Node))
		{
			return false;
		}

		if (Node.Index == RootIndex)
		{
			return true;
		}
	}

	return false;
}

int32 FglTFRuntimeParser::FindTopRoot(int32 Index)
{
	FglTFRuntimeNode Node;
	if (!LoadNode(Index, Node))
		return INDEX_NONE;
	while (Node.ParentIndex != INDEX_NONE)
	{
		if (!LoadNode(Node.ParentIndex, Node))
			return INDEX_NONE;
	}

	return Node.Index;
}

int32 FglTFRuntimeParser::FindCommonRoot(const TArray<int32>& Indices)
{
	int32 CurrentRootIndex = Indices[0];
	bool bTryNextParent = true;

	while (bTryNextParent && CurrentRootIndex > INDEX_NONE)
	{
		FglTFRuntimeNode Node;
		if (!LoadNode(CurrentRootIndex, Node))
		{
			return INDEX_NONE;
		}

		bTryNextParent = false;
		for (int32 Index : Indices)
		{
			if (!HasRoot(Index, CurrentRootIndex))
			{
				bTryNextParent = true;
				CurrentRootIndex = Node.ParentIndex;
				break;
			}
		}
	}

	return CurrentRootIndex;
}

bool FglTFRuntimeParser::LoadCameraIntoCameraComponent(const int32 CameraIndex, UCameraComponent* CameraComponent)
{
	if (!CameraComponent)
	{
		AddError("LoadCameraIntoCameraComponent()", "No valid CameraComponent specified.");
		return false;
	}

	TSharedPtr<FJsonObject> CameraObject = GetJsonObjectFromRootIndex("cameras", CameraIndex);
	if (!CameraObject)
	{
		AddError("LoadCameraIntoCameraComponent()", "Invalid Camera Index.");
		return false;
	}

	FString CameraType = GetJsonObjectString(CameraObject.ToSharedRef(), "type", "");
	if (CameraType.IsEmpty())
	{
		AddError("LoadCameraIntoCameraComponent()", "No Camera type specified.");
		return false;
	}

	if (CameraType.Equals("perspective", ESearchCase::IgnoreCase))
	{
		CameraComponent->ProjectionMode = ECameraProjectionMode::Perspective;
		const TSharedPtr<FJsonObject>* PerspectiveObject;
		if (CameraObject->TryGetObjectField(TEXT("perspective"), PerspectiveObject))
		{
			double AspectRatio;
			if ((*PerspectiveObject)->TryGetNumberField(TEXT("aspectRatio"), AspectRatio))
			{
				CameraComponent->AspectRatio = AspectRatio;
			}

			double YFov;
			if ((*PerspectiveObject)->TryGetNumberField(TEXT("yfov"), YFov))
			{
				CameraComponent->FieldOfView = FMath::RadiansToDegrees(YFov) * CameraComponent->AspectRatio;
			}
		}

		return true;
	}

	if (CameraType.Equals("orthographic", ESearchCase::IgnoreCase))
	{
		CameraComponent->ProjectionMode = ECameraProjectionMode::Orthographic;
		const TSharedPtr<FJsonObject>* OrthographicObject;
		if (CameraObject->TryGetObjectField(TEXT("orthographic"), OrthographicObject))
		{
			double XMag;
			if (!(*OrthographicObject)->TryGetNumberField(TEXT("xmag"), XMag))
			{
				AddError("LoadCameraIntoCameraComponent()", "No Orthographic Width specified.");
				return false;
			}
			double YMag;
			if (!(*OrthographicObject)->TryGetNumberField(TEXT("ymag"), YMag))
			{
				AddError("LoadCameraIntoCameraComponent()", "No Orthographic Height specified.");
				return false;
			}
			double ZFar;
			if (!(*OrthographicObject)->TryGetNumberField(TEXT("zfar"), ZFar))
			{
				AddError("LoadCameraIntoCameraComponent()", "No Orthographic Far specified.");
				return false;
			}
			double ZNear;
			if (!(*OrthographicObject)->TryGetNumberField(TEXT("znear"), ZNear))
			{
				AddError("LoadCameraIntoCameraComponent()", "No Orthographic Near specified.");
				return false;
			}

			CameraComponent->AspectRatio = XMag / YMag;
			CameraComponent->OrthoWidth = XMag * SceneScale;

			CameraComponent->OrthoFarClipPlane = ZFar * SceneScale;
			CameraComponent->OrthoNearClipPlane = ZNear * SceneScale;
		}
		return true;
	}

	AddError("LoadCameraIntoCameraComponent()", "Unsupported Camera Type.");
	return false;
}

USkeleton* FglTFRuntimeParser::LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	TSharedPtr<FJsonObject> JsonSkinObject = GetJsonObjectFromRootIndex("skins", SkinIndex);
	if (!JsonSkinObject)
	{
		return nullptr;
	}

	if (CanReadFromCache(SkeletonConfig.CacheMode) && SkeletonsCache.Contains(SkinIndex))
	{
		return SkeletonsCache[SkinIndex];
	}

	TMap<int32, FName> BoneMap;

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Public);
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public);

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
#else
	FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;
#endif

	if (!FillReferenceSkeleton(JsonSkinObject.ToSharedRef(), RefSkeleton, BoneMap, SkeletonConfig))
	{
		AddError("FillReferenceSkeleton()", "Unable to fill RefSkeleton.");
		return nullptr;
	}

	if (SkeletonConfig.bNormalizeSkeletonScale)
	{
		NormalizeSkeletonScale(RefSkeleton);
	}

	if (SkeletonConfig.bClearRotations || SkeletonConfig.CopyRotationsFrom)
	{
		ClearSkeletonRotations(RefSkeleton);
	}

	if (SkeletonConfig.CopyRotationsFrom)
	{
		CopySkeletonRotationsFrom(RefSkeleton, SkeletonConfig.CopyRotationsFrom->GetReferenceSkeleton());
	}

	if (SkeletonConfig.BonesDeltaTransformMap.Num() > 0)
	{
		AddSkeletonDeltaTranforms(RefSkeleton, SkeletonConfig.BonesDeltaTransformMap);
	}

	Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

	if (CanWriteToCache(SkeletonConfig.CacheMode))
	{
		SkeletonsCache.Add(SkinIndex, Skeleton);
	}

	return Skeleton;
}

USkeleton* FglTFRuntimeParser::LoadSkeletonFromNode(const FglTFRuntimeNode& Node, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	TMap<int32, FName> BoneMap;

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Public);
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public);

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
#else
	FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;
#endif

	if (!FillReferenceSkeletonFromNode(Node, RefSkeleton, BoneMap, SkeletonConfig))
	{
		AddError("FillReferenceSkeleton()", "Unable to fill RefSkeleton.");
		return nullptr;
	}

	if (SkeletonConfig.bNormalizeSkeletonScale)
	{
		NormalizeSkeletonScale(RefSkeleton);
	}

	if (SkeletonConfig.bClearRotations || SkeletonConfig.CopyRotationsFrom)
	{
		ClearSkeletonRotations(RefSkeleton);
	}

	if (SkeletonConfig.CopyRotationsFrom)
	{
		CopySkeletonRotationsFrom(RefSkeleton, SkeletonConfig.CopyRotationsFrom->GetReferenceSkeleton());
	}

	if (SkeletonConfig.BonesDeltaTransformMap.Num() > 0)
	{
		AddSkeletonDeltaTranforms(RefSkeleton, SkeletonConfig.BonesDeltaTransformMap);
	}

	Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

	return Skeleton;
}

bool FglTFRuntimeParser::NodeIsBone(const int32 NodeIndex)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonSkins;
	if (!Root->TryGetArrayField(TEXT("skins"), JsonSkins))
	{
		return false;
	}

	for (TSharedPtr<FJsonValue> JsonSkin : *JsonSkins)
	{
		TSharedPtr<FJsonObject> JsonSkinObject = JsonSkin->AsObject();
		if (!JsonSkinObject)
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* JsonJoints;
		if (!JsonSkinObject->TryGetArrayField(TEXT("joints"), JsonJoints))
		{
			continue;
		}

		for (TSharedPtr<FJsonValue> JsonJoint : *JsonJoints)
		{
			int64 JointIndex;
			if (!JsonJoint->TryGetNumber(JointIndex))
			{
				continue;
			}
			if (JointIndex == NodeIndex)
			{
				return true;
			}
		}
	}

	return false;
}

bool FglTFRuntimeParser::FillLODSkeleton(FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const TArray<FglTFRuntimeBone>& Skeleton)
{
	RefSkeleton.Empty();

	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);

	bool bFoundRoot = false;
	for (int32 BoneIndex = 0; BoneIndex < Skeleton.Num(); BoneIndex++)
	{
		const FString CurrentBoneName = Skeleton[BoneIndex].BoneName;
		const int32 CurrentBoneParentIndex = Skeleton[BoneIndex].ParentIndex;
		const FTransform CurrentBoneTransform = Skeleton[BoneIndex].Transform;
		if (CurrentBoneParentIndex == INDEX_NONE)
		{
			if (bFoundRoot)
			{
				AddError("FillLODSkeleton()", "Only one root bone can be defined.");
				return false;
			}
			bFoundRoot = true;
		}
		else if (CurrentBoneParentIndex >= 0)
		{
			if (CurrentBoneParentIndex >= Skeleton.Num())
			{
				AddError("FillLODSkeleton()", "Bone ParentIndex is not valid.");
				return false;
			}
		}
		else
		{
			AddError("FillLODSkeleton()", "The only supported negative ParentIndex is -1 (for root bone)");
			return false;
		}

		// now check for duplicated bone names
		for (int32 CheckBoneIndex = 0; CheckBoneIndex < Skeleton.Num(); CheckBoneIndex++)
		{
			if (CheckBoneIndex == BoneIndex)
			{
				continue;
			}

			if (Skeleton[CheckBoneIndex].BoneName == CurrentBoneName)
			{
				AddError("FillLODSkeleton()", "Duplicated bone name found");
				return false;
			}
		}
		FName BoneName = FName(CurrentBoneName);
		Modifier.Add(FMeshBoneInfo(BoneName, CurrentBoneName, CurrentBoneParentIndex), CurrentBoneTransform);

		BoneMap.Add(BoneIndex, BoneName);
	}

	if (!bFoundRoot)
	{
		return false;
	}

	OnLoadedRefSkeleton.Broadcast(AsShared(), nullptr, Modifier);

	return true;
}

bool FglTFRuntimeParser::FillFakeSkeleton(FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	RefSkeleton.Empty();

	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);

	if (SkeletalMeshConfig.CustomSkeleton.Num() > 0)
	{
		bool bFoundRoot = false;
		for (int32 BoneIndex = 0; BoneIndex < SkeletalMeshConfig.CustomSkeleton.Num(); BoneIndex++)
		{
			const FString CurrentBoneName = SkeletalMeshConfig.CustomSkeleton[BoneIndex].BoneName;
			const int32 CurrentBoneParentIndex = SkeletalMeshConfig.CustomSkeleton[BoneIndex].ParentIndex;
			const FTransform CurrentBoneTransform = SkeletalMeshConfig.CustomSkeleton[BoneIndex].Transform;
			if (CurrentBoneParentIndex == INDEX_NONE)
			{
				if (bFoundRoot)
				{
					AddError("FillFakeSkeleton()", "Only one root bone can be defined.");
					return false;
				}
				bFoundRoot = true;
			}
			else if (CurrentBoneParentIndex >= 0)
			{
				if (CurrentBoneParentIndex >= SkeletalMeshConfig.CustomSkeleton.Num())
				{
					AddError("FillFakeSkeleton()", "Bone ParentIndex is not valid.");
					return false;
				}
			}
			else
			{
				AddError("FillFakeSkeleton()", "The only supported negative ParentIndex is -1 (for root bone)");
				return false;
			}

			// now check for duplicated bone names
			for (int32 CheckBoneIndex = 0; CheckBoneIndex < SkeletalMeshConfig.CustomSkeleton.Num(); CheckBoneIndex++)
			{
				if (CheckBoneIndex == BoneIndex)
				{
					continue;
				}

				if (SkeletalMeshConfig.CustomSkeleton[CheckBoneIndex].BoneName == CurrentBoneName)
				{
					AddError("FillFakeSkeleton()", "Duplicated bone name found");
					return false;
				}
			}
			FName BoneName = FName(CurrentBoneName);
			Modifier.Add(FMeshBoneInfo(BoneName, CurrentBoneName, CurrentBoneParentIndex), CurrentBoneTransform);

			BoneMap.Add(BoneIndex, BoneName);
		}
	}
	else
	{
		FName RootBoneName = FName("root");
		if (!SkeletalMeshConfig.SkeletonConfig.RootBoneName.IsEmpty())
		{
			RootBoneName = FName(SkeletalMeshConfig.SkeletonConfig.RootBoneName);
		}
		Modifier.Add(FMeshBoneInfo(RootBoneName, RootBoneName.ToString(), INDEX_NONE), FTransform::Identity);
		BoneMap.Add(0, RootBoneName);
	}

	OnLoadedRefSkeleton.Broadcast(AsShared(), nullptr, Modifier);

	return true;
}


bool FglTFRuntimeParser::GetRootBoneIndex(TSharedRef<FJsonObject> JsonSkinObject, int64& RootBoneIndex, TArray<int32>& Joints, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	// get the list of valid joints	
	const TArray<TSharedPtr<FJsonValue>>* JsonJoints;
	if (JsonSkinObject->TryGetArrayField(TEXT("joints"), JsonJoints))
	{
		for (TSharedPtr<FJsonValue> JsonJoint : *JsonJoints)
		{
			int64 JointIndex;
			if (!JsonJoint->TryGetNumber(JointIndex))
			{
				return false;
			}
			Joints.Add(JointIndex);
		}
	}

	if (Joints.Num() == 0)
	{
		AddError("GetRootBoneIndex()", "No Joints available");
		return false;
	}

	FglTFRuntimeNode RootNode;
	RootBoneIndex = INDEX_NONE;

	if (SkeletonConfig.RootNodeIndex > INDEX_NONE)
	{
		RootBoneIndex = SkeletonConfig.RootNodeIndex;
	}
	else if (!SkeletonConfig.ForceRootNode.IsEmpty())
	{
		if (LoadNodeByName(SkeletonConfig.ForceRootNode, RootNode))
		{
			RootBoneIndex = RootNode.Index;
		}
	}
	else if (JsonSkinObject->TryGetNumberField(TEXT("skeleton"), RootBoneIndex))
	{
		// use the "skeleton" field as the root bone
	}
	else
	{
		RootBoneIndex = FindCommonRoot(Joints);
		if (RootBoneIndex < 0 && SkeletonConfig.bAddRootNodeIfMissing)
		{
			RootBoneIndex = AddFakeRootNode(SkeletonConfig.RootBoneName.IsEmpty() ? "root" : SkeletonConfig.RootBoneName);
		}
	}

	if (RootBoneIndex <= INDEX_NONE)
	{
		AddError("GetRootBoneIndex()", "Unable to find root node.");
		return false;
	}

	return true;
}

bool FglTFRuntimeParser::FillReferenceSkeleton(TSharedRef<FJsonObject> JsonSkinObject, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	int64 RootBoneIndex = INDEX_NONE;
	TArray<int32> Joints;

	if (!GetRootBoneIndex(JsonSkinObject, RootBoneIndex, Joints, SkeletonConfig))
	{
		return false;
	}

	// fill the root bone
	FglTFRuntimeNode RootNode;
	if (!LoadNode(RootBoneIndex, RootNode))
	{
		AddError("FillReferenceSkeleton()", "Unable to load joint node.");
		return false;
	}

	TMap<int32, FMatrix> InverseBindMatricesMap;
	int64 InverseBindMatricesIndex;
	if (JsonSkinObject->TryGetNumberField(TEXT("inverseBindMatrices"), InverseBindMatricesIndex))
	{
		FglTFRuntimeBlob InverseBindMatricesBytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		bool bNormalized = false;
		if (!GetAccessor(InverseBindMatricesIndex, ComponentType, Stride, Elements, ElementSize, Count, bNormalized, InverseBindMatricesBytes, nullptr))
		{
			AddError("FillReferenceSkeleton()", FString::Printf(TEXT("Unable to load accessor: %lld."), InverseBindMatricesIndex));
			return false;
		}

		if (Elements != 16 || ComponentType != 5126)
		{
			return false;
		}

		for (int64 i = 0; i < Count; i++)
		{
			FMatrix Matrix;
			int64 MatrixIndex = i * Stride;

			float* MatrixCell = (float*)&InverseBindMatricesBytes.Data[MatrixIndex];

			for (int32 j = 0; j < 16; j++)
			{
				float Value = MatrixCell[j];

				Matrix.M[j / 4][j % 4] = Value;
			}

			if (i < Joints.Num())
			{
				InverseBindMatricesMap.Add(Joints[i], Matrix);
			}
		}
	}

	RefSkeleton.Empty();

	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);

	// now traverse from the root and check if the node is in the "joints" list
	if (!TraverseJoints(Modifier, RootNode.Index, INDEX_NONE, RootNode, Joints, BoneMap, InverseBindMatricesMap, SkeletonConfig))
	{
		return false;
	}

	OnLoadedRefSkeleton.Broadcast(AsShared(), JsonSkinObject, Modifier);

	return true;
}

bool FglTFRuntimeParser::FillReferenceSkeletonFromNode(const FglTFRuntimeNode& RootNode, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	RefSkeleton.Empty();

	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);

	// now traverse from the root and check if the node is in the "joints" list
	return TraverseJoints(Modifier, RootNode.Index, INDEX_NONE, RootNode, {}, BoneMap, {}, SkeletonConfig);
}

bool FglTFRuntimeParser::TraverseJoints(FReferenceSkeletonModifier& Modifier, const int32 RootIndex, int32 Parent, const FglTFRuntimeNode& Node, const TArray<int32>& Joints, TMap<int32, FName>& BoneMap, const TMap<int32, FMatrix>& InverseBindMatricesMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	TArray<FString> AppendBones;
	// add fake root bone ?
	if (Parent == INDEX_NONE && SkeletonConfig.bAddRootBone)
	{
		FName RootBoneName = FName("root");
		if (!SkeletonConfig.RootBoneName.IsEmpty())
		{
			RootBoneName = FName(SkeletonConfig.RootBoneName);
		}
		Modifier.Add(FMeshBoneInfo(RootBoneName, RootBoneName.ToString(), INDEX_NONE), FTransform::Identity);
		Parent = 0;
	}

	FName BoneName = FName(*Node.Name);
	if (SkeletonConfig.BoneRemapper.Remapper.IsBound())
	{
		BoneName = FName(SkeletonConfig.BoneRemapper.Remapper.Execute(Node.Index, Node.Name, SkeletonConfig.BoneRemapper.Context));
	}

	if (SkeletonConfig.BonesNameMap.Contains(BoneName.ToString()))
	{
		FString BoneNameMapValue = SkeletonConfig.BonesNameMap[BoneName.ToString()];
		if (BoneNameMapValue.IsEmpty())
		{
			AddError("TraverseJoints()", FString::Printf(TEXT("Invalid Bone Name Map for %s"), *BoneName.ToString()));
			return false;
		}

		if (BoneNameMapValue.Contains(","))
		{
			TArray<FString> Parts;
			if (BoneNameMapValue.ParseIntoArray(Parts, TEXT(",")) > 0)
			{
				BoneNameMapValue = Parts[0];
				Parts.RemoveAt(0);
				AppendBones = Parts;
			}
		}

		BoneName = FName(BoneNameMapValue);
	}
	else if (SkeletonConfig.BonesNameMap.Num() > 0 && SkeletonConfig.bAssignUnmappedBonesToParent)
	{
		int32 ParentNodeIndex = Node.ParentIndex;
		while (ParentNodeIndex != INDEX_NONE)
		{
			FglTFRuntimeNode ParentNode;
			if (!LoadNode(ParentNodeIndex, ParentNode))
			{
				return false;
			}

			if (SkeletonConfig.BonesNameMap.Contains(ParentNode.Name))
			{
				if (Joints.Contains(Node.Index))
				{
					BoneMap.Add(Joints.IndexOfByKey(Node.Index), *SkeletonConfig.BonesNameMap[ParentNode.Name]);
				}

				// continue with the other children...
				for (int32 ChildIndex : Node.ChildrenIndices)
				{
					FglTFRuntimeNode ChildNode;
					if (!LoadNode(ChildIndex, ChildNode))
					{
						return false;
					}

					if (!TraverseJoints(Modifier, RootIndex, Parent, ChildNode, Joints, BoneMap, InverseBindMatricesMap, SkeletonConfig))
					{
						return false;
					}
				}

				return true;
			}

			ParentNodeIndex = ParentNode.ParentIndex;
		}

		return false;
	}

	// Check if a bone with the same name exists
	int32 CollidingIndex = Modifier.FindBoneIndex(BoneName);
	if (CollidingIndex != INDEX_NONE)
	{
		if (SkeletonConfig.bSkipAlreadyExistentBoneNames)
		{
			AddError("TraverseJoints()", FString::Printf(TEXT("Stopping at Bone %s (already exists)."), *BoneName.ToString()));
			return true;
		}
		else if (SkeletonConfig.bAppendNodeIndexOnNameCollision)
		{
			BoneName = FName(FString::Printf(TEXT("%s%d"), *BoneName.ToString(), Node.Index));
			CollidingIndex = Modifier.FindBoneIndex(BoneName);
			if (CollidingIndex != INDEX_NONE)
			{
				AddError("TraverseJoints()", FString::Printf(TEXT("Automatically renamed Bone %s already exists."), *BoneName.ToString()));
				return false;
			}
		}
		else
		{
			AddError("TraverseJoints()", FString::Printf(TEXT("Bone %s already exists."), *BoneName.ToString()));
			return false;
		}
	}

	FTransform Transform = Node.Transform;
	if (InverseBindMatricesMap.Contains(Node.Index))
	{
		bool bSlowPath = false;
		FMatrix M = InverseBindMatricesMap[Node.Index].Inverse();
		if (Node.ParentIndex != INDEX_NONE && Node.Index != RootIndex)
		{
			if (InverseBindMatricesMap.Contains(Node.ParentIndex))
			{
				M *= InverseBindMatricesMap[Node.ParentIndex];
			}
			else
			{
				bSlowPath = true;
			}
		}

		M.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
		const FMatrix SkeletonBasis = SceneBasis;
		Transform = FTransform(SkeletonBasis.Inverse() * M * SkeletonBasis);

		// we are here if the parent has no joint inverse bind matrix (and we need to build the bind pose from it
		// we could use the Skeleton hiearchy here for building the pose but we will check for inverse bind matrix too
		// for improving performance
		if (bSlowPath)
		{
			FTransform ParentTransform = FTransform::Identity;
			int32 CurrentParentIndex = Node.ParentIndex;
			while (CurrentParentIndex > INDEX_NONE)
			{
				FglTFRuntimeNode ParentNode;
				if (!LoadNode(CurrentParentIndex, ParentNode))
				{
					return false;
				}

				// do we have an inverse bind matrix ?
				if (InverseBindMatricesMap.Contains(CurrentParentIndex))
				{
					M = InverseBindMatricesMap[CurrentParentIndex];
					M.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
					Transform *= FTransform(SkeletonBasis.Inverse() * M * SkeletonBasis) * ParentTransform.Inverse();
					ParentTransform = FTransform::Identity; // this is required for avoiding double transform application
					break;
				}
				else // fallback to (slower) node transform
				{
					ParentTransform *= ParentNode.Transform;
				}

				if (CurrentParentIndex == RootIndex) // stop at the root
				{
					break;
				}
				CurrentParentIndex = ParentNode.ParentIndex;
			}

			Transform *= ParentTransform.Inverse();
		}
	}
	else if (Parent == INDEX_NONE && SkeletonConfig.bApplyParentNodesTransformsToRoot)
	{
		int32 CurrentParentIndex = Node.ParentIndex;
		while (CurrentParentIndex > INDEX_NONE)
		{
			FglTFRuntimeNode ParentNode;
			if (!LoadNode(CurrentParentIndex, ParentNode))
			{
				return false;
			}

			Transform *= ParentNode.Transform;
			CurrentParentIndex = ParentNode.ParentIndex;
		}
	}

	if (SkeletonConfig.bApplyUnmappedBonesTransforms && Parent > INDEX_NONE && SkeletonConfig.BonesNameMap.Contains(Node.Name))
	{
		// climb the hierarchy til a mapped joint is found
		int32 CurrentParentIndex = Node.ParentIndex;
		while (CurrentParentIndex > INDEX_NONE)
		{
			FglTFRuntimeNode ParentNode;
			if (!LoadNode(CurrentParentIndex, ParentNode))
			{
				return false;
			}

			if (SkeletonConfig.BonesNameMap.Contains(ParentNode.Name))
			{
				break;
			}

			FTransform ParentTransform = ParentNode.Transform;

			// do we have an inverse bind matrix ?
			if (InverseBindMatricesMap.Contains(CurrentParentIndex))
			{
				FMatrix ParentMatrix = InverseBindMatricesMap[CurrentParentIndex].Inverse();
				// do we have a valid parent matrix for it?
				if (InverseBindMatricesMap.Contains(ParentNode.ParentIndex))
				{
					ParentMatrix *= InverseBindMatricesMap[ParentNode.ParentIndex];
				}
				ParentMatrix.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
				ParentTransform = FTransform(SceneBasis.Inverse() * ParentMatrix * SceneBasis);
			}

			Transform *= ParentTransform;
			CurrentParentIndex = ParentNode.ParentIndex;
		}
	}

	Modifier.Add(FMeshBoneInfo(BoneName, Node.Name, Parent), Transform);

	int32 NewParentIndex = Modifier.FindBoneIndex(BoneName);
	// something horrible happened...
	if (NewParentIndex == INDEX_NONE)
	{
		return false;
	}

	if (Joints.Contains(Node.Index))
	{
		BoneMap.Add(Joints.IndexOfByKey(Node.Index), BoneName);
	}

	for (const FString& AdditionalBone : AppendBones)
	{
		CollidingIndex = Modifier.FindBoneIndex(*AdditionalBone);
		if (CollidingIndex > INDEX_NONE)
		{
			AddError("TraverseJoints()", FString::Printf(TEXT("Bone %s already exists."), *AdditionalBone));
			return false;
		}
		Modifier.Add(FMeshBoneInfo(*AdditionalBone, AdditionalBone, NewParentIndex), FTransform::Identity);
		NewParentIndex = Modifier.FindBoneIndex(*AdditionalBone);
	}

	if (SkeletonConfig.MaxNodesTreeDepth >= 0)
	{
		int32 Depth = 0;
		const TArray<FMeshBoneInfo>& BoneInfos = Modifier.GetRefBoneInfo();
		int32 CurrentIndex = NewParentIndex;
		while (BoneInfos[CurrentIndex].ParentIndex > INDEX_NONE)
		{
			Depth++;
			CurrentIndex = BoneInfos[CurrentIndex].ParentIndex;
		}

		if (Depth >= SkeletonConfig.MaxNodesTreeDepth)
		{
			return true;
		}
	}

	for (int32 ChildIndex : Node.ChildrenIndices)
	{
		FglTFRuntimeNode ChildNode;
		if (!LoadNode(ChildIndex, ChildNode))
		{
			return false;
		}

		if (!TraverseJoints(Modifier, RootIndex, NewParentIndex, ChildNode, Joints, BoneMap, InverseBindMatricesMap, SkeletonConfig))
		{
			return false;
		}
	}

	return true;
}

bool FglTFRuntimeParser::LoadPrimitives(TSharedRef<FJsonObject> JsonMeshObject, TArray<FglTFRuntimePrimitive>& Primitives, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bTriangulatePointsAndLines)
{
	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	if (!JsonMeshObject->TryGetArrayField(TEXT("primitives"), JsonPrimitives))
	{
		AddError("LoadPrimitives()", "No primitives defined in the asset.");
		return false;
	}

	int32 FirstPrimitive = Primitives.Num();

	for (TSharedPtr<FJsonValue> JsonPrimitive : *JsonPrimitives)
	{
		TSharedPtr<FJsonObject> JsonPrimitiveObject = JsonPrimitive->AsObject();
		if (!JsonPrimitiveObject)
		{
			return false;
		}

		FglTFRuntimePrimitive Primitive;
		if (!LoadPrimitive(JsonPrimitiveObject.ToSharedRef(), Primitive, MaterialsConfig, bTriangulatePointsAndLines))
		{
			return false;
		}

		// add the primitive only if it has at least one index 
		if (Primitive.Indices.Num() > 0)
		{
			Primitives.Add(Primitive);
		}
	}

	const TSharedPtr<FJsonObject>* JsonExtrasObject;
	if (JsonMeshObject->TryGetObjectField(TEXT("extras"), JsonExtrasObject))
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonTargetNamesArray;
		if ((*JsonExtrasObject)->TryGetArrayField(TEXT("targetNames"), JsonTargetNamesArray))
		{
			auto ApplyTargetName = [FirstPrimitive](TArray<FglTFRuntimePrimitive>& Primitives, const int32 TargetNameIndex, const FString& TargetName)
				{
					for (int32 PrimitiveIndex = FirstPrimitive; PrimitiveIndex < Primitives.Num(); PrimitiveIndex++)
					{
						int32 MorphTargetCounter = 0;
						FglTFRuntimePrimitive& Primitive = Primitives[PrimitiveIndex];
						for (FglTFRuntimeMorphTarget& MorphTarget : Primitive.MorphTargets)
						{
							if (MorphTargetCounter == TargetNameIndex)
							{
								MorphTarget.Name = TargetName;
								break;
							}
							MorphTargetCounter++;
						}
					}
				};
			for (int32 TargetNameIndex = 0; TargetNameIndex < JsonTargetNamesArray->Num(); TargetNameIndex++)
			{
				const FString TargetName = (*JsonTargetNamesArray)[TargetNameIndex]->AsString();
				ApplyTargetName(Primitives, TargetNameIndex, TargetName);
			}
		}
	}

	if (MaterialsConfig.bMergeSectionsByMaterial)
	{
		MergePrimitivesByMaterial(Primitives);
	}

	return true;
}

void FglTFRuntimeParser::MergePrimitivesByMaterial(TArray<FglTFRuntimePrimitive>& Primitives)
{
	TMap<UMaterialInterface*, TArray<FglTFRuntimePrimitive>> PrimitivesMap;
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		if (PrimitivesMap.Contains(Primitive.Material))
		{
			PrimitivesMap[Primitive.Material].Add(Primitive);
		}
		else
		{
			TArray<FglTFRuntimePrimitive> NewPrimitives;
			NewPrimitives.Add(Primitive);
			PrimitivesMap.Add(Primitive.Material, NewPrimitives);
		}
	}

	TArray<FglTFRuntimePrimitive> MergedPrimitives;
	for (TPair<UMaterialInterface*, TArray<FglTFRuntimePrimitive>>& Pair : PrimitivesMap)
	{
		FglTFRuntimePrimitive MergedPrimitive;
		if (MergePrimitives(Pair.Value, MergedPrimitive))
		{
			MergedPrimitives.Add(MergedPrimitive);
		}
		else
		{
			// unable to merge, just leave as is
			for (FglTFRuntimePrimitive& Primitive : Pair.Value)
			{
				MergedPrimitives.Add(Primitive);
			}
		}
	}

	Primitives = MergedPrimitives;
}

FVector FglTFRuntimeParser::TransformVector(const FVector Vector) const
{
	return SceneBasis.TransformVector(Vector);
}

FVector FglTFRuntimeParser::TransformPosition(const FVector Position) const
{
	return SceneBasis.TransformPosition(Position) * SceneScale;
}

FVector4 FglTFRuntimeParser::TransformVector4(const FVector4 Vector) const
{
	return SceneBasis.TransformFVector4(Vector);
}

FTransform FglTFRuntimeParser::TransformTransform(const FTransform& Transform) const
{
	FTransform NewTransform = FTransform(SceneBasis.Inverse() * Transform.ToMatrixWithScale() * SceneBasis);
	NewTransform.ScaleTranslation(SceneScale);

	return NewTransform;
}

bool FglTFRuntimeParser::LoadPrimitive(TSharedRef<FJsonObject> JsonPrimitiveObject, FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bTriangulatePointsAndLines)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_LoadPrimitive, FColor::Magenta);

	UMaterialInterface* ForceBaseMaterial = nullptr;

	OnPreLoadedPrimitive.Broadcast(AsShared(), JsonPrimitiveObject, Primitive);

	if (!JsonPrimitiveObject->TryGetNumberField(TEXT("mode"), Primitive.Mode))
	{
		Primitive.Mode = 4; // triangles
	}

	if (Primitive.Mode == 0 && MaterialsConfig.bSkipPoints)
	{
		return false;
	}

	if (Primitive.Mode >= 1 && Primitive.Mode <= 3 && MaterialsConfig.bSkipLines)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* JsonAttributesObject;
	if (!JsonPrimitiveObject->TryGetObjectField(TEXT("attributes"), JsonAttributesObject))
	{
		AddError("LoadPrimitive()", "No attributes array available");
		return false;
	}

	// POSITION is required for generating a valid Mesh
	if (!(*JsonAttributesObject)->HasField(TEXT("POSITION")))
	{
		AddError("LoadPrimitive()", "POSITION attribute is required");
		return false;
	}

	const bool bHasMeshQuantization = ExtensionsRequired.Contains("KHR_mesh_quantization");

	TArray<int64> SupportedPositionComponentTypes = { 5126 };
	TArray<int64> SupportedNormalComponentTypes = { 5126 };
	TArray<int64> SupportedTangentComponentTypes = { 5126 };
	TArray<int64> SupportedTexCoordComponentTypes = { 5126, 5121, 5123 };
	if (bHasMeshQuantization)
	{
		// the bDefaultNormalized is not necessarely a good idea but I do not want to break old (technically invalid) assets
		SupportedPositionComponentTypes.Append({ 5120, 5121, 5122, 5123 });
		SupportedNormalComponentTypes.Append({ 5120, 5122 });
		SupportedTangentComponentTypes.Append({ 5120, 5122 });
		SupportedTexCoordComponentTypes.Append({ 5120, 5122 });
	}

	if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "POSITION", Primitive.Positions,
		{ 3 }, SupportedPositionComponentTypes, [&](FVector Value) -> FVector {return SceneBasis.TransformPosition(Value) * SceneScale; }, Primitive.AdditionalBufferView, false, nullptr))
	{
		AddError("LoadPrimitive()", "Unable to load POSITION attribute");
		return false;
	}

	if ((*JsonAttributesObject)->HasField(TEXT("NORMAL")))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "NORMAL", Primitive.Normals,
			{ 3 }, SupportedNormalComponentTypes, [&](FVector Value) -> FVector { return SceneBasis.TransformVector(Value); }, Primitive.AdditionalBufferView, true, nullptr))
		{
			AddError("LoadPrimitive()", "Unable to load NORMAL attribute");
			return false;
		}
	}

	if ((*JsonAttributesObject)->HasField(TEXT("TANGENT")))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TANGENT", Primitive.Tangents,
			{ 4 }, SupportedTangentComponentTypes, [&](FVector4 Value) -> FVector4 { return SceneBasis.TransformFVector4(Value); }, Primitive.AdditionalBufferView, true, nullptr))
		{
			AddError("LoadPrimitive()", "Unable to load TANGENT attribute");
			return false;
		}
	}

	if ((*JsonAttributesObject)->HasField(TEXT("TEXCOORD_0")))
	{
		TArray<FVector2D> UV;
		int64 TexCoordComponentType = 0;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TEXCOORD_0", UV,
			{ 2 }, SupportedTexCoordComponentTypes, [&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, Value.Y); }, Primitive.AdditionalBufferView, !bHasMeshQuantization, &TexCoordComponentType))
		{
			AddError("LoadPrimitive()", "Error loading TEXCOORD_0");
			return false;
		}

		if (TexCoordComponentType == 5126)
		{
			Primitive.bHighPrecisionUVs = true;
		}

		Primitive.UVs.Add(UV);
	}

	if ((*JsonAttributesObject)->HasField(TEXT("TEXCOORD_1")))
	{
		TArray<FVector2D> UV;
		int64 TexCoordComponentType = 0;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TEXCOORD_1", UV,
			{ 2 }, SupportedTexCoordComponentTypes, [&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, Value.Y); }, Primitive.AdditionalBufferView, !bHasMeshQuantization, &TexCoordComponentType))
		{
			AddError("LoadPrimitive()", "Error loading TEXCOORD_1");
			return false;
		}

		if (TexCoordComponentType == 5126)
		{
			Primitive.bHighPrecisionUVs = true;
		}

		Primitive.UVs.Add(UV);
	}

	if ((*JsonAttributesObject)->HasField(TEXT("JOINTS_0")))
	{
		TArray<FglTFRuntimeUInt16Vector4> Joints;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "JOINTS_0", Joints,
			{ 4 }, { 5121, 5123 }, Primitive.AdditionalBufferView, false, nullptr))
		{
			AddError("LoadPrimitive()", "Error loading JOINTS_0");
			return false;
		}

		Primitive.Joints.Add(Joints);
	}

	if ((*JsonAttributesObject)->HasField(TEXT("JOINTS_1")))
	{
		TArray<FglTFRuntimeUInt16Vector4> Joints;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "JOINTS_1", Joints,
			{ 4 }, { 5121, 5123 }, Primitive.AdditionalBufferView, false, nullptr))
		{
			AddError("LoadPrimitive()", "Error loading JOINTS_1");
			return false;
		}

		Primitive.Joints.Add(Joints);
	}

	if ((*JsonAttributesObject)->HasField(TEXT("JOINTS_2")))
	{
		TArray<FglTFRuntimeUInt16Vector4> Joints;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "JOINTS_2", Joints,
			{ 4 }, { 5121, 5123 }, Primitive.AdditionalBufferView, false, nullptr))
		{
			AddError("LoadPrimitive()", "Error loading JOINTS_2");
			return false;
		}

		Primitive.Joints.Add(Joints);
	}

	if ((*JsonAttributesObject)->HasField(TEXT("WEIGHTS_0")))
	{
		TArray<FVector4> Weights;
		int64 WeightsComponentType = 0;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "WEIGHTS_0", Weights,
			{ 4 }, { 5126, 5121, 5123 }, Primitive.AdditionalBufferView, true, &WeightsComponentType))
		{
			AddError("LoadPrimitive()", "Error loading WEIGHTS_0");
			return false;
		}

		if (WeightsComponentType == 5126 || WeightsComponentType == 5123)
		{
			Primitive.bHighPrecisionWeights = true;
		}

		Primitive.Weights.Add(Weights);
	}

	if ((*JsonAttributesObject)->HasField(TEXT("WEIGHTS_1")))
	{
		TArray<FVector4> Weights;
		int64 WeightsComponentType = 0;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "WEIGHTS_1", Weights,
			{ 4 }, { 5126, 5121, 5123 }, Primitive.AdditionalBufferView, true, &WeightsComponentType))
		{
			AddError("LoadPrimitive()", "Error loading WEIGHTS_1");
			return false;
		}

		if (WeightsComponentType == 5126 || WeightsComponentType == 5123)
		{
			Primitive.bHighPrecisionWeights = true;
		}

		Primitive.Weights.Add(Weights);
	}

	if ((*JsonAttributesObject)->HasField(TEXT("WEIGHTS_2")))
	{
		TArray<FVector4> Weights;
		int64 WeightsComponentType = 0;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "WEIGHTS_2", Weights,
			{ 4 }, { 5126, 5121, 5123 }, Primitive.AdditionalBufferView, true, &WeightsComponentType))
		{
			AddError("LoadPrimitive()", "Error loading WEIGHTS_2");
			return false;
		}

		if (WeightsComponentType == 5126 || WeightsComponentType == 5123)
		{
			Primitive.bHighPrecisionWeights = true;
		}

		Primitive.Weights.Add(Weights);
	}

	if ((*JsonAttributesObject)->HasField(TEXT("COLOR_0")))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "COLOR_0", Primitive.Colors,
			{ 3, 4 }, { 5126, 5121, 5123 }, Primitive.AdditionalBufferView, true, nullptr))
		{
			AddError("LoadPrimitive()", "Error loading COLOR_0");
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTargetsArray;
	if (JsonPrimitiveObject->TryGetArrayField(TEXT("targets"), JsonTargetsArray))
	{
		for (TSharedPtr<FJsonValue> JsonTargetItem : *JsonTargetsArray)
		{
			TSharedPtr<FJsonObject> JsonTargetObject = JsonTargetItem->AsObject();
			if (!JsonTargetObject)
			{
				AddError("LoadPrimitive()", "Error on MorphTarget item: expected an object.");
				return false;
			}

			FglTFRuntimeMorphTarget MorphTarget;

			bool bValid = false;

			if (JsonTargetObject->HasField(TEXT("POSITION")))
			{
				if (!BuildFromAccessorField(JsonTargetObject.ToSharedRef(), "POSITION", MorphTarget.Positions,
					{ 3 }, SupportedPositionComponentTypes, [&](FVector Value) -> FVector { return SceneBasis.TransformPosition(Value) * SceneScale; }, INDEX_NONE, false, nullptr))
				{
					AddError("LoadPrimitive()", "Unable to load POSITION attribute for MorphTarget");
					return false;
				}
				if (MorphTarget.Positions.Num() != Primitive.Positions.Num())
				{
					AddError("LoadPrimitive()", "Invalid POSITION attribute size for MorphTarget.");
					return false;
				}
				bValid = true;
			}

			if (JsonTargetObject->HasField(TEXT("NORMAL")))
			{
				if (!BuildFromAccessorField(JsonTargetObject.ToSharedRef(), "NORMAL", MorphTarget.Normals,
					{ 3 }, SupportedNormalComponentTypes, [&](FVector Value) -> FVector { return SceneBasis.TransformVector(Value); }, INDEX_NONE, true, nullptr))
				{
					AddError("LoadPrimitive()", "Unable to load NORMAL attribute for MorphTarget");
					return false;
				}
				if (MorphTarget.Normals.Num() != Primitive.Normals.Num())
				{
					AddError("LoadPrimitive()", "Invalid NORMAL attribute size for MorphTarget.");
					return false;
				}
				bValid = true;
			}

			if (bValid)
			{
				Primitive.MorphTargets.Add(MorphTarget);
			}
		}
	}

	int64 IndicesAccessorIndex;
	if (JsonPrimitiveObject->TryGetNumberField(TEXT("indices"), IndicesAccessorIndex))
	{
		FglTFRuntimeBlob IndicesBytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		bool bNormalized = false;
		if (!GetAccessor(IndicesAccessorIndex, ComponentType, Stride, Elements, ElementSize, Count, bNormalized, IndicesBytes, GetAdditionalBufferView(Primitive.AdditionalBufferView, "indices")))
		{
			AddError("LoadPrimitive()", FString::Printf(TEXT("Unable to load accessor: %lld"), IndicesAccessorIndex));
			return false;
		}

		if (Elements != 1)
		{
			return false;
		}

		if (IndicesBytes.Num < (Count * Stride))
		{
			AddError("LoadPrimitive()", FString::Printf(TEXT("Invalid size for accessor indices: %lld"), IndicesBytes.Num));
			return false;
		}

		if (ComponentType != 5121 && ComponentType != 5123 && ComponentType != 5125)
		{
			AddError("LoadPrimitive()", FString::Printf(TEXT("Invalid component type for indices: %lld"), ComponentType));
			return false;
		}

		Primitive.Indices.AddUninitialized(Count);
		ParallelFor(Count, [&](const int32 Index)
			{
				int64 IndexIndex = Index * Stride;

				uint32 VertexIndex = 0;
				if (ComponentType == 5121)
				{
					VertexIndex = IndicesBytes.Data[IndexIndex];
				}
				else if (ComponentType == 5123)
				{
					uint16* IndexPtr = (uint16*)&(IndicesBytes.Data[IndexIndex]);
					VertexIndex = *IndexPtr;
				}
				else if (ComponentType == 5125)
				{
					uint32* IndexPtr = (uint32*)&(IndicesBytes.Data[IndexIndex]);
					VertexIndex = *IndexPtr;
				}

				Primitive.Indices[Index] = VertexIndex;
			});

		// use indices only if their number is higher than positions (this reduces gpu usage on assets reusing the same POSITION buffer)
		if (Primitive.Positions.Num() < Primitive.Indices.Num())
		{
			Primitive.bHasIndices = true;
		}
	}
	else
	{
		Primitive.Indices.AddUninitialized(Primitive.Positions.Num());
		ParallelFor(Primitive.Positions.Num(), [&](const int32 VertexIndex)
			{
				Primitive.Indices[VertexIndex] = VertexIndex;
			});
	}

	// fixing indices... 5: TRIANGLE_STRIP 6: TRIANGLE_FAN
	if (Primitive.Mode == 5)
	{
		TArray<uint32> StripIndices;
		if (Primitive.Indices.Num() >= 3)
		{
			StripIndices.Add(Primitive.Indices[0]);
			StripIndices.Add(Primitive.Indices[1]);
			StripIndices.Add(Primitive.Indices[2]);
			StripIndices.AddUninitialized((Primitive.Indices.Num() - 3) * 3);
		}
		int64 StripIndex = 3;
		for (int64 Index = 3; Index < Primitive.Indices.Num(); Index++)
		{
			StripIndices[StripIndex] = StripIndices[StripIndex - 1];
			StripIndices[StripIndex + 1] = StripIndices[StripIndex - 2];
			StripIndices[StripIndex + 2] = Primitive.Indices[Index];
			StripIndex += 3;
		}
		Primitive.Indices = StripIndices;
	}
	else if (Primitive.Mode == 6)
	{
		TArray<uint32> FanIndices;
		if (Primitive.Indices.Num() >= 3)
		{
			FanIndices.Add(Primitive.Indices[0]);
			FanIndices.Add(Primitive.Indices[1]);
			FanIndices.Add(Primitive.Indices[2]);
			FanIndices.AddUninitialized((Primitive.Indices.Num() - 3) * 3);
		}
		int64 FanIndex = 3;
		for (int64 Index = 3; Index < Primitive.Indices.Num(); Index++)
		{
			FanIndices[FanIndex] = FanIndices[0];
			FanIndices[FanIndex + 1] = FanIndices[FanIndex - 1];
			FanIndices[FanIndex + 2] = Primitive.Indices[Index];
			FanIndex += 3;
		}
		Primitive.Indices = FanIndices;
	}
	else if (bTriangulatePointsAndLines)
	{
		ForceBaseMaterial = TriangulatePointsAndLines(Primitive, MaterialsConfig);
	}

	Primitive.Material = UMaterial::GetDefaultMaterial(MD_Surface);

	if (!MaterialsConfig.bSkipLoad)
	{
		int64 MaterialIndex = INDEX_NONE;
		if (!MaterialsConfig.Variant.IsEmpty() && MaterialsVariants.Contains(MaterialsConfig.Variant))
		{
			int32 WantedIndex = MaterialsVariants.IndexOfByKey(MaterialsConfig.Variant);
			TArray<TSharedRef<FJsonObject>> VariantsMappings = GetJsonObjectArrayFromExtension(JsonPrimitiveObject, "KHR_materials_variants", "mappings");
			bool bMappingFound = false;
			for (TSharedRef<FJsonObject> VariantsMapping : VariantsMappings)
			{
				const TArray<TSharedPtr<FJsonValue>>* Variants;
				if (VariantsMapping->TryGetArrayField(TEXT("variants"), Variants))
				{
					for (TSharedPtr<FJsonValue> Variant : (*Variants))
					{
						int64 VariantIndex;
						if (Variant->TryGetNumber(VariantIndex) && VariantIndex == WantedIndex)
						{
							MaterialIndex = VariantsMapping->GetNumberField(TEXT("material"));
							bMappingFound = true;
							break;
						}
					}
				}
				if (bMappingFound)
				{
					break;
				}
			}
		}

		if (MaterialIndex == INDEX_NONE)
		{
			if (!JsonPrimitiveObject->TryGetNumberField(TEXT("material"), MaterialIndex))
			{
				MaterialIndex = INDEX_NONE;
			}
		}

		if (MaterialIndex != INDEX_NONE)
		{
			Primitive.Material = LoadMaterial(MaterialIndex, MaterialsConfig, Primitive.Colors.Num() > 0, Primitive.MaterialName, ForceBaseMaterial);
			if (!Primitive.Material)
			{
				AddError("LoadPrimitive()", FString::Printf(TEXT("Unable to load material %lld"), MaterialIndex));
				return false;
			}
			Primitive.bHasMaterial = true;
		}
		// special case for primitives without a material but with a color buffer
		else if (Primitive.Colors.Num() > 0)
		{
			Primitive.Material = BuildVertexColorOnlyMaterial(MaterialsConfig, false);
		}
	}

	OnLoadedPrimitive.Broadcast(AsShared(), JsonPrimitiveObject, Primitive);

	return true;
}

UMaterialInterface* FglTFRuntimeParser::TriangulatePoints(FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	TArray<uint32> PointsIndices;
	TArray<FVector> PointsPositions;
	TArray<FVector> PointsNormals;
	TArray<FVector2D> PointsUV;
	TArray<FVector2D> PointsUVXY;
	TArray<FVector2D> PointsUVZW;
	TArray<FVector4> PointsColors;

	const bool bHasColors = Primitive.Colors.Num() > 0;
	const bool bHasNormals = Primitive.Normals.Num() > 0;
	const bool bHasUVs = Primitive.UVs.Num() > 0;

	const float Scale = MaterialsConfig.PointsScaleFactor;

	const int32 NumIndices = Primitive.Indices.Num();

	bool bUVXY = false;
	bool bUVZW = false;
	bool bOpened = false;

	auto ToTriangle = [&](const int32 Index)
		{
			if (!Primitive.Indices.IsValidIndex(Index))
			{
				return;
			}

			const uint32 PointIndex = Primitive.Indices[Index];

			if (!Primitive.Positions.IsValidIndex(PointIndex))
			{
				return;
			}

			const FVector& Point = Primitive.Positions[PointIndex];

			PointsIndices[Index * 3] = Index * 3;
			PointsIndices[Index * 3 + 1] = Index * 3 + 1;
			PointsIndices[Index * 3 + 2] = Index * 3 + 2;

			PointsPositions[Index * 3] = Point + FVector(0, 0, 0.5) * Scale;
			PointsPositions[Index * 3 + 1] = Point + FVector(0, -0.5, -0.5) * Scale;
			PointsPositions[Index * 3 + 2] = Point + FVector(0, 0.5, -0.5) * Scale;

			if (bHasNormals)
			{
				if (Primitive.Normals.IsValidIndex(PointIndex))
				{
					PointsNormals[Index * 3] = Primitive.Normals[PointIndex];
					PointsNormals[Index * 3 + 1] = Primitive.Normals[PointIndex];
					PointsNormals[Index * 3 + 2] = Primitive.Normals[PointIndex];
				}
				else
				{
					PointsNormals[Index * 3] = FVector(0, 0, -1);
					PointsNormals[Index * 3 + 1] = FVector(0, 0, -1);
					PointsNormals[Index * 3 + 2] = FVector(0, 0, -1);
				}
			}

			if (bHasUVs)
			{
				if (Primitive.UVs[0].IsValidIndex(PointIndex))
				{
					PointsUV[Index * 3] = Primitive.UVs[0][PointIndex];
					PointsUV[Index * 3 + 1] = Primitive.UVs[0][PointIndex];
					PointsUV[Index * 3 + 2] = Primitive.UVs[0][PointIndex];
				}
				else
				{
					PointsUV[Index * 3] = FVector2D(0, 0);
					PointsUV[Index * 3 + 1] = FVector2D(0, 0);
					PointsUV[Index * 3 + 2] = FVector2D(0, 0);
				}
			}

			if (bUVXY)
			{
				PointsUVXY[Index * 3] = FVector2D(Point.X, Point.Y);
				PointsUVXY[Index * 3 + 1] = FVector2D(Point.X, Point.Y);
				PointsUVXY[Index * 3 + 2] = FVector2D(Point.X, Point.Y);
			}

			if (bUVZW)
			{
				PointsUVZW[Index * 3] = FVector2D(Point.Z, 0);
				PointsUVZW[Index * 3 + 1] = FVector2D(Point.Z, 0);
				PointsUVZW[Index * 3 + 2] = FVector2D(Point.Z, 0);
			}

			if (bHasColors)
			{
				if (Primitive.Colors.IsValidIndex(PointIndex))
				{
					const FVector4 Color = Primitive.Colors[PointIndex];
					PointsColors[Index * 3] = Color;
					PointsColors[Index * 3 + 1] = Color;
					PointsColors[Index * 3 + 2] = Color;
				}
				else
				{
					PointsColors[Index * 3] = FVector4(1, 1, 1, 1);
					PointsColors[Index * 3 + 1] = FVector4(1, 1, 1, 1);
					PointsColors[Index * 3 + 2] = FVector4(1, 1, 1, 1);
				}
			}
		};

	auto ToQuad = [&](const int32 Index)
		{
			if (!Primitive.Indices.IsValidIndex(Index))
			{
				return;
			}

			const uint32 PointIndex = Primitive.Indices[Index];

			if (!Primitive.Positions.IsValidIndex(PointIndex))
			{
				return;
			}

			const FVector& Point = Primitive.Positions[PointIndex];

			PointsIndices[Index * 6] = Index * 6;
			PointsIndices[Index * 6 + 1] = Index * 6 + 1;
			PointsIndices[Index * 6 + 2] = Index * 6 + 2;
			PointsIndices[Index * 6 + 3] = Index * 6 + 3;
			PointsIndices[Index * 6 + 4] = Index * 6 + 4;
			PointsIndices[Index * 6 + 5] = Index * 6 + 5;

			// top triangle
			PointsPositions[Index * 6] = Point + FVector(0, -0.5, 0.5) * Scale;
			PointsPositions[Index * 6 + 1] = Point + FVector(0, -0.5, -0.5) * Scale;
			PointsPositions[Index * 6 + 2] = Point + FVector(0, 0.5, 0.5) * Scale;
			// bottom triangle
			PointsPositions[Index * 6 + 3] = Point + FVector(0, 0.5, 0.5) * Scale;
			PointsPositions[Index * 6 + 4] = Point + FVector(0, -0.5, -0.5) * Scale;
			PointsPositions[Index * 6 + 5] = Point + FVector(0, 0.5, -0.5) * Scale;

			if (bHasNormals)
			{
				if (Primitive.Normals.IsValidIndex(PointIndex))
				{
					PointsNormals[Index * 6] = Primitive.Normals[PointIndex];
					PointsNormals[Index * 6 + 1] = Primitive.Normals[PointIndex];
					PointsNormals[Index * 6 + 2] = Primitive.Normals[PointIndex];
					PointsNormals[Index * 6 + 3] = Primitive.Normals[PointIndex];
					PointsNormals[Index * 6 + 4] = Primitive.Normals[PointIndex];
					PointsNormals[Index * 6 + 5] = Primitive.Normals[PointIndex];
				}
				else
				{
					PointsNormals[Index * 6] = FVector(0, 0, -1);
					PointsNormals[Index * 6 + 1] = FVector(0, 0, -1);
					PointsNormals[Index * 6 + 2] = FVector(0, 0, -1);
					PointsNormals[Index * 6 + 3] = FVector(0, 0, -1);
					PointsNormals[Index * 6 + 4] = FVector(0, 0, -1);
					PointsNormals[Index * 6 + 5] = FVector(0, 0, -1);
				}
			}

			if (bHasUVs)
			{
				if (Primitive.UVs[0].IsValidIndex(PointIndex))
				{
					PointsUV[Index * 6] = Primitive.UVs[0][PointIndex];
					PointsUV[Index * 6 + 1] = Primitive.UVs[0][PointIndex];
					PointsUV[Index * 6 + 2] = Primitive.UVs[0][PointIndex];
					PointsUV[Index * 6 + 3] = Primitive.UVs[0][PointIndex];
					PointsUV[Index * 6 + 4] = Primitive.UVs[0][PointIndex];
					PointsUV[Index * 6 + 5] = Primitive.UVs[0][PointIndex];
				}
				else
				{
					PointsUV[Index * 6] = FVector2D(0, 0);
					PointsUV[Index * 6 + 1] = FVector2D(0, 0);
					PointsUV[Index * 6 + 2] = FVector2D(0, 0);
					PointsUV[Index * 6 + 3] = FVector2D(0, 0);
					PointsUV[Index * 6 + 4] = FVector2D(0, 0);
					PointsUV[Index * 6 + 5] = FVector2D(0, 0);
				}
			}

			if (bUVXY)
			{
				PointsUVXY[Index * 6] = FVector2D(Point.X, Point.Y);
				PointsUVXY[Index * 6 + 1] = FVector2D(Point.X, Point.Y);
				PointsUVXY[Index * 6 + 2] = FVector2D(Point.X, Point.Y);
				PointsUVXY[Index * 6 + 3] = FVector2D(Point.X, Point.Y);
				PointsUVXY[Index * 6 + 4] = FVector2D(Point.X, Point.Y);
				PointsUVXY[Index * 6 + 5] = FVector2D(Point.X, Point.Y);
			}

			if (bUVZW)
			{
				PointsUVZW[Index * 6] = FVector2D(Point.Z, 0);
				PointsUVZW[Index * 6 + 1] = FVector2D(Point.Z, 0);
				PointsUVZW[Index * 6 + 2] = FVector2D(Point.Z, 0);
				PointsUVZW[Index * 6 + 3] = FVector2D(Point.Z, 0);
				PointsUVZW[Index * 6 + 4] = FVector2D(Point.Z, 0);
				PointsUVZW[Index * 6 + 5] = FVector2D(Point.Z, 0);
			}

			if (bHasColors)
			{
				if (Primitive.Colors.IsValidIndex(PointIndex))
				{
					const FVector4 Color = Primitive.Colors[PointIndex];
					PointsColors[Index * 6] = Color;
					PointsColors[Index * 6 + 1] = Color;
					PointsColors[Index * 6 + 2] = Color;
					PointsColors[Index * 6 + 3] = Color;
					PointsColors[Index * 6 + 4] = Color;
					PointsColors[Index * 6 + 5] = Color;
				}
				else
				{
					PointsColors[Index * 6] = FVector4(1, 1, 1, 1);
					PointsColors[Index * 6 + 1] = FVector4(1, 1, 1, 1);
					PointsColors[Index * 6 + 2] = FVector4(1, 1, 1, 1);
					PointsColors[Index * 6 + 3] = FVector4(1, 1, 1, 1);
					PointsColors[Index * 6 + 4] = FVector4(1, 1, 1, 1);
					PointsColors[Index * 6 + 5] = FVector4(1, 1, 1, 1);
				}
			}
		};

	auto ToTetrahedron = [&](const int32 Index)
		{
			const int32 NumVertices = bOpened ? 9 : 12;

			if (!Primitive.Indices.IsValidIndex(Index))
			{
				return;
			}

			const uint32 PointIndex = Primitive.Indices[Index];

			if (!Primitive.Positions.IsValidIndex(PointIndex))
			{
				return;
			}

			const FVector& Point = Primitive.Positions[PointIndex];

			for (int32 Iter = 0; Iter < NumVertices; Iter++)
			{
				PointsIndices[Index * NumVertices + Iter] = Index * NumVertices + Iter;
			}

			// front
			PointsPositions[Index * NumVertices] = Point + FVector(0, 0, 0.5) * Scale;
			PointsPositions[Index * NumVertices + 1] = Point + FVector(-0.5, -0.5, -0.5) * Scale;
			PointsPositions[Index * NumVertices + 2] = Point + FVector(-0.5, 0.5, -0.5) * Scale;

			if (bHasNormals)
			{
				if (Primitive.Normals.IsValidIndex(PointIndex))
				{
					PointsNormals[Index * NumVertices] = Primitive.Normals[PointIndex];
					PointsNormals[Index * NumVertices + 1] = Primitive.Normals[PointIndex];
					PointsNormals[Index * NumVertices + 2] = Primitive.Normals[PointIndex];
				}
				else
				{
					PointsNormals[Index * NumVertices] = FVector(0, 0, -1);
					PointsNormals[Index * NumVertices + 1] = FVector(0, 0, -1);
					PointsNormals[Index * NumVertices + 2] = FVector(0, 0, -1);
				}
			}

			// left
			PointsPositions[Index * NumVertices + 3] = Point + FVector(0, 0, 0.5) * Scale;
			PointsPositions[Index * NumVertices + 4] = Point + FVector(0.5, 0, -0.5) * Scale;
			PointsPositions[Index * NumVertices + 5] = Point + FVector(-0.5, -0.5, -0.5) * Scale;
			if (bHasNormals)
			{
				if (Primitive.Normals.IsValidIndex(PointIndex))
				{
					const FVector LeftNormal = -FVector::CrossProduct(Primitive.Normals[PointIndex], FVector(0, 0, 1)).GetSafeNormal();
					PointsNormals[Index * NumVertices + 3] = LeftNormal;
					PointsNormals[Index * NumVertices + 4] = LeftNormal;
					PointsNormals[Index * NumVertices + 5] = LeftNormal;
				}
				else
				{
					PointsNormals[Index * NumVertices + 3] = FVector(0, -1, 0);
					PointsNormals[Index * NumVertices + 4] = FVector(0, -1, 0);
					PointsNormals[Index * NumVertices + 5] = FVector(0, -1, 0);
				}
			}

			// right
			PointsPositions[Index * NumVertices + 6] = Point + FVector(0, 0, 0.5) * Scale;
			PointsPositions[Index * NumVertices + 7] = Point + FVector(-0.5, 0.5, -0.5) * Scale;
			PointsPositions[Index * NumVertices + 8] = Point + FVector(0.5, 0, -0.5) * Scale;
			if (bHasNormals)
			{
				if (Primitive.Normals.IsValidIndex(PointIndex))
				{
					const FVector RightNormal = FVector::CrossProduct(Primitive.Normals[PointIndex], FVector(0, 0, 1)).GetSafeNormal();
					PointsNormals[Index * NumVertices + 6] = RightNormal;
					PointsNormals[Index * NumVertices + 7] = RightNormal;
					PointsNormals[Index * NumVertices + 8] = RightNormal;
				}
				else
				{
					PointsNormals[Index * NumVertices + 6] = FVector(0, 1, 0);
					PointsNormals[Index * NumVertices + 7] = FVector(0, 1, 0);
					PointsNormals[Index * NumVertices + 8] = FVector(0, 1, 0);
				}
			}

			if (!bOpened)
			{
				// base
				PointsPositions[Index * NumVertices + 9] = Point + FVector(0.5, 0, -0.5) * Scale;
				PointsPositions[Index * NumVertices + 10] = Point + FVector(-0.5, 0.5, -0.5) * Scale;
				PointsPositions[Index * NumVertices + 11] = Point + FVector(-0.5, -0.5, -0.5) * Scale;
				if (bHasNormals)
				{
					PointsNormals[Index * NumVertices + 9] = FVector(0, 0, -1);
					PointsNormals[Index * NumVertices + 10] = FVector(0, 0, -1);
					PointsNormals[Index * NumVertices + 11] = FVector(0, 0, -1);
				}
			}

			if (bHasUVs)
			{
				if (Primitive.UVs[0].IsValidIndex(PointIndex))
				{
					for (int32 Iter = 0; Iter < NumVertices; Iter++)
					{
						PointsUV[Index * NumVertices + Iter] = Primitive.UVs[0][PointIndex];
					}
				}
				else
				{
					for (int32 Iter = 0; Iter < NumVertices; Iter++)
					{
						PointsUV[Index * NumVertices + Iter] = FVector2D(0, 0);
					}
				}
			}

			if (bUVXY)
			{
				for (int32 Iter = 0; Iter < NumVertices; Iter++)
				{
					PointsUVXY[Index * NumVertices + Iter] = FVector2D(Point.X, Point.Y);
				}
			}

			if (bUVZW)
			{
				for (int32 Iter = 0; Iter < NumVertices; Iter++)
				{
					PointsUVZW[Index * NumVertices + Iter] = FVector2D(Point.Z, 0);
				}
			}

			if (bHasColors)
			{
				if (Primitive.Colors.IsValidIndex(PointIndex))
				{
					const FVector4 Color = Primitive.Colors[PointIndex];
					for (int32 Iter = 0; Iter < NumVertices; Iter++)
					{
						PointsColors[Index * NumVertices + Iter] = Color;
					}
				}
				else
				{
					for (int32 Iter = 0; Iter < NumVertices; Iter++)
					{
						PointsColors[Index * NumVertices + Iter] = FVector4(1, 1, 1, 1);
					}
				}
			}
		};


	TFunction<void(const int32)> Triangulator = nullptr;

	switch (MaterialsConfig.PointsTriangulationMode)
	{
	case(EglTFRuntimePointsTriangulationMode::Triangle):
	{
		PointsIndices.AddZeroed(NumIndices * 3);
		PointsPositions.AddZeroed(NumIndices * 3);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 3);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 3);
		}
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 3);
		}
		Triangulator = ToTriangle;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::TriangleWithXYInUV1):
	{
		PointsIndices.AddZeroed(NumIndices * 3);
		PointsPositions.AddZeroed(NumIndices * 3);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 3);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 3);
		}
		PointsUVXY.AddZeroed(NumIndices * 3);
		bUVXY = true;
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 3);
		}
		Triangulator = ToTriangle;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::TriangleWithXYInUV1ZWInUV2):
	{
		PointsIndices.AddZeroed(NumIndices * 3);
		PointsPositions.AddZeroed(NumIndices * 3);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 3);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 3);
		}
		PointsUVXY.AddZeroed(NumIndices * 3);
		bUVXY = true;
		PointsUVZW.AddZeroed(NumIndices * 3);
		bUVZW = true;
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 3);
		}
		Triangulator = ToTriangle;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::Quad):
	{
		PointsIndices.AddZeroed(NumIndices * 6);
		PointsPositions.AddZeroed(NumIndices * 6);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 6);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 6);
		}
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 6);
		}
		Triangulator = ToQuad;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::QuadWithXYInUV1):
	{
		PointsIndices.AddZeroed(NumIndices * 6);
		PointsPositions.AddZeroed(NumIndices * 6);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 6);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 6);
		}
		PointsUVXY.AddZeroed(NumIndices * 6);
		bUVXY = true;
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 6);
		}
		Triangulator = ToQuad;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::QuadWithXYInUV1ZWInUV2):
	{
		PointsIndices.AddZeroed(NumIndices * 6);
		PointsPositions.AddZeroed(NumIndices * 6);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 6);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 6);
		}
		PointsUVXY.AddZeroed(NumIndices * 6);
		bUVXY = true;
		PointsUVZW.AddZeroed(NumIndices * 6);
		bUVZW = true;
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 6);
		}
		Triangulator = ToQuad;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::OpenedTetrahedron):
	{
		PointsIndices.AddZeroed(NumIndices * 9);
		PointsPositions.AddZeroed(NumIndices * 9);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 9);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 9);
		}
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 9);
		}
		bOpened = true;
		Triangulator = ToTetrahedron;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::OpenedTetrahedronWithXYInUV1ZWInUV2):
	{
		PointsIndices.AddZeroed(NumIndices * 9);
		PointsPositions.AddZeroed(NumIndices * 9);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 9);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 9);
		}
		PointsUVXY.AddZeroed(NumIndices * 9);
		bUVXY = true;
		PointsUVZW.AddZeroed(NumIndices * 9);
		bUVZW = true;
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 9);
		}
		bOpened = true;
		Triangulator = ToTetrahedron;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::Tetrahedron):
	{
		PointsIndices.AddZeroed(NumIndices * 12);
		PointsPositions.AddZeroed(NumIndices * 12);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 12);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 12);
		}
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 12);
		}
		Triangulator = ToTetrahedron;
	}
	break;
	case(EglTFRuntimePointsTriangulationMode::TetrahedronWithXYInUV1ZWInUV2):
	{
		PointsIndices.AddZeroed(NumIndices * 12);
		PointsPositions.AddZeroed(NumIndices * 12);
		if (bHasNormals)
		{
			PointsNormals.AddZeroed(NumIndices * 12);
		}
		if (bHasUVs)
		{
			PointsUV.AddZeroed(NumIndices * 12);
		}
		PointsUVXY.AddZeroed(NumIndices * 12);
		bUVXY = true;
		PointsUVZW.AddZeroed(NumIndices * 12);
		bUVZW = true;
		if (bHasColors)
		{
			PointsColors.AddZeroed(NumIndices * 12);
		}
		Triangulator = ToTetrahedron;
	}
	break;
	default:
		UE_LOG(LogGLTFRuntime, Error, TEXT("Unsupported/Unimplemented Triangulator algorithm"));
		break;
	}

	if (Triangulator)
	{
		ParallelFor(NumIndices, Triangulator);
		Primitive.Mode = 4;
		Primitive.bDisableShadows = true;
		Primitive.Indices = PointsIndices;
		Primitive.Positions = PointsPositions;
		if (bHasNormals)
		{
			Primitive.Normals = PointsNormals;
		}
		Primitive.UVs = { PointsUV };
		if (bUVXY)
		{
			Primitive.UVs.Add(MoveTemp(PointsUVXY));
		}
		if (bUVZW)
		{
			Primitive.UVs.Add(MoveTemp(PointsUVZW));
		}
		Primitive.Tangents.Empty(); // TODO we should honour supplied tangents...
		Primitive.bHighPrecisionUVs = true;
		if (bHasColors)
		{
			Primitive.Colors = PointsColors;
		}

		return MaterialsConfig.PointsBaseMaterial;
	}

	return nullptr;
}

UMaterialInterface* FglTFRuntimeParser::TriangulateLines(FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (Primitive.Mode != 1) // only plain lines for now
	{
		return nullptr;
	}

	TArray<uint32> LinesIndices;
	TArray<FVector> LinesPositions;
	TArray<FVector4> LinesColors;
	TArray<FVector2D> LinesUV;
	TArray<FVector2D> PointsUVXY;
	TArray<FVector2D> PointsUVZW;

	bool bHasColors = Primitive.Colors.Num() > 0;
	bool bHasNormals = Primitive.Normals.Num() > 0;
	const bool bHasUVs = Primitive.UVs.Num() > 0;

	bool bUVXY = false;
	bool bUVZW = false;
	bool bOpened = false;

	const float Scale = MaterialsConfig.LinesScaleFactor;

	auto ToOpenedTriangularPrism = [&](const int32 Index)
		{
			const uint32 Point0Index = Primitive.Indices[Index * 2];
			const uint32 Point1Index = Primitive.Indices[Index * 2 + 1];

			if (!Primitive.Positions.IsValidIndex(Point0Index) || !Primitive.Positions.IsValidIndex(Point1Index))
			{
				return;
			}

			const FVector& Point0 = Primitive.Positions[Point0Index];
			const FVector& Point1 = Primitive.Positions[Point1Index];

			const FVector LineForward = (Point1 - Point0).GetSafeNormal();
			FVector LineRight = FVector::CrossProduct(LineForward, FVector(0, 0, 1));
			if (FMath::Abs(FVector::DotProduct(LineForward, FVector(0, 0, 1))) > FMath::Abs(FVector::DotProduct(LineForward, FVector(-1, 0, 0))))
			{
				LineRight = FVector::CrossProduct(LineForward, FVector(-1, 0, 0));
			}
			const FVector LineUp = FVector::CrossProduct(LineForward, LineRight);
			LineRight = FVector::CrossProduct(LineForward, LineUp);

			FVector2D Point0UV = FVector2D(0, 0);
			FVector2D Point1UV = FVector2D(0, 0);

			if (bHasUVs)
			{
				if (Primitive.UVs[0].IsValidIndex(Point0Index))
				{
					Point0UV = Primitive.UVs[0][Point0Index];
				}
				if (Primitive.UVs[0].IsValidIndex(Point1Index))
				{
					Point1UV = Primitive.UVs[0][Point1Index];
				}
			}

			const int32 NumVertices = bOpened ? 12 : 18;

			for (int32 Iter = 0; Iter < NumVertices; Iter++)
			{
				LinesIndices[Index * NumVertices + Iter] = Index * 12 + Iter;
			}

			// right bottom
			LinesPositions[Index * NumVertices] = Point1 - (LineUp * 0.5 + LineRight * 0.5) * Scale;
			LinesPositions[Index * NumVertices + 1] = Point0 + (LineUp * 0.5) * Scale;
			LinesPositions[Index * NumVertices + 2] = Point0 - (LineUp * 0.5 + LineRight * 0.5) * Scale;

			// right top
			LinesPositions[Index * NumVertices + 3] = Point1 + (LineUp * 0.5) * Scale;
			LinesPositions[Index * NumVertices + 4] = Point0 + (LineUp * 0.5) * Scale;
			LinesPositions[Index * NumVertices + 5] = Point1 - (LineUp * 0.5 + LineRight * 0.5) * Scale;

			// left bottom
			LinesPositions[Index * NumVertices + 6] = Point1 - (LineUp * 0.5 - LineRight * 0.5) * Scale;
			LinesPositions[Index * NumVertices + 7] = Point0 + (LineUp * 0.5) * Scale;
			LinesPositions[Index * NumVertices + 8] = Point0 - (LineUp * 0.5 - LineRight * 0.5) * Scale;

			// left top
			LinesPositions[Index * NumVertices + 9] = Point1 + (LineUp * 0.5) * Scale;
			LinesPositions[Index * NumVertices + 10] = Point0 + (LineUp * 0.5) * Scale;
			LinesPositions[Index * NumVertices + 11] = Point1 - (LineUp * 0.5 - LineRight * 0.5) * Scale;

			if (!bOpened)
			{
				// bottom triangle
				// top triangle
			}

			if (bHasUVs)
			{
				LinesUV[Index * NumVertices] = Point1UV;
				LinesUV[Index * NumVertices + 1] = Point0UV;
				LinesUV[Index * NumVertices + 2] = Point0UV;
				LinesUV[Index * NumVertices + 3] = Point1UV;
				LinesUV[Index * NumVertices + 4] = Point0UV;
				LinesUV[Index * NumVertices + 5] = Point1UV;
				LinesUV[Index * NumVertices + 6] = Point1UV;
				LinesUV[Index * NumVertices + 7] = Point0UV;
				LinesUV[Index * NumVertices + 8] = Point0UV;
				LinesUV[Index * NumVertices + 9] = Point1UV;
				LinesUV[Index * NumVertices + 10] = Point0UV;
				LinesUV[Index * NumVertices + 11] = Point1UV;
			}

			if (bUVXY)
			{
				PointsUVXY[Index * NumVertices] = FVector2D(Point1.X, Point1.Y);
				PointsUVXY[Index * NumVertices + 1] = FVector2D(Point0.X, Point0.Y);
				PointsUVXY[Index * NumVertices + 2] = FVector2D(Point0.X, Point0.Y);
				PointsUVXY[Index * NumVertices + 3] = FVector2D(Point1.X, Point1.Y);
				PointsUVXY[Index * NumVertices + 4] = FVector2D(Point0.X, Point0.Y);
				PointsUVXY[Index * NumVertices + 5] = FVector2D(Point1.X, Point1.Y);
				PointsUVXY[Index * NumVertices + 6] = FVector2D(Point1.X, Point1.Y);
				PointsUVXY[Index * NumVertices + 7] = FVector2D(Point0.X, Point0.Y);
				PointsUVXY[Index * NumVertices + 8] = FVector2D(Point0.X, Point0.Y);
				PointsUVXY[Index * NumVertices + 9] = FVector2D(Point1.X, Point1.Y);
				PointsUVXY[Index * NumVertices + 10] = FVector2D(Point0.X, Point0.Y);
				PointsUVXY[Index * NumVertices + 11] = FVector2D(Point1.X, Point1.Y);
			}

			if (bUVZW)
			{
				PointsUVZW[Index * NumVertices] = FVector2D(Point1.Z, 0);
				PointsUVZW[Index * NumVertices + 1] = FVector2D(Point0.Z, 0);
				PointsUVZW[Index * NumVertices + 2] = FVector2D(Point0.Z, 0);
				PointsUVZW[Index * NumVertices + 3] = FVector2D(Point1.Z, 0);
				PointsUVZW[Index * NumVertices + 4] = FVector2D(Point0.Z, 0);
				PointsUVZW[Index * NumVertices + 5] = FVector2D(Point1.Z, 0);
				PointsUVZW[Index * NumVertices + 6] = FVector2D(Point1.Z, 0);
				PointsUVZW[Index * NumVertices + 7] = FVector2D(Point0.Z, 0);
				PointsUVZW[Index * NumVertices + 8] = FVector2D(Point0.Z, 0);
				PointsUVZW[Index * NumVertices + 9] = FVector2D(Point1.Z, 0);
				PointsUVZW[Index * NumVertices + 10] = FVector2D(Point0.Z, 0);
				PointsUVZW[Index * NumVertices + 11] = FVector2D(Point1.Z, 0);
			}

			if (bHasColors)
			{
				FVector4 Color0 = FVector4(1, 1, 1, 1);
				if (Primitive.Colors.IsValidIndex(Point0Index))
				{
					Color0 = Primitive.Colors[Point0Index];
				}

				FVector4 Color1 = FVector4(1, 1, 1, 1);
				if (Primitive.Colors.IsValidIndex(Point1Index))
				{
					Color1 = Primitive.Colors[Point1Index];
				}

				LinesColors[Index * NumVertices] = Color1;
				LinesColors[Index * NumVertices + 1] = Color0;
				LinesColors[Index * NumVertices + 2] = Color0;
				LinesColors[Index * NumVertices + 3] = Color1;
				LinesColors[Index * NumVertices + 4] = Color0;
				LinesColors[Index * NumVertices + 5] = Color1;
				LinesColors[Index * NumVertices + 6] = Color1;
				LinesColors[Index * NumVertices + 7] = Color0;
				LinesColors[Index * NumVertices + 8] = Color0;
				LinesColors[Index * NumVertices + 9] = Color1;
				LinesColors[Index * NumVertices + 10] = Color0;
				LinesColors[Index * NumVertices + 11] = Color1;
			}

		};

	TFunction<void(const int32)> Triangulator = nullptr;

	switch (MaterialsConfig.LinesTriangulationMode)
	{
	case(EglTFRuntimeLinesTriangulationMode::OpenedTriangularPrism):
	{
		const int32 NumIndices = (Primitive.Indices.Num() / 2) * 12;
		LinesIndices.AddZeroed(NumIndices);
		LinesPositions.AddZeroed(NumIndices);
		if (bHasUVs)
		{
			LinesUV.AddZeroed(NumIndices);
		}
		if (bHasColors)
		{
			LinesColors.AddZeroed(NumIndices);
		}
		bOpened = true;
		Triangulator = ToOpenedTriangularPrism;
	}
	break;
	case(EglTFRuntimeLinesTriangulationMode::OpenedTriangularPrismWithXYInUV1ZWInUV2):
	{
		const int32 NumIndices = (Primitive.Indices.Num() / 2) * 12;
		LinesIndices.AddZeroed(NumIndices);
		LinesPositions.AddZeroed(NumIndices);
		if (bHasUVs)
		{
			LinesUV.AddZeroed(NumIndices);
		}
		if (bHasColors)
		{
			LinesColors.AddZeroed(NumIndices);
		}
		bUVXY = true;
		bUVZW = true;
		bOpened = true;
		Triangulator = ToOpenedTriangularPrism;
	}
	break;
	default:
		UE_LOG(LogGLTFRuntime, Error, TEXT("Unsupported/Unimplemented Triangulator algorithm"));
		break;
	}

	if (Triangulator)
	{
		const int32 NumLines = Primitive.Indices.Num() / 2;
		ParallelFor(NumLines, Triangulator);
		Primitive.Mode = 4;
		Primitive.bDisableShadows = true;
		Primitive.Indices = LinesIndices;
		Primitive.Positions = LinesPositions;
		Primitive.UVs = { LinesUV };
		if (bUVXY)
		{
			Primitive.UVs.Add(MoveTemp(PointsUVXY));
		}
		if (bUVZW)
		{
			Primitive.UVs.Add(MoveTemp(PointsUVZW));
		}
		Primitive.Normals.Empty(); // TODO we should honour supplied normals...
		Primitive.Tangents.Empty(); // TODO we should honour supplied tangents...
		Primitive.bHighPrecisionUVs = true;
		if (bHasColors)
		{
			Primitive.Colors = LinesColors;
		}

		return MaterialsConfig.LinesBaseMaterial;
	}

	return nullptr;
}

UMaterialInterface* FglTFRuntimeParser::TriangulatePointsAndLines(FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (Primitive.Mode == 0 && !MaterialsConfig.bSkipPoints) // points
	{
		return TriangulatePoints(Primitive, MaterialsConfig);
	}
	else if (Primitive.Mode >= 1 && Primitive.Mode <= 3 && !MaterialsConfig.bSkipLines)
	{
		return TriangulateLines(Primitive, MaterialsConfig);
	}

	return nullptr;
}

bool FglTFRuntimeParser::GetBuffer(const int32 Index, FglTFRuntimeBlob& Blob)
{
	if (Index < 0)
	{
		return false;
	}

	if (Index == 0 && BinaryBuffer.Num() > 0)
	{
		Blob.Data = BinaryBuffer.GetData();
		Blob.Num = BinaryBuffer.Num();
		return true;
	}

	// first check cache
	if (BuffersCache.Contains(Index))
	{
		Blob.Data = BuffersCache[Index].GetData();
		Blob.Num = BuffersCache[Index].Num();
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonBuffers;

	// no buffers ?
	if (!Root->TryGetArrayField(TEXT("buffers"), JsonBuffers))
	{
		return false;
	}

	if (Index >= JsonBuffers->Num())
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonBufferObject = (*JsonBuffers)[Index]->AsObject();
	if (!JsonBufferObject)
	{
		return false;
	}

	int64 ByteLength;
	if (!JsonBufferObject->TryGetNumberField(TEXT("byteLength"), ByteLength))
	{
		return false;
	}

	FString Uri;
	if (!JsonBufferObject->TryGetStringField(TEXT("uri"), Uri))
	{
		return false;
	}

	// check it is a valid base64 data uri
	if (Uri.StartsWith("data:"))
	{
		TArray64<uint8> Base64Data;
		if (ParseBase64Uri(Uri, Base64Data))
		{
			BuffersCache.Add(Index, Base64Data);
			Blob.Data = BuffersCache[Index].GetData();
			Blob.Num = BuffersCache[Index].Num();
			return true;
		}
		return false;
	}

	if (Archive)
	{
		TArray64<uint8> ArchiveItemData;
		if (Archive->GetFileContent(Uri, ArchiveItemData))
		{
			BuffersCache.Add(Index, ArchiveItemData);
			Blob.Data = BuffersCache[Index].GetData();
			Blob.Num = BuffersCache[Index].Num();
			return true;
		}
	}

	// fallback
	if (!BaseDirectory.IsEmpty())
	{
		TArray64<uint8> FileData;
		if (FFileHelper::LoadFileToArray(FileData, *FPaths::Combine(BaseDirectory, Uri)))
		{
			BuffersCache.Add(Index, FileData);
			Blob.Data = BuffersCache[Index].GetData();
			Blob.Num = BuffersCache[Index].Num();
			return true;
		}
	}

	AddError("GetBuffer()", FString::Printf(TEXT("Unable to load buffer %d from Uri %s (you may want to enable external files loading...)"), Index, *Uri));
	return false;
}

bool FglTFRuntimeParser::ParseBase64Uri(const FString& Uri, TArray64<uint8>& Bytes)
{
	const FString Base64Signature = ";base64,";

	int32 StringIndex = Uri.Find(Base64Signature, ESearchCase::IgnoreCase, ESearchDir::FromStart, 5);

	if (StringIndex < 5)
		return false;

	StringIndex += Base64Signature.Len();

	TArray<uint8> BytesBase64;

	bool bSuccess = FBase64::Decode(Uri.Mid(StringIndex), BytesBase64);
	if (bSuccess)
	{
		Bytes.Append(BytesBase64);
	}
	return bSuccess;
}

bool FglTFRuntimeParser::GetBufferView(const int32 Index, FglTFRuntimeBlob& Blob, int64& Stride)
{
	TSharedPtr<FJsonObject> JsonBufferViewObject = GetJsonObjectFromRootIndex("bufferViews", Index);
	if (!JsonBufferViewObject)
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonBufferViewCompressedObject = GetJsonObjectExtension(JsonBufferViewObject.ToSharedRef(), "EXT_meshopt_compression");
	if (JsonBufferViewCompressedObject)
	{
		JsonBufferViewObject = JsonBufferViewCompressedObject;
		if (CompressedBufferViewsCache.Contains(Index))
		{
			Blob.Data = CompressedBufferViewsCache[Index].GetData();
			Blob.Num = CompressedBufferViewsCache[Index].Num();
			Stride = CompressedBufferViewsStridesCache[Index];
			return true;
		}
	}

	int64 BufferIndex;
	if (!JsonBufferViewObject->TryGetNumberField(TEXT("buffer"), BufferIndex))
	{
		return false;
	}

	FglTFRuntimeBlob BufferBlob;
	if (!GetBuffer(BufferIndex, BufferBlob))
	{
		return false;
	}

	int64 ByteLength;
	if (!JsonBufferViewObject->TryGetNumberField(TEXT("byteLength"), ByteLength))
	{
		return false;
	}

	int64 ByteOffset;
	if (!JsonBufferViewObject->TryGetNumberField(TEXT("byteOffset"), ByteOffset))
	{
		ByteOffset = 0;
	}

	if (!JsonBufferViewObject->TryGetNumberField(TEXT("byteStride"), Stride))
	{
		Stride = 0;
	}

	if (ByteOffset + ByteLength > BufferBlob.Num)
	{
		return false;
	}

	Blob.Data = BufferBlob.Data + ByteOffset;
	Blob.Num = ByteLength;

	if (JsonBufferViewCompressedObject)
	{
		// decompress bitstream
		if (Stride == 0)
		{
			return false;
		}
		int64 Elements;
		if (!JsonBufferViewObject->TryGetNumberField(TEXT("count"), Elements))
		{
			return false;
		}
		FString MeshOptMode;
		if (!JsonBufferViewObject->TryGetStringField(TEXT("mode"), MeshOptMode))
		{
			return false;
		}
		FString MeshOptFilter;
		if (!JsonBufferViewObject->TryGetStringField(TEXT("filter"), MeshOptFilter))
		{
			MeshOptFilter = "NONE";
		}

		CompressedBufferViewsCache.Add(Index);
		if (!DecompressMeshOptimizer(Blob, Stride, Elements, MeshOptMode, MeshOptFilter, CompressedBufferViewsCache[Index]))
		{
			CompressedBufferViewsCache.Remove(Index);
			return false;
		}
		Blob.Data = CompressedBufferViewsCache[Index].GetData();
		Blob.Num = CompressedBufferViewsCache[Index].Num();
		CompressedBufferViewsStridesCache.Add(Index, Stride);
	}

	return true;
}

bool FglTFRuntimeParser::GetAccessor(const int32 Index, int64& ComponentType, int64& Stride, int64& Elements, int64& ElementSize, int64& Count, bool& bNormalized, FglTFRuntimeBlob& Blob, const FglTFRuntimeBlob* AdditionalBufferView)
{

	TSharedPtr<FJsonObject> JsonAccessorObject = GetJsonObjectFromRootIndex("accessors", Index);
	if (!JsonAccessorObject)
	{
		return false;
	}

	bool bInitWithZeros = false;
	bool bHasSparse = false;

	int64 BufferViewIndex = INDEX_NONE;
	int64 ByteOffset = 0;

	if (!AdditionalBufferView)
	{
		if (!JsonAccessorObject->TryGetNumberField(TEXT("bufferView"), BufferViewIndex))
		{
			bInitWithZeros = true;
		}


		if (!JsonAccessorObject->TryGetNumberField(TEXT("byteOffset"), ByteOffset))
		{
			ByteOffset = 0;
		}
	}

	const TSharedPtr<FJsonObject>* JsonSparseObject = nullptr;
	if (JsonAccessorObject->TryGetObjectField(TEXT("sparse"), JsonSparseObject))
	{
		bHasSparse = true;
	}

	const bool bOriginalNormalized = bNormalized;

	if (!JsonAccessorObject->TryGetBoolField(TEXT("normalized"), bNormalized))
	{
		bNormalized = bOriginalNormalized;
	}

	if (!JsonAccessorObject->TryGetNumberField(TEXT("componentType"), ComponentType))
	{
		return false;
	}

	if (!JsonAccessorObject->TryGetNumberField(TEXT("count"), Count))
	{
		return false;
	}

	FString Type;
	if (!JsonAccessorObject->TryGetStringField(TEXT("type"), Type))
	{
		return false;
	}

	ElementSize = GetComponentTypeSize(ComponentType);
	if (ElementSize == 0)
	{
		return false;
	}

	Elements = GetTypeSize(Type);
	if (Elements == 0)
	{
		return false;
	}

	int64 FinalSize = ElementSize * Elements * Count;

	if (AdditionalBufferView)
	{
		if (AdditionalBufferView->Num < FinalSize)
		{
			return false;
		}
		Blob.Data = AdditionalBufferView->Data;
		Blob.Num = FinalSize;

		// special case for bigger buffers
		if (FinalSize < AdditionalBufferView->Num && (AdditionalBufferView->Num % (ElementSize * Elements)) == 0)
		{
			Count = AdditionalBufferView->Num / (ElementSize * Elements);
		}

		if (!bHasSparse)
		{
			Stride = ElementSize * Elements;
			return true;
		}
	}
	else if (bInitWithZeros)
	{

		if (ZeroBuffer.Num() < FinalSize)
		{
			ZeroBuffer.AddZeroed(FinalSize - ZeroBuffer.Num());
		}
		Blob.Data = ZeroBuffer.GetData();
		Blob.Num = FinalSize;
		if (!bHasSparse)
		{
			Stride = ElementSize * Elements;
			return true;
		}
	}
	else
	{
		if (!GetBufferView(BufferViewIndex, Blob, Stride))
		{
			return false;
		}

		if (Stride == 0)
		{
			Stride = ElementSize * Elements;
		}

		FinalSize = Stride * Count;

		if (FinalSize > Blob.Num)
		{
			return false;
		}

		if (ByteOffset > 0)
		{
			Blob.Data += ByteOffset;
			if (Stride > ElementSize * Elements)
			{
				Blob.Num = FinalSize - (Stride - (ElementSize * Elements));
			}
			else
			{
				Blob.Num = FinalSize;
			}
		}

		if (!bHasSparse)
		{
			return true;
		}
	}

	if (SparseAccessorsCache.Contains(Index))
	{
		Stride = SparseAccessorsStridesCache[Index];
		Blob.Data = SparseAccessorsCache[Index].GetData();
		Blob.Num = SparseAccessorsCache[Index].Num();
		return true;
	}

	int64 SparseCount;
	if (!(*JsonSparseObject)->TryGetNumberField(TEXT("count"), SparseCount))
	{
		return false;
	}

	if ((SparseCount > FinalSize) || (SparseCount < 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* JsonSparseIndicesObject = nullptr;
	if (!(*JsonSparseObject)->TryGetObjectField(TEXT("indices"), JsonSparseIndicesObject))
	{
		return true;
	}

	int32 SparseBufferViewIndex = GetJsonObjectIndex(JsonSparseIndicesObject->ToSharedRef(), "bufferView", INDEX_NONE);
	if (SparseBufferViewIndex < 0)
	{
		return false;
	}

	int64 SparseByteOffset;
	if (!(*JsonSparseIndicesObject)->TryGetNumberField(TEXT("byteOffset"), SparseByteOffset))
	{
		SparseByteOffset = 0;
	}

	int64 SparseComponentType;
	if (!(*JsonSparseIndicesObject)->TryGetNumberField(TEXT("componentType"), SparseComponentType))
	{
		return false;
	}

	FglTFRuntimeBlob SparseBytesIndices;
	int64 SparseBufferViewIndicesStride;
	if (!GetBufferView(SparseBufferViewIndex, SparseBytesIndices, SparseBufferViewIndicesStride))
	{
		return false;
	}

	if (SparseBufferViewIndicesStride == 0)
	{
		SparseBufferViewIndicesStride = GetComponentTypeSize(SparseComponentType);
	}


	if (((SparseBytesIndices.Num - SparseByteOffset) / SparseBufferViewIndicesStride) < SparseCount)
	{
		return false;
	}

	TArray<uint32> SparseIndices;
	uint8* SparseIndicesBase = &SparseBytesIndices.Data[SparseByteOffset];

	for (int32 SparseIndexOffset = 0; SparseIndexOffset < SparseCount; SparseIndexOffset++)
	{
		// UNSIGNED_BYTE
		if (SparseComponentType == 5121)
		{
			SparseIndices.Add(*SparseIndicesBase);
		}
		// UNSIGNED_SHORT
		else if (SparseComponentType == 5123)
		{
			uint16* SparseIndicesBaseUint16 = (uint16*)SparseIndicesBase;
			SparseIndices.Add(*SparseIndicesBaseUint16);
		}
		// UNSIGNED_INT
		else if (SparseComponentType == 5125)
		{
			uint32* SparseIndicesBaseUint32 = (uint32*)SparseIndicesBase;
			SparseIndices.Add(*SparseIndicesBaseUint32);
		}
		else
		{
			return false;
		}
		SparseIndicesBase += SparseBufferViewIndicesStride;
	}

	const TSharedPtr<FJsonObject>* JsonSparseValuesObject = nullptr;
	if (!(*JsonSparseObject)->TryGetObjectField(TEXT("values"), JsonSparseValuesObject))
	{
		return true;
	}

	int32 SparseValueBufferViewIndex = GetJsonObjectIndex(JsonSparseValuesObject->ToSharedRef(), "bufferView", INDEX_NONE);
	if (SparseValueBufferViewIndex < 0)
	{
		return false;
	}

	int64 SparseValueByteOffset;
	if (!(*JsonSparseValuesObject)->TryGetNumberField(TEXT("byteOffset"), SparseValueByteOffset))
	{
		SparseValueByteOffset = 0;
	}

	FglTFRuntimeBlob SparseBytesValues;
	int64 SparseBufferViewValuesStride;
	if (!GetBufferView(SparseValueBufferViewIndex, SparseBytesValues, SparseBufferViewValuesStride))
	{
		return false;
	}

	if (SparseBufferViewValuesStride == 0)
	{
		SparseBufferViewValuesStride = ElementSize * Elements;
	}

	Stride = SparseBufferViewValuesStride;

	SparseAccessorsCache.Add(Index);
	SparseAccessorsStridesCache.Add(Index, Stride);
	TArray64<uint8>& SparseData = SparseAccessorsCache[Index];
	SparseData.Append(Blob.Data, Blob.Num);

	for (int32 IndexToChange = 0; IndexToChange < SparseCount; IndexToChange++)
	{
		uint32 SparseIndexToChange = SparseIndices[IndexToChange];
		if (SparseIndexToChange >= (Blob.Num / Stride))
		{
			return false;
		}

		uint8* OriginalValuePtr = (uint8*)(SparseData.GetData() + Stride * SparseIndexToChange);
		uint8* NewValuePtr = (uint8*)(SparseBytesValues.Data + SparseBufferViewValuesStride * IndexToChange);
		FMemory::Memcpy(OriginalValuePtr, NewValuePtr, SparseBufferViewValuesStride);
	}

	Blob.Data = SparseData.GetData();

	return true;
}

int64 FglTFRuntimeParser::GetComponentTypeSize(const int64 ComponentType) const
{
	switch (ComponentType)
	{
	case(5120):
		return 1;
	case(5121):
		return 1;
	case(5122):
		return 2;
	case(5123):
		return 2;
	case(5125):
		return 4;
	case(5126):
		return 4;
	default:
		break;
	}

	return 0;
}

int64 FglTFRuntimeParser::GetTypeSize(const FString& Type) const
{
	if (Type == "SCALAR")
		return 1;
	else if (Type == "VEC2")
		return 2;
	else if (Type == "VEC3")
		return 3;
	else if (Type == "VEC4")
		return 4;
	else if (Type == "MAT2")
		return 4;
	else if (Type == "MAT3")
		return 9;
	else if (Type == "MAT4")
		return 16;

	return 0;
}

void FglTFRuntimeParser::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(StaticMeshesCache);
	Collector.AddReferencedObjects(MaterialsCache);
	Collector.AddReferencedObjects(SkeletonsCache);
	Collector.AddReferencedObjects(SkeletalMeshesCache);
	Collector.AddReferencedObjects(TexturesCache);
	Collector.AddReferencedObjects(MaterialsNameCache);
	Collector.AddReferencedObjects(MetallicRoughnessMaterialsMap);
	Collector.AddReferencedObjects(SpecularGlossinessMaterialsMap);
	Collector.AddReferencedObjects(UnlitMaterialsMap);
	Collector.AddReferencedObjects(TransmissionMaterialsMap);
	Collector.AddReferencedObjects(ClearCoatMaterialsMap);
}

void FglTFRuntimeParser::ClearCache()
{
	StaticMeshesCache.Empty();
	MaterialsCache.Empty();
	SkeletonsCache.Empty();
	SkeletalMeshesCache.Empty();
	TexturesCache.Empty();
	MaterialsNameCache.Empty();
	MetallicRoughnessMaterialsMap.Empty();
	SpecularGlossinessMaterialsMap.Empty();
	UnlitMaterialsMap.Empty();
	TransmissionMaterialsMap.Empty();
	ClearCoatMaterialsMap.Empty();
}

float FglTFRuntimeParser::FindBestFrames(const TArray<float>& FramesTimes, float WantedTime, int32& FirstIndex, int32& SecondIndex)
{
	SecondIndex = INDEX_NONE;
	// first search for second (higher value)
	for (int32 i = 0; i < FramesTimes.Num(); i++)
	{
		float TimeValue = FramesTimes[i] - FramesTimes[0];
		if (FMath::IsNearlyEqual(TimeValue, WantedTime))
		{
			FirstIndex = i;
			SecondIndex = i;
			return 0;
		}
		else if (TimeValue > WantedTime)
		{
			SecondIndex = i;
			break;
		}
	}

	// not found ? use the last value
	if (SecondIndex == INDEX_NONE)
	{
		SecondIndex = FramesTimes.Num() - 1;
	}

	if (SecondIndex == 0)
	{
		FirstIndex = 0;
		return 1.f;
	}

	FirstIndex = SecondIndex - 1;

	return ((WantedTime + FramesTimes[0]) - FramesTimes[FirstIndex]) / (FramesTimes[SecondIndex] - FramesTimes[FirstIndex]);
}

bool FglTFRuntimeParser::MergePrimitives(TArray<FglTFRuntimePrimitive> SourcePrimitives, FglTFRuntimePrimitive& OutPrimitive)
{
	if (SourcePrimitives.Num() < 1)
	{
		return false;
	}

	FglTFRuntimePrimitive& MainPrimitive = SourcePrimitives[0];
	for (FglTFRuntimePrimitive& SourcePrimitive : SourcePrimitives)
	{
		if (FMath::Clamp(SourcePrimitive.Positions.Num(), 0, 1) != FMath::Clamp(MainPrimitive.Positions.Num(), 0, 1))
		{
			return false;
		}

		if (FMath::Clamp(SourcePrimitive.Normals.Num(), 0, 1) != FMath::Clamp(MainPrimitive.Normals.Num(), 0, 1))
		{
			return false;
		}

		if (FMath::Clamp(SourcePrimitive.Tangents.Num(), 0, 1) != FMath::Clamp(MainPrimitive.Tangents.Num(), 0, 1))
		{
			return false;
		}

		if (FMath::Clamp(SourcePrimitive.Colors.Num(), 0, 1) != FMath::Clamp(MainPrimitive.Colors.Num(), 0, 1))
		{
			return false;
		}

		if (SourcePrimitive.UVs.Num() != MainPrimitive.UVs.Num())
		{
			return false;
		}

		if (SourcePrimitive.Joints.Num() != MainPrimitive.Joints.Num())
		{
			return false;
		}

		if (SourcePrimitive.Weights.Num() != MainPrimitive.Weights.Num())
		{
			return false;
		}

		if (SourcePrimitive.MorphTargets.Num() != MainPrimitive.MorphTargets.Num())
		{
			return false;
		}
	}

	uint32 BaseIndex = 0;
	for (FglTFRuntimePrimitive& SourcePrimitive : SourcePrimitives)
	{
		OutPrimitive.Material = SourcePrimitive.Material;

		// TODO the logic here is available only for staticmeshes loaded as skeletal ones.
		// It should be improved to support plain recursive loading of skeletalmeshes
		if (SourcePrimitive.OverrideBoneMap.Num() == 1 && SourcePrimitive.OverrideBoneMap.Contains(0))
		{
			OutPrimitive.OverrideBoneMap.Add(OutPrimitive.Indices.Num(), SourcePrimitive.OverrideBoneMap[0]);
		}

		for (uint32 Index : SourcePrimitive.Indices)
		{
			OutPrimitive.Indices.Add(Index + BaseIndex);
		}

		if (BaseIndex == 0)
		{
			OutPrimitive.UVs = SourcePrimitive.UVs;
			OutPrimitive.Joints = SourcePrimitive.Joints;
			OutPrimitive.Weights = SourcePrimitive.Weights;
			OutPrimitive.MorphTargets = SourcePrimitive.MorphTargets;
		}
		else
		{
			for (int32 UVChannel = 0; UVChannel < OutPrimitive.UVs.Num(); UVChannel++)
			{
				OutPrimitive.UVs[UVChannel].Append(SourcePrimitive.UVs[UVChannel]);
			}

			for (int32 JointsIndex = 0; JointsIndex < OutPrimitive.Joints.Num(); JointsIndex++)
			{
				OutPrimitive.Joints[JointsIndex].Append(SourcePrimitive.Joints[JointsIndex]);
			}

			for (int32 WeightsIndex = 0; WeightsIndex < OutPrimitive.Weights.Num(); WeightsIndex++)
			{
				OutPrimitive.Weights[WeightsIndex].Append(SourcePrimitive.Weights[WeightsIndex]);
			}

			for (int32 MorphTargetsIndex = 0; MorphTargetsIndex < OutPrimitive.MorphTargets.Num(); MorphTargetsIndex++)
			{
				OutPrimitive.MorphTargets[MorphTargetsIndex].Positions.Append(SourcePrimitive.MorphTargets[MorphTargetsIndex].Positions);
				OutPrimitive.MorphTargets[MorphTargetsIndex].Normals.Append(SourcePrimitive.MorphTargets[MorphTargetsIndex].Normals);
			}
		}

		OutPrimitive.Positions.Append(SourcePrimitive.Positions);
		OutPrimitive.Normals.Append(SourcePrimitive.Normals);
		OutPrimitive.Tangents.Append(SourcePrimitive.Tangents);
		OutPrimitive.Colors.Append(SourcePrimitive.Colors);


		BaseIndex += SourcePrimitive.Positions.Num();
	}

	return true;
}

bool FglTFRuntimeParser::MeshHasMorphTargets(const int32 MeshIndex) const
{
	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		return false;
	}

	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	if (!JsonMeshObject->TryGetArrayField(TEXT("primitives"), JsonPrimitives))
	{
		return false;
	}

	for (TSharedPtr<FJsonValue> JsonPrimitive : *JsonPrimitives)
	{
		TSharedPtr<FJsonObject> JsonPrimitiveObject = JsonPrimitive->AsObject();
		if (!JsonPrimitiveObject)
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* JsonTargetsArray;
		if (!JsonPrimitiveObject->TryGetArrayField(TEXT("targets"), JsonTargetsArray))
		{
			return false;
		}

		if (JsonTargetsArray->Num() > 0)
		{
			return true;
		}
	}

	return false;
}

bool FglTFRuntimeParser::GetMorphTargetNames(const int32 MeshIndex, TArray<FName>& MorphTargetNames)
{
	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		AddError("GetMorphTargetNames()", FString::Printf(TEXT("Unable to find Mesh with index %d"), MeshIndex));
		return false;
	}
	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	if (!JsonMeshObject->TryGetArrayField(TEXT("primitives"), JsonPrimitives))
	{
		AddError("GetMorphTargetNames()", "No primitives defined in the asset.");
		return false;
	}

	int32 MorphTargetIndex = 0;
	bool bCheckOnly = false;
	for (TSharedPtr<FJsonValue> JsonPrimitive : *JsonPrimitives)
	{
		TSharedPtr<FJsonObject> JsonPrimitiveObject = JsonPrimitive->AsObject();
		if (!JsonPrimitiveObject)
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* JsonTargetsArray;
		if (!JsonPrimitiveObject->TryGetArrayField(TEXT("targets"), JsonTargetsArray))
		{
			AddError("GetMorphTargetNames()", "No MorphTarget defined in the asset.");
			return false;
		}

		// check only ? (all primitives must have the same number of morph targets)
		if (bCheckOnly)
		{
			if (JsonTargetsArray->Num() != MorphTargetNames.Num())
			{
				AddError("GetMorphTargetNames()", FString::Printf(TEXT("Invalid number of morph targets: %d, expected %d"), JsonTargetsArray->Num(), MorphTargetNames.Num()));
			}
			continue;
		}

		for (int32 MorphIndex = 0; MorphIndex < JsonTargetsArray->Num(); MorphIndex++)
		{
			FName MorphTargetName = FName(FString::Printf(TEXT("MorphTarget_%d"), MorphTargetIndex++));
			MorphTargetNames.Add(MorphTargetName);
		}

		bCheckOnly = true;
	}

	// eventually cleanup names using targetNames extras
	const TSharedPtr<FJsonObject>* JsonExtrasObject;
	if (JsonMeshObject->TryGetObjectField(TEXT("extras"), JsonExtrasObject))
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonTargetNamesArray;
		if ((*JsonExtrasObject)->TryGetArrayField(TEXT("targetNames"), JsonTargetNamesArray))
		{
			for (int32 TargetNameIndex = 0; TargetNameIndex < JsonTargetNamesArray->Num(); TargetNameIndex++)
			{
				if (MorphTargetNames.IsValidIndex(TargetNameIndex))
				{
					MorphTargetNames[TargetNameIndex] = FName((*JsonTargetNamesArray)[TargetNameIndex]->AsString());
				}
			}
		}
	}

	return true;
}

void FglTFRuntimeArchiveZip::SetPassword(const FString& EncryptionKey)
{
	Password.Empty();
#if ENGINE_MAJOR_VERSION >= 5
	auto UTF8Conversion = StringCast<UTF8CHAR>(*EncryptionKey);
#else
	auto UTF8Conversion = StringCast<char>(*EncryptionKey);
#endif
	Password.Append(reinterpret_cast<const uint8*>(UTF8Conversion.Get()), UTF8Conversion.Length());
}

bool FglTFRuntimeArchiveZip::FromData(const uint8* DataPtr, const int64 DataNum)
{
	Data.Append(DataPtr, DataNum);

	// step0: retrieve the trailer magic
	TArray<uint8> Magic;
	bool bIndexFound = false;
	uint64 Index = 0;
	for (Index = Data.Num() - 1; Index >= 0; Index--)
	{
		Magic.Insert(Data[Index], 0);
		if (Magic.Num() == 4)
		{
			if (Magic[0] == 0x50 && Magic[1] == 0x4b && Magic[2] == 0x05 && Magic[3] == 0x06)
			{
				bIndexFound = true;
				break;
			}
			Magic.Pop();
		}
	}

	if (!bIndexFound)
	{
		return false;
	}

	uint16 DiskEntries = 0;
	uint16 TotalEntries = 0;
	uint32 CentralDirectorySize = 0;
	uint32 CentralDirectoryOffset = 0;
	uint16 CommentLen = 0;

	constexpr uint64 TrailerMinSize = 22;
	constexpr uint64 CentralDirectoryMinSize = 46;

	if (Index + TrailerMinSize > Data.Num())
	{
		return false;
	}

	// skip signature and disk data
	Data.Seek(Index + 8);
	Data << DiskEntries;
	Data << TotalEntries;
	Data << CentralDirectorySize;
	Data << CentralDirectoryOffset;
	Data << CommentLen;

	uint16 DirectoryEntries = FMath::Min(DiskEntries, TotalEntries);

	for (uint16 DirectoryIndex = 0; DirectoryIndex < DirectoryEntries; DirectoryIndex++)
	{
		if (CentralDirectoryOffset + CentralDirectoryMinSize > Data.Num())
		{
			return false;
		}

		uint16 FilenameLen = 0;
		uint16 ExtraFieldLen = 0;
		uint16 EntryCommentLen = 0;
		uint32 EntryOffset = 0;

		// seek to FilenameLen
		Data.Seek(CentralDirectoryOffset + 28);
		Data << FilenameLen;
		Data << ExtraFieldLen;
		Data << EntryCommentLen;
		// seek to EntryOffset
		Data.Seek(CentralDirectoryOffset + 42);
		Data << EntryOffset;

		if (CentralDirectoryOffset + CentralDirectoryMinSize + FilenameLen + ExtraFieldLen + EntryCommentLen > Data.Num())
		{
			return false;
		}

		TArray64<uint8> FilenameBytes;
		FilenameBytes.Append(Data.GetData() + CentralDirectoryOffset + CentralDirectoryMinSize, FilenameLen);
		FilenameBytes.Add(0);

		FString Filename = FString(UTF8_TO_TCHAR(FilenameBytes.GetData()));

		OffsetsMap.Add(Filename, EntryOffset);

		CentralDirectoryOffset += CentralDirectoryMinSize + FilenameLen + ExtraFieldLen + EntryCommentLen;
	}

	return true;
}

bool FglTFRuntimeArchiveZip::GetFileContent(const FString& Filename, TArray64<uint8>& OutData)
{
	uint32* Offset = OffsetsMap.Find(Filename);
	if (!Offset)
	{
		return false;
	}

	constexpr uint64 LocalEntryMinSize = 30;

	if (*Offset + LocalEntryMinSize > Data.Num())
	{
		return false;
	}

	uint16 Flags = 0;
	uint16 Compression = 0;
	uint32 CompressedSize;
	uint32 UncompressedSize = 0;
	uint16 FilenameLen = 0;
	uint16 ExtraFieldLen = 0;

	// seek to Compression
	Data.Seek(*Offset + 6);
	Data << Flags;
	Data << Compression;
	// seek to CompressedSize
	Data.Seek(*Offset + 18);
	Data << CompressedSize;
	Data << UncompressedSize;
	Data << FilenameLen;
	Data << ExtraFieldLen;

	if (*Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen + CompressedSize > Data.Num())
	{
		return false;
	}

	const uint8* CompressedData = Data.GetData() + *Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen;

	// encrypted ?
	TArray64<uint8> DecryptedData;
	if (Flags & 1 && Password.Num() > 0)
	{
		if (*Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen + CompressedSize + 12 > Data.Num())
		{
			return false;
		}
		DecryptedData.AddUninitialized(CompressedSize + 12);

		uint32 Key0 = 305419896;
		uint32 Key1 = 591751049;
		uint32 Key2 = 878082192;

		auto Crc32 = [](const uint8 Byte, const uint32 Crc) -> uint32
			{
				return (Crc >> 8) ^ FCrc::CRCTablesSB8[0][(Crc ^ Byte) & 0xFF];
			};

		auto UpdateKeys = [&Key0, &Key1, &Key2, &Crc32](const uint8 Byte)
			{
				Key0 = Crc32(Byte, Key0);
				Key1 = Key1 + (Key0 & 0xFF);
				Key1 = Key1 * 134775813 + 1;
				Key2 = Crc32(Key1 >> 24, Key2);
			};

		for (const uint8& Byte : Password)
		{
			UpdateKeys(Byte);
		}

		for (int64 EncryptedIndex = 0; EncryptedIndex < CompressedSize + 12; EncryptedIndex++)
		{
			const uint16 Temp = Key2 | 2;
			DecryptedData[EncryptedIndex] = CompressedData[EncryptedIndex] ^ ((Temp * (Temp ^ 1)) >> 8);
			UpdateKeys(DecryptedData[EncryptedIndex]);
		}

		CompressedData = DecryptedData.GetData() + 12;
	}

	if (Compression == 8)
	{
		OutData.AddUninitialized(UncompressedSize);
		if (!FCompression::UncompressMemory(NAME_Zlib, OutData.GetData(), UncompressedSize, CompressedData, CompressedSize, COMPRESS_NoFlags, -15))
		{
			return false;
		}
	}
	else if (Compression == 0 && CompressedSize == UncompressedSize)
	{
		OutData.Append(CompressedData, UncompressedSize);
	}
	else
	{
		return false;
	}

	return true;
}

bool FglTFRuntimeArchive::FileExists(const FString& Filename) const
{
	return OffsetsMap.Contains(Filename);
}

FString FglTFRuntimeArchive::GetFirstFilenameByExtension(const FString& Extension) const
{
	for (const TPair<FString, uint32>& Pair : OffsetsMap)
	{
		if (Pair.Key.EndsWith(Extension, ESearchCase::IgnoreCase))
		{
			return Pair.Key;
		}
	}

	return "";
}

bool FglTFRuntimeParser::GetJsonObjectBytes(TSharedRef<FJsonObject> JsonObject, TArray64<uint8>& Bytes)
{
	FString Uri;
	if (JsonObject->TryGetStringField(TEXT("uri"), Uri))
	{
		// check it is a valid base64 data uri
		if (Uri.StartsWith("data:"))
		{
			if (!ParseBase64Uri(Uri, Bytes))
			{
				return false;
			}
		}
		else if (Uri.StartsWith("http://") || Uri.StartsWith("https://"))
		{
			AddError("GetJsonObjectBytes()", FString::Printf(TEXT("Unable to open from external url %s (feature not supported)"), *Uri));
			return false;
		}
		else
		{
			bool bFound = false;
			if (Archive)
			{
				if (Archive->GetFileContent(Uri, Bytes))
				{
					bFound = true;
				}
			}

			if (!bFound && !BaseDirectory.IsEmpty())
			{
				FString DecodedUri = Uri;
				// this is a bit annoying (and very hacky) but we need to support % in filesystem names...
				if (DecodedUri.Contains("%") && !FPaths::FileExists(FPaths::Combine(BaseDirectory, DecodedUri)))
				{
					DecodedUri = FGenericPlatformHttp::UrlDecode(DecodedUri);
				}
				if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(BaseDirectory, DecodedUri)))
				{
					AddError("GetJsonObjectBytes()", FString::Printf(TEXT("Unable to load bytes from uri %s"), *Uri));
					return false;
				}
				bFound = true;
			}

			if (!bFound)
			{
				AddError("GetJsonObjectBytes()", FString::Printf(TEXT("Unable to open uri %s, you may want to enable external files loading..."), *Uri));
				return false;
			}
		}
	}
	else
	{
		int64 BufferViewIndex;
		if (JsonObject->TryGetNumberField(TEXT("bufferView"), BufferViewIndex))
		{
			int64 Stride;
			FglTFRuntimeBlob Blob;
			if (!GetBufferView(BufferViewIndex, Blob, Stride))
			{
				AddError("GetJsonObjectBytes()", FString::Printf(TEXT("Unable to get bufferView: %d"), BufferViewIndex));
				return false;
			}
			Bytes.Append(Blob.Data, Blob.Num);
		}
	}

	return Bytes.Num() > 0;
}

FVector FglTFRuntimeParser::ComputeTangentY(const FVector Normal, const FVector TangetX)
{
	float Determinant = GetBasisDeterminantSign(Normal.GetSafeNormal(),
		(Normal ^ TangetX).GetSafeNormal(),
		Normal.GetSafeNormal());

	return (Normal ^ TangetX) * Determinant;
}

FVector FglTFRuntimeParser::ComputeTangentYWithW(const FVector Normal, const FVector TangetX, const float W)
{
	return (Normal ^ TangetX) * W;
}

TArray<TSharedRef<FJsonObject>> FglTFRuntimeParser::GetMeshes() const
{
	TArray<TSharedRef<FJsonObject>> Meshes;

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Root->TryGetArrayField(TEXT("meshes"), JsonArray))
	{
		for (TSharedPtr<FJsonValue> JsonValue : *JsonArray)
		{
			const TSharedPtr<FJsonObject>* JsonObject;
			if (JsonValue->TryGetObject(JsonObject))
			{
				Meshes.Add(JsonObject->ToSharedRef());
			}
		}
	}

	return Meshes;
}

TArray<TSharedRef<FJsonObject>> FglTFRuntimeParser::GetMeshPrimitives(TSharedRef<FJsonObject> Mesh) const
{
	TArray<TSharedRef<FJsonObject>> Primitives;

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Mesh->TryGetArrayField(TEXT("primitives"), JsonArray))
	{
		for (TSharedPtr<FJsonValue> JsonValue : *JsonArray)
		{
			const TSharedPtr<FJsonObject>* JsonObject;
			if (JsonValue->TryGetObject(JsonObject))
			{
				Primitives.Add(JsonObject->ToSharedRef());
			}
		}
	}

	return Primitives;
}

TArray<TSharedRef<FJsonObject>> FglTFRuntimeParser::GetMaterials() const
{
	TArray<TSharedRef<FJsonObject>> Materials;

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Root->TryGetArrayField(TEXT("materials"), JsonArray))
	{
		for (TSharedPtr<FJsonValue> JsonValue : *JsonArray)
		{
			const TSharedPtr<FJsonObject>* JsonObject;
			if (JsonValue->TryGetObject(JsonObject))
			{
				Materials.Add(JsonObject->ToSharedRef());
			}
		}
	}

	return Materials;
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetJsonObjectExtras(TSharedRef<FJsonObject> JsonObject) const
{
	return GetJsonObjectFromObject(JsonObject, TEXT("extras"));
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetJsonObjectFromObject(TSharedRef<FJsonObject> JsonObject, const FString& Name) const
{
	const TSharedPtr<FJsonObject>* ExtrasObject = nullptr;
	if (JsonObject->TryGetObjectField(Name, ExtrasObject))
	{
		return *ExtrasObject;
	}
	return nullptr;
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetJsonObjectExtension(TSharedRef<FJsonObject> JsonObject, const FString& Name) const
{
	const TSharedPtr<FJsonObject>* ExtensionsObject = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("extensions"), ExtensionsObject))
	{
		const TSharedPtr<FJsonObject>* RequestedExtensionObject = nullptr;
		if ((*ExtensionsObject)->TryGetObjectField(Name, RequestedExtensionObject))
		{
			return *RequestedExtensionObject;
		}
	}
	return nullptr;
}

int64 FglTFRuntimeParser::GetJsonObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& Name) const
{
	int64 Index;
	if (JsonObject->TryGetNumberField(Name, Index))
	{
		return Index;
	}
	return INDEX_NONE;
}

const FglTFRuntimeBlob* FglTFRuntimeParser::GetAdditionalBufferView(const int64 Index, const FString& Name) const
{
	if (Index <= INDEX_NONE)
	{
		return nullptr;
	}

	const TMap<FString, FglTFRuntimeBlob>* Value = AdditionalBufferViewsCache.Find(Index);
	if (!Value)
	{
		return nullptr;
	}

	if (!Value->Contains(Name))
	{
		return nullptr;
	}

	const FglTFRuntimeBlob& Blob = (*Value)[Name];

	return &Blob;
}

void FglTFRuntimeParser::AddAdditionalBufferView(const int64 Index, const FString& Name, const FglTFRuntimeBlob& Blob)
{
	if (Index <= INDEX_NONE)
	{
		return;
	}

	if (!AdditionalBufferViewsCache.Contains(Index))
	{
		AdditionalBufferViewsCache.Add(Index);
	}

	if (!AdditionalBufferViewsCache[Index].Contains(Name))
	{
		AdditionalBufferViewsCache[Index].Add(Name, Blob);
	}
	else
	{
		AdditionalBufferViewsCache[Index][Name] = Blob;
	}

}

bool FglTFRuntimeParser::GetNumberFromExtras(const FString& Key, float& Value) const
{
	TSharedPtr<FJsonObject> JsonExtras = GetJsonObjectExtras(Root);
	if (!JsonExtras)
	{
		return false;
	}

	double DoubleValue = 0;

	if (JsonExtras->TryGetNumberField(Key, DoubleValue))
	{
		Value = DoubleValue;
		return true;
	}

	return false;
}

bool FglTFRuntimeParser::GetStringFromExtras(const FString& Key, FString& Value) const
{
	TSharedPtr<FJsonObject> JsonExtras = GetJsonObjectExtras(Root);
	if (!JsonExtras)
	{
		return false;
	}

	return JsonExtras->TryGetStringField(Key, Value);
}

bool FglTFRuntimeParser::GetBooleanFromExtras(const FString& Key, bool& Value) const
{
	TSharedPtr<FJsonObject> JsonExtras = GetJsonObjectExtras(Root);
	if (!JsonExtras)
	{
		return false;
	}

	return JsonExtras->TryGetBoolField(Key, Value);
}

bool FglTFRuntimeParser::GetStringMapFromExtras(const FString& Key, TMap<FString, FString>& StringMap) const
{
	TSharedPtr<FJsonObject> JsonExtras = GetJsonObjectExtras(Root);
	if (!JsonExtras)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* JsonExtraObject = nullptr;
	if (!JsonExtras->TryGetObjectField(Key, JsonExtraObject))
	{
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*JsonExtraObject)->Values)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}

		FString Value;
		if (!Pair.Value->TryGetString(Value))
		{
			continue;
		}

		StringMap.Add(Pair.Key, Value);
	}

	return true;
}

bool FglTFRuntimeParser::GetStringArrayFromExtras(const FString& Key, TArray<FString>& StringArray) const
{
	TSharedPtr<FJsonObject> JsonExtras = GetJsonObjectExtras(Root);
	if (!JsonExtras)
	{
		return false;
	}

	return JsonExtras->TryGetStringArrayField(Key, StringArray);
}

bool FglTFRuntimeParser::GetNumberArrayFromExtras(const FString& Key, TArray<float>& NumberArray) const
{
	TSharedPtr<FJsonObject> JsonExtras = GetJsonObjectExtras(Root);
	if (!JsonExtras)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!JsonExtras->TryGetArrayField(Key, JsonArray))
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& JsonItem : *JsonArray)
	{
		double Value = 0;
		if (!JsonItem->TryGetNumber(Value))
		{
			return false;
		}
		NumberArray.Add(Value);
	}

	return true;
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetNodeExtensionObject(const int32 NodeIndex, const FString& ExtensionName)
{
	TSharedPtr<FJsonObject> JsonNodeObject = GetJsonObjectFromRootIndex("nodes", NodeIndex);
	if (!JsonNodeObject)
	{
		return nullptr;
	}

	return GetJsonObjectExtension(JsonNodeObject.ToSharedRef(), ExtensionName);
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetNodeObject(const int32 NodeIndex)
{
	return GetJsonObjectFromRootIndex("nodes", NodeIndex);
}

bool FglTFRuntimeParser::DecompressMeshOptimizer(const FglTFRuntimeBlob& Blob, const int64 Stride, const int64 Elements, const FString& Mode, const FString& Filter, TArray64<uint8>& UncompressedBytes)
{
	auto DecodeZigZag = [](const uint8 V)
		{
			return ((V & 1) != 0) ? ~(V >> 1) : (V >> 1);
		};

	// refactored in april 2024 to be more compliant with https://www.npmjs.com/package/meshoptimize
	if (Mode == "ATTRIBUTES" && Blob.Num > 32 && Blob.Data[0] == 0xa0)
	{
		const int64 MaxBlockElements = FMath::Min<int64>((8192 / Stride) & ~15, 256);
		TArray<uint8, TInlineAllocator<16>> Deltas;
		Deltas.AddZeroed(16);

		TArray<uint8> TempData;
		TempData.Append(Blob.Data + Blob.Num - Stride, Stride);

		int64 Offset = 1;
		const int64 Limit = Blob.Num - Stride;

		// preallocated output
		UncompressedBytes.AddUninitialized(Elements * Stride);

		for (int64 ElementIndex = 0; ElementIndex < Elements; ElementIndex += MaxBlockElements)
		{
			const int64 BlockElements = FMath::Min<int64>(Elements - ElementIndex, MaxBlockElements);

			const int64 GroupCount = ((BlockElements + 0x0F) & ~0x0F) >> 4;

			const int64 NumberOfHeaderBytes = ((GroupCount + 0x03) & ~0x03) >> 2;

			for (int64 ElementByteIndex = 0; ElementByteIndex < Stride; ElementByteIndex++)
			{

				if (Offset + NumberOfHeaderBytes > Limit)
				{
					return false;
				}

				int64 HeaderOffset = Offset;
				Offset += NumberOfHeaderBytes;

				for (int64 GroupIndex = 0; GroupIndex < GroupCount; GroupIndex++)
				{
					const uint8 ModeValue = (Blob.Data[HeaderOffset] >> ((GroupIndex & 0x03) << 1)) & 0x03;
					if ((GroupIndex & 0x03) == 0x03)
					{
						HeaderOffset++;
					}

					if (ModeValue == 0)
					{
						FMemory::Memset(Deltas.GetData(), 0, 16);
					}
					else if (ModeValue == 1)
					{
						if (Offset + 4 > Limit)
						{
							return false;
						}

						const int64 BaseOffset = Offset;
						Offset += 4;

						for (int64 Index = 0; Index < 16; Index++)
						{
							const int64 Shift = (6 - ((Index & 0x03) << 1));
							uint8 Delta = (Blob.Data[BaseOffset + (Index >> 2)] >> Shift) & 0x03;
							if (Delta == 3)
							{
								if (Offset + 1 > Limit)
								{
									return false;
								}
								Delta = Blob.Data[Offset++];
							}
							Deltas[Index] = Delta;
						}
					}
					else if (ModeValue == 2)
					{
						if (Offset + 8 > Limit)
						{
							return false;
						}

						const int64 BaseOffset = Offset;
						Offset += 8;

						for (int64 Index = 0; Index < 16; Index++)
						{
							const int64 Shift = (Index & 0x01) ? 0 : 4;
							uint8 Delta = (Blob.Data[BaseOffset + (Index >> 1)] >> Shift) & 0x0f;
							if (Delta == 0xf)
							{
								if (Offset + 1 > Limit)
								{
									return false;
								}
								Delta = Blob.Data[Offset++];
							}
							Deltas[Index] = Delta;
						}
					}
					else if (ModeValue == 3)
					{
						if (Offset + 16 > Limit)
						{
							return false;
						}
						FMemory::Memcpy(Deltas.GetData(), Blob.Data + Offset, 16);
						Offset += 16;
					}

					const int64 DestinationElementGroup = ElementIndex + (GroupIndex << 4);

					for (int64 Index = 0; Index < 16; Index++)
					{
						const int64 DestinationElementIndex = DestinationElementGroup + Index;
						if (DestinationElementIndex >= Elements)
						{
							break;
						}

						const uint8 Delta = DecodeZigZag(Deltas[Index]);
						TempData[ElementByteIndex] += Delta;
						UncompressedBytes[DestinationElementIndex * Stride + ElementByteIndex] = TempData[ElementByteIndex];
					}

				}
			}
		}
	}
	else if (Mode == "TRIANGLES" && Blob.Num >= 17 && Blob.Data[0] == 0xe1 && (Stride == 2 || Stride == 4) && ((Elements % 3) == 0))
	{
		TArray<uint8> CodeAux;
		const int64 Limit = Blob.Num - 16;
		CodeAux.Append(Blob.Data + Limit, 16);

		uint32 Next = 0;
		uint32 Last = 0;
		TArray<TPair<uint32, uint32>> EdgeFifo;
		TArray<uint32> VertexFifo;

		int64 Offset = 1;
		const uint32 TrianglesNum = Elements / 3;
		int64 DataOffset = Offset + TrianglesNum;
		int64 TriangleOffset = 0;

		UncompressedBytes.AddUninitialized(Elements * Stride);

		auto EmitTriangle = [Stride, &TriangleOffset, &UncompressedBytes](const uint32 A, const uint32 B, const uint32 C)
			{
				if (Stride == 2)
				{
					const uint16 AShort = A;
					const uint16 BShort = B;
					const uint16 CShort = C;
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&AShort)[0];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&AShort)[1];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&BShort)[0];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&BShort)[1];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&CShort)[0];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&CShort)[1];
				}
				else
				{
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&A)[0];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&A)[1];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&A)[2];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&A)[3];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&B)[0];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&B)[1];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&B)[2];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&B)[3];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&C)[0];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&C)[1];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&C)[2];
					UncompressedBytes[TriangleOffset++] = reinterpret_cast<const uint8*>(&C)[3];
				}
			};

		auto DecodeIndex = [&Blob, &DataOffset, &Last, Limit]() -> bool
			{
				uint32 V = 0;
				for (int32 Shift = 0; ; Shift += 7)
				{
					if (DataOffset >= Limit)
					{
						return false;
					}

					const uint32 Byte = Blob.Data[DataOffset++];
					V |= (Byte & 0x7F) << Shift;

					if (Byte < 0x80)
					{
						break;
					}
				}

				int32 Delta = ((V & 1) != 0) ? ~(V >> 1) : (V >> 1);

				Last += Delta;
				return true;
			};

		for (uint32 TriangleIndex = 0; TriangleIndex < TrianglesNum; TriangleIndex++)
		{
			if (Offset >= Limit)
			{
				return false;
			}
			uint8 Code = Blob.Data[Offset++];
			uint8 NibbleLeft = Code >> 4;
			uint8 NibbleRight = Code & 0x0f;

			if (NibbleLeft < 0xf && NibbleRight == 0) // 0xX0
			{
				if (NibbleLeft >= EdgeFifo.Num())
				{
					return false;
				}
				const TPair<uint32, uint32> AB = EdgeFifo[NibbleLeft];
				const uint32 C = Next++;

				EdgeFifo.Insert(TPair<uint32, uint32>(C, AB.Value), 0); // push CB
				EdgeFifo.Insert(TPair<uint32, uint32>(AB.Key, C), 0); // push AC
				VertexFifo.Insert(C, 0);

				EmitTriangle(AB.Key, AB.Value, C);
			}
			else if (NibbleLeft < 0xf && NibbleRight > 0 && NibbleRight < 0x0d) // 0xXY
			{
				if (NibbleLeft >= EdgeFifo.Num())
				{
					return false;
				}
				const TPair<uint32, uint32> AB = EdgeFifo[NibbleLeft];

				if (NibbleRight >= VertexFifo.Num())
				{
					return false;
				}

				const uint32 C = VertexFifo[NibbleRight];
				EdgeFifo.Insert(TPair<uint32, uint32>(C, AB.Value), 0); // push CB
				EdgeFifo.Insert(TPair<uint32, uint32>(AB.Key, C), 0); // push AC

				EmitTriangle(AB.Key, AB.Value, C);
			}
			else if (NibbleLeft < 0xf && NibbleRight == 0x0d) // 0xXd
			{
				if (NibbleLeft >= EdgeFifo.Num())
				{
					return false;
				}
				const TPair<uint32, uint32> AB = EdgeFifo[NibbleLeft];

				const uint32 C = Last - 1;
				Last = C;

				EdgeFifo.Insert(TPair<uint32, uint32>(C, AB.Value), 0); // push CB
				EdgeFifo.Insert(TPair<uint32, uint32>(AB.Key, C), 0); // push AC
				VertexFifo.Insert(C, 0);

				EmitTriangle(AB.Key, AB.Value, C);
			}
			else if (NibbleLeft < 0xf && NibbleRight == 0x0e) // 0xXe
			{
				if (NibbleLeft >= EdgeFifo.Num())
				{
					return false;
				}
				const TPair<uint32, uint32> AB = EdgeFifo[NibbleLeft];

				const uint32 C = Last + 1;
				Last = C;

				EdgeFifo.Insert(TPair<uint32, uint32>(C, AB.Value), 0); // push CB
				EdgeFifo.Insert(TPair<uint32, uint32>(AB.Key, C), 0); // push AC
				VertexFifo.Insert(C, 0);

				EmitTriangle(AB.Key, AB.Value, C);
			}
			else if (NibbleLeft < 0xf && NibbleRight == 0x0f) // 0xXf
			{
				if (NibbleLeft >= EdgeFifo.Num())
				{
					return false;
				}
				const TPair<uint32, uint32> AB = EdgeFifo[NibbleLeft];

				if (!DecodeIndex())
				{
					return false;
				}

				const uint32 C = Last;

				EdgeFifo.Insert(TPair<uint32, uint32>(C, AB.Value), 0); // push CB
				EdgeFifo.Insert(TPair<uint32, uint32>(AB.Key, C), 0); // push AC
				VertexFifo.Insert(C, 0);

				EmitTriangle(AB.Key, AB.Value, C);
			}
			else if (NibbleLeft == 0xf && NibbleRight < 0xe) // 0xfY
			{
				const uint8 ZW = CodeAux[NibbleRight];
				const uint8 Z = ZW >> 4;
				const uint8 W = ZW & 0x0f;

				const uint32 A = Next++;
				uint32 B = 0;
				uint32 C = 0;

				if (Z == 0)
				{
					B = Next++;
				}
				else
				{
					if (Z - 1 >= VertexFifo.Num())
					{
						return false;
					}
					B = VertexFifo[Z - 1];
				}

				if (W == 0)
				{
					C = Next++;
				}
				else
				{
					if (W - 1 >= VertexFifo.Num())
					{
						return false;
					}
					C = VertexFifo[W - 1];
				}

				EdgeFifo.Insert(TPair<uint32, uint32>(B, A), 0); // push BA
				EdgeFifo.Insert(TPair<uint32, uint32>(C, B), 0); // push CB
				EdgeFifo.Insert(TPair<uint32, uint32>(A, C), 0); // push AC
				VertexFifo.Insert(A, 0);
				if (Z == 0)
				{
					VertexFifo.Insert(B, 0);
				}
				if (W == 0)
				{
					VertexFifo.Insert(C, 0);
				}

				EmitTriangle(A, B, C);
			}
			else if (Code == 0xfe || Code == 0xff) // 0xfe - 0xff
			{
				if (DataOffset >= Limit)
				{
					return false;
				}

				uint8 ZW = Blob.Data[DataOffset++];
				uint8 Z = ZW >> 4;
				uint8 W = ZW & 0x0f;
				if (ZW == 0)
				{
					Next = 0;
				}

				uint32 A = 0;
				if (Code == 0xfe)
				{
					A = Next++;
				}
				else
				{
					if (!DecodeIndex())
					{
						return false;
					}
					A = Last;
				}

				uint32 B = 0;
				if (Z == 0)
				{
					B = Next++;
				}
				else if (Z < 0xf)
				{
					if (Z - 1 >= VertexFifo.Num())
					{
						return false;
					}
					B = VertexFifo[Z - 1];
				}
				else
				{
					if (!DecodeIndex())
					{
						return false;
					}
					B = Last;
				}

				uint32 C = 0;
				if (W == 0)
				{
					C = Next++;
				}
				else if (W < 0xf)
				{
					if (W - 1 >= VertexFifo.Num())
					{
						return false;
					}
					C = VertexFifo[W - 1];
				}
				else
				{
					if (!DecodeIndex())
					{
						return false;
					}
					C = Last;
				}

				EdgeFifo.Insert(TPair<uint32, uint32>(B, A), 0); // push BA
				EdgeFifo.Insert(TPair<uint32, uint32>(C, B), 0); // push CB
				EdgeFifo.Insert(TPair<uint32, uint32>(A, C), 0); // push AC
				VertexFifo.Insert(A, 0);
				if (Z == 0 || Z == 0xf)
				{
					VertexFifo.Insert(B, 0);
				}
				if (W == 0 || W == 0xf)
				{
					VertexFifo.Insert(C, 0);
				}

				EmitTriangle(A, B, C);
			}
		}
	}
	else
	{
		return false;
	}

	if (UncompressedBytes.Num() > 0)
	{
		if (Filter == "OCTAHEDRAL" && (Stride == 4 || Stride == 8))
		{
			if (Stride == 4)
			{
				int8* Data = reinterpret_cast<int8*>(UncompressedBytes.GetData());
				const int64 MaxInt = 127;
				for (int64 Index = 0; Index < 4 * Elements; Index += 4)
				{
					float X = Data[Index];
					float Y = Data[Index + 1];
					float One = Data[Index + 2];
					X /= One;
					Y /= One;
					const float Z = 1.0 - FMath::Abs(X) - FMath::Abs(Y);
					const float T = FMath::Max(-Z, 0.0f);
					X -= (X >= 0) ? T : -T;
					Y -= (Y >= 0) ? T : -T;
					const float H = MaxInt / FMath::Sqrt(X * X + Y * Y + Z * Z);
					Data[Index + 0] = FMath::RoundToInt(X * H);
					Data[Index + 1] = FMath::RoundToInt(Y * H);
					Data[Index + 2] = FMath::RoundToInt(Z * H);
				}
			}
			else
			{
				int16* Data = reinterpret_cast<int16*>(UncompressedBytes.GetData());
				const int64 MaxInt = 32767;
				for (int64 Index = 0; Index < 4 * Elements; Index += 4)
				{
					float X = Data[Index];
					float Y = Data[Index + 1];
					float One = Data[Index + 2];
					X /= One;
					Y /= One;
					const float Z = 1.0 - FMath::Abs(X) - FMath::Abs(Y);
					const float T = FMath::Max(-Z, 0.0f);
					X -= (X >= 0) ? T : -T;
					Y -= (Y >= 0) ? T : -T;
					const float H = MaxInt / FMath::Sqrt(X * X + Y * Y + Z * Z);
					Data[Index + 0] = FMath::RoundToInt(X * H);
					Data[Index + 1] = FMath::RoundToInt(Y * H);
					Data[Index + 2] = FMath::RoundToInt(Z * H);
				}
			}
		}
		else if (Filter == "QUATERNION" && Stride == 8)
		{
			int16* Data = reinterpret_cast<int16*>(UncompressedBytes.GetData());

			const float Range = 1.0f / FMath::Sqrt(2.0f);

			for (int64 Offset = 0; Offset < Elements * 4; Offset += 4)
			{
				float One = Data[Offset + 3] | 3;

				float X = Data[Offset] / One * Range;
				float Y = Data[Offset + 1] / One * Range;
				float Z = Data[Offset + 2] / One * Range;

				float W = FMath::Sqrt(FMath::Max(0.0, 1.0 - X * X - Y * Y - Z * Z));

				int32 MaxComp = Data[Offset + 3] & 3;

				Data[Offset + ((MaxComp + 1) % 4)] = FMath::RoundToInt(X * 32767.0);
				Data[Offset + ((MaxComp + 2) % 4)] = FMath::RoundToInt(Y * 32767.0);
				Data[Offset + ((MaxComp + 3) % 4)] = FMath::RoundToInt(Z * 32767.0);
				Data[Offset + ((MaxComp + 0) % 4)] = FMath::RoundToInt(W * 32767.0);
			}
		}
		else if (Filter == "EXPONENTIAL" && (Stride % 4) == 0)
		{
			int32* Data = reinterpret_cast<int32*>(UncompressedBytes.GetData());
			float* Dest = reinterpret_cast<float*>(UncompressedBytes.GetData());
			for (int64 Offset = 0; Offset < UncompressedBytes.Num() / 4; Offset++)
			{
				int32 E = Data[Offset] >> 24;
				int32 M = (Data[Offset] << 8) >> 8;
				Dest[Offset] = FMath::Pow(2.0f, E) * M;
			}
		}
		else if (Filter != "" && Filter != "NONE")
		{
			AddError("DecompressMeshOptimizer()", "Unsupported Filter");
			return false;
		}
	}

	return true;
}

FTransform FglTFRuntimeParser::GetParentNodeWorldTransform(const FglTFRuntimeNode& Node)
{
	FTransform WorldTransform = FTransform::Identity;
	int32 ParentIndex = Node.ParentIndex;
	while (ParentIndex > INDEX_NONE)
	{
		FglTFRuntimeNode ParentNode;
		if (!LoadNode(ParentIndex, ParentNode))
		{
			break;
		}

		WorldTransform = ParentNode.Transform * WorldTransform;
		ParentIndex = ParentNode.ParentIndex;
	}

	return WorldTransform;
}

FTransform FglTFRuntimeParser::GetNodeWorldTransform(const FglTFRuntimeNode& Node)
{
	return GetParentNodeWorldTransform(Node) * Node.Transform;
}

FString FglTFRuntimeParser::ToJsonString() const
{
	FString Json;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(MakeShared<FJsonValueObject>(GetJsonRoot()), "", JsonWriter);

	return Json;
}

int32 FglTFRuntimeParser::GetNodeDistance(const FglTFRuntimeNode& Node, const int32 Ancestor)
{
	if (Node.Index == Ancestor)
	{
		return 0;
	}

	int32 Distance = 0;

	FglTFRuntimeNode CurrentNode = Node;
	while (CurrentNode.ParentIndex > INDEX_NONE)
	{
		Distance++;
		if (!LoadNode(CurrentNode.ParentIndex, CurrentNode))
		{
			return -1;
		}

		if (CurrentNode.Index == Ancestor)
		{
			return Distance;
		}
	}

	return -1;
}

FString FglTFRuntimeParser::GetVersion() const
{
	TSharedPtr<FJsonObject> Asset = GetJsonObjectFromObject(GetJsonRoot().ToSharedRef(), "asset");
	if (!Asset)
	{
		return "";
	}

	return GetJsonObjectString(Asset.ToSharedRef(), "version", "");
}

FString FglTFRuntimeParser::GetGenerator() const
{
	TSharedPtr<FJsonObject> Asset = GetJsonObjectFromObject(GetJsonRoot().ToSharedRef(), "asset");
	if (!Asset)
	{
		return "";
	}

	return GetJsonObjectString(Asset.ToSharedRef(), "generator", "");
}

bool FglTFRuntimeParser::IsArchive() const
{
	return Archive.IsValid();
}

TArray<FString> FglTFRuntimeParser::GetArchiveItems() const
{
	TArray<FString> Items;
	if (Archive.IsValid())
	{
		Archive->GetItems(Items);
	}
	return Items;
}

bool FglTFRuntimeParser::GetBlobByName(const FString& Name, TArray64<uint8>& Blob) const
{
	if (!IsArchive())
	{
		return false;
	}

	return Archive->GetFileContent(Name, Blob);
}

void FglTFRuntimeParser::LoadMeshAsRuntimeLODAsync(const int32 MeshIndex, const FglTFRuntimeMeshLODAsync& AsyncCallback, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		AsyncCallback.ExecuteIfBound(false, FglTFRuntimeMeshLOD());
	}

	Async(EAsyncExecution::Thread, [this, JsonMeshObject, MaterialsConfig, AsyncCallback]()
		{
			FglTFRuntimeMeshLOD* LOD;
			bool bSuccess = LoadMeshIntoMeshLOD(JsonMeshObject.ToSharedRef(), LOD, MaterialsConfig);
			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([bSuccess, LOD, AsyncCallback]()
				{
					AsyncCallback.ExecuteIfBound(bSuccess, bSuccess ? *LOD : FglTFRuntimeMeshLOD());
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		}

	);
}

bool FglTFRuntimeParser::LoadPathToBlob(const FString& Path, TArray64<uint8>& Blob)
{
	if (IsArchive())
	{
		return GetBlobByName(Path, Blob);
	}

	if (!BaseDirectory.IsEmpty())
	{
		return FFileHelper::LoadFileToArray(Blob, *FPaths::Combine(BaseDirectory, Path));
	}

	return false;
}

void FglTFRuntimeParser::SetDownloadTime(const float Value)
{
	DownloadTime = Value;
}

float FglTFRuntimeParser::GetDownloadTime() const
{
	return DownloadTime;
}

TArray<TSharedRef<FJsonObject>> FglTFRuntimeParser::GetAnimations() const
{
	TArray<TSharedRef<FJsonObject>> Animations;

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Root->TryGetArrayField(TEXT("animations"), JsonArray))
	{
		for (TSharedPtr<FJsonValue> JsonValue : *JsonArray)
		{
			const TSharedPtr<FJsonObject>* JsonObject;
			if (JsonValue->TryGetObject(JsonObject))
			{
				Animations.Add(JsonObject->ToSharedRef());
			}
		}
	}

	return Animations;
}

TArray<FString> FglTFRuntimeParser::GetAnimationsNames(const bool bIncludeUnnameds) const
{
	TArray<FString> Names;
	const TArray<TSharedRef<FJsonObject>> Animations = GetAnimations();
	for (int32 AnimationIndex = 0; AnimationIndex < Animations.Num(); AnimationIndex++)
	{
		const TSharedRef<FJsonObject>& Animation = Animations[AnimationIndex];
		FString Name;
		if (bIncludeUnnameds && (!Animation->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty()))
		{
			Name = FString::Printf(TEXT("Animation_%d"), AnimationIndex);
		}

		if (!Name.IsEmpty())
		{
			Names.Add(Name);
		}
	}
	return Names;
}

void FglTFRuntimeArchiveMap::FromMap(const TMap<FString, TArray64<uint8>> InMap)
{
	for (const TPair<FString, TArray64<uint8>>& Pair : InMap)
	{
		const int32 NewOffset = MapItems.Add(Pair.Value);
		OffsetsMap.Add(Pair.Key, NewOffset);
	}
}

bool FglTFRuntimeArchiveMap::GetFileContent(const FString& Filename, TArray64<uint8>& OutData)
{
	if (!OffsetsMap.Contains(Filename))
	{
		return false;
	}

	if (!MapItems.IsValidIndex(OffsetsMap[Filename]))
	{
		return false;
	}

	OutData = MapItems[OffsetsMap[Filename]];

	return true;
}

void FglTFRuntimeParser::FillAssetUserData(const int32 Index, IInterface_AssetUserData* InObject)
{
	for (TSubclassOf<UglTFRuntimeAssetUserData> AssetUserDataClass : AssetUserDataClasses)
	{
		if (AssetUserDataClass)
		{
			UglTFRuntimeAssetUserData* AssetUserData = NewObject<UglTFRuntimeAssetUserData>(InObject->_getUObject(), AssetUserDataClass, NAME_None, RF_Public);
			AssetUserData->SetParser(AsShared());
			AssetUserData->ReceiveFillAssetUserData(Index);
			InObject->AddAssetUserData(AssetUserData);
		}
	}
}