// Small GTK4 helpers: value bindings between widgets and the profile JSON, a curve editor and an
// input preview. Plain GTK 4, no libadwaita.
#pragma once

#include "profile.h"
#include "protocol.h"

#include <gtk/gtk.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace bw::ui {

// G_CALLBACK is a macro, so lambdas with top-level commas break it; this is a function instead.
template <typename F>
inline GCallback cb(F fn) {
    return reinterpret_cast<GCallback>(fn);
}

// A widget whose integer value mirrors something in the model.
struct Binding {
    GtkWidget* widget = nullptr;
    std::function<int()> get;
    std::function<void(int)> set;
    std::function<bool()> sensitive;  // optional
    class Binder* owner = nullptr;
};

class Binder {
public:
    // Called after the user changed a value (not during refresh()).
    std::function<void()> onChanged;

    GtkWidget* scale(int min, int max, std::function<int()> get, std::function<void(int)> set);
    GtkWidget* toggle(std::function<int()> get, std::function<void(int)> set);  // GtkSwitch
    GtkWidget* dropdown(const std::vector<std::string>& labels, std::function<int()> get,
                        std::function<void(int)> set, bool searchable = false);
    // A row of grouped toggle buttons (one per label), for short choices.
    GtkWidget* segmented(const std::vector<std::string>& labels, std::function<int()> get, std::function<void(int)> set);
    // A custom-drawn widget that reads the model itself: redrawn on every refresh().
    GtkWidget* redraw(GtkWidget* w);
    // Extra hook for sensitivity, evaluated on every refresh.
    void setSensitive(GtkWidget* w, std::function<bool()> pred);
    // Re-reads every bound value from the model.
    void refresh();
    bool updating() const { return updating_; }

private:
    Binding& add(GtkWidget* w, std::function<int()> get, std::function<void(int)> set);
    std::deque<Binding> bindings_;
    bool updating_ = false;
};

// Integer accessors for a JSON value located by `locate` at call time.
std::function<int()> jsonGet(std::function<Json&()> locate);
std::function<void(int)> jsonSet(std::function<Json&()> locate, bool asBool = false);

// Response curve editor: draws deadzone/anti-deadzone/curve of an object that has "deadzone" and
// "curve" members; the three middle points can be dragged when curve.type == 3 (custom).
GtkWidget* curveEditor(std::function<Json&()> owner, std::function<void()> changed);
void curveEditorRedraw(GtkWidget* editor);

// Stick position preview (-32768..32767), redrawn from `value`.
GtkWidget* stickView(std::function<std::pair<int, int>()> value);

// A generic gamepad outline (original drawing, theme foreground colour), purely decorative.
GtkWidget* controllerArt(int width, int height);

// Two-handle range slider over two integer model values (lo <= hi), e.g. a dead zone's start and end.
GtkWidget* rangeSlider(Binder& binder, int min, int max, std::function<Json&()> lo, std::function<Json&()> hi);

// Small original glyph for card titles: "vibration", "trigger", "stick", "motion", "controller", "buttons",
// "profile", "curve", "layout", "test".
GtkWidget* glyph(const char* name);

// Layout helpers
GtkWidget* section(const char* title, const char* glyphName = nullptr);  // card with a title (and glyph)
GtkWidget* sectionBody(GtkWidget* section);            // the box to append rows to
GtkWidget* row(const char* label, GtkWidget* control, const char* hint = nullptr);
GtkWidget* sideSwitcher(const std::vector<std::string>& labels, std::function<void(int)> onSelect);

}  // namespace bw::ui
