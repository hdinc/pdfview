#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

namespace pdfview {

// a key sequence: up to four chords (an ImGuiKey with ImGuiMod_ flags),
// typed one after another ("G, G", "Ctrl+K, Ctrl+S")
struct KeySeq {
    static constexpr int kMax = 4;
    std::array<ImGuiKeyChord, kMax> chords{};
    int n = 0;

    KeySeq() = default;
    KeySeq(std::initializer_list<ImGuiKeyChord> c);
    bool empty() const { return n == 0; }
    bool operator==(const KeySeq& o) const;
    // o starts with this sequence and is longer
    bool prefixOf(const KeySeq& o) const;
    void push(ImGuiKeyChord c);
};

// "Ctrl+Shift+N", "G, G"; empty for no keys. The names are our own (not
// ImGui's debug names), so saved shortcuts stay valid.
std::string toString(ImGuiKeyChord c);
std::string toString(const KeySeq& s);
// false when the text names a key we don't know
bool parse(std::string_view text, KeySeq& out);
// a key that can end a chord: not a modifier, not a mouse button
bool bindable(ImGuiKey k);

struct Action {
    std::string id; // "view.overview": what the saved shortcuts use
    std::string name; // "Overview"
    std::string category; // "View"
    std::array<KeySeq, 2> defaults; // primary and alternate
    std::array<KeySeq, 2> keys;
    std::function<void()> run;
    std::function<bool()> enabled; // optional: run only when true
    std::function<bool()> checked; // optional: a toggle's state, for menus
    std::function<bool(double seconds)> hold; // false falls back to ordinary key repeat
};

// the actions of a program and their key bindings; matches typed keys to
// actions. A sequence that is also the start of a longer one waits a
// moment for the next key.
class Actions {
public:
    static constexpr double kTimeout = 1.0; // seconds between keys of a sequence

    // ids are unique: adding one again replaces it (keeping its keys)
    Action& add(std::string id, std::string name, std::string category, std::string_view keys = {}, std::string_view alternate = {}, std::function<void()> run = {});
    Action* find(std::string_view id);
    const std::vector<std::unique_ptr<Action>>& all() const { return mActions; }
    // runs the action when it is enabled; false when it isn't there or isn't
    // enabled
    bool trigger(std::string_view id);
    // its primary keys as text, for menus ("" when none)
    std::string keysOf(std::string_view id) const;

    // takes one typed chord at time t (seconds) and returns the action it
    // completes, if any
    Action* feed(ImGuiKeyChord c, double t);
    // the action a waiting sequence stands for once its time is up
    Action* tick(double t);
    // typed so far, waiting for the rest
    const KeySeq& pending() const { return mBuf; }
    void clearPending() { mBuf = {}; mExact = nullptr; }

    // reads this frame's key presses from ImGui and runs what they complete;
    // nothing while a text field has the keyboard or keys are being captured
    void update();
    bool holding() const { return mHeld != nullptr; }
    void clearHeld() { mHeld = nullptr; }
    // set while an editor captures keys
    bool capturing = false;

    // the actions bound to s, other than except
    std::vector<Action*> conflicts(const KeySeq& s, const Action* except = nullptr);
    // bindings that differ from the defaults, one action a line:
    // "id: keys; alternate"
    std::string save() const;
    // applies saved bindings over the defaults; unknown ids and keys are
    // skipped
    void load(std::string_view text);
    void resetAll();

private:
    std::vector<std::unique_ptr<Action>> mActions;
    KeySeq mBuf;
    Action* mExact = nullptr; // what mBuf completes, while waiting for more
    double mLast = 0;
    Action* mHeld = nullptr;
    ImGuiKeyChord mHeldChord = ImGuiKey_None;
    double mHeldSince = 0;
};

}
