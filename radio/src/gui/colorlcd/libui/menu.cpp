/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   libopenui - https://github.com/opentx/libopenui
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

#include "menu.h"

#include "edgetx.h"
#include "etx_lv_theme.h"
#include "keyboard_base.h"
#include "menutoolbar.h"
#include "static.h"
#include "table.h"

//-----------------------------------------------------------------------------

class MenuBody : public TableField
{
  enum MENU_DIRECTION { DIRECTION_UP = 1, DIRECTION_DOWN = -1 };

  class MenuLine
  {
    friend class MenuBody;
   public:
    MenuLine(const std::string& text, std::function<void()> onPress,
             std::function<bool()> isChecked, lv_obj_t* icon) :
        text(text), onPress(std::move(onPress)),
        isChecked(std::move(isChecked)), icon(icon) {}
    ~MenuLine() { if (icon) lv_obj_del(icon); }
    lv_obj_t* getIcon() const { return icon; }
   protected:
    std::string text;
    std::function<void()> onPress;
    std::function<bool()> isChecked;
    lv_obj_t* icon;
  };

 public:
  MenuBody(Window* parent, const rect_t& rect) : TableField(parent, rect)
  {
    lv_obj_add_flag(lvobj, LV_OBJ_FLAG_ENCODER_ACCEL);
    setColumnWidth(0, rect.w);
    setAutoEdit();
    setLongPressHandler([=]() { getParentMenu()->handleLongPress(); });
  }

  ~MenuBody() { clearLines(); }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "MenuBody"; }
#endif

  void setIndex(int index)
  {
    if (index < 0 || index >= (int)lines.size()) return;
    if (index == selectedIndex) return;
    selectedIndex = index;

    lv_obj_invalidate(lvobj);
    lv_table_t* table = (lv_table_t*)lvobj;
    table->row_act = index;
    table->col_act = 0;

    lv_coord_t h_before = 0;
    for (uint16_t i = 0; i < table->row_act; i++) h_before += table->row_h[i];
    lv_coord_t row_h = table->row_h[table->row_act];
    lv_coord_t scroll_y = lv_obj_get_scroll_y(lvobj);
    lv_obj_update_layout(lvobj);
    lv_coord_t h = lv_obj_get_height(lvobj);
    lv_coord_t diff_y = 0;
    if (h_before < scroll_y) diff_y = scroll_y - h_before;
    else if (scroll_y + h < h_before + row_h) diff_y = scroll_y + h - h_before - row_h;
    else return;
    lv_obj_scroll_by_bounded(lvobj, 0, diff_y, LV_ANIM_OFF);
  }

  int selection() const { return selectedIndex; }
  int count() const { return lines.size(); }

  void addLine(const MaskBitmap* icon_mask, const std::string& text,
               std::function<void()> onPress, std::function<bool()> isChecked,
               bool update = true)
  {
    lv_obj_t* canvas = nullptr;
    if (icon_mask) {
      canvas = lv_canvas_create(nullptr);
      lv_coord_t w = icon_mask->width;
      lv_coord_t h = icon_mask->height;
      lv_canvas_set_buffer(canvas, (void*)&icon_mask->data[0], w, h, LV_IMG_CF_ALPHA_8BIT);
    }
    auto l = new MenuLine(text, onPress, isChecked, canvas);
    lines.push_back(l);
    if (update) {
      auto idx = lines.size() - 1;
      lv_table_set_cell_value(lvobj, idx, 0, text.c_str());
    }
  }

  void updateLines()
  {
    setRowCount(lines.size());
    for (unsigned int idx = 0; idx < lines.size(); idx++)
      lv_table_set_cell_value(lvobj, idx, 0, lines[idx]->text.c_str());
  }

  void clearLines()
  {
    for (auto itr = lines.begin(); itr != lines.end();) {
      delete *itr;
      itr = lines.erase(itr);
    }
    lines.clear();
  }

  void removeLines()
  {
    clearLines();
    setRowCount(0);
    selectedIndex = 0;
    lv_obj_scroll_to_y(lvobj, 0, LV_ANIM_OFF);
  }

  void onPress(uint16_t row, uint16_t col) override
  {
    Menu* menu = getParentMenu();
    if (row < lines.size()) {
      if (menu->isMultiple()) {
        if (selectedIndex == (int)row) lines[row]->onPress();
        else { setIndex(row); lines[row]->onPress(); }
      } else {
        menu->deleteLater();
        lines[row]->onPress();
      }
    }
  }

  void onDrawBegin(uint16_t row, uint16_t col, lv_obj_draw_part_dsc_t* dsc) override
  {
    if (lines.size() == 0) return;
    lv_canvas_t* icon = (lv_canvas_t*)lines[row]->getIcon();
    if (!icon) return;
    lv_coord_t cell_left = lv_obj_get_style_pad_left(lvobj, LV_PART_ITEMS);
    dsc->label_dsc->ofs_x = icon->img.w + cell_left;
  }

  void onDrawEnd(uint16_t row, uint16_t col, lv_obj_draw_part_dsc_t* dsc) override
  {
    if (row >= lines.size()) return;
    lv_obj_t* icon = lines[row]->getIcon();
    if (icon) {
      lv_draw_img_dsc_t img_dsc; lv_draw_img_dsc_init(&img_dsc);
      lv_area_t coords;
      lv_coord_t area_h = lv_area_get_height(dsc->draw_area);
      lv_coord_t cell_left = lv_obj_get_style_pad_left(lvobj, LV_PART_ITEMS);
      coords.x1 = dsc->draw_area->x1 + cell_left;
      coords.x2 = coords.x1 + ((lv_img_dsc_t*)lv_canvas_get_img(icon))->header.w - 1;
      coords.y1 = dsc->draw_area->y1 + (area_h - ((lv_img_dsc_t*)lv_canvas_get_img(icon))->header.h) / 2;
      coords.y2 = coords.y1 + ((lv_img_dsc_t*)lv_canvas_get_img(icon))->header.h - 1;
      lv_draw_img(dsc->draw_ctx, &img_dsc, &coords, lv_canvas_get_img(icon));
    }
    if (lines[row]->isChecked != nullptr && lines[row]->isChecked()) {
      lv_area_t coords;
      lv_coord_t area_h = lv_area_get_height(dsc->draw_area);
      lv_coord_t cell_right = lv_obj_get_style_pad_right(lvobj, LV_PART_ITEMS);
      lv_coord_t font_h = getFontHeight(FONT(STD));
      coords.x1 = dsc->draw_area->x2 - cell_right - font_h;
      coords.x2 = coords.x1 + font_h;
      coords.y1 = dsc->draw_area->y1 + (area_h - font_h) / 2;
      coords.y2 = coords.y1 + font_h - 1;
      lv_draw_label(dsc->draw_ctx, dsc->label_dsc, &coords, LV_SYMBOL_OK, nullptr);
    }
  }

  void onSelected(uint16_t row, uint16_t col) override { selectedIndex = row; }

 protected:
  std::vector<MenuLine*> lines;
  int selectedIndex = 0;
  Menu* getParentMenu() { return static_cast<Menu*>(getParent()->getParent()); }
};

//-----------------------------------------------------------------------------

static void menu_content_constructor(const lv_obj_class_t* class_p,
                                     lv_obj_t* obj)
{
  etx_solid_bg(obj);
  etx_obj_add_style(obj, styles->outline, LV_PART_MAIN);
  etx_obj_add_style(obj, styles->outline_color_normal, LV_PART_MAIN);
}

static const lv_obj_class_t menu_content_class = {
    .base_class = &window_base_class,
    .constructor_cb = menu_content_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .instance_size = sizeof(lv_obj_t)};

static lv_obj_t* menu_content_create(lv_obj_t* parent)
{
  return etx_create(&menu_content_class, parent);
}

class MenuWindowContent : public NavWindow
{
 public:
  explicit MenuWindowContent(Menu* parent, coord_t popupWidth) :
      NavWindow(parent, rect_t{}, menu_content_create)
  {
    setWindowFlag(OPAQUE);

    // Dark theme - match timer popup style exactly
    lv_obj_set_style_bg_color(lvobj, lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN);

    coord_t w = (popupWidth > 0) ? popupWidth : MENUS_WIDTH;

    lv_obj_center(lvobj);
    setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO, w, LV_SIZE_CONTENT);

    header = new StaticText(this, {0, 0, LV_PCT(100), 0}, "",
                            COLOR_THEME_PRIMARY2_INDEX);
    lv_obj_set_style_bg_color(header->getLvObj(), lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(header->getLvObj(), lv_color_make(0xFF, 0x8C, 0x00), LV_PART_MAIN);
    header->padAll(PAD_SMALL);
    header->hide();

    // Menu lines are TextButton children; use a regular flex container.
    // A TableField/MenuBody only accepts table rows, causing menu content
    // such as the ELRS parameter browser to disappear.
    body = new Window(this, rect_t{0, 0, w, LV_SIZE_CONTENT});
    body->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(body->getLvObj(), lv_color_make(0x18, 0x18, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(body->getLvObj(), 0, LV_PART_MAIN);
    lv_obj_set_style_max_height(body->getLvObj(), LCD_H - EdgeTxStyles::STD_FONT_HEIGHT - PAD_SMALL * 2 - PAD_LARGE * 2, LV_PART_MAIN);
  }

  virtual void setTitle(const std::string& text)
  {
    header->setText(text);
    header->show();
  }

  void onClicked() override { Keyboard::hide(false); }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "MenuWindowContent"; }
#endif

  void updateLines()
  {
    body->clear();
    btns.clear();

    Menu* m = static_cast<Menu*>(getParent());
    for (auto& line : lines) {
      auto cb = line.onPress;
      auto btn = new TextButton(body,
          {0, 0, LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL},
          line.text, [=]() -> uint8_t {
            if (!m->isMultiple()) m->deleteLater();
            cb();
            return 0;
          });
      lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0x28,0x28,0x28), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(btn->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_text_color(btn->getLvObj(), lv_color_white(), LV_PART_MAIN);
      lv_obj_set_style_border_width(btn->getLvObj(), 0, LV_PART_MAIN);
      lv_obj_set_style_outline_width(btn->getLvObj(), 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
      lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0x00,0xA0,0x00), LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_text_color(btn->getLvObj(), lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0xFF,0x8C,0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
      lv_obj_set_style_text_color(btn->getLvObj(), lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
      btns.push_back(btn->getLvObj());
      lv_obj_add_event_cb(btn->getLvObj(), [](lv_event_t* e) {
        auto* self = static_cast<MenuWindowContent*>(lv_event_get_user_data(e));
        if (static_cast<Menu*>(self->getParent())->deleted()) return;
        lv_obj_t* focused = lv_event_get_target(e);
        for (unsigned int i = 0; i < self->btns.size(); i++) {
          if (self->btns[i] == focused) {
            self->sel = i;
            if (self->lines[i].onFocus) self->lines[i].onFocus();
            break;
          }
        }
      }, LV_EVENT_FOCUSED, this);
    }
    lv_obj_update_layout(lvobj);
  }
  void removeLines() { body->clear(); lines.clear(); btns.clear(); sel = 0; }
  int count() { return (int)lines.size(); }
  int selection() { return sel; }
  void setIndex(int idx) {
    if (idx < 0 || idx >= (int)btns.size()) return;
    lv_group_focus_obj(btns[idx]);
    sel = idx;
  }

  void addLine(const MaskBitmap* icon_mask, const std::string& text,
               std::function<void()> onPress, std::function<bool()> isChecked,
               bool update = true, std::function<void()> onFocus = nullptr)
  {
    if (!update) { lines.push_back({text, onPress, isChecked, onFocus}); return; }
    auto cb = onPress;
    Menu* m = static_cast<Menu*>(getParent());
    auto btn = new TextButton(body,
        {0,0,LV_PCT(100), EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL},
        text, [=]() -> uint8_t {
          // Same order as original Menu::onPress
          if (!m->isMultiple()) m->deleteLater();
          cb();
          return 0;
        });
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0x28,0x28,0x28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn->getLvObj(), 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn->getLvObj(), 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0x00,0xA0,0x00), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_black(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn->getLvObj(), lv_color_make(0xFF,0x8C,0x00), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btn->getLvObj(), lv_color_black(), LV_PART_MAIN | LV_STATE_FOCUSED);
    btns.push_back(btn->getLvObj());
    lv_obj_add_event_cb(btn->getLvObj(), [](lv_event_t* e) {
      auto* self = static_cast<MenuWindowContent*>(lv_event_get_user_data(e));
      if (static_cast<Menu*>(self->getParent())->deleted()) return;
      lv_obj_t* focused = lv_event_get_target(e);
      for (unsigned int i = 0; i < self->btns.size(); i++) {
        if (self->btns[i] == focused) {
          self->sel = i;
          if (self->lines[i].onFocus) self->lines[i].onFocus();
          break;
        }
      }
    }, LV_EVENT_FOCUSED, this);
    lines.push_back({text, onPress, isChecked, onFocus});
    lv_obj_update_layout(lvobj);
  }

#if defined(HARDWARE_KEYS)
  void onPressPGUP() override
  {
    Messaging::send(Messaging::MENU_CHANGE_FILTER, -1);
  }

  void onPressPGDN() override
  {
    Messaging::send(Messaging::MENU_CHANGE_FILTER, 1);
  }
#endif

  static LAYOUT_VAL_SCALED(MENUS_WIDTH, 200)

 protected:
  StaticText* header = nullptr;
  Window* body = nullptr;
  struct Line { std::string text; std::function<void()> onPress; std::function<bool()> isChecked; std::function<void()> onFocus; };
  std::vector<Line> lines;
  std::vector<lv_obj_t*> btns;
  int sel = 0;
};

//-----------------------------------------------------------------------------

Menu::Menu(bool multiple, coord_t popupWidth) :
    ModalWindow(true),
    multiple(multiple),
    content(new MenuWindowContent(this, popupWidth))
{
}

void Menu::setToolbar(MenuToolbar* window)
{
  toolbar = window;
  updatePosition();
}

void Menu::updatePosition()
{
  if (toolbar) {
    coord_t cw = lv_obj_get_width(content->getLvObj());
    coord_t ch = lv_obj_get_height(content->getLvObj());
    coord_t tw = lv_obj_get_width(toolbar->getLvObj());
    coord_t th = lv_obj_get_height(toolbar->getLvObj());

    lv_obj_align(toolbar->getLvObj(), LV_ALIGN_CENTER, -cw / 2, 0);
    lv_obj_align(content->getLvObj(), LV_ALIGN_CENTER, tw / 2, 0);

    toolbar->setHeight(max(ch, th));
    content->setHeight(max(ch, th));
  }
}

void Menu::setTitle(std::string text)
{
  content->setTitle(std::move(text));
  updatePosition();
}

void Menu::addLine(const MaskBitmap* icon_mask, const std::string& text,
                   std::function<void()> onPress,
                   std::function<bool()> isChecked)
{
  content->addLine(icon_mask, text, std::move(onPress), std::move(isChecked));
  updatePosition();
}

void Menu::addLine(const std::string &text, std::function<void()> onPress,
                  std::function<bool()> isChecked)
{
  addLine(nullptr, text, onPress, isChecked);
}

void Menu::addLineBuffered(const std::string &text, std::function<void()> onPress,
                           std::function<bool()> isChecked, std::function<void()> onFocus)
{
  content->addLine(nullptr, text, std::move(onPress), std::move(isChecked), false,
                   std::move(onFocus));
}

void Menu::updateLines()
{
  content->updateLines();
  updatePosition();
}

void Menu::removeLines()
{
  content->removeLines();
  updatePosition();
}

void Menu::onCancel()
{
  if (cancelHandler) cancelHandler();
  deleteLater();
}

void Menu::setCancelHandler(std::function<void()> handler)
{
  cancelHandler = std::move(handler);
}

void Menu::setWaitHandler(std::function<void()> handler)
{
  waitHandler = std::move(handler);
}

void Menu::setLongPressHandler(std::function<void()> handler)
{
  longPressHandler = std::move(handler);
}

void Menu::handleLongPress()
{
  if (longPressHandler)
    longPressHandler();
}

unsigned Menu::count() const { return content->count(); }

int Menu::selection() const { return content->selection(); }

void Menu::select(int index) { content->setIndex(index); }

void Menu::checkEvents()
{
  ModalWindow::checkEvents();
  if (waitHandler)
    waitHandler();
}
