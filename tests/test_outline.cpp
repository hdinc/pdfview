#include <doctest/doctest.h>

#include <pdfview/Panels.h>

using namespace pdfview;

namespace {

OutlineItem item(const char* title, int page, double y, std::vector<OutlineItem> children = {})
{
    return {title, {page, y}, false, std::move(children)};
}

const char* current(const std::vector<OutlineItem>& items, int page, double y)
{
    const OutlineItem* it = currentOutlineItem(items, {page, y});
    return it ? it->title.c_str() : "";
}

}

TEST_CASE("currentOutlineItem: the latest start at or before the position")
{
    std::vector<OutlineItem> items = {
        item("Welcome", 1, 0),
        item("Chapter 1", 2, 0, {item("Section 1.1", 2, 0), item("Section 1.2", 5, 300)}),
        item("Chapter 2", 10, -1),
    };
    CHECK(std::string(current(items, 0, 0)).empty()); // before the first entry
    CHECK(std::string(current(items, 1, 50)) == "Welcome");
    // a section starting where its chapter does wins (the last in reading order)
    CHECK(std::string(current(items, 3, 0)) == "Section 1.1");
    // up to 40 points below the view's top counts as begun
    CHECK(std::string(current(items, 5, 200)) == "Section 1.1");
    CHECK(std::string(current(items, 5, 270)) == "Section 1.2");
    // an unknown y is the page's top
    CHECK(std::string(current(items, 10, 0)) == "Chapter 2");
    CHECK(std::string(current(items, 12, 0)) == "Chapter 2");
}

TEST_CASE("currentOutlineItem: entries out of page order")
{
    // Crafting Interpreters ends its outline with "Blank Page" -> page 1:
    // it must not win past its own page
    std::vector<OutlineItem> items = {
        item("Welcome", 1, 0),
        item("Chapter 1", 2, 0),
        item("Chapter 3", 20, 0),
        item("Appendix", 30, 0),
        item("Blank Page", 0, 0),
    };
    CHECK(std::string(current(items, 0, 0)) == "Blank Page");
    CHECK(std::string(current(items, 1, 0)) == "Welcome");
    CHECK(std::string(current(items, 20, 0)) == "Chapter 3");
    CHECK(std::string(current(items, 40, 0)) == "Appendix");
}
