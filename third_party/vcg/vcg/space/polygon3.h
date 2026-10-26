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

#ifndef POLYGON_H
#define POLYGON_H

#include <vcg/space/plane3.h>
#include <vcg/math/matrix33.h>
#include <vcg/space/distance3.h>

namespace vcg {

////return true if the
//template <class CoordType>
//bool CheckNormalizedCoords(CoordType dir)
//{
//    typedef typename CoordType::ScalarType ScalarType;
//    if(isnan(dir.X()))return false;
//    if(isnan(dir.Y()))return false;
//    if(isnan(dir.Z()))return false;
//    ScalarType Norm=dir.Norm();
//    if(fabs(Norm-1.f)>0.01f)return false;
//    return true;
//}

//return per vertex Normals of a polygonal face stored as a vector of coords
template <class CoordType>
void GetNormals(std::vector<CoordType> &Pos,
                std::vector<CoordType> &Norms)
{
    Norms.clear();
    int size=Pos.size();
    if (size<=2) return;
    for (int i=0;i<size;i++)
        Norms.push_back(Normal(Pos[i],Pos[(i+1)%size],Pos[(i+2)%size]).Normalize());
}

//return the normal of a polygonal face stored as a vector of coords
template <class CoordType>
CoordType Normal(std::vector<CoordType> &Pos)
{
    std::vector<CoordType> Norms;
    GetNormals(Pos,Norms);
    if (Norms.size()==0)
        return(CoordType(1,0,0));

    CoordType NSum=CoordType(0,0,0);
    for (size_t i=0;i<Norms.size();i++)
        NSum+=Norms[i];

    NSum.Normalize();
    return (NSum);
}

//return the area of a polygonal face stored as a vector of coords
template <class CoordType>
typename CoordType::ScalarType Area(const std::vector<CoordType> &Pos)
{
    typedef typename CoordType::ScalarType ScalarType;
    CoordType bary=CoordType(0,0,0);
    for (size_t i=0;i<Pos.size();i++)
        bary+=Pos[i];

    bary/=Pos.size();
    ScalarType Area=0;
    for (size_t i=0;i<Pos.size();i++)
    {
        CoordType p0=Pos[i];
        CoordType p1=Pos[(i+1)% Pos.size()];
        CoordType p2=bary;
        vcg::Triangle3<ScalarType> T(p0,p1,p2);
        Area+=(vcg::DoubleArea(T)/2);
    }
    return Area;
}

//return per vertex Normals of a polygonal face
template<class PolygonType>
void PolyNormals(const PolygonType &F,
                 std::vector<typename PolygonType::CoordType> &Norms)
{
    Norms.clear();
    if (F.VN()<=2) return;
    for (int i=0;i<F.VN();i++)
        Norms.push_back(Normal(F.cP0(i),F.cP1(i),F.cP2(i)).Normalize());
}

//return the barycenter of a polygonal face
template<class PolygonType>
typename PolygonType::CoordType PolyBarycenter(const PolygonType &F)
{
    typename PolygonType::CoordType bary(0,0,0);
    for (int i=0;i<F.VN();i++)
        bary+=F.cP(i);

    bary/=(typename PolygonType::ScalarType)F.VN();
    return bary;
}

//return the area of a polygonal face
template<class PolygonType>
typename PolygonType::ScalarType PolyArea(const PolygonType &F)
{
    typedef typename PolygonType::CoordType CoordType;
    typedef typename PolygonType::ScalarType ScalarType;

	if (F.VN() == 3)
		return vcg::DoubleArea(F) / 2;

    CoordType bary=PolyBarycenter(F);
    ScalarType Area=0;
    for (size_t i=0;i<(size_t)F.VN();i++)
    {
        CoordType p0=F.cP0(i);
        CoordType p1=F.cP1(i);
        CoordType p2=bary;
        vcg::Triangle3<ScalarType> T(p0,p1,p2);
        Area+=(vcg::DoubleArea(T)/2);
    }
    return Area;
}

//return the normal of a polygonal face
template<class PolygonType>
typename PolygonType::CoordType PolygonNormal(const PolygonType &F)
{
    typename PolygonType::CoordType n(0,0,0);

    for (int i=0;i<F.VN();i++)
        n+=Normal(F.cP0(i),F.cP1(i),F.cP2(i)).Normalize();

    return n.Normalize();
}

//return the perimeter of a polygonal face
template<class PolygonType>
typename PolygonType::ScalarType PolyPerimeter(const PolygonType &F)
{
    typedef typename PolygonType::ScalarType ScalarType;

    ScalarType SumL=0;
    for (int i=0;i<F.VN();i++)
    {
        ScalarType L=(F.cP0(i)-F.cP1(i)).Norm();
        SumL+=L;
    }
    return (SumL);
}

//return a Scalar value that encode the variance of the normals
//wrt the average one (1 means hight variance, 0 no variance)
template<class PolygonType>
typename PolygonType::ScalarType PolyNormDeviation(const PolygonType &F)
{
    typedef typename PolygonType::CoordType CoordType;
    typedef typename PolygonType::ScalarType ScalarType;

    std::vector<CoordType> Norms;
    PolyNormals(F,Norms);

    //calculate the Avg Normal
    CoordType AvgNorm(0,0,0);
    for (int i=0;i<Norms.size();i++)
        AvgNorm+=Norms[i];

    AvgNorm.Normalize();

    //if (!CheckNormalizedCoords(AvgNorm))return 1;

    ScalarType Dev=0;
    for (int i=0;i<Norms.size();i++)
        Dev+=pow((Norms[i]-AvgNorm).Norm()/2.0,2);

    Dev/=(ScalarType)Norms.size();
    Dev=sqrt(Dev);
    return Dev;
}

//return a Scalar value that encode the distance wrt ideal angle for each
//wrt the average one (1 correspond to hight variance, 0 no variance)
template<class PolygonType>
void PolyAngleDeviation(const PolygonType &F,
                        typename PolygonType::ScalarType &AvgDev,
                        typename PolygonType::ScalarType &MaxDev)
{
    typedef typename PolygonType::CoordType CoordType;
    typedef typename PolygonType::ScalarType ScalarType;
    assert(F.VN()>2);
    ScalarType IdealAngle=M_PI-(2*M_PI/(ScalarType)F.VN());
    assert(IdealAngle>0);

    //then compute the angle deviation
    MaxDev=0;
    AvgDev=0;

    for (int i=0;i<F.VN();i++)
    {
        CoordType dir0=F.cP0(i)-F.cP1(i);
        CoordType dir1=F.cP2(i)-F.cP1(i);

        ScalarType VAngle=vcg::Angle(dir0,dir1);
        assert(VAngle>=0);
        ScalarType VAngleDiff=fabs(VAngle-IdealAngle);

        if (VAngleDiff>MaxDev)MaxDev=VAngleDiff;

        AvgDev+=VAngleDiff;
    }
    AvgDev/=(ScalarType)F.VN();

    AvgDev/=(M_PI/2.0);
    MaxDev/=(M_PI/2.0);

    if (AvgDev>1)AvgDev=1;
    if (MaxDev>1)MaxDev=1;
}

//return the base template polygon as
//described by "Static Aware Grid Shells" by Pietroni et Al.
template<class CoordType>
void getBaseTemplatePolygon(int N,
                            std::vector<CoordType> &TemplatePos)
{
    typedef typename CoordType::ScalarType ScalarType;
    ///first find positions in the
    ///reference frame of the passed matrix
    ScalarType AngleInterval=2.0*M_PI/(ScalarType)N;
    ScalarType CurrAngle=0;
    TemplatePos.resize(N);
    for (size_t i=0;i<TemplatePos.size();i++)
    {
        ///find with trigonometric functions
        TemplatePos[i].X()=cos(CurrAngle);
        TemplatePos[i].Y()=sin(CurrAngle);
        TemplatePos[i].Z()=0;
        //            TemplatePos[i].Normalize();
        //            TemplatePos[i].X()*=Anisotropy;
        //            TemplatePos[i].Y()*=(1-Anisotropy);

        ///increment the angle
        CurrAngle+=AngleInterval;
    }
}

template<class PolygonType>
typename PolygonType::ScalarType PolygonPointDistance(const PolygonType &F,
                                                      const vcg::Point3<typename PolygonType::ScalarType> &pos,
                                                      vcg::Point3<typename PolygonType::ScalarType> &ClosestP,
                                                      typename PolygonType::ScalarType minD = std::numeric_limits<typename PolygonType::ScalarType>::max())
{
    typedef typename PolygonType::ScalarType ScalarType;
    typedef typename PolygonType::CoordType CoordType;

    CoordType bary=vcg::PolyBarycenter(F);
    for (size_t j=0;j<F.VN();j++)
    {
        vcg::Triangle3<ScalarType> T(F.cP0(j),F.cP1(j),bary);
        ScalarType dist;
        CoordType closest;
        vcg::TrianglePointDistance(T,pos,dist,closest);
        if (dist>minD)continue;
        minD=dist;
        ClosestP=closest;
    }
    return minD;
}

template<class PolygonType>
vcg::Box3<typename PolygonType::ScalarType> PolygonBox(const PolygonType &F)
{
    typedef typename PolygonType::ScalarType ScalarType;
    vcg::Box3<ScalarType> bb;
    for (size_t j=0;j<F.VN();j++)
        bb.Add(F.V(j)->P());
    return bb;
}

template<class PolygonType>
typename PolygonType::ScalarType PolygonTorsion(const PolygonType &F,int side)
{
    typedef typename PolygonType::CoordType CoordType;
    typedef typename PolygonType::ScalarType ScalarType;

    assert(side>=0);
    assert(side<2);
    assert(F.VN()==4);

    //get firts two edges directions
    CoordType Dir0,Dir1;
    if (side==0)
    {
        Dir0=F.cP(1)-F.cP(0);
        Dir1=F.cP(2)-F.cP(3);
    }
    else
    {
        Dir0=F.cP(2)-F.cP(1);
        Dir1=F.cP(3)-F.cP(0);
    }

    Dir0.Normalize();
    Dir1.Normalize();

    //then make them lying on face's Normal
    CoordType DirPlane0=Dir0*0.5+Dir1*0.5;
    CoordType DirPlane1=F.cN();
    CoordType NormPlane=DirPlane0^DirPlane1;
    NormPlane.Normalize();
    CoordType subV0=NormPlane*(NormPlane*Dir0);
    CoordType subV1=NormPlane*(NormPlane*Dir1);
    Dir0-=subV0;
    Dir1-=subV1;
    Dir0.Normalize();
    Dir1.Normalize();
    ScalarType AngleVal=vcg::Angle(Dir0,Dir1);
    return AngleVal;
}

template<class PolygonType>
typename PolygonType::ScalarType PolygonBending(const PolygonType &F,int side)
{
    typedef typename PolygonType::CoordType CoordType;
    typedef typename PolygonType::ScalarType ScalarType;

    assert(side>=0);
    assert(side<2);
    assert(F.VN()==4);

    //get firts two edges directions
    CoordType Norm0,Norm1;
    CoordType Avg0,Avg1;
    if (side==0)
    {
        Norm0=F.V(0)->N()*0.5+F.V(1)->N()*0.5;
        Avg0=F.cP(0)*0.5+F.cP(1)*0.5;
        Norm1=F.V(2)->N()*0.5+F.V(3)->N()*0.5;
        Avg1=F.cP(2)*0.5+F.cP(3)*0.5;
    }
    else
    {
        Norm0=F.V(2)->N()*0.5+F.V(1)->N()*0.5;
        Avg0=F.cP(2)*0.5+F.cP(1)*0.5;
        Norm1=F.V(3)->N()*0.5+F.V(0)->N()*0.5;
        Avg1=F.cP(3)*0.5+F.cP(0)*0.5;
    }

    Norm0.Normalize();
    Norm1.Normalize();

    //then make them lying on face's Normal
    CoordType DirPlane0=Avg0-Avg1;
    DirPlane0.Normalize();
    CoordType DirPlane1=F.cN();
    CoordType NormPlane=DirPlane0^DirPlane1;
    NormPlane.Normalize();
    CoordType subV0=NormPlane*(NormPlane*Norm0);
    CoordType subV1=NormPlane*(NormPlane*Norm1);
    Norm0-=subV0;
    Norm1-=subV1;
    Norm0.Normalize();
    Norm1.Normalize();
    ScalarType AngleVal=vcg::Angle(Norm0,Norm1);
    return AngleVal;
}

template<class PolygonType>
typename PolygonType::ScalarType PolygonBending(const PolygonType &F)
{
    typedef typename PolygonType::ScalarType ScalarType;
    ScalarType Bend0=PolygonBending(F,0);
    ScalarType Bend1=PolygonBending(F,1);
    assert(Bend0>=0);
    assert(Bend1>=0);
    return (std::max(Bend0,Bend1));
}

template<class PolygonType>
typename PolygonType::ScalarType PolygonTorsion(const PolygonType &F)
{
    typedef typename PolygonType::ScalarType ScalarType;
    ScalarType Torsion0=PolygonTorsion(F,0);
    ScalarType Torsion1=PolygonTorsion(F,1);
    assert(Torsion0>=0);
    assert(Torsion1>=0);
    return (std::max(Torsion0,Torsion1));
}

}
#endif // POLYGON_H
