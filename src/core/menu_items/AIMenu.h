#ifndef __AI_MENU_H__
#define __AI_MENU_H__

#if !defined(LITE_VERSION)

#include <MenuItemInterface.h>

class AIMenu : public MenuItemInterface {
public:
    AIMenu() : MenuItemInterface("AI") {}

    void optionsMenu(void) override;
    void drawIcon(float scale) override;
    bool hasTheme() override { return false; }
    const String &themePath() override {
        static const String empty = "";
        return empty;
    }
};

#endif // !defined(LITE_VERSION)
#endif // __AI_MENU_H__
