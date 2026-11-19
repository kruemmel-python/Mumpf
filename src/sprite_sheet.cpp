#include "sprite_sheet.h"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace mumpf {

bool SpriteSheet::LoadFromFile(const std::wstring& path) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return false;

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    UINT w = 0, h = 0;
    hr = converter->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0) return false;

    width_ = static_cast<int>(w);
    height_ = static_cast<int>(h);
    const UINT stride = w * 4;
    pixels_.resize(static_cast<size_t>(stride) * h);

    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels_.size()), pixels_.data());
    if (FAILED(hr)) {
        pixels_.clear();
        width_ = 0;
        height_ = 0;
        return false;
    }

    return true;
}

bool SpriteSheet::CreateD2DBitmap(ID2D1RenderTarget* target, ComPtr<ID2D1Bitmap>& outBitmap) const {
    if (!target || pixels_.empty()) return false;

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    HRESULT hr = target->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(width_), static_cast<UINT32>(height_)),
        pixels_.data(),
        static_cast<UINT32>(width_ * 4),
        props,
        &outBitmap);
    return SUCCEEDED(hr);
}

AnimSpec SpriteSheet::GetAnimSpec(AnimRow row) {
    switch (row) {
        case AnimRow::Idle:         return { AnimRow::Idle,         6, 5.0f, true };
        case AnimRow::RunningRight: return { AnimRow::RunningRight, 8, 12.0f, true };
        case AnimRow::RunningLeft:  return { AnimRow::RunningLeft,  8, 12.0f, true };
        case AnimRow::Waving:       return { AnimRow::Waving,       4, 7.0f, true };
        case AnimRow::Jumping:      return { AnimRow::Jumping,      5, 9.0f, false };
        case AnimRow::Failed:       return { AnimRow::Failed,       8, 8.0f, false };
        case AnimRow::Waiting:      return { AnimRow::Waiting,      6, 6.0f, true };
        case AnimRow::Running:      return { AnimRow::Running,      6, 9.0f, true };
        case AnimRow::Review:       return { AnimRow::Review,       6, 7.0f, true };
        default:                    return { AnimRow::Idle,         6, 5.0f, true };
    }
}

D2D1_RECT_F SpriteSheet::GetCellRect(int row, int col) {
    const float x = static_cast<float>(col * kCellWidth);
    const float y = static_cast<float>(row * kCellHeight);
    return D2D1::RectF(x, y, x + static_cast<float>(kCellWidth), y + static_cast<float>(kCellHeight));
}

D2D1_RECT_F SpriteSheet::GetLookDirectionRect(float angleDegrees) {
    float norm = std::fmod(angleDegrees, 360.0f);
    if (norm < 0.0f) norm += 360.0f;
    int idx = static_cast<int>(std::round(norm / 22.5f)) % 16;
    int row = (idx < 8) ? 9 : 10;
    int col = idx % 8;
    return GetCellRect(row, col);
}

bool SpriteSheet::IsPixelOpaque(int row, int col, int localX, int localY) const {
    if (pixels_.empty()) return false;
    if (localX < 0 || localX >= kCellWidth || localY < 0 || localY >= kCellHeight) return false;
    const int px = col * kCellWidth + localX;
    const int py = row * kCellHeight + localY;
    if (px < 0 || px >= width_ || py < 0 || py >= height_) return false;
    const size_t offset = (static_cast<size_t>(py) * width_ + px) * 4;
    return pixels_[offset + 3] > 20;
}

} // namespace mumpf
