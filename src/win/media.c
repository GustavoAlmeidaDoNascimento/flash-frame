#include "app.h"

#define COBJMACROS
#include <initguid.h>
#include <mmsystem.h>
#include <objbase.h>
#include <psapi.h>
#include <wincodec.h>

#include <stdio.h>
#include <string.h>

HBITMAP load_image_file(const wchar_t *path, int *width, int *height) {
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    IWICBitmapSource *source = NULL;
    HBITMAP bitmap = NULL;
    HDC screen = NULL;
    void *bits = NULL;
    UINT w = 0;
    UINT h = 0;
    UINT nw;
    UINT nh;
    BITMAPINFO info;
    if (width) {
        *width = 0;
    }
    if (height) {
        *height = 0;
    }
    if (!path || FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory))) {
        return NULL;
    }
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) {
        goto done;
    }
    if (FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame))) {
        goto done;
    }
    IWICBitmapFrameDecode_GetSize(frame, &w, &h);
    if (w == 0 || h == 0) {
        goto done;
    }
    nw = w;
    nh = h;
    if (w > 640 || h > 480) {
        double sx = 640.0 / (double)w;
        double sy = 480.0 / (double)h;
        double scale = sx < sy ? sx : sy;
        nw = (UINT)((double)w * scale);
        nh = (UINT)((double)h * scale);
        if (nw < 1) {
            nw = 1;
        }
        if (nh < 1) {
            nh = 1;
        }
        if (FAILED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler))) {
            goto done;
        }
        if (FAILED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)frame, nw, nh, WICBitmapInterpolationModeFant))) {
            goto done;
        }
        source = (IWICBitmapSource *)scaler;
    } else {
        source = (IWICBitmapSource *)frame;
    }
    if (FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter))) {
        goto done;
    }
    if (FAILED(IWICFormatConverter_Initialize(converter, source, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
        goto done;
    }
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = (LONG)nw;
    info.bmiHeader.biHeight = -(LONG)nh;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    screen = GetDC(NULL);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap || !bits || FAILED(IWICFormatConverter_CopyPixels(converter, NULL, nw * 4, nw * nh * 4, (BYTE *)bits))) {
        if (bitmap) {
            DeleteObject(bitmap);
            bitmap = NULL;
        }
        goto done;
    }
    if (width) {
        *width = (int)nw;
    }
    if (height) {
        *height = (int)nh;
    }
done:
    if (screen) {
        ReleaseDC(NULL, screen);
    }
    if (converter) {
        IWICFormatConverter_Release(converter);
    }
    if (scaler) {
        IWICBitmapScaler_Release(scaler);
    }
    if (frame) {
        IWICBitmapFrameDecode_Release(frame);
    }
    if (decoder) {
        IWICBitmapDecoder_Release(decoder);
    }
    if (factory) {
        IWICImagingFactory_Release(factory);
    }
    return bitmap;
}

void audio_stop(void) {
    mciSendStringW(L"close ffSnd", NULL, 0, NULL);
}

void audio_play(const wchar_t *path) {
    wchar_t command[1200];
    audio_stop();
    if (!path || !path[0]) {
        return;
    }
    _snwprintf(command, 1200, L"open \"%s\" alias ffSnd", path);
    if (mciSendStringW(command, NULL, 0, NULL) != 0) {
        return;
    }
    mciSendStringW(L"play ffSnd", NULL, 0, NULL);
}

unsigned long long sample_working_set(void) {
    PROCESS_MEMORY_COUNTERS counters;
    memset(&counters, 0, sizeof(counters));
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return 0;
    }
    return counters.WorkingSetSize;
}
