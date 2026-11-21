// Original vector illustration of a G7-style controller (drawn for this project from the controller's physical
// layout, not traced from vendor artwork), coloured per edition.
#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <vector>

namespace bw::ui {

struct ArtColors {
    GdkRGBA body;       // shell
    GdkRGBA faceTop;    // top of the face panel (the Black-Red edition's frosted red), fades into...
    GdkRGBA faceplate;  // ...the lower face panel
    GdkRGBA grips;      // textured grips
    GdkRGBA bumpers;    // shoulder buttons
    GdkRGBA ring;       // light rings around the sticks
    GdkRGBA buttons;    // face/centre buttons and stick caps
    GdkRGBA dpad;       // D-pad well
    GdkRGBA outline;
    bool themed = false;  // true: use the GTK theme's foreground instead (unknown edition)
};

// Colour schemes by edition name ("White", "Black", "Black-Red", ...). Unknown names give a themed outline drawing.
ArtColors artColorsFor(const std::string& edition);
// Names offered in the colour override dropdown (index 0 = "Automatic").
const std::vector<std::string>& artSchemeNames();

// Draws the controller into a w x h area (aspect 3:2 is ideal). `fg` is used for the themed scheme.
void drawController(cairo_t* cr, int w, int h, const ArtColors& c, const GdkRGBA& fg);

// A drawing area showing the controller; `scheme` is asked on every redraw.
GtkWidget* controllerArtWidget(int width, int height, std::function<ArtColors()> scheme);

}  // namespace bw::ui
