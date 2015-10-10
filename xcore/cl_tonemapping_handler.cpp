/*
 * cl_tonemapping_handler.cpp - CL tonemapping handler
 *
 *  Copyright (c) 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *  Author: Yao Wang <yao.y.wang@intel.com>
 */
#include "xcam_utils.h"
#include "cl_tonemapping_handler.h"

namespace XCam {

CLTonemappingImageKernel::CLTonemappingImageKernel (SmartPtr<CLContext> &context,
        const char *name)
    : CLImageKernel (context, name)
    , _tm_gamma(2.0f);
{
    _wb_config.r_gain = 1.0;
    _wb_config.gr_gain = 1.0;
    _wb_config.gb_gain = 1.0;
    _wb_config.b_gain = 1.0;
}

bool
CLTonemappingImageKernel::set_wb (const XCam3aResultWhiteBalance &wb)
{
    _wb_config.r_gain = (float)wb.r_gain;
    _wb_config.gr_gain = (float)wb.gr_gain;
    _wb_config.gb_gain = (float)wb.gb_gain;
    _wb_config.b_gain = (float)wb.b_gain;
    return true;
}


XCamReturn
CLTonemappingImageKernel::prepare_arguments (
    SmartPtr<DrmBoBuffer> &input, SmartPtr<DrmBoBuffer> &output,
    CLArgument args[], uint32_t &arg_count,
    CLWorkSize &work_size)
{
    SmartPtr<CLContext> context = get_context ();

    _image_in = new CLVaImage (context, input);
    _image_out = new CLVaImage (context, output);

    XCAM_ASSERT (_image_in->is_valid () && _image_out->is_valid ());
    XCAM_FAIL_RETURN (
        WARNING,
        _image_in->is_valid () && _image_out->is_valid (),
        XCAM_RETURN_ERROR_MEM,
        "cl image kernel(%s) in/out memory not available", get_kernel_name ());

    SmartPtr<X3aStats> stats = input->find_3a_stats ();
    XCam3AStats *stats_ptr = stats->get_stats ();
    _stats_buffer = new CLBuffer(
        context, stats_ptr->info.aligned_width * stats_ptr->info.aligned_height * sizeof (XCamGridStat),
        CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR , &(stats_ptr->stats));

    //set args;
    args[0].arg_adress = &_image_in->get_mem_id ();
    args[0].arg_size = sizeof (cl_mem);
    args[1].arg_adress = &_image_out->get_mem_id ();
    args[1].arg_size = sizeof (cl_mem);
    args[2].arg_adress = &_stats_buffer->get_mem_id ();
    args[2].arg_size = sizeof (cl_mem);
    args[3].arg_adress = &_wb_config;
    args[3].arg_size = sizeof (_wb_config);
    args[4].arg_adress = &_tm_gamma;
    args[4].arg_size = sizeof (float);
    arg_count = 5;

    const CLImageDesc out_info = _image_out->get_image_desc ();
    work_size.dim = XCAM_DEFAULT_IMAGE_DIM;
    work_size.global[0] = out_info.width;
    work_size.global[1] = stats_ptr->info.aligned_height * 16;
    work_size.local[0] = 16;
    work_size.local[1] = 16;

    return XCAM_RETURN_NO_ERROR;
}

CLTonemappingImageHandler::CLTonemappingImageHandler (const char *name)
    : CLImageHandler (name)
    , _output_format (V4L2_PIX_FMT_RGBA32)
{
}

bool
CLTonemappingImageHandler::set_tonemapping_kernel(SmartPtr<CLTonemappingImageKernel> &kernel)
{
    SmartPtr<CLImageKernel> image_kernel = kernel;
    add_kernel (image_kernel);
    _tonemapping_kernel = kernel;
    return true;
}

bool
CLTonemappingImageHandler::set_wb_config (const XCam3aResultWhiteBalance &wb)
{
    return _tonemapping_kernel->set_wb (wb);
}


XCamReturn
CLTonemappingImageHandler::prepare_buffer_pool_video_info (
    const VideoBufferInfo &input,
    VideoBufferInfo &output)
{
    bool format_inited = output.init (_output_format, input.width, input.height);

    XCAM_FAIL_RETURN (
        WARNING,
        format_inited,
        XCAM_RETURN_ERROR_PARAM,
        "CL image handler(%s) ouput format(%s) unsupported",
        get_name (), xcam_fourcc_to_string (_output_format));

    return XCAM_RETURN_NO_ERROR;
}


SmartPtr<CLImageHandler>
create_cl_tonemapping_image_handler (SmartPtr<CLContext> &context)
{
    SmartPtr<CLTonemappingImageHandler> tonemapping_handler;
    SmartPtr<CLTonemappingImageKernel> tonemapping_kernel;
    XCamReturn ret = XCAM_RETURN_NO_ERROR;

    tonemapping_kernel = new CLTonemappingImageKernel (context, "kernel_tonemapping");
    {
        XCAM_CL_KERNEL_FUNC_SOURCE_BEGIN(kernel_tonemapping)
#include "kernel_tonemapping.clx"
        XCAM_CL_KERNEL_FUNC_END;
        ret = tonemapping_kernel->load_from_source (kernel_tonemapping_body, strlen (kernel_tonemapping_body));
        XCAM_FAIL_RETURN (
            WARNING,
            ret == XCAM_RETURN_NO_ERROR,
            NULL,
            "CL image handler(%s) load source failed", tonemapping_kernel->get_kernel_name());
    }
    XCAM_ASSERT (tonemapping_kernel->is_valid ());
    tonemapping_handler = new CLTonemappingImageHandler("cl_handler_tonemapping");
    tonemapping_handler->set_tonemapping_kernel(tonemapping_kernel);

    return tonemapping_handler;
}

};
