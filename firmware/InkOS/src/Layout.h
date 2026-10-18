#pragma once

namespace ink::layout {
// Native 800 x 480 pixels: shared page edges and the Home column grid.
constexpr int Margin = 12, Right = 787, ContentWidth = 776, Bottom = 468;
constexpr int HeaderBaseline = 28, HeaderRule = 42;
constexpr int HeaderTitleWidth = 288;
constexpr int HomeLeftRight = 436, HomeDivider = 456, HomeRight = 476;
constexpr int HomeLowerRule = 240, HomeBottom = 464;
// Lower Home band (0.7.10): three columns on one baseline grid. Spacing scale 4/8/12/16 px;
// 12 px inner padding, dividers start 16 px below the rule, last baselines 16 px above the bottom.
constexpr int HomeBlockTop = 256, WeatherDivider = 236, MoonDivider = 336, SolarLeft = 348;
constexpr int HomeRowLabel[3] = {288, 356, 424}; // 14 px labels (PREV / NEXT / Kp)
constexpr int HomeRowValue[3] = {312, 380, 448}; // 20 px values; shared by weather, moon and sun
constexpr int HomeIconTop = 272;                // 64 px weather icon and moon disc box
} // namespace ink::layout
