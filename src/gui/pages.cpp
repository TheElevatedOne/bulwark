// The pages of the main window, mirroring Nexus' G7 Pro pages (REBUILD.md §7).
#include "app.h"

#include "codec.h"
#include "names.h"

#include <cstdio>

namespace bw::ui {

#define LOC(expr) [&app]() -> Json& { return expr; }

namespace {

std::vector<std::string> labels(const std::vector<const char*>& v) { return {v.begin(), v.end()}; }

GtkWidget* page() {
    GtkWidget* p = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(p, 18);
    gtk_widget_set_margin_end(p, 18);
    gtk_widget_set_margin_top(p, 14);
    gtk_widget_set_margin_bottom(p, 18);
    return p;
}

GtkWidget* add(GtkWidget* box, GtkWidget* child) {
    gtk_box_append(GTK_BOX(box), child);
    return child;
}

GtkWidget* scaleRow(App& app, const char* label, int min, int max, std::function<Json&()> loc,
                    const char* hint = nullptr) {
    return row(label, app.binder.scale(min, max, jsonGet(loc), jsonSet(loc)), hint);
}

GtkWidget* switchRow(App& app, const char* label, std::function<Json&()> loc, const char* hint = nullptr) {
    return row(label, app.binder.toggle(jsonGet(loc), jsonSet(loc, true)), hint);
}

GtkWidget* dropRow(App& app, const char* label, const std::vector<std::string>& opts, std::function<Json&()> loc,
                   const char* hint = nullptr) {
    return row(label, app.binder.dropdown(opts, jsonGet(loc), jsonSet(loc)), hint);
}

// A control for a setting whose bytes on the controller are not known yet: shown, but not editable, so the
// GUI never offers an edit that Apply would have to refuse (codec.h).
GtkWidget* notYet(GtkWidget* w) {
    gtk_widget_set_sensitive(w, FALSE);
    gtk_widget_set_tooltip_text(w, "Not supported yet: where the controller stores this setting is not known.");
    gtk_widget_add_css_class(w, "bw-notyet");
    return w;
}

// Map-target dropdown over a string id. `first` is the label of index 0 (empty id / "NC").
GtkWidget* targetDrop(App& app, std::function<Json&()> loc, const char* first, const char* emptyId,
                      bool controllerOnly) {
    std::vector<std::string> ids{emptyId}, names{first};
    for (const auto& t : mapTargets()) {
        if (controllerOnly && t.group != TargetGroup::Controller) continue;
        if (std::string(t.id) == emptyId) continue;
        if (deviceTargetCode(t.id) < 0) continue;  // code not known yet: could not be written
        ids.push_back(t.id);
        const char* prefix = t.group == TargetGroup::Keyboard ? "Keyboard: "
                             : t.group == TargetGroup::Mouse  ? "Mouse: "
                                                               : "";
        names.push_back(std::string(prefix) + t.label);
    }
    auto get = [loc, ids] {
        Json& j = loc();
        std::string id = j.is_string() ? j.get<std::string>() : "";
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == id) return int(i);
        return 0;
    };
    auto set = [loc, ids](int v) { loc() = ids[size_t(std::max(0, v))]; };
    return app.binder.dropdown(names, get, set, true);
}

void deadzoneSection(App& app, GtkWidget* pg, std::function<Json&()> owner) {
    GtkWidget* s = add(pg, section("DEADZONE", "stick"));
    GtkWidget* b = sectionBody(s);
    auto dz = [owner](const char* k) { return [owner, k]() -> Json& { return owner()["deadzone"][k]; }; };
    add(b, row("Dead zone", rangeSlider(app.binder, 0, 100, dz("beginDeadzone"), dz("endDeadzone")),
               "No output below the start; full output above the end"));
    add(b, row("Anti dead zone", rangeSlider(app.binder, 0, 100, dz("beginAntiDeadzone"), dz("endAntiDeadzone")),
               "Output range once the dead zone is passed (for games with their own dead zone)"));
}

void curveSection(App& app, GtkWidget* pg, std::function<Json&()> owner) {
    GtkWidget* s = add(pg, section("RESPONSE CURVE", "curve"));
    GtkWidget* b = sectionBody(s);
    auto type = [owner]() -> Json& { return owner()["curve"]["type"]; };
    add(b, row("Curve", app.binder.dropdown(labels(curveTypeNames()), jsonGet(type), jsonSet(type)),
               "Custom: drag the three points"));
    GtkWidget* curv = app.binder.scale(0, 100, jsonGet([owner]() -> Json& { return owner()["curve"]["curvature"]; }),
                                       jsonSet([owner]() -> Json& { return owner()["curve"]["curvature"]; }));
    app.binder.setSensitive(curv, [type] {
        int t = type().is_number() ? type().get<int>() : 0;
        return t == 1 || t == 2;
    });
    add(b, row("Curvature", curv, "Concave / convex and S-curve strength"));
    GtkWidget* editor = curveEditor(owner, [&app] { app.modelChanged(); });
    gtk_widget_set_halign(editor, GTK_ALIGN_START);
    app.curveEditors.push_back(editor);
    add(b, editor);
}

GtkWidget* note(const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "bw-note");
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

}  // namespace

// ---- overview -----------------------------------------------------------------

static GtkWidget* g_nameEntry = nullptr;

// Dashboard rows: narrower label column and slider minimum, so three columns of cards fit a normal window.
static GtkWidget* compact(GtkWidget* r) {
    for (GtkWidget* c = gtk_widget_get_first_child(r); c; c = gtk_widget_get_next_sibling(c)) {
        if (GTK_IS_LABEL(c)) gtk_widget_set_size_request(c, 96, -1);
        if (GTK_IS_SCALE(c)) gtk_widget_set_size_request(c, 110, -1);
    }
    return r;
}

// Mapping dropdown for one button's first target (empty mapKeys = the button's own function).
static GtkWidget* mapDrop(App& app, const std::string& key) {
    std::vector<std::string> ids{""}, names{"Default"};
    for (const auto& t : mapTargets()) {
        if (deviceTargetCode(t.id) < 0) continue;
        ids.push_back(t.id);
        const char* prefix = t.group == TargetGroup::Keyboard ? "Key: " : t.group == TargetGroup::Mouse ? "Mouse: " : "";
        names.push_back(std::string(prefix) + t.label);
    }
    auto maps = [&app, key]() -> Json& { return app.profile()["KeyProfiles"][key]["key"]["mapKeys"]; };
    auto get = [maps, ids] {
        Json& m = maps();
        if (!m.is_array() || m.empty() || !m[0].is_string()) return 0;
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == m[0].get<std::string>()) return int(i);
        return 0;
    };
    auto set = [maps, ids](int v) {
        maps() = v <= 0 ? Json::array() : Json::array({ids[size_t(v)]});
    };
    return app.binder.dropdown(names, get, set, true);
}

// Home: a dashboard of cards in three columns (left: vibration, triggers; centre: controller, sticks, profile;
// right: motion, controller info, extra buttons), like the original app's main screen.
GtkWidget* pageOverview(App& app) {
    GtkWidget* pg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(pg, "bw-dashboard");
    GtkWidget* col[3];
    for (auto& c : col) {
        c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
        gtk_widget_set_hexpand(c, TRUE);
        gtk_box_append(GTK_BOX(pg), c);
    }
    gtk_box_set_homogeneous(GTK_BOX(pg), TRUE);
    auto mo = [&app](const char* k) { return [&app, k]() -> Json& { return app.profile()["BasicInfo"]["motor"][k]; }; };
    auto dz = [&app](const char* side, const char* k) {
        return [&app, side, k]() -> Json& {
            return std::string(side) == "L2" || std::string(side) == "R2"
                       ? app.profile()["KeyProfiles"][side]["deadzone"][k]
                       : app.profile()["Sticks"][side]["deadzone"][k];
        };
    };

    // ---- left column
    GtkWidget* vib = add(col[0], section("VIBRATION", "vibration"));
    add(sectionBody(vib), compact(scaleRow(app, "Left grip", 0, 4, mo("leftMotor"))));
    add(sectionBody(vib), compact(scaleRow(app, "Right grip", 0, 4, mo("rightMotor"))));
    add(sectionBody(vib), compact(scaleRow(app, "Left trigger", 0, 4, mo("ltMotor"))));
    add(sectionBody(vib), compact(scaleRow(app, "Right trigger", 0, 4, mo("rtMotor"))));
    GtkWidget* trg = add(col[0], section("TRIGGERS", "trigger"));
    for (const char* t : {"L2", "R2"}) {
        GtkWidget* l = gtk_label_new(std::string(t) == "L2" ? "LT" : "RT");
        gtk_widget_add_css_class(l, "bw-subtitle");
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        add(sectionBody(trg), l);
        add(sectionBody(trg), compact(row("Dead zone", rangeSlider(app.binder, 0, 100, dz(t, "beginDeadzone"),
                                                                   dz(t, "endDeadzone")))));
        auto h = [&app, t](const char* k) {
            return [&app, t, k]() -> Json& { return app.profile()["KeyProfiles"][t]["hairTrigger"][k]; };
        };
        auto get = [h] {
            bool on = h("enabled")().is_boolean() && h("enabled")().get<bool>();
            return on ? (h("mode")().is_number() ? h("mode")().get<int>() : 1) : 0;
        };
        auto set = [h](int v) {
            h("enabled")() = v != 0;
            if (v) h("mode")() = v;
        };
        add(sectionBody(trg), compact(row("Hair trigger", app.binder.segmented(labels(hairTriggerNames()), get, set))));
    }

    // ---- centre column
    GtkWidget* art = app.binder.redraw(controllerArtWidget(330, 220, [&app] { return app.artColors(); }));
    gtk_widget_set_halign(art, GTK_ALIGN_CENTER);
    add(col[1], art);
    GtkWidget* model = gtk_label_new("G7 PRO");
    gtk_widget_add_css_class(model, "bw-model");
    add(col[1], model);
    {
        const auto& names = artSchemeNames();
        auto get = [&app, names] {
            for (size_t k = 0; k < names.size(); ++k)
                if (names[k] == app.artScheme) return int(k);
            return 0;
        };
        auto set = [&app, names](int v) {
            app.artScheme = names[size_t(std::clamp(v, 0, int(names.size()) - 1))];
            app.saveSettings();
            app.binder.refresh();
        };
        GtkWidget* colours = app.binder.dropdown(names, get, set);
        gtk_widget_set_tooltip_text(colours, "Colours of the drawing. Automatic follows the connected controller's edition.");
        gtk_widget_set_halign(colours, GTK_ALIGN_CENTER);
        add(col[1], colours);
    }
    GtkWidget* sticks = add(col[1], section("STICKS", "stick"));
    GtkWidget* sgrid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    for (const char* side : {"Left", "Right"}) {
        GtkWidget* sc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget* l = gtk_label_new(std::string(side) == "Left" ? "LEFT STICK" : "RIGHT STICK");
        gtk_widget_add_css_class(l, "bw-subtitle");
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_box_append(GTK_BOX(sc), l);
        gtk_box_append(GTK_BOX(sc), compact(row("Dead zone", rangeSlider(app.binder, 0, 100, dz(side, "beginDeadzone"),
                                                                         dz(side, "endDeadzone")))));
        gtk_box_append(GTK_BOX(sc), compact(row("Anti dead zone",
                                                rangeSlider(app.binder, 0, 100, dz(side, "beginAntiDeadzone"),
                                                            dz(side, "endAntiDeadzone")))));
        gtk_box_append(GTK_BOX(sgrid), sc);
    }
    add(sectionBody(sticks), sgrid);
    GtkWidget* prof = add(col[1], section("PROFILE", "profile"));
    g_nameEntry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(g_nameEntry), int(kNameBytes));
    gtk_widget_set_hexpand(g_nameEntry, TRUE);
    g_signal_connect(g_nameEntry, "changed", cb(+[](GtkEditable* e, gpointer d) {
                         auto* a = static_cast<App*>(d);
                         if (a->binder.updating()) return;
                         std::string t = gtk_editable_get_text(e);
                         if (a->profile()["BasicInfo"]["ProfileName"] == t) return;
                         a->profile()["BasicInfo"]["ProfileName"] = t;
                         a->modelChanged();
                     }),
                     &app);
    add(sectionBody(prof), compact(row("Name", g_nameEntry, "At most 16 characters (ASCII)")));
    add(sectionBody(prof), compact(row("Report rate",
                                      app.binder.segmented(labels(reportRateNames()),
                                                           jsonGet(LOC(app.profile()["BasicInfo"]["extend"]["reportRate"])),
                                                           jsonSet(LOC(app.profile()["BasicInfo"]["extend"]["reportRate"]))))));

    // ---- right column
    auto m = [&app](const char* k) { return [&app, k]() -> Json& { return app.profile()["Motion"]["Aim"][k]; }; };
    GtkWidget* mot = add(col[2], section("MOTION", "motion"));
    add(sectionBody(mot), compact(dropRow(app, "Turn on", labels(motionActivateNames()), m("activateBehavior"))));
    add(sectionBody(mot), compact(row("Activation", targetDrop(app, m("triggerKey"), "None", "NC", true))));
    add(sectionBody(mot), compact(dropRow(app, "Output as", labels(motionOutputNames()),
                                          LOC(app.profile()["Motion"]["Aim"]["advanceMap"]["outputType"]))));
    GtkWidget* info = add(col[2], section("CONTROLLER", "controller"));
    app.infoGrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(app.infoGrid), 18);
    gtk_grid_set_row_spacing(GTK_GRID(app.infoGrid), 4);
    add(sectionBody(info), app.infoGrid);
    GtkWidget* extra = add(col[2], section("EXTRA BUTTONS", "buttons"));
    for (const auto& [key, label] : {std::pair{"FL1", "L4 (back)"}, {"FR1", "R4 (back)"}, {"FL2", "L5 (shoulder)"},
                                     {"FR2", "R5 (shoulder)"}})
        add(sectionBody(extra), compact(row(label, mapDrop(app, key))));
    return pg;
}

void updateOverview(App& app) {
    if (!app.infoGrid) return;
    while (GtkWidget* c = gtk_widget_get_first_child(app.infoGrid)) gtk_grid_remove(GTK_GRID(app.infoGrid), c);
    std::vector<std::pair<std::string, std::string>> rows;
    if (!app.ctl) {
        rows.push_back({"Status", "not connected — press Connect"});
        rows.push_back({"Note", "Connecting detaches the kernel driver (xone/xpad); the controller is unavailable to "
                                "games until you disconnect."});
    } else {
        const auto& i = app.ctl->info();
        char buf[64];
        rows.push_back({"Model", i.model ? i.model->name : "unknown"});
        if (i.model) rows.push_back({"Edition", i.model->edition});
        rows.push_back({"Connection", i.model ? linkName(i.model->link) : "?"});
        std::snprintf(buf, sizeof buf, "%04x:%04x  (bcdDevice %04x)", i.identity.vid, i.identity.pid, i.identity.bcdDevice);
        rows.push_back({"USB IDs", buf});
        if (!app.firmware.empty()) rows.push_back({"Firmware", app.firmware});
        rows.push_back({"Mode", "GIP app mode"});
        rows.push_back({"Active profile", app.activeSlot > 0 ? slotName(app.activeSlot) : "?"});
    }
    int r = 0;
    for (auto& [k, v] : rows) {
        GtkWidget* kl = gtk_label_new(k.c_str());
        gtk_widget_add_css_class(kl, "bw-key");
        gtk_label_set_xalign(GTK_LABEL(kl), 0);
        GtkWidget* vl = gtk_label_new(v.c_str());
        gtk_label_set_xalign(GTK_LABEL(vl), 0);
        gtk_label_set_wrap(GTK_LABEL(vl), TRUE);
        gtk_label_set_selectable(GTK_LABEL(vl), TRUE);
        gtk_grid_attach(GTK_GRID(app.infoGrid), kl, 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(app.infoGrid), vl, 1, r, 1, 1);
        ++r;
    }
    if (g_nameEntry) {
        Json& n = app.profile()["BasicInfo"]["ProfileName"];
        std::string name = n.is_string() ? n.get<std::string>() : "";
        if (name != gtk_editable_get_text(GTK_EDITABLE(g_nameEntry))) {
            g_signal_handlers_block_matched(g_nameEntry, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, &app);
            gtk_editable_set_text(GTK_EDITABLE(g_nameEntry), name.c_str());
            g_signal_handlers_unblock_matched(g_nameEntry, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, &app);
        }
    }
}

// ---- buttons --------------------------------------------------------------------

// Buttons: the controller drawing in the middle and a "maps to" choice for every input around it, in the order the
// inputs sit on the controller (left side on the left, right side on the right), like the original app.
GtkWidget* pageButtons(App& app) {
    GtkWidget* pg = page();
    auto label = [](const std::string& id) {
        for (const auto& k : profileKeys())
            if (id == k.id) return std::string(k.label);
        return id;
    };
    GtkWidget* map = add(pg, section("BUTTON MAPPING", "buttons"));
    GtkWidget* body = sectionBody(map);
    GtkWidget* cols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    add(body, cols);
    auto column = [&](std::initializer_list<const char*> ids) {
        GtkWidget* c = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(c, TRUE);
        gtk_widget_set_valign(c, GTK_ALIGN_CENTER);
        for (const char* id : ids) gtk_box_append(GTK_BOX(c), compact(row(label(id).c_str(), mapDrop(app, id))));
        return c;
    };
    gtk_box_append(GTK_BOX(cols), column({"L2", "L1", "FL2", "L3", "Up", "Under", "Left", "Right", "FL1", "Sel", "Camera"}));
    GtkWidget* mid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_valign(mid, GTK_ALIGN_CENTER);
    GtkWidget* art = app.binder.redraw(controllerArtWidget(330, 220, [&app] { return app.artColors(); }));
    gtk_box_append(GTK_BOX(mid), art);
    gtk_box_append(GTK_BOX(mid), note("Default = the button keeps its own function. One target per button (keys, mouse "
                                      "buttons or another controller input); the G7 Pro has no turbo."));
    gtk_box_append(GTK_BOX(cols), mid);
    gtk_box_append(GTK_BOX(cols), column({"R2", "R1", "FR2", "TRI", "CIR", "CRO", "SQU", "R3", "FR1", "Sta", "PS"}));

    GtkWidget* lay = add(pg, section("LAYOUT", "layout"));
    GtkWidget* lb = sectionBody(lay);
    add(lb, notYet(switchRow(app, "Swap left stick and D-pad", LOC(app.profile()["BasicInfo"]["extend"]["swapLS2DPad"]))));
    add(lb, notYet(switchRow(app, "D-pad diagonal lock",
                             LOC(app.profile()["BasicInfo"]["extend"]["dpadDiagonalLockEnable"]))));
    add(lb, notYet(switchRow(app, "Swap A/B and X/Y", LOC(app.profile()["BasicInfo"]["extend"]["xinputAbxyChange"]),
                             "Nintendo-style face button layout")));
    return pg;
}

// ---- sticks ---------------------------------------------------------------------

GtkWidget* pageSticks(App& app) {
    GtkWidget* pg = page();
    add(pg, sideSwitcher({"LEFT STICK", "RIGHT STICK"}, [&app](int i) {
            app.stickSide = i == 0 ? "Left" : "Right";
            app.refreshAll();
        }));
    auto owner = LOC(app.profile()["Sticks"][app.stickSide]);
    deadzoneSection(app, pg, owner);
    curveSection(app, pg, owner);

    GtkWidget* s = add(pg, section("OUTPUT", "stick"));
    GtkWidget* b = sectionBody(s);
    add(b, notYet(switchRow(app, "Raw trajectory", LOC(app.profile()["Sticks"][app.stickSide]["rawEnabled"]),
                            "Off: circular outer edge. On: raw square-ish edge, easier diagonals.")));
    add(b, notYet(dropRow(app, "Joystick resolution", labels(stickResolutionNames()),
                          LOC(app.profile()["BasicInfo"]["extend"]["stickResolution"]),
                          "G7 Pro only. Applies to both sticks.")));
    auto am = [&app](const char* k) {
        return [&app, k]() -> Json& { return app.profile()["Sticks"][app.stickSide]["advanceMap"][k]; };
    };
    add(b, dropRow(app, "Output as", labels(stickOutputNames()), am("outputType"),
                   "Values beyond left/right stick are a guess until verified"));
    add(b, scaleRow(app, "X/Y sensitivity ratio", 0, 100, am("axisRatio"), "50 = equal, 0 = only X, 100 = only Y"));
    add(b, notYet(scaleRow(app, "Direction overlap", 0, 100, am("cross"),
                           "When driving D-pad/WASD: 0 = four directions, 50 = eight, 100 = diagonals only")));
    add(b, scaleRow(app, "Mouse speed", 0, 100, am("mouseDpi")));
    add(b, notYet(row("Invert X", app.binder.toggle(jsonGet(am("reverseHorizontal")), jsonSet(am("reverseHorizontal"), true)))));
    add(b, notYet(row("Invert Y", app.binder.toggle(jsonGet(am("reverseVertical")), jsonSet(am("reverseVertical"), true)))));
    return pg;
}

// ---- motion ---------------------------------------------------------------------

GtkWidget* pageMotion(App& app) {
    GtkWidget* pg = page();
    add(pg, note("Gyro aiming (G7 Pro). Aim and Tilt are two set-ups; only one can be switched on at a time."));
    add(pg, sideSwitcher({"AIM", "TILT"}, [&app](int i) {
            app.motionSide = i == 0 ? "Aim" : "Tilt";
            app.refreshAll();
        }));
    auto m = [&app](const char* k) { return [&app, k]() -> Json& { return app.profile()["Motion"][app.motionSide][k]; }; };
    auto am = [&app](const char* k) {
        return [&app, k]() -> Json& { return app.profile()["Motion"][app.motionSide]["advanceMap"][k]; };
    };
    GtkWidget* s = add(pg, section("ACTIVATION", "motion"));
    GtkWidget* b = sectionBody(s);
    add(b, notYet(dropRow(app, "Mode", labels(motionModeNames()), m("mappingType"))));
    add(b, dropRow(app, "Turn on", labels(motionActivateNames()), m("activateBehavior")));
    add(b, row("Activation button", targetDrop(app, m("triggerKey"), "None", "NC", true)));
    add(b, notYet(dropRow(app, "Horizontal from", labels(motionOrientationNames()), m("orientation"))));

    GtkWidget* o = add(pg, section("OUTPUT", "stick"));
    GtkWidget* ob = sectionBody(o);
    add(ob, dropRow(app, "Output as", labels(motionOutputNames()), am("outputType")));
    add(ob, scaleRow(app, "Mouse speed (DPI)", 0, 100, am("mouseDpi")));
    add(ob, scaleRow(app, "X/Y sensitivity ratio", 0, 100, am("axisRatio")));
    add(ob, notYet(row("Invert X (roll)", app.binder.toggle(jsonGet(am("reverseHorizontal")), jsonSet(am("reverseHorizontal"), true)))));
    add(ob, notYet(row("Invert yaw", app.binder.toggle(jsonGet(am("reverseHorizontal_Yaw")), jsonSet(am("reverseHorizontal_Yaw"), true)))));
    add(ob, notYet(row("Invert Y", app.binder.toggle(jsonGet(am("reverseVertical")), jsonSet(am("reverseVertical"), true)))));

    auto owner = LOC(app.profile()["Motion"][app.motionSide]);
    deadzoneSection(app, pg, owner);
    curveSection(app, pg, owner);
    app.binder.setSensitive(pg, [&app] { return app.product() == Product::G7Pro; });
    return pg;
}

// ---- triggers -------------------------------------------------------------------

GtkWidget* pageTriggers(App& app) {
    GtkWidget* pg = page();
    add(pg, sideSwitcher({"LEFT TRIGGER", "RIGHT TRIGGER"}, [&app](int i) {
            app.triggerSide = i == 0 ? "L2" : "R2";
            app.refreshAll();
        }));
    auto owner = LOC(app.profile()["KeyProfiles"][app.triggerSide]);
    deadzoneSection(app, pg, owner);
    curveSection(app, pg, owner);

    GtkWidget* s = add(pg, section("HAIR TRIGGER", "trigger"));
    GtkWidget* b = sectionBody(s);
    auto h = [&app](const char* k) {
        return [&app, k]() -> Json& { return app.profile()["KeyProfiles"][app.triggerSide]["hairTrigger"][k]; };
    };
    auto get = [h] {
        bool on = h("enabled")().is_boolean() && h("enabled")().get<bool>();
        return on ? (h("mode")().is_number() ? h("mode")().get<int>() : 1) : 0;
    };
    auto set = [h](int v) {
        h("enabled")() = v != 0;
        if (v) h("mode")() = v;
    };
    add(b, row("Mode", app.binder.dropdown(labels(hairTriggerNames()), get, set),
                      "Adaptive: re-triggers as soon as the trigger moves back down. Fixed: triggers at a fixed point."));
    GtkWidget* r1 = add(b, scaleRow(app, "Range start", 1, 99, h("beginRange")));
    GtkWidget* r2 = add(b, scaleRow(app, "Range end", 2, 100, h("endRange")));
    // the scales are the last child of each row
    app.binder.setSensitive(gtk_widget_get_last_child(r1), [get] { return get() != 0; });
    app.binder.setSensitive(gtk_widget_get_last_child(r2), [get] { return get() != 0; });
    return pg;
}

// ---- vibration ------------------------------------------------------------------

GtkWidget* pageVibration(App& app) {
    GtkWidget* pg = page();
    auto mo = [&app](const char* k) { return [&app, k]() -> Json& { return app.profile()["BasicInfo"]["motor"][k]; }; };
    GtkWidget* s = add(pg, section("STRENGTH", "vibration"));
    GtkWidget* b = sectionBody(s);
    add(b, scaleRow(app, "Left grip", 0, 4, mo("leftMotor"), "0 = off"));
    add(b, scaleRow(app, "Right grip", 0, 4, mo("rightMotor")));
    add(b, scaleRow(app, "Left trigger", 0, 4, mo("ltMotor")));
    add(b, scaleRow(app, "Right trigger", 0, 4, mo("rtMotor")));

    GtkWidget* t = add(pg, section("TRIGGER VIBRATION", "trigger"));
    GtkWidget* tb = sectionBody(t);
    std::vector<std::string> modes{"From the game", "Force (stronger the deeper you press)", "Sync with grips"};
    for (const char* side : {"lt", "rt"}) {
        std::string f = std::string(side) + "MotorForcibly", sy = std::string(side) + "MotorSync";
        auto get = [&app, f, sy] {
            Json& m = app.profile()["BasicInfo"]["motor"];
            return m[f] == true ? 1 : m[sy] == true ? 2 : 0;
        };
        auto set = [&app, f, sy](int v) {
            Json& m = app.profile()["BasicInfo"]["motor"];
            m[f] = v == 1;
            m[sy] = v == 2;
        };
        add(tb, row(std::string(side) == "lt" ? "Left trigger" : "Right trigger",
                           app.binder.dropdown(modes, get, set), "Force and sync override the game's trigger vibration"));
    }

    GtkWidget* x = add(pg, section("TRY IT", "vibration"));
    GtkWidget* xb = sectionBody(x);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* grip = gtk_button_new_with_label("Try grip vibration");
    GtkWidget* trig = gtk_button_new_with_label("Try trigger vibration");
    g_signal_connect(grip, "clicked", cb(+[](GtkButton*, gpointer d) { static_cast<App*>(d)->testRumble(false); }), &app);
    g_signal_connect(trig, "clicked", cb(+[](GtkButton*, gpointer d) { static_cast<App*>(d)->testRumble(true); }), &app);
    app.binder.setSensitive(grip, [&app] { return app.canSendCommands(); });
    app.binder.setSensitive(trig, [&app] { return app.canSendCommands(); });
    gtk_box_append(GTK_BOX(box), grip);
    gtk_box_append(GTK_BOX(box), trig);
    add(xb, box);
    add(xb, note("Plays the levels above for 0.6 s using the standard GIP rumble message."));
    return pg;
}

// ---- button test ----------------------------------------------------------------

GtkWidget* pageTest(App& app) {
    GtkWidget* pg = page();
    app.inputHint = add(pg, note(""));
    GtkWidget* s = add(pg, section("STICKS AND TRIGGERS", "stick"));
    GtkWidget* b = sectionBody(s);
    GtkWidget* sticks = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget* ls = stickView([&app] { return std::make_pair(app.input.lx, app.input.ly); });
    GtkWidget* rs = stickView([&app] { return std::make_pair(app.input.rx, app.input.ry); });
    app.liveViews = {ls, rs};
    gtk_box_append(GTK_BOX(sticks), ls);
    gtk_box_append(GTK_BOX(sticks), rs);
    add(b, sticks);
    app.ltBar = gtk_level_bar_new_for_interval(0, 1023);
    app.rtBar = gtk_level_bar_new_for_interval(0, 1023);
    gtk_level_bar_set_mode(GTK_LEVEL_BAR(app.ltBar), GTK_LEVEL_BAR_MODE_CONTINUOUS);
    gtk_level_bar_set_mode(GTK_LEVEL_BAR(app.rtBar), GTK_LEVEL_BAR_MODE_CONTINUOUS);
    for (GtkWidget* bar : {app.ltBar, app.rtBar}) {
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_LOW);
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_HIGH);
        gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(bar), GTK_LEVEL_BAR_OFFSET_FULL);
        gtk_widget_set_hexpand(bar, TRUE);
    }
    add(b, row("Left trigger", app.ltBar));
    add(b, row("Right trigger", app.rtBar));

    GtkWidget* bs = add(pg, section("BUTTONS", "buttons"));
    GtkWidget* flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 16);
    const char* const* names = inputButtonNames();
    for (int i = 0; i < 16; ++i) {
        if (std::string(names[i]) == "?") continue;
        GtkWidget* l = gtk_label_new(names[i]);
        gtk_widget_add_css_class(l, "bw-lamp");
        app.buttonLamps[size_t(i)] = l;
        gtk_flow_box_append(GTK_FLOW_BOX(flow), l);
    }
    add(sectionBody(bs), flow);

    GtkWidget* cal = add(pg, section("CALIBRATION", "test"));
    add(sectionBody(cal), note("Stick, trigger and motion calibration and the stick centre fine-tune are not "
                               "available yet: their command sequences still have to be captured."));
    return pg;
}

void updateTest(App& app) {
    if (!app.ltBar) return;
    if (!app.ctl) gtk_label_set_text(GTK_LABEL(app.inputHint), "Connect a controller to see its inputs.");
    else if (!app.inputSeen)
        gtk_label_set_text(GTK_LABEL(app.inputHint), "Waiting for input reports… move a stick.");
    else
        gtk_label_set_text(GTK_LABEL(app.inputHint),
                           "Live values from the controller's standard input report (after its own mapping).");
    for (auto* v : app.liveViews) gtk_widget_queue_draw(v);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(app.ltBar), app.input.leftTrigger);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(app.rtBar), app.input.rightTrigger);
    for (int i = 0; i < 16; ++i) {
        GtkWidget* l = app.buttonLamps[size_t(i)];
        if (!l) continue;
        if (app.input.buttons & (1u << i)) gtk_widget_add_css_class(l, "bw-lamp-on");
        else gtk_widget_remove_css_class(l, "bw-lamp-on");
    }
}

}  // namespace bw::ui
