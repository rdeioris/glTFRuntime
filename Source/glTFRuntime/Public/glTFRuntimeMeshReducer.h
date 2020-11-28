// Copyright 2020, Roberto De Ioris.

/*
	This is a reimplementation + search&replace of
	Sven Fortsmann's https://github.com/sp4cerat/Fast-Quadric-Mesh-Simplification

	Main modifications are usage of Unreal structures and renaming variables
	to "meaningful" names.

*/

#pragma once

#include "CoreMinimal.h"
#include "glTFRuntimeParser.h"

#define loopi(start_l,end_l) for ( int i=start_l;i<end_l;++i )

/**
 *
 */
class GLTFRUNTIME_API FglTFRuntimeMeshReducer
{

public:

	FglTFRuntimeMeshReducer(FglTFRuntimePrimitive& InSourcePrimitive);

	struct FQuadricMatrix {


		FQuadricMatrix(double c = 0) { loopi(0, 10) m[i] = c; }

		FQuadricMatrix(double m11, double m12, double m13, double m14,
			double m22, double m23, double m24,
			double m33, double m34,
			double m44) {
			m[0] = m11;  m[1] = m12;  m[2] = m13;  m[3] = m14;
			m[4] = m22;  m[5] = m23;  m[6] = m24;
			m[7] = m33;  m[8] = m34;
			m[9] = m44;
		}

		// Make plane

		FQuadricMatrix(double a, double b, double c, double d)
		{
			m[0] = a * a;  m[1] = a * b;  m[2] = a * c;  m[3] = a * d;
			m[4] = b * b;  m[5] = b * c;  m[6] = b * d;
			m[7] = c * c; m[8] = c * d;
			m[9] = d * d;
		}

		double operator[](int c) const { return m[c]; }

		// Determinant

		double det(int a11, int a12, int a13,
			int a21, int a22, int a23,
			int a31, int a32, int a33)
		{
			double det = m[a11] * m[a22] * m[a33] + m[a13] * m[a21] * m[a32] + m[a12] * m[a23] * m[a31]
				- m[a13] * m[a22] * m[a31] - m[a11] * m[a23] * m[a32] - m[a12] * m[a21] * m[a33];
			return det;
		}

		const FQuadricMatrix operator+(const FQuadricMatrix& n) const
		{
			return FQuadricMatrix(m[0] + n[0], m[1] + n[1], m[2] + n[2], m[3] + n[3],
				m[4] + n[4], m[5] + n[5], m[6] + n[6],
				m[7] + n[7], m[8] + n[8],
				m[9] + n[9]);
		}

		FQuadricMatrix& operator+=(const FQuadricMatrix& n)
		{
			m[0] += n[0];   m[1] += n[1];   m[2] += n[2];   m[3] += n[3];
			m[4] += n[4];   m[5] += n[5];   m[6] += n[6];   m[7] += n[7];
			m[8] += n[8];   m[9] += n[9];
			return *this;
		}

		double m[10];
	};

	struct FTriangle
	{
		uint32 Vertices[3];
		double err[4];
		bool bDeleted;
		bool bDirty;
		FVector Normal;

		FVector UV[3];
		FVector Normals[3];
		FVector Colors[3];
		FVector Tangents[3];

		FTriangle()
		{

		}
	};

	struct FVertex
	{
		FVector Position;
		int tstart, tcount;
		FQuadricMatrix q;
		bool bIsBorder;

		FglTFRuntimeUInt16Vector4 Joints;
		FVector4 Weights;

		FVertex()
		{

		}
	};

	struct FRef
	{
		int32 TriangleId;
		int32 TriangleVertexId;
	};

	TArray<FTriangle> Triangles;
	TArray<FVertex> Vertices;
	TArray<FRef> Refs;

	void SimplifyMesh(FglTFRuntimePrimitive& DestinationPrimitive, const float ReductionFactor, const double Aggressiveness = 7);

	bool IsFlipped(FVector p, int i0, int i1, FVertex& v0, FVertex& v1, TArray<int32>& Deleted)
	{

		for (int32 k = 0; k < v0.tcount; k++)
		{
			FTriangle& Triangle = Triangles[Refs[v0.tstart + k].TriangleId];
			if (Triangle.bDeleted)
			{
				continue;
			}

			int s = Refs[v0.tstart + k].TriangleVertexId;
			int id1 = Triangle.Vertices[(s + 1) % 3];
			int id2 = Triangle.Vertices[(s + 2) % 3];

			if (id1 == i1 || id2 == i1) // delete ?
			{

				Deleted[k] = 1;
				continue;
			}
			FVector d1 = Vertices[id1].Position - p; d1.Normalize();
			FVector d2 = Vertices[id2].Position - p; d2.Normalize();
			if (FMath::Abs(FVector::DotProduct(d1, d2)) > 0.999) return true;
			FVector n = FVector::CrossProduct(d1, d2).GetSafeNormal();
			Deleted[k] = 0;
			if (FVector::DotProduct(n, Triangle.Normal) < 0.2) return true;
		}
		return false;
	}

	FVector Barycentric(const FVector& p, const FVector& a, const FVector& b, const FVector& c)
	{
		FVector v0 = b - a;
		FVector v1 = c - a;
		FVector v2 = p - a;
		double d00 = FVector::DotProduct(v0, v0);
		double d01 = FVector::DotProduct(v0, v1);
		double d11 = FVector::DotProduct(v1, v1);
		double d20 = FVector::DotProduct(v2, v0);
		double d21 = FVector::DotProduct(v2, v1);
		double denom = d00 * d11 - d01 * d01;
		double v = (d11 * d20 - d01 * d21) / denom;
		double w = (d00 * d21 - d01 * d20) / denom;
		double u = 1.0 - v - w;
		return FVector(u, v, w);
	}

	FVector Interpolate(const FVector& p, const FVector& a, const FVector& b, const FVector& c, const FVector* Attrs)
	{
		FVector bary = Barycentric(p, a, b, c);
		FVector out = FVector::ZeroVector;
		out = out + Attrs[0] * bary.X;
		out = out + Attrs[1] * bary.Y;
		out = out + Attrs[2] * bary.Z;
		return out;
	}

	void UpdateVertexUVs(int i0, const FVertex& v, const FVector& p, const TArray<int32>& Deleted)
	{
		for (int32 k = 0; k < v.tcount; k++)
		{
			FRef& r = Refs[v.tstart + k];
			FTriangle& t = Triangles[r.TriangleId];
			if (t.bDeleted)
			{
				continue;
			}
			if (Deleted[k])continue;
			FVector p1 = Vertices[t.Vertices[0]].Position;
			FVector p2 = Vertices[t.Vertices[1]].Position;
			FVector p3 = Vertices[t.Vertices[2]].Position;
			t.UV[r.TriangleVertexId] = Interpolate(p, p1, p2, p3, t.UV);
		}
	}

	void UpdateVertexNormals(int i0, const FVertex& v, const FVector& p, const TArray<int32>& Deleted)
	{
		for (int32 k = 0; k < v.tcount; k++)
		{
			FRef& r = Refs[v.tstart + k];
			FTriangle& t = Triangles[r.TriangleId];
			if (t.bDeleted)
			{
				continue;
			}
			if (Deleted[k])continue;
			FVector p1 = Vertices[t.Vertices[0]].Position;
			FVector p2 = Vertices[t.Vertices[1]].Position;
			FVector p3 = Vertices[t.Vertices[2]].Position;
			t.UV[r.TriangleVertexId] = Interpolate(p, p1, p2, p3, t.UV);
		}
	}

	void UpdateVertexTangents(int i0, const FVertex& v, const FVector& p, const TArray<int32>& Deleted)
	{
		for (int32 k = 0; k < v.tcount; k++)
		{
			FRef& r = Refs[v.tstart + k];
			FTriangle& t = Triangles[r.TriangleId];
			if (t.bDeleted)
			{
				continue;
			}
			if (Deleted[k])continue;
			FVector p1 = Vertices[t.Vertices[0]].Position;
			FVector p2 = Vertices[t.Vertices[1]].Position;
			FVector p3 = Vertices[t.Vertices[2]].Position;
			t.Tangents[r.TriangleVertexId] = Interpolate(p, p1, p2, p3, t.Tangents);
		}
	}

	void UpdateVertexColors(int i0, const FVertex& v, const FVector& p, const TArray<int32>& Deleted)
	{
		for (int32 k = 0; k < v.tcount; k++)
		{
			FRef& r = Refs[v.tstart + k];
			FTriangle& t = Triangles[r.TriangleId];
			if (t.bDeleted)
			{
				continue;
			}
			if (Deleted[k])continue;
			FVector p1 = Vertices[t.Vertices[0]].Position;
			FVector p2 = Vertices[t.Vertices[1]].Position;
			FVector p3 = Vertices[t.Vertices[2]].Position;
			t.Colors[r.TriangleVertexId] = Interpolate(p, p1, p2, p3, t.Colors);
		}
	}

	void UpdateTriangles(int i0, FVertex& Vertex, const TArray<int32>& Deleted, int32& DeletedTriangles);

	void UpdateMesh(int32 Iteration)
	{
		if (Iteration > 0) // compact triangles
		{
			int dst = 0;
			loopi(0, Triangles.Num())
				if (!Triangles[i].bDeleted)
				{
					Triangles[dst++] = Triangles[i];
				}
			Triangles.SetNum(dst);
		}

		if (Iteration == 0)
		{
			for (FVertex& Vertex : Vertices)
			{
				Vertex.q = FQuadricMatrix(0.0);
			}

			for (FTriangle& Triangle : Triangles)
			{
				FVector Points[3];
				for (int32 PointIndex = 0; PointIndex < 3; PointIndex++)
				{
					Points[PointIndex] = Vertices[Triangle.Vertices[PointIndex]].Position;
				}
				Triangle.Normal = FVector::CrossProduct(Points[1] - Points[0], Points[2] - Points[0]).GetSafeNormal();
				for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
				{
					Vertices[Triangle.Vertices[VertexIndex]].q =
						Vertices[Triangle.Vertices[VertexIndex]].q + FQuadricMatrix(Triangle.Normal.X, Triangle.Normal.Y, Triangle.Normal.Z, FVector::DotProduct(-Triangle.Normal, Points[0]));
				}
			}

			for (FTriangle& Triangle : Triangles)
			{
				FVector Point;
				for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
				{
					Triangle.err[VertexIndex] = CalculateError(Triangle.Vertices[VertexIndex], Triangle.Vertices[(VertexIndex + 1) % 3], Point);
				}
				Triangle.err[3] = FMath::Min(Triangle.err[0], FMath::Min(Triangle.err[1], Triangle.err[2]));
			}
		}

		for (FVertex& Vertex : Vertices)
		{
			Vertex.tstart = 0;
			Vertex.tcount = 0;
		}

		for (FTriangle& Triangle : Triangles)
		{
			for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
			{
				Vertices[Triangle.Vertices[VertexIndex]].tcount++;
			}
		}

		int tstart = 0;
		for (FVertex& Vertex : Vertices)
		{
			Vertex.tstart = tstart;
			tstart += Vertex.tcount;
			Vertex.tcount = 0;
		}

		Refs.SetNum(Triangles.Num() * 3);
		for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); TriangleIndex++)
		{
			FTriangle& Triangle = Triangles[TriangleIndex];
			for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
			{
				FVertex& Vertex = Vertices[Triangle.Vertices[VertexIndex]];
				Refs[Vertex.tstart + Vertex.tcount].TriangleId = TriangleIndex;
				Refs[Vertex.tstart + Vertex.tcount].TriangleVertexId = VertexIndex;
				Vertex.tcount++;
			}
		}

		// Identify boundary : vertices[].border=0,1
		if (Iteration == 0)
		{
			TArray<int32> vcount;
			TArray<int32>	vids;

			for (FVertex& Vertex : Vertices)
			{
				Vertex.bIsBorder = false;
			}

			loopi(0, Vertices.Num())
			{
				FVertex& v = Vertices[i];
				vcount.Empty();
				vids.Empty();
				for (int32 TriangleIndex = 0; TriangleIndex < v.tcount; TriangleIndex++)
				{
					int k = Refs[v.tstart + TriangleIndex].TriangleId;
					FTriangle& t = Triangles[k];
					for (int32 Index = 0; Index < 3; Index++)
					{
						int ofs = 0, id = t.Vertices[Index];
						while (ofs < vcount.Num())
						{
							if (vids[ofs] == id)
							{
								break;
							}
							ofs++;
						}
						if (ofs == vcount.Num())
						{
							vcount.Add(1);
							vids.Add(id);
						}
						else
							vcount[ofs]++;
					}
				}
				for (int32 VertexIndex = 0; VertexIndex < vcount.Num(); VertexIndex++)
				{
					if (vcount[VertexIndex] == 1)
					{
						Vertices[vids[VertexIndex]].bIsBorder = true;
					}
				}
			}
		}
	}

	void CompactMesh()
	{
		int32 NewSize = 0;
		for (FVertex& Vertex : Vertices)
		{
			Vertex.tcount = 0;
		}
		loopi(0, Triangles.Num())
			if (!Triangles[i].bDeleted)
			{
				FTriangle& t = Triangles[i];
				Triangles[NewSize++] = t;
				for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
				{
					Vertices[t.Vertices[VertexIndex]].tcount = 1;
				}
			}
		Triangles.SetNum(NewSize);
		NewSize = 0;
		loopi(0, Vertices.Num())
			if (Vertices[i].tcount)
			{
				Vertices[i].tstart = NewSize;
				Vertices[NewSize].Position = Vertices[i].Position;
				NewSize++;
			}
		loopi(0, Triangles.Num())
		{
			FTriangle& t = Triangles[i];
			for (int32 VertexIndex = 0; VertexIndex < 3; VertexIndex++)
			{
				t.Vertices[VertexIndex] = Vertices[t.Vertices[VertexIndex]].tstart;
			}
		}
		Vertices.SetNum(NewSize);
	}

	// Error between vertex and Quadric

	double VertexError(FQuadricMatrix q, double x, double y, double z)
	{
		return   q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y
			+ 2 * q[5] * y * z + 2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9];
	}

	// Error for one edge

	double CalculateError(int id_v1, int id_v2, FVector& p_result)
	{
		// compute interpolated vertex

		FQuadricMatrix q = Vertices[id_v1].q + Vertices[id_v2].q;
		bool bIsBorder = Vertices[id_v1].bIsBorder && Vertices[id_v2].bIsBorder;
		double error = 0;
		double det = q.det(0, 1, 2, 1, 4, 5, 2, 5, 7);
		if (det != 0 && !bIsBorder)
		{

			// q_delta is invertible
			p_result.X = -1 / det * (q.det(1, 2, 3, 4, 5, 6, 5, 7, 8));	// vx = A41/det(q_delta)
			p_result.Y = 1 / det * (q.det(0, 2, 3, 1, 5, 6, 2, 7, 8));	// vy = A42/det(q_delta)
			p_result.Z = -1 / det * (q.det(0, 1, 3, 1, 4, 6, 2, 5, 8));	// vz = A43/det(q_delta)

			error = VertexError(q, p_result.X, p_result.Y, p_result.Z);
		}
		else
		{
			// det = 0 -> try to find best result
			FVector p1 = Vertices[id_v1].Position;
			FVector p2 = Vertices[id_v2].Position;
			FVector p3 = (p1 + p2) / 2;
			double error1 = VertexError(q, p1.X, p1.Y, p1.Z);
			double error2 = VertexError(q, p2.X, p2.Y, p2.Z);
			double error3 = VertexError(q, p3.X, p3.Y, p3.Z);
			error = FMath::Min(error1, FMath::Min(error2, error3));
			if (error1 == error) p_result = p1;
			if (error2 == error) p_result = p2;
			if (error3 == error) p_result = p3;
		}
		return error;
	}

protected:
	FglTFRuntimePrimitive& SourcePrimitive;
};