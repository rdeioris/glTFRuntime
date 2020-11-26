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
		NewTriangle.v[0] = SourcePrimitive.Indices[VertexIndex];
		NewTriangle.v[1] = SourcePrimitive.Indices[VertexIndex + 1];
		NewTriangle.v[2] = SourcePrimitive.Indices[VertexIndex + 2];

		for (int32 Index = 0; Index < 3; Index++)
		{
			if (NewTriangle.v[Index] < (uint32)SourcePrimitive.Normals.Num())
			{
				NewTriangle.Normals[Index] = SourcePrimitive.Normals[NewTriangle.v[Index]];
			}

			if (NewTriangle.v[Index] < (uint32)SourcePrimitive.Tangents.Num())
			{
				NewTriangle.Tangents[Index] = SourcePrimitive.Tangents[NewTriangle.v[Index]];
			}

			if (NewTriangle.v[Index] < (uint32)SourcePrimitive.Colors.Num())
			{
				NewTriangle.Colors[Index] = SourcePrimitive.Colors[NewTriangle.v[Index]];
			}

			if (SourcePrimitive.UVs.Num() > 0)
			{
				if (NewTriangle.v[Index] < (uint32)SourcePrimitive.UVs[0].Num())
				{
					NewTriangle.UV[Index] = FVector(SourcePrimitive.UVs[0][NewTriangle.v[Index]].X, SourcePrimitive.UVs[0][NewTriangle.v[Index]].Y, 0);
				}
			}
		}
		triangles.push_back(NewTriangle);
	}
}

void FglTFRuntimeMeshReducer::SimplifyMesh(FglTFRuntimePrimitive& DestinationPrimitive, const float ReductionFactor, const double Aggressiveness)
{
	const int32 TargetCount = (int32)((float)triangles.size() * FMath::Clamp(ReductionFactor, 0.0f, 1.0f));

	// init
	loopi(0, triangles.size())
	{
		triangles[i].deleted = 0;
	}

	// main iteration loop
	int deleted_triangles = 0;
	std::vector<int> deleted0, deleted1;
	int triangle_count = triangles.size();

	for (int iteration = 0; iteration < 100; iteration++)
	{
		if (triangle_count - deleted_triangles <= TargetCount)break;

		// update mesh once in a while
		if (iteration % 5 == 0)
		{
			update_mesh(iteration);
		}

		// clear dirty flag
		loopi(0, triangles.size()) triangles[i].dirty = 0;

		//
		// All triangles with edges below the threshold will be removed
		//
		// The following numbers works well for most models.
		// If it does not, try to adjust the 3 parameters
		//
		double threshold = 0.000000001 * pow(double(iteration + 3), Aggressiveness);

		// remove vertices & mark deleted triangles
		loopi(0, triangles.size())
		{
			FTriangle& t = triangles[i];
			if (t.err[3] > threshold) continue;
			if (t.deleted) continue;
			if (t.dirty) continue;

			loopj(0, 3)if (t.err[j] < threshold)
			{

				int i0 = t.v[j]; FVertex& v0 = Vertices[i0];
				int i1 = t.v[(j + 1) % 3]; FVertex& v1 = Vertices[i1];
				// Border check
				if (v0.border != v1.border)  continue;

				// Compute vertex to collapse to
				FVector p;
				CalculateError(i0, i1, p);
				deleted0.resize(v0.tcount); // normals temporarily
				deleted1.resize(v1.tcount); // normals temporarily
				// don't remove if flipped
				if (IsFlipped(p, i0, i1, v0, v1, deleted0)) continue;

				if (IsFlipped(p, i1, i0, v1, v0, deleted1)) continue;

				if (SourcePrimitive.Normals.Num() > 0)
				{
					UpdateVertexNormals(i0, v0, p, deleted0);
					UpdateVertexNormals(i0, v1, p, deleted1);
				}

				if (SourcePrimitive.Tangents.Num() > 0)
				{
					UpdateVertexTangents(i0, v0, p, deleted0);
					UpdateVertexTangents(i0, v1, p, deleted1);
				}

				if (SourcePrimitive.Colors.Num() > 0)
				{
					UpdateVertexColors(i0, v0, p, deleted0);
					UpdateVertexColors(i0, v1, p, deleted1);
				}

				if (SourcePrimitive.UVs.Num() > 0)
				{
					UpdateVertexUVs(i0, v0, p, deleted0);
					UpdateVertexUVs(i0, v1, p, deleted1);
				}


				// not flipped, so remove edge
				v0.Position = p;
				v0.q = v1.q + v0.q;
				int tstart = refs.size();

				update_triangles(i0, v0, deleted0, deleted_triangles);
				update_triangles(i0, v1, deleted1, deleted_triangles);

				int tcount = refs.size() - tstart;

				if (tcount <= v0.tcount)
				{
					// save ram
					if (tcount)memcpy(&refs[v0.tstart], &refs[tstart], tcount * sizeof(FRef));
				}
				else
					// append
					v0.tstart = tstart;

				v0.tcount = tcount;
				break;
			}
			// done?
			if (triangle_count - deleted_triangles <= TargetCount)break;
		}
	}
	// clean up mesh
	compact_mesh();

	if (SourcePrimitive.Joints.Num() > 0 && SourcePrimitive.Weights.Num())
	{
		DestinationPrimitive.Joints.AddZeroed();
		DestinationPrimitive.Weights.AddZeroed();
	}

	for (FVertex& Vertex : Vertices)
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

	for (FTriangle& Triangle : triangles)
	{
		DestinationPrimitive.Indices.Add(Triangle.v[0]);
		DestinationPrimitive.Indices.Add(Triangle.v[1]);
		DestinationPrimitive.Indices.Add(Triangle.v[2]);

		if (SourcePrimitive.Normals.Num() > 0)
		{
			DestinationPrimitive.Normals[Triangle.v[0]] = Triangle.Normals[0];
			DestinationPrimitive.Normals[Triangle.v[1]] = Triangle.Normals[1];
			DestinationPrimitive.Normals[Triangle.v[2]] = Triangle.Normals[2];
		}

		if (SourcePrimitive.Tangents.Num() > 0)
		{
			DestinationPrimitive.Tangents[Triangle.v[0]] = Triangle.Tangents[0];
			DestinationPrimitive.Tangents[Triangle.v[1]] = Triangle.Tangents[1];
			DestinationPrimitive.Tangents[Triangle.v[2]] = Triangle.Tangents[2];
		}

		if (SourcePrimitive.Colors.Num() > 0)
		{
			DestinationPrimitive.Colors[Triangle.v[0]] = Triangle.Colors[0];
			DestinationPrimitive.Colors[Triangle.v[1]] = Triangle.Colors[1];
			DestinationPrimitive.Colors[Triangle.v[2]] = Triangle.Colors[2];
		}

		if (SourcePrimitive.UVs.Num() > 0)
		{
			DestinationPrimitive.UVs[0][Triangle.v[0]] = FVector2D(Triangle.UV[0].X, Triangle.UV[0].Y);
			DestinationPrimitive.UVs[0][Triangle.v[1]] = FVector2D(Triangle.UV[1].X, Triangle.UV[1].Y);
			DestinationPrimitive.UVs[0][Triangle.v[2]] = FVector2D(Triangle.UV[2].X, Triangle.UV[2].Y);
		}

	}

	DestinationPrimitive.Material = SourcePrimitive.Material;
}