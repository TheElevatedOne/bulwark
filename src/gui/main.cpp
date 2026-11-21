// bulwark: GTK4 configuration tool for GameSir G7-family controllers.
#include "app.h"

#include "css.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bw;
using namespace bw::ui;

namespace {

void loadCss() {
    GtkCssProvider* p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p, kStylesheet);
    // Above USER priority: pywal-style user stylesheets (~/.config/gtk-4.0) style every `button`, which would turn
    // the flat tabs back into filled buttons. Our rules only target Bulwark's own classes and use theme colours.
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    g_object_unref(p);
}

// Builds the window against the simulator without showing it and exercises the main paths.
int selfTest(App& app) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "self-test: FAILED %s\n", what);
            ++failures;
        }
    };
    buildWindow(app);
    app.connect();
    check(app.ctl != nullptr, "connect to simulator");
    if (!app.ctl) return 1;
    check(app.fromDevice[1], "profile 1 read");
    app.profile()["BasicInfo"]["extend"]["swapLS2DPad"] = true;  // not mapped: must be flagged
    app.modelChanged();
    check(!app.unwritableEdits().empty(), "unmapped edit is flagged");
    app.revert();
    app.profile()["BasicInfo"]["motor"]["leftMotor"] = 1;
    app.profile()["BasicInfo"]["ProfileName"] = "Test";
    app.modelChanged();
    check(app.dirty(1) && app.unwritableEdits().empty(), "edit marks profile dirty");
    app.apply();
    check(!app.dirty(1), "apply clears dirty");
    check(app.ctl->readProfile(1)["BasicInfo"]["motor"]["leftMotor"] == 1, "apply reaches the device");
    app.selectSlot(5);
    check(app.slot == 5 && app.fromDevice[5], "shift layer read");
    app.selectSlot(2);
    app.activateSlot();
    check(app.activeSlot == 2, "activate profile 2");
    for (int i = 0; i < 20; ++i) app.ctl->pump(1);
    check(app.inputSeen, "input reports");
    app.disconnect();
    check(!app.ctl, "disconnect");
    gtk_window_destroy(GTK_WINDOW(app.window));
    std::printf("self-test: %s\n", failures ? "failed" : "ok");
    return failures ? 1 : 0;
}

gboolean takeScreenshot(gpointer data) {
    auto* app = static_cast<App*>(data);
    GtkWidget* w = app->window;
    // Live pages (button test) relayout on every input tick, which can leave no frame to snapshot: freeze the
    // updates and retry a few times before giving up.
    static int attempts = 0;
    if (app->pumpSource) {
        g_source_remove(app->pumpSource);
        app->pumpSource = 0;
    }
    int width = gtk_widget_get_width(w), height = gtk_widget_get_height(w);
    GdkPaintable* paintable = gtk_widget_paintable_new(w);
    GtkSnapshot* snap = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snap, width, height);
    GskRenderNode* node = gtk_snapshot_free_to_node(snap);
    int rc = 1;
    if (!node && ++attempts < 10) {
        g_object_unref(paintable);
        g_timeout_add(100, takeScreenshot, app);
        return G_SOURCE_REMOVE;
    }
    if (node) {
        GskRenderer* renderer = gtk_native_get_renderer(GTK_NATIVE(w));
        graphene_rect_t r = GRAPHENE_RECT_INIT(0, 0, float(width), float(height));
        GdkTexture* tex = gsk_renderer_render_texture(renderer, node, &r);
        rc = gdk_texture_save_to_png(tex, app->opt.screenshot.c_str()) ? 0 : 1;
        g_object_unref(tex);
        gsk_render_node_unref(node);
    }
    g_object_unref(paintable);
    std::fprintf(stderr, "screenshot %s: %s (%dx%d%s)\n", app->opt.screenshot.c_str(), rc ? "failed" : "written", width,
                 height, node ? "" : ", empty render node");
    g_application_quit(G_APPLICATION(app->gtkApp));
    return G_SOURCE_REMOVE;
}

void activate(GtkApplication* gapp, gpointer data) {
    auto* app = static_cast<App*>(data);
    if (app->window) {
        gtk_window_present(GTK_WINDOW(app->window));
        return;
    }
    app->gtkApp = gapp;
    loadCss();
    app->loadSettings();
    buildWindow(*app);
    if (app->opt.dryRun) app->connect();
    else app->disconnect();  // starts watching for controllers, does not claim one
    app->status(app->opt.dryRun ? "simulator: nothing is sent to a real controller"
                                : "press Connect to configure the controller (it is released again on Disconnect)");
    app->refreshAll();
    gtk_window_present(GTK_WINDOW(app->window));
    if (!app->opt.screenshot.empty()) g_timeout_add(900, takeScreenshot, app);
}

void usage() {
    std::puts(R"(Usage: bulwark [options]
  --dry-run          use a simulated G7 Pro instead of the USB controller
  --sim-pid HEX      simulate another model (e.g. 1010 for a G7 SE), implies --dry-run
  --read-only        never send commands that change the controller
  --allow-writes     allow commands that change a real controller (default: read-only)
  --unverified-layout  read/write profiles on real hardware with the unverified byte layout
  --page NAME        open on a page (overview, buttons, sticks, motion, triggers, vibration, test)
  --screenshot FILE  render the window to a PNG (implies --dry-run) and quit
  --self-test        exercise the GUI against the simulator without showing it; exit 77 without a display)");
}

}  // namespace

int main(int argc, char** argv) {
    // --render-art OUT.png [SCHEME] [WIDTH]: draw only the controller illustration (no display needed), for
    // checking it against reference photos.
    if (argc >= 3 && std::strcmp(argv[1], "--render-art") == 0) {
        std::string scheme = argc >= 4 ? argv[3] : "Black-Red";
        int w = argc >= 5 ? std::atoi(argv[4]) : 900, h = w * 2 / 3;
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* cr = cairo_create(surf);
        GdkRGBA fg{0.9f, 0.9f, 0.9f, 1.0f};
        drawController(cr, w, h, artColorsFor(scheme), fg);
        cairo_destroy(cr);
        cairo_status_t st = cairo_surface_write_to_png(surf, argv[2]);
        cairo_surface_destroy(surf);
        return st == CAIRO_STATUS_SUCCESS ? 0 : 1;
    }
    App app;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--dry-run") app.opt.dryRun = true;
        else if (a == "--sim-pid") app.opt.dryRun = true, app.opt.simPid = uint16_t(std::strtol(next().c_str(), nullptr, 16));
        else if (a == "--read-only") app.opt.readOnly = true;
        else if (a == "--allow-writes") app.opt.allowWrites = true;
        else if (a == "--unverified-layout") app.opt.unverifiedLayout = true;
        else if (a == "--page") app.opt.page = next();
        else if (a == "--screenshot") app.opt.screenshot = next(), app.opt.dryRun = true;
        else if (a == "--self-test") app.opt.selfTest = true, app.opt.dryRun = true;
        else if (a == "-h" || a == "--help") return usage(), 0;
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage();
            return 2;
        }
    }

    if (app.opt.selfTest) {
        if (!gtk_init_check()) {
            std::puts("self-test: no display, skipped");
            return 77;
        }
        app.gtkApp = gtk_application_new("io.github.theelevatedone.bulwark.selftest", G_APPLICATION_NON_UNIQUE);
        g_application_register(G_APPLICATION(app.gtkApp), nullptr, nullptr);
        int rc = selfTest(app);
        g_object_unref(app.gtkApp);
        return rc;
    }

    GtkApplication* gapp = gtk_application_new("io.github.theelevatedone.bulwark", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(gapp, "activate", G_CALLBACK(activate), &app);
    char* fakeArgv[] = {argv[0], nullptr};
    int rc = g_application_run(G_APPLICATION(gapp), 1, fakeArgv);
    app.endSession(false);  // releases the interface, re-attaches the kernel driver, restores app mode (blocking)
    if (app.restoreThread.joinable()) app.restoreThread.join();
    g_object_unref(gapp);
    return rc;
}
