#include <pdfview/Actions.h>

#include <algorithm>
#include <cctype>

namespace pdfview {

namespace {

struct KeyName {
    ImGuiKey key;
    const char* name;
};

// keys without a letter, digit, F or keypad number in their name
constexpr KeyName kNames[] = {
    {ImGuiKey_Tab, "Tab"},
    {ImGuiKey_LeftArrow, "Left"},
    {ImGuiKey_RightArrow, "Right"},
    {ImGuiKey_UpArrow, "Up"},
    {ImGuiKey_DownArrow, "Down"},
    {ImGuiKey_PageUp, "PgUp"},
    {ImGuiKey_PageDown, "PgDown"},
    {ImGuiKey_Home, "Home"},
    {ImGuiKey_End, "End"},
    {ImGuiKey_Insert, "Ins"},
    {ImGuiKey_Delete, "Del"},
    {ImGuiKey_Backspace, "Backspace"},
    {ImGuiKey_Space, "Space"},
    {ImGuiKey_Enter, "Enter"},
    {ImGuiKey_Escape, "Esc"},
    {ImGuiKey_Apostrophe, "'"},
    {ImGuiKey_Comma, "Comma"},
    {ImGuiKey_Minus, "-"},
    {ImGuiKey_Period, "."},
    {ImGuiKey_Slash, "/"},
    {ImGuiKey_Semicolon, "Semicolon"},
    {ImGuiKey_Equal, "="},
    {ImGuiKey_LeftBracket, "["},
    {ImGuiKey_Backslash, "\\"},
    {ImGuiKey_RightBracket, "]"},
    {ImGuiKey_GraveAccent, "`"},
    {ImGuiKey_Pause, "Pause"},
    {ImGuiKey_PrintScreen, "Print"},
    {ImGuiKey_KeypadDecimal, "Num."},
    {ImGuiKey_KeypadDivide, "Num/"},
    {ImGuiKey_KeypadMultiply, "Num*"},
    {ImGuiKey_KeypadSubtract, "Num-"},
    {ImGuiKey_KeypadAdd, "NumPlus"},
    {ImGuiKey_KeypadEnter, "NumEnter"},
    {ImGuiKey_KeypadEqual, "Num="},
    {ImGuiKey_MouseX1, "Back"},
    {ImGuiKey_MouseX2, "Forward"},
};

// other spellings parse() accepts
constexpr KeyName kAliases[] = {
    {ImGuiKey_PageUp, "PageUp"},
    {ImGuiKey_PageDown, "PageDown"},
    {ImGuiKey_Insert, "Insert"},
    {ImGuiKey_Delete, "Delete"},
    {ImGuiKey_Enter, "Return"},
    {ImGuiKey_Escape, "Escape"},
    {ImGuiKey_Comma, ","},
    {ImGuiKey_Minus, "Minus"},
    {ImGuiKey_Equal, "Equal"},
    {ImGuiKey_Period, "Period"},
    {ImGuiKey_Slash, "Slash"},
    {ImGuiKey_KeypadAdd, "Num+"},
};

bool same(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::string keyName(ImGuiKey k)
{
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return std::string(1, static_cast<char>('A' + (k - ImGuiKey_A)));
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return std::string(1, static_cast<char>('0' + (k - ImGuiKey_0)));
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F24) return "F" + std::to_string(1 + (k - ImGuiKey_F1));
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return "Num" + std::to_string(k - ImGuiKey_Keypad0);
    for (const auto& n : kNames)
        if (n.key == k) return n.name;
    return {};
}

ImGuiKey keyFromName(std::string_view s)
{
    if (s.size() == 1 && std::isalpha(static_cast<unsigned char>(s[0]))) return static_cast<ImGuiKey>(ImGuiKey_A + (std::toupper(static_cast<unsigned char>(s[0])) - 'A'));
    if (s.size() == 1 && std::isdigit(static_cast<unsigned char>(s[0]))) return static_cast<ImGuiKey>(ImGuiKey_0 + (s[0] - '0'));
    auto number = [](std::string_view d, int lo, int hi) {
        if (d.empty() || d.size() > 2 || !std::all_of(d.begin(), d.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) return -1;
        int v = std::stoi(std::string(d));
        return v >= lo && v <= hi ? v : -1;
    };
    if (s.size() >= 2 && (s[0] == 'F' || s[0] == 'f'))
        if (int v = number(s.substr(1), 1, 24); v > 0) return static_cast<ImGuiKey>(ImGuiKey_F1 + v - 1);
    if (s.size() == 4 && same(s.substr(0, 3), "Num"))
        if (int v = number(s.substr(3), 0, 9); v >= 0) return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + v);
    for (const auto& n : kNames)
        if (same(s, n.name)) return n.key;
    for (const auto& n : kAliases)
        if (same(s, n.name)) return n.key;
    return ImGuiKey_None;
}

}

KeySeq::KeySeq(std::initializer_list<ImGuiKeyChord> c)
{
    for (ImGuiKeyChord k : c)
        push(k);
}

bool KeySeq::operator==(const KeySeq& o) const { return n == o.n && std::equal(chords.begin(), chords.begin() + n, o.chords.begin()); }

bool KeySeq::prefixOf(const KeySeq& o) const { return n > 0 && n < o.n && std::equal(chords.begin(), chords.begin() + n, o.chords.begin()); }

void KeySeq::push(ImGuiKeyChord c)
{
    if (n < kMax) chords[static_cast<size_t>(n++)] = c;
}

bool bindable(ImGuiKey k)
{
    if (k == ImGuiKey_MouseX1 || k == ImGuiKey_MouseX2) return true;
    return !keyName(k).empty();
}

std::string toString(ImGuiKeyChord c)
{
    std::string s;
    if (c & ImGuiMod_Ctrl) s += "Ctrl+";
    if (c & ImGuiMod_Alt) s += "Alt+";
    if (c & ImGuiMod_Shift) s += "Shift+";
    if (c & ImGuiMod_Super) s += "Meta+";
    return s + keyName(static_cast<ImGuiKey>(c & ~ImGuiMod_Mask_));
}

std::string toString(const KeySeq& seq)
{
    std::string s;
    for (int i = 0; i < seq.n; i++) {
        if (i) s += ", ";
        s += toString(seq.chords[static_cast<size_t>(i)]);
    }
    return s;
}

bool parse(std::string_view text, KeySeq& out)
{
    out = {};
    text = trim(text);
    if (text.empty()) return true;
    while (!text.empty()) {
        // chords are split by ", " ("," alone is a key)
        size_t cut = text.find(", ");
        std::string_view chord = trim(text.substr(0, cut));
        text = cut == std::string_view::npos ? std::string_view{} : text.substr(cut + 2);
        if (out.n == KeySeq::kMax) return false;
        ImGuiKeyChord c = 0;
        ImGuiKey key = ImGuiKey_None;
        while (!chord.empty()) {
            // "+" is the separator, but a last "+" alone is the key
            size_t plus = chord.find('+', 1);
            std::string_view part = trim(chord.substr(0, plus));
            chord = plus == std::string_view::npos ? std::string_view{} : chord.substr(plus + 1);
            if (key != ImGuiKey_None) return false; // a key before the end
            if (same(part, "Ctrl") || same(part, "Control"))
                c |= ImGuiMod_Ctrl;
            else if (same(part, "Alt"))
                c |= ImGuiMod_Alt;
            else if (same(part, "Shift"))
                c |= ImGuiMod_Shift;
            else if (same(part, "Meta") || same(part, "Super") || same(part, "Win") || same(part, "Cmd"))
                c |= ImGuiMod_Super;
            else if ((key = keyFromName(part)) == ImGuiKey_None)
                return false;
        }
        if (key == ImGuiKey_None) return false;
        out.push(c | key);
    }
    return true;
}

Action& Actions::add(std::string id, std::string name, std::string category, std::string_view keys, std::string_view alternate, std::function<void()> run)
{
    Action* a = find(id);
    bool fresh = !a;
    if (fresh) {
        mActions.push_back(std::make_unique<Action>());
        a = mActions.back().get();
        a->id = std::move(id);
    }
    a->name = std::move(name);
    a->category = std::move(category);
    parse(keys, a->defaults[0]);
    parse(alternate, a->defaults[1]);
    if (fresh) a->keys = a->defaults;
    a->run = std::move(run);
    return *a;
}

Action* Actions::find(std::string_view id)
{
    for (auto& a : mActions)
        if (a->id == id) return a.get();
    return nullptr;
}

bool Actions::trigger(std::string_view id)
{
    Action* a = find(id);
    if (!a || !a->run || (a->enabled && !a->enabled())) return false;
    a->run();
    return true;
}

std::string Actions::keysOf(std::string_view id) const
{
    for (const auto& a : mActions)
        if (a->id == id) return toString(a->keys[0].empty() ? a->keys[1] : a->keys[0]);
    return {};
}

Action* Actions::feed(ImGuiKeyChord c, double t)
{
    if (mBuf.n > 0 && t - mLast > kTimeout) clearPending();
    mLast = t;
    for (int attempt = 0; attempt < 2; attempt++) {
        mBuf.push(c);
        Action* exact = nullptr;
        bool longer = false;
        for (auto& a : mActions)
            for (const KeySeq& k : a->keys) {
                if (k == mBuf && (!exact || (a->enabled && !exact->enabled))) exact = a.get();
                if (mBuf.prefixOf(k)) longer = true;
            }
        if (exact && !longer) {
            clearPending();
            return exact;
        }
        if (longer) {
            mExact = exact;
            return nullptr;
        }
        // nothing starts like this: the key may start something on its own
        bool retry = mBuf.n > 1;
        clearPending();
        if (!retry) break;
    }
    return nullptr;
}

Action* Actions::tick(double t)
{
    if (mBuf.n == 0 || t - mLast <= kTimeout) return nullptr;
    Action* a = mExact;
    clearPending();
    return a;
}

void Actions::update()
{
    ImGuiIO& io = ImGui::GetIO();
    double t = ImGui::GetTime();
    if (capturing || io.WantTextInput || io.AppFocusLost) {
        clearPending();
        clearHeld();
        return;
    }
    auto run = [](Action* a) {
        if (!a || !a->run || (a->enabled && !a->enabled())) return false;
        a->run();
        return true;
    };
    auto mods = io.KeyMods & ImGuiMod_Mask_;
    if (mHeld) {
        auto key = static_cast<ImGuiKey>(mHeldChord & ~ImGuiMod_Mask_);
        if (mHeldChord != (key | mods) || !ImGui::IsKeyDown(key) || ImGui::IsKeyPressed(key, false))
            clearHeld();
    }
    run(tick(t));
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        auto key = static_cast<ImGuiKey>(k);
        if (!bindable(key) || !ImGui::IsKeyPressed(key, true)) continue;
        ImGuiKeyChord chord = key | mods;
        if (mHeld && chord == mHeldChord) continue;
        Action* action = feed(chord, t);
        bool hasHold = action && action->hold;
        if (hasHold && !ImGui::IsKeyPressed(key, false)) continue;
        if (run(action) && hasHold) {
            mHeld = action;
            mHeldChord = chord;
            mHeldSince = t;
        }
    }
    if (mHeld && mHeld->hold && (!mHeld->enabled || mHeld->enabled())) {
        constexpr double holdDelay = 0.05;
        double elapsed = t - mHeldSince - holdDelay;
        if (elapsed <= 0) return;
        double dt = std::min(elapsed, static_cast<double>(io.DeltaTime));
        auto key = static_cast<ImGuiKey>(mHeldChord & ~ImGuiMod_Mask_);
        if (!mHeld->hold(dt) && ImGui::IsKeyPressed(key, true)) run(mHeld);
    }
}

std::vector<Action*> Actions::conflicts(const KeySeq& s, const Action* except)
{
    std::vector<Action*> out;
    if (s.empty()) return out;
    for (auto& a : mActions)
        if (a.get() != except && (a->keys[0] == s || a->keys[1] == s)) out.push_back(a.get());
    return out;
}

std::string Actions::save() const
{
    std::string out;
    for (const auto& a : mActions)
        if (a->keys != a->defaults) out += a->id + ": " + toString(a->keys[0]) + "; " + toString(a->keys[1]) + "\n";
    return out;
}

void Actions::load(std::string_view text)
{
    resetAll();
    while (!text.empty()) {
        size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        Action* a = find(trim(line.substr(0, colon)));
        if (!a) continue;
        std::string_view rest = line.substr(colon + 1);
        size_t semi = rest.find(';');
        KeySeq k0, k1;
        if (!parse(rest.substr(0, semi), k0)) continue;
        if (semi != std::string_view::npos && !parse(rest.substr(semi + 1), k1)) continue;
        a->keys = {k0, k1};
    }
}

void Actions::resetAll()
{
    for (auto& a : mActions)
        a->keys = a->defaults;
    clearPending();
}

}
