#pragma once
// 自動生成（build/AmpLab/adfit.py）: Logic の Amp Designer を測って合わせ込んだフィルターの表。手で直さない
// voice = アンプ単体の音色（GAIN 2・つまみ全部 5）、knob = つまみ 0 / 10 で変わる分、cab = スピーカー＋箱（Ribbon・真ん中・近い）、
// mic = Ribbon との違い、pos = 真ん中→端、dist = 近い→離す
#include "Biquad.h"
namespace ampsim { namespace tables {
struct F { Biquad::Type type; float f, q, g; };
const F voice[5][5] {
    { { Biquad::HighPass, 98.623f, 1.1818f, 0.0f }, { Biquad::Peak, 89.683f, 2.8907f, -2.2011f }, { Biquad::Peak, 497.47f, 0.63234f, 2.4453f }, { Biquad::Peak, 4971.4f, 0.3f, 7.8498f }, { Biquad::HighShelf, 10000.0f, 0.9382f, 12.0f } },   // clean
    { { Biquad::HighPass, 72.578f, 1.2f, 0.0f }, { Biquad::Peak, 209.14f, 1.9006f, 3.4104f }, { Biquad::Peak, 660.58f, 1.1666f, 3.8179f }, { Biquad::Peak, 1000.0f, 0.3f, -1.3584f }, { Biquad::HighShelf, 10000.0f, 0.91085f, 12.0f } },   // tweed
    { { Biquad::HighPass, 78.086f, 0.41691f, 0.0f }, { Biquad::Peak, 250.0f, 1.3254f, 1.2316f }, { Biquad::Peak, 333.08f, 0.68266f, -11.725f }, { Biquad::Peak, 2809.4f, 1.1344f, 2.8741f }, { Biquad::HighShelf, 8681.7f, 1.2f, -2.4454f } },   // chime
    { { Biquad::HighPass, 38.353f, 1.2f, 0.0f }, { Biquad::Peak, 67.184f, 2.354f, -1.6225f }, { Biquad::Peak, 579.28f, 0.3f, -7.9577f }, { Biquad::Peak, 4294.0f, 3.0f, 12.0f }, { Biquad::HighShelf, 7451.2f, 1.2f, 12.0f } },   // crunch
    { { Biquad::HighPass, 44.171f, 1.2f, 0.0f }, { Biquad::Peak, 250.0f, 0.47863f, -10.0f }, { Biquad::Peak, 454.06f, 0.3f, -14.0f }, { Biquad::Peak, 1101.8f, 3.0f, 3.8588f }, { Biquad::HighShelf, 4504.6f, 0.70231f, 12.0f } },   // modern
};
// knob filters: [family clean / crunch / modern][knob bass / mid / treble / presence][0 = turned to 0, 1 = turned to 10][3]
const F knob[3][4][2][3] {
    { { { { Biquad::LowShelf, 179.95f, 0.51373f, -22.618f }, { Biquad::Peak, 560.57f, 1.0133f, 7.0565f }, { Biquad::HighShelf, 800.0f, 0.52655f, 0.038303f } }, { { Biquad::LowShelf, 77.221f, 0.4f, 22.732f }, { Biquad::Peak, 557.59f, 2.4106f, -8.9078f }, { Biquad::HighShelf, 800.0f, 1.5f, -0.058869f } } },
      { { { Biquad::LowShelf, 415.41f, 1.3295f, -0.50081f }, { Biquad::Peak, 502.86f, 1.6268f, -1.8549f }, { Biquad::HighShelf, 800.0f, 0.63203f, -0.21212f } }, { { Biquad::LowShelf, 600.0f, 0.4f, 5.8855f }, { Biquad::Peak, 510.42f, 0.75411f, 9.3736f }, { Biquad::HighShelf, 1790.5f, 0.64233f, 2.8732f } } },
      { { { Biquad::LowShelf, 257.82f, 1.5f, 0.092891f }, { Biquad::Peak, 551.59f, 1.5704f, 2.542f }, { Biquad::HighShelf, 1217.4f, 1.0976f, -29.708f } }, { { Biquad::LowShelf, 324.09f, 1.5f, -0.73295f }, { Biquad::Peak, 579.03f, 1.0218f, 10.336f }, { Biquad::HighShelf, 800.0f, 0.66692f, 9.7526f } } },
      { { { Biquad::LowShelf, 600.0f, 0.96963f, -0.0021625f }, { Biquad::Peak, 3000.0f, 0.40759f, -4.0717f }, { Biquad::HighShelf, 5034.9f, 0.56632f, -5.322f } }, { { Biquad::LowShelf, 600.0f, 0.4f, 0.11668f }, { Biquad::Peak, 3000.0f, 0.51189f, 9.2205f }, { Biquad::HighShelf, 4645.9f, 1.1291f, 11.548f } } } },   // clean
    { { { { Biquad::LowShelf, 424.68f, 0.50182f, -22.523f }, { Biquad::Peak, 1843.3f, 0.9029f, 1.7629f }, { Biquad::HighShelf, 800.0f, 1.5f, -0.036844f } }, { { Biquad::LowShelf, 60.0f, 0.52678f, 9.5807f }, { Biquad::Peak, 646.73f, 0.39756f, -0.4368f }, { Biquad::HighShelf, 6000.0f, 0.4f, 0.044252f } } },
      { { { Biquad::LowShelf, 180.19f, 0.83643f, -1.104f }, { Biquad::Peak, 1363.8f, 0.82198f, -3.1462f }, { Biquad::HighShelf, 800.0f, 0.7461f, -1.7741f } }, { { Biquad::LowShelf, 600.0f, 0.4f, 3.0979f }, { Biquad::Peak, 818.99f, 0.47781f, 6.0367f }, { Biquad::HighShelf, 1159.3f, 0.81409f, 5.3395f } } },
      { { { Biquad::LowShelf, 473.86f, 0.83293f, -0.018237f }, { Biquad::Peak, 1512.2f, 0.46807f, 8.3187f }, { Biquad::HighShelf, 1161.1f, 0.48715f, -13.885f } }, { { Biquad::LowShelf, 600.0f, 0.40778f, 0.017748f }, { Biquad::Peak, 596.01f, 0.67895f, -6.2329f }, { Biquad::HighShelf, 800.0f, 0.50297f, 14.232f } } },
      { { { Biquad::LowShelf, 600.0f, 0.96963f, -0.0021625f }, { Biquad::Peak, 3000.0f, 0.40759f, -4.0717f }, { Biquad::HighShelf, 5034.9f, 0.56632f, -5.322f } }, { { Biquad::LowShelf, 600.0f, 0.4f, 0.11668f }, { Biquad::Peak, 3000.0f, 0.51189f, 9.2205f }, { Biquad::HighShelf, 4645.9f, 1.1291f, 11.548f } } } },   // crunch
    { { { { Biquad::LowShelf, 188.36f, 0.55446f, -10.61f }, { Biquad::Peak, 434.49f, 0.86314f, 3.2801f }, { Biquad::HighShelf, 6000.0f, 0.83723f, -0.011957f } }, { { Biquad::LowShelf, 189.97f, 0.55164f, 10.565f }, { Biquad::Peak, 434.11f, 0.85963f, -3.3117f }, { Biquad::HighShelf, 6000.0f, 0.4f, 0.025386f } } },
      { { { Biquad::LowShelf, 554.95f, 0.91806f, -0.0068124f }, { Biquad::Peak, 790.36f, 0.70717f, -5.9621f }, { Biquad::HighShelf, 1823.5f, 1.4241f, 0.14866f } }, { { Biquad::LowShelf, 76.772f, 1.0436f, 0.0024294f }, { Biquad::Peak, 783.78f, 0.70798f, 5.9056f }, { Biquad::HighShelf, 1715.6f, 1.5f, -0.15292f } } },
      { { { Biquad::LowShelf, 190.94f, 1.3873f, 5.6843e-05f }, { Biquad::Peak, 3000.0f, 0.43515f, 2.1017f }, { Biquad::HighShelf, 2028.4f, 0.91361f, -4.2399f } }, { { Biquad::LowShelf, 60.0f, 0.95284f, 0.0066887f }, { Biquad::Peak, 1130.7f, 1.2686f, -1.0123f }, { Biquad::HighShelf, 2042.9f, 1.5f, 1.1748f } } },
      { { { Biquad::LowShelf, 600.0f, 0.96963f, -0.0021625f }, { Biquad::Peak, 3000.0f, 0.40759f, -4.0717f }, { Biquad::HighShelf, 5034.9f, 0.56632f, -5.322f } }, { { Biquad::LowShelf, 600.0f, 0.4f, 0.11668f }, { Biquad::Peak, 3000.0f, 0.51189f, 9.2205f }, { Biquad::HighShelf, 4645.9f, 1.1291f, 11.548f } } } },   // modern
};
const F cab[5][7] {
    { { Biquad::HighPass, 62.957f, 1.5f, 0.0f }, { Biquad::Peak, 111.53f, 0.91892f, 9.138f }, { Biquad::Peak, 770.99f, 1.4604f, 6.0522f }, { Biquad::Peak, 2013.8f, 1.7783f, 3.4118f }, { Biquad::Peak, 4446.8f, 1.3902f, 7.2778f }, { Biquad::LowPass, 9000.0f, 0.44689f, 0.0f }, { Biquad::LowPass, 4841.4f, 0.5f, 0.0f } },   // 1x12 Open
    { { Biquad::HighPass, 40.0f, 1.1276f, 0.0f }, { Biquad::Peak, 152.92f, 3.0f, -5.8247f }, { Biquad::Peak, 239.11f, 0.4f, 5.4387f }, { Biquad::Peak, 1292.3f, 3.0f, -3.7673f }, { Biquad::Peak, 5936.9f, 4.0f, -5.8613f }, { Biquad::LowPass, 6917.6f, 0.4f, 0.0f }, { Biquad::LowPass, 4000.0f, 2.2448f, 0.0f } },   // 2x12 Open
    { { Biquad::HighPass, 72.035f, 1.4687f, 0.0f }, { Biquad::Peak, 150.13f, 0.5f, 10.0f }, { Biquad::Peak, 369.38f, 0.51118f, 6.0836f }, { Biquad::Peak, 2681.4f, 1.3346f, 12.0f }, { Biquad::Peak, 4610.7f, 2.1089f, 9.8124f }, { Biquad::LowPass, 3500.0f, 0.62489f, 0.0f }, { Biquad::LowPass, 8338.7f, 0.5f, 0.0f } },   // 4x12 Closed
    { { Biquad::HighPass, 54.32f, 0.64697f, 0.0f }, { Biquad::Peak, 157.55f, 0.5f, 10.0f }, { Biquad::Peak, 676.82f, 3.0f, 4.0487f }, { Biquad::Peak, 2144.9f, 3.0f, 5.8775f }, { Biquad::Peak, 7000.0f, 2.3505f, -12.0f }, { Biquad::LowPass, 9000.0f, 0.4f, 0.0f }, { Biquad::LowPass, 4507.9f, 2.5f, 0.0f } },   // 4x12 Modern
    { { Biquad::HighPass, 77.624f, 0.95337f, 0.0f }, { Biquad::Peak, 103.82f, 2.515f, 5.295f }, { Biquad::Peak, 312.96f, 1.6569f, 0.86822f }, { Biquad::Peak, 1363.0f, 2.9699f, -7.4435f }, { Biquad::Peak, 6362.8f, 3.0107f, -12.0f }, { Biquad::LowPass, 3649.2f, 0.57125f, 0.0f }, { Biquad::LowPass, 5541.8f, 1.1416f, 0.0f } },   // 4x10 Tweed
};
const F micDynamic[4] { { Biquad::LowShelf, 104.91f, 0.5f, -7.2938f }, { Biquad::Peak, 934.77f, 2.4105f, -3.6177f }, { Biquad::Peak, 4794.0f, 0.5f, 5.1624f }, { Biquad::HighShelf, 11000.0f, 1.1794f, 5.4264f } };
const F micCondenser[4] { { Biquad::LowShelf, 173.18f, 1.2f, -2.6282f }, { Biquad::Peak, 2519.8f, 1.4687f, 2.7263f }, { Biquad::Peak, 7774.5f, 3.0f, -6.4379f }, { Biquad::HighShelf, 5700.1f, 1.112f, 7.9505f } };
const F posEdge[3] { { Biquad::Peak, 800.0f, 0.4f, 5.6187f }, { Biquad::Peak, 4437.9f, 0.4f, -11.404f }, { Biquad::HighShelf, 4764.4f, 0.4f, 5.0f } };
const F distFar[4] { { Biquad::LowShelf, 168.24f, 0.91461f, 1.1587f }, { Biquad::Peak, 1012.8f, 3.0f, 3.5585f }, { Biquad::Peak, 2000.0f, 0.83806f, -1.5894f }, { Biquad::HighShelf, 5212.3f, 1.2f, -0.96584f } };
}}
