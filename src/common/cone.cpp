/*
Nexus

Copyright(C) 2012 - Federico Ponchio
ISTI - Italian National Research Council - Visual Computing Lab

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License (http://www.gnu.org/licenses/gpl.txt)
for more details.
*/
#include "cone.h"

using namespace std;
using namespace vcg;
using namespace bm3d::detail;

AnchoredCone3f::AnchoredCone3f() {
	scaledNormal = Point3f(0,0,0);
	frontAnchor = Point3f(0,0,0);
	backAnchor = Point3f(0,0,0);
}

bool AnchoredCone3f::Frontface(const Point3f &viewPoint) {
	Point3f d = frontAnchor - viewPoint; //Vector from viewPoint to frontAnchor.
	float f = - d * scaledNormal;
	if (f < 0.001 || f * f < d * d)
		return false;
	return true;
}

bool AnchoredCone3f::Backface(const Point3f &viewPoint) {
	Point3f d = backAnchor - viewPoint; //Vector from viewPoint to frontAnchor.
	float f = d * scaledNormal;
	if (f < 0.001 || f * f < d * d)
		return false;
	return true;
}

//The cone of a node is found by putting the normals into kConeRings rings of equal angle around
//their mean, ring k holding the angles in [k, k+1) * pi / 49 (ring 49 only the opposite direction),
//and taking the smallest number of rings that holds the share `threshold` of them. The angles are
//compared through their cosines, and the half angle of the cone through its sine, with constants:
//acos and cos of the mathematical library differ in the last bit between platforms, and a normal
//moved to the next ring, or a sine one bit apart, would change the cone in the file.
static const int kConeRings = 50;

//kRingCosine[k-1] = cos(k * pi / 49), k = 1..49, each the double nearest to the exact value
static const double kRingCosine[kConeRings - 1] = {
	0.9979453927503363, 0.9917900138232462, 0.9815591569910653, 0.9672948630390295,
	0.9490557470106686, 0.9269167573460217, 0.9009688679024191, 0.8713187041233893,
	0.8380881048918407, 0.8014136218679566, 0.7614459583691344, 0.7183493500977276,
	0.6723008902613168, 0.6234898018587335, 0.5721166601221697, 0.518392568310525,
	0.46253829024083526, 0.40478334312239383, 0.3453650544213076, 0.28452758663103245,
	0.2225209339563144, 0.15959989503337924, 0.09602302590768176, 0.03205157757165517,
	-0.03205157757165517, -0.09602302590768176, -0.15959989503337924, -0.2225209339563144,
	-0.28452758663103245, -0.3453650544213076, -0.40478334312239383, -0.46253829024083526,
	-0.518392568310525, -0.5721166601221697, -0.6234898018587335, -0.6723008902613168,
	-0.7183493500977276, -0.7614459583691344, -0.8014136218679566, -0.8380881048918407,
	-0.8713187041233893, -0.9009688679024191, -0.9269167573460217, -0.9490557470106686,
	-0.9672948630390295, -0.9815591569910653, -0.9917900138232462, -0.9979453927503363,
	-1.0,
};

//The cones up to the ring kConeWidestRing; a wider one culls nothing and is stored as no cone
static const int kConeWidestRing = 22;
//kRingSine[k] = sin(pi * (k + 1) / 50), k = 0..kConeWidestRing: the sine of the half angle of a
//cone of k + 1 rings, each the double nearest to the exact value
static const double kRingSine[kConeWidestRing + 1] = {
	0.06279051952931337, 0.12533323356430426, 0.18738131458572463, 0.2486898871648548,
	0.30901699437494745, 0.368124552684678, 0.42577929156507266, 0.48175367410171527,
	0.5358267949789967, 0.5877852522924731, 0.6374239897486897, 0.6845471059286887,
	0.7289686274214116, 0.7705132427757893, 0.8090169943749475, 0.8443279255020151,
	0.8763066800438636, 0.9048270524660196, 0.9297764858882515, 0.9510565162951535,
	0.9685831611286311, 0.9822872507286887, 0.9921147013144779,
};

//The ring of the direction n around the unit axis, or -1 for a direction of no length: the number
//of ring boundaries the angle between them reaches
int bm3d::detail::coneRing(const Point3f &axis, const Point3f &n) {
	const float w = axis.Norm()*n.Norm();
	if(w == 0)
		return -1;
	const float t = (axis*n)/w;
	int ring = 0;
	while(ring < kConeRings - 1 && t <= kRingCosine[ring])
		ring++;
	return ring;
}

//The axis of a cone of the rings 0..widest: the unit mean scaled by 1/sin of the half angle (the
//file stores that length), or no axis when the cone is too wide to cull anything
static Point3f coneAxis(const Point3f &unitMean, int widest) {
	if(widest > kConeWidestRing)
		return Point3f(0, 0, 0);
	return unitMean / float(kRingSine[widest]);
}

void AnchoredCone3f::AddNormals(vector<Point3f> &normal, vector<float> &area, float threshold) {
	assert(normal.size() == area.size());
	//compute average normal
	scaledNormal = Point3f(0,0,0);
	for(auto &n: normal)
		scaledNormal += n;
	scaledNormal.Normalize();

	double distr[kConeRings];
	for(int k = 0; k < kConeRings; k++)
		distr[k] = 0;

	double tot_area = 0;
	for(size_t i = 0; i < normal.size(); i++) {
		const int ring = coneRing(scaledNormal, normal[i]);
		if(ring < 0) continue;
		distr[ring] += area[i];
		tot_area += area[i];
	}

	float tot = 0;
	int best;
	for(best = 0; best < kConeRings; best++) {
		tot += (float)distr[best];
		if(tot > threshold * tot_area)
			break;
	}
	scaledNormal = coneAxis(scaledNormal, best);
}

void AnchoredCone3f::AddNormals(vector<Point3f> &normal, float threshold) {
	assert(normal.size() > 0);
	scaledNormal = Point3f(0,0,0);
	int count = 0;
	for(auto &n: normal) {
		if(n.Norm() < 0.00001) continue;
		n.Normalize();
		scaledNormal += n;
		count++;
	}
	scaledNormal /= count;
	float len = scaledNormal.Norm();
	if(len == 0) return;
	scaledNormal /= len;

	int distr[kConeRings];
	for(int k = 0; k < kConeRings; k++)
		distr[k] =0;

	for(auto &n: normal) {
		const int ring = coneRing(scaledNormal, n);
		if(ring < 0) continue;
		distr[ring]++;
	}
	int tot = 0;
	int best;
	for(best = 0; best < kConeRings; best++) {
		tot += distr[best];
		if(tot >= threshold * normal.size())
			break;
	}
	scaledNormal = coneAxis(scaledNormal, best);
}

void AnchoredCone3f::AddAnchors(vector<Point3f> &anchors) {
	assert(anchors.size() > 0);
	frontAnchor = anchors[0];
	backAnchor = anchors[0];

	float fa = frontAnchor * scaledNormal;
	float fb = -backAnchor * scaledNormal;

	vector<Point3f>::iterator i;
	for(i = anchors.begin(); i != anchors.end(); i++) {
		Point3f &anchor = *i;
		float na = anchor * scaledNormal;
		if(na < fa) {
			frontAnchor = anchor;
			fa = na;
		}
		if(-na < fb) {
			backAnchor = anchor;
			fb = -na;
		}
	}
}

void Cone3s::Import(const AnchoredCone3f &c) {
	for(int i = 0; i < 4; i++)
		n[i] = 0;

	Point3f normal = c.scaledNormal;
	float len = normal.Norm();
	//the file says: all four values are 0 when the axis is not longer than 0.001, that is when there
	//is no cone (no normal, or one so wide that it culls nothing)
	if(!(len > 0.001))
		return;
	normal /= len;

	for(int i = 0; i < 3; i++) {
		assert(normal[i] < 1.01 && normal[i] > -1.01);

		if(normal[i] > 1.0f) normal[i] = 1;
		if(normal[i] < -1.0f) normal[i] = -1;
	}
	n[0] = (short)(normal[0] * 32766);
	n[1] = (short)(normal[1] * 32766);
	n[2] = (short)(normal[2] * 32766);
	//i want to represent numbers from -10 to 10
	if(len > 10.0f) len = 10.0f;
	n[3] = (short)(len * 3276);
}


bool Cone3s::Backface(const vcg::Sphere3f &sphere,
					  const vcg::Point3f &view) const {
	vcg::Point3f norm(n[0]/32766.0f, n[1]/32766.0f, n[2]/32766.0f);
	vcg::Point3f d = (sphere.Center() - norm * sphere.Radius()) - view;
	norm *= n[3]/32766.0f;

	float f = d * norm;
	if (f < 0.001 || f * f < d * d)
		return false;
	return true;
}

bool Cone3s::Frontface(const vcg::Sphere3f &sphere,
					   const vcg::Point3f &view) const {
	vcg::Point3f norm(n[0]/32766.0f, n[1]/32766.0f, n[2]/32766.0f);
	vcg::Point3f d = (sphere.Center() + norm * sphere.Radius()) - view;
	norm *= n[3]/3276.0f;

	float f = -d * norm;
	if (f < 0.001 || f * f < d * d)
		return false;
	return true;
}
