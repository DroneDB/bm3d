/****************************************************************************
* VCGLib                                                            o o     *
* Visual and Computer Graphics Library                            o     o   *
*                                                                _   O  _   *
* Copyright(C) 2004-2016                                           \/)\/    *
* Visual Computing Lab                                            /\/|      *
* ISTI - Italian National Research Council                           |      *
*                                                                    \      *
* All rights reserved.                                                      *
*                                                                           *
* This program is free software; you can redistribute it and/or modify      *
* it under the terms of the GNU General Public License as published by      *
* the Free Software Foundation; either version 2 of the License, or         *
* (at your option) any later version.                                       *
*                                                                           *
* This program is distributed in the hope that it will be useful,           *
* but WITHOUT ANY WARRANTY; without even the implied warranty of            *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
* GNU General Public License (http://www.gnu.org/licenses/gpl.txt)          *
* for more details.                                                         *
*                                                                           *
****************************************************************************/
#ifndef __VCGLIB_QUADRIC
#define __VCGLIB_QUADRIC

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <vcg/space/point3.h>
#include <vcg/space/plane3.h>
#include <vcg/math/matrix33.h>

namespace vcg {
namespace math {

/* Solves A x = b for a 3x3 matrix by LU decomposition with full pivoting, in the very order of
 * operations (and the same zero tests) of Eigen's FullPivLU::solve, so that the solution is the
 * same to the last bit: the largest remaining coefficient (the first one in column order wins a
 * tie) is the pivot, the rank counts the pivots above 3 * epsilon times the first one, the
 * triangular systems are solved column by column, and a column whose right-hand side is zero is
 * skipped. Unknowns of a singular matrix that no pivot reaches are 0.
 */
inline void SolveFullPivot3x3(const double A[3][3], const double b[3], double x[3])
{
  double lu[3][3];
  std::memcpy(lu, A, sizeof(lu));
  int rowSwap[3] = {0, 1, 2}, colSwap[3] = {0, 1, 2};
  int pivots = 3;
  double maxPivot = 0;
  for(int k = 0; k < 3; ++k)
  {
    int pr = k, pc = k;
    double biggest = std::fabs(lu[k][k]);
    for(int c = k; c < 3; ++c)
      for(int r = k; r < 3; ++r)
      {
        const double v = std::fabs(lu[r][c]);
        if(v > biggest) { biggest = v; pr = r; pc = c; }
      }
    if(biggest == 0) { pivots = k; break; }
    if(biggest > maxPivot) maxPivot = biggest;
    rowSwap[k] = pr;
    colSwap[k] = pc;
    if(pr != k) for(int c = 0; c < 3; ++c) std::swap(lu[k][c], lu[pr][c]);
    if(pc != k) for(int r = 0; r < 3; ++r) std::swap(lu[r][k], lu[r][pc]);
    for(int r = k + 1; r < 3; ++r) lu[r][k] /= lu[k][k];
    for(int c = k + 1; c < 3; ++c)
      for(int r = k + 1; r < 3; ++r)
        lu[r][c] -= lu[r][k] * lu[k][c];
  }
  const double threshold = maxPivot * (std::numeric_limits<double>::epsilon() * 3);
  int rank = 0;
  for(int i = 0; i < pivots; ++i) rank += std::fabs(lu[i][i]) > threshold;

  x[0] = x[1] = x[2] = 0;
  if(rank == 0) return;
  double c[3] = {b[0], b[1], b[2]};
  for(int k = 0; k < 3; ++k) std::swap(c[k], c[rowSwap[k]]);
  for(int i = 0; i < 3; ++i)
    if(c[i] != 0)
      for(int j = i + 1; j < 3; ++j) c[j] -= c[i] * lu[j][i];
  for(int i = rank - 1; i >= 0; --i)
    if(c[i] != 0)
    {
      c[i] /= lu[i][i];
      for(int j = 0; j < i; ++j) c[j] -= c[i] * lu[j][i];
    }
  int unknown[3] = {0, 1, 2};
  for(int k = 0; k < 3; ++k) std::swap(unknown[k], unknown[colSwap[k]]);
  for(int i = 0; i < rank; ++i) x[unknown[i]] = c[i];
}

/* Whether the minimum of a quadric is a place the vertex of a collapse can go to: every one of the
 * n coordinates of place lies in the box of the edge (the box of its two ends, a and b) made
 * larger on every side by EdgePlacementSlack times the length of the edge (the length of the part
 * of the edge in these n coordinates) and by the rounding of a float of the ends. The minimum is
 * exact only when the planes of the quadric meet at one point that is well determined; where they
 * are nearly parallel (a flat or a gently curved region, planes that differ by the noise of the
 * input) the system is solvable to the precision of the residual and still puts the minimum far
 * from the edge, on a part of the plane that is not there, and the surface grows a spike outside
 * the model. A place that is not near the edge is not used: the caller takes the best of the ends
 * and the middle. A coordinate that is not a number is outside.
 */
const double EdgePlacementSlack = 1.0;  // as a fraction of the length of the edge

inline bool IsNearEdge(const double *place, const double *a, const double *b, int n, double length)
{
  const double rounding = 16 * double(std::numeric_limits<float>::epsilon());
  for(int i = 0; i < n; ++i)
  {
    const double slack = EdgePlacementSlack * length + rounding * std::max(std::fabs(a[i]), std::fabs(b[i]));
    const double low = std::min(a[i], b[i]) - slack;
    const double high = std::max(a[i], b[i]) + slack;
    if(!(place[i] >= low && place[i] <= high)) return false;
  }
  return true;
}

/*
 *  This class encode a quadric function
 *  f(x) = xAx +bx + c
 *  where A is a symmetric 3x3 matrix, b a vector and c a scalar constant.  
 */ 
template<typename  _ScalarType>
class Quadric
{
public:
  typedef _ScalarType ScalarType;
  ScalarType a[6];		// Symmetric Matrix 3x3 : a11 a12 a13 a22 a23 a33
  ScalarType b[3];		// Vector r3
  ScalarType c;			  // Scalar (-1 means null/un-initialized quadric)
  
  inline Quadric() { c = -1; }
  
  bool IsValid() const { return c>=0; }
  void SetInvalid() { c = -1.0; }
  
   // Initialize the quadric to keep the squared distance from a given Plane  
  template< class PlaneType >
  void ByPlane( const PlaneType & p )
  {
    a[0] =  (ScalarType)p.Direction()[0]*p.Direction()[0];	// a11
    a[1] =  (ScalarType)p.Direction()[1]*p.Direction()[0];	// a12 (=a21)
    a[2] =  (ScalarType)p.Direction()[2]*p.Direction()[0];	// a13 (=a31)
    a[3] =  (ScalarType)p.Direction()[1]*p.Direction()[1];	// a22
    a[4] =  (ScalarType)p.Direction()[2]*p.Direction()[1];	// a23 (=a32)
    a[5] =  (ScalarType)p.Direction()[2]*p.Direction()[2];	// a33
    b[0] =  (ScalarType)(-2.0)*p.Offset()*p.Direction()[0];
    b[1] =  (ScalarType)(-2.0)*p.Offset()*p.Direction()[1];
    b[2] =  (ScalarType)(-2.0)*p.Offset()*p.Direction()[2];
    c    =  (ScalarType)p.Offset()*p.Offset();
  }
  
  /* 
   * Initializes the quadric as the squared distance from a given line.
   * Note that this code also works for a vcg::Ray<T>, even though the (squared) distance
   * from a ray is different "before" its origin.
   */
  template< class LineType >
  void ByLine( const LineType & r ) // Init dato un raggio
  {
    ScalarType K = (ScalarType)(r.Origin()*r.Direction());
    a[0] = (ScalarType)1.0-r.Direction()[0]*r.Direction()[0]; // a11
    a[1] = (ScalarType)-r.Direction()[0]*r.Direction()[1]; // a12 (=a21)
    a[2] = (ScalarType)-r.Direction()[0]*r.Direction()[2]; // a13 (=a31)
    a[3] = (ScalarType)1.0-r.Direction()[1]*r.Direction()[1]; // a22
    a[4] = (ScalarType)-r.Direction()[1]*r.Direction()[2]; // a23 (=a32)
    a[5] = (ScalarType)1.0-r.Direction()[2]*r.Direction()[2]; // a33
    b[0] = (ScalarType)2.0*(r.Direction()[0]*K - r.Origin()[0]);
    b[1] = (ScalarType)2.0*(r.Direction()[1]*K - r.Origin()[1]);
    b[2] = (ScalarType)2.0*(r.Direction()[2]*K - r.Origin()[2]);
    c = -K*K + (ScalarType)(r.Origin()*r.Origin());
  }

  /*
   * Initializes the quadric as the squared distance from a given point.
   *
   */
  template< class CoordType >
  void ByPoint( const CoordType & p ) // Init dato un raggio
  {
    a[0] = 1; // a11
    a[1] = 0; // a12 (=a21)
    a[2] = 0; // a13 (=a31)
    a[3] = 1; // a22
    a[4] = 0; // a23 (=a32)
    a[5] = 1; // a33
    b[0] = (ScalarType)-2.0*((ScalarType)p.X());
    b[1] = (ScalarType)-2.0*((ScalarType)p.Y());
    b[2] = (ScalarType)-2.0*((ScalarType)p.Z());
    c = pow(p.X(),2) + pow(p.Y(),2)+ pow(p.Z(),2);
  }

  void SetZero()
  {
    a[0] = 0;
    a[1] = 0;
    a[2] = 0;
    a[3] = 0;
    a[4] = 0;
    a[5] = 0;
    b[0] = 0;
    b[1] = 0;
    b[2] = 0;
    c    = 0;
  }
  
  void operator = ( const Quadric & q )
  {
    assert( q.IsValid() );
    
    a[0] = q.a[0];
    a[1] = q.a[1];
    a[2] = q.a[2];
    a[3] = q.a[3];
    a[4] = q.a[4];
    a[5] = q.a[5];
    b[0] = q.b[0];
    b[1] = q.b[1];
    b[2] = q.b[2];
    c    = q.c;
  }
  
  void operator += ( const Quadric & q )
  {
    assert( IsValid() );
    assert( q.IsValid() );
    
    a[0] += q.a[0];
    a[1] += q.a[1];
    a[2] += q.a[2];
    a[3] += q.a[3];
    a[4] += q.a[4];
    a[5] += q.a[5];
    b[0] += q.b[0];
    b[1] += q.b[1];
    b[2] += q.b[2];
    c    += q.c;
  }
  
  void operator *= ( const ScalarType & w )			// Amplifica una quadirca
  {
    assert( IsValid() );
    
    a[0] *= w;
    a[1] *= w;
    a[2] *= w;
    a[3] *= w;
    a[4] *= w;
    a[5] *= w;
    b[0] *= w;
    b[1] *= w;
    b[2] *= w;
    c    *= w;
  }
  
  
  
  /* Evaluate a quadric over a point p.
   */
  template <class ResultScalarType>
  ResultScalarType Apply( const Point3<ResultScalarType> & p ) const
  {
    assert( IsValid() );
    return ResultScalarType (
            p[0]*p[0]*a[0] + 2*p[0]*p[1]*a[1] + 2*p[0]*p[2]*a[2] + p[0]*b[0]
        +   p[1]*p[1]*a[3] + 2*p[1]*p[2]*a[4] + p[1]*b[1]
        +   p[2]*p[2]*a[5] + p[2]*b[2]	+ c);
  }
  
  
  static double &RelativeErrorThr()
  {
    static double _err = 0.000001;
    return _err;
  }
  
  // Find the point minimizing the quadric xAx + bx + c 
  // by solving the first derivative 2 Ax + b = 0 
  // return true if the found solution fits the system. 
  
  template <class ReturnScalarType>
  bool Minimum(Point3<ReturnScalarType> &x)
  {
    const double A[3][3] = { {double(a[0]), double(a[1]), double(a[2])},
                             {double(a[1]), double(a[3]), double(a[4])},
                             {double(a[2]), double(a[4]), double(a[5])} };
    const double be[3] = { double(-b[0]/2), double(-b[1]/2), double(-b[2]/2) };
    double xe[3];
    SolveFullPivot3x3(A, be, xe);
    // Check relative error of solution xe
    double r[3];
    for(int i=0;i<3;++i)
    {
      double sum = 0;
      for(int j=0;j<3;++j) sum += A[i][j]*xe[j];
      r[i] = sum - be[i];
    }
    double error = std::sqrt(r[0]*r[0] + (r[1]*r[1] + r[2]*r[2]));
    double bnorm = std::sqrt(be[0]*be[0] + (be[1]*be[1] + be[2]*be[2]));
    if(error > bnorm * Quadric<ScalarType>::RelativeErrorThr() )
      return false;
    
    x[0] = ReturnScalarType(xe[0]);
    x[1] = ReturnScalarType(xe[1]);
    x[2] = ReturnScalarType(xe[2]);
    return true;
  }
  
};

typedef Quadric<short>  Quadrics;
typedef Quadric<int>	  Quadrici;
typedef Quadric<float>  Quadricf;
typedef Quadric<double> Quadricd;



	} // end namespace math
} // end namespace vcg

#endif
