#include <pdfview/Document.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <list>

#include <cairo.h>
#include <poppler.h>

namespace pdfview {

namespace {

// poppler pages hold their text once it has been asked for; keep a few
constexpr size_t kPageCache = 24;

std::string take(gchar* s)
{
    std::string out = s ? s : "";
    g_free(s);
    return out;
}

}

struct Document::Impl {
    PopplerDocument* doc = nullptr;
    // most recently used first
    std::list<std::pair<int, PopplerPage*>> pages;

    ~Impl()
    {
        for (auto& [i, p] : pages)
            g_object_unref(p);
        if (doc) g_object_unref(doc);
    }

    PopplerPage* page(int i)
    {
        for (auto it = pages.begin(); it != pages.end(); ++it)
            if (it->first == i) {
                pages.splice(pages.begin(), pages, it);
                return it->second;
            }
        PopplerPage* p = poppler_document_get_page(doc, i);
        if (!p) return nullptr;
        pages.emplace_front(i, p);
        if (pages.size() > kPageCache) {
            g_object_unref(pages.back().second);
            pages.pop_back();
        }
        return p;
    }
};

Document::Document()
    : d(std::make_unique<Impl>())
{
}

Document::~Document() = default;

std::unique_ptr<Document> Document::open(const std::string& path, std::string& err, const std::string& password)
{
    std::error_code ec;
    auto abs = std::filesystem::absolute(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
    if (ec || !std::filesystem::is_regular_file(abs, ec)) {
        err = "can't open " + path;
        return nullptr;
    }
    auto u8 = abs.u8string();
    GError* gerr = nullptr;
    gchar* uri = g_filename_to_uri(reinterpret_cast<const char*>(u8.c_str()), nullptr, &gerr);
    if (!uri) {
        err = "can't open " + path + (gerr ? std::string(": ") + gerr->message : "");
        if (gerr) g_error_free(gerr);
        return nullptr;
    }
    PopplerDocument* doc = poppler_document_new_from_file(uri, password.empty() ? nullptr : password.c_str(), &gerr);
    g_free(uri);
    if (!doc) {
        if (gerr && gerr->domain == POPPLER_ERROR && gerr->code == POPPLER_ERROR_ENCRYPTED)
            err = "locked";
        else
            err = "can't open " + path + " as a PDF" + (gerr ? std::string(": ") + gerr->message : "");
        if (gerr) g_error_free(gerr);
        return nullptr;
    }
    std::unique_ptr<Document> d(new Document());
    d->d->doc = doc;
    int n = poppler_document_get_n_pages(doc);
    d->mSizes.resize(static_cast<size_t>(std::max(n, 0)));
    d->mLabels.resize(d->mSizes.size());
    for (int i = 0; i < n; i++) {
        PageSize s{612, 792}; // a broken page gets letter size
        if (PopplerPage* p = poppler_document_get_page(doc, i)) {
            poppler_page_get_size(p, &s.w, &s.h);
            s.w = std::abs(s.w);
            s.h = std::abs(s.h);
            if (s.w < 1 || s.h < 1) s = {612, 792};
            d->mLabels[static_cast<size_t>(i)] = take(poppler_page_get_label(p));
            g_object_unref(p);
        }
        d->mSizes[static_cast<size_t>(i)] = s;
    }
    if (d->mSizes.empty()) {
        err = path + ": the PDF has no pages";
        return nullptr;
    }
    return d;
}

std::string Document::title() const { return take(poppler_document_get_title(d->doc)); }

std::string Document::author() const { return take(poppler_document_get_author(d->doc)); }

std::string Document::pageLabel(int i) const
{
    const std::string& l = mLabels[static_cast<size_t>(i)];
    return l.empty() ? std::to_string(i + 1) : l;
}

namespace {

// a page shown turned a quarter: its crop box is the displayed size
// swapped. (Half turned pages aren't told apart; their dests are rare.)
bool turned(PopplerDocument* pdoc, int page, PageSize s)
{
    PopplerPage* p = poppler_document_get_page(pdoc, page);
    if (!p) return false;
    PopplerRectangle r;
    poppler_page_get_crop_box(p, &r);
    g_object_unref(p);
    double w = std::abs(r.x2 - r.x1), h = std::abs(r.y2 - r.y1);
    return std::abs(w - s.w) > 0.5 && std::abs(w - s.h) < 0.5 && std::abs(h - s.w) < 0.5;
}

// poppler's dest y is from the page's bottom (after its crop box); ours is
// from the top. Rotated pages: the page's top.
Dest toDest(const Document& doc, PopplerDocument* pdoc, const PopplerDest* pd, int depth = 0)
{
    Dest out;
    if (!pd) return out;
    if (pd->type == POPPLER_DEST_NAMED) {
        if (depth > 4 || !pd->named_dest) return out;
        PopplerDest* r = poppler_document_find_dest(pdoc, pd->named_dest);
        out = toDest(doc, pdoc, r, depth + 1);
        if (r) poppler_dest_free(r);
        return out;
    }
    if (pd->page_num < 1 || pd->page_num > doc.pages()) return out;
    out.page = pd->page_num - 1;
    PageSize s = doc.pageSize(out.page);
    bool hasTop = false;
    switch (pd->type) {
    case POPPLER_DEST_XYZ:
        hasTop = pd->change_top;
        break;
    case POPPLER_DEST_FITH:
    case POPPLER_DEST_FITBH:
    case POPPLER_DEST_FITR:
        hasTop = true;
        break;
    default:
        break;
    }
    if (hasTop && !turned(pdoc, out.page, s)) out.y = std::clamp(s.h - pd->top, 0.0, s.h);
    return out;
}

Dest actionDest(const Document& doc, PopplerDocument* pdoc, const PopplerAction* a, int fromPage)
{
    if (!a) return {};
    if (a->type == POPPLER_ACTION_GOTO_DEST) return toDest(doc, pdoc, a->goto_dest.dest);
    if (a->type == POPPLER_ACTION_NAMED && a->named.named_dest) {
        std::string n = a->named.named_dest;
        int p = n == "NextPage" ? fromPage + 1 : n == "PrevPage" ? fromPage - 1 : n == "FirstPage" ? 0 : n == "LastPage" ? doc.pages() - 1 : -1;
        if (p >= 0 && p < doc.pages()) return {p, -1};
    }
    return {};
}

void readOutline(const Document& doc, PopplerDocument* pdoc, PopplerIndexIter* it, std::vector<OutlineItem>& out, int depth)
{
    if (depth > 64) return;
    do {
        OutlineItem item;
        PopplerAction* a = poppler_index_iter_get_action(it);
        if (a) {
            if (a->any.title) item.title = a->any.title;
            item.dest = actionDest(doc, pdoc, a, -1);
            poppler_action_free(a);
        }
        item.open = poppler_index_iter_is_open(it);
        if (PopplerIndexIter* child = poppler_index_iter_get_child(it)) {
            readOutline(doc, pdoc, child, item.children, depth + 1);
            poppler_index_iter_free(child);
        }
        out.push_back(std::move(item));
    } while (poppler_index_iter_next(it));
}

}

std::vector<OutlineItem> Document::outline()
{
    std::vector<OutlineItem> out;
    if (PopplerIndexIter* it = poppler_index_iter_new(d->doc)) {
        readOutline(*this, d->doc, it, out, 0);
        poppler_index_iter_free(it);
    }
    return out;
}

std::vector<Link> Document::links(int page)
{
    std::vector<Link> out;
    PopplerPage* p = d->page(page);
    if (!p) return out;
    double h = pageSize(page).h;
    GList* list = poppler_page_get_link_mapping(p);
    for (GList* l = list; l; l = l->next) {
        auto* m = static_cast<PopplerLinkMapping*>(l->data);
        Link k;
        // link areas are from the page's bottom
        k.rect = {std::min(m->area.x1, m->area.x2), h - std::max(m->area.y1, m->area.y2), std::max(m->area.x1, m->area.x2), h - std::min(m->area.y1, m->area.y2)};
        if (m->action && m->action->type == POPPLER_ACTION_URI && m->action->uri.uri) {
            k.kind = Link::Kind::Uri;
            k.uri = m->action->uri.uri;
        } else if (Dest dst = actionDest(*this, d->doc, m->action, page); dst.valid()) {
            k.kind = Link::Kind::Goto;
            k.dest = dst;
        }
        out.push_back(std::move(k));
    }
    poppler_page_free_link_mapping(list);
    // poppler lists them last first
    std::reverse(out.begin(), out.end());
    return out;
}

TextLayout Document::text(int page)
{
    TextLayout out;
    PopplerPage* p = d->page(page);
    if (!p) return out;
    gchar* utf8 = poppler_page_get_text(p);
    PopplerRectangle* rects = nullptr;
    guint n = 0;
    if (utf8 && poppler_page_get_text_layout(p, &rects, &n)) {
        glong len = 0;
        gunichar* u = g_utf8_to_ucs4_fast(utf8, -1, &len);
        size_t m = std::min(static_cast<size_t>(len), static_cast<size_t>(n));
        out.text.assign(reinterpret_cast<const char32_t*>(u), m);
        out.boxes.resize(m);
        for (size_t i = 0; i < m; i++)
            out.boxes[i] = {rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2};
        g_free(u);
    }
    g_free(rects);
    g_free(utf8);
    return out;
}

std::vector<Rect> Document::find(int page, const std::string& utf8, unsigned flags)
{
    std::vector<Rect> out;
    PopplerPage* p = d->page(page);
    if (!p || utf8.empty()) return out;
    unsigned pf = POPPLER_FIND_DEFAULT;
    if (flags & FindCaseSensitive) pf |= POPPLER_FIND_CASE_SENSITIVE;
    if (flags & FindWholeWords) pf |= POPPLER_FIND_WHOLE_WORDS_ONLY;
    double h = pageSize(page).h;
    GList* list = poppler_page_find_text_with_options(p, utf8.c_str(), static_cast<PopplerFindFlags>(pf));
    for (GList* l = list; l; l = l->next) {
        auto* r = static_cast<PopplerRectangle*>(l->data);
        // matches are from the page's bottom
        out.push_back({std::min(r->x1, r->x2), h - std::max(r->y1, r->y2), std::max(r->x1, r->x2), h - std::min(r->y1, r->y2)});
        poppler_rectangle_free(r);
    }
    g_list_free(list);
    std::stable_sort(out.begin(), out.end(), [](const Rect& a, const Rect& b) {
        // same line when they overlap vertically
        if (a.y1 > b.y0 && b.y1 > a.y0) return a.x0 < b.x0;
        return a.y0 < b.y0;
    });
    return out;
}

void Document::pixelSize(PageSize s, double scale, int& w, int& h)
{
    // the tolerance keeps exact sizes (612 pt at scale 1) from rounding up
    w = std::max(1, static_cast<int>(std::ceil(s.w * scale - 1e-6)));
    h = std::max(1, static_cast<int>(std::ceil(s.h * scale - 1e-6)));
}

bool Document::render(int i, double scale, int x, int y, int w, int h, Bitmap& out, std::string& err)
{
    PopplerPage* p = d->page(i);
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
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        err = "can't render page " + std::to_string(i + 1);
        return false;
    }
    cairo_t* cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_translate(cr, -x, -y);
    cairo_scale(cr, scale, scale);
    poppler_page_render(p, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    // ARGB32 is B, G, R, A in memory on little endian machines; the page is
    // opaque, so premultiplied or not makes no difference
    const unsigned char* src = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    out.w = w;
    out.h = h;
    out.bgra.resize(static_cast<size_t>(w) * h * 4);
    for (int row = 0; row < h; row++)
        memcpy(&out.bgra[static_cast<size_t>(row) * w * 4], src + static_cast<size_t>(row) * stride, static_cast<size_t>(w) * 4);
    cairo_surface_destroy(surf);
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

std::string toUtf8(std::u32string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char32_t c : s) {
        if (c < 0x80)
            out += static_cast<char>(c);
        else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

}
