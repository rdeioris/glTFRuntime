// Copyright 2020-2024, Roberto De Ioris.
// OpenFlight (.flt) runtime ingestion support for glTFRuntime.

#include "glTFRuntimeFLTParser.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogglTFRuntimeFLT, Log, All);

namespace glTFRuntimeFLT
{
	// --- OpenFlight opcodes (subset) -------------------------------------------------------------
	enum class EOpcode : uint16
	{
		Header = 1,
		Group = 2,
		Object = 4,
		Face = 5,
		PushLevel = 10,
		PopLevel = 11,
		PushSubface = 19,
		PopSubface = 20,
		PushExtension = 21,
		PopExtension = 22,
		MorphVertexList = 89,
		VertexList = 72,
		LevelOfDetail = 73,
		ExternalReference = 63,
		Matrix = 49,
		LongID = 33,
		Mesh = 84,
		LocalVertexPool = 85,
		MeshPrimitive = 86,
		Comment = 31,
		TexturePalette = 64,
		VertexPalette = 67,
		VertexC = 68,    // color
		VertexCN = 69,   // color + normal
		VertexCNT = 70,  // color + normal + uv
		VertexCT = 71,   // color + uv
	};

	// --- Big-endian reading helpers --------------------------------------------------------------
	struct FByteReader
	{
		const uint8* Data;
		int64 Size;

		FByteReader(const uint8* InData, int64 InSize) : Data(InData), Size(InSize) {}

		FORCEINLINE bool InBounds(int64 Offset, int64 Count) const
		{
			return Offset >= 0 && Count >= 0 && (Offset + Count) <= Size;
		}

		uint16 U16(int64 Offset) const
		{
			if (!InBounds(Offset, 2)) return 0;
			return (uint16(Data[Offset]) << 8) | uint16(Data[Offset + 1]);
		}

		int16 I16(int64 Offset) const { return static_cast<int16>(U16(Offset)); }

		uint32 U32(int64 Offset) const
		{
			if (!InBounds(Offset, 4)) return 0;
			return (uint32(Data[Offset]) << 24) | (uint32(Data[Offset + 1]) << 16) |
				(uint32(Data[Offset + 2]) << 8) | uint32(Data[Offset + 3]);
		}

		int32 I32(int64 Offset) const { return static_cast<int32>(U32(Offset)); }

		float F32(int64 Offset) const
		{
			const uint32 Bits = U32(Offset);
			float Out;
			FMemory::Memcpy(&Out, &Bits, sizeof(float));
			return Out;
		}

		double F64(int64 Offset) const
		{
			if (!InBounds(Offset, 8)) return 0.0;
			const uint64 Hi = U32(Offset);
			const uint64 Lo = U32(Offset + 4);
			const uint64 Bits = (Hi << 32) | Lo;
			double Out;
			FMemory::Memcpy(&Out, &Bits, sizeof(double));
			return Out;
		}

		FString FixedString(int64 Offset, int64 Length) const
		{
			if (!InBounds(Offset, Length)) return FString();
			TArray<ANSICHAR> Chars;
			for (int64 i = 0; i < Length; ++i)
			{
				const ANSICHAR C = static_cast<ANSICHAR>(Data[Offset + i]);
				if (C == 0) break;
				Chars.Add(C);
			}
			Chars.Add(0);
			return FString(ANSI_TO_TCHAR(Chars.GetData()));
		}
	};

	// --- Intermediate geometry representation ----------------------------------------------------
	struct FVertex
	{
		double X = 0, Y = 0, Z = 0;     // position (database units)
		float NX = 0, NY = 0, NZ = 1;   // normal
		float U = 0, V = 0;             // uv
		bool bHasNormal = false;
		bool bHasUV = false;
	};

	// A primitive groups all triangles that share the same texture index.
	struct FPrimitiveBuilder
	{
		int32 TextureIndex = -1;
		TArray<FVertex> Vertices;       // de-duplicated per primitive
		TArray<uint32> Indices;
	};

	struct FMeshBuilder
	{
		FString Name;
		// Keyed by texture index so each texture becomes a glTF primitive (== material slot).
		TMap<int32, FPrimitiveBuilder> Primitives;

		FPrimitiveBuilder& GetPrimitive(int32 TextureIndex)
		{
			FPrimitiveBuilder* Found = Primitives.Find(TextureIndex);
			if (Found) return *Found;
			FPrimitiveBuilder& New = Primitives.Add(TextureIndex);
			New.TextureIndex = TextureIndex;
			return New;
		}

		bool IsEmpty() const
		{
			for (const TPair<int32, FPrimitiveBuilder>& Pair : Primitives)
			{
				if (Pair.Value.Indices.Num() > 0) return false;
			}
			return true;
		}
	};

	struct FTextureEntry
	{
		int32 Index = -1;
		FString Path;
	};

	// Header units -> meters scale factor (OpenFlight vertex unit code at header offset 120).
	static double UnitsToMeters(int32 UnitCode)
	{
		switch (UnitCode)
		{
		case 0: return 1.0;        // meters
		case 1: return 1000.0;     // kilometers
		case 4: return 0.3048;     // feet
		case 5: return 1609.344;   // miles (statute)
		case 8: return 0.0254;     // inches
		case 9: return 1852.0;     // nautical miles
		default: return 1.0;
		}
	}
}

using namespace glTFRuntimeFLT;

bool FglTFRuntimeFLTConverter::IsFLTData(const TArray<uint8>& Data)
{
	if (Data.Num() < 4)
	{
		return false;
	}
	const FByteReader Reader(Data.GetData(), Data.Num());
	// An OpenFlight database always begins with a Header record (opcode 1)
	// whose record length is at least 4 bytes.
	return Reader.U16(0) == static_cast<uint16>(EOpcode::Header) && Reader.U16(2) >= 4;
}

bool FglTFRuntimeFLTConverter::ConvertToGltf(
	const TArray<uint8>& FLTData,
	const FString& BaseDirectory,
	const bool bEmbedTextures,
	FString& OutGltfJson,
	FString& OutError)
{
	if (!IsFLTData(FLTData))
	{
		OutError = TEXT("Data is not a valid OpenFlight (.flt) database");
		return false;
	}

	const FByteReader Reader(FLTData.GetData(), FLTData.Num());
	const int64 Size = FLTData.Num();

	double UnitScale = 1.0;

	// Texture palette: index -> file path.
	TMap<int32, FTextureEntry> TexturePalette;

	// Vertex palette: byte offset of a vertex record (within the file) -> resolved vertex.
	// OpenFlight face/vertex-list records reference vertices by their byte offset into
	// the vertex palette, so we capture that mapping during the palette pass.
	TMap<int32, FVertex> VertexPalette;

	// All meshes produced. Each Group/Object becomes (potentially) one mesh.
	TArray<FMeshBuilder> Meshes;

	// --- Pass 1: header, texture palette and vertex palette --------------------------------------
	{
		int64 Pos = 0;
		while (Pos + 4 <= Size)
		{
			const uint16 Opcode = Reader.U16(Pos);
			const uint16 RecLen = Reader.U16(Pos + 2);
			if (RecLen < 4)
			{
				break; // malformed / end
			}
			const int64 RecStart = Pos;

			switch (static_cast<EOpcode>(Opcode))
			{
			case EOpcode::Header:
			{
				// Vertex storage unit code is a single byte at offset 120 in the header record.
				if (Reader.InBounds(RecStart + 120, 1))
				{
					UnitScale = UnitsToMeters(Reader.Data[RecStart + 120]);
				}
				break;
			}
			case EOpcode::TexturePalette:
			{
				// opcode(2) len(2) filename(200 chars) index(4) ...
				FTextureEntry Entry;
				Entry.Path = Reader.FixedString(RecStart + 4, 200);
				Entry.Index = Reader.I32(RecStart + 4 + 200);
				if (Entry.Index >= 0 && !Entry.Path.IsEmpty())
				{
					TexturePalette.Add(Entry.Index, Entry);
				}
				break;
			}
			case EOpcode::VertexC:
			case EOpcode::VertexCN:
			case EOpcode::VertexCNT:
			case EOpcode::VertexCT:
			{
				FVertex V;
				// All vertex records: opcode(2) len(2) colorNameIndex(2) flags(2) then doubles.
				const int64 PosOff = RecStart + 8;
				V.X = Reader.F64(PosOff);
				V.Y = Reader.F64(PosOff + 8);
				V.Z = Reader.F64(PosOff + 16);

				int64 Cursor = PosOff + 24;
				const EOpcode Kind = static_cast<EOpcode>(Opcode);
				if (Kind == EOpcode::VertexCN || Kind == EOpcode::VertexCNT)
				{
					V.NX = Reader.F32(Cursor);
					V.NY = Reader.F32(Cursor + 4);
					V.NZ = Reader.F32(Cursor + 8);
					V.bHasNormal = true;
					Cursor += 12;
				}
				if (Kind == EOpcode::VertexCNT || Kind == EOpcode::VertexCT)
				{
					V.U = Reader.F32(Cursor);
					V.V = Reader.F32(Cursor + 4);
					V.bHasUV = true;
					Cursor += 8;
				}

				VertexPalette.Add(static_cast<int32>(RecStart), V);
				break;
			}
			default:
				break;
			}

			Pos += RecLen;
		}
	}

	if (VertexPalette.Num() == 0)
	{
		OutError = TEXT("OpenFlight database contains no vertices in its vertex palette");
		return false;
	}

	// --- Pass 2: scene graph + faces -------------------------------------------------------------
	// We maintain a stack of "current mesh" while descending Push/Pop levels. Faces attach to the
	// mesh on the top of the stack; a Group/Object/LOD opens a new mesh scope.
	{
		struct FScope
		{
			int32 MeshIndex = INDEX_NONE;
		};

		TArray<FScope> Stack;
		Stack.Push(FScope());

		// State carried by the most recent Face record until its Vertex List is read.
		int32 PendingTextureIndex = -1;
		bool bFaceOpen = false;

		auto EnsureMesh = [&](const FString& Name) -> int32
		{
			FScope& Top = Stack.Top();
			if (Top.MeshIndex == INDEX_NONE)
			{
				FMeshBuilder NewMesh;
				NewMesh.Name = Name.IsEmpty() ? FString::Printf(TEXT("FLT_Mesh_%d"), Meshes.Num()) : Name;
				Top.MeshIndex = Meshes.Add(MoveTemp(NewMesh));
			}
			return Top.MeshIndex;
		};

		int64 Pos = 0;
		while (Pos + 4 <= Size)
		{
			const uint16 Opcode = Reader.U16(Pos);
			const uint16 RecLen = Reader.U16(Pos + 2);
			if (RecLen < 4)
			{
				break;
			}
			const int64 RecStart = Pos;

			switch (static_cast<EOpcode>(Opcode))
			{
			case EOpcode::Group:
			case EOpcode::Object:
			case EOpcode::LevelOfDetail:
			case EOpcode::Mesh:
			{
				// Record id is an 8 char ASCII name right after opcode+len.
				const FString NodeName = Reader.FixedString(RecStart + 4, 8);
				FScope& Top = Stack.Top();
				// Open a fresh mesh for this node.
				FMeshBuilder NewMesh;
				NewMesh.Name = NodeName.IsEmpty()
					? FString::Printf(TEXT("FLT_Node_%d"), Meshes.Num())
					: NodeName;
				Top.MeshIndex = Meshes.Add(MoveTemp(NewMesh));
				break;
			}
			case EOpcode::PushLevel:
			{
				FScope Child;
				Child.MeshIndex = Stack.Top().MeshIndex;
				Stack.Push(Child);
				break;
			}
			case EOpcode::PopLevel:
			{
				if (Stack.Num() > 1)
				{
					Stack.Pop();
				}
				bFaceOpen = false;
				break;
			}
			case EOpcode::Face:
			{
				// Face record layout (offsets from record start):
				//   4   id (8 chars)
				//   ... many fields; texture pattern index is a signed int16 at offset 24.
				PendingTextureIndex = Reader.I16(RecStart + 24);
				bFaceOpen = true;
				break;
			}
			case EOpcode::VertexList:
			{
				if (!bFaceOpen)
				{
					break;
				}
				const int32 MeshIndex = EnsureMesh(TEXT(""));
				FMeshBuilder& Mesh = Meshes[MeshIndex];
				FPrimitiveBuilder& Prim = Mesh.GetPrimitive(PendingTextureIndex);

				// Each entry is a 4-byte offset into the vertex palette.
				TArray<int32> FaceVertexLocalIndices;
				const int64 NumEntries = (RecLen - 4) / 4;
				for (int64 i = 0; i < NumEntries; ++i)
				{
					const int32 PaletteOffset = Reader.I32(RecStart + 4 + i * 4);
					const FVertex* PalVertex = VertexPalette.Find(PaletteOffset);
					if (!PalVertex)
					{
						continue;
					}
					const int32 LocalIndex = Prim.Vertices.Add(*PalVertex);
					FaceVertexLocalIndices.Add(LocalIndex);
				}

				// Fan-triangulate the (convex) polygon.
				for (int32 i = 1; i + 1 < FaceVertexLocalIndices.Num(); ++i)
				{
					Prim.Indices.Add(FaceVertexLocalIndices[0]);
					Prim.Indices.Add(FaceVertexLocalIndices[i]);
					Prim.Indices.Add(FaceVertexLocalIndices[i + 1]);
				}

				bFaceOpen = false;
				break;
			}
			default:
				break;
			}

			Pos += RecLen;
		}
	}

	// Drop empty meshes.
	Meshes.RemoveAll([](const FMeshBuilder& M) { return M.IsEmpty(); });

	if (Meshes.Num() == 0)
	{
		OutError = TEXT("OpenFlight database produced no triangles");
		return false;
	}

	// --- Build the binary buffer + glTF accessors ------------------------------------------------
	// Layout per primitive: POSITION (vec3 f32), NORMAL (vec3 f32), TEXCOORD_0 (vec2 f32), INDICES (u32).
	TArray<uint8> BinaryBuffer;

	TArray<TSharedPtr<FJsonValue>> JsonBufferViews;
	TArray<TSharedPtr<FJsonValue>> JsonAccessors;
	TArray<TSharedPtr<FJsonValue>> JsonMeshes;
	TArray<TSharedPtr<FJsonValue>> JsonNodes;
	TArray<TSharedPtr<FJsonValue>> JsonMaterials;
	TArray<TSharedPtr<FJsonValue>> JsonTextures;
	TArray<TSharedPtr<FJsonValue>> JsonImages;
	TArray<TSharedPtr<FJsonValue>> JsonSamplers;
	TArray<TSharedPtr<FJsonValue>> SceneNodeIndices;

	auto Align4 = [&BinaryBuffer]()
	{
		while (BinaryBuffer.Num() % 4 != 0)
		{
			BinaryBuffer.Add(0);
		}
	};

	auto AddBufferView = [&](int64 Offset, int64 Length, int32 Target) -> int32
	{
		TSharedRef<FJsonObject> BV = MakeShared<FJsonObject>();
		BV->SetNumberField(TEXT("buffer"), 0);
		BV->SetNumberField(TEXT("byteOffset"), Offset);
		BV->SetNumberField(TEXT("byteLength"), Length);
		if (Target > 0)
		{
			BV->SetNumberField(TEXT("target"), Target);
		}
		JsonBufferViews.Add(MakeShared<FJsonValueObject>(BV));
		return JsonBufferViews.Num() - 1;
	};

	// Map a texture palette index -> glTF material index (created lazily).
	TMap<int32, int32> TextureIndexToMaterial;

	auto ResolveMaterial = [&](int32 FltTextureIndex) -> int32
	{
		if (FltTextureIndex < 0)
		{
			return INDEX_NONE;
		}
		if (int32* Existing = TextureIndexToMaterial.Find(FltTextureIndex))
		{
			return *Existing;
		}
		const FTextureEntry* Entry = TexturePalette.Find(FltTextureIndex);
		if (!Entry)
		{
			return INDEX_NONE;
		}

		// Build image URI.
		FString ImageUri;
		FString ResolvedPath = Entry->Path;
		if (FPaths::IsRelative(ResolvedPath) && !BaseDirectory.IsEmpty())
		{
			ResolvedPath = FPaths::Combine(BaseDirectory, Entry->Path);
		}

		if (bEmbedTextures)
		{
			TArray<uint8> ImageBytes;
			if (FFileHelper::LoadFileToArray(ImageBytes, *ResolvedPath))
			{
				const FString Ext = FPaths::GetExtension(ResolvedPath).ToLower();
				FString Mime = TEXT("image/png");
				if (Ext == TEXT("jpg") || Ext == TEXT("jpeg")) Mime = TEXT("image/jpeg");
				else if (Ext == TEXT("rgb") || Ext == TEXT("rgba") || Ext == TEXT("sgi")) Mime = TEXT("image/x-sgi");
				ImageUri = FString::Printf(TEXT("data:%s;base64,%s"), *Mime,
					*FBase64::Encode(ImageBytes.GetData(), ImageBytes.Num()));
			}
			else
			{
				UE_LOG(LogglTFRuntimeFLT, Warning, TEXT("FLT texture not found for embedding: %s"), *ResolvedPath);
			}
		}
		else
		{
			// Relative URI; resolved later by glTFRuntime's base directory handling.
			ImageUri = Entry->Path;
		}

		if (ImageUri.IsEmpty())
		{
			// No usable image: still create a material so the slot is consistent.
			TextureIndexToMaterial.Add(FltTextureIndex, INDEX_NONE);
			return INDEX_NONE;
		}

		// image
		TSharedRef<FJsonObject> Image = MakeShared<FJsonObject>();
		Image->SetStringField(TEXT("uri"), ImageUri);
		const int32 ImageIndex = JsonImages.Add(MakeShared<FJsonValueObject>(Image));

		// sampler (one shared)
		if (JsonSamplers.Num() == 0)
		{
			TSharedRef<FJsonObject> Sampler = MakeShared<FJsonObject>();
			Sampler->SetNumberField(TEXT("wrapS"), 10497); // REPEAT
			Sampler->SetNumberField(TEXT("wrapT"), 10497);
			JsonSamplers.Add(MakeShared<FJsonValueObject>(Sampler));
		}

		// texture
		TSharedRef<FJsonObject> Texture = MakeShared<FJsonObject>();
		Texture->SetNumberField(TEXT("source"), ImageIndex);
		Texture->SetNumberField(TEXT("sampler"), 0);
		const int32 TexIndex = JsonTextures.Add(MakeShared<FJsonValueObject>(Texture));

		// material
		TSharedRef<FJsonObject> Material = MakeShared<FJsonObject>();
		Material->SetStringField(TEXT("name"), FPaths::GetBaseFilename(Entry->Path));
		TSharedRef<FJsonObject> PBR = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> BaseColorTex = MakeShared<FJsonObject>();
		BaseColorTex->SetNumberField(TEXT("index"), TexIndex);
		BaseColorTex->SetNumberField(TEXT("texCoord"), 0);
		PBR->SetObjectField(TEXT("baseColorTexture"), BaseColorTex);
		PBR->SetNumberField(TEXT("metallicFactor"), 0.0);
		PBR->SetNumberField(TEXT("roughnessFactor"), 1.0);
		Material->SetObjectField(TEXT("pbrMetallicRoughness"), PBR);
		const int32 MatIndex = JsonMaterials.Add(MakeShared<FJsonValueObject>(Material));

		TextureIndexToMaterial.Add(FltTextureIndex, MatIndex);
		return MatIndex;
	};

	auto AddAccessor = [&](int32 BufferView, int32 ComponentType, const FString& Type, int32 Count,
		const TArray<double>* Min, const TArray<double>* Max) -> int32
	{
		TSharedRef<FJsonObject> Acc = MakeShared<FJsonObject>();
		Acc->SetNumberField(TEXT("bufferView"), BufferView);
		Acc->SetNumberField(TEXT("byteOffset"), 0);
		Acc->SetNumberField(TEXT("componentType"), ComponentType);
		Acc->SetStringField(TEXT("type"), Type);
		Acc->SetNumberField(TEXT("count"), Count);
		if (Min)
		{
			TArray<TSharedPtr<FJsonValue>> MinArr;
			for (double D : *Min) MinArr.Add(MakeShared<FJsonValueNumber>(D));
			Acc->SetArrayField(TEXT("min"), MinArr);
		}
		if (Max)
		{
			TArray<TSharedPtr<FJsonValue>> MaxArr;
			for (double D : *Max) MaxArr.Add(MakeShared<FJsonValueNumber>(D));
			Acc->SetArrayField(TEXT("max"), MaxArr);
		}
		JsonAccessors.Add(MakeShared<FJsonValueObject>(Acc));
		return JsonAccessors.Num() - 1;
	};

	// Coordinate conversion: OpenFlight is right-handed Z-up (X east, Y north, Z up).
	// glTF is right-handed Y-up. Map (X, Y, Z)_flt -> (X, Z, -Y)_gltf. glTFRuntime then applies
	// its own basis change to reach Unreal space. Positions are scaled to meters.
	auto ConvPos = [&](const FVertex& Vtx, float& OX, float& OY, float& OZ)
	{
		OX = static_cast<float>(Vtx.X * UnitScale);
		OY = static_cast<float>(Vtx.Z * UnitScale);
		OZ = static_cast<float>(-Vtx.Y * UnitScale);
	};
	auto ConvNormal = [&](const FVertex& Vtx, float& OX, float& OY, float& OZ)
	{
		OX = Vtx.NX;
		OY = Vtx.NZ;
		OZ = -Vtx.NY;
	};

	for (FMeshBuilder& Mesh : Meshes)
	{
		TArray<TSharedPtr<FJsonValue>> Primitives;

		for (TPair<int32, FPrimitiveBuilder>& PrimPair : Mesh.Primitives)
		{
			FPrimitiveBuilder& Prim = PrimPair.Value;
			if (Prim.Indices.Num() == 0)
			{
				continue;
			}

			const int32 NumVerts = Prim.Vertices.Num();

			// POSITION
			Align4();
			const int64 PosStart = BinaryBuffer.Num();
			TArray<double> MinPos = { TNumericLimits<double>::Max(), TNumericLimits<double>::Max(), TNumericLimits<double>::Max() };
			TArray<double> MaxPos = { TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest() };
			for (const FVertex& Vtx : Prim.Vertices)
			{
				float X, Y, Z;
				ConvPos(Vtx, X, Y, Z);
				const float Comp[3] = { X, Y, Z };
				BinaryBuffer.Append(reinterpret_cast<const uint8*>(Comp), sizeof(Comp));
				MinPos[0] = FMath::Min<double>(MinPos[0], X); MaxPos[0] = FMath::Max<double>(MaxPos[0], X);
				MinPos[1] = FMath::Min<double>(MinPos[1], Y); MaxPos[1] = FMath::Max<double>(MaxPos[1], Y);
				MinPos[2] = FMath::Min<double>(MinPos[2], Z); MaxPos[2] = FMath::Max<double>(MaxPos[2], Z);
			}
			const int32 PosBV = AddBufferView(PosStart, BinaryBuffer.Num() - PosStart, 34962);
			const int32 PosAcc = AddAccessor(PosBV, 5126, TEXT("VEC3"), NumVerts, &MinPos, &MaxPos);

			// NORMAL
			Align4();
			const int64 NrmStart = BinaryBuffer.Num();
			for (const FVertex& Vtx : Prim.Vertices)
			{
				float X, Y, Z;
				ConvNormal(Vtx, X, Y, Z);
				// Normalize defensively (FLT normals may be unnormalized or zero).
				const float Len = FMath::Sqrt(X * X + Y * Y + Z * Z);
				if (Len > KINDA_SMALL_NUMBER) { X /= Len; Y /= Len; Z /= Len; }
				else { X = 0; Y = 1; Z = 0; }
				const float Comp[3] = { X, Y, Z };
				BinaryBuffer.Append(reinterpret_cast<const uint8*>(Comp), sizeof(Comp));
			}
			const int32 NrmBV = AddBufferView(NrmStart, BinaryBuffer.Num() - NrmStart, 34962);
			const int32 NrmAcc = AddAccessor(NrmBV, 5126, TEXT("VEC3"), NumVerts, nullptr, nullptr);

			// TEXCOORD_0
			Align4();
			const int64 UvStart = BinaryBuffer.Num();
			for (const FVertex& Vtx : Prim.Vertices)
			{
				// glTF UV origin is top-left; OpenFlight is bottom-left, so flip V.
				const float Comp[2] = { Vtx.U, 1.0f - Vtx.V };
				BinaryBuffer.Append(reinterpret_cast<const uint8*>(Comp), sizeof(Comp));
			}
			const int32 UvBV = AddBufferView(UvStart, BinaryBuffer.Num() - UvStart, 34962);
			const int32 UvAcc = AddAccessor(UvBV, 5126, TEXT("VEC2"), NumVerts, nullptr, nullptr);

			// INDICES
			Align4();
			const int64 IdxStart = BinaryBuffer.Num();
			BinaryBuffer.Append(reinterpret_cast<const uint8*>(Prim.Indices.GetData()), Prim.Indices.Num() * sizeof(uint32));
			const int32 IdxBV = AddBufferView(IdxStart, BinaryBuffer.Num() - IdxStart, 34963);
			const int32 IdxAcc = AddAccessor(IdxBV, 5125, TEXT("SCALAR"), Prim.Indices.Num(), nullptr, nullptr);

			// Primitive JSON
			TSharedRef<FJsonObject> PrimObj = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Attribs = MakeShared<FJsonObject>();
			Attribs->SetNumberField(TEXT("POSITION"), PosAcc);
			Attribs->SetNumberField(TEXT("NORMAL"), NrmAcc);
			Attribs->SetNumberField(TEXT("TEXCOORD_0"), UvAcc);
			PrimObj->SetObjectField(TEXT("attributes"), Attribs);
			PrimObj->SetNumberField(TEXT("indices"), IdxAcc);
			PrimObj->SetNumberField(TEXT("mode"), 4); // TRIANGLES

			const int32 MaterialIndex = ResolveMaterial(Prim.TextureIndex);
			if (MaterialIndex != INDEX_NONE)
			{
				PrimObj->SetNumberField(TEXT("material"), MaterialIndex);
			}

			Primitives.Add(MakeShared<FJsonValueObject>(PrimObj));
		}

		if (Primitives.Num() == 0)
		{
			continue;
		}

		TSharedRef<FJsonObject> MeshObj = MakeShared<FJsonObject>();
		MeshObj->SetStringField(TEXT("name"), Mesh.Name);
		MeshObj->SetArrayField(TEXT("primitives"), Primitives);
		const int32 MeshIndex = JsonMeshes.Add(MakeShared<FJsonValueObject>(MeshObj));

		TSharedRef<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("name"), Mesh.Name);
		NodeObj->SetNumberField(TEXT("mesh"), MeshIndex);
		const int32 NodeIndex = JsonNodes.Add(MakeShared<FJsonValueObject>(NodeObj));

		SceneNodeIndices.Add(MakeShared<FJsonValueNumber>(NodeIndex));
	}

	if (JsonMeshes.Num() == 0)
	{
		OutError = TEXT("OpenFlight conversion produced no renderable primitives");
		return false;
	}

	// --- Assemble root glTF document -------------------------------------------------------------
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	TSharedRef<FJsonObject> AssetObj = MakeShared<FJsonObject>();
	AssetObj->SetStringField(TEXT("version"), TEXT("2.0"));
	AssetObj->SetStringField(TEXT("generator"), TEXT("glTFRuntime OpenFlight Converter"));
	Root->SetObjectField(TEXT("asset"), AssetObj);

	// buffer (embedded base64)
	TSharedRef<FJsonObject> BufferObj = MakeShared<FJsonObject>();
	BufferObj->SetNumberField(TEXT("byteLength"), BinaryBuffer.Num());
	BufferObj->SetStringField(TEXT("uri"),
		FString::Printf(TEXT("data:application/octet-stream;base64,%s"),
			*FBase64::Encode(BinaryBuffer.GetData(), BinaryBuffer.Num())));
	Root->SetArrayField(TEXT("buffers"), { MakeShared<FJsonValueObject>(BufferObj) });

	Root->SetArrayField(TEXT("bufferViews"), JsonBufferViews);
	Root->SetArrayField(TEXT("accessors"), JsonAccessors);
	Root->SetArrayField(TEXT("meshes"), JsonMeshes);
	Root->SetArrayField(TEXT("nodes"), JsonNodes);

	if (JsonMaterials.Num() > 0) Root->SetArrayField(TEXT("materials"), JsonMaterials);
	if (JsonTextures.Num() > 0) Root->SetArrayField(TEXT("textures"), JsonTextures);
	if (JsonImages.Num() > 0) Root->SetArrayField(TEXT("images"), JsonImages);
	if (JsonSamplers.Num() > 0) Root->SetArrayField(TEXT("samplers"), JsonSamplers);

	TSharedRef<FJsonObject> SceneObj = MakeShared<FJsonObject>();
	SceneObj->SetArrayField(TEXT("nodes"), SceneNodeIndices);
	Root->SetArrayField(TEXT("scenes"), { MakeShared<FJsonValueObject>(SceneObj) });
	Root->SetNumberField(TEXT("scene"), 0);

	// Serialize.
	OutGltfJson.Reset();
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutGltfJson);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("Failed to serialize generated glTF JSON");
		return false;
	}

	UE_LOG(LogglTFRuntimeFLT, Log,
		TEXT("OpenFlight converted: %d mesh(es), %d material(s), %d bytes of geometry"),
		JsonMeshes.Num(), JsonMaterials.Num(), BinaryBuffer.Num());

	return true;
}
