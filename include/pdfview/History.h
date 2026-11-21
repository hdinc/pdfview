#pragma once

#include <deque>

namespace pdfview {

// back and forward through the places jumped from, like a browser's
struct History {
    struct Entry {
        int page = 0;
        double y = 0; // points below the page's top

        bool near(const Entry& o) const { return page == o.page && (y - o.y) * (y - o.y) < 4; }
    };
    static constexpr size_t kMax = 100;

    std::deque<Entry> backs;
    std::deque<Entry> forwards;

    // before a jump away from cur
    void push(Entry cur)
    {
        forwards.clear();
        if (!backs.empty() && backs.back().near(cur)) return;
        backs.push_back(cur);
        if (backs.size() > kMax) backs.pop_front();
    }
    // where to go back to from cur; false when there's nowhere
    bool back(Entry cur, Entry& to)
    {
        // skip places we're at already
        while (!backs.empty() && backs.back().near(cur))
            backs.pop_back();
        if (backs.empty()) return false;
        to = backs.back();
        backs.pop_back();
        forwards.push_back(cur);
        return true;
    }
    bool forward(Entry cur, Entry& to)
    {
        while (!forwards.empty() && forwards.back().near(cur))
            forwards.pop_back();
        if (forwards.empty()) return false;
        to = forwards.back();
        forwards.pop_back();
        backs.push_back(cur);
        return true;
    }
    void clear()
    {
        backs.clear();
        forwards.clear();
    }
};

}
