// Original controller illustration for Bulwark. Geometry is our own, on a 300 x 200 unit canvas, following the
// G7-style physical layout (offset sticks, faceplate, bumpers with L5/R5 next to them); no vendor artwork is used.
#include "art.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace bw::ui {

namespace {

GdkRGBA hex(const char* s) {
    GdkRGBA c;
    gdk_rgba_parse(&c, s);
    return c;
}

// body, faceTop, faceplate, grips, bumpers, ring, buttons, dpad, outline
ArtColors scheme(const char* body, const char* top, const char* face, const char* grips, const char* bumpers,
                 const char* ring, const char* buttons, const char* dpad, const char* outline) {
    return {hex(body), hex(top), hex(face), hex(grips), hex(bumpers), hex(ring), hex(buttons), hex(dpad), hex(outline), false};
}

std::string norm(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (std::isalnum(static_cast<unsigned char>(ch))) out.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
    return out;
}

void set(cairo_t* cr, const GdkRGBA& c, double alpha = 1.0) { cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha * alpha); }

// Geometry on a 300 x 200 canvas, measured from the user's photo of their G7 Pro (scale 0.27, centred on the guide
// button, left/right averaged to cancel the camera's perspective). Face panel = 80 % of the width.
void shellPath(cairo_t* cr) {
    cairo_new_path(cr);
    cairo_move_to(cr, 150, 9);
    // left half, top centre -> bottom centre
    cairo_curve_to(cr, 110, 9, 72, 10, 52, 14);     // top edge (slight arch)
    cairo_curve_to(cr, 38, 17, 31, 28, 29, 44);     // top-left shoulder
    cairo_curve_to(cr, 25, 66, 20, 92, 17, 122);    // outer side, widening into the grip
    cairo_curve_to(cr, 14, 150, 16, 176, 28, 187);  // grip outer edge
    cairo_curve_to(cr, 40, 197, 62, 196, 70, 186);  // rounded grip tip
    cairo_curve_to(cr, 76, 176, 82, 158, 92, 145);  // grip inner edge up to the face corner
    cairo_curve_to(cr, 112, 144, 130, 144, 150, 144);  // bottom edge between the grips
    // right half, mirrored, bottom centre -> top centre
    cairo_curve_to(cr, 170, 144, 188, 144, 208, 145);
    cairo_curve_to(cr, 218, 158, 224, 176, 230, 186);
    cairo_curve_to(cr, 238, 196, 260, 197, 272, 187);
    cairo_curve_to(cr, 284, 176, 286, 150, 283, 122);
    cairo_curve_to(cr, 280, 92, 275, 66, 271, 44);
    cairo_curve_to(cr, 269, 28, 262, 17, 248, 14);
    cairo_curve_to(cr, 228, 10, 190, 9, 150, 9);
    cairo_close_path(cr);
}

void facePath(cairo_t* cr) {
    // front panel: arched top, sides slightly sloping inward, corners cut diagonally to a flat bottom edge
    cairo_new_path(cr);
    cairo_move_to(cr, 150, 9);
    cairo_curve_to(cr, 190, 9, 228, 10, 246, 14);
    cairo_curve_to(cr, 254, 16, 258, 24, 258, 34);
    cairo_line_to(cr, 252, 74);
    cairo_line_to(cr, 211, 143);
    cairo_line_to(cr, 89, 143);
    cairo_line_to(cr, 48, 74);
    cairo_line_to(cr, 42, 34);
    cairo_curve_to(cr, 42, 24, 46, 16, 54, 14);
    cairo_curve_to(cr, 72, 10, 110, 9, 150, 9);
    cairo_close_path(cr);
}

void bumpers(cairo_t* cr, const ArtColors& c) {
    for (bool right : {false, true}) {
        cairo_save(cr);
        if (right) {
            cairo_translate(cr, 300, 0);
            cairo_scale(cr, -1, 1);
        }
        cairo_new_path(cr);  // bumper peeking above the shell's top corner
        cairo_move_to(cr, 40, 18);
        cairo_curve_to(cr, 46, 7, 64, 3, 90, 4);
        cairo_line_to(cr, 92, 10);
        cairo_curve_to(cr, 70, 10, 54, 13, 46, 20);
        cairo_close_path(cr);
        set(cr, c.bumpers);
        cairo_fill_preserve(cr);
        set(cr, c.outline);
        cairo_stroke(cr);
        cairo_new_path(cr);  // L5 / R5 next to the cable port
        cairo_rectangle(cr, 100, 3, 12, 5);
        set(cr, c.bumpers);
        cairo_fill_preserve(cr);
        set(cr, c.outline);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
}

void disc(cairo_t* cr, double x, double y, double r, const GdkRGBA& fill, const GdkRGBA& line, double lineAlpha = 1) {
    cairo_new_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * G_PI);
    set(cr, fill);
    cairo_fill_preserve(cr);
    set(cr, line, lineAlpha);
    cairo_stroke(cr);
}

void pill(cairo_t* cr, double cx, double cy, double w, double h, const GdkRGBA& fill, const GdkRGBA& line) {
    double r = h / 2;
    cairo_new_path(cr);
    cairo_arc(cr, cx - w / 2 + r, cy, r, G_PI / 2, 3 * G_PI / 2);
    cairo_arc(cr, cx + w / 2 - r, cy, r, -G_PI / 2, G_PI / 2);
    cairo_close_path(cr);
    set(cr, fill);
    cairo_fill_preserve(cr);
    set(cr, line);
    cairo_stroke(cr);
}

void stick(cairo_t* cr, double x, double y, const ArtColors& c) {
    disc(cr, x, y, 22.5, c.body, c.outline);        // opening
    cairo_new_path(cr);                              // light ring
    cairo_arc(cr, x, y, 20.5, 0, 2 * G_PI);
    set(cr, c.ring, 0.9);
    cairo_set_line_width(cr, 2.4);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1.3);
    disc(cr, x, y, 15.5, c.buttons, c.outline);     // cap
    cairo_new_path(cr);
    cairo_arc(cr, x, y, 10, 0, 2 * G_PI);
    set(cr, c.outline, 0.35);
    cairo_stroke(cr);
}

void letter(cairo_t* cr, double x, double y, const char* t, const GdkRGBA& col) {
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);
    cairo_text_extents_t e;
    cairo_text_extents(cr, t, &e);
    cairo_move_to(cr, x - e.width / 2 - e.x_bearing, y - e.height / 2 - e.y_bearing);
    set(cr, col, 0.8);
    cairo_show_text(cr, t);
}

GdkRGBA mix(const GdkRGBA& a, const GdkRGBA& b, double t) {
    return {float(a.red + (b.red - a.red) * t), float(a.green + (b.green - a.green) * t),
            float(a.blue + (b.blue - a.blue) * t), 1.0f};
}

}  // namespace

const std::vector<std::string>& artSchemeNames() {
    static const std::vector<std::string> v = {"Automatic", "White", "Black", "Black-Red", "Black-Pink", "Blue",
                                               "Purple",    "Orange", "Pink", "Gray",      "Outline"};
    return v;
}

ArtColors artColorsFor(const std::string& edition) {
    std::string e = norm(edition);
    //            body       faceTop    faceplate  grips      bumpers    ring       buttons    dpad       outline
    if (e == "white")
        return scheme("#e8e7e3", "#f7f7f5", "#f7f7f5", "#d0cfcb", "#c9cacc", "#ffffff", "#e4e3df", "#c4c6c9", "#3a3a3c");
    if (e == "black")
        return scheme("#1d1d20", "#2a2b2f", "#202124", "#121214", "#85878b", "#d8d8d8", "#151517", "#9a9ca0", "#050506");
    if (e == "blackred" || e == "redblack")
        // semi-transparent red over a black shell: red at the top fading to black (the user's photos look lighter
        // only because of the flash)
        return scheme("#141416", "#8c1d25", "#121214", "#0e0e10", "#85878b", "#dedede", "#141416", "#9c9ea2", "#050506");
    if (e == "blackpink" || e == "pinkblack")
        return scheme("#1c1c1f", "#c25381", "#1f1f22", "#121214", "#85878b", "#dedede", "#141416", "#9c9ea2", "#050506");
    if (e == "blue")
        return scheme("#22406e", "#4a7bc4", "#2e5590", "#1a3257", "#9db3d6", "#e6eefb", "#1b2f50", "#aab6c8", "#0a1424");
    if (e == "purple")
        return scheme("#3d2c63", "#7a62b8", "#4d3a80", "#2e2149", "#b4a6d6", "#efeafa", "#2c2047", "#b2abc4", "#130c22");
    if (e == "orange")
        return scheme("#1d1d20", "#e2843a", "#25252a", "#121214", "#d4772d", "#f6dcc4", "#151517", "#9c9ea2", "#050506");
    if (e == "pink" || e == "gradientpink")
        return scheme("#efc6d4", "#f3a9c3", "#f8dbe5", "#e2aec0", "#e9b3c6", "#ffffff", "#f3d3de", "#d9c3cb", "#4a2433");
    if (e == "gray" || e == "grey" || e == "gradientgray")
        return scheme("#7d8086", "#a7aab0", "#8d9096", "#62656a", "#b5b7bb", "#f0f0f0", "#6b6e73", "#c6c8cb", "#1d1e20");
    ArtColors themed{};
    themed.themed = true;
    return themed;
}

void drawController(cairo_t* cr, int w, int h, const ArtColors& in, const GdkRGBA& fg) {
    ArtColors c = in;
    if (c.themed) {  // outline drawing in the theme's foreground colour
        GdkRGBA line = fg, faint = fg, mid = fg;
        line.alpha = 0.9;
        faint.alpha = 0.06;
        mid.alpha = 0.18;
        c = {faint, faint, faint, mid, mid, line, mid, mid, line, true};
    }
    double s = std::min(w / 300.0, h / 200.0);
    cairo_save(cr);
    cairo_translate(cr, (w - 300 * s) / 2, (h - 200 * s) / 2);
    cairo_scale(cr, s, s);
    cairo_set_line_width(cr, 1.3);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    bumpers(cr, c);

    // shell, then textured grips (everything of the shell below the face panel), then the face panel
    shellPath(cr);
    set(cr, c.grips);
    cairo_fill_preserve(cr);
    cairo_save(cr);
    cairo_clip(cr);
    set(cr, c.outline, 0.35);  // grip texture: fine dot grid
    for (double y = 30; y < 196; y += 3.2)
        for (double x = 20 + std::fmod(y, 6.4) / 2; x < 282; x += 3.2) cairo_rectangle(cr, x, y, 0.8, 0.8);
    cairo_fill(cr);
    cairo_restore(cr);
    facePath(cr);
    if (c.themed) {
        set(cr, c.faceplate);
    } else {
        cairo_pattern_t* g = cairo_pattern_create_linear(0, 9, 0, 143);
        GdkRGBA mid = mix(c.faceTop, c.faceplate, 0.55);
        cairo_pattern_add_color_stop_rgb(g, 0.0, c.faceTop.red, c.faceTop.green, c.faceTop.blue);
        cairo_pattern_add_color_stop_rgb(g, 0.3, mid.red, mid.green, mid.blue);
        cairo_pattern_add_color_stop_rgb(g, 0.55, c.faceplate.red, c.faceplate.green, c.faceplate.blue);
        cairo_pattern_add_color_stop_rgb(g, 1.0, c.faceplate.red, c.faceplate.green, c.faceplate.blue);
        cairo_set_source(cr, g);
        cairo_pattern_destroy(g);
    }
    cairo_fill_preserve(cr);
    set(cr, c.outline, 0.7);
    cairo_stroke(cr);
    shellPath(cr);
    set(cr, c.outline);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1.3);

    stick(cr, 75, 56.5, c);    // left stick, upper left
    stick(cr, 187, 104.5, c);  // right stick, lower right

    // D-pad: round well with a cross
    disc(cr, 110, 104.5, 23.5, c.body, c.outline);
    disc(cr, 110, 104.5, 21, c.dpad, c.outline, 0.6);
    cairo_new_path(cr);
    cairo_rectangle(cr, 105.5, 88, 9, 33);
    cairo_rectangle(cr, 93.5, 100, 33, 9);
    set(cr, c.buttons);
    cairo_fill(cr);

    // face buttons (letters are generic labels)
    struct Face {
        double x, y;
        const char* t;
    };
    for (const Face& f : {Face{223.5, 37.5, "Y"}, Face{203, 58, "X"}, Face{243.5, 58, "B"}, Face{223.5, 78, "A"}}) {
        disc(cr, f.x, f.y, 9.5, c.buttons, c.outline);
        letter(cr, f.x, f.y, f.t, c.ring);
    }

    // centre: plain guide button, View / Menu, Share pill, M pill
    disc(cr, 149, 41, 11.5, c.ring, c.outline);         // guide button (plain, no logo)
    disc(cr, 149, 41, 8, c.buttons, c.outline, 0.5);
    disc(cr, 115.5, 33, 6, c.buttons, c.outline);       // View
    disc(cr, 184, 32.5, 6, c.buttons, c.outline);       // Menu
    pill(cr, 149, 65.5, 16, 8, c.buttons, c.outline);   // Share
    pill(cr, 149, 124, 16, 8, c.buttons, c.outline);    // M
    cairo_restore(cr);
}

GtkWidget* controllerArtWidget(int width, int height, std::function<ArtColors()> scheme) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_add_css_class(area, "bw-art");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), width);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), height);
    auto* fn = new std::function<ArtColors()>(std::move(scheme));
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(area),
        [](GtkDrawingArea* a, cairo_t* cr, int w, int h, gpointer d) {
            GdkRGBA fg;
            gtk_widget_get_color(GTK_WIDGET(a), &fg);
            drawController(cr, w, h, (*static_cast<std::function<ArtColors()>*>(d))(), fg);
        },
        fn, [](gpointer d) { delete static_cast<std::function<ArtColors()>*>(d); });
    return area;
}

}  // namespace bw::ui
