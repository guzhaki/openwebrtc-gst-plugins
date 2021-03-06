/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 * Copyright (c) 2014, Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstercolorspace.h"

#if defined(__arm__) || defined(__arm64__)

#include <arm_neon.h>
#include <stdio.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_ercolorspace_debug);
#define GST_CAT_DEFAULT gst_ercolorspace_debug

G_DEFINE_TYPE_WITH_CODE (GstERColorspace, gst_ercolorspace, GST_TYPE_VIDEO_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_ercolorspace_debug, "ercolorspace", 0,
    "debug category for ercolorspace element"));

/* the capabilities of the inputs and outputs.
 */
static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE("{ BGRA, RGBA, I420, NV12, NV21 }"))
);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE("{ BGRA, RGBA, I420 }"))
);


GType gst_ercolorspace_get_type (void);

static gboolean gst_ercolorspace_set_info (GstVideoFilter *filter,
                           GstCaps *incaps, GstVideoInfo *in_info,
                           GstCaps *outcaps, GstVideoInfo *out_info);
static GstFlowReturn gst_ercolorspace_transform_frame (GstVideoFilter * filter, GstVideoFrame *in_frame, GstVideoFrame *out_frame);


static void gst_ercolorspace_transform_nv12_to_bgra_neon (guint8 * Y, guint8 * UV, guint8 * RGB, const guint width)
{
    guint x;
    uint8x8_t srcY_d8, srcUV_d8, srcU_d8, srcV_d8, srcUtmp_d8, srcVtmp_d8;
    int16x8_t const16_q16, const128_q16;
    int16x4_t const298_d16, const409_d16, const208_d16, const100_d16, const516_d16;
    int16x8_t srcY_q16, srcU_q16, srcV_q16;
    int16x8_t tmp1_q16;
    int32x4_t tmp1_q32, tmp2_q32, tmp3_q32, tmp4_q32, tmp5_q32, tmp6_q32;
    uint16x4_t tmp1_d16, tmp2_d16;
    uint8x8x4_t BGRA_d8x4;

    BGRA_d8x4.val[3] = vdup_n_u8(255);

    const16_q16  = vdupq_n_s16(16);
    const128_q16 = vdupq_n_s16(128);
    const298_d16 = vdup_n_s16(298);
    const409_d16 = vdup_n_s16(409);
    const100_d16 = vdup_n_s16(100);
    const208_d16 = vdup_n_s16(208);
    const516_d16 = vdup_n_s16(516);

    for (x = 0; x < width; x+=8)
    {
        /* Set up input*/
        srcY_d8  = vld1_u8(Y + x);
        srcUV_d8 = vld1_u8(UV + x);              /*load src data UV, [v1 u1 v2 u2 v3 u3 v4 u4] */

        srcV_d8 = vreinterpret_u8_u16( vshr_n_u16( vreinterpret_u16_u8(srcUV_d8), 8) );          /*shift down V, [0 v1 0 v2 0 v3 0 v4]*/
        srcU_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcUV_d8), 8) );          /*shift up   U, [u1 0 u2 0 u3 0 u4 0]*/

        srcVtmp_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcV_d8), 8) );        /*shift up   V, [v1 0 v2 0 v3 0 v4 0]*/
        srcUtmp_d8 = vreinterpret_u8_u16( vshr_n_u16( vreinterpret_u16_u8(srcU_d8), 8) );        /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/

        srcV_q16 = vreinterpretq_s16_u16( vaddl_u8(srcV_d8, srcVtmp_d8) );     /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
        srcU_q16 = vreinterpretq_s16_u16( vaddl_u8(srcU_d8, srcUtmp_d8) );     /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

        srcY_q16 = vreinterpretq_s16_u16( vmovl_u8(srcY_d8) );

        /* R channel*/
        tmp1_q16 = vsubq_s16(srcY_q16, const16_q16);                  /* C = yarr[] - 16*/
        tmp1_q32 = vmull_s16(vget_low_s16(tmp1_q16), const298_d16);   /* 298 * C*/
        tmp2_q32 = vmull_s16(vget_high_s16(tmp1_q16), const298_d16);

        tmp1_q16 = vsubq_s16(srcV_q16, const128_q16);                 /* E = uvarr[v] - 128*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const409_d16);   /* 409 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const409_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           + 409 * E*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp4_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[2] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, R channel done!*/

        /* G channel*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const208_d16);   /* 208 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const208_d16);

        tmp3_q32 = vsubq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp2_q32, tmp4_q32);

        tmp1_q16 = vsubq_s16(srcU_q16, const128_q16);                 /* D = uvarr[u] - 128*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const100_d16);   /* 100 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const100_d16);

        tmp3_q32 = vsubq_s32(tmp3_q32, tmp5_q32);                      /* 298 * C - 100 * D - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp4_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[1] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, G channel done!*/

        /* B channel*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const516_d16);   /* 516 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const516_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp5_q32);                      /* 298 * C + 516 * D*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[0] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, B channel done!*/

        /* store results*/
        vst4_u8(RGB + x*4, BGRA_d8x4);
    }
}

static void gst_ercolorspace_transform_nv21_to_bgra_neon (guint8 * Y, guint8 * UV, guint8 * RGB, const guint width)
{
    guint x;
    uint8x8_t srcY_d8, srcUV_d8, srcU_d8, srcV_d8, srcUtmp_d8, srcVtmp_d8;
    int16x8_t const16_q16, const128_q16;
    int16x4_t const298_d16, const409_d16, const208_d16, const100_d16, const516_d16;
    int16x8_t srcY_q16, srcU_q16, srcV_q16;
    int16x8_t tmp1_q16;
    int32x4_t tmp1_q32, tmp2_q32, tmp3_q32, tmp4_q32, tmp5_q32, tmp6_q32;
    uint16x4_t tmp1_d16, tmp2_d16;
    uint8x8x4_t BGRA_d8x4;

    BGRA_d8x4.val[3] = vdup_n_u8(255);

    const16_q16  = vdupq_n_s16(16);
    const128_q16 = vdupq_n_s16(128);
    const298_d16 = vdup_n_s16(298);
    const409_d16 = vdup_n_s16(409);
    const100_d16 = vdup_n_s16(100);
    const208_d16 = vdup_n_s16(208);
    const516_d16 = vdup_n_s16(516);

    for (x = 0; x < width; x+=8)
    {
        // Set up input
        srcY_d8  = vld1_u8(Y + x);
        srcUV_d8 = vld1_u8(UV + x);              /*load src data UV, [u1 v1 u2 v2 u3 v3 u4 v4] */

        srcU_d8 = vreinterpret_u8_u16( vshr_n_u16( vreinterpret_u16_u8(srcUV_d8), 8) );          /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/
        srcV_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcUV_d8), 8) );          /*shift up V,   [v1 0 v2 0 v3 0 v4 0]*/

        srcUtmp_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcU_d8), 8) );        /*shift up   U, [u1 0 u2 0 u3 0 u4 0]*/
        srcVtmp_d8 = vreinterpret_u8_u16( vshr_n_u16( vreinterpret_u16_u8(srcV_d8), 8) );        /*shift down V, [0 v1 0 v2 0 v3 0 v4]*/

        srcV_q16 = vreinterpretq_s16_u16( vaddl_u8(srcV_d8, srcVtmp_d8) );     /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
        srcU_q16 = vreinterpretq_s16_u16( vaddl_u8(srcU_d8, srcUtmp_d8) );     /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

        srcY_q16 = vreinterpretq_s16_u16( vmovl_u8(srcY_d8) );

        /* R channel*/
        tmp1_q16 = vsubq_s16(srcY_q16, const16_q16);                  /* C = yarr[] - 16*/
        tmp1_q32 = vmull_s16(vget_low_s16(tmp1_q16), const298_d16);   /* 298 * C*/
        tmp2_q32 = vmull_s16(vget_high_s16(tmp1_q16), const298_d16);

        tmp1_q16 = vsubq_s16(srcV_q16, const128_q16);                 /* E = uvarr[v] - 128*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const409_d16);   /* 409 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const409_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           + 409 * E*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp4_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[2] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, R channel done!*/

        /* G channel*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const208_d16);   /* 208 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const208_d16);

        tmp3_q32 = vsubq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp2_q32, tmp4_q32);

        tmp1_q16 = vsubq_s16(srcU_q16, const128_q16);                 /* D = uvarr[u] - 128*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const100_d16);   /* 100 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const100_d16);

        tmp3_q32 = vsubq_s32(tmp3_q32, tmp5_q32);                      /* 298 * C - 100 * D - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp4_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[1] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, G channel done!*/

        /* B channel*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const516_d16);   /* 516 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const516_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp5_q32);                      /* 298 * C + 516 * D*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[0] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, B channel done!*/

        /* store results*/
        vst4_u8(RGB + x*4, BGRA_d8x4);
    }
}

static void gst_ercolorspace_transform_i420_to_bgra_neon (guint8 * Y, guint8 * U, guint8 * V, const guint width, guint8 * RGB)
{
    guint x;
    uint8x8_t srcY_d8, srcU_d8, srcV_d8, srcUtmp_d8, srcVtmp_d8;
    int16x8_t const16_q16, const128_q16;
    int16x4_t const298_d16, const409_d16, const208_d16, const100_d16, const516_d16;
    int16x8_t srcY_q16, srcU_q16, srcV_q16;
    int16x8_t tmp1_q16;
    int32x4_t tmp1_q32, tmp2_q32, tmp3_q32, tmp4_q32, tmp5_q32, tmp6_q32;
    uint16x4_t tmp1_d16, tmp2_d16;
    uint8x8x4_t BGRA_d8x4;

    BGRA_d8x4.val[3] = vdup_n_u8(255);

    const16_q16  = vdupq_n_s16(16);
    const128_q16 = vdupq_n_s16(128);
    const298_d16 = vdup_n_s16(298);
    const409_d16 = vdup_n_s16(409);
    const100_d16 = vdup_n_s16(100);
    const208_d16 = vdup_n_s16(208);
    const516_d16 = vdup_n_s16(516);

    for (x = 0; x < width; x+=8)
    {
        // Set up input
        srcY_d8 = vld1_u8(Y + x);
        srcU_d8 = vld1_u8(U + x/2);     /*load src data U, [u1 u2 u3 u4 u5 u6 u7 u8] */
        srcV_d8 = vld1_u8(V + x/2);     /*load src data V, [v1 v2 v3 v4 v5 v6 v7 v8] */

        srcU_d8 = vget_low_u8( vmovl_u8(srcU_d8) );          /*8 to 16bit U, [0 u1 0 u2 0 u3 0 u4]*/
        srcV_d8 = vget_low_u8( vmovl_u8(srcV_d8) );          /*8 to 16bit V, [0 v1 0 v2 0 v3 0 v4]*/

        srcUtmp_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcU_d8), 8) );        /*shift up U, [u1 0 u2 0 u3 0 u4 0]*/
        srcVtmp_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcV_d8), 8) );        /*shift up V, [v1 0 v2 0 v3 0 v4 0]*/

        srcU_q16 = vreinterpretq_s16_u16( vaddl_u8(srcU_d8, srcUtmp_d8) );     /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/
        srcV_q16 = vreinterpretq_s16_u16( vaddl_u8(srcV_d8, srcVtmp_d8) );     /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/

        srcY_q16 = vreinterpretq_s16_u16( vmovl_u8(srcY_d8) );

        /* R channel*/
        tmp1_q16 = vsubq_s16(srcY_q16, const16_q16);                  /* C = yarr[] - 16*/
        tmp1_q32 = vmull_s16(vget_low_s16(tmp1_q16), const298_d16);   /* 298 * C*/
        tmp2_q32 = vmull_s16(vget_high_s16(tmp1_q16), const298_d16);

        tmp1_q16 = vsubq_s16(srcV_q16, const128_q16);                 /* E = uvarr[v] - 128*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const409_d16);   /* 409 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const409_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           + 409 * E*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp4_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[2] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, R channel done!*/

        /* G channel*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const208_d16);   /* 208 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const208_d16);

        tmp3_q32 = vsubq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp2_q32, tmp4_q32);

        tmp1_q16 = vsubq_s16(srcU_q16, const128_q16);                 /* D = uvarr[u] - 128*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const100_d16);   /* 100 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const100_d16);

        tmp3_q32 = vsubq_s32(tmp3_q32, tmp5_q32);                      /* 298 * C - 100 * D - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp4_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[1] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, G channel done!*/

        /* B channel*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const516_d16);   /* 516 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const516_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp5_q32);                      /* 298 * C + 516 * D*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        BGRA_d8x4.val[0] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, B channel done!*/

        /* store results*/
        vst4_u8(RGB + x*4, BGRA_d8x4);
    }
}

static void gst_ercolorspace_transform_nv12_to_rgba_neon (guint8 * Y, guint8 * UV, guint8 * RGB, const guint width)
{
    guint x;
    uint8x8_t srcY_d8, srcUV_d8, srcU_d8, srcV_d8, srcUtmp_d8, srcVtmp_d8;
    int16x8_t const16_q16, const128_q16;
    int16x4_t const298_d16, const409_d16, const208_d16, const100_d16, const516_d16;
    int16x8_t srcY_q16, srcU_q16, srcV_q16;
    int16x8_t tmp1_q16;
    int32x4_t tmp1_q32, tmp2_q32, tmp3_q32, tmp4_q32, tmp5_q32, tmp6_q32;
    uint16x4_t tmp1_d16, tmp2_d16;
    uint8x8x4_t RGBA_d8x4;

    RGBA_d8x4.val[3] = vdup_n_u8(255);

    const16_q16  = vdupq_n_s16(16);
    const128_q16 = vdupq_n_s16(128);
    const298_d16 = vdup_n_s16(298);
    const409_d16 = vdup_n_s16(409);
    const100_d16 = vdup_n_s16(100);
    const208_d16 = vdup_n_s16(208);
    const516_d16 = vdup_n_s16(516);

    for (x = 0; x < width; x+=8)
    {
        /* Set up input*/
        srcY_d8  = vld1_u8(Y + x);
        srcUV_d8 = vld1_u8(UV + x);              /*load src data UV, [v1 u1 v2 u2 v3 u3 v4 u4] */

        srcV_d8 = vreinterpret_u8_u16( vshr_n_u16( vreinterpret_u16_u8(srcUV_d8), 8) );          /*shift down V, [0 v1 0 v2 0 v3 0 v4]*/
        srcU_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcUV_d8), 8) );          /*shift up   U, [u1 0 u2 0 u3 0 u4 0]*/

        srcVtmp_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcV_d8), 8) );        /*shift up   V, [v1 0 v2 0 v3 0 v4 0]*/
        srcUtmp_d8 = vreinterpret_u8_u16( vshr_n_u16( vreinterpret_u16_u8(srcU_d8), 8) );        /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/

        srcV_q16 = vreinterpretq_s16_u16( vaddl_u8(srcV_d8, srcVtmp_d8) );     /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
        srcU_q16 = vreinterpretq_s16_u16( vaddl_u8(srcU_d8, srcUtmp_d8) );     /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

        srcY_q16 = vreinterpretq_s16_u16( vmovl_u8(srcY_d8) );

        /* R channel*/
        tmp1_q16 = vsubq_s16(srcY_q16, const16_q16);                  /* C = yarr[] - 16*/
        tmp1_q32 = vmull_s16(vget_low_s16(tmp1_q16), const298_d16);   /* 298 * C*/
        tmp2_q32 = vmull_s16(vget_high_s16(tmp1_q16), const298_d16);

        tmp1_q16 = vsubq_s16(srcV_q16, const128_q16);                 /* E = uvarr[v] - 128*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const409_d16);   /* 409 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const409_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           + 409 * E*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp4_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[0] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, R channel done!*/

        /* G channel*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const208_d16);   /* 208 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const208_d16);

        tmp3_q32 = vsubq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp2_q32, tmp4_q32);

        tmp1_q16 = vsubq_s16(srcU_q16, const128_q16);                 /* D = uvarr[u] - 128*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const100_d16);   /* 100 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const100_d16);

        tmp3_q32 = vsubq_s32(tmp3_q32, tmp5_q32);                      /* 298 * C - 100 * D - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp4_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[1] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, G channel done!*/

        /* B channel*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const516_d16);   /* 516 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const516_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp5_q32);                      /* 298 * C + 516 * D*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[2] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, B channel done!*/

        /* store results*/
        vst4_u8(RGB + x*4, RGBA_d8x4);
    }
}

static void gst_ercolorspace_transform_nv21_to_rgba_neon (guint8 * Y, guint8 * UV, guint8 * RGB, const guint width)
{
    guint x;
    uint8x8_t srcY_d8, srcUV_d8, srcU_d8, srcV_d8, srcUtmp_d8, srcVtmp_d8;
    int16x8_t const16_q16, const128_q16;
    int16x4_t const298_d16, const409_d16, const208_d16, const100_d16, const516_d16;
    int16x8_t srcY_q16, srcU_q16, srcV_q16;
    int16x8_t tmp1_q16;
    int32x4_t tmp1_q32, tmp2_q32, tmp3_q32, tmp4_q32, tmp5_q32, tmp6_q32;
    uint16x4_t tmp1_d16, tmp2_d16;
    uint8x8x4_t RGBA_d8x4;

    RGBA_d8x4.val[3] = vdup_n_u8(255);

    const16_q16  = vdupq_n_s16(16);
    const128_q16 = vdupq_n_s16(128);
    const298_d16 = vdup_n_s16(298);
    const409_d16 = vdup_n_s16(409);
    const100_d16 = vdup_n_s16(100);
    const208_d16 = vdup_n_s16(208);
    const516_d16 = vdup_n_s16(516);

    for (x = 0; x < width; x+=8)
    {
        // Set up input
        srcY_d8  = vld1_u8(Y + x);
        srcUV_d8 = vld1_u8(UV + x);              /*load src data UV, [u1 v1 u2 v2 u3 v3 u4 v4] */

        srcU_d8 = vreinterpret_u8_u16( vshr_n_u16( vreinterpret_u16_u8(srcUV_d8), 8) );          /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/
        srcV_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcUV_d8), 8) );          /*shift up V,   [v1 0 v2 0 v3 0 v4 0]*/

        srcUtmp_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcU_d8), 8) );        /*shift up   U, [u1 0 u2 0 u3 0 u4 0]*/
        srcVtmp_d8 = vreinterpret_u8_u16( vshr_n_u16( vreinterpret_u16_u8(srcV_d8), 8) );        /*shift down V, [0 v1 0 v2 0 v3 0 v4]*/

        srcV_q16 = vreinterpretq_s16_u16( vaddl_u8(srcV_d8, srcVtmp_d8) );     /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
        srcU_q16 = vreinterpretq_s16_u16( vaddl_u8(srcU_d8, srcUtmp_d8) );     /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

        srcY_q16 = vreinterpretq_s16_u16( vmovl_u8(srcY_d8) );

        /* R channel*/
        tmp1_q16 = vsubq_s16(srcY_q16, const16_q16);                  /* C = yarr[] - 16*/
        tmp1_q32 = vmull_s16(vget_low_s16(tmp1_q16), const298_d16);   /* 298 * C*/
        tmp2_q32 = vmull_s16(vget_high_s16(tmp1_q16), const298_d16);

        tmp1_q16 = vsubq_s16(srcV_q16, const128_q16);                 /* E = uvarr[v] - 128*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const409_d16);   /* 409 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const409_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           + 409 * E*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp4_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[0] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, R channel done!*/

        /* G channel*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const208_d16);   /* 208 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const208_d16);

        tmp3_q32 = vsubq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp2_q32, tmp4_q32);

        tmp1_q16 = vsubq_s16(srcU_q16, const128_q16);                 /* D = uvarr[u] - 128*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const100_d16);   /* 100 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const100_d16);

        tmp3_q32 = vsubq_s32(tmp3_q32, tmp5_q32);                      /* 298 * C - 100 * D - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp4_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[1] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, G channel done!*/

        /* B channel*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const516_d16);   /* 516 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const516_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp5_q32);                      /* 298 * C + 516 * D*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[2] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, B channel done!*/

        /* store results*/
        vst4_u8(RGB + x*4, RGBA_d8x4);
    }
}

static void gst_ercolorspace_transform_i420_to_rgba_neon (guint8 * Y, guint8 * U, guint8 * V, const guint width, guint8 * RGB)
{
    guint x;
    uint8x8_t srcY_d8, srcU_d8, srcV_d8, srcUtmp_d8, srcVtmp_d8;
    int16x8_t const16_q16, const128_q16;
    int16x4_t const298_d16, const409_d16, const208_d16, const100_d16, const516_d16;
    int16x8_t srcY_q16, srcU_q16, srcV_q16;
    int16x8_t tmp1_q16;
    int32x4_t tmp1_q32, tmp2_q32, tmp3_q32, tmp4_q32, tmp5_q32, tmp6_q32;
    uint16x4_t tmp1_d16, tmp2_d16;
    uint8x8x4_t RGBA_d8x4;

    RGBA_d8x4.val[3] = vdup_n_u8(255);

    const16_q16  = vdupq_n_s16(16);
    const128_q16 = vdupq_n_s16(128);
    const298_d16 = vdup_n_s16(298);
    const409_d16 = vdup_n_s16(409);
    const100_d16 = vdup_n_s16(100);
    const208_d16 = vdup_n_s16(208);
    const516_d16 = vdup_n_s16(516);

    for (x = 0; x < width; x+=8)
    {
        // Set up input
        srcY_d8 = vld1_u8(Y + x);
        srcU_d8 = vld1_u8(U + x/2);     /*load src data U, [u1 u2 u3 u4 u5 u6 u7 u8] */
        srcV_d8 = vld1_u8(V + x/2);     /*load src data V, [v1 v2 v3 v4 v5 v6 v7 v8] */

        srcU_d8 = vget_low_u8( vmovl_u8(srcU_d8) );          /*8 to 16bit U, [0 u1 0 u2 0 u3 0 u4]*/
        srcV_d8 = vget_low_u8( vmovl_u8(srcV_d8) );          /*8 to 16bit V, [0 v1 0 v2 0 v3 0 v4]*/

        srcUtmp_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcU_d8), 8) );        /*shift up U, [u1 0 u2 0 u3 0 u4 0]*/
        srcVtmp_d8 = vreinterpret_u8_u16( vshl_n_u16( vreinterpret_u16_u8(srcV_d8), 8) );        /*shift up V, [v1 0 v2 0 v3 0 v4 0]*/

        srcU_q16 = vreinterpretq_s16_u16( vaddl_u8(srcU_d8, srcUtmp_d8) );     /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/
        srcV_q16 = vreinterpretq_s16_u16( vaddl_u8(srcV_d8, srcVtmp_d8) );     /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/

        srcY_q16 = vreinterpretq_s16_u16( vmovl_u8(srcY_d8) );

        /* R channel*/
        tmp1_q16 = vsubq_s16(srcY_q16, const16_q16);                  /* C = yarr[] - 16*/
        tmp1_q32 = vmull_s16(vget_low_s16(tmp1_q16), const298_d16);   /* 298 * C*/
        tmp2_q32 = vmull_s16(vget_high_s16(tmp1_q16), const298_d16);

        tmp1_q16 = vsubq_s16(srcV_q16, const128_q16);                 /* E = uvarr[v] - 128*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const409_d16);   /* 409 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const409_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           + 409 * E*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp4_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[0] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, R channel done!*/

        /* G channel*/
        tmp3_q32 = vmull_s16(vget_low_s16(tmp1_q16), const208_d16);   /* 208 * E*/
        tmp4_q32 = vmull_s16(vget_high_s16(tmp1_q16), const208_d16);

        tmp3_q32 = vsubq_s32(tmp1_q32, tmp3_q32);                      /* 298 * C           - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp2_q32, tmp4_q32);

        tmp1_q16 = vsubq_s16(srcU_q16, const128_q16);                 /* D = uvarr[u] - 128*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const100_d16);   /* 100 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const100_d16);

        tmp3_q32 = vsubq_s32(tmp3_q32, tmp5_q32);                      /* 298 * C - 100 * D - 208 * E*/
        tmp4_q32 = vsubq_s32(tmp4_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[1] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, G channel done!*/

        /* B channel*/
        tmp5_q32 = vmull_s16(vget_low_s16(tmp1_q16), const516_d16);   /* 516 * D*/
        tmp6_q32 = vmull_s16(vget_high_s16(tmp1_q16), const516_d16);

        tmp3_q32 = vaddq_s32(tmp1_q32, tmp5_q32);                      /* 298 * C + 516 * D*/
        tmp4_q32 = vaddq_s32(tmp2_q32, tmp6_q32);

        tmp1_d16 = vqrshrun_n_s32(tmp3_q32, 8);                       /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
        tmp2_d16 = vqrshrun_n_s32(tmp4_q32, 8);

        RGBA_d8x4.val[2] = vqmovn_u16(vcombine_u16(tmp1_d16, tmp2_d16));          /* 16bit to 8bit, B channel done!*/

        /* store results*/
        vst4_u8(RGB + x*4, RGBA_d8x4);
    }
}

static void gst_ercolorspace_transform_nv12_to_i420_neon (guint8 * UV, const guint size, guint8 * OU, guint8 * OV)
{
    guint x;
    uint8x8x2_t src_d8x2;
    for (x = 0; x < size; x+=16)
    {
        src_d8x2 = vld2_u8(UV + x); /* Load and deinterlace source*/

        vst1_u8 (OU + x/2, src_d8x2.val[0]);
        vst1_u8 (OV + x/2, src_d8x2.val[1]);
    }
}

static void gst_ercolorspace_transform_nv21_to_i420_neon (guint8 * UV, const guint size, guint8 * OU, guint8 * OV)
{
    guint x;
    uint8x8x2_t src_d8x2;
    for (x = 0; x < size; x+=16)
    {
        src_d8x2 = vld2_u8(UV + x); /* Load and deinterlace source*/

        vst1_u8 (OU + x/2, src_d8x2.val[1]);
        vst1_u8 (OV + x/2, src_d8x2.val[0]);
    }
}


static GstCaps *
transform_structure_for_format (const GstStructure * str, GstPadDirection direction, const gchar * format)
{
    /* We can transform:
     *
     * I420 -> BGRA/RGBA/I420
     * NV21 -> I420/BGRA/RGBA/NV21
     * NV12 -> I420/BGRA/RGBA/NV12
     *
     * The passthrough cases are handled elsewhere
     */
    if (direction == GST_PAD_SINK) {
        if (strcmp (format, "I420") == 0) {
            GstStructure *s;
            GValue vl = G_VALUE_INIT;
            GValue v = G_VALUE_INIT;

            g_value_init (&vl, GST_TYPE_LIST);
            g_value_init (&v, G_TYPE_STRING);
            s = gst_structure_copy (str);
            gst_structure_remove_fields (s, "chroma-site", "colorimetry", NULL);
            g_value_set_string (&v, "BGRA");
            gst_value_list_append_value (&vl, &v);
            g_value_set_string (&v, "RGBA");
            gst_value_list_append_value (&vl, &v);
            gst_structure_set_value (s, "format", &vl);
            g_value_unset (&v);
            g_value_unset (&vl);

            return gst_caps_new_full (s, NULL);
        } else if (strcmp (format, "NV21") == 0 || strcmp (format, "NV12") == 0) {
            GstStructure *s1, *s2;
            GValue vl = G_VALUE_INIT;
            GValue v = G_VALUE_INIT;

            s1 = gst_structure_copy (str);
            gst_structure_set (s1, "format", G_TYPE_STRING, "I420", NULL);

            g_value_init (&vl, GST_TYPE_LIST);
            g_value_init (&v, G_TYPE_STRING);
            s2 = gst_structure_copy (str);
            gst_structure_remove_fields (s2, "chroma-site", "colorimetry", NULL);
            g_value_set_string (&v, "BGRA");
            gst_value_list_append_value (&vl, &v);
            g_value_set_string (&v, "RGBA");
            gst_value_list_append_value (&vl, &v);
            gst_structure_set_value (s2, "format", &vl);
            g_value_unset (&v);
            g_value_unset (&vl);

            return gst_caps_new_full (s1, s2, NULL);
        } else {
            /* Only passthrough */
            return gst_caps_new_empty ();
        }
    } else {
        if (strcmp (format, "I420") == 0) {
            GstStructure *s;
            GValue vl = G_VALUE_INIT;
            GValue v = G_VALUE_INIT;

            g_value_init (&vl, GST_TYPE_LIST);
            g_value_init (&v, G_TYPE_STRING);
            s = gst_structure_copy (str);
            g_value_set_string (&v, "NV12");
            gst_value_list_append_value (&vl, &v);
            g_value_set_string (&v, "NV21");
            gst_value_list_append_value (&vl, &v);
            gst_structure_set_value (s, "format", &vl);
            g_value_unset (&v);
            g_value_unset (&vl);

            return gst_caps_new_full (s, NULL);
        } else if (strcmp (format, "RGBA") == 0 || strcmp (format, "BGRA") == 0) {
            GstStructure *s;
            GValue vl = G_VALUE_INIT;
            GValue v = G_VALUE_INIT;

            g_value_init (&vl, GST_TYPE_LIST);
            g_value_init (&v, G_TYPE_STRING);
            s = gst_structure_copy (str);
            gst_structure_remove_fields (s, "colorimetry", NULL);
            g_value_set_string (&v, "I420");
            gst_value_list_append_value (&vl, &v);
            g_value_set_string (&v, "NV12");
            gst_value_list_append_value (&vl, &v);
            g_value_set_string (&v, "NV21");
            gst_value_list_append_value (&vl, &v);
            gst_structure_set_value (s, "format", &vl);
            g_value_unset (&v);
            g_value_unset (&vl);

            return gst_caps_new_full (s, NULL);
        } else {
            /* Only passthrough */
            return gst_caps_new_empty ();
        }
    }
}

static GstCaps *
gst_ercolorspace_transform_caps (GstBaseTransform * trans, GstPadDirection direction, GstCaps * query_caps, GstCaps * filter_caps)
{
    GstCaps *result_caps;
    int caps_size, i;
    GstCaps *to_caps;

    caps_size = gst_caps_get_size(query_caps);

    to_caps = gst_caps_new_empty ();
    for (i = 0; i < caps_size; ++i) {
        GstStructure *str = gst_caps_get_structure(query_caps, i);
        const GValue *format = gst_structure_get_value (str, "format");

        if (GST_VALUE_HOLDS_LIST (format)) {
            gint j, len;

            len = gst_value_list_get_size (format);
            for (j = 0; j < len; j++) {
                const GValue *v = gst_value_list_get_value (format, j);
                if (G_VALUE_HOLDS_STRING (v)) {
                    GstCaps *tmp = transform_structure_for_format (str, direction, g_value_get_string (v));
                    gst_caps_append (to_caps, tmp);
                }
            }
        } else if (G_VALUE_HOLDS_STRING (format)) {
            GstCaps *tmp = transform_structure_for_format (str, direction, g_value_get_string (format));
            gst_caps_append (to_caps, tmp);
        }
    }

    /* Prefer passthrough */
    result_caps = gst_caps_merge (gst_caps_ref (query_caps), to_caps);

    /* basetransform will filter against our template caps */

    if (filter_caps) {
        GstCaps *tmp = gst_caps_intersect_full (filter_caps, result_caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (result_caps);
        return tmp;
    } else {
        return result_caps;
    }
}


static gboolean
gst_ercolorspace_set_info (GstVideoFilter *filter,
                           GstCaps *incaps, GstVideoInfo *in_info,
                           GstCaps *outcaps, GstVideoInfo *out_info)
{
    GstERColorspace *space;

    space = GST_ERCOLORSPACE (filter);

    space->from_info = *in_info;
    space->to_format = out_info->finfo->format;

    return TRUE;
}

void
gst_ercolorspace_dispose (GObject * obj)
{
    G_OBJECT_CLASS (gst_ercolorspace_parent_class)->dispose (obj);
}


static void
gst_ercolorspace_finalize (GObject * obj)
{
    G_OBJECT_CLASS (gst_ercolorspace_parent_class)->finalize (obj);
}


/* initialize the ercolorspace's class */
static void
gst_ercolorspace_class_init (GstERColorspaceClass * klass)
{
    GObjectClass *gobject_class = (GObjectClass *) klass;
    GstElementClass *element_class = (GstElementClass *) klass;
    GstBaseTransformClass *gstbasetransform_class = (GstBaseTransformClass *) klass;
    GstVideoFilterClass *gstvideofilter_class = (GstVideoFilterClass *) klass;

    gobject_class->dispose = gst_ercolorspace_dispose;
    gobject_class->finalize = gst_ercolorspace_finalize;

    gstbasetransform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_ercolorspace_transform_caps);
    gstbasetransform_class->passthrough_on_same_caps = TRUE;

    gstvideofilter_class->set_info = GST_DEBUG_FUNCPTR (gst_ercolorspace_set_info);
    gstvideofilter_class->transform_frame = GST_DEBUG_FUNCPTR (gst_ercolorspace_transform_frame);

    gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&src_template));
    gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_template));

    gst_element_class_set_static_metadata (element_class,
      "OpenWebRTC colorspace converter",
      "Filter/Converter/Video",
      "Converts video from one colorspace to another",
      "Ericsson AB, http://www.ericsson.com/");
}


static void
gst_ercolorspace_init (GstERColorspace * space)
{
    gst_video_info_init (&space->from_info);
    space->to_format = GST_VIDEO_FORMAT_UNKNOWN;
}


/* this function does the actual processing
 */
static GstFlowReturn
gst_ercolorspace_transform_frame (GstVideoFilter * filter, GstVideoFrame *in_frame, GstVideoFrame *out_frame)
{
    GstERColorspace *space = GST_ERCOLORSPACE (filter);

    if (G_UNLIKELY (space->from_info.finfo->format == GST_VIDEO_FORMAT_UNKNOWN
        || space->to_format == GST_VIDEO_FORMAT_UNKNOWN))
    {
        GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
                           ("colorspace conversion failed: unknown formats"));
        return GST_FLOW_NOT_NEGOTIATED;
    }

    switch (space->from_info.finfo->format) {
    case GST_VIDEO_FORMAT_NV12: {
        guint8 * yarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
        guint8 * uvarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 1);
        gint width, height;
        gint y_stride, uv_stride;
        gint row;

        width = GST_VIDEO_FRAME_WIDTH (in_frame);
        height = GST_VIDEO_FRAME_HEIGHT (in_frame);
        y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
        uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 1);

        if (space->to_format != GST_VIDEO_FORMAT_I420) {
            guint8 * argbarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
            gint argb_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

            for (row = 0; row < height; row++)
            {
                if (space->to_format == GST_VIDEO_FORMAT_BGRA) {
                    gst_ercolorspace_transform_nv12_to_bgra_neon (yarr, uvarr, argbarr, width);
                } else {
                    gst_ercolorspace_transform_nv12_to_rgba_neon (yarr, uvarr, argbarr, width);
                }
                yarr += y_stride;
                if (row % 2 == 1)
                  uvarr += uv_stride;
                argbarr += argb_stride;
            }
        } else /* I420 */ {
          guint8 *out_yarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
          guint8 *out_uarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 1);
          guint8 *out_varr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 2);
          gint out_y_stride, out_u_stride, out_v_stride;

          out_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);
          out_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 1);
          out_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 2);

          for (row = 0; row < height; row++)
          {
            memcpy (out_yarr, yarr, width);
            yarr += y_stride;
            out_yarr += out_y_stride;

            if (row % 2 == 1) {
              gst_ercolorspace_transform_nv12_to_i420_neon (uvarr, width, out_uarr, out_varr);
              uvarr += uv_stride;
              out_uarr += out_u_stride;
              out_varr += out_v_stride;
            }
          }
        }
        return GST_FLOW_OK;
        break;
      }
    case GST_VIDEO_FORMAT_NV21: {
        guint8 * yarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
        guint8 * uvarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 1);
        gint width, height;
        gint y_stride, uv_stride;
        gint row;

        width = GST_VIDEO_FRAME_WIDTH (in_frame);
        height = GST_VIDEO_FRAME_HEIGHT (in_frame);
        y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
        uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 1);

        if (space->to_format != GST_VIDEO_FORMAT_I420) {
            guint8 * argbarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
            gint argb_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

            for (row = 0; row < height; row++)
            {
                if (space->to_format == GST_VIDEO_FORMAT_BGRA) {
                    gst_ercolorspace_transform_nv21_to_bgra_neon (yarr, uvarr, argbarr, width);
                } else {
                    gst_ercolorspace_transform_nv21_to_rgba_neon (yarr, uvarr, argbarr, width);
                }
                yarr += y_stride;

                if (row % 2 == 1)
                  uvarr += uv_stride;
                argbarr += argb_stride;
            }
        } else /* I420 */ {
          guint8 *out_yarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
          guint8 *out_uarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 1);
          guint8 *out_varr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 2);
          gint out_y_stride, out_u_stride, out_v_stride;

          out_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);
          out_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 1);
          out_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 2);

          for (row = 0; row < height; row++)
          {
            memcpy (out_yarr, yarr, width);
            yarr += y_stride;
            out_yarr += out_y_stride;

            if (row % 2 == 1) {
              gst_ercolorspace_transform_nv21_to_i420_neon (uvarr, width, out_uarr, out_varr);
              uvarr += uv_stride;
              out_uarr += out_u_stride;
              out_varr += out_v_stride;
            }
          }
        }
        return GST_FLOW_OK;
        break;
      }
    case GST_VIDEO_FORMAT_I420: {
        guint8 * yarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
        guint8 * uarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 1);
        guint8 * varr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 2);
        guint8 * argbarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
        gint width, height;
        gint y_stride, u_stride, v_stride, argb_stride;
        gint row;

        width = GST_VIDEO_FRAME_WIDTH (in_frame);
        height = GST_VIDEO_FRAME_HEIGHT (in_frame);

        y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
        u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 1);
        v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 2);
        argb_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

        for (row = 0; row < height; row++)
        {
            if (space->to_format == GST_VIDEO_FORMAT_BGRA) {
                gst_ercolorspace_transform_i420_to_bgra_neon (yarr, uarr, varr, width, argbarr);
            } else {
                gst_ercolorspace_transform_i420_to_rgba_neon (yarr, uarr, varr, width, argbarr);
            }

            yarr += y_stride;
            argbarr += argb_stride;

            if (row % 2 == 1) {
              uarr += u_stride;
              varr += v_stride;
            }
        }
        return GST_FLOW_OK;
        break;
      }
    default:
        break;
    }

    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
                       ("colorspace conversion failed: unsupported formats"));
    return GST_FLOW_NOT_NEGOTIATED;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
ercolorspace_init (GstPlugin * ercolorspace)
{
  return gst_element_register (ercolorspace, "ercolorspace", GST_RANK_NONE,
      GST_TYPE_ERCOLORSPACE);
}
#else
static gboolean
ercolorspace_init (GstPlugin * ercolorspace)
{
  return TRUE;
}
#endif

/* gstreamer looks for this structure to register ercolorspaces
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ercolorspace,
    "OpenWebRTC colorspace converter",
    ercolorspace_init,
    VERSION,
    "BSD",
    "OpenWebRTC GStreamer plugins",
    "http://www.openwebrtc.io/"
)
