#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace poppler {
class document;
class page;
class page_renderer;
}

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

// a PDF opened read-only through poppler. Not thread safe: one thread uses
// it at a time (the viewer's render thread).
class Document {
public:
    // nullptr and err on failure; a password protected file fails with
    // err "locked"
    static std::unique_ptr<Document> open(const std::string& path, std::string& err);
    ~Document();

    int pages() const { return static_cast<int>(mSizes.size()); }
    PageSize pageSize(int i) const { return mSizes[static_cast<size_t>(i)]; }
    const std::vector<PageSize>& pageSizes() const { return mSizes; }
    std::string title() const;
    std::string author() const;

    // pixel size of a page rendered at scale (pixels per point)
    static void pixelSize(PageSize s, double scale, int& w, int& h);

    // renders the part x, y, w, h (pixels at scale) of page i; the part is
    // clipped to the page
    bool render(int page, double scale, int x, int y, int w, int h, Bitmap& out, std::string& err);
    // the whole page
    bool render(int page, double scale, Bitmap& out, std::string& err);

private:
    Document() = default;
    poppler::page* page(int i);

    std::unique_ptr<poppler::document> mDoc;
    std::vector<std::unique_ptr<poppler::page>> mPages; // created on first use
    std::vector<PageSize> mSizes;
    std::unique_ptr<poppler::page_renderer> mRenderer;
};

}
