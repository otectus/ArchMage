#include "ProbePng.h"

#include <png.h>

#include <cstdio>
#include <vector>

namespace archmage::probes {

std::string writePngRgba8(const std::filesystem::path &path, std::uint32_t width, std::uint32_t height,
                          std::span<const std::uint8_t> rgba) {
    if (rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        return "pixel buffer size does not match the dimensions";
    }
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return "cannot open " + path.string();
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png != nullptr ? png_create_info_struct(png) : nullptr;
    if (png == nullptr || info == nullptr) {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        return "libpng initialization failed";
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        return "libpng failed while writing";
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_set_sRGB_gAMA_and_cHRM(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
    png_write_info(png, info);
    for (std::uint32_t y = 0; y < height; ++y) {
        png_write_row(png, rgba.data() + static_cast<std::size_t>(y) * width * 4);
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    return {};
}

} // namespace archmage::probes
