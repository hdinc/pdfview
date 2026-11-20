#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <pdfview/Document.h>

namespace pdfview {

// pages stacked top to bottom, centred on the widest one; all in points
struct Layout {
    std::vector<PageSize> pages;
    double gap = 10; // around and between pages
    std::vector<double> top; // of each page
    double width = 0; // of the whole document, gaps included
    double height = 0;

    void build(std::vector<PageSize> sizes);
    double left(int i) const { return (width - pages[static_cast<size_t>(i)].w) / 2; }
    int count() const { return static_cast<int>(pages.size()); }
    // the page at y, or the nearest one
    int pageAt(double y) const;
    // first and last page touching [y0, y1]; first > last when none
    std::pair<int, int> range(double y0, double y1) const;
};

// what the viewer shows: zoom is screen pixels per point, x and y the
// document point at the view's top left corner
struct View {
    static constexpr double kMinZoom = 0.05;
    static constexpr double kMaxZoom = 16;

    double zoom = 1;
    double x = 0;
    double y = 0;
    double w = 0; // view size in screen pixels
    double h = 0;

    // keeps the view on the document: centred horizontally when it is
    // narrower than the view
    void clamp(const Layout& l);
    // zooms keeping the document point under screen point (sx, sy) in place
    void zoomAt(double zoom, double sx, double sy, const Layout& l);
    double fitWidth(const Layout& l, int page) const;
    double fitPage(const Layout& l, int page) const;
    // the page at the view's centre
    int currentPage(const Layout& l) const { return l.pageAt(y + h / zoom / 2); }
    void goToPage(int page, const Layout& l);
};

// a rendered piece of a page. level picks the scale (see scaleOf), tx/ty the
// tile; kPreview is the whole page small, shown while tiles load
struct TileKey {
    static constexpr int kPreview = -1000;

    int page = 0;
    int level = kPreview;
    int tx = 0;
    int ty = 0;

    bool operator==(const TileKey&) const = default;
    auto operator<=>(const TileKey&) const = default;
};

// scales come in steps of 2^(1/8), about 9%, so a little zooming reuses
// tiles until the zoom settles
int levelFor(double scale);
double scaleOf(int level);
// the scale of a page's preview: its longest side about kPreviewSide pixels
constexpr int kPreviewSide = 320;
double previewScale(PageSize s);

// the tiles of level that cover the view (plus margin screen pixels around
// it), nearest the view's centre first
std::vector<TileKey> visibleTiles(const Layout& l, const View& v, int level, int tileSize, double margin = 0);

}
