#if !defined(LITE_VERSION)

#include "AIMenu.h"
#include "modules/ai/ai_app.h"

void AIMenu::optionsMenu() {
    ai::runAIControlApp();
}

void AIMenu::drawIcon(float scale) {
    clearIconArea();

    const int cx = iconCenterX;
    const int cy = iconCenterY;
    const int r = max(8, (int)(scale * 26));
    const int inner = max(3, r / 3);

    // 4-point AI sparkle star
    tft.fillTriangle(cx, cy - r, cx - inner, cy, cx + inner, cy, bruceConfig.priColor);
    tft.fillTriangle(cx, cy + r, cx - inner, cy, cx + inner, cy, bruceConfig.priColor);
    tft.fillTriangle(cx - r, cy, cx, cy - inner, cx, cy + inner, bruceConfig.priColor);
    tft.fillTriangle(cx + r, cy, cx, cy - inner, cx, cy + inner, bruceConfig.priColor);

    // Inner core cutout for crisp contrast
    tft.fillCircle(cx, cy, max(2, inner / 2), bruceConfig.bgColor);

    // Accent mini-sparkle top-right
    const int sx = cx + (int)(r * 0.72f);
    const int sy = cy - (int)(r * 0.72f);
    const int sr = max(3, r / 4);
    tft.drawLine(sx - sr, sy, sx + sr, sy, bruceConfig.priColor);
    tft.drawLine(sx, sy - sr, sx, sy + sr, bruceConfig.priColor);

    // Accent node bottom-left
    const int nx = cx - (int)(r * 0.72f);
    const int ny = cy + (int)(r * 0.72f);
    tft.fillCircle(nx, ny, max(2, sr / 2), bruceConfig.priColor);
}

#endif // !defined(LITE_VERSION)
