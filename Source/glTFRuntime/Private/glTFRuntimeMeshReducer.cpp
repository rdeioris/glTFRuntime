// Copyright 2020, Roberto De Ioris.


#include "glTFRuntimeMeshReducer.h"

FglTFRuntimeMeshReducer::FglTFRuntimeMeshReducer(FglTFRuntimePrimitive& InSourcePrimitive) : SourcePrimitive(InSourcePrimitive)
{
	for (int32 PositionIndex = 0; PositionIndex < SourcePrimitive.Positions.Num(); PositionIndex++)
	{
		FVertex NewVertex;
		NewVertex.Position = SourcePrimitive.Positions[PositionIndex];

		if (SourcePrimitive.Joints.Num() > 0 && SourcePrimitive.Weights.Num() > 0)
		{
			if (PositionIndex < SourcePrimitive.Joints[0].Num() && PositionIndex < SourcePrimitive.Weights[0].Num())
			{
				NewVertex.Joints = SourcePrimitive.Joints[0][PositionIndex];
				NewVertex.Weights = SourcePrimitive.Weights[0][PositionIndex];
			}
		}

		Vertices.Add(NewVertex);
	}

	for (int32 VertexIndex = 0; VertexIndex < SourcePrimitive.Indices.Num(); VertexIndex += 3)
	{
		FTriangle NewTriangle;
		NewTriangle.Vertices[0] = SourcePrimitive.Indices[VertexIndex];
		NewTriangle.Vertices[1] = SourcePrimitive.Indices[VertexIndex + 1];
		NewTriangle.Vertices[2] = SourcePrimitive.Indices[VertexIndex + 2];

		for (int32 Index = 0; Index < 3; Index++)
		{
			if (NewTriangle.Vertices[Index] < (uint32)SourcePrimitive.Normals.Num())
			{
				NewTriangle.Normals[Index] = SourcePrimitive.Normals[NewTriangle.Vertices[Index]];
			}

			if (NewTriangle.Vertices[Index] < (uint32)SourcePrimitive.Tangents.Num())
			{
				NewTriangle.Tangents[Index] = SourcePrimitive.Tangents[NewTriangle.Vertices[Index]];
			}

			if (NewTriangle.Vertices[Index] < (uint32)SourcePrimitive.Colors.Num())
			{
				NewTriangle.Colors[Index] = SourcePrimitive.Colors[NewTriangle.Vertices[Index]];
			}

			if (SourcePrimitive.UVs.Num() > 0)
			{
				if (NewTriangle.Vertices[Index] < (uint32)SourcePrimitive.UVs[0].Num())
				{
					NewTriangle.UV[Index] = FVector(SourcePrimitive.UVs[0][NewTriangle.Vertices[Index]].X, SourcePrimitive.UVs[0][NewTriangle.Vertices[Index]].Y, 0);
				}
			}
		}
		Triangles.Add(NewTriangle);
	}
}

void FglTFRuntimeMeshReducer::SimplifyMesh(FglTFRuntimePrimitive& DestinationPrimitive, const float ReductionFactor, const double Aggressiveness)
{
	const int32 TargetCount = (int32)((float)Triangles.Num() * FMath::Clamp(ReductionFactor, 0.0f, 1.0f));

	for (FTriangle& Triangle : Triangles)
	{
		Triangle.bDeleted = false;
	}

	int32 DeletedTriangles = 0;
	TArray<int32> Deleted0;
	TArray<int32> Deleted1;

	int32 TriangleCount = Triangles.Num();

	for (int32 Iteration = 0; Iteration < 100; Iteration++)
	{
		if (TriangleCount - DeletedTriangles <= TargetCount)break;

		// update mesh once in a while
		if (Iteration % 5 == 0)
		{
			UpdateMesh(Iteration);
		}

		for (FTriangle& Triangle : Triangles)
		{
			Triangle.bDirty = false;
		}

		//
		// All triangles with edges below the threshold will be removed
		//
		// The following numbers works well for most models.
		// If it does not, try to adjust the 3 parameters
		//
		double Threshold = 0.000000001 * pow(double(Iteration + 3), Aggressiveness);

		// remove vertices & mark deleted triangles
		for (FTriangle& Triangle : Triangles)
		{
			if (Triangle.err[3] > Threshold) continue;
			if (Triangle.bDeleted)
			{
				continue;
			}

			if (Triangle.bDirty)
			{
				continue;
			}

			for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
			{
				if (Triangle.err[VertexIndex] < Threshold)
				{

					int i0 = Triangle.Vertices[VertexIndex]; FVertex& v0 = Vertices[i0];
					int i1 = Triangle.Vertices[(VertexIndex + 1) % 3]; FVertex& v1 = Vertices[i1];
					// Border check
					if (v0.bIsBorder != v1.bIsBorder)
					{
						continue;
					}

					// Compute vertex to collapse to
					FVector p;
					CalculateError(i0, i1, p);
					Deleted0.SetNum(v0.tcount); // normals temporarily
					Deleted1.SetNum(v1.tcount); // normals temporarily
					// don't remove if flipped
					if (IsFlipped(p, i0, i1, v0, v1, Deleted0)) continue;

					if (IsFlipped(p, i1, i0, v1, v0, Deleted1)) continue;

					if (SourcePrimitive.Normals.Num() > 0)
					{
						UpdateVertexNormals(i0, v0, p, Deleted0);
						UpdateVertexNormals(i0, v1, p, Deleted1);
					}

					if (SourcePrimitive.Tangents.Num() > 0)
					{
						UpdateVertexTangents(i0, v0, p, Deleted0);
						UpdateVertexTangents(i0, v1, p, Deleted1);
					}

					if (SourcePrimitive.Colors.Num() > 0)
					{
						UpdateVertexColors(i0, v0, p, Deleted0);
						UpdateVertexColors(i0, v1, p, Deleted1);
					}

					if (SourcePrimitive.UVs.Num() > 0)
					{
						UpdateVertexUVs(i0, v0, p, Deleted0);
						UpdateVertexUVs(i0, v1, p, Deleted1);
					}

					// not flipped, so remove edge
					v0.Position = p;
					v0.q = v1.q + v0.q;
					int tstart = Refs.Num();

					UpdateTriangles(i0, v0, Deleted0, DeletedTriangles);
					UpdateTriangles(i0, v1, Deleted1, DeletedTriangles);

					int tcount = Refs.Num() - tstart;

					if (tcount <= v0.tcount)
					{
						// save ram
						if (tcount)memcpy(&Refs[v0.tstart], &Refs[tstart], tcount * sizeof(FRef));
					}
					else
						// append
						v0.tstart = tstart;

					v0.tcount = tcount;
					break;
				}
			}
			// done?
			if (TriangleCount - DeletedTriangles <= TargetCount)break;
		}
	}

	CompactMesh();

	if (SourcePrimitive.Joints.Num() > 0 && SourcePrimitive.Weights.Num())
	{
		DestinationPrimitive.Joints.AddZeroed();
		DestinationPrimitive.Weights.AddZeroed();
	}

	for (const FVertex& Vertex : Vertices)
	{
		DestinationPrimitive.Positions.Add(Vertex.Position);
		if (SourcePrimitive.Joints.Num() > 0 && SourcePrimitive.Weights.Num())
		{
			DestinationPrimitive.Joints[0].Add(Vertex.Joints);
			DestinationPrimitive.Weights[0].Add(Vertex.Weights);
		}
	}

	if (SourcePrimitive.Normals.Num() > 0)
	{
		DestinationPrimitive.Normals.AddZeroed(Vertices.Num());
	}

	if (SourcePrimitive.Tangents.Num() > 0)
	{
		DestinationPrimitive.Tangents.AddZeroed(Vertices.Num());
	}

	if (SourcePrimitive.Colors.Num() > 0)
	{
		DestinationPrimitive.Colors.AddZeroed(Vertices.Num());
	}

	if (SourcePrimitive.UVs.Num() > 0)
	{
		DestinationPrimitive.UVs.AddZeroed();
		DestinationPrimitive.UVs[0].AddZeroed(Vertices.Num());
	}

	for (const FTriangle& Triangle : Triangles)
	{
		DestinationPrimitive.Indices.Add(Triangle.Vertices[0]);
		DestinationPrimitive.Indices.Add(Triangle.Vertices[1]);
		DestinationPrimitive.Indices.Add(Triangle.Vertices[2]);

		if (SourcePrimitive.Normals.Num() > 0)
		{
			DestinationPrimitive.Normals[Triangle.Vertices[0]] = Triangle.Normals[0];
			DestinationPrimitive.Normals[Triangle.Vertices[1]] = Triangle.Normals[1];
			DestinationPrimitive.Normals[Triangle.Vertices[2]] = Triangle.Normals[2];
		}

		if (SourcePrimitive.Tangents.Num() > 0)
		{
			DestinationPrimitive.Tangents[Triangle.Vertices[0]] = Triangle.Tangents[0];
			DestinationPrimitive.Tangents[Triangle.Vertices[1]] = Triangle.Tangents[1];
			DestinationPrimitive.Tangents[Triangle.Vertices[2]] = Triangle.Tangents[2];
		}

		if (SourcePrimitive.Colors.Num() > 0)
		{
			DestinationPrimitive.Colors[Triangle.Vertices[0]] = Triangle.Colors[0];
			DestinationPrimitive.Colors[Triangle.Vertices[1]] = Triangle.Colors[1];
			DestinationPrimitive.Colors[Triangle.Vertices[2]] = Triangle.Colors[2];
		}

		if (SourcePrimitive.UVs.Num() > 0)
		{
			DestinationPrimitive.UVs[0][Triangle.Vertices[0]] = FVector2D(Triangle.UV[0].X, Triangle.UV[0].Y);
			DestinationPrimitive.UVs[0][Triangle.Vertices[1]] = FVector2D(Triangle.UV[1].X, Triangle.UV[1].Y);
			DestinationPrimitive.UVs[0][Triangle.Vertices[2]] = FVector2D(Triangle.UV[2].X, Triangle.UV[2].Y);
		}

	}

	DestinationPrimitive.Material = SourcePrimitive.Material;
}

void FglTFRuntimeMeshReducer::UpdateTriangles(int i0, FVertex& Vertex, const TArray<int32>& Deleted, int32& DeletedTriangles)
{
	FVector Point;
	for (int32 Index = 0; Index < Vertex.tcount; Index++)
	{
		FRef RefCopy = Refs[Vertex.tstart + Index];
		FTriangle& Triangle = Triangles[RefCopy.TriangleId];
		if (Triangle.bDeleted)
		{
			continue;
		}
		if (Deleted[Index])
		{
			Triangle.bDeleted = true;
			DeletedTriangles++;
			continue;
		}
		Triangle.Vertices[RefCopy.TriangleVertexId] = i0;
		Triangle.bDirty = true;
		Triangle.err[0] = CalculateError(Triangle.Vertices[0], Triangle.Vertices[1], Point);
		Triangle.err[1] = CalculateError(Triangle.Vertices[1], Triangle.Vertices[2], Point);
		Triangle.err[2] = CalculateError(Triangle.Vertices[2], Triangle.Vertices[0], Point);
		Triangle.err[3] = FMath::Min(Triangle.err[0], FMath::Min(Triangle.err[1], Triangle.err[2]));
		Refs.Add(RefCopy);
	}
}