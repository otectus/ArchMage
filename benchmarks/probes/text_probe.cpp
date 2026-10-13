// Text shaping probe (plan S1-6.7, M0-E5 text part; spec §17.1): HarfBuzz shapes an Arabic and a Devanagari
// sample with Noto Sans; FreeType rasterizes the glyphs into ArchMage tiles; the probe logs glyph counts and
// the cluster map, and renders each sample shaped and, for contrast, naively (one glyph per code point, no
// shaping) through the CPU compositor.
//
//   text_probe <output-directory> [arabic-font] [devanagari-font]

#include "ProbePng.h"

#include "archmage/commands/Command.h"
#include "archmage/render/CpuEvaluator.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::ostringstream report;

[[gnu::format(printf, 1, 2)]] void line(const char *format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    report << buffer << '\n';
}

constexpr int kPixelSize = 48;
constexpr std::int64_t kCanvasWidth = 640;
constexpr std::int64_t kRowHeight = 80;

struct Sample {
    const char *label;
    const char *text;
    std::filesystem::path font;
};

struct Placed {
    unsigned glyph;
    double x; // pen position plus offset, in pixels
    double y;
};

// Alpha coverage of a text line, one float per pixel of a kCanvasWidth × kRowHeight row.
void draw(FT_Face face, const std::vector<Placed> &glyphs, std::vector<float> &coverage, std::int64_t rowTop,
          double startX) {
    const double baseline = static_cast<double>(rowTop) + kRowHeight * 0.68;
    for (const Placed &placed : glyphs) {
        if (FT_Load_Glyph(face, placed.glyph, FT_LOAD_RENDER) != 0) {
            continue;
        }
        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap &bitmap = slot->bitmap;
        const auto originX = static_cast<std::int64_t>(std::lround(startX + placed.x)) + slot->bitmap_left;
        const auto originY = static_cast<std::int64_t>(std::lround(baseline - placed.y)) - slot->bitmap_top;
        for (unsigned row = 0; row < bitmap.rows; ++row) {
            for (unsigned col = 0; col < bitmap.width; ++col) {
                const std::int64_t x = originX + col;
                const std::int64_t y = originY + row;
                if (x < 0 || x >= kCanvasWidth || y < rowTop || y >= rowTop + kRowHeight) {
                    continue;
                }
                const float value = bitmap.buffer[row * static_cast<unsigned>(bitmap.pitch) + col] / 255.0f;
                float &target = coverage[static_cast<std::size_t>(y * kCanvasWidth + x)];
                target = std::max(target, value);
            }
        }
    }
}

std::vector<std::uint32_t> codepoints(const char *utf8) {
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, utf8, -1, 0, -1);
    unsigned count = 0;
    const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &count);
    std::vector<std::uint32_t> out;
    for (unsigned i = 0; i < count; ++i) {
        out.push_back(info[i].codepoint);
    }
    hb_buffer_destroy(buffer);
    return out;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: text_probe <output-directory> [arabic-font] [devanagari-font]\n");
        return 2;
    }
    const std::filesystem::path output = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(output, ec);
    const std::vector<Sample> samples{
        {"Arabic", "\xD8\xA7\xD9\x84\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 \xD8\xB9\xD9\x84\xD9\x8A\xD9\x83\xD9\x85",
         argc > 2 ? argv[2] : "/usr/share/fonts/noto/NotoSansArabic-Regular.ttf"},
        {"Devanagari",
         "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87 "
         "\xE0\xA4\xA6\xE0\xA5\x81\xE0\xA4\xA8\xE0\xA4\xBF\xE0\xA4\xAF\xE0\xA4\xBE",
         argc > 3 ? argv[3] : "/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf"},
    };
    int major = 0, minor = 0, patch = 0;
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        std::fprintf(stderr, "text_probe: FreeType initialization failed\n");
        return 2;
    }
    FT_Library_Version(library, &major, &minor, &patch);
    line("HarfBuzz %s, FreeType %d.%d.%d, %d px", hb_version_string(), major, minor, patch, kPixelSize);

    // Four rows: each sample shaped, then naive.
    std::vector<float> coverage(static_cast<std::size_t>(kCanvasWidth * kRowHeight * 4), 0.0f);
    std::int64_t row = 0;
    int status = 0;
    for (const Sample &sample : samples) {
        FT_Face face = nullptr;
        if (FT_New_Face(library, sample.font.c_str(), 0, &face) != 0) {
            line("%s: cannot open font %s", sample.label, sample.font.c_str());
            status = 1;
            row += 2;
            continue;
        }
        FT_Set_Pixel_Sizes(face, 0, kPixelSize);
        line("%s: font %s (%s %s)", sample.label, sample.font.filename().c_str(), face->family_name, face->style_name);
        const std::vector<std::uint32_t> input = codepoints(sample.text);
        std::string points;
        for (const std::uint32_t c : input) {
            char hex[16];
            std::snprintf(hex, sizeof hex, "%sU+%04X", points.empty() ? "" : " ", c);
            points += hex;
        }
        line("  input: %zu code points: %s", input.size(), points.c_str());

        hb_font_t *font = hb_ft_font_create_referenced(face);
        hb_buffer_t *buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, sample.text, -1, 0, -1);
        hb_buffer_guess_segment_properties(buffer);
        const hb_direction_t direction = hb_buffer_get_direction(buffer);
        char script[5] = {};
        hb_tag_to_string(hb_script_to_iso15924_tag(hb_buffer_get_script(buffer)), script);
        hb_shape(font, buffer, nullptr, 0);
        unsigned count = 0;
        const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &count);
        const hb_glyph_position_t *position = hb_buffer_get_glyph_positions(buffer, &count);
        line("  shaped: script %s, direction %s, %u glyphs", script, hb_direction_to_string(direction), count);
        std::vector<Placed> shaped;
        double pen = 0.0;
        std::string clusters;
        for (unsigned i = 0; i < count; ++i) {
            char name[64] = {};
            hb_font_get_glyph_name(font, info[i].codepoint, name, sizeof name);
            line("    glyph %2u: id %4u %-16s cluster %2u, advance %6.2f, offset (%5.2f, %5.2f)", i, info[i].codepoint,
                 name[0] != '\0' ? name : "-", info[i].cluster, position[i].x_advance / 64.0,
                 position[i].x_offset / 64.0, position[i].y_offset / 64.0);
            shaped.push_back({info[i].codepoint, pen + position[i].x_offset / 64.0, position[i].y_offset / 64.0});
            pen += position[i].x_advance / 64.0;
            clusters += (clusters.empty() ? "" : " ") + std::to_string(info[i].cluster);
        }
        line("  cluster map (glyph order): %s", clusters.c_str());
        line("  width %.1f px", pen);
        // Cluster byte offsets to code points: a cluster spanning several code points is a ligature or conjunct.
        std::vector<unsigned> starts;
        for (unsigned i = 0; i < count; ++i) {
            starts.push_back(info[i].cluster);
        }
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        line("  %zu clusters for %zu code points; %u glyphs", starts.size(), input.size(), count);
        draw(face, shaped, coverage, row * kRowHeight, 16.0);
        ++row;

        // Naive: each code point's nominal glyph, left to right, no shaping.
        std::vector<Placed> naive;
        double naivePen = 0.0;
        for (const std::uint32_t c : input) {
            const FT_UInt glyph = FT_Get_Char_Index(face, c);
            naive.push_back({glyph, naivePen, 0.0});
            if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) == 0) {
                naivePen += static_cast<double>(face->glyph->advance.x) / 64.0;
            }
        }
        line("  naive: %zu glyphs, one per code point, left to right", naive.size());
        draw(face, naive, coverage, row * kRowHeight, 16.0);
        ++row;
        hb_buffer_destroy(buffer);
        hb_font_destroy(font); // drops HarfBuzz's reference to the face
        FT_Done_Face(face);    // and ours
    }
    FT_Done_FreeType(library);

    // Black text over a white background through the CPU compositor.
    auto created = archmage::commands::createDocument(archmage::commands::CommandEnvelope::make(
        "archmage.document.create", {{"width", kCanvasWidth}, {"height", kRowHeight * 4}, {"background", "white"}}));
    if (!created) {
        std::fprintf(stderr, "text_probe: %s\n", archmage::base::format(created.error()).c_str());
        return 2;
    }
    archmage::document::DocumentState doc = created->state;
    auto text = std::make_shared<archmage::document::Layer>(*archmage::document::makeRasterLayer(doc, "Text"));
    auto content = std::make_shared<archmage::document::RasterContent>(*text->content);
    const std::int64_t height = kRowHeight * 4;
    for (std::int64_t ty = 0; ty * archmage::tiles::kTileSize < height; ++ty) {
        for (std::int64_t tx = 0; tx * archmage::tiles::kTileSize < kCanvasWidth; ++tx) {
            archmage::tiles::TileBuffer buffer(archmage::tiles::SampleType::UInt8, 4);
            for (int y = 0; y < archmage::tiles::kTileSize; ++y) {
                for (int x = 0; x < archmage::tiles::kTileSize; ++x) {
                    const std::int64_t px = tx * archmage::tiles::kTileSize + x;
                    const std::int64_t py = ty * archmage::tiles::kTileSize + y;
                    if (px < kCanvasWidth && py < height) {
                        buffer.setSample(3, x, y, coverage[static_cast<std::size_t>(py * kCanvasWidth + px)]);
                    }
                }
            }
            content->tiles.set({tx, ty}, std::move(buffer).seal());
        }
    }
    text->content = std::move(content);
    doc.layers.push_back(std::move(text));
    archmage::render::CpuEvaluator evaluator;
    archmage::render::RenderRequest request;
    request.document = doc.id;
    request.region = doc.canvasRect();
    auto rendered = evaluator.render(doc, request);
    if (!rendered) {
        std::fprintf(stderr, "text_probe: %s\n", archmage::base::format(rendered.error()).c_str());
        return 2;
    }
    std::vector<std::uint8_t> rgba(rendered->pixels.size());
    std::memcpy(rgba.data(), rendered->pixels.data(), rgba.size());
    if (auto problem = archmage::probes::writePngRgba8(output / "text-probe.png", kCanvasWidth,
                                                       static_cast<std::uint32_t>(height), rgba);
        !problem.empty()) {
        line("PNG: %s", problem.c_str());
        status = 2;
    }
    line("rows of text-probe.png: Arabic shaped, Arabic naive, Devanagari shaped, Devanagari naive");
    const std::string textReport = report.str();
    std::fwrite(textReport.data(), 1, textReport.size(), stdout);
    std::ofstream(output / "text-probe.txt") << textReport;
    return status;
}
