// GUI state: connection, profiles and the main window.
#pragma once

#include "controller.h"
#include "art.h"
#include "widgets.h"

#include <array>
#include <memory>
#include <thread>
#include <string>

namespace bw::ui {

struct Options {
    bool dryRun = false;        // use the simulator
    bool readOnly = false;      // force read-only even for the simulator
    bool allowWrites = false;   // real hardware: allow commands that change the controller
    bool unverifiedLayout = false;
    bool selfTest = false;
    std::string screenshot;     // write a PNG of the window and quit
    std::string page;           // page to show first (screenshot)
    uint16_t simPid = 0x109B;
};

struct App {
    Options opt;
    GtkApplication* gtkApp = nullptr;
    GtkWidget* window = nullptr;

    // connection
    std::unique_ptr<Controller> ctl;
    guint pumpSource = 0, scanSource = 0;
    InputState input;
    bool inputSeen = false;
    std::string firmware;  // e.g. "0251"; empty when unknown
    guint appModeSource = 0;  // pending reconnect after the app-mode knock
    std::thread restoreThread;  // failsafe: knocks the controller back into app mode after a session
    // Releases the controller. If a config session ran, the controller leaves app mode ~2 s later; this then
    // knocks it back (on a worker thread when `background`, reporting in the status bar; else blocking).
    void endSession(bool background);
    std::string scanText = "No controller";  // refreshed by the 2 s scan timer while disconnected
    void rescan();

    // profiles: index 1..5 (5 = Shift layer)
    std::array<Json, kProfileSlots + 1> profiles;     // edited copy
    std::array<Json, kProfileSlots + 1> onDevice;     // last read from / written to the device
    std::array<bool, kProfileSlots + 1> fromDevice{}; // false = defaults shown, not read
    int slot = 1;
    int activeSlot = -1;

    // controller drawing: "Automatic" (from the connected edition) or a scheme name from artSchemeNames()
    std::string artScheme = "Automatic";
    ArtColors artColors() const;
    void loadSettings();
    void saveSettings() const;

    // sub-page selectors
    std::string stickSide = "Left", triggerSide = "L2", motionSide = "Aim";

    Binder binder;
    std::vector<GtkWidget*> curveEditors, liveViews;

    // widgets
    GtkWidget *deviceLabel = nullptr, *modeBadge = nullptr, *connectButton = nullptr;
    GtkWidget *statusLabel = nullptr, *applyButton = nullptr, *revertButton = nullptr;
    GtkWidget *activateButton = nullptr, *stack = nullptr, *sidebar = nullptr, *banner = nullptr;
    std::array<GtkWidget*, kProfileSlots + 1> slotButtons{};
    GtkWidget* infoGrid = nullptr;
    std::array<GtkWidget*, 16> buttonLamps{};
    GtkWidget *ltBar = nullptr, *rtBar = nullptr, *inputHint = nullptr;

    Json& profile() { return profiles[size_t(slot)]; }
    Product product() const;
    bool dirty(int s) const { return profiles[size_t(s)] != onDevice[size_t(s)]; }
    bool canWriteProfiles(std::string* why = nullptr) const;
    // Edits in the current slot that the codec cannot write yet (JSON paths); empty in the simulator too.
    std::vector<std::string> unwritableEdits() const;
    bool canSendCommands() const { return ctl && !ctl->info().readOnly; }

    // actions
    void connect();
    void disconnect(const std::string& reason = {});
    void loadSlot(int s);
    void selectSlot(int s);
    void apply();
    void revert();
    void activateSlot();
    void resetSlot();
    void exportSlot();
    void importSlot();
    void testRumble(bool triggers);

    void modelChanged();   // a bound value changed
    void refreshAll();     // re-read every widget from the model
    void updateChrome();   // header, slot buttons, apply button, banner
    void status(const std::string& text, bool error = false);
};

GtkWidget* buildWindow(App& app);
// Pages (pages.cpp). Each returns the page widget; bindings go through app.binder.
GtkWidget* pageOverview(App& app);
GtkWidget* pageButtons(App& app);
GtkWidget* pageSticks(App& app);
GtkWidget* pageMotion(App& app);
GtkWidget* pageTriggers(App& app);
GtkWidget* pageVibration(App& app);
GtkWidget* pageTest(App& app);
void updateOverview(App& app);
void updateTest(App& app);

}  // namespace bw::ui
