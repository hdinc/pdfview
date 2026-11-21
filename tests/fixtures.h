#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace test {

// a fresh scratch directory under PDFVIEW_TEST_TMP (set by ctest)
inline std::string scratch(const std::string& name)
{
    const char* base = getenv("PDFVIEW_TEST_TMP");
    std::string d = std::string(base ? base : std::filesystem::temp_directory_path().string()) + "/pdfview-" + name;
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
}

// text drawn in Helvetica, black; x, y of the baseline's start in PDF
// space (from the page's bottom left)
struct TextSpec {
    double x = 72, y = 700, size = 12;
    std::string s;
};

// a link annotation, rect in PDF space; to a page (0 based, top in PDF
// space or -1) or to a URI
struct LinkSpec {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int page = -1;
    double top = -1;
    std::string uri;
};

struct PageSpec {
    double w = 612, h = 792; // points, before /Rotate
    int rotate = 0;
    double r = 0.8, g = 0.2, b = 0.2; // fill, 0..1
    // the right half in another colour, when set
    bool split = false;
    double r2 = 0.2, g2 = 0.2, b2 = 0.8;
    std::vector<TextSpec> text;
    std::vector<LinkSpec> links;
};

struct OutlineSpec {
    std::string title;
    int page = 0;
    double top = -1; // PDF space; -1: /Fit
    bool open = true;
    std::vector<OutlineSpec> children;
};

struct DocSpec {
    std::string title;
    std::vector<PageSpec> pages;
    std::vector<OutlineSpec> outline;
    // the /Nums array of /PageLabels, e.g. "0 << /S /r >> 2 << /S /D >>"
    std::string pageLabels;
};

// writes a small valid PDF. Test PDFs are generated, not stored in the
// repository.
inline void writePdf(const std::string& path, const DocSpec& doc)
{
    auto num = [](double v) {
        char b[32];
        snprintf(b, sizeof b, "%.3f", v);
        return std::string(b);
    };
    auto ref = [](size_t n) { return std::to_string(n) + " 0 R"; };
    const auto& pages = doc.pages;
    size_t n = pages.size();
    // 1 catalog, 2 pages, 3 info, (page, content) pairs, the font, then
    // annotations and outline items
    std::vector<std::string> objs(4 + 2 * n);
    auto pageObj = [&](int i) { return 4 + static_cast<size_t>(i) * 2; };
    size_t font = 4 + 2 * n;
    auto add = [&](std::string o) {
        objs.push_back(std::move(o));
        return objs.size();
    };
    auto dest = [&](int page, double top) {
        if (top < 0) return "[" + ref(pageObj(page)) + " /Fit]";
        return "[" + ref(pageObj(page)) + " /XYZ null " + num(top) + " null]";
    };
    std::string kids;
    for (size_t i = 0; i < n; i++)
        kids += ref(pageObj(static_cast<int>(i))) + " ";
    objs[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(n) + " >>";
    // an empty title leaves the info dictionary empty (no title, no author)
    objs[2] = doc.title.empty() ? "<< >>" : "<< /Title (" + doc.title + ") /Author (pdfview tests) >>";
    objs[font - 1] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    for (size_t i = 0; i < n; i++) {
        const PageSpec& p = pages[i];
        std::string annots;
        for (const LinkSpec& l : p.links) {
            std::string a = "<< /Type /Annot /Subtype /Link /Border [0 0 0] /Rect [" + num(l.x0) + " " + num(l.y0) + " " + num(l.x1) + " " + num(l.y1) + "] ";
            a += l.uri.empty() ? "/Dest " + dest(l.page, l.top) : "/A << /S /URI /URI (" + l.uri + ") >>";
            annots += ref(add(a + " >>")) + " ";
        }
        objs[pageObj(static_cast<int>(i)) - 1] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(p.w) + " " + num(p.h) + "] /Rotate " + std::to_string(p.rotate) + " /Resources << /Font << /F1 " + ref(font) + " >> >> /Contents " + ref(pageObj(static_cast<int>(i)) + 1) + (annots.empty() ? "" : " /Annots [" + annots + "]") + " >>";
        std::string c = num(p.r) + " " + num(p.g) + " " + num(p.b) + " rg 0 0 " + num(p.w) + " " + num(p.h) + " re f";
        if (p.split) c += "\n" + num(p.r2) + " " + num(p.g2) + " " + num(p.b2) + " rg " + num(p.w / 2) + " 0 " + num(p.w / 2) + " " + num(p.h) + " re f";
        for (const TextSpec& t : p.text)
            c += "\n0 g BT /F1 " + num(t.size) + " Tf " + num(t.x) + " " + num(t.y) + " Td (" + t.s + ") Tj ET";
        objs[pageObj(static_cast<int>(i))] = "<< /Length " + std::to_string(c.size()) + " >>\nstream\n" + c + "\nendstream";
    }
    std::string catalog = "<< /Type /Catalog /Pages 2 0 R";
    if (!doc.pageLabels.empty()) catalog += " /PageLabels << /Nums [" + doc.pageLabels + "] >>";
    if (!doc.outline.empty()) {
        // number the items first: they point at each other
        size_t root = add("");
        std::function<std::pair<size_t, size_t>(const std::vector<OutlineSpec>&, size_t)> items = [&](const std::vector<OutlineSpec>& list, size_t parent) {
            std::vector<size_t> ids;
            for (size_t k = 0; k < list.size(); k++)
                ids.push_back(add(""));
            for (size_t k = 0; k < list.size(); k++) {
                const OutlineSpec& o = list[k];
                std::string d = "<< /Title (" + o.title + ") /Parent " + ref(parent) + " /Dest " + dest(o.page, o.top);
                if (k > 0) d += " /Prev " + ref(ids[k - 1]);
                if (k + 1 < list.size()) d += " /Next " + ref(ids[k + 1]);
                if (!o.children.empty()) {
                    auto [first, last] = items(o.children, ids[k]);
                    int count = static_cast<int>(o.children.size());
                    d += " /First " + ref(first) + " /Last " + ref(last) + " /Count " + std::to_string(o.open ? count : -count);
                }
                objs[ids[k] - 1] = d + " >>";
            }
            return std::pair{ids.front(), ids.back()};
        };
        auto [first, last] = items(doc.outline, root);
        objs[root - 1] = "<< /Type /Outlines /First " + ref(first) + " /Last " + ref(last) + " /Count " + std::to_string(doc.outline.size()) + " >>";
        catalog += " /Outlines " + ref(root);
    }
    objs[0] = catalog + " >>";

    std::string out = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objs.size(); i++) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
    }
    size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objs.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t o : offsets) {
        char line[21];
        snprintf(line, sizeof line, "%010zu 00000 n \n", o);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(objs.size() + 1) + " /Root 1 0 R /Info 3 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    std::ofstream(path, std::ios::binary) << out;
}

inline void writePdf(const std::string& path, const std::string& title, const std::vector<PageSpec>& pages)
{
    DocSpec d;
    d.title = title;
    d.pages = pages;
    writePdf(path, d);
}

// `pages` equal pages of w x h points in one colour
inline void writePdf(const std::string& path, const std::string& title, int pages = 1, double w = 612, double h = 792, double r = 0.8, double g = 0.2, double b = 0.2)
{
    PageSpec p;
    p.w = w;
    p.h = h;
    p.r = r;
    p.g = g;
    p.b = b;
    writePdf(path, title, std::vector<PageSpec>(static_cast<size_t>(pages), p));
}

}
