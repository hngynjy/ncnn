// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "slice_arm.h"

#include "layer_type.h"

#if __ARM_NEON
#include <arm_neon.h>
#endif // __ARM_NEON

namespace ncnn {

DEFINE_LAYER_CREATOR(Slice_arm)

Slice_arm::Slice_arm()
{
#if __ARM_NEON
    support_packing = true;

    packing_pack1 = 0;
#endif // __ARM_NEON

    support_bf16_storage = true;
}

int Slice_arm::create_pipeline(const Option& opt)
{
#if __ARM_NEON
    if (opt.use_packing_layout)
    {

    {
        packing_pack1 = ncnn::create_layer(ncnn::LayerType::Packing);

        ncnn::ParamDict pd;
        pd.set(0, 1);

        packing_pack1->load_param(pd);

        packing_pack1->create_pipeline(opt);
    }

    }
#endif // __ARM_NEON

    return 0;
}

int Slice_arm::destroy_pipeline(const Option& opt)
{
#if __ARM_NEON
    if (opt.use_packing_layout)
    {

    if (packing_pack1)
    {
        packing_pack1->destroy_pipeline(opt);
        delete packing_pack1;
        packing_pack1 = 0;
    }

    }
#endif // __ARM_NEON

    return 0;
}

int Slice_arm::forward(const std::vector<Mat>& bottom_blobs, std::vector<Mat>& top_blobs, const Option& opt) const
{
    if (opt.use_bf16_storage)
        return forward_bf16s(bottom_blobs, top_blobs, opt);

    const Mat& bottom_blob = bottom_blobs[0];
    int dims = bottom_blob.dims;
    size_t elemsize = bottom_blob.elemsize;
    int elempack = bottom_blob.elempack;
    const int* slices_ptr = slices;

#if __ARM_NEON
    if (opt.use_packing_layout)
    {

    if (dims == 1) // axis == 0
    {
        // slice vector
        int w = bottom_blob.w * elempack;
        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (w - q) / (top_blobs.size() - i);
            }

            int out_elempack = slice % 4 == 0 ? 4 : 1;
            size_t out_elemsize = elemsize / elempack * out_elempack;

            Mat& top_blob = top_blobs[i];
            top_blob.create(slice / out_elempack, out_elemsize, out_elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            const float* ptr = (const float*)bottom_blob + q;
            float* outptr = top_blob;
            memcpy(outptr, ptr, top_blob.w * top_blob.elemsize);

            q += slice;
        }

        return 0;
    }

    if (dims == 2 && axis == 0)
    {
        // slice image height
        int w = bottom_blob.w;
        int h = bottom_blob.h * elempack;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (h - q) / (top_blobs.size() - i);
            }

            int out_elempack = slice % 4 == 0 ? 4 : 1;
            size_t out_elemsize = elemsize / elempack * out_elempack;

            Mat& top_blob = top_blobs[i];
            top_blob.create(w, slice / out_elempack, out_elemsize, out_elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        size_t out_elemsize = top_blobs[0].elemsize;
        int out_elempack = top_blobs[0].elempack;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            out_elemsize = std::min(out_elemsize, top_blobs[i].elemsize);
            out_elempack = std::min(out_elempack, top_blobs[i].elempack);
        }

        Mat bottom_blob_unpacked = bottom_blob;
        if (elempack == 4 && out_elempack == 1)
        {
            packing_pack1->forward(bottom_blob, bottom_blob_unpacked, opt);
        }

        const float* ptr = bottom_blob_unpacked;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            Mat& top_blob = top_blobs[i];

            if (out_elempack == 1 && top_blob.elempack == 4)
            {
                for (int j=0; j<top_blob.h; j++)
                {
                    const float* r0 = ptr;
                    const float* r1 = ptr + w;
                    const float* r2 = ptr + w*2;
                    const float* r3 = ptr + w*3;

                    float* outptr0 = top_blob.row(j);

                    for (int j=0; j<w; j++)
                    {
                        outptr0[0] = *r0++;
                        outptr0[1] = *r1++;
                        outptr0[2] = *r2++;
                        outptr0[3] = *r3++;

                        outptr0 += 4;
                    }

                    ptr += w * 4;
                }
            }
            else // if (out_elempack == 1 && top_blob.elempack == 1) if (out_elempack == 4 && top_blob.elempack == 4)
            {
                int size = w * top_blob.h;

                float* outptr = top_blob;
                memcpy(outptr, ptr, size * top_blob.elemsize);

                ptr += size * top_blob.elempack;
            }
        }

        return 0;
    }

    if (dims == 2 && axis == 1)
    {
        // slice image width
        int w = bottom_blob.w;
        int h = bottom_blob.h;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (w - q) / (top_blobs.size() - i);
            }

            Mat& top_blob = top_blobs[i];
            top_blob.create(slice, h, elemsize, elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int j=0; j<h; j++)
        {
            const float* ptr = bottom_blob.row(j);
            for (size_t i=0; i<top_blobs.size(); i++)
            {
                Mat& top_blob = top_blobs[i];

                float* outptr = top_blob.row(j);
                memcpy(outptr, ptr, top_blob.w * elemsize);

                ptr += top_blob.w * elempack;
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 0)
    {
        // slice dim channel
        int w = bottom_blob.w;
        int h = bottom_blob.h;
        int channels = bottom_blob.c * elempack;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (channels - q) / (top_blobs.size() - i);
            }

            int out_elempack = slice % 4 == 0 ? 4 : 1;
            size_t out_elemsize = elemsize / elempack * out_elempack;

            Mat& top_blob = top_blobs[i];
            top_blob.create(w, h, slice / out_elempack, out_elemsize, out_elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        size_t out_elemsize = top_blobs[0].elemsize;
        int out_elempack = top_blobs[0].elempack;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            out_elemsize = std::min(out_elemsize, top_blobs[i].elemsize);
            out_elempack = std::min(out_elempack, top_blobs[i].elempack);
        }

        Mat bottom_blob_unpacked = bottom_blob;
        if (elempack == 4 && out_elempack == 1)
        {
            packing_pack1->forward(bottom_blob, bottom_blob_unpacked, opt);
        }

        int p = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            Mat& top_blob = top_blobs[i];

            if (out_elempack == 1 && top_blob.elempack == 4)
            {
                int size = top_blob.w * top_blob.h;

                for (int q=0; q<top_blob.c; q++)
                {
                    const float* r0 = bottom_blob_unpacked.channel(p);
                    const float* r1 = bottom_blob_unpacked.channel(p+1);
                    const float* r2 = bottom_blob_unpacked.channel(p+2);
                    const float* r3 = bottom_blob_unpacked.channel(p+3);

                    float* outptr0 = top_blob.channel(q);

                    for (int j=0; j<size; j++)
                    {
                        outptr0[0] = *r0++;
                        outptr0[1] = *r1++;
                        outptr0[2] = *r2++;
                        outptr0[3] = *r3++;

                        outptr0 += 4;
                    }

                    p += 4;
                }
            }
            else // if (out_elempack == 1 && top_blob.elempack == 1) if (out_elempack == 4 && top_blob.elempack == 4)
            {
                int size = top_blob.total();

                const float* ptr = bottom_blob_unpacked.channel(p);
                float* outptr = top_blob;
                memcpy(outptr, ptr, size * top_blob.elemsize);

                p += top_blob.c;
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 1)
    {
        // slice dim height
        int w = bottom_blob.w;
        int h = bottom_blob.h;
        int channels = bottom_blob.c;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (h - q) / (top_blobs.size() - i);
            }

            Mat& top_blob = top_blobs[i];
            top_blob.create(w, slice, channels, elemsize, elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int p=0; p<channels; p++)
        {
            const float* ptr = bottom_blob.channel(p);

            for (size_t i=0; i<top_blobs.size(); i++)
            {
                Mat& top_blob = top_blobs[i];

                int size = top_blob.w * top_blob.h;

                float* outptr = top_blob.channel(p);
                memcpy(outptr, ptr, size * elemsize);

                ptr += size * elempack;
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 2)
    {
        // slice dim width
        int w = bottom_blob.w;
        int h = bottom_blob.h;
        int channels = bottom_blob.c;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (w - q) / (top_blobs.size() - i);
            }

            Mat& top_blob = top_blobs[i];
            top_blob.create(slice, h, channels, elemsize, elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int p=0; p<channels; p++)
        {
            const float* ptr = bottom_blob.channel(p);

            for (int j=0; j<h; j++)
            {
                for (size_t i=0; i<top_blobs.size(); i++)
                {
                    Mat& top_blob = top_blobs[i];

                    float* outptr = top_blob.channel(p).row(j);
                    memcpy(outptr, ptr, top_blob.w * elemsize);

                    ptr += top_blob.w * elempack;
                }
            }
        }

        return 0;
    }

    } // opt.use_packing_layout
#endif // __ARM_NEON

    return Slice::forward(bottom_blobs, top_blobs, opt);
}

int Slice_arm::forward_bf16s(const std::vector<Mat>& bottom_blobs, std::vector<Mat>& top_blobs, const Option& opt) const
{
    const Mat& bottom_blob = bottom_blobs[0];
    int dims = bottom_blob.dims;
    size_t elemsize = bottom_blob.elemsize;
    int elempack = bottom_blob.elempack;
    const int* slices_ptr = slices;

#if __ARM_NEON
    if (opt.use_packing_layout)
    {

    if (dims == 1) // axis == 0
    {
        // slice vector
        int w = bottom_blob.w * elempack;
        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (w - q) / (top_blobs.size() - i);
            }

            int out_elempack = slice % 4 == 0 ? 4 : 1;
            size_t out_elemsize = elemsize / elempack * out_elempack;

            Mat& top_blob = top_blobs[i];
            top_blob.create(slice / out_elempack, out_elemsize, out_elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            const unsigned short* ptr = (const unsigned short*)bottom_blob + q;
            unsigned short* outptr = top_blob;
            memcpy(outptr, ptr, top_blob.w * top_blob.elemsize);

            q += slice;
        }

        return 0;
    }

    if (dims == 2 && axis == 0)
    {
        // slice image height
        int w = bottom_blob.w;
        int h = bottom_blob.h * elempack;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (h - q) / (top_blobs.size() - i);
            }

            int out_elempack = slice % 4 == 0 ? 4 : 1;
            size_t out_elemsize = elemsize / elempack * out_elempack;

            Mat& top_blob = top_blobs[i];
            top_blob.create(w, slice / out_elempack, out_elemsize, out_elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        size_t out_elemsize = top_blobs[0].elemsize;
        int out_elempack = top_blobs[0].elempack;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            out_elemsize = std::min(out_elemsize, top_blobs[i].elemsize);
            out_elempack = std::min(out_elempack, top_blobs[i].elempack);
        }

        Mat bottom_blob_unpacked = bottom_blob;
        if (elempack == 4 && out_elempack == 1)
        {
            packing_pack1->forward(bottom_blob, bottom_blob_unpacked, opt);
        }

        const unsigned short* ptr = bottom_blob_unpacked;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            Mat& top_blob = top_blobs[i];

            if (out_elempack == 1 && top_blob.elempack == 4)
            {
                for (int j=0; j<top_blob.h; j++)
                {
                    const unsigned short* r0 = ptr;
                    const unsigned short* r1 = ptr + w;
                    const unsigned short* r2 = ptr + w*2;
                    const unsigned short* r3 = ptr + w*3;

                    unsigned short* outptr0 = top_blob.row<unsigned short>(j);

                    for (int j=0; j<w; j++)
                    {
                        outptr0[0] = *r0++;
                        outptr0[1] = *r1++;
                        outptr0[2] = *r2++;
                        outptr0[3] = *r3++;

                        outptr0 += 4;
                    }

                    ptr += w * 4;
                }
            }
            else // if (out_elempack == 1 && top_blob.elempack == 1) if (out_elempack == 4 && top_blob.elempack == 4)
            {
                int size = w * top_blob.h;

                unsigned short* outptr = top_blob;
                memcpy(outptr, ptr, size * top_blob.elemsize);

                ptr += size * top_blob.elempack;
            }
        }

        return 0;
    }

    if (dims == 2 && axis == 1)
    {
        // slice image width
        int w = bottom_blob.w;
        int h = bottom_blob.h;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (w - q) / (top_blobs.size() - i);
            }

            Mat& top_blob = top_blobs[i];
            top_blob.create(slice, h, elemsize, elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int j=0; j<h; j++)
        {
            const unsigned short* ptr = bottom_blob.row<const unsigned short>(j);
            for (size_t i=0; i<top_blobs.size(); i++)
            {
                Mat& top_blob = top_blobs[i];

                unsigned short* outptr = top_blob.row<unsigned short>(j);
                memcpy(outptr, ptr, top_blob.w * elemsize);

                ptr += top_blob.w * elempack;
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 0)
    {
        // slice dim channel
        int w = bottom_blob.w;
        int h = bottom_blob.h;
        int channels = bottom_blob.c * elempack;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (channels - q) / (top_blobs.size() - i);
            }

            int out_elempack = slice % 4 == 0 ? 4 : 1;
            size_t out_elemsize = elemsize / elempack * out_elempack;

            Mat& top_blob = top_blobs[i];
            top_blob.create(w, h, slice / out_elempack, out_elemsize, out_elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        size_t out_elemsize = top_blobs[0].elemsize;
        int out_elempack = top_blobs[0].elempack;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            out_elemsize = std::min(out_elemsize, top_blobs[i].elemsize);
            out_elempack = std::min(out_elempack, top_blobs[i].elempack);
        }

        Mat bottom_blob_unpacked = bottom_blob;
        if (elempack == 4 && out_elempack == 1)
        {
            packing_pack1->forward(bottom_blob, bottom_blob_unpacked, opt);
        }

        int p = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            Mat& top_blob = top_blobs[i];

            if (out_elempack == 1 && top_blob.elempack == 4)
            {
                int size = top_blob.w * top_blob.h;

                for (int q=0; q<top_blob.c; q++)
                {
                    const unsigned short* r0 = bottom_blob_unpacked.channel(p);
                    const unsigned short* r1 = bottom_blob_unpacked.channel(p+1);
                    const unsigned short* r2 = bottom_blob_unpacked.channel(p+2);
                    const unsigned short* r3 = bottom_blob_unpacked.channel(p+3);

                    unsigned short* outptr0 = top_blob.channel(q);

                    for (int j=0; j<size; j++)
                    {
                        outptr0[0] = *r0++;
                        outptr0[1] = *r1++;
                        outptr0[2] = *r2++;
                        outptr0[3] = *r3++;

                        outptr0 += 4;
                    }

                    p += 4;
                }
            }
            else // if (out_elempack == 1 && top_blob.elempack == 1) if (out_elempack == 4 && top_blob.elempack == 4)
            {
                int size = top_blob.total();

                const unsigned short* ptr = bottom_blob_unpacked.channel(p);
                unsigned short* outptr = top_blob;
                memcpy(outptr, ptr, size * top_blob.elemsize);

                p += top_blob.c;
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 1)
    {
        // slice dim height
        int w = bottom_blob.w;
        int h = bottom_blob.h;
        int channels = bottom_blob.c;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (h - q) / (top_blobs.size() - i);
            }

            Mat& top_blob = top_blobs[i];
            top_blob.create(w, slice, channels, elemsize, elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int p=0; p<channels; p++)
        {
            const unsigned short* ptr = bottom_blob.channel(p);

            for (size_t i=0; i<top_blobs.size(); i++)
            {
                Mat& top_blob = top_blobs[i];

                int size = top_blob.w * top_blob.h;

                unsigned short* outptr = top_blob.channel(p);
                memcpy(outptr, ptr, size * elemsize);

                ptr += size * elempack;
            }
        }

        return 0;
    }

    if (dims == 3 && axis == 2)
    {
        // slice dim width
        int w = bottom_blob.w;
        int h = bottom_blob.h;
        int channels = bottom_blob.c;

        int q = 0;
        for (size_t i=0; i<top_blobs.size(); i++)
        {
            int slice = slices_ptr[i];
            if (slice == -233)
            {
                slice = (w - q) / (top_blobs.size() - i);
            }

            Mat& top_blob = top_blobs[i];
            top_blob.create(slice, h, channels, elemsize, elempack, opt.blob_allocator);
            if (top_blob.empty())
                return -100;

            q += slice;
        }

        #pragma omp parallel for num_threads(opt.num_threads)
        for (int p=0; p<channels; p++)
        {
            const unsigned short* ptr = bottom_blob.channel(p);

            for (int j=0; j<h; j++)
            {
                for (size_t i=0; i<top_blobs.size(); i++)
                {
                    Mat& top_blob = top_blobs[i];

                    unsigned short* outptr = top_blob.channel(p).row<unsigned short>(j);
                    memcpy(outptr, ptr, top_blob.w * elemsize);

                    ptr += top_blob.w * elempack;
                }
            }
        }

        return 0;
    }

    } // opt.use_packing_layout
#endif // __ARM_NEON

    return Slice::forward(bottom_blobs, top_blobs, opt);
}

} // namespace ncnn
