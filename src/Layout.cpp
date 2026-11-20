#include <pdfview/Layout.h>

#include <algorithm>
#include <cmath>

namespace pdfview {

void Layout::build(std::vector<PageSize> sizes)
{
    pages = std::move(sizes);
    top.resize(pages.size());
    double y = gap, maxW = 0;
    for (size_t i = 0; i < pages.size(); i++) {
        top[i] = y;
        y += pages[i].h + gap;
        maxW = std::max(maxW, pages[i].w);
    }
    width = maxW + 2 * gap;
    height = y;
}

int Layout::pageAt(double y) const
{
    if (pages.empty()) return 0;
    // the last page starting above y (counting the gap above it)
    auto it = std::upper_bound(top.begin(), top.end(), y + gap / 2);
    int i = static_cast<int>(it - top.begin()) - 1;
    return std::clamp(i, 0, count() - 1);
}

std::pair<int, int> Layout::range(double y0, double y1) const
{
    if (pages.empty()) return {0, -1};
    int a = static_cast<int>(std::upper_bound(top.begin(), top.end(), y0) - top.begin()) - 1;
    a = std::max(a, 0);
    if (top[static_cast<size_t>(a)] + pages[static_cast<size_t>(a)].h < y0) a++;
    int b = static_cast<int>(std::upper_bound(top.begin(), top.end(), y1) - top.begin()) - 1;
    return {a, std::min(b, count() - 1)};
}

void View::clamp(const Layout& l)
{
    double vw = w / zoom, vh = h / zoom;
    if (l.width <= vw)
        x = (l.width - vw) / 2;
    else
        x = std::clamp(x, 0.0, l.width - vw);
    if (l.height <= vh)
        y = (l.height - vh) / 2;
    else
        y = std::clamp(y, 0.0, l.height - vh);
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
    return std::clamp(w / (l.pages[static_cast<size_t>(page)].w + 2 * l.gap), kMinZoom, kMaxZoom);
}

double View::fitPage(const Layout& l, int page) const
{
    if (l.pages.empty() || h <= 0) return zoom;
    const PageSize& p = l.pages[static_cast<size_t>(page)];
    return std::clamp(std::min(w / (p.w + 2 * l.gap), h / (p.h + 2 * l.gap)), kMinZoom, kMaxZoom);
}

void View::goToPage(int page, const Layout& l)
{
    if (l.pages.empty()) return;
    page = std::clamp(page, 0, l.count() - 1);
    y = l.top[static_cast<size_t>(page)] - l.gap / 2;
    clamp(l);
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
    auto [a, b] = l.range(y0, y1);
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
