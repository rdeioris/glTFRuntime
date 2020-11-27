// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "glTFRuntimeParser.h"

#define loopi(start_l,end_l) for ( int i=start_l;i<end_l;++i )
#define loopj(start_l,end_l) for ( int j=start_l;j<end_l;++j )

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
		uint32 v[3];
		double err[4];
		int deleted, dirty;
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
		int border;

		FglTFRuntimeUInt16Vector4 Joints;
		FVector4 Weights;

		FVertex()
		{

		}
	};

	struct FRef { int tid, tvertex; };
	std::vector<FTriangle> triangles;
	TArray<FVertex> Vertices;
	std::vector<FRef> refs;

	//
	// Main simplification function
	//
	// target_count  : target nr. of triangles
	// agressiveness : sharpness to increase the threshold.
	//                 5..8 are good numbers
	//                 more iterations yield higher quality
	//

	void SimplifyMesh(FglTFRuntimePrimitive& DestinationPrimitive, const float ReductionFactor, const double Aggressiveness = 7);

	// Check if a triangle flips when this edge is removed

	bool IsFlipped(FVector p, int i0, int i1, FVertex& v0, FVertex& v1, TArray<int32>& Deleted)
	{

		for (int32 k = 0; k < v0.tcount; k++)
		{
			FTriangle& Triangle = triangles[refs[v0.tstart + k].tid];
			if (Triangle.deleted)continue;

			int s = refs[v0.tstart + k].tvertex;
			int id1 = Triangle.v[(s + 1) % 3];
			int id2 = Triangle.v[(s + 2) % 3];

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
			FRef& r = refs[v.tstart + k];
			FTriangle& t = triangles[r.tid];
			if (t.deleted)continue;
			if (Deleted[k])continue;
			FVector p1 = Vertices[t.v[0]].Position;
			FVector p2 = Vertices[t.v[1]].Position;
			FVector p3 = Vertices[t.v[2]].Position;
			t.UV[r.tvertex] = Interpolate(p, p1, p2, p3, t.UV);
		}
	}

	void UpdateVertexNormals(int i0, const FVertex& v, const FVector& p, const TArray<int32>& Deleted)
	{
		for (int32 k = 0; k < v.tcount; k++)
		{
			FRef& r = refs[v.tstart + k];
			FTriangle& t = triangles[r.tid];
			if (t.deleted)continue;
			if (Deleted[k])continue;
			FVector p1 = Vertices[t.v[0]].Position;
			FVector p2 = Vertices[t.v[1]].Position;
			FVector p3 = Vertices[t.v[2]].Position;
			t.UV[r.tvertex] = Interpolate(p, p1, p2, p3, t.UV);
		}
	}

	void UpdateVertexTangents(int i0, const FVertex& v, const FVector& p, const TArray<int32>& Deleted)
	{
		for (int32 k = 0; k < v.tcount; k++)
		{
			FRef& r = refs[v.tstart + k];
			FTriangle& t = triangles[r.tid];
			if (t.deleted)continue;
			if (Deleted[k])continue;
			FVector p1 = Vertices[t.v[0]].Position;
			FVector p2 = Vertices[t.v[1]].Position;
			FVector p3 = Vertices[t.v[2]].Position;
			t.Tangents[r.tvertex] = Interpolate(p, p1, p2, p3, t.Tangents);
		}
	}

	void UpdateVertexColors(int i0, const FVertex& v, const FVector& p, const TArray<int32>& Deleted)
	{
		for (int32 k = 0; k < v.tcount; k++)
		{
			FRef& r = refs[v.tstart + k];
			FTriangle& t = triangles[r.tid];
			if (t.deleted)continue;
			if (Deleted[k])continue;
			FVector p1 = Vertices[t.v[0]].Position;
			FVector p2 = Vertices[t.v[1]].Position;
			FVector p3 = Vertices[t.v[2]].Position;
			t.Colors[r.tvertex] = Interpolate(p, p1, p2, p3, t.Colors);
		}
	}

	// Update triangle connections and edge error after a edge is collapsed

	void UpdateTriangles(int i0, FVertex& v, const TArray<int32>& Deleted, int32& DeletedTriangles)
	{
		FVector p;
		for (int32 k = 0; k < v.tcount; k++)
		{
			FRef& r = refs[v.tstart + k];
			FTriangle& t = triangles[r.tid];
			if (t.deleted)continue;
			if (Deleted[k])
			{
				t.deleted = 1;
				DeletedTriangles++;
				continue;
			}
			t.v[r.tvertex] = i0;
			t.dirty = 1;
			t.err[0] = CalculateError(t.v[0], t.v[1], p);
			t.err[1] = CalculateError(t.v[1], t.v[2], p);
			t.err[2] = CalculateError(t.v[2], t.v[0], p);
			t.err[3] = FMath::Min(t.err[0], FMath::Min(t.err[1], t.err[2]));
			refs.push_back(r);
		}
	}

	// compact triangles, compute edge error and build reference list

	void update_mesh(int iteration)
	{
		if (iteration > 0) // compact triangles
		{
			int dst = 0;
			loopi(0, triangles.size())
				if (!triangles[i].deleted)
				{
					triangles[dst++] = triangles[i];
				}
			triangles.resize(dst);
		}
		//
		// Init Quadrics by Plane & Edge Errors
		//
		// required at the beginning ( iteration == 0 )
		// recomputing during the simplification is not required,
		// but mostly improves the result for closed meshes
		//
		if (iteration == 0)
		{
			loopi(0, Vertices.Num())
				Vertices[i].q = FQuadricMatrix(0.0);

			loopi(0, triangles.size())
			{
				FTriangle& t = triangles[i];
				FVector p[3];
				loopj(0, 3) p[j] = Vertices[t.v[j]].Position;
				t.Normal = FVector::CrossProduct(p[1] - p[0], p[2] - p[0]).GetSafeNormal();
				loopj(0, 3) Vertices[t.v[j]].q =
					Vertices[t.v[j]].q + FQuadricMatrix(t.Normal.X, t.Normal.Y, t.Normal.Z, FVector::DotProduct(-t.Normal, p[0]));
			}
			loopi(0, triangles.size())
			{
				// Calc Edge Error
				FTriangle& t = triangles[i]; FVector p;
				loopj(0, 3) t.err[j] = CalculateError(t.v[j], t.v[(j + 1) % 3], p);
				t.err[3] = FMath::Min(t.err[0], FMath::Min(t.err[1], t.err[2]));
			}
		}

		// Init Reference ID list
		loopi(0, Vertices.Num())
		{
			Vertices[i].tstart = 0;
			Vertices[i].tcount = 0;
		}
		loopi(0, triangles.size())
		{
			FTriangle& t = triangles[i];
			loopj(0, 3) Vertices[t.v[j]].tcount++;
		}
		int tstart = 0;
		loopi(0, Vertices.Num())
		{
			FVertex& v = Vertices[i];
			v.tstart = tstart;
			tstart += v.tcount;
			v.tcount = 0;
		}

		// Write References
		refs.resize(triangles.size() * 3);
		loopi(0, triangles.size())
		{
			FTriangle& t = triangles[i];
			loopj(0, 3)
			{
				FVertex& v = Vertices[t.v[j]];
				refs[v.tstart + v.tcount].tid = i;
				refs[v.tstart + v.tcount].tvertex = j;
				v.tcount++;
			}
		}

		// Identify boundary : vertices[].border=0,1
		if (iteration == 0)
		{
			std::vector<int> vcount, vids;

			loopi(0, Vertices.Num())
				Vertices[i].border = 0;

			loopi(0, Vertices.Num())
			{
				FVertex& v = Vertices[i];
				vcount.clear();
				vids.clear();
				loopj(0, v.tcount)
				{
					int k = refs[v.tstart + j].tid;
					FTriangle& t = triangles[k];
					for (int32 Index = 0; Index < 3; Index++)
					{
						int ofs = 0, id = t.v[Index];
						while (ofs < vcount.size())
						{
							if (vids[ofs] == id)break;
							ofs++;
						}
						if (ofs == vcount.size())
						{
							vcount.push_back(1);
							vids.push_back(id);
						}
						else
							vcount[ofs]++;
					}
				}
				loopj(0, vcount.size()) if (vcount[j] == 1)
					Vertices[vids[j]].border = 1;
			}
		}
	}

	// Finally compact mesh before exiting

	void CompactMesh()
	{
		int32 NewSize = 0;
		for (FVertex& Vertex : Vertices)
		{
			Vertex.tcount = 0;
		}
		loopi(0, triangles.size())
			if (!triangles[i].deleted)
			{
				FTriangle& t = triangles[i];
				triangles[NewSize++] = t;
				loopj(0, 3)Vertices[t.v[j]].tcount = 1;
			}
		triangles.resize(NewSize);
		NewSize = 0;
		loopi(0, Vertices.Num())
			if (Vertices[i].tcount)
			{
				Vertices[i].tstart = NewSize;
				Vertices[NewSize].Position = Vertices[i].Position;
				NewSize++;
			}
		loopi(0, triangles.size())
		{
			FTriangle& t = triangles[i];
			loopj(0, 3)t.v[j] = Vertices[t.v[j]].tstart;
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
		bool   border = Vertices[id_v1].border & Vertices[id_v2].border;
		double error = 0;
		double det = q.det(0, 1, 2, 1, 4, 5, 2, 5, 7);
		if (det != 0 && !border)
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