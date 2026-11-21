#include "widgets.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <memory>

namespace bw::ui {

// ---- bindings ---------------------------------------------------------------

Binding& Binder::add(GtkWidget* w, std::function<int()> get, std::function<void(int)> set) {
    bindings_.push_back(Binding{w, std::move(get), std::move(set), {}, this});
    return bindings_.back();
}

static void changedFrom(Binding* b, int value) {
    if (b->owner->updating()) return;
    b->set(value);
    if (b->owner->onChanged) b->owner->onChanged();
}

GtkWidget* Binder::scale(int min, int max, std::function<int()> get, std::function<void(int)> set) {
    GtkWidget* s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, 1);
    gtk_scale_set_digits(GTK_SCALE(s), 0);
    gtk_scale_set_draw_value(GTK_SCALE(s), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(s), GTK_POS_RIGHT);
    gtk_widget_set_hexpand(s, TRUE);
    gtk_widget_set_size_request(s, 220, -1);
    Binding& b = add(s, std::move(get), std::move(set));
    g_signal_connect(s, "value-changed", cb(+[](GtkRange* r, gpointer data) {
                         changedFrom(static_cast<Binding*>(data), int(std::lround(gtk_range_get_value(r))));
                     }),
                     &b);
    return s;
}

GtkWidget* Binder::toggle(std::function<int()> get, std::function<void(int)> set) {
    GtkWidget* s = gtk_switch_new();
    gtk_widget_set_halign(s, GTK_ALIGN_END);
    gtk_widget_set_valign(s, GTK_ALIGN_CENTER);
    Binding& b = add(s, std::move(get), std::move(set));
    g_signal_connect(s, "notify::active", cb(+[](GObject* o, GParamSpec*, gpointer data) {
                         changedFrom(static_cast<Binding*>(data), gtk_switch_get_active(GTK_SWITCH(o)) ? 1 : 0);
                     }),
                     &b);
    return s;
}

GtkWidget* Binder::dropdown(const std::vector<std::string>& labels, std::function<int()> get,
                            std::function<void(int)> set, bool searchable) {
    std::vector<const char*> strs;
    for (auto& l : labels) strs.push_back(l.c_str());
    strs.push_back(nullptr);
    GtkWidget* d = gtk_drop_down_new_from_strings(strs.data());
    if (searchable) {
        gtk_drop_down_set_enable_search(GTK_DROP_DOWN(d), TRUE);
        gtk_drop_down_set_expression(GTK_DROP_DOWN(d),
                                     gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, nullptr, "string"));
    }
    gtk_widget_set_halign(d, GTK_ALIGN_END);
    Binding& b = add(d, std::move(get), std::move(set));
    g_signal_connect(d, "notify::selected", cb(+[](GObject* o, GParamSpec*, gpointer data) {
                         guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
                         if (sel != GTK_INVALID_LIST_POSITION) changedFrom(static_cast<Binding*>(data), int(sel));
                     }),
                     &b);
    return d;
}

void Binder::setSensitive(GtkWidget* w, std::function<bool()> pred) {
    for (auto& b : bindings_)
        if (b.widget == w) {
            b.sensitive = std::move(pred);
            return;
        }
    add(w, [] { return 0; }, [](int) {}).sensitive = std::move(pred);
}

GtkWidget* Binder::redraw(GtkWidget* w) {
    g_object_set_data(G_OBJECT(w), "bw-redraw", GINT_TO_POINTER(1));
    add(w, [] { return 0; }, [](int) {});
    return w;
}

GtkWidget* Binder::segmented(const std::vector<std::string>& labels, std::function<int()> get,
                             std::function<void(int)> set) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(box, "bw-seg");
    g_object_set_data(G_OBJECT(box), "bw-seg", GINT_TO_POINTER(1));
    GtkToggleButton* first = nullptr;
    Binding& b = add(box, std::move(get), std::move(set));
    for (size_t i = 0; i < labels.size(); ++i) {
        GtkWidget* t = gtk_toggle_button_new_with_label(labels[i].c_str());
        gtk_widget_set_hexpand(t, TRUE);
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(t), first);
        else first = GTK_TOGGLE_BUTTON(t);
        g_object_set_data(G_OBJECT(t), "index", GINT_TO_POINTER(int(i)));
        g_signal_connect(t, "toggled", cb(+[](GtkToggleButton* tb, gpointer d) {
                             auto* bind = static_cast<Binding*>(d);
                             if (!gtk_toggle_button_get_active(tb) || bind->owner->updating()) return;
                             bind->set(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tb), "index")));
                             if (bind->owner->onChanged) bind->owner->onChanged();
                         }),
                         &b);
        gtk_box_append(GTK_BOX(box), t);
    }
    return box;
}

void Binder::refresh() {
    updating_ = true;
    for (auto& b : bindings_) {
        if (GTK_IS_SCALE(b.widget)) {
            gtk_range_set_value(GTK_RANGE(b.widget), b.get());
        } else if (GTK_IS_SWITCH(b.widget)) {
            gtk_switch_set_active(GTK_SWITCH(b.widget), b.get() != 0);
        } else if (g_object_get_data(G_OBJECT(b.widget), "bw-redraw")) {
            gtk_widget_queue_draw(b.widget);
        } else if (g_object_get_data(G_OBJECT(b.widget), "bw-seg")) {
            int v = b.get(), i = 0;
            for (GtkWidget* t = gtk_widget_get_first_child(b.widget); t; t = gtk_widget_get_next_sibling(t), ++i)
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(t), i == v);
        } else if (GTK_IS_DROP_DOWN(b.widget)) {
            int v = b.get();
            guint n = g_list_model_get_n_items(gtk_drop_down_get_model(GTK_DROP_DOWN(b.widget)));
            gtk_drop_down_set_selected(GTK_DROP_DOWN(b.widget), v >= 0 && guint(v) < n ? guint(v) : 0);
        }
        if (b.sensitive) gtk_widget_set_sensitive(b.widget, b.sensitive());
    }
    updating_ = false;
}

std::function<int()> jsonGet(std::function<Json&()> locate) {
    return [locate] {
        Json& j = locate();
        if (j.is_boolean()) return j.get<bool>() ? 1 : 0;
        if (j.is_number()) return j.get<int>();
        return 0;
    };
}

std::function<void(int)> jsonSet(std::function<Json&()> locate, bool asBool) {
    return [locate, asBool](int v) {
        if (asBool) locate() = v != 0;
        else locate() = v;
    };
}

// ---- curve editor -----------------------------------------------------------

namespace {

struct CurveState {
    std::function<Json&()> owner;
    std::function<void()> changed;
    int dragging = -1;
    double startX = 0, startY = 0;
};

constexpr double kPad = 14;

void colorOf(GtkWidget* w, GdkRGBA* c) { gtk_widget_get_color(w, c); }

int iget(Json& j, int fallback) { return j.is_number() ? j.get<int>() : fallback; }

// Model points of the curve in 0..100 space: start, 3 middle, end.
std::vector<std::pair<double, double>> modelPoints(Json& o) {
    Json& dz = o["deadzone"];
    Json& pts = o["curve"]["points"];
    std::vector<std::pair<double, double>> p;
    p.emplace_back(iget(dz["beginDeadzone"], 0), iget(dz["beginAntiDeadzone"], 0));
    for (size_t i = 0; i < 3; ++i) {
        Json& q = pts.is_array() && pts.size() > i ? pts[i] : pts;
        p.emplace_back(iget(q["x"], int(25 * (i + 1))), iget(q["y"], int(25 * (i + 1))));
    }
    p.emplace_back(iget(dz["endDeadzone"], 100), iget(dz["endAntiDeadzone"], 100));
    return p;
}

// Output for input x (0..100) for the non-custom curve types. The real firmware formulas are
// unknown; this is only a visual approximation.
double shape(int type, int curvature, double t) {
    double k = (curvature - 50) / 50.0;  // -1..1
    switch (type) {
    case 1: return k >= 0 ? std::pow(t, 1 + 2 * k) : 1 - std::pow(1 - t, 1 - 2 * k);
    case 2: {
        double s = t * t * (3 - 2 * t);
        return t + (s - t) * (k >= 0 ? 1 : -1) * std::abs(k == 0 ? 1 : k);
    }
    default: return t;
    }
}

void drawCurve(GtkDrawingArea* area, cairo_t* cr, int w, int h, gpointer data) {
    auto* st = static_cast<CurveState*>(data);
    Json& o = st->owner();
    GdkRGBA fg;
    colorOf(GTK_WIDGET(area), &fg);
    double x0 = kPad, y0 = h - kPad, sx = (w - 2 * kPad) / 100.0, sy = (h - 2 * kPad) / 100.0;
    auto X = [&](double v) { return x0 + v * sx; };
    auto Y = [&](double v) { return y0 - v * sy; };

    // grid
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.15);
    for (int i = 0; i <= 4; ++i) {
        cairo_move_to(cr, X(i * 25), Y(0));
        cairo_line_to(cr, X(i * 25), Y(100));
        cairo_move_to(cr, X(0), Y(i * 25));
        cairo_line_to(cr, X(100), Y(i * 25));
    }
    cairo_stroke(cr);

    auto p = modelPoints(o);
    int type = iget(o["curve"]["type"], 0);
    int curvature = iget(o["curve"]["curvature"], 100);
    // deadzone shading
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.08);
    cairo_rectangle(cr, X(0), Y(100), p[0].first * sx, 100 * sy);
    cairo_rectangle(cr, X(p[4].first), Y(100), (100 - p[4].first) * sx, 100 * sy);
    cairo_fill(cr);

    // curve
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.9);
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, X(0), Y(0));
    cairo_line_to(cr, X(p[0].first), Y(0));
    if (type == 3) {
        for (auto& q : p) cairo_line_to(cr, X(q.first), Y(q.second));
    } else {
        for (int i = 0; i <= 40; ++i) {
            double t = i / 40.0;
            double x = p[0].first + t * (p[4].first - p[0].first);
            double y = p[0].second + shape(type, curvature, t) * (p[4].second - p[0].second);
            cairo_line_to(cr, X(x), Y(y));
        }
    }
    cairo_line_to(cr, X(100), Y(p[4].second));
    cairo_stroke(cr);

    if (type == 3) {
        for (size_t i = 1; i <= 3; ++i) {
            cairo_arc(cr, X(p[i].first), Y(p[i].second), int(i) - 1 == st->dragging ? 7 : 5, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
}

}  // namespace

GtkWidget* curveEditor(std::function<Json&()> owner, std::function<void()> changed) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_add_css_class(area, "bw-curve");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 260);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 220);
    auto* st = new CurveState{std::move(owner), std::move(changed)};
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), drawCurve, st,
                                   [](gpointer d) { delete static_cast<CurveState*>(d); });

    GtkGesture* drag = gtk_gesture_drag_new();
    g_object_set_data(G_OBJECT(drag), "area", area);
    g_signal_connect(drag, "drag-begin", cb(+[](GtkGestureDrag* g, double x, double y, gpointer data) {
                         auto* st = static_cast<CurveState*>(data);
                         auto* a = GTK_WIDGET(g_object_get_data(G_OBJECT(g), "area"));
                         Json& o = st->owner();
                         st->dragging = -1;
                         if (iget(o["curve"]["type"], 0) != 3) return;
                         int w = gtk_widget_get_width(a), h = gtk_widget_get_height(a);
                         double sx = (w - 2 * kPad) / 100.0, sy = (h - 2 * kPad) / 100.0;
                         auto p = modelPoints(o);
                         for (int i = 1; i <= 3; ++i) {
                             double px = kPad + p[size_t(i)].first * sx, py = h - kPad - p[size_t(i)].second * sy;
                             if (std::hypot(px - x, py - y) < 14) st->dragging = i - 1;
                         }
                         st->startX = x;
                         st->startY = y;
                     }),
                     st);
    g_signal_connect(drag, "drag-update", cb(+[](GtkGestureDrag* g, double dx, double dy, gpointer data) {
                         auto* st = static_cast<CurveState*>(data);
                         if (st->dragging < 0) return;
                         auto* a = GTK_WIDGET(g_object_get_data(G_OBJECT(g), "area"));
                         int w = gtk_widget_get_width(a), h = gtk_widget_get_height(a);
                         double vx = (st->startX + dx - kPad) / ((w - 2 * kPad) / 100.0);
                         double vy = (h - kPad - (st->startY + dy)) / ((h - 2 * kPad) / 100.0);
                         Json& o = st->owner();
                         auto p = modelPoints(o);
                         size_t i = size_t(st->dragging);
                         double lo = p[i].first + 1, hi = p[i + 2].first - 1;
                         Json& pt = o["curve"]["points"][i];
                         pt["x"] = int(std::lround(std::clamp(vx, lo, std::max(lo, hi))));
                         pt["y"] = int(std::lround(std::clamp(vy, 0.0, 100.0)));
                         gtk_widget_queue_draw(a);
                     }),
                     st);
    g_signal_connect(drag, "drag-end", cb(+[](GtkGestureDrag* g, double, double, gpointer data) {
                         auto* st = static_cast<CurveState*>(data);
                         if (st->dragging < 0) return;
                         st->dragging = -1;
                         gtk_widget_queue_draw(GTK_WIDGET(g_object_get_data(G_OBJECT(g), "area")));
                         if (st->changed) st->changed();
                     }),
                     st);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));
    return area;
}

void curveEditorRedraw(GtkWidget* editor) { gtk_widget_queue_draw(editor); }

// ---- stick view -------------------------------------------------------------

GtkWidget* stickView(std::function<std::pair<int, int>()> value) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_add_css_class(area, "bw-stick");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 150);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 150);
    auto* fn = new std::function<std::pair<int, int>()>(std::move(value));
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(area),
        [](GtkDrawingArea* a, cairo_t* cr, int w, int h, gpointer data) {
            auto v = (*static_cast<std::function<std::pair<int, int>()>*>(data))();
            GdkRGBA fg;
            gtk_widget_get_color(GTK_WIDGET(a), &fg);
            double cx = w / 2.0, cy = h / 2.0, r = std::min(w, h) / 2.0 - 8;
            cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.25);
            cairo_set_line_width(cr, 1);
            cairo_rectangle(cr, cx - r, cy - r, 2 * r, 2 * r);
            cairo_move_to(cr, cx - r, cy);
            cairo_line_to(cr, cx + r, cy);
            cairo_move_to(cr, cx, cy - r);
            cairo_line_to(cr, cx, cy + r);
            cairo_stroke(cr);
            cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
            cairo_stroke(cr);
            double x = cx + r * v.first / 32768.0, y = cy - r * v.second / 32768.0;
            cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.95);
            cairo_rectangle(cr, x - 5, y - 5, 10, 10);
            cairo_fill(cr);
        },
        fn, [](gpointer d) { delete static_cast<std::function<std::pair<int, int>()>*>(d); });
    return area;
}

// ---- layout -----------------------------------------------------------------

GtkWidget* controllerArt(int width, int height) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_add_css_class(area, "bw-art");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), width);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), height);
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(area),
        [](GtkDrawingArea* a, cairo_t* cr, int w, int h, gpointer) {
            GdkRGBA fg;
            gtk_widget_get_color(GTK_WIDGET(a), &fg);
            double s = std::min(w / 300.0, h / 200.0), ox = (w - 300 * s) / 2, oy = (h - 200 * s) / 2;
            cairo_translate(cr, ox, oy);
            cairo_scale(cr, s, s);
            // body with two grips (generic gamepad silhouette)
            cairo_move_to(cr, 70, 40);
            cairo_line_to(cr, 230, 40);
            cairo_curve_to(cr, 275, 40, 290, 90, 295, 150);
            cairo_curve_to(cr, 298, 190, 255, 200, 240, 170);
            cairo_line_to(cr, 215, 130);
            cairo_line_to(cr, 85, 130);
            cairo_line_to(cr, 60, 170);
            cairo_curve_to(cr, 45, 200, 2, 190, 5, 150);
            cairo_curve_to(cr, 10, 90, 25, 40, 70, 40);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.10);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.85);
            cairo_set_line_width(cr, 2.5);
            cairo_stroke(cr);
            auto circle = [&](double x, double y, double r, double fill) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, x, y, r, 0, 2 * G_PI);
                cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fill);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.85);
                cairo_stroke(cr);
            };
            circle(80, 80, 18, 0.18);    // left stick
            circle(185, 112, 16, 0.18);  // right stick
            for (auto [x, y] : {std::pair{225.0, 62.0}, {245.0, 80.0}, {205.0, 80.0}, {225.0, 98.0}})
                circle(x, y, 7, 0.30);   // face buttons
            cairo_rectangle(cr, 109, 96, 8, 30);  // D-pad
            cairo_rectangle(cr, 98, 107, 30, 8);
            cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.60);
            cairo_fill(cr);
            circle(150, 62, 8, 0.25);    // home button
        },
        nullptr, nullptr);
    return area;
}

namespace {
struct Range {
    Binder* binder;
    int min, max;
    std::function<Json&()> lo, hi;
    int dragging = -1;  // 0 = lo, 1 = hi
    double startX = 0;
};
int jsonInt(Json& j, int fallback) { return j.is_number() ? j.get<int>() : fallback; }
constexpr double kRangePadL = 8, kRangePadR = 64;
double rangeX(const Range& r, int v, int w) {
    return kRangePadL + (w - kRangePadL - kRangePadR) * double(v - r.min) / double(r.max - r.min);
}
}  // namespace

GtkWidget* rangeSlider(Binder& binder, int min, int max, std::function<Json&()> lo, std::function<Json&()> hi) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_add_css_class(area, "bw-range");
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_size_request(area, 140, 26);
    auto* r = new Range{&binder, min, max, std::move(lo), std::move(hi)};
    g_object_set_data_full(G_OBJECT(area), "range", r, [](gpointer d) { delete static_cast<Range*>(d); });
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(area),
        [](GtkDrawingArea* a, cairo_t* cr, int w, int h, gpointer d) {
            auto* rg = static_cast<Range*>(d);
            GdkRGBA acc;
            gtk_widget_get_color(GTK_WIDGET(a), &acc);  // .bw-range colour = theme accent
            int lo = jsonInt(rg->lo(), rg->min), hi = jsonInt(rg->hi(), rg->max);
            double y = h / 2.0, x0 = rangeX(*rg, lo, w), x1 = rangeX(*rg, hi, w);
            cairo_set_source_rgba(cr, acc.red, acc.green, acc.blue, 0.25);
            cairo_rectangle(cr, kRangePadL, y - 1.5, w - kRangePadL - kRangePadR, 3);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, acc.red, acc.green, acc.blue, 1);
            cairo_rectangle(cr, x0, y - 2.5, x1 - x0, 5);
            cairo_fill(cr);
            for (double x : {x0, x1}) {
                cairo_rectangle(cr, x - 6, y - 8, 12, 16);
                cairo_fill(cr);
            }
            char buf[32];
            std::snprintf(buf, sizeof buf, "%d – %d", lo, hi);
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12);
            cairo_text_extents_t e;
            cairo_text_extents(cr, buf, &e);
            cairo_move_to(cr, w - e.width - e.x_bearing - 2, y - e.height / 2 - e.y_bearing);
            cairo_show_text(cr, buf);
        },
        r, nullptr);
    GtkGesture* drag = gtk_gesture_drag_new();
    auto apply = [](GtkWidget* w, Range* rg, double x) {
        int width = gtk_widget_get_width(w);
        double t = (x - kRangePadL) / std::max(1.0, width - kRangePadL - kRangePadR);
        int v = rg->min + int(std::lround(std::clamp(t, 0.0, 1.0) * (rg->max - rg->min)));
        int lo = jsonInt(rg->lo(), rg->min), hi = jsonInt(rg->hi(), rg->max);
        if (rg->dragging == 0) rg->lo() = std::min(v, hi - 1);
        else rg->hi() = std::max(v, lo + 1);
        gtk_widget_queue_draw(w);
        if (rg->binder->onChanged) rg->binder->onChanged();
    };
    g_signal_connect(drag, "drag-begin", cb(+[](GtkGestureDrag* g, double x, double, gpointer d) {
                         GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
                         auto* rg = static_cast<Range*>(g_object_get_data(G_OBJECT(w), "range"));
                         int width = gtk_widget_get_width(w);
                         double x0 = rangeX(*rg, jsonInt(rg->lo(), rg->min), width);
                         double x1 = rangeX(*rg, jsonInt(rg->hi(), rg->max), width);
                         rg->dragging = std::abs(x - x0) <= std::abs(x - x1) ? 0 : 1;
                         rg->startX = x;
                         (*static_cast<decltype(apply)*>(d))(w, rg, x);
                     }),
                     new decltype(apply)(apply));
    g_signal_connect(drag, "drag-update", cb(+[](GtkGestureDrag* g, double dx, double, gpointer d) {
                         GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
                         auto* rg = static_cast<Range*>(g_object_get_data(G_OBJECT(w), "range"));
                         (*static_cast<decltype(apply)*>(d))(w, rg, rg->startX + dx);
                     }),
                     new decltype(apply)(apply));
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));
    return binder.redraw(area);
}

GtkWidget* glyph(const char* name) {
    GtkWidget* g = gtk_drawing_area_new();
    gtk_widget_add_css_class(g, "bw-glyph");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(g), 16);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(g), 16);
    gtk_widget_set_valign(g, GTK_ALIGN_CENTER);
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(g),
        [](GtkDrawingArea* a, cairo_t* cr, int, int, gpointer d) {
            std::string n = static_cast<const char*>(d);
            GdkRGBA c;
            gtk_widget_get_color(GTK_WIDGET(a), &c);
            gdk_cairo_set_source_rgba(cr, &c);
            cairo_set_line_width(cr, 1.6);
            if (n == "vibration") {
                cairo_rectangle(cr, 5, 4, 6, 8);
                cairo_fill(cr);
                for (double x : {2.0, 14.0}) {
                    cairo_move_to(cr, x, 4);
                    cairo_line_to(cr, x, 12);
                }
                cairo_stroke(cr);
            } else if (n == "trigger") {
                cairo_move_to(cr, 4, 14);
                cairo_line_to(cr, 4, 6);
                cairo_curve_to(cr, 4, 2, 12, 2, 12, 6);
                cairo_line_to(cr, 12, 14);
                cairo_close_path(cr);
                cairo_fill(cr);
            } else if (n == "stick") {
                cairo_arc(cr, 8, 8, 6, 0, 2 * G_PI);
                cairo_stroke(cr);
                cairo_arc(cr, 8, 8, 2.5, 0, 2 * G_PI);
                cairo_fill(cr);
            } else if (n == "motion") {
                cairo_arc(cr, 8, 8, 6, 0, 2 * G_PI);
                cairo_stroke(cr);
                cairo_save(cr);
                cairo_translate(cr, 8, 8);
                cairo_scale(cr, 1, 0.4);
                cairo_arc(cr, 0, 0, 6, 0, 2 * G_PI);
                cairo_restore(cr);
                cairo_stroke(cr);
            } else if (n == "controller") {
                cairo_move_to(cr, 3, 5);
                cairo_line_to(cr, 13, 5);
                cairo_line_to(cr, 15, 13);
                cairo_line_to(cr, 11, 12);
                cairo_line_to(cr, 5, 12);
                cairo_line_to(cr, 1, 13);
                cairo_close_path(cr);
                cairo_fill(cr);
            } else if (n == "buttons") {
                for (auto [x, y] : {std::pair{8.0, 3.5}, {12.5, 8.0}, {8.0, 12.5}, {3.5, 8.0}}) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, x, y, 2.2, 0, 2 * G_PI);
                }
                cairo_fill(cr);
            } else if (n == "profile") {
                cairo_rectangle(cr, 3, 3, 10, 10);
                cairo_stroke(cr);
                cairo_rectangle(cr, 5.5, 6, 5, 1.6);
                cairo_rectangle(cr, 5.5, 9, 5, 1.6);
                cairo_fill(cr);
            } else if (n == "curve") {
                cairo_move_to(cr, 2, 14);
                cairo_curve_to(cr, 8, 14, 8, 2, 14, 2);
                cairo_stroke(cr);
            } else if (n == "layout") {
                cairo_rectangle(cr, 2, 2, 5, 5);
                cairo_rectangle(cr, 9, 2, 5, 5);
                cairo_rectangle(cr, 2, 9, 5, 5);
                cairo_rectangle(cr, 9, 9, 5, 5);
                cairo_stroke(cr);
            } else {  // "test" and fallback
                cairo_move_to(cr, 3, 8);
                cairo_line_to(cr, 7, 12);
                cairo_line_to(cr, 13, 4);
                cairo_stroke(cr);
            }
        },
        const_cast<char*>(name), nullptr);
    return g;
}

GtkWidget* section(const char* title, const char* glyphName) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "bw-section");
    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(head, glyphName ? "bw-section-head" : "bw-section-title");
    if (glyphName) gtk_box_append(GTK_BOX(head), glyph(glyphName));
    GtkWidget* t = gtk_label_new(title);
    gtk_widget_add_css_class(t, "bw-section-label");
    gtk_label_set_xalign(GTK_LABEL(t), 0);
    gtk_box_append(GTK_BOX(head), t);
    gtk_box_append(GTK_BOX(box), head);
    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(body, "bw-section-body");
    gtk_box_append(GTK_BOX(box), body);
    g_object_set_data(G_OBJECT(box), "body", body);
    return box;
}

GtkWidget* sectionBody(GtkWidget* s) { return GTK_WIDGET(g_object_get_data(G_OBJECT(s), "body")); }

GtkWidget* row(const char* label, GtkWidget* control, const char* hint) {
    GtkWidget* r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(r, "bw-row");
    GtkWidget* l = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_widget_set_size_request(l, 170, -1);
    gtk_box_append(GTK_BOX(r), l);
    if (control) {
        if (!gtk_widget_get_hexpand(control)) {
            GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_set_hexpand(spacer, TRUE);
            gtk_box_append(GTK_BOX(r), spacer);
        }
        gtk_box_append(GTK_BOX(r), control);
    }
    if (hint) gtk_widget_set_tooltip_text(r, hint);
    return r;
}

GtkWidget* sideSwitcher(const std::vector<std::string>& labels, std::function<void(int)> onSelect) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "bw-tabs");
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    auto* handler = new std::function<void(int)>(std::move(onSelect));
    g_object_set_data_full(G_OBJECT(box), "cb", handler, [](gpointer d) { delete static_cast<std::function<void(int)>*>(d); });
    GtkToggleButton* first = nullptr;
    for (size_t i = 0; i < labels.size(); ++i) {
        GtkWidget* b = gtk_toggle_button_new_with_label(labels[i].c_str());
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), first);
        else first = GTK_TOGGLE_BUTTON(b), gtk_toggle_button_set_active(first, TRUE);
        g_object_set_data(G_OBJECT(b), "index", GINT_TO_POINTER(int(i)));
        g_object_set_data(G_OBJECT(b), "cb", handler);
        g_signal_connect(b, "toggled", cb(+[](GtkToggleButton* t, gpointer) {
                             if (!gtk_toggle_button_get_active(t)) return;
                             auto* f = static_cast<std::function<void(int)>*>(g_object_get_data(G_OBJECT(t), "cb"));
                             (*f)(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(t), "index")));
                         }),
                         nullptr);
        gtk_box_append(GTK_BOX(box), b);
    }
    return box;
}

}  // namespace bw::ui
