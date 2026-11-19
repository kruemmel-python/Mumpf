#pragma once

#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace mumpf {

enum class AnimRow : int {
    Idle = 0,
    RunningRight = 1,
    RunningLeft = 2,
    Waving = 3,
    Jumping = 4,
    Failed = 5,
    Waiting = 6,
    Running = 7,
    Review = 8,
    LookRow0 = 9,
    LookRow1 = 10
};

struct AnimSpec {
    AnimRow row;
    int frameCount;
    float fps;
    bool loop;
};

class SpriteSheet {
public:
    static constexpr int kColumns = 8;
    static constexpr int kRows = 11;
    static constexpr int kCellWidth = 192;
    static constexpr int kCellHeight = 208;

    bool LoadFromFile(const std::wstring& path);
    bool CreateD2DBitmap(ID2D1RenderTarget* target, Microsoft::WRL::ComPtr<ID2D1Bitmap>& outBitmap) const;

    static AnimSpec GetAnimSpec(AnimRow row);
    static D2D1_RECT_F GetCellRect(int row, int col);
    static D2D1_RECT_F GetLookDirectionRect(float angleDegrees);

    // Prüft, ob ein Pixel innerhalb einer Zelle (0..kCellWidth-1, 0..kCellHeight-1) sichtbar ist.
    bool IsPixelOpaque(int row, int col, int localX, int localY) const;

    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsLoaded() const { return !pixels_.empty(); }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> pixels_; // 32bpp PBGRA
};

} // namespace mumpf
