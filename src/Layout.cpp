#include <pdfview/Layout.h>

#include <algorithm>
#include <cmath>

namespace pdfview {

void Layout::build(std::vector<PageSize> sizes, ViewMode m)
{
    pages = std::move(sizes);
    mode = m;
    int cols = std::max(1, m.columns);
    size_t n = pages.size();
    x.assign(n, 0);
    top.assign(n, 0);
    rowOf.assign(n, 0);
    rowFirst.clear();
    rowTop.clear();
    rowH.clear();
    double colW = 0;
    for (const auto& p : pages)
        colW = std::max(colW, p.w);
    // gaps between pages and rows only: the document's edges are the pages'
    width = cols * colW + (cols - 1) * gap;
    double y = 0;
    for (size_t i = 0; i < n;) {
        bool alone = i == 0 && m.firstAlone && cols > 1;
        size_t end = alone ? 1 : std::min(n, i + static_cast<size_t>(cols));
        int r = static_cast<int>(rowFirst.size());
        double rh = 0;
        for (size_t k = i; k < end; k++)
            rh = std::max(rh, pages[k].h);
        for (size_t k = i; k < end; k++) {
            const PageSize& p = pages[k];
            double slot = static_cast<double>(k - i) * (colW + gap);
            if (alone)
                x[k] = (width - p.w) / 2;
            else if (cols == 2)
                x[k] = k == i ? slot + colW - p.w : slot; // meet in the middle
            else
                x[k] = slot + (colW - p.w) / 2;
            top[k] = y + (rh - p.h) / 2;
            rowOf[k] = r;
        }
        rowFirst.push_back(static_cast<int>(i));
        rowTop.push_back(y);
        rowH.push_back(rh);
        y += rh + gap;
        i = end;
    }
    height = rowTop.empty() ? 0 : y - gap;
}

int Layout::rowAt(double y) const
{
    if (rowTop.empty()) return 0;
    // the last row starting above y (counting the gap above it)
    auto it = std::upper_bound(rowTop.begin(), rowTop.end(), y + gap / 2);
    int r = static_cast<int>(it - rowTop.begin()) - 1;
    return std::clamp(r, 0, rows() - 1);
}

int Layout::pageAt(double px, double py) const
{
    if (pages.empty()) return 0;
    int r = rowAt(py);
    int best = rowFirst[static_cast<size_t>(r)];
    double bestD = 1e300;
    for (int k = best; k <= rowLast(r); k++) {
        double x0 = left(k), x1 = x0 + pages[static_cast<size_t>(k)].w;
        if (px >= x0 && px <= x1) return k;
        double d = std::min(std::abs(px - x0), std::abs(px - x1));
        if (d < bestD) {
            bestD = d;
            best = k;
        }
    }
    return best;
}

std::pair<int, int> Layout::range(double y0, double y1) const
{
    if (pages.empty()) return {0, -1};
    int a = static_cast<int>(std::upper_bound(rowTop.begin(), rowTop.end(), y0) - rowTop.begin()) - 1;
    a = std::max(a, 0);
    if (rowTop[static_cast<size_t>(a)] + rowH[static_cast<size_t>(a)] < y0) a++;
    int b = static_cast<int>(std::upper_bound(rowTop.begin(), rowTop.end(), y1) - rowTop.begin()) - 1;
    if (a >= rows()) return {count(), count() - 1};
    if (b < 0) return {0, -1};
    return {rowFirst[static_cast<size_t>(a)], rowLast(std::min(b, rows() - 1))};
}

double Layout::rowWidth(int r) const
{
    int a = rowFirst[static_cast<size_t>(r)], b = rowLast(r);
    return left(b) + pages[static_cast<size_t>(b)].w - left(a);
}

std::pair<double, double> View::band(const Layout& l) const
{
    if (l.mode.continuous || l.rows() == 0) return {0, l.height};
    int r = std::clamp(row, 0, l.rows() - 1);
    double t = l.rowTop[static_cast<size_t>(r)];
    return {t, t + l.rowH[static_cast<size_t>(r)]};
}

void View::clamp(const Layout& l)
{
    if (l.rows() > 0) row = std::clamp(row, 0, l.rows() - 1);
    double vw = w / zoom, vh = h / zoom;
    if (l.width <= vw)
        x = (l.width - vw) / 2;
    else
        x = std::clamp(x, 0.0, l.width - vw);
    auto [b0, b1] = band(l);
    if (b1 - b0 <= vh)
        y = b0 - (vh - (b1 - b0)) / 2;
    else
        y = std::clamp(y, b0, b1 - vh);
}

void View::zoomAt(double z, double sx, double sy, const Layout& l)
{
    z = std::clamp(z, kMinZoom, kMaxZoom);
    double px = x + sx / zoom, py = y + sy / zoom;
    zoom = z;
    x = px - sx / zoom;
    y = py - sy / zoom;
    clamp(l);
}

double View::fitWidth(const Layout& l, int page) const
{
    if (l.pages.empty() || w <= 0) return zoom;
    double pw = l.mode.columns <= 1 ? l.pages[static_cast<size_t>(page)].w : l.width;
    return std::clamp(w / pw, kMinZoom, kMaxZoom);
}

double View::fitPage(const Layout& l, int page) const
{
    if (l.pages.empty() || h <= 0) return zoom;
    if (l.mode.columns <= 1) {
        const PageSize& p = l.pages[static_cast<size_t>(page)];
        return std::clamp(std::min(w / p.w, h / p.h), kMinZoom, kMaxZoom);
    }
    int r = l.rowOf[static_cast<size_t>(page)];
    return std::clamp(std::min(w / l.width, h / l.rowH[static_cast<size_t>(r)]), kMinZoom, kMaxZoom);
}

int View::currentPage(const Layout& l) const
{
    if (l.pages.empty()) return 0;
    double cx = x + w / zoom / 2, cy = y + h / zoom / 2;
    if (!l.mode.continuous) {
        int r = std::clamp(row, 0, l.rows() - 1);
        cy = std::clamp(cy, l.rowTop[static_cast<size_t>(r)], l.rowTop[static_cast<size_t>(r)] + l.rowH[static_cast<size_t>(r)]);
    }
    return l.pageAt(cx, cy);
}

void View::goToPage(int page, const Layout& l, double yInPage)
{
    if (l.pages.empty()) return;
    page = std::clamp(page, 0, l.count() - 1);
    auto i = static_cast<size_t>(page);
    row = l.rowOf[i];
    if (yInPage < 0)
        y = l.rowTop[static_cast<size_t>(row)];
    else
        y = l.top[i] + yInPage;
    // bring the page in sideways when it is off the view
    double vw = w / zoom, px0 = l.left(page), px1 = px0 + l.pages[i].w;
    if (px0 < x || px1 > x + vw) x = vw >= px1 - px0 ? (px0 + px1 - vw) / 2 : px0;
    clamp(l);
}

bool View::flip(int dir, const Layout& l)
{
    if (l.mode.continuous || l.rows() == 0) return false;
    int r = row + (dir > 0 ? 1 : -1);
    if (r < 0 || r >= l.rows()) return false;
    row = r;
    auto [b0, b1] = band(l);
    y = dir > 0 ? b0 : b1 - h / zoom;
    clamp(l);
    return true;
}

std::pair<int, int> View::pages(const Layout& l, double margin) const
{
    double m = margin / zoom;
    auto [a, b] = l.range(y - m, y + h / zoom + m);
    if (!l.mode.continuous && l.rows() > 0) {
        int r = std::clamp(row, 0, l.rows() - 1);
        a = std::max(a, l.rowFirst[static_cast<size_t>(r)]);
        b = std::min(b, l.rowLast(r));
    }
    return {a, b};
}

int levelFor(double scale) { return static_cast<int>(std::ceil(std::log2(scale) * 8 - 1e-9)); }

double scaleOf(int level) { return std::exp2(level / 8.0); }

double previewScale(PageSize s) { return kPreviewSide / std::max(s.w, s.h); }

std::vector<TileKey> visibleTiles(const Layout& l, const View& v, int level, int tileSize, double margin)
{
    std::vector<TileKey> out;
    if (l.pages.empty() || v.w <= 0 || v.h <= 0) return out;
    double s = scaleOf(level);
    double m = margin / v.zoom;
    double x0 = v.x - m, x1 = v.x + v.w / v.zoom + m;
    double y0 = v.y - m, y1 = v.y + v.h / v.zoom + m;
    double cx = v.x + v.w / v.zoom / 2, cy = v.y + v.h / v.zoom / 2;
    std::vector<std::pair<double, TileKey>> found;
    auto [a, b] = v.pages(l, margin);
    for (int i = a; i <= b; i++) {
        const PageSize& p = l.pages[static_cast<size_t>(i)];
        double px = l.left(i), py = l.top[static_cast<size_t>(i)];
        int pw = 0, ph = 0;
        Document::pixelSize(p, s, pw, ph);
        int nx = (pw + tileSize - 1) / tileSize, ny = (ph + tileSize - 1) / tileSize;
        // the view in this page's pixels
        int tx0 = std::max(0, static_cast<int>(std::floor((x0 - px) * s / tileSize)));
        int tx1 = std::min(nx - 1, static_cast<int>(std::floor((x1 - px) * s / tileSize)));
        int ty0 = std::max(0, static_cast<int>(std::floor((y0 - py) * s / tileSize)));
        int ty1 = std::min(ny - 1, static_cast<int>(std::floor((y1 - py) * s / tileSize)));
        for (int ty = ty0; ty <= ty1; ty++)
            for (int tx = tx0; tx <= tx1; tx++) {
                double mx = px + (tx + 0.5) * tileSize / s, my = py + (ty + 0.5) * tileSize / s;
                found.push_back({(mx - cx) * (mx - cx) + (my - cy) * (my - cy), {i, level, tx, ty}});
            }
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
    out.reserve(found.size());
    for (auto& f : found)
        out.push_back(f.second);
    return out;
}

}
