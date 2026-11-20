#include <pdfview/Document.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <poppler-document.h>
#include <poppler-image.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>

namespace pdfview {

std::unique_ptr<Document> Document::open(const std::string& path, std::string& err)
{
    std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(path));
    if (!doc) {
        err = "can't open " + path + " as a PDF";
        return nullptr;
    }
    if (doc->is_locked()) {
        err = "locked";
        return nullptr;
    }
    std::unique_ptr<Document> d(new Document());
    int n = doc->pages();
    d->mPages.resize(static_cast<size_t>(std::max(n, 0)));
    d->mSizes.resize(d->mPages.size());
    d->mDoc = std::move(doc);
    for (int i = 0; i < n; i++) {
        PageSize s{612, 792}; // a broken page gets letter size
        if (poppler::page* p = d->page(i)) {
            poppler::rectf r = p->page_rect();
            s = {std::abs(r.width()), std::abs(r.height())};
            auto o = p->orientation();
            if (o == poppler::page::landscape || o == poppler::page::seascape) std::swap(s.w, s.h);
            if (s.w < 1 || s.h < 1) s = {612, 792};
        }
        d->mSizes[static_cast<size_t>(i)] = s;
    }
    if (d->mSizes.empty()) {
        err = path + ": the PDF has no pages";
        return nullptr;
    }
    d->mRenderer = std::make_unique<poppler::page_renderer>();
    d->mRenderer->set_render_hint(poppler::page_renderer::antialiasing, true);
    d->mRenderer->set_render_hint(poppler::page_renderer::text_antialiasing, true);
    d->mRenderer->set_image_format(poppler::image::format_argb32);
    return d;
}

Document::~Document() = default;

poppler::page* Document::page(int i)
{
    if (i < 0 || i >= pages()) return nullptr;
    auto& p = mPages[static_cast<size_t>(i)];
    if (!p) p.reset(mDoc->create_page(i));
    return p.get();
}

// poppler's UTF-8 is a byte array: not null terminated, and empty (a null
// data()) when the field is missing
static std::string utf8(const poppler::ustring& s)
{
    auto b = s.to_utf8();
    return std::string(b.begin(), b.end());
}

std::string Document::title() const { return utf8(mDoc->get_title()); }

std::string Document::author() const { return utf8(mDoc->get_author()); }

void Document::pixelSize(PageSize s, double scale, int& w, int& h)
{
    // the tolerance keeps exact sizes (612 pt at scale 1) from rounding up
    w = std::max(1, static_cast<int>(std::ceil(s.w * scale - 1e-6)));
    h = std::max(1, static_cast<int>(std::ceil(s.h * scale - 1e-6)));
}

bool Document::render(int i, double scale, int x, int y, int w, int h, Bitmap& out, std::string& err)
{
    poppler::page* p = page(i);
    if (!p) {
        err = "no page " + std::to_string(i + 1);
        return false;
    }
    int pw = 0, ph = 0;
    pixelSize(mSizes[static_cast<size_t>(i)], scale, pw, ph);
    x = std::clamp(x, 0, pw);
    y = std::clamp(y, 0, ph);
    w = std::min(w, pw - x);
    h = std::min(h, ph - y);
    if (w <= 0 || h <= 0) {
        err = "empty region";
        return false;
    }
    poppler::image img = mRenderer->render_page(p, 72.0 * scale, 72.0 * scale, x, y, w, h);
    if (!img.is_valid() || img.format() != poppler::image::format_argb32) {
        err = "can't render page " + std::to_string(i + 1);
        return false;
    }
    out.w = std::min(w, img.width());
    out.h = std::min(h, img.height());
    out.bgra.assign(static_cast<size_t>(w) * h * 4, 0xff);
    const char* src = img.const_data();
    for (int row = 0; row < out.h; row++)
        memcpy(&out.bgra[static_cast<size_t>(row) * w * 4], src + static_cast<size_t>(row) * img.bytes_per_row(), static_cast<size_t>(out.w) * 4);
    // poppler may come back a pixel short; the rest stays white
    out.w = w;
    out.h = h;
    return true;
}

bool Document::render(int i, double scale, Bitmap& out, std::string& err)
{
    if (i < 0 || i >= pages()) {
        err = "no page " + std::to_string(i + 1);
        return false;
    }
    int w = 0, h = 0;
    pixelSize(mSizes[static_cast<size_t>(i)], scale, w, h);
    return render(i, scale, 0, 0, w, h, out, err);
}

}
