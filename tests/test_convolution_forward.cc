#include <numeric>
#include <mkldnn_test_common.hpp>
#include <gtest/gtest.h>
#include <ideep.hpp>
#include <test_convolution_forward_common.hpp>

#include "test_ideep_common.hpp"

using namespace ideep;

template <typename data_t_src, typename data_t_wei,
         typename data_t_acc, typename data_t_dst>
class convolution_forward_tests :
  public ::testing::TestWithParam<test_convolution_params_t> {
protected:
  virtual void SetUp() {
    test_convolution_params_t p =
      ::testing::TestWithParam<test_convolution_params_t>::GetParam();
    test_convolution_sizes_t cd = p.sizes;

    tensor::descriptor src_desc ({cd.mb, cd.ic, cd.ih, cd.iw},
        data_traits<data_t_src>::data_type,
        static_cast<format>(p.formats.src_format));

    auto weights_desc = cd.ng > 1 ?
      tensor::descriptor(
          {cd.ng, cd.oc/cd.ng, cd.ic/cd.ng, cd.kh, cd.kw},
          data_traits<data_t_wei>::data_type,
          static_cast<format>(p.formats.weights_format)) :
      tensor::descriptor(
          {cd.oc, cd.ic, cd.kh, cd.kw},
          data_traits<data_t_wei>::data_type,
          static_cast<format>(p.formats.weights_format));

    with_bias_ = p.formats.bias_format !=
      static_cast<mkldnn_memory_format_t>(format::format_undef);
    auto bias_desc = with_bias_ ?
          tensor::descriptor({cd.oc}, data_traits<data_t_dst>::data_type,
              static_cast<format>(p.formats.dst_format)) :
            tensor::descriptor({}, data_traits<data_t_dst>::data_type,
              static_cast<format>(p.formats.dst_format));

    src_.init(src_desc);
    weights_.init(weights_desc);
    bias_.init(bias_desc);

    fill_data<data_t_src>(
        src_.get_size() / sizeof(data_t_src),
        reinterpret_cast<data_t_src *>(src_.get_data_handle()));
    fill_data<data_t_wei>(
        weights_.get_size() / sizeof(data_t_src),
        reinterpret_cast<data_t_src *>(weights_.get_data_handle()));

    if (with_bias_) {
      fill_data<data_t_dst>(
          bias_.get_size() / sizeof(data_t_dst),
          reinterpret_cast<data_t_src *>(bias_.get_data_handle()));
    }

    padR_ =  {cd.padh, cd.padw};
    for (int i = 0; i < 2; ++ i) {
      if ((cd.ih - ((cd.kh - 1) * (cd.dilh + 1) + 1) + cd.padh + padR_[0])
        / cd.strh + 1 != cd.oh)
        ++padR_[0];
      if ((cd.iw - ((cd.kw - 1) * (cd.dilw + 1) + 1) + cd.padw + padR_[1])
        / cd.strw + 1 != cd.ow)
        ++padR_[1];
    }

    dst_dims_ = {cd.mb, cd.oc, cd.oh, cd.ow};
    auto dst_size = std::accumulate(dst_dims_.begin(), dst_dims_.end(),
        1, std::multiplies<int>());
    raw_dst_.reset(new char [dst_size]);

  }

  tensor src_, weights_, bias_;
  tensor::dims dst_dims_;
  tensor::dims padR_;
  std::unique_ptr<char> raw_dst_;
  bool with_bias_;
};

using convolution_test =
    convolution_forward_tests<float, float, float, float>;

TEST_P(convolution_test, TestConvolution) {
    test_convolution_params_t p =
      ::testing::TestWithParam<test_convolution_params_t>::GetParam();
    test_convolution_sizes_t cd = p.sizes;

    auto dst_desc = with_bias_ ?
      convolution_forward::compute(src_, weights_, bias_, dst_dims_,
          raw_dst_.get(), tensor::dims {cd.strh, cd.strw },
          tensor::dims {cd.dilh, cd.dilw}, tensor::dims {cd.padh, cd.padw },
          padR_) :
      convolution_forward::compute(src_, weights_, dst_dims_,
          raw_dst_.get(), tensor::dims {cd.strh, cd.strw },
          tensor::dims {cd.dilh, cd.dilw}, tensor::dims {cd.padh, cd.padw },
          padR_);

    tensor ref_dst(dst_desc);
    test_convolution_attr_t attr = p.attr;
    attr.mkldnn_attr_recreate();
    compute_ref_conv_fwd<float, float, float, float>(
        cd, attr, src_, weights_, bias_, ref_dst);

    compare_tensor<float>(ref_dst, tensor {dst_desc, raw_dst_.get()});
}

#define FP32
#define DIRECTION_FORWARD
#include "convolution_common.h"
#include "diluted_convolution.h"