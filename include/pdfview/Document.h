#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pdfview {

// a page's size in points (1/72 inch) as displayed, its /Rotate applied
struct PageSize {
    double w = 0;
    double h = 0;
};

// tightly packed pixels, 4 bytes each: B, G, R, A in memory (GL_BGRA)
struct Bitmap {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> bgra;
};

// a rectangle on a page in points, as displayed: origin at the page's top
// left, y down
struct Rect {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    bool contains(double x, double y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    bool operator==(const Rect&) const = default;
};

// a place in the document: page (0 based, -1: none) and the y on it in
// points from the page's top (-1: the page's top, keep the x)
struct Dest {
    int page = -1;
    double y = -1;

    bool valid() const { return page >= 0; }
};

struct OutlineItem {
    std::string title;
    Dest dest;
    bool open = false; // shown expanded at first
    std::vector<OutlineItem> children;
};

struct Link {
    enum class Kind { Goto, Uri, Other };
    Rect rect;
    Kind kind = Kind::Other;
    Dest dest; // Goto
    std::string uri; // Uri
};

// a page's text in reading order, one entry per character (code point) with
// its box; '\n' ends a line
struct TextLayout {
    std::u32string text;
    std::vector<Rect> boxes;
};

enum FindFlags : unsigned {
    FindDefault = 0,
    FindCaseSensitive = 1,
    FindWholeWords = 2,
};

// a PDF opened read-only through poppler. Not thread safe: one thread uses
// it at a time (the viewer's render thread).
class Document {
public:
    // nullptr and err on failure; a password protected file fails with
    // err "locked" unless the right password is given
    static std::unique_ptr<Document> open(const std::string& path, std::string& err, const std::string& password = {});
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    int pages() const { return static_cast<int>(mSizes.size()); }
    PageSize pageSize(int i) const { return mSizes[static_cast<size_t>(i)]; }
    const std::vector<PageSize>& pageSizes() const { return mSizes; }
    std::string title() const;
    std::string author() const;
    // the page's label ("iv", "12"); its number when the PDF has none
    std::string pageLabel(int i) const;
    std::vector<OutlineItem> outline();

    std::vector<Link> links(int page);
    TextLayout text(int page);
    // each match's box on the page, in page order
    std::vector<Rect> find(int page, const std::string& utf8, unsigned flags = FindDefault);

    // pixel size of a page rendered at scale (pixels per point)
    static void pixelSize(PageSize s, double scale, int& w, int& h);

    // renders the part x, y, w, h (pixels at scale) of page i; the part is
    // clipped to the page
    bool render(int page, double scale, int x, int y, int w, int h, Bitmap& out, std::string& err);
    // the whole page
    bool render(int page, double scale, Bitmap& out, std::string& err);

private:
    struct Impl;
    Document();
    std::unique_ptr<Impl> d;
    std::vector<PageSize> mSizes;
    std::vector<std::string> mLabels;
};

std::string toUtf8(std::u32string_view s);

}
