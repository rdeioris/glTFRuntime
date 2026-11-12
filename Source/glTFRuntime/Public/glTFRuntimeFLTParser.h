// Copyright 2020-2024, Roberto De Ioris.
// OpenFlight (.flt) runtime ingestion support for glTFRuntime.

#pragma once

#include "CoreMinimal.h"

/**
 * FglTFRuntimeFLTConverter
 *
 * Converts an OpenFlight (.flt) binary database into an in-memory glTF 2.0
 * JSON document (with an embedded base64 binary buffer). The resulting JSON can
 * be handed straight to FglTFRuntimeParser / UglTFRuntimeAsset::LoadFromString,
 * which means OpenFlight assets reuse glTFRuntime's entire mesh, material and
 * scene-graph pipeline.
 *
 * The converter implements the geometry-relevant subset of the OpenFlight 15.x
 * / 16.x specification (FAA based, big-endian, record/opcode based):
 *   - Header (units, scale)
 *   - Group / Object / LOD scene hierarchy (Push/Pop level stack)
 *   - Matrix (per-node transforms)
 *   - Vertex palette (with / without color, normal, uv)
 *   - Face records (with material + texture index) and vertex lists
 *   - Morph / mesh primitive records are triangulated (fan) on the fly
 *   - Texture palette (external image references)
 *
 * Anything outside that subset (DOF, sound, light points, switches, animation,
 * external xrefs to other .flt files, etc.) is skipped gracefully so the file
 * still loads.
 */
class GLTFRUNTIME_API FglTFRuntimeFLTConverter
{
public:
	/**
	 * Convert raw OpenFlight bytes into a glTF 2.0 JSON string.
	 *
	 * @param FLTData          Raw .flt file bytes.
	 * @param BaseDirectory    Directory the .flt was loaded from. Used to resolve
	 *                         relative texture paths in the texture palette. May be empty.
	 * @param bEmbedTextures   If true, referenced textures found on disk are read and
	 *                         embedded as base64 data URIs. If false, textures are emitted
	 *                         as relative URIs (resolved later via LoaderConfig base directory).
	 * @param OutGltfJson      Receives the generated glTF JSON on success.
	 * @param OutError         Receives a human readable error message on failure.
	 * @return true on success.
	 */
	static bool ConvertToGltf(
		const TArray<uint8>& FLTData,
		const FString& BaseDirectory,
		const bool bEmbedTextures,
		FString& OutGltfJson,
		FString& OutError);

	/** Lightweight magic check: returns true if the bytes look like an OpenFlight database. */
	static bool IsFLTData(const TArray<uint8>& Data);
};
