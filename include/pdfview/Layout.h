#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <pdfview/Document.h>

namespace pdfview {

// how pages are arranged: in rows of `columns` pages (1: single, 2: facing,
// more: overview); firstAlone puts the first page on a row of its own,
// centred (a book's cover); continuous shows all rows, otherwise one row at
// a time
struct ViewMode {
    int columns = 1;
    bool firstAlone = false;
    bool continuous = true;

    bool operator==(const ViewMode&) const = default;
};

// pages placed in rows, top to bottom; all in points. Each column is as
// wide as the widest page. Facing pages meet at the middle; other pages
// are centred in their column and their row.
struct Layout {
    std::vector<PageSize> pages;
    double gap = 10; // between pages and rows; none around the document
    ViewMode mode;
    std::vector<double> x; // of each page's left edge
    std::vector<double> top; // of each page
    std::vector<int> rowOf; // each page's row
    std::vector<int> rowFirst; // each row's first page
    std::vector<double> rowTop; // each row's top, its tallest page's
    std::vector<double> rowH;
    double width = 0; // of the whole document, from the first page's edge to the last's
    double height = 0;

    void build(std::vector<PageSize> sizes, ViewMode m = {});
    double left(int i) const { return x[static_cast<size_t>(i)]; }
    int count() const { return static_cast<int>(pages.size()); }
    int rows() const { return static_cast<int>(rowFirst.size()); }
    int rowLast(int r) const { return r + 1 < rows() ? rowFirst[static_cast<size_t>(r + 1)] - 1 : count() - 1; }
    // the row at y, or the nearest one
    int rowAt(double y) const;
    // the page at (px, py), or the nearest one in the row at py
    int pageAt(double px, double py) const;
    // the page at y in a single column, or the first of the row at y
    int pageAt(double y) const { return rows() ? rowFirst[static_cast<size_t>(rowAt(y))] : 0; }
    // first and last page in the rows touching [y0, y1]; first > last when
    // none
    std::pair<int, int> range(double y0, double y1) const;
    // the width of row r, from its first page's left edge to its last's right
    double rowWidth(int r) const;
};

// what the viewer shows: zoom is screen pixels per point, x and y the
// document point at the view's top left corner. Not continuous, the view
// stays on row `row`.
struct View {
    static constexpr double kMinZoom = 0.05;
    static constexpr double kMaxZoom = 16;

    double zoom = 1;
    double x = 0;
    double y = 0;
    double w = 0; // view size in screen pixels
    double h = 0;
    int row = 0;

    // keeps the view on the document (on its row when not continuous):
    // centred when it is smaller than the view
    void clamp(const Layout& l);
    // zooms keeping the document point under screen point (sx, sy) in place
    void zoomAt(double zoom, double sx, double sy, const Layout& l);
    // the zoom that fits the page's width (single column) or the rows'
    double fitWidth(const Layout& l, int page) const;
    // the zoom that shows the whole page (single column) or its row
    double fitPage(const Layout& l, int page) const;
    // the page at the view's centre
    int currentPage(const Layout& l) const;
    // shows y points below the page's top (its top when < 0) at the top of
    // the view
    void goToPage(int page, const Layout& l, double yInPage = -1);
    // not continuous: moves to the next (dir 1) or previous row, showing
    // its top or its bottom; false at either end
    bool flip(int dir, const Layout& l);
    // the band of y the view may show: the document, or the row
    std::pair<double, double> band(const Layout& l) const;
    // the pages shown, margin screen pixels around the view included
    std::pair<int, int> pages(const Layout& l, double margin = 0) const;
};

// a rendered piece of a page. level picks the scale (see scaleOf), tx/ty the
// tile. kPreview is the small whole-page preview; kWholePage in tx
// requests a whole-page render at the level's scale.
struct TileKey {
    static constexpr int kPreview = -1000;
    static constexpr int kWholePage = -1;

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
