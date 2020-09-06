#include "Base.h"
#include "mask.h"

extern "C" {
#include "maskApi.h"
}

using namespace std;
using namespace torch;
using namespace Detectron2;
using namespace Detectron2::pycocotools;

//**************************************************************************
// Microsoft COCO Toolbox.      version 2.0
// Data, paper, and tutorials available at:  http://mscoco.org/
// Code written by Piotr Dollar and Tsung-Yi Lin, 2015.
// Licensed under the Simplified BSD License [see coco/license.txt]
//**************************************************************************

// the class handles the memory allocation and deallocation
class RLEs {
public:
	RLE *_R;
	siz _n;

	RLEs(siz n = 0) : _n(n) {
		rlesInit(&_R, n);
	}
	RLEs(const RLEs &) = delete;
	void operator=(const RLEs &) = delete;

	// free the RLE array here
	~RLEs() {
		if (_R != nullptr) {
			for (int i = 0; i < _n; i++) {
				free(_R[i].cnts);
			}
			free(_R);
		}
	}
};

// the class handles the memory allocation and deallocation
class Masks {
public:
	byte *_mask;
	siz _h;
	siz _w;
	siz _n;

	Masks(siz h, siz w, siz n) : _h(h), _w(w), _n(n) {
		_mask = (byte*)malloc(h * w * n * sizeof(byte));
	}
	Masks(const Masks &) = delete;
	void operator=(const Masks &) = delete;

	~Masks() {
		if (_mask) {
			free(_mask);
		}
	}

	// called when passing into np.array() and return an np.ndarray in column-major order
	torch::Tensor toTensor() {
		assert(_mask);

		// Create a 1D array, and reshape it to fortran/Matlab column-major array
		auto ret = torch::from_blob(_mask, { _h, _w, _n }, Deleter(free), torch::kUInt8);
		_mask = nullptr;
		return ret;
	}
};

// internal conversion from Python RLEs object to compressed RLE format
static MaskObjectVec _toString(const RLEs &Rs) {
	siz n = Rs._n;
	char* c_string;
	MaskObjectVec objs;
	objs.reserve(n);
	for (int i = 0; i < n; i++) {
		c_string = rleToString(&Rs._R[i]);
		auto obj = new MaskObjectImpl({ { (int)Rs._R[i].h, (int)Rs._R[i].w }, c_string });
		objs.push_back(MaskObject(obj));
		free(c_string);
	}
	return objs;
}

// internal conversion from compressed RLE format to Python RLEs object
static shared_ptr<RLEs> _frString(const MaskObjectVec &rleObjs) {
	siz n = rleObjs.size();
	auto Rs = make_shared<RLEs>(n);
	for (int i = 0; i < n; i++) {
		auto &obj = rleObjs[i];
		rleFrString(&Rs->_R[i], (char*)obj->counts.c_str(), obj->size.height, obj->size.width);
	}
	return Rs;
}

// encode mask to RLEs objects
// list of RLE string can be generated by RLEs member function
MaskObjectVec pycocotools::encode(torch::Tensor mask) {
	assert(mask.dtype() == torch::kUInt8);
	assert(mask.dim() == 3);
	auto h = mask.size(0);
	auto w = mask.size(1);
	auto n = mask.size(2);
	RLEs Rs(n);
	rleEncode(Rs._R, mask.cpu().data_ptr<byte>(), h, w, n);
	auto objs = _toString(Rs);
	return objs;
}
MaskObject pycocotools::encode_single(torch::Tensor mask) {
	assert(mask.dim() == 2);
	auto h = mask.size(0);
	auto w = mask.size(1);
	mask = mask.reshape({ h, w, 1 });
	return encode(mask)[0];
}

// decode mask from compressed list of RLE string or RLEs object
torch::Tensor pycocotools::decode(const MaskObjectVec &rleObjs) {
	auto Rs = _frString(rleObjs);
	auto h = Rs->_R[0].h;
	auto w = Rs->_R[0].w;
	auto n = Rs->_n;
	Masks masks(h, w, n);
	rleDecode(Rs->_R, masks._mask, n);
	return masks.toTensor();
}
torch::Tensor pycocotools::decode_single(const MaskObject &rleObj) {
	return decode({ rleObj }).index({ torch::indexing::Slice(), torch::indexing::Slice(), 0 });
}

MaskObject pycocotools::merge(const MaskObjectVec &rleObjs, bool intersect) {
	auto Rs = _frString(rleObjs);
	RLEs R(1);
	rleMerge(Rs->_R, R._R, Rs->_n, intersect ? 1 : 0);
	auto obj = _toString(R)[0];
	return obj;
}

torch::Tensor pycocotools::area(const MaskObjectVec &rleObjs) {
	auto Rs = _frString(rleObjs);
	uint* _a = (uint*)malloc(Rs->_n * sizeof(uint));
	rleArea(Rs->_R, Rs->_n, _a);
	return torch::from_blob(_a, { Rs->_n }, Deleter(free), torch::kInt32);
}
torch::Tensor pycocotools::area_single(const MaskObject &rleObj) {
	return area({ rleObj })[0];
}

// iou computation. support function overload (RLEs-RLEs and bbox-bbox).
torch::Tensor pycocotools::iou(torch::Tensor dt, torch::Tensor gt, const torch::Tensor &pyiscrowd) {
	auto _preproc = [](torch::Tensor objs){
		if (objs.dim() == 1) {
			objs = objs.reshape({ -1, 1 });
		}
		// check if it's Nx4 bbox
		// numpy ndarray input is only for *bounding boxes* and should have Nx4 dimension
		assert(objs.dim() == 2 && objs.size(1) == 4);
		objs = objs.to(torch::kDouble);
		return objs;
	};
	dt = _preproc(dt);
	gt = _preproc(gt);

	siz m = dt.size(0);
	siz n = gt.size(0);
	if (m == 0 || n == 0) {
		return Tensor();
	}

	auto _iou = (double*)malloc(m * n * sizeof(double));
	auto iscrowd = pyiscrowd.to(torch::kUInt8).reshape(-1);
	bbIou(dt.cpu().data_ptr<double>(), gt.cpu().data_ptr<double>(), m, n, iscrowd.data_ptr<byte>(), _iou);
	return torch::from_blob(_iou, { m, n }, Deleter(free), torch::kDouble);
}

torch::Tensor pycocotools::iou(const MaskObjectVec &dt, const MaskObjectVec &gt, const torch::Tensor &pyiscrowd) {
	auto dtRs = _frString(dt);
	auto gtRs = _frString(gt);

	siz m = dtRs->_n;
	siz n = gtRs->_n;
	if (m == 0 || n == 0) {
		return Tensor();
	}

	auto _iou = (double*)malloc(m * n * sizeof(double));
	auto iscrowd = pyiscrowd.to(torch::kUInt8).reshape(-1);
	rleIou(dtRs->_R, gtRs->_R, m, n, iscrowd.cpu().data_ptr<byte>(), _iou);
	return torch::from_blob(_iou, { m, n }, Deleter(free), torch::kDouble);
}

torch::Tensor pycocotools::toBbox(const MaskObjectVec &rleObjs) {
	auto Rs = _frString(rleObjs);
	siz n = Rs->_n;
	BB _bb = (BB)malloc(4 * n * sizeof(double));
	rleToBbox(Rs->_R, _bb, n);
	return torch::from_blob(_bb, { n, 4 }, Deleter(free), torch::kDouble);
}
torch::Tensor pycocotools::toBbox_single(const MaskObject &rleObj) {
	return toBbox({ rleObj })[0];
}

static MaskObjectVec frBbox(const torch::Tensor &bb, siz h, siz w) {
	assert(bb.dtype() == torch::kDouble);
	assert(bb.dim() == 2);
	assert(bb.size(1) == 4);
	siz n = bb.size(0);
	RLEs Rs(n);
	rleFrBbox(Rs._R, bb.cpu().data_ptr<double>(), h, w, n);
	auto objs = _toString(Rs);
	return objs;
}

static MaskObjectVec frPoly(const std::vector<torch::Tensor> &poly, siz h, siz w) {
	auto n = poly.size();
	RLEs Rs(n);
	for (int i = 0; i < n; i++) {
		assert(poly[i].dim() == 2);
		assert(poly[i].size(1) == 2);
		auto p = poly[i].flatten().cpu().to(torch::kDouble);
		rleFrPoly(&Rs._R[i], p.data_ptr<double>(), p.size(0) / 2, h, w);
	}
	auto objs = _toString(Rs);
	return objs;
}

static MaskObjectVec frUncompressedRLE(const MaskObjectVec &ucRles, siz h, siz w) {
	auto n = ucRles.size();
	MaskObjectVec objs;
	objs.reserve(n);
	for (int i = 0; i < n; i++) {
		auto &ucRle = ucRles[i];

		RLEs Rs(1);
		auto &cnts = ucRle->counts_uncompressed;
		// time for malloc can be saved here but it's fine
		int count = cnts.size();
		uint *data = (uint*)malloc(count * sizeof(uint));
		for (int j = 0; j < count; j++) {
			data[j] = cnts[j];
		}
		Rs._R[0] = { (siz)ucRle->size.height, (siz)ucRle->size.width, (siz)count, data };
		objs.push_back(_toString(Rs)[0]);
	}
	return objs;
}

MaskObjectVec pycocotools::frPyObjects_boxes(const torch::Tensor &pyobj, int h, int w) {
	// encode rle from a list of python objects
	assert(pyobj.dim() == 2 && pyobj.size(1) == 4);
	return frBbox(pyobj, h, w);
}

MaskObjectVec pycocotools::frPyObjects_polygons(const std::vector<torch::Tensor> &pyobj, int h, int w) {
	return frPoly(pyobj, h, w);
}
MaskObject pycocotools::frPyObjects_single(const torch::Tensor &pyobj, int h, int w) {
	if (pyobj.dim() == 1 && pyobj.size(0) == 4) {
		return frBbox(pyobj.reshape({ 1, -1 }), h, w)[0];
	}
	assert(pyobj.dim() == 2 && pyobj.size(1) == 2);
	return frPoly({ pyobj }, h, w)[0];
}

MaskObjectVec pycocotools::frPyObjects(const MaskObjectVec &pyobj, int h, int w) {
	assert(!pyobj.empty());
	assert(!pyobj[0]->counts_uncompressed.empty());
	return frUncompressedRLE(pyobj, h, w);
}
MaskObject pycocotools::frPyObjects_single(const MaskObject &pyobj, int h, int w) {
	return frPyObjects({ pyobj }, h, w)[0];
}
