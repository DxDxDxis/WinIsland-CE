#pragma once
namespace wi {
// Scale only animation time. Hold, hover, copy and inactivity clocks are separate.
inline constexpr double TransferMotionSpeed=1.2;
inline constexpr double transferMotionSeconds(double baseline){return baseline/TransferMotionSpeed;}
}
