#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include <pdfview/Document.h>
#include <pdfview/Layout.h>
#include <pdfview/RenderWorker.h>

#include "fixtures.h"

using namespace pdfview;

namespace {

// B, G, R of a pixel
struct Px {
    int b, g, r;
};
Px at(const Bitmap& bm, int x, int y)
{
    const uint8_t* p = &bm.bgra[(static_cast<size_t>(y) * bm.w + x) * 4];
    return {p[0], p[1], p[2]};
}

std::string mixedPdf(const std::string& dir)
{
    std::string path = dir + "/mixed.pdf";
    test::PageSpec red;
    red.w = 300;
    red.h = 400;
    red.r = 1, red.g = 0, red.b = 0;
    red.split = true;
    red.r2 = 0, red.g2 = 0, red.b2 = 1;
    test::PageSpec turned = red;
    turned.rotate = 90;
    test::PageSpec wide;
    wide.w = 800;
    wide.h = 200;
    test::writePdf(path, "Mixed", {red, turned, wide});
    return path;
}

}

TEST_CASE("Document: sizes with /Rotate, metadata, region rendering")
{
    std::string dir = test::scratch("pdfview-doc");
    std::string err;
    auto doc = Document::open(mixedPdf(dir), err);
    REQUIRE_MESSAGE(doc, err);
    REQUIRE(doc->pages() == 3);
    CHECK(doc->pageSize(0).w == doctest::Approx(300));
    CHECK(doc->pageSize(0).h == doctest::Approx(400));
    // turned a quarter: displayed landscape
    CHECK(doc->pageSize(1).w == doctest::Approx(400));
    CHECK(doc->pageSize(1).h == doctest::Approx(300));
    CHECK(doc->title() == "Mixed");
    CHECK(doc->author() == "pdfview tests");

    Bitmap bm;
    REQUIRE_MESSAGE(doc->render(0, 1.0, bm, err), err);
    CHECK(bm.w == 300);
    CHECK(bm.h == 400);
    Px l = at(bm, 50, 200), r = at(bm, 250, 200);
    CHECK(l.r == 255);
    CHECK(l.b == 0);
    CHECK(r.b == 255);
    CHECK(r.r == 0);

    // a region of the right half at 2x is all blue, clipped at the page edge
    REQUIRE_MESSAGE(doc->render(0, 2.0, 400, 100, 512, 512, bm, err), err);
    CHECK(bm.w == 200);
    CHECK(bm.h == 512);
    CHECK(at(bm, 0, 0).b == 255);
    CHECK(at(bm, 199, 511).b == 255);

    // the turned page renders landscape
    REQUIRE_MESSAGE(doc->render(1, 1.0, bm, err), err);
    CHECK(bm.w == 400);
    CHECK(bm.h == 300);

    CHECK_FALSE(doc->render(5, 1.0, bm, err));
    CHECK_FALSE(doc->render(0, 1.0, 400, 0, 10, 10, bm, err)); // outside
}

TEST_CASE("Document: no title or author")
{
    std::string dir = test::scratch("pdfview-untitled");
    test::writePdf(dir + "/untitled.pdf", "");
    std::string err;
    auto doc = pdfview::Document::open(dir + "/untitled.pdf", err);
    REQUIRE_MESSAGE(doc, err);
    CHECK(doc->title().empty());
    CHECK(doc->author().empty());
}

TEST_CASE("Document: missing and broken files fail cleanly")
{
    std::string dir = test::scratch("pdfview-bad");
    std::string err;
    CHECK_FALSE(Document::open(dir + "/missing.pdf", err));
    CHECK_FALSE(err.empty());
    std::ofstream(dir + "/junk.pdf") << "%PDF-1.4\nthis is not a pdf";
    CHECK_FALSE(Document::open(dir + "/junk.pdf", err));
    std::ofstream(dir + "/empty.pdf");
    CHECK_FALSE(Document::open(dir + "/empty.pdf", err));
}

TEST_CASE("Layout and View geometry")
{
    Layout l;
    l.gap = 10;
    l.build({{100, 200}, {300, 100}, {100, 200}});
    CHECK(l.width == doctest::Approx(320));
    CHECK(l.top[0] == doctest::Approx(10));
    CHECK(l.top[1] == doctest::Approx(220));
    CHECK(l.top[2] == doctest::Approx(330));
    CHECK(l.height == doctest::Approx(540));
    CHECK(l.left(0) == doctest::Approx(110));
    CHECK(l.pageAt(0) == 0);
    CHECK(l.pageAt(250) == 1);
    CHECK(l.pageAt(10000) == 2);
    CHECK(l.range(215, 225) == std::pair{1, 1});
    CHECK(l.range(0, 1000) == std::pair{0, 2});
    CHECK(l.range(212, 216).first > l.range(212, 216).second); // in a gap

    View v;
    v.w = 160;
    v.h = 100;
    v.zoom = 1;
    // zooming keeps the point under the cursor in place
    v.y = 100;
    v.x = 50;
    v.clamp(l);
    double px = v.x + 40 / v.zoom, py = v.y + 30 / v.zoom;
    v.zoomAt(2, 40, 30, l);
    CHECK(v.x + 40 / v.zoom == doctest::Approx(px));
    CHECK(v.y + 30 / v.zoom == doctest::Approx(py));
    // a document narrower than the view is centred
    v.zoomAt(0.25, 0, 0, l);
    CHECK(v.x == doctest::Approx((l.width - v.w / v.zoom) / 2));
    // fit width of the wide page
    CHECK(v.fitWidth(l, 1) == doctest::Approx(160.0 / 320));
    v.zoom = 1;
    v.goToPage(2, l);
    CHECK(v.currentPage(l) == 2);
}

TEST_CASE("levels and visible tiles")
{
    CHECK(levelFor(1.0) == 0);
    CHECK(levelFor(2.0) == 8);
    CHECK(scaleOf(levelFor(1.3)) >= 1.3);
    CHECK(scaleOf(levelFor(1.3)) < 1.3 * 1.1);

    Layout l;
    l.build({{612, 792}, {612, 792}});
    View v;
    v.w = 800;
    v.h = 600;
    v.zoom = 1;
    v.clamp(l);
    auto tiles = visibleTiles(l, v, 0, 512);
    // the first page at 612 x 792 pixels: 2 x 2 tiles, the top ones visible
    REQUIRE_FALSE(tiles.empty());
    for (const auto& t : tiles) {
        CHECK(t.page == 0);
        CHECK(t.tx <= 1);
        CHECK(t.ty <= 1);
    }
    // a margin reaches the next page
    auto more = visibleTiles(l, v, 0, 512, 600);
    bool page1 = false;
    for (const auto& t : more)
        page1 |= t.page == 1;
    CHECK(page1);
    CHECK(more.size() > tiles.size());
}

TEST_CASE("RenderWorker renders what is wanted, in order")
{
    std::string dir = test::scratch("pdfview-worker");
    std::atomic<int> wakes{0};
    RenderWorker w(mixedPdf(dir), 256, [&] { wakes++; });
    auto waitFor = [&](auto pred) {
        for (int i = 0; i < 500 && !pred(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return pred();
    };
    REQUIRE(waitFor([&] { return w.state() != RenderWorker::State::Loading; }));
    REQUIRE(w.state() == RenderWorker::State::Ready);
    CHECK(w.pageSizes().size() == 3);
    CHECK(w.title() == "Mixed");

    std::vector<TileKey> keys{{0, TileKey::kPreview, 0, 0}, {0, 0, 1, 1}, {2, 8, 0, 0}, {9, 0, 0, 0}};
    w.want(keys);
    std::vector<RenderWorker::Result> got;
    REQUIRE(waitFor([&] {
        for (auto& r : w.take())
            got.push_back(std::move(r));
        return got.size() == keys.size();
    }));
    for (size_t i = 0; i < keys.size(); i++)
        CHECK(got[i].key == keys[i]);
    // preview: longest side kPreviewSide
    CHECK(std::max(got[0].bitmap.w, got[0].bitmap.h) == kPreviewSide);
    // tile (1,1) of a 300 x 400 page at 256 px: 44 x 144
    CHECK(got[1].bitmap.w == 44);
    CHECK(got[1].bitmap.h == 144);
    CHECK(got[2].bitmap.w == 256);
    // page 10 doesn't exist
    CHECK(got[3].bitmap.bgra.empty());
    CHECK_FALSE(got[3].error.empty());
    CHECK(wakes >= 5);
    CHECK_FALSE(w.busy());

    RenderWorker bad(dir + "/missing.pdf", 256);
    REQUIRE(waitFor([&] { return bad.state() != RenderWorker::State::Loading; }));
    CHECK(bad.state() == RenderWorker::State::Failed);
}
