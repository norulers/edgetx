/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "page.h"

#include "edgetx.h"
#include "etx_lv_theme.h"
#include "keyboard_base.h"
#include "mainwindow.h"
#include "pagegroup.h"
#include "quick_menu.h"
#include "theme_manager.h"
#include "view_main.h"

PageHeader::PageHeader(Window* parent, EdgeTxIcon icon) :
    Window(parent, {0, 0, LCD_W, EdgeTxStyles::MENU_HEADER_HEIGHT})
{
  setWindowFlag(NO_FOCUS | OPAQUE);

  lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(lvobj, LV_GRAD_DIR_NONE, LV_PART_MAIN);

  new HeaderIcon(this, icon);

  title = new StaticText(this,
                         {PAGE_TITLE_LEFT, PAGE_TITLE_TOP,
                          LCD_W - PAGE_TITLE_LEFT, EdgeTxStyles::STD_FONT_HEIGHT},
                         "", COLOR_THEME_QM_FG_INDEX);
}

PageHeader::PageHeader(Window* parent, const char* iconFile) :
    Window(parent, {0, 0, LCD_W, EdgeTxStyles::MENU_HEADER_HEIGHT})
{
  setWindowFlag(NO_FOCUS | OPAQUE);

  lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(lvobj, LV_GRAD_DIR_NONE, LV_PART_MAIN);

  new HeaderIcon(this, iconFile);

  title = new StaticText(this,
                         {PAGE_TITLE_LEFT, PAGE_TITLE_TOP,
                          LCD_W - PAGE_TITLE_LEFT, EdgeTxStyles::STD_FONT_HEIGHT},
                         "", COLOR_THEME_QM_FG_INDEX);
}

StaticText* PageHeader::setTitle2(std::string txt)
{
  if (title2 == nullptr) {
    title2 = new StaticText(this,
                            {PAGE_TITLE_LEFT, PAGE_TITLE_TOP + EdgeTxStyles::STD_FONT_HEIGHT,
                             LCD_W - PAGE_TITLE_LEFT, EdgeTxStyles::STD_FONT_HEIGHT},
                            "", COLOR_THEME_QM_FG_INDEX);
  }
  title2->setText(std::move(txt));
  return title2;
}

Page::Page(EdgeTxIcon icon, PaddingSize padding, bool pauseRefresh) :
    NavWindow(MainWindow::instance(), {0, 0, LCD_W, LCD_H})
{
  if (pauseRefresh)
    lv_obj_enable_style_refresh(false);

  header = new PageHeader(this, icon);

#if VERSION_MAJOR > 2
  new HeaderBackIcon(header);
#endif

#if defined(HARDWARE_TOUCH)
#if VERSION_MAJOR == 2
  addCustomButton(0, 0, [=]() { onCancel(); });
#else
  addCustomButton(0, 0, [=]() { QuickMenu::openQuickMenu(); });
  addCustomButton(LCD_W - EdgeTxStyles::MENU_HEADER_HEIGHT, 0, [=]() { onCancel(); });
#endif
#endif

  body = new Window(this,
                    {0, EdgeTxStyles::MENU_HEADER_HEIGHT, LCD_W, LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT});
  body->setWindowFlag(NO_FOCUS | OPAQUE);
  lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(body->getLvObj(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(body->getLvObj(), LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(body->getLvObj(), LV_DIR_VER);

  lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(lvobj, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_max_height(body->getLvObj(), LCD_H - EdgeTxStyles::MENU_HEADER_HEIGHT,
                              LV_PART_MAIN);

  pushLayer(true);

  body->padAll(padding);

  quickMenuMsg.subscribe(Messaging::QUICK_MENU_ITEM_SELECT,
      [=](uint32_t param) {
        onCancel();
      });
}

void Page::onCancel()
{
  if (!_deleted)
    deleteLater();
}

void Page::onClicked() { Keyboard::hide(false); }

void Page::enableRefresh()
{
  lv_obj_enable_style_refresh(true);
  lv_obj_refresh_style(lvobj, LV_PART_ANY, LV_STYLE_PROP_ANY);
}

NavWindow* Page::navWindow()
{
  auto p = Window::topWindow();
  if (p->isNavWindow()) return (NavWindow*)p;
  return nullptr;
}

// Recursively force every label's own text color to white. Needed because
// StaticText/DynamicText default to COLOR_THEME_PRIMARY1_INDEX (dark text
// meant for a light background) set as a *local* style, which is not
// overridden by an inherited text_color on an ancestor. Left unfixed, those
// labels become invisible (dark-on-dark) once the row backgrounds are
// darkened below, making it look like menu content went missing.
static void darkenLabelsRecursive(lv_obj_t* obj)
{
  uint32_t cnt = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t* child = lv_obj_get_child(obj, i);
    if (lv_obj_check_type(child, &lv_label_class)) {
      lv_obj_set_style_text_color(child, lv_color_white(), LV_PART_MAIN);
    }
    darkenLabelsRecursive(child);
  }
}

void Page::setDarkBody()
{
  lv_obj_t* b = body->getLvObj();
  lv_obj_set_style_bg_color(b, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(b, lv_color_white(), LV_PART_MAIN);
  // Dark scrollbar — always visible
  lv_obj_set_scrollbar_mode(b, LV_SCROLLBAR_MODE_ON);
  static lv_style_t sbStyle;
  lv_style_init(&sbStyle);
  lv_style_set_bg_color(&sbStyle, lv_color_make(0x66, 0x66, 0x66));
  lv_style_set_bg_opa(&sbStyle, LV_OPA_COVER);
  lv_style_set_width(&sbStyle, 6);
  lv_obj_add_style(b, &sbStyle, LV_PART_SCROLLBAR);
  // Apply page group control style to each SetupLine row (level-1)
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(b); i++) {
    lv_obj_t* c1 = lv_obj_get_child(b, i);
    stylePageGroupControl(c1);
    for (uint32_t j = 0; j < lv_obj_get_child_cnt(c1); j++) {
      lv_obj_t* c2 = lv_obj_get_child(c1, j);
      // Style dropdown/choice controls
      bool isChoice = false;
      bool isLabel = lv_obj_check_type(c2, &lv_label_class);
      for (uint32_t gi = 0; gi < lv_obj_get_child_cnt(c2); gi++) {
        lv_obj_t* gc = lv_obj_get_child(c2, gi);
        if (lv_obj_check_type(gc, &lv_img_class)) {
          const void* src = lv_img_get_src(gc);
          if (lv_img_src_get_type(src) == LV_IMG_SRC_SYMBOL) {
            const char* sym = (const char*)src;
            if (strcmp(sym, LV_SYMBOL_DOWN) == 0 || strcmp(sym, LV_SYMBOL_DIRECTORY) == 0) {
              isChoice = true;
              lv_obj_set_style_img_recolor(gc, lv_color_white(), LV_PART_MAIN);
              lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN);
              break;
            }
          }
        }
      }
      if (isChoice)
        stylePageGroupControl(c2);
      else if (!isLabel) {
        // All other controls (sliders, toggles, number edits): visible dark bg
        lv_obj_set_style_bg_color(c2, lv_color_make(0x50, 0x50, 0x50), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(c2, LV_OPA_COVER, LV_PART_MAIN);
      }
      else {
        lv_obj_set_style_bg_color(c2, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(c2, LV_OPA_COVER, LV_PART_MAIN);
      }
      for (uint32_t k = 0; k < lv_obj_get_child_cnt(c2); k++) {
        lv_obj_t* c3 = lv_obj_get_child(c2, k);
        lv_obj_set_style_bg_color(c3, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(c3, LV_OPA_COVER, LV_PART_MAIN);
      }
    }
  }
  // Force every label (any depth) to white text so nothing goes dark-on-dark
  darkenLabelsRecursive(b);
  enableRefresh();
}

void Page::setDarkHeader(EdgeTxIcon icon)
{
  // Dark FPV header: hide original canvas icons, place new ones with orange color
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(header->getLvObj()); i++) {
    auto child = lv_obj_get_child(header->getLvObj(), i);
    if (lv_obj_check_type(child, &lv_canvas_class))
      lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
  }
  auto leftBg = new StaticIcon(header, 0, 0, ICON_TOPLEFT_BG, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(leftBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(leftBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  leftBg->setTop((EdgeTxStyles::MENU_HEADER_HEIGHT - leftBg->height()) / 2);
  auto leftIco = new StaticIcon(leftBg, 0, 0, icon, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(leftIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(leftIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
  leftIco->center(leftBg->width() + PAD_MEDIUM, leftBg->height());
  auto rightBg = new StaticIcon(header, LCD_W, 0, ICON_TOPRIGHT_BG, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(rightBg->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(rightBg->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
  rightBg->setPos(LCD_W - rightBg->width(), (EdgeTxStyles::MENU_HEADER_HEIGHT - rightBg->height()) / 2);
  auto rightIco = new StaticIcon(rightBg, 0, 0, ICON_BTN_CLOSE, COLOR_THEME_PRIMARY2_INDEX);
  lv_obj_set_style_img_recolor_opa(rightIco->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(rightIco->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
  rightIco->center(rightBg->width() + PAD_MEDIUM, rightBg->height());
}

#if defined(HARDWARE_KEYS)
void Page::doKeyShortcut(event_t event)
{
  QMPage pg = g_eeGeneral.getKeyShortcut(event);
  if (pg == QM_OPEN_QUICK_MENU) {
    QuickMenu::openQuickMenu();
  } else if (pg != QM_NONE) {
    onCancel();
    auto p = navWindow();
    if (p)
      p->doKeyShortcut(event);
  }
}
void Page::onLongPressRTN() { onCancel(); }
#endif

SubPage::SubPage(EdgeTxIcon icon, const char* title, const char* subtitle, bool pauseRefresh) :
  Page(icon, PAD_SMALL, pauseRefresh)
{
  // FPV dark theme: solid bg, no gradient
  lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(lvobj, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(body->getLvObj(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(header->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(header->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(header->getLvObj(), LV_GRAD_DIR_NONE, LV_PART_MAIN);

  body->padBottom(PAD_LARGE * 2);

  header->setTitle(title);
  header->setTitle2(subtitle);
}

SubPage::SubPage(EdgeTxIcon icon, const char* title, const char* subtitle, const SetupLineDef* setupLines) :
  Page(icon, PAD_SMALL, true)
{
  // FPV dark theme: solid bg, no gradient
  lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(lvobj, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(body->getLvObj(), LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(header->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(header->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(header->getLvObj(), LV_GRAD_DIR_NONE, LV_PART_MAIN);

  body->padBottom(PAD_LARGE * 2);

  header->setTitle(title);
  header->setTitle2(subtitle);

  SetupLine::showLines(body, y, EDT_X, PAD_SMALL, setupLines);

  // Apply FPV dark theme to setup line elements
  for (uint32_t ci = 0; ci < lv_obj_get_child_cnt(body->getLvObj()); ci++) {
    lv_obj_t* setupLine = lv_obj_get_child(body->getLvObj(), ci);
    for (uint32_t si = 0; si < lv_obj_get_child_cnt(setupLine); si++) {
      lv_obj_t* sc = lv_obj_get_child(setupLine, si);
      if (lv_obj_check_type(sc, &lv_label_class)) {
        lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
      }
      if (lv_obj_check_type(sc, &lv_textarea_class)) {
        lv_obj_set_style_bg_color(sc, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
      }
      for (uint32_t gi = 0; gi < lv_obj_get_child_cnt(sc); gi++) {
        lv_obj_t* gc = lv_obj_get_child(sc, gi);
        if (lv_obj_check_type(gc, &lv_img_class)) {
          const void* src = lv_img_get_src(gc);
          if (lv_img_src_get_type(src) == LV_IMG_SRC_SYMBOL) {
            const char* sym = (const char*)src;
            if (strcmp(sym, LV_SYMBOL_DOWN) == 0 || strcmp(sym, LV_SYMBOL_DIRECTORY) == 0) {
              lv_obj_set_style_bg_color(sc, lv_color_make(0x28, 0x28, 0x28), LV_PART_MAIN);
              lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, LV_PART_MAIN);
              lv_obj_set_style_text_color(sc, lv_color_white(), LV_PART_MAIN);
              lv_obj_set_style_bg_color(sc, lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
              lv_obj_set_style_text_color(sc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
              lv_obj_set_style_img_recolor(gc, lv_color_white(), LV_PART_MAIN);
              lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN);
              lv_obj_set_style_img_recolor(gc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
              lv_obj_set_style_img_recolor_opa(gc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
              for (uint32_t ki = 0; ki < lv_obj_get_child_cnt(sc); ki++) {
                lv_obj_t* kc = lv_obj_get_child(sc, ki);
                if (lv_obj_check_type(kc, &lv_label_class)) {
                  lv_obj_set_style_text_color(kc, lv_color_white(), LV_PART_MAIN);
                  lv_obj_set_style_text_color(kc, lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
                }
              }
              break;
            }
          }
        }
      }
    }
  }

  enableRefresh();
}

Window* SubPage::setupLine(const char* title, std::function<void(SetupLine*, coord_t, coord_t)> createEdit, coord_t lblYOffset)
{
  auto w = new SetupLine(body, y, EDT_X, PAD_SMALL, title, createEdit, lblYOffset);
  y += w->height() + PAD_SMALL;
  return w;
}

void SubPage::useFlexLayout()
{
  body->setFlexLayout();
}
