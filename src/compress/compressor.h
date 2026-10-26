#ifndef BM3D_COMPRESSOR_H
#define BM3D_COMPRESSOR_H

#include <string>

#include "../common/textfile.h"

#include <vcg/math/matrix44.h>

#include "../common/traversal.h"

namespace bm3d::detail {

class Compressor: public Traversal {
public:
	//compression options
	int coord_q;      //step of coord quantizations (a power of 2: 0 means 1 unit)
	int color_bits[4];
	int norm_bits;
	float tex_step; //in pixel units

	Compressor(Reader *input);

	int levelCount(); //return number of levels in the model (assuming it's a constant depth tree)
	void setMatrix(vcg::Matrix44f m);
	void selectBySize(uint64_t size);
	void selectByLevel(int level); //select up to level included (zero based)
	void selectByError(float error);
	void selectByTriangles(uint64_t triangles);
	void dropLevel();

	void save(const std::string &output, Signature &signature);

	virtual float nodeError(uint32_t node, bool &visible);
	Traversal::Action expand(HeapNode h);

protected:

	bool transform;
	vcg::Matrix44f matrix;
	uint64_t max_size, current_size;
	float min_error, current_error;
	int max_level = -1, nlevels = 0;
	uint64_t max_triangles, current_triangles;

	uint64_t pad(uint64_t s);
	int sinkDistance(int node);

	void compress(OutputFile &file, Signature &signature, Node &node, NodeData &data, Patch *patches);
};

} //namespace

#endif // BM3D_COMPRESSOR_H
