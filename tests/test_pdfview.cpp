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

namespace {

// three white pages: text and links on the first, a turned page with text
// and a link, an outline, labels i, ii, 1
std::string structuredPdf(const std::string& dir)
{
    test::DocSpec d;
    d.title = "Structured";
    test::PageSpec p;
    p.r = p.g = p.b = 1;
    p.text = {{72, 700, 12, "Hello world"}, {72, 680, 12, "second line world"}};
    p.links = {{72, 600, 200, 620, 1, 500, ""}, {72, 560, 200, 580, -1, -1, "https://example.com/"}};
    test::PageSpec turned = p;
    turned.rotate = 90;
    turned.text = {{72, 700, 12, "Turned"}};
    turned.links = {{72, 600, 200, 620, 0, -1, ""}};
    test::PageSpec plain;
    plain.r = plain.g = plain.b = 1;
    plain.text = {{100, 100, 20, "WORLD"}};
    d.pages = {p, turned, plain};
    d.outline = {{"Chapter 1", 0, 700, true, {{"Section 1.1", 1, -1, true, {}}}}, {"Chapter 2", 2, 300, false, {{"Hidden", 2, -1, true, {}}}}};
    d.pageLabels = "0 << /S /r >> 2 << /S /D >>";
    std::string path = dir + "/structured.pdf";
    test::writePdf(path, d);
    return path;
}

}

TEST_CASE("Document: labels, outline, links, text, find")
{
    std::string dir = test::scratch("pdfview-structure");
    std::string err;
    auto doc = Document::open(structuredPdf(dir), err);
    REQUIRE_MESSAGE(doc, err);
    REQUIRE(doc->pages() == 3);
    CHECK(doc->pageLabel(0) == "i");
    CHECK(doc->pageLabel(1) == "ii");
    CHECK(doc->pageLabel(2) == "1");

    auto outline = doc->outline();
    REQUIRE(outline.size() == 2);
    CHECK(outline[0].title == "Chapter 1");
    CHECK(outline[0].dest.page == 0);
    CHECK(outline[0].dest.y == doctest::Approx(92));
    CHECK(outline[0].open);
    REQUIRE(outline[0].children.size() == 1);
    CHECK(outline[0].children[0].title == "Section 1.1");
    CHECK(outline[0].children[0].dest.page == 1);
    CHECK(outline[0].children[0].dest.y == -1);
    CHECK(outline[1].dest.page == 2);
    CHECK(outline[1].dest.y == doctest::Approx(492));
    CHECK_FALSE(outline[1].open);
    CHECK(outline[1].children.size() == 1);

    auto links = doc->links(0);
    REQUIRE(links.size() == 2);
    CHECK(links[0].kind == Link::Kind::Goto);
    CHECK(links[0].dest.page == 1);
    // /XYZ on a turned page: its top
    CHECK(links[0].dest.y == -1);
    CHECK(links[0].rect.x0 == doctest::Approx(72));
    CHECK(links[0].rect.y0 == doctest::Approx(172));
    CHECK(links[0].rect.x1 == doctest::Approx(200));
    CHECK(links[0].rect.y1 == doctest::Approx(192));
    CHECK(links[1].kind == Link::Kind::Uri);
    CHECK(links[1].uri == "https://example.com/");

    auto text = doc->text(0);
    std::string all = toUtf8(text.text);
    CHECK(all.find("Hello world") != std::string::npos);
    CHECK(all.find("second line world") != std::string::npos);
    REQUIRE(text.boxes.size() == text.text.size());
    // "H": its baseline at y 92 from the top, starting at x 72
    CHECK(text.boxes[0].x0 == doctest::Approx(72).epsilon(0.02));
    CHECK(text.boxes[0].y0 < 92);
    CHECK(text.boxes[0].y1 > 88);
    CHECK(text.boxes[0].y1 < 96);

    // turned a quarter clockwise: PDF (x, y) shows at (y, x)
    auto turned = doc->text(1);
    REQUIRE_FALSE(turned.boxes.empty());
    CHECK(turned.boxes[0].x0 > 690);
    CHECK(turned.boxes[0].x0 < 705);
    CHECK(turned.boxes[0].y0 == doctest::Approx(72).epsilon(0.02));
    auto tl = doc->links(1);
    REQUIRE(tl.size() == 1);
    MESSAGE("turned link ", tl[0].rect.x0, " ", tl[0].rect.y0, " ", tl[0].rect.x1, " ", tl[0].rect.y1);
    CHECK(tl[0].rect.x0 == doctest::Approx(600));
    CHECK(tl[0].rect.x1 == doctest::Approx(620));
    CHECK(tl[0].rect.y0 == doctest::Approx(72));
    CHECK(tl[0].rect.y1 == doctest::Approx(200));

    auto found = doc->find(0, "world");
    REQUIRE(found.size() == 2);
    CHECK(found[0].y0 < found[1].y0); // page order
    CHECK(found[0].x0 > 100);
    CHECK(found[0].y0 < 92);
    CHECK(found[0].y1 > 88);
    CHECK(doc->find(2, "world").size() == 1);
    CHECK(doc->find(2, "world", FindCaseSensitive).empty());
    CHECK(doc->find(0, "wor", FindWholeWords).empty());
    CHECK(doc->find(1, "turned").size() == 1);
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
    CHECK(l.width == doctest::Approx(300)); // no margin around the document
    CHECK(l.top[0] == doctest::Approx(0));
    CHECK(l.top[1] == doctest::Approx(210));
    CHECK(l.top[2] == doctest::Approx(320));
    CHECK(l.height == doctest::Approx(520));
    CHECK(l.left(0) == doctest::Approx(100));
    CHECK(l.pageAt(0) == 0);
    CHECK(l.pageAt(250) == 1);
    CHECK(l.pageAt(10000) == 2);
    CHECK(l.range(215, 225) == std::pair{1, 1});
    CHECK(l.range(0, 1000) == std::pair{0, 2});
    CHECK(l.range(202, 206).first > l.range(202, 206).second); // in a gap

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
    CHECK(v.fitWidth(l, 1) == doctest::Approx(160.0 / 300));
    v.zoom = 1;
    v.goToPage(2, l);
    CHECK(v.currentPage(l) == 2);
}

TEST_CASE("fitted pages reach the view's edges")
{
    // mixed sizes: a narrow page, a wide one, a tall one
    std::vector<PageSize> pages{{100, 200}, {300, 100}, {100, 400}};
    Layout l;
    l.gap = 10;
    View v;
    v.w = 600;
    v.h = 400;

    // one column: Fit Width makes the page exactly as wide as the view, its
    // left edge at the view's left
    l.build(pages, {1, false, true});
    for (int p = 0; p < 3; p++) {
        v.zoom = v.fitWidth(l, p);
        CHECK(l.pages[static_cast<size_t>(p)].w * v.zoom == doctest::Approx(v.w));
        v.x = -1e6;
        v.goToPage(p, l);
        CHECK((l.left(p) - v.x) * v.zoom == doctest::Approx(0).epsilon(1e-9));
        CHECK(v.y == doctest::Approx(l.top[static_cast<size_t>(p)]));
    }
    // Fit Page: the page fills the width or the height, its top at the view's top
    for (int p = 0; p < 3; p++) {
        const PageSize& s = l.pages[static_cast<size_t>(p)];
        v.zoom = v.fitPage(l, p);
        bool fillsWidth = std::abs(s.w * v.zoom - v.w) < 1e-6, fillsHeight = std::abs(s.h * v.zoom - v.h) < 1e-6;
        CHECK((fillsWidth || fillsHeight));
        CHECK(s.w * v.zoom <= v.w + 1e-6);
        CHECK(s.h * v.zoom <= v.h + 1e-6);
        v.goToPage(p, l);
        CHECK(v.y == doctest::Approx(l.top[static_cast<size_t>(p)]));
    }
    // the first page's top and the last page's bottom are the document's ends
    v.zoom = v.fitWidth(l, 0);
    v.y = -1e6;
    v.clamp(l);
    CHECK(v.y == doctest::Approx(0));
    v.y = 1e6;
    v.clamp(l);
    CHECK((v.y + v.h / v.zoom) == doctest::Approx(l.top[2] + 400));

    // two columns: both columns fill the width (each as wide as the widest
    // page, so the zoom stays the same from row to row)
    l.build(pages, {2, false, true});
    v.zoom = v.fitWidth(l, 0);
    CHECK(l.width * v.zoom == doctest::Approx(v.w));
    // same-size pages: the pair fills it exactly
    l.build({{100, 200}, {100, 200}}, {2, false, true});
    v.zoom = v.fitWidth(l, 0);
    CHECK((l.left(1) + l.pages[1].w - l.left(0)) * v.zoom == doctest::Approx(v.w));
    // first alone: the pairs below still fill it
    l.build(pages, {2, true, true});
    v.zoom = v.fitWidth(l, 1);
    CHECK(l.width * v.zoom == doctest::Approx(v.w));

    // not continuous: a fitted page fills its row's band exactly, so nothing scrolls
    l.build(pages, {1, false, false});
    v.zoom = v.fitPage(l, 2);
    v.goToPage(2, l);
    auto [b0, b1] = v.band(l);
    CHECK((b1 - b0) * v.zoom <= v.h + 1e-6);
    CHECK(v.y == doctest::Approx(l.top[2]));
}

TEST_CASE("Layout without gaps: pages touch")
{
    std::vector<PageSize> pages{{100, 200}, {100, 100}, {100, 200}, {100, 200}};
    Layout l;
    l.gap = 0;
    l.build(pages, {1, false, true});
    CHECK(l.top[1] == doctest::Approx(200));
    CHECK(l.top[2] == doctest::Approx(300));
    CHECK(l.height == doctest::Approx(700));
    // a join belongs to the page below it
    CHECK(l.pageAt(50, 199.9) == 0);
    CHECK(l.pageAt(50, 200) == 1);
    CHECK(l.pageAt(50, 300) == 2);
    CHECK(l.range(200, 299).first == 1);
    CHECK(l.range(200, 299).second == 1);
    // facing and overview: columns meet
    l.build(pages, {2, false, true});
    CHECK(l.width == doctest::Approx(200));
    CHECK(l.left(1) == doctest::Approx(100));
    CHECK(l.pageAt(100.1, 50) == 1);
    CHECK(l.pageAt(99.9, 50) == 0);
    l.build(pages, {3, false, true});
    CHECK(l.width == doctest::Approx(300));
    CHECK(l.rowTop[1] == doctest::Approx(200));
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

    std::vector<TileKey> keys{{0, TileKey::kPreview, 0, 0}, {0, 0, 1, 1}, {2, 8, 0, 0},
                              {1, 8, TileKey::kWholePage, 0}, {9, 0, 0, 0}};
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
    // The rotated page is 400 x 300 points; the thumbnail covers it at 2x.
    CHECK(got[3].bitmap.w == 800);
    CHECK(got[3].bitmap.h == 600);
    CHECK(got[3].error.empty());
    // page 10 doesn't exist
    CHECK(got[4].bitmap.bgra.empty());
    CHECK_FALSE(got[4].error.empty());
    CHECK(wakes >= 6);
    CHECK_FALSE(w.busy());

    RenderWorker bad(dir + "/missing.pdf", 256);
    REQUIRE(waitFor([&] { return bad.state() != RenderWorker::State::Loading; }));
    CHECK(bad.state() == RenderWorker::State::Failed);
}

TEST_CASE("RenderWorker: page info, outline, labels and find")
{
    std::string dir = test::scratch("pdfview-worker-info");
    RenderWorker w(structuredPdf(dir), 256);
    auto waitFor = [&](auto pred) {
        for (int i = 0; i < 500 && !pred(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return pred();
    };
    // a search asked for while loading starts once loaded
    unsigned early = w.find("world", FindDefault, 0);
    REQUIRE(waitFor([&] { return w.state() != RenderWorker::State::Loading; }));
    REQUIRE(w.state() == RenderWorker::State::Ready);
    CHECK(w.pageLabels() == std::vector<std::string>{"i", "ii", "1"});
    REQUIRE(w.outline().size() == 2);

    std::vector<RenderWorker::FindResult> found;
    REQUIRE(waitFor([&] {
        for (auto& r : w.takeFind())
            found.push_back(std::move(r));
        return found.size() == 3;
    }));
    size_t matches = 0;
    for (const auto& r : found) {
        CHECK(r.search == early);
        matches += r.matches.size();
    }
    CHECK(matches == 3);

    // Replacing the thumbnail query leaves the document search's results intact.
    constexpr auto thumbnails = RenderWorker::FindChannel::Thumbnails;
    unsigned document = w.find("world", FindDefault, 0);
    unsigned obsolete = w.find("world", FindDefault, 0, thumbnails);
    unsigned latest = w.find("Turned", FindDefault, 0, thumbnails);
    CHECK(latest != obsolete);
    REQUIRE(waitFor([&] { return !w.busy(); }));
    auto documentResults = w.takeFind();
    auto thumbnailResults = w.takeFind(thumbnails);
    REQUIRE(documentResults.size() == 3);
    REQUIRE(thumbnailResults.size() == 3);
    const size_t documentMatchesByPage[] = {2, 0, 1};
    for (const auto& r : documentResults) {
        CHECK(r.search == document);
        REQUIRE((r.page >= 0 && r.page < 3));
        CHECK(r.matches.size() == documentMatchesByPage[r.page]);
    }
    for (const auto& r : thumbnailResults) {
        CHECK(r.search == latest);
        CHECK(r.matches.size() == (r.page == 1 ? 1 : 0));
    }

    // a new search from page 2 goes round: 2, 0, 1
    unsigned s = w.find("Turned", FindDefault, 2);
    found.clear();
    REQUIRE(waitFor([&] {
        for (auto& r : w.takeFind())
            found.push_back(std::move(r));
        return found.size() == 3;
    }));
    CHECK(found[0].search == s);
    CHECK(found[0].page == 2);
    CHECK(found[1].page == 0);
    CHECK(found[2].matches.size() == 1);
    CHECK(waitFor([&] { return !w.busy(); }));

    w.wantInfo({2, 0});
    std::vector<RenderWorker::PageInfo> info;
    REQUIRE(waitFor([&] {
        for (auto& r : w.takeInfo())
            info.push_back(std::move(r));
        return info.size() == 2;
    }));
    CHECK(info[0].page == 2);
    CHECK(info[1].page == 0);
    CHECK(info[1].links.size() == 2);
    CHECK(toUtf8(info[0].text.text).find("WORLD") != std::string::npos);
    CHECK(waitFor([&] { return !w.busy(); }));
}

TEST_CASE("Layout: view modes")
{
    std::vector<PageSize> five(5, PageSize{100, 200});
    five[3] = {50, 100};
    Layout l;
    l.gap = 10;

    // facing: pairs meeting in the middle
    l.build(five, {2, false, true});
    CHECK(l.rows() == 3);
    CHECK(l.width == doctest::Approx(210));
    CHECK(l.left(0) == doctest::Approx(0));
    CHECK(l.left(1) == doctest::Approx(110));
    CHECK(l.left(2) == doctest::Approx(0));
    CHECK(l.left(3) == doctest::Approx(110));
    CHECK(l.top[3] == doctest::Approx(l.rowTop[1] + 50)); // centred in its row
    CHECK(l.left(4) == doctest::Approx(0));
    CHECK(l.rowOf[4] == 2);
    CHECK(l.range(0, 50) == std::pair{0, 1});
    CHECK(l.range(0, 300) == std::pair{0, 3});
    CHECK(l.pageAt(150, 100) == 1);
    CHECK(l.pageAt(5, 100) == 0);
    CHECK(l.pageAt(150, 450) == 4); // alone in its row
    CHECK(l.height == doctest::Approx(3 * 200 + 2 * 10));

    // the first page alone, centred
    l.build(five, {2, true, true});
    CHECK(l.rows() == 3);
    CHECK(l.left(0) == doctest::Approx(55));
    CHECK(l.rowFirst == std::vector<int>{0, 1, 3});
    CHECK(l.left(1) == doctest::Approx(0));
    CHECK(l.left(3) == doctest::Approx(100 - 50)); // left of the pair: against the middle

    // overview: 3 columns, centred in each
    l.build(five, {3, false, true});
    CHECK(l.rows() == 2);
    CHECK(l.left(2) == doctest::Approx(220));
    CHECK(l.left(3) == doctest::Approx(25));
    CHECK(l.rowWidth(0) == doctest::Approx(l.width));

    // fits: the rows' width
    View v;
    v.w = 460;
    v.h = 1000;
    CHECK(v.fitWidth(l, 0) == doctest::Approx(460 / l.width));

    // not continuous: one row at a time, flipping between them
    l.build(five, {1, false, false});
    v.w = 200;
    v.h = 100;
    v.zoom = 1;
    v.goToPage(1, l);
    CHECK(v.row == 1);
    CHECK(v.currentPage(l) == 1);
    v.y = 1e6;
    v.clamp(l);
    CHECK(v.y + v.h == doctest::Approx(l.rowTop[1] + 200)); // the row's bottom
    CHECK(v.pages(l, 1000) == std::pair{1, 1});
    REQUIRE(v.flip(1, l));
    CHECK(v.row == 2);
    CHECK(v.y == doctest::Approx(l.rowTop[2]));
    REQUIRE(v.flip(-1, l));
    CHECK(v.row == 1);
    CHECK(v.y + v.h == doctest::Approx(l.rowTop[1] + 200)); // back at its bottom
    v.goToPage(0, l);
    CHECK_FALSE(v.flip(-1, l));
    // a row smaller than the view is centred in it
    v.zoom = 0.25;
    v.clamp(l);
    CHECK(v.y + v.h / v.zoom / 2 == doctest::Approx(l.rowTop[0] + 100));
    CHECK(v.pages(l, 1000) == std::pair{0, 0});

    // goToPage with a position on the page
    l.build(five, {1, false, true});
    v.zoom = 1;
    v.goToPage(2, l, 50);
    CHECK(v.y == doctest::Approx(l.top[2] + 50));
}
