#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

struct PageSpec {
    double w = 612, h = 792; // points, before /Rotate
    int rotate = 0;
    double r = 0.8, g = 0.2, b = 0.2; // fill, 0..1
    // the right half in another colour, when set
    bool split = false;
    double r2 = 0.2, g2 = 0.2, b2 = 0.8;
};

// writes a small valid PDF with the given pages, each filled with colour,
// and the title in its info dictionary. Test PDFs are generated, not
// stored in the repository.
inline void writePdf(const std::string& path, const std::string& title, const std::vector<PageSpec>& pages)
{
    auto num = [](double v) {
        char b[32];
        snprintf(b, sizeof b, "%.3f", v);
        return std::string(b);
    };
    std::vector<std::string> objs;
    // 1 catalog, 2 pages, 3 info, then (page, content) pairs
    std::string kids;
    for (size_t i = 0; i < pages.size(); i++)
        kids += std::to_string(4 + i * 2) + " 0 R ";
    objs.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objs.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size()) + " >>");
    // an empty title leaves the info dictionary empty (no title, no author)
    objs.push_back(title.empty() ? "<< >>" : "<< /Title (" + title + ") /Author (pdfview tests) >>");
    for (size_t i = 0; i < pages.size(); i++) {
        const PageSpec& p = pages[i];
        objs.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(p.w) + " " + num(p.h) + "] /Rotate " + std::to_string(p.rotate) + " /Contents " + std::to_string(5 + i * 2) + " 0 R >>");
        std::string c = num(p.r) + " " + num(p.g) + " " + num(p.b) + " rg 0 0 " + num(p.w) + " " + num(p.h) + " re f";
        if (p.split) c += "\n" + num(p.r2) + " " + num(p.g2) + " " + num(p.b2) + " rg " + num(p.w / 2) + " 0 " + num(p.w / 2) + " " + num(p.h) + " re f";
        objs.push_back("<< /Length " + std::to_string(c.size()) + " >>\nstream\n" + c + "\nendstream");
    }
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
