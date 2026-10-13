// RAW decode probe (plan S1-6.6, M0-E5; spec §18.1): opens a camera RAW or DNG file with LibRaw, reports
// the camera, orientation, black and white levels and CFA layout, demosaics into an ArchMage float32 tile
// map (sRGB-encoded, as the document profile expects) and exports it through the CPU compositor as PNG.
//
//   raw_probe <file> <output-directory>
//
// A file LibRaw cannot open yields an unsupported_input diagnostic and exit code 1.

#include "ProbePng.h"

#include "archmage/base/Sha256.h"
#include "archmage/commands/Command.h"
#include "archmage/render/CpuEvaluator.h"

#include <libraw/libraw.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::ostringstream report;

template <class... Args> void line(const char *format, Args... args) {
    char buffer[1024];
    std::snprintf(buffer, sizeof buffer, format, args...);
    report << buffer << '\n';
}

int finish(const std::filesystem::path &output, int code) {
    const std::string text = report.str();
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::ofstream(output / "raw-probe.txt") << text;
    return code;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: raw_probe <file> <output-directory>\n");
        return 2;
    }
    const std::filesystem::path input = argv[1];
    const std::filesystem::path output = argv[2];
    std::error_code ec;
    std::filesystem::create_directories(output, ec);
    {
        std::ifstream file(input, std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        line("file: %s, %zu bytes, sha256 %s", input.filename().c_str(), bytes.size(),
             archmage::base::Sha256::of(std::as_bytes(std::span(bytes))).hex().c_str());
    }
    line("LibRaw %s", LibRaw::version());

    LibRaw raw;
    raw.imgdata.rawparams.max_raw_memory_mb = 2048; // bounded allocation (spec §26.1)
    if (const int code = raw.open_file(input.c_str()); code != LIBRAW_SUCCESS) {
        line("error: unsupported_input/raw.unsupported: This file is not a camera RAW file ArchMage can read (%s)",
             libraw_strerror(code));
        return finish(output, 1);
    }
    const auto &id = raw.imgdata.idata;
    const auto &size = raw.imgdata.sizes;
    const auto &color = raw.imgdata.color;
    const auto &other = raw.imgdata.other;
    line("camera: %s %s (normalized %s %s)", id.make, id.model, id.normalized_make, id.normalized_model);
    line("raw: %u x %u; visible %u x %u at (%u, %u); output %u x %u", size.raw_width, size.raw_height, size.width,
         size.height, size.left_margin, size.top_margin, size.iwidth, size.iheight);
    static const char *flips[] = {"none", "?", "?", "180 degrees", "?", "90 degrees counter-clockwise",
                                  "90 degrees clockwise", "?"};
    line("orientation: LibRaw flip %d (%s); applied when the image is produced", size.flip,
         size.flip >= 0 && size.flip < 8 ? flips[size.flip] : "?");
    line("color filter: %s, %d colors, filters 0x%08x, DNG %s", id.cdesc, id.colors, id.filters,
         id.dng_version != 0 ? "yes" : "no");
    line("exposure: ISO %.0f, shutter %.6f s, aperture f/%.1f, focal length %.1f mm", other.iso_speed, other.shutter,
         other.aperture, other.focal_len);

    if (const int code = raw.unpack(); code != LIBRAW_SUCCESS) {
        line("error: damaged_input/raw.unpack: The RAW data could not be decoded (%s)", libraw_strerror(code));
        return finish(output, 1);
    }
    line("black level: %u (per channel %u %u %u %u); white level: %u", color.black, color.cblack[0], color.cblack[1],
         color.cblack[2], color.cblack[3], color.maximum);

    // Demosaic (LibRaw's default AHD), camera white balance, sRGB primaries and sRGB transfer, 16 bits.
    auto &params = raw.imgdata.params;
    params.use_camera_wb = 1;
    params.output_color = 1; // sRGB
    params.output_bps = 16;
    params.no_auto_bright = 1;
    if (const int code = raw.dcraw_process(); code != LIBRAW_SUCCESS) {
        line("error: damaged_input/raw.process: Demosaicing failed (%s)", libraw_strerror(code));
        return finish(output, 1);
    }
    int error = 0;
    libraw_processed_image_t *image = raw.dcraw_make_mem_image(&error);
    if (image == nullptr || image->type != LIBRAW_IMAGE_BITMAP || image->colors != 3 || image->bits != 16) {
        line("error: internal/raw.image: LibRaw produced no 16-bit RGB image (%s)", libraw_strerror(error));
        LibRaw::dcraw_clear_mem(image);
        return finish(output, 1);
    }
    const std::int64_t width = image->width;
    const std::int64_t height = image->height;
    line("demosaiced: %lld x %lld, 3 x 16 bits, camera white balance, sRGB", static_cast<long long>(width),
         static_cast<long long>(height));

    // Into a float32 document: one raster layer, straight samples in [0, 1].
    auto created = archmage::commands::createDocument(archmage::commands::CommandEnvelope::make(
        "archmage.document.create",
        {{"width", width}, {"height", height}, {"sampleType", "float32"}, {"background", "transparent"}}));
    if (!created) {
        line("error: %s", archmage::base::format(created.error()).c_str());
        LibRaw::dcraw_clear_mem(image);
        return finish(output, 2);
    }
    archmage::document::DocumentState doc = created->state;
    auto layer = std::make_shared<archmage::document::Layer>(*doc.layers[0]);
    layer->name = input.filename().string();
    auto content = std::make_shared<archmage::document::RasterContent>(*layer->content);
    const auto *samples = reinterpret_cast<const std::uint16_t *>(image->data);
    double minimum = 1.0, maximum = 0.0, sum = 0.0;
    for (std::int64_t ty = 0; ty * archmage::tiles::kTileSize < height; ++ty) {
        for (std::int64_t tx = 0; tx * archmage::tiles::kTileSize < width; ++tx) {
            archmage::tiles::TileBuffer buffer(archmage::tiles::SampleType::Float32, 4);
            for (int y = 0; y < archmage::tiles::kTileSize; ++y) {
                const std::int64_t py = ty * archmage::tiles::kTileSize + y;
                if (py >= height) {
                    break;
                }
                for (int x = 0; x < archmage::tiles::kTileSize; ++x) {
                    const std::int64_t px = tx * archmage::tiles::kTileSize + x;
                    if (px >= width) {
                        break;
                    }
                    const std::uint16_t *rgb = samples + (py * width + px) * 3;
                    for (std::size_t c = 0; c < 3; ++c) {
                        const float v = static_cast<float>(rgb[c]) / 65535.0f;
                        buffer.setSample(c, x, y, v);
                        minimum = std::min(minimum, static_cast<double>(v));
                        maximum = std::max(maximum, static_cast<double>(v));
                        sum += v;
                    }
                    buffer.setSample(3, x, y, 1.0f);
                }
            }
            content->tiles.set({tx, ty}, std::move(buffer).seal());
        }
    }
    LibRaw::dcraw_clear_mem(image);
    layer->content = std::move(content);
    doc.layers[0] = std::move(layer);
    line("float32 tile map: %zu tiles; samples in [%.4f, %.4f], mean %.4f", doc.layers[0]->content->tiles.tileCount(),
         minimum, maximum, sum / static_cast<double>(width * height * 3));

    archmage::render::CpuEvaluator evaluator;
    archmage::render::RenderRequest request;
    request.document = doc.id;
    request.region = doc.canvasRect();
    auto rendered = evaluator.render(doc, request);
    if (!rendered) {
        line("error: %s", archmage::base::format(rendered.error()).c_str());
        return finish(output, 2);
    }
    std::vector<std::uint8_t> rgba(rendered->pixels.size());
    std::memcpy(rgba.data(), rendered->pixels.data(), rgba.size());
    if (auto problem = archmage::probes::writePngRgba8(output / "raw-probe.png", static_cast<std::uint32_t>(width),
                                                       static_cast<std::uint32_t>(height), rgba);
        !problem.empty()) {
        line("error: PNG: %s", problem.c_str());
        return finish(output, 2);
    }
    line("exported raw-probe.png (%lld x %lld, 8-bit sRGB) through the CPU compositor", static_cast<long long>(width),
         static_cast<long long>(height));

    // A box-filtered preview of at most 800 pixels, small enough to keep as evidence.
    const std::int64_t factor = (std::max(width, height) + 799) / 800;
    const std::int64_t pw = width / factor;
    const std::int64_t ph = height / factor;
    std::vector<std::uint8_t> preview(static_cast<std::size_t>(pw * ph * 4));
    for (std::int64_t y = 0; y < ph; ++y) {
        for (std::int64_t x = 0; x < pw; ++x) {
            for (std::size_t c = 0; c < 4; ++c) {
                std::uint32_t total = 0;
                for (std::int64_t dy = 0; dy < factor; ++dy) {
                    for (std::int64_t dx = 0; dx < factor; ++dx) {
                        total += rgba[static_cast<std::size_t>(((y * factor + dy) * width + x * factor + dx) * 4) + c];
                    }
                }
                preview[static_cast<std::size_t>((y * pw + x) * 4) + c] =
                    static_cast<std::uint8_t>((total + static_cast<std::uint32_t>(factor * factor / 2)) /
                                              static_cast<std::uint32_t>(factor * factor));
            }
        }
    }
    if (auto problem = archmage::probes::writePngRgba8(output / "raw-probe-preview.png", static_cast<std::uint32_t>(pw),
                                                       static_cast<std::uint32_t>(ph), preview);
        !problem.empty()) {
        line("error: PNG: %s", problem.c_str());
        return finish(output, 2);
    }
    line("exported raw-probe-preview.png (%lld x %lld, %lld x %lld box filter)", static_cast<long long>(pw),
         static_cast<long long>(ph), static_cast<long long>(factor), static_cast<long long>(factor));
    return finish(output, 0);
}
