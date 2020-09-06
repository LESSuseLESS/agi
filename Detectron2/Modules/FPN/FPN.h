#pragma once

#include <Detectron2/Modules/ResNet/ResNet.h>
#include "TopBlock.h"

namespace Detectron2
{
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// converted from modeling/backbone/fpn.py

	class FPNImpl : public BackboneImpl {
	public:
		/**
			bottom_up: module representing the bottom up subnetwork. The multi-scale feature maps generated by the
				bottom up network, and listed in "in_features", are used to generate FPN levels.
			in_features: names of the input feature maps coming from the backbone to which FPN is attached.
				For example, if the backbone produces["res2", "res3", "res4"], any *contiguous* sublist of these
				may be used; order must be from high to low resolution.
			out_channels: number of channels in the output feature maps.
			norm: the normalization to use.
			fuse_type: types for fusing the top down features and the lateral ones.It can be "sum" (default),
				which sums up element-wise; or "avg", which takes the element-wise mean of the two.
			top_block: if provided, an extra operation will be performed on the output of the last (smallest
				resolution) FPN output, and the result will extend the result list. The top_block further
				downsamples the feature map.
		*/
		FPNImpl(Backbone bottom_up, const std::vector<std::string> &in_features,
			int out_channels, BatchNorm::Type norm, TopBlock top_block, const std::string &fuse_type);

		virtual void initialize(const ModelImporter &importer, const std::string &prefix) override;

		/**
			input(dict[str->Tensor]): mapping feature map name(e.g., "res5") to feature map tensor for each
				feature level in high to low resolution order.
			dict[str->Tensor]: mapping from feature map name to FPN feature map tensor in high to low
				resolution order.
			Returned feature names follow the FPN paper convention : "p<stage>", where stage has
				stride = 2 * * stage e.g., ["p2", "p3", ..., "p6"].
		*/
		virtual TensorMap forward(torch::Tensor x) override;

		virtual int size_divisibility() override {
			return m_size_divisibility;
		}

	private:
		Backbone m_bottom_up;
		TopBlock m_top_block;
		std::vector<std::string> m_in_features;
		int m_size_divisibility;
		std::string m_fuse_type;

		std::vector<int> m_stages;
		std::vector<ConvBn2d> m_lateral_convs;
		std::vector<ConvBn2d> m_output_convs;

		// Assert that each stride is 2x times its preceding stride, i.e. "contiguous in log2".
		static void _assert_strides_are_log2_contiguous(const std::vector<int64_t> &strides);
	};
	TORCH_MODULE(FPN);

	/**
		Build a backbone from `cfg.MODEL.BACKBONE.NAME`.

		Returns:
			an instance of :class:`Backbone`
	*/
	Backbone build_backbone(CfgNode &cfg, const ShapeSpec *input_shape = nullptr);
	
    /**
		Args:
			cfg: a detectron2 CfgNode

		Returns:
			backbone (Backbone): backbone module, must be a subclass of :class:`Backbone`.
	*/
	Backbone build_resnet_fpn_backbone(CfgNode &cfg, const ShapeSpec &input_shape);

	/**
		Args:
			cfg: a detectron2 CfgNode

		Returns:
			backbone (Backbone): backbone module, must be a subclass of :class:`Backbone`.
	*/
	Backbone build_retinanet_resnet_fpn_backbone(CfgNode &cfg, const ShapeSpec &input_shape);
}