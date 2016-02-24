#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/features2d/features2d.hpp>

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <time.h>

using namespace std;
using namespace cv;

#ifndef int64_t
#define int64_t long long
#endif

#ifndef uint8_t
#define uint8_t unsigned char
#endif

#ifndef int32_t
#define int32_t int
#endif

#ifndef uint32_t
#define uint32_t unsigned int
#endif

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "myrobot", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "myrobot", __VA_ARGS__)

#define DEBUG 1
#define IMAGE_FORMAT_YV12 842094169

// Round x up to a multiple of mask.
// E.g., ALIGN(x, 16) means round x up to the nearest multiple of 16.
#define ALIGN(x, mask) (((x) + (mask)-1) & ~((mask)-1))

bool checkBufferSizesMatch(int srcWidth, int srcHeight,
        const ANativeWindow_Buffer *buf) {
    return (srcWidth == buf->width && srcHeight == buf->height);
}

// The source buffers must be YUV_420_888:
//   chroma_width = luma_width / 2
//   chroma_height = luma_height / 2
//   luma pixel stride is guaranteed to be 1.
//   both chroma planes have the same pixel stride and row stride.
//
//   We support chroma element strides of 1 or 2.
//   If the element stride is 1, then they are not interleaved. Just memcpy().
//   If the element stride is 2, then we further require that the chroma
//   planes be interleaved (the pointers differ by 1, it does not matter which
//   is first).
//
// The destination Surface must be YV12.
//
// The src and dst buffers have the same width, height, and row stride.
extern "C" {
JNIEXPORT bool JNICALL Java_com_neza_myrobot_JNIUtils_blit(
    JNIEnv *env, jobject obj, jint srcWidth, jint srcHeight,
    jobject srcLumaByteBuffer, jint srcLumaRowStrideBytes,
    jobject srcChromaUByteBuffer, jobject srcChromaVByteBuffer,
    jint srcChromaElementStrideBytes, jint srcChromaRowStrideBytes,
    jobject dstSurface) {
    if (srcChromaElementStrideBytes != 1 && srcChromaElementStrideBytes != 2) {
        return false;
    }

    uint8_t *srcLumaPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcLumaByteBuffer));
    uint8_t *srcChromaUPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcChromaUByteBuffer));
    uint8_t *srcChromaVPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcChromaVByteBuffer));

    LOGE("blit src: width=%d height=%d ...\n", srcWidth, srcHeight);
    Mat mYuv = *(Mat*)srcLumaPtr;
//    int sz[] = {100, 100, 100};
//    Mat bigCube(3, sz, CV_8U, Scalar::all(0));


    if (srcLumaPtr == nullptr || srcChromaUPtr == nullptr || srcChromaVPtr == nullptr) {
        LOGE("blit NULL pointer ERROR");
        return false;
    }


    uint8_t *srcChromaUVInterleavedPtr = nullptr;
    bool swapDstUV;
    if (srcChromaElementStrideBytes == 2) {
        if (srcChromaVPtr - srcChromaUPtr == 1) {
            srcChromaUVInterleavedPtr = srcChromaUPtr;  // UVUVUV...
            swapDstUV = false;
        } else if (srcChromaUPtr - srcChromaVPtr == 1) {
            srcChromaUVInterleavedPtr = srcChromaVPtr;  // VUVUVU...
            swapDstUV = true;
        } else {
            // stride is 2 but the pointers are not off by 1.
            return false;
        }
    }

    ANativeWindow *win = ANativeWindow_fromSurface(env, dstSurface);
    ANativeWindow_acquire(win);

    ANativeWindow_Buffer buf;

    if (int32_t err = ANativeWindow_lock(win, &buf, NULL)) {
        LOGE("ANativeWindow_lock failed with error code %d\n", err);
        ANativeWindow_release(win);
        return false;
    }
    LOGE("bob dst: width=%d height=%d\n", buf.width, buf.height);

    ANativeWindow_setBuffersGeometry(win, srcWidth, srcHeight, 0 /*format unchanged*/);
/*
    if (buf.format != IMAGE_FORMAT_YV12) {
        LOGE("ANativeWindow buffer locked but its format was not YV12. %d\n", buf.format);
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }
*/
    if (!checkBufferSizesMatch(srcWidth, srcHeight, &buf)) {
        LOGE("ANativeWindow buffer locked but its size was %d x %d, expected "
                     "%d x %d", buf.width, buf.height, srcWidth, srcHeight);
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }
    uint8_t *dstLumaPtr = reinterpret_cast<uint8_t *>(buf.bits);

    Mat mRgb =*(Mat*)dstLumaPtr;

    cv::cvtColor(mYuv, mRgb, CV_YCrCb2RGB);
#if 0
    uint8_t p[4] = {0xff,0x00,0x00,0x00};
    for( int i=0;i<buf.width; i++) {
        for( int j=0; j<buf.height;j++)
            for(int k=0; k<4;k++)
                *dstLumaPtr++ = p[k];
    }
#endif
    //memcpy(dstLumaPtr, srcLumaPtr, dstLumaSizeBytes);

#if 0
    int32_t srcChromaWidth = srcWidth / 2;
    int32_t srcChromaHeight = srcHeight / 2;

    uint8_t *dstLumaPtr = reinterpret_cast<uint8_t *>(buf.bits);
    if (dstLumaPtr == NULL) {
        LOGE("ANativeWindow dstLumaPtr NULL");
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    uint32_t dstLumaRowStrideBytes = buf.stride;
    uint32_t dstLumaSizeBytes = dstLumaRowStrideBytes * buf.height;
    uint32_t dstChromaRowStrideBytes = ALIGN(buf.stride / 2, 16);
    // Size of one chroma plane.
    uint32_t dstChromaSizeBytes = dstChromaRowStrideBytes * buf.height / 2;
    // Yes, V is actually first.
    uint8_t *dstChromaVPtr = dstLumaPtr + dstLumaSizeBytes;
    uint8_t *dstChromaUPtr = dstLumaPtr + dstLumaSizeBytes + dstChromaSizeBytes;

    // Copy over the luma channel.
    // If strides match, then it's a single copy.
    if (srcLumaRowStrideBytes == dstLumaRowStrideBytes) {
        memcpy(dstLumaPtr, srcLumaPtr, dstLumaSizeBytes);
    } else {
        // Else, copy row by row.
        for (int y = 0; y < srcHeight; y++) {
            uint8_t *srcLumaRow = srcLumaPtr + y * srcLumaRowStrideBytes;
            uint8_t *dstLumaRow = dstLumaPtr + y * dstLumaRowStrideBytes;
            memcpy(dstLumaRow, srcLumaRow, srcLumaRowStrideBytes);
        }
    }

    bool succeeded;

    // Handle the chroma channels.
    // If they are not interleaved, then use memcpy.
    // Otherwise, use Halide to deinterleave.
//    if (srcChromaElementStrideBytes == 1) {
    if (srcChromaElementStrideBytes == 1) {
        // If strides match, then it's a single copy per channel.
        if (srcChromaRowStrideBytes == dstChromaRowStrideBytes) {
            memcpy(dstChromaUPtr, srcChromaUPtr, dstChromaSizeBytes);
            memcpy(dstChromaVPtr, srcChromaVPtr, dstChromaSizeBytes);
        } else {
            // Else, copy row by row.
            for (int y = 0; y < srcHeight; y++) {
                uint8_t *srcChromaURow =
                        srcChromaUPtr + y * srcChromaRowStrideBytes;
                uint8_t *dstChromaURow =
                        dstChromaUPtr + y * srcChromaRowStrideBytes;
                memcpy(dstChromaURow, srcChromaURow, srcChromaRowStrideBytes);
            }
            for (int y = 0; y < srcHeight; y++) {
                uint8_t *srcChromaVRow =
                        srcChromaVPtr + y * srcChromaRowStrideBytes;
                uint8_t *dstChromaVRow =
                        dstChromaVPtr + y * srcChromaRowStrideBytes;
                memcpy(dstChromaVRow, srcChromaVRow, srcChromaRowStrideBytes);
            }
        }
        succeeded = true;
    } else {
        LOGE("dddddd");
#if 0
        // Make these static so that we can reuse device allocations across frames.
        // It doesn't matter now, but useful for GPU backends.
        static buffer_t srcBuf = { 0 };
        static buffer_t dstBuf0 = { 0 };
        static buffer_t dstBuf1 = { 0 };

        srcBuf.host = srcChromaUVInterleavedPtr;
        srcBuf.host_dirty = true;
        srcBuf.extent[0] = 2 * srcChromaWidth;  // src is interleaved.
        srcBuf.extent[1] = srcChromaHeight;
        srcBuf.extent[2] = 0;
        srcBuf.extent[3] = 0;
        srcBuf.stride[0] = 1;
        srcBuf.stride[1] = 2 * srcChromaWidth;
        srcBuf.min[0] = 0;
        srcBuf.min[1] = 0;
        srcBuf.elem_size = 1;

        dstBuf0.host = swapDstUV ? dstChromaVPtr : dstChromaUPtr;
        dstBuf0.extent[0] = srcChromaWidth;  // src and dst width and height match.
        dstBuf0.extent[1] = srcChromaHeight;
        dstBuf0.extent[2] = 0;
        dstBuf0.extent[3] = 0;
        dstBuf0.stride[0] = 1;
        dstBuf0.stride[1] = srcChromaWidth;
        dstBuf0.min[0] = 0;
        dstBuf0.min[1] = 0;
        dstBuf0.elem_size = 1;

        dstBuf1.host = swapDstUV ? dstChromaUPtr : dstChromaVPtr;
        dstBuf1.extent[0] = srcChromaWidth;  // src and dst width and height match.
        dstBuf1.extent[1] = srcChromaHeight;
        dstBuf1.extent[2] = 0;
        dstBuf1.extent[3] = 0;
        dstBuf1.stride[0] = 1;
        dstBuf1.stride[1] = srcChromaWidth;
        dstBuf1.min[0] = 0;
        dstBuf1.min[1] = 0;
        dstBuf1.elem_size = 1;

        // Use Halide to deinterleave the chroma channels.
//        int err = deinterleave(&srcBuf, &dstBuf0, &dstBuf1);
//        if (err != halide_error_code_success) {
//            LOGE("deinterleave failed with error code: %d", err);
//        }
//        succeeded = (err != halide_error_code_success);
#endif
    }
#endif

    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);

    LOGE("blit done");
    return 0;
}
}

extern "C" {
JNIEXPORT void JNICALL
Java_com_neza_myrobot_JNIUtils_test(JNIEnv *env, jobject assetManager) {

}
}


#if 0
// src luma must have an element stride of 1.
extern "C" {
JNIEXPORT bool JNICALL Java_com_example_hogcamera_JNIUtils_edgeDetect(
    JNIEnv *env, jobject obj, jint srcWidth, jint srcHeight,
    jobject srcLumaByteBuffer, jint srcLumaRowStrideBytes, jobject dstSurface) {
    uint8_t *srcLumaPtr = reinterpret_cast<uint8_t *>(
        env->GetDirectBufferAddress(srcLumaByteBuffer));
    if (srcLumaPtr == NULL) {
        return false;
    }

    ANativeWindow *win = ANativeWindow_fromSurface(env, dstSurface);
    ANativeWindow_acquire(win);

    ANativeWindow_Buffer buf;
    if (int err = ANativeWindow_lock(win, &buf, NULL)) {
        LOGE("ANativeWindow_lock failed with error code %d\n", err);
        ANativeWindow_release(win);
        return false;
    }

    ANativeWindow_setBuffersGeometry(win, srcWidth, srcHeight, 0 /*format unchanged*/);

    uint8_t *dstLumaPtr = reinterpret_cast<uint8_t *>(buf.bits);
    if (dstLumaPtr == NULL) {
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    if (buf.format != IMAGE_FORMAT_YV12) {
        LOGE("ANativeWindow buffer locked but its format was not YV12.");
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    if (!checkBufferSizesMatch(srcWidth, srcHeight, &buf)) {
        LOGE("ANativeWindow buffer locked but its size was %d x %d, expected "
                "%d x %d", buf.width, buf.height, srcWidth, srcHeight);
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    uint32_t dstLumaSizeBytes = buf.stride * buf.height;
    uint32_t dstChromaRowStrideBytes = ALIGN(buf.stride / 2, 16);
    // Size of one chroma plane.
    uint32_t dstChromaSizeBytes = dstChromaRowStrideBytes * buf.height / 2;
    uint8_t *dstChromaVPtr = dstLumaPtr + dstLumaSizeBytes;
    uint8_t *dstChromaUPtr = dstLumaPtr + dstLumaSizeBytes + dstChromaSizeBytes;

    // Make these static so that we can reuse device allocations across frames.
    // It doesn't matter now, but useful for GPU backends.
    static buffer_t srcBuf = { 0 };
    static buffer_t dstBuf = { 0 };
    static buffer_t dstChromaBuf = { 0 };

    srcBuf.host = srcLumaPtr;
    srcBuf.host_dirty = true;
    srcBuf.extent[0] = srcWidth;
    srcBuf.extent[1] = srcHeight;
    srcBuf.extent[2] = 0;
    srcBuf.extent[3] = 0;
    srcBuf.stride[0] = 1;
    srcBuf.stride[1] = srcLumaRowStrideBytes;
    srcBuf.min[0] = 0;
    srcBuf.min[1] = 0;
    srcBuf.elem_size = 1;

    dstBuf.host = dstLumaPtr;
    dstBuf.extent[0] = buf.width;  // src and dst width/height actually match.
    dstBuf.extent[1] = buf.height;
    dstBuf.extent[2] = 0;
    dstBuf.extent[3] = 0;
    dstBuf.stride[0] = 1;
    dstBuf.stride[1] = buf.stride;  // src and dst strides actually match.
    dstBuf.min[0] = 0;
    dstBuf.min[1] = 0;
    dstBuf.elem_size = 1;

    static bool first_call = true;
    static unsigned counter = 0;
    static unsigned times[16];
    if (first_call) {
        LOGD("According to Halide, host system has %d cpus\n",
             halide_host_cpu_count());
        first_call = false;
        for (int t = 0; t < 16; t++) {
            times[t] = 0;
        }
    }

    // Set chrominance to 128 to appear grayscale.
    // The dst chroma is guaranteed to be tightly packed since it's YV12.
    memset(dstChromaVPtr, 128, dstChromaSizeBytes * 2);

    int64_t t1 = halide_current_time_ns();
    int err = edge_detect(&srcBuf, &dstBuf);
    if (err != halide_error_code_success) {
        LOGE("edge_detect failed with error code: %d", err);
    }

    int64_t t2 = halide_current_time_ns();
    unsigned elapsed_us = (t2 - t1) / 1000;

    times[counter & 15] = elapsed_us;
    counter++;
    unsigned min = times[0];
    for (int i = 1; i < 16; i++) {
        if (times[i] < min) {
            min = times[i];
        }
    }
    LOGD("Time taken: %d us (minimum: %d us)", elapsed_us, min);

    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);

    return (err != halide_error_code_success);
}

}
#endif
