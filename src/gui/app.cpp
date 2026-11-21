#include "app.h"

#include "codec.h"
#include "names.h"
#include "usb_transport.h"

#include <cstdio>

namespace bw::ui {

Product App::product() const {
    if (ctl && ctl->info().product != Product::Unknown) return ctl->info().product;
    return Product::G7Pro;
}

bool App::canWriteProfiles(std::string* why) const {
    if (!ctl) {
        if (why) *why = "not connected";
        return false;
    }
    return ctl->profileWritesAllowed(why);
}

std::vector<std::string> App::unwritableEdits() const {
    return unwritableChanges(onDevice[size_t(slot)], profiles[size_t(slot)]);
}

void App::status(const std::string& text, bool error) {
    if (!statusLabel) return;
    gtk_label_set_text(GTK_LABEL(statusLabel), text.c_str());
    if (error) gtk_widget_add_css_class(statusLabel, "bw-error");
    else gtk_widget_remove_css_class(statusLabel, "bw-error");
}

// ---- connection -------------------------------------------------------------

static gboolean pumpTick(gpointer data) {
    auto* app = static_cast<App*>(data);
    if (!app->ctl) return G_SOURCE_REMOVE;
    try {
        app->ctl->pump(2);
    } catch (const std::exception& e) {
        app->pumpSource = 0;
        app->disconnect(std::string("connection lost: ") + e.what());
        return G_SOURCE_REMOVE;
    }
    updateTest(*app);
    return G_SOURCE_CONTINUE;
}

static gboolean scanTick(gpointer data) {
    auto* app = static_cast<App*>(data);
    if (app->ctl) {
        app->scanSource = 0;
        return G_SOURCE_REMOVE;
    }
    app->rescan();
    app->updateChrome();
    return G_SOURCE_CONTINUE;
}

void App::rescan() {
    scanText = "No controller";
    scanText = "No controller";
    for (auto& c : scanUsb())
        if (c.mode == UsbCandidate::Mode::XInput) {
            scanText = "G7 Pro in XInput mode at " + c.path + " — Connect switches it to app mode";
        } else if (c.interface >= 0) {
            scanText = std::string(c.model ? std::string(c.model->name) + " " + c.model->edition : "GameSir device") +
                       " found at " + c.path + " (" + c.kernelDriverNote + ")";
            break;
        }
}

void App::connect() {
    if (ctl) return;
    std::string err;
    if (opt.dryRun) {
        ctl = openSimulatedController(opt.readOnly, opt.simPid, true);
    } else {
        ControllerOptions o;
        o.allowUnverifiedProfileWrites = opt.unverifiedLayout;
        ctl = openUsbController(opt.allowWrites && !opt.readOnly, {}, &err, o);
    }
    if (!ctl && !opt.dryRun) {
        // Like Nexus: a controller in XInput mode is knocked into GIP app mode, then we connect again.
        for (const auto& c : scanUsb()) {
            if (c.mode != UsbCandidate::Mode::XInput) continue;
            std::string kerr;
            if (!switchToAppMode(c, &kerr)) {
                status("could not switch to app mode: " + kerr, true);
                break;
            }
            status("switching the controller to app mode…");
            if (!appModeSource)
                appModeSource = g_timeout_add(2500, +[](gpointer d) -> gboolean {
                    auto* a = static_cast<App*>(d);
                    a->appModeSource = 0;
                    a->connect();
                    return G_SOURCE_REMOVE;
                }, this);
            updateChrome();
            return;
        }
    }
    if (!ctl) {
        status("could not connect: " + err, true);
        updateChrome();
        return;
    }
    ctl->setFrameHandler([this](const gip::Frame& f) {
        if (f.id == gip::kInput && parseInputReport(f.payload, input)) inputSeen = true;
        else if (f.id == gip::kGuideKey && !f.payload.empty()) input.guide = f.payload[0] != 0;
    });
    status("connected: " + ctl->info().transport);
    try {
        activeSlot = ctl->readProfileIndex();
        firmware = ctl->readVersion();
    } catch (const std::exception& e) {
        status(std::string("connected, but queries failed: ") + e.what(), true);
    }
    for (int s = 1; s <= kProfileSlots; ++s) {
        profiles[size_t(s)] = defaultProfile(product());
        onDevice[size_t(s)] = profiles[size_t(s)];
        fromDevice[size_t(s)] = false;
    }
    loadSlot(activeSlot >= 1 && activeSlot <= kProfileSlots ? activeSlot : 1);
    if (!pumpSource) pumpSource = g_timeout_add(16, pumpTick, this);
    if (scanSource) {
        g_source_remove(scanSource);
        scanSource = 0;
    }
    refreshAll();
}

ArtColors App::artColors() const {
    if (artScheme != "Automatic") return artColorsFor(artScheme);
    if (ctl && ctl->info().model) return artColorsFor(ctl->info().model->edition);
    return artColorsFor("");  // not connected: outline in theme colours
}

static std::string settingsPath() {
    return std::string(g_get_user_config_dir()) + "/bulwark/settings.ini";
}

void App::loadSettings() {
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, settingsPath().c_str(), G_KEY_FILE_NONE, nullptr)) {
        if (char* v = g_key_file_get_string(kf, "ui", "controller-colours", nullptr)) {
            artScheme = v;
            g_free(v);
        }
    }
    g_key_file_free(kf);
}

void App::saveSettings() const {
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, settingsPath().c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
    g_key_file_set_string(kf, "ui", "controller-colours", artScheme.c_str());
    std::string dir = std::string(g_get_user_config_dir()) + "/bulwark";
    g_mkdir_with_parents(dir.c_str(), 0700);
    g_key_file_save_to_file(kf, settingsPath().c_str(), nullptr);
    g_key_file_free(kf);
}

void App::endSession(bool background) {
    if (!ctl) return;
    bool need = ctl->appSessionStarted() && !ctl->info().simulated;
    std::string path = ctl->info().identity.busPath;
    ctl.reset();
    if (!need) return;
    if (restoreThread.joinable()) restoreThread.join();
    if (!background) {
        restoreAppMode(path);
        markSessionEnded();
        return;
    }
    status("restoring app mode on the controller…");
    restoreThread = std::thread([this, path] {
        std::string err;
        bool ok = restoreAppMode(path, 12000, &err);
        markSessionEnded();
        auto* msg = new std::pair<App*, std::string>(this, ok ? "controller back in app mode; the kernel driver has it"
                                                               : "could not restore app mode: " + err);
        g_idle_add(+[](gpointer d) -> gboolean {
            auto* m = static_cast<std::pair<App*, std::string>*>(d);
            m->first->status(m->second, m->second.rfind("could not", 0) == 0);
            delete m;
            return G_SOURCE_REMOVE;
        }, msg);
    });
}

void App::disconnect(const std::string& reason) {
    if (pumpSource) {
        g_source_remove(pumpSource);
        pumpSource = 0;
    }
    endSession(true);  // releases the interface, re-attaches xone/xpad, restores app mode
    inputSeen = false;
    firmware.clear();
    activeSlot = -1;
    status(reason.empty() ? "disconnected; the kernel driver has the controller again" : reason, !reason.empty());
    if (!opt.dryRun) rescan();
    if (!scanSource && !opt.dryRun) scanSource = g_timeout_add_seconds(2, scanTick, this);
    refreshAll();
}

// ---- profiles ---------------------------------------------------------------

void App::loadSlot(int s) {
    slot = s;
    if (!ctl || fromDevice[size_t(s)]) return;
    try {
        Json p = ctl->readProfile(s);
        normalizeProfile(p, product());
        profiles[size_t(s)] = p;
        onDevice[size_t(s)] = p;
        fromDevice[size_t(s)] = true;
        status("read " + slotName(s) + " from the controller");
    } catch (const std::exception& e) {
        status(std::string("reading ") + slotName(s) + " failed: " + e.what(), true);
    }
}

void App::selectSlot(int s) {
    if (s == slot && fromDevice[size_t(s)]) return;
    loadSlot(s);
    refreshAll();
}

void App::apply() {
    std::string why;
    if (!canWriteProfiles(&why)) {
        status("not written: " + why, true);
        return;
    }
    auto notes = normalizeProfile(profile(), product());
    try {
        ctl->writeProfile(slot, profile());
        onDevice[size_t(slot)] = profile();
        fromDevice[size_t(slot)] = true;
        status("wrote " + slotName(slot) + (notes.empty() ? "" : " (some values were adjusted)"));
    } catch (const std::exception& e) {
        status(std::string("write failed: ") + e.what(), true);
    }
    refreshAll();
}

void App::revert() {
    profiles[size_t(slot)] = onDevice[size_t(slot)];
    status("changes to " + slotName(slot) + " discarded");
    refreshAll();
}

void App::activateSlot() {
    if (!ctl) return;
    try {
        ctl->switchProfile(slot);
        activeSlot = ctl->readProfileIndex();
        status(slotName(slot) + " is now active on the controller");
    } catch (const std::exception& e) {
        status(std::string("could not switch profile: ") + e.what(), true);
    }
    updateChrome();
}

void App::resetSlot() {
    std::string name = profile()["BasicInfo"]["ProfileName"].is_string()
                           ? profile()["BasicInfo"]["ProfileName"].get<std::string>()
                           : "";
    profile() = defaultProfile(product());
    profile()["BasicInfo"]["ProfileName"] = name;
    status(slotName(slot) + " reset to defaults (not written yet)");
    refreshAll();
}

void App::exportSlot() {
    GdkClipboard* cb = gtk_widget_get_clipboard(window);
    gdk_clipboard_set_text(cb, exportProfile(profile()).c_str());
    status(slotName(slot) + " copied to the clipboard as JSON");
}

void App::importSlot() {
    GdkClipboard* cb = gtk_widget_get_clipboard(window);
    gdk_clipboard_read_text_async(
        cb, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* app = static_cast<App*>(data);
            GError* err = nullptr;
            char* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(src), res, &err);
            if (!text) {
                app->status(std::string("clipboard: ") + (err ? err->message : "empty"), true);
                if (err) g_error_free(err);
                return;
            }
            Json p;
            std::string e;
            if (!importProfile(text, p, e)) {
                app->status("import failed: " + e, true);
            } else {
                auto notes = normalizeProfile(p, app->product());
                app->profile() = p;
                app->status("imported into " + slotName(app->slot) +
                            (notes.empty() ? "" : " (" + std::to_string(notes.size()) + " values adjusted)") +
                            "; not written yet");
                app->refreshAll();
            }
            g_free(text);
        },
        this);
}

void App::testRumble(bool triggers) {
    if (!ctl) return;
    try {
        auto& m = profile()["BasicInfo"]["motor"];
        auto lvl = [&](const char* k) { return uint8_t(std::min(4, m[k].is_number() ? m[k].get<int>() : 3) * 255 / 4); };
        if (triggers) ctl->rumble(lvl("ltMotor"), lvl("rtMotor"), 0, 0, 600);
        else ctl->rumble(0, 0, lvl("leftMotor"), lvl("rightMotor"), 600);
    } catch (const std::exception& e) {
        status(std::string("vibration test: ") + e.what(), true);
    }
}

// ---- view updates -----------------------------------------------------------

void App::modelChanged() {
    normalizeProfile(profile(), product());  // keep ranges and exclusive options consistent
    for (auto* e : curveEditors) curveEditorRedraw(e);
    binder.refresh();  // dependent sensitivity, exclusive options
    updateChrome();
}

void App::refreshAll() {
    binder.refresh();
    for (auto* e : curveEditors) curveEditorRedraw(e);
    updateOverview(*this);
    updateChrome();
}

void App::updateChrome() {
    if (!window || !applyButton) return;  // still building the window
    std::string dev, badge;
    if (ctl) {
        const auto& i = ctl->info();
        char ids[32];
        std::snprintf(ids, sizeof ids, "%04x:%04x", i.identity.vid, i.identity.pid);
        dev = i.model ? std::string(i.model->name) + "  ·  " + i.model->edition + "  ·  " + linkName(i.model->link)
                      : std::string("Unknown GameSir device ") + ids;
        badge = i.simulated ? "SIMULATOR" : "";
        if (i.readOnly) badge += badge.empty() ? "READ-ONLY" : " · READ-ONLY";
        gtk_button_set_label(GTK_BUTTON(connectButton), "Disconnect");
    } else {
        if (opt.dryRun) {
            dev = "Simulator not connected";
        } else {
            dev = scanText;
        }
        gtk_button_set_label(GTK_BUTTON(connectButton), "Connect");
    }
    gtk_label_set_text(GTK_LABEL(deviceLabel), dev.c_str());
    gtk_label_set_text(GTK_LABEL(modeBadge), badge.c_str());
    gtk_widget_set_visible(modeBadge, !badge.empty());

    for (int s = 1; s <= kProfileSlots; ++s) {
        GtkWidget* b = slotButtons[size_t(s)];
        if (!b) continue;
        std::string label = s == kShiftSlot ? "SHIFT" : "PROFILE " + std::to_string(s);
        if (ctl && dirty(s)) label += " •";
        gtk_button_set_label(GTK_BUTTON(b), label.c_str());
        if (s == activeSlot) gtk_widget_add_css_class(b, "bw-active-slot");
        else gtk_widget_remove_css_class(b, "bw-active-slot");
        if (s == slot && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b)))
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE);
    }

    std::string why;
    bool canWrite = canWriteProfiles(&why);
    auto blocked = ctl ? unwritableEdits() : std::vector<std::string>();
    if (canWrite && !blocked.empty()) {
        canWrite = false;
        why = "These changes cannot be written yet:";
        for (const auto& b : blocked) why += "\n" + b;
    }
    gtk_widget_set_sensitive(applyButton, ctl && canWrite && dirty(slot));
    gtk_widget_set_tooltip_text(applyButton, canWrite ? "Write this profile to the controller" : why.c_str());
    gtk_widget_set_sensitive(revertButton, ctl && dirty(slot));
    gtk_widget_set_sensitive(activateButton, canSendCommands() && slot != activeSlot && slot != kShiftSlot);

    std::string bannerText;
    if (ctl && !ctl->info().simulated)
        bannerText = "Prototype: turbo, keyboard/mouse targets, report rate and lighting are not read or written yet. "
                     "Apply refuses changes it cannot write exactly (see its tooltip).";
    else if (ctl && ctl->info().simulated)
        bannerText = "Simulator: nothing is sent to a real controller.";
    gtk_label_set_text(GTK_LABEL(banner), bannerText.c_str());
    gtk_widget_set_visible(banner, !bannerText.empty());
}

// ---- window -----------------------------------------------------------------

static GtkWidget* button(const char* label, void (App::*action)(), App& app) {
    GtkWidget* b = gtk_button_new_with_label(label);
    struct Ctx {
        App* app;
        void (App::*action)();
    };
    auto* ctx = new Ctx{&app, action};
    g_object_set_data_full(G_OBJECT(b), "ctx", ctx, [](gpointer d) { delete static_cast<Ctx*>(d); });
    g_signal_connect(b, "clicked", cb(+[](GtkButton* btn, gpointer) {
                         auto* c = static_cast<Ctx*>(g_object_get_data(G_OBJECT(btn), "ctx"));
                         (c->app->*(c->action))();
                     }),
                     nullptr);
    return b;
}

GtkWidget* buildWindow(App& app) {
    GtkWidget* win = gtk_application_window_new(app.gtkApp);
    app.window = win;
    gtk_window_set_title(GTK_WINDOW(win), "Bulwark");
    gtk_window_set_default_size(GTK_WINDOW(win), 1060, 720);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_widget_add_css_class(win, "bw");

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), root);

    // ---- header: brand + device | profile tabs (centred) | badge, connect, close
    GtkWidget* header = gtk_center_box_new();
    gtk_widget_add_css_class(header, "bw-header");
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* brand = gtk_label_new("BULWARK");
    gtk_widget_add_css_class(brand, "bw-brand");
    gtk_label_set_xalign(GTK_LABEL(brand), 0);
    app.deviceLabel = gtk_label_new("");
    gtk_widget_add_css_class(app.deviceLabel, "bw-device");
    gtk_label_set_ellipsize(GTK_LABEL(app.deviceLabel), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(app.deviceLabel), 34);
    gtk_label_set_xalign(GTK_LABEL(app.deviceLabel), 0);
    gtk_box_append(GTK_BOX(left), brand);
    gtk_box_append(GTK_BOX(left), app.deviceLabel);
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(header), left);

    GtkWidget* tabs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(tabs, "bw-ptabs");
    GtkToggleButton* first = nullptr;
    for (int s2 = 1; s2 <= kProfileSlots; ++s2) {
        GtkWidget* b2 = gtk_toggle_button_new_with_label(s2 == kShiftSlot ? "SHIFT" : ("PROFILE " + std::to_string(s2)).c_str());
        gtk_widget_add_css_class(b2, "bw-slot");
        gtk_widget_set_tooltip_text(b2, slotName(s2).c_str());
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b2), first);
        else first = GTK_TOGGLE_BUTTON(b2);
        g_object_set_data(G_OBJECT(b2), "slot", GINT_TO_POINTER(s2));
        g_signal_connect(b2, "toggled", cb(+[](GtkToggleButton* t, gpointer d) {
                             if (gtk_toggle_button_get_active(t))
                                 static_cast<App*>(d)->selectSlot(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(t), "slot")));
                         }),
                         &app);
        app.slotButtons[size_t(s2)] = b2;
        gtk_box_append(GTK_BOX(tabs), b2);
    }
    gtk_toggle_button_set_active(first, TRUE);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(header), tabs);

    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app.modeBadge = gtk_label_new("");
    gtk_widget_add_css_class(app.modeBadge, "bw-badge");
    gtk_widget_set_valign(app.modeBadge, GTK_ALIGN_CENTER);
    app.connectButton = gtk_button_new_with_label("Connect");
    g_signal_connect(app.connectButton, "clicked", cb(+[](GtkButton*, gpointer d) {
                         auto* a = static_cast<App*>(d);
                         if (a->ctl) a->disconnect();
                         else a->connect();
                     }),
                     &app);
    GtkWidget* quit = gtk_button_new_with_label("✕");
    gtk_widget_set_tooltip_text(quit, "Quit");
    g_signal_connect_swapped(quit, "clicked", G_CALLBACK(gtk_window_close), win);
    for (GtkWidget* w : {app.modeBadge, app.connectButton, quit}) {
        gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(right), w);
    }
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(header), right);
    gtk_box_append(GTK_BOX(root), header);

    // ---- page tabs (centred), like the original's top navigation
    app.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app.stack), GTK_STACK_TRANSITION_TYPE_NONE);
    struct PageDef {
        const char* id;
        const char* title;
        GtkWidget* (*build)(App&);
    };
    const PageDef pages[] = {
        {"overview", "HOME", pageOverview},     {"buttons", "BUTTONS", pageButtons},
        {"sticks", "STICKS", pageSticks},       {"motion", "MOTION", pageMotion},
        {"triggers", "TRIGGERS", pageTriggers}, {"vibration", "VIBRATION", pageVibration},
        {"test", "BUTTON TEST", pageTest},
    };
    GtkWidget* nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(nav, "bw-nav");
    gtk_widget_set_halign(nav, GTK_ALIGN_CENTER);
    GtkWidget* navRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(navRow, "bw-navrow");
    gtk_widget_set_hexpand(nav, TRUE);
    gtk_box_append(GTK_BOX(navRow), nav);
    GtkToggleButton* firstTab = nullptr;
    GtkToggleButton* wanted = nullptr;
    for (const auto& pd : pages) {
        GtkWidget* scroller = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        GtkWidget* content = pd.build(app);
        gtk_widget_add_css_class(content, "bw-page");
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);
        gtk_stack_add_named(GTK_STACK(app.stack), scroller, pd.id);
        GtkWidget* t = gtk_toggle_button_new_with_label(pd.title);
        gtk_widget_add_css_class(t, "bw-tab");
        if (firstTab) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(t), firstTab);
        else firstTab = GTK_TOGGLE_BUTTON(t);
        g_object_set_data(G_OBJECT(t), "page", const_cast<char*>(pd.id));
        g_signal_connect(t, "toggled", cb(+[](GtkToggleButton* tb, gpointer d) {
                             if (!gtk_toggle_button_get_active(tb)) return;
                             gtk_stack_set_visible_child_name(GTK_STACK(static_cast<App*>(d)->stack),
                                                              static_cast<const char*>(g_object_get_data(G_OBJECT(tb), "page")));
                         }),
                         &app);
        if (app.opt.page == pd.id) wanted = GTK_TOGGLE_BUTTON(t);
        gtk_box_append(GTK_BOX(nav), t);
    }
    gtk_toggle_button_set_active(wanted ? wanted : firstTab, TRUE);
    gtk_box_append(GTK_BOX(root), navRow);

    app.banner = gtk_label_new("");
    gtk_widget_add_css_class(app.banner, "bw-banner");
    gtk_label_set_wrap(GTK_LABEL(app.banner), TRUE);
    gtk_label_set_xalign(GTK_LABEL(app.banner), 0);
    gtk_box_append(GTK_BOX(root), app.banner);

    gtk_widget_set_vexpand(app.stack, TRUE);
    gtk_box_append(GTK_BOX(root), app.stack);

    // ---- footer: status | profile actions | version
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(footer, "bw-footer");
    app.statusLabel = gtk_label_new("");
    gtk_widget_add_css_class(app.statusLabel, "bw-status");
    gtk_label_set_xalign(GTK_LABEL(app.statusLabel), 0);
    gtk_label_set_ellipsize(GTK_LABEL(app.statusLabel), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(app.statusLabel, TRUE);
    gtk_box_append(GTK_BOX(footer), app.statusLabel);
    app.activateButton = button("Make active", &App::activateSlot, app);
    gtk_widget_set_tooltip_text(app.activateButton, "Switch the controller to this profile");
    GtkWidget* exp = button("Export", &App::exportSlot, app);
    gtk_widget_set_tooltip_text(exp, "Copy this profile as JSON");
    GtkWidget* imp = button("Import", &App::importSlot, app);
    gtk_widget_set_tooltip_text(imp, "Paste a profile JSON from the clipboard");
    GtkWidget* reset = button("Default", &App::resetSlot, app);
    app.revertButton = button("Revert", &App::revert, app);
    app.applyButton = button("Apply", &App::apply, app);
    gtk_widget_add_css_class(app.applyButton, "bw-apply");
    for (GtkWidget* w : {app.activateButton, exp, imp, reset, app.revertButton, app.applyButton})
        gtk_box_append(GTK_BOX(footer), w);
    GtkWidget* version = gtk_label_new("v" BULWARK_VERSION);
    gtk_widget_add_css_class(version, "bw-version");
    gtk_box_append(GTK_BOX(footer), version);
    gtk_box_append(GTK_BOX(root), footer);

    app.binder.onChanged = [&app] { app.modelChanged(); };
    for (int s = 1; s <= kProfileSlots; ++s) {
        app.profiles[size_t(s)] = defaultProfile(Product::G7Pro);
        app.onDevice[size_t(s)] = app.profiles[size_t(s)];
    }
    return win;
}

}  // namespace bw::ui
