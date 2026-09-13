#include "texture/TextureExporter.hpp"

#include "texture/ImageWriter.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>

namespace bfrass::tex {

namespace {

std::string sanitizeFileName(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    return out.empty() ? std::string("unnamed") : out;
}

} // namespace

const char* extensionFor(ExportFormat format) {
    return format == ExportFormat::Dds ? "dds" : "png";
}

EncodedImage encodeTexture(const TextureResource& texture, const ExportOptions& options, uint32_t arrayIndex) {
    EncodedImage image;
    image.extension = extensionFor(options.format);
    if (options.format == ExportFormat::Png) {
        const auto rgba = decodeTextureSurface(texture, arrayIndex, 0, options.decode);
        image.bytes = encodePng(rgba, texture.width, texture.height);
        return image;
    }

    const uint32_t mips = std::max<uint32_t>(1, texture.availableMips());
    const uint32_t layers = std::max<uint32_t>(1, texture.arrayCount);
    DdsImage dds;
    dds.dxgiFormat = dxgiFormat(texture.format);
    dds.cubeMap = texture.platform == bfres::Platform::Switch ? (texture.nx.imageDimension == 3)
                                                              : (texture.gx2.dim == 3);
    const bool raw = dds.dxgiFormat != 0;
    if (!raw) {
        dds.dxgiFormat = texture.format.isSrgb() ? 29 : 28;
    }
    dds.layers.resize(layers);
    for (uint32_t layer = 0; layer < layers; ++layer) {
        for (uint32_t mip = 0; mip < mips; ++mip) {
            DdsSurface surface;
            surface.width = texture.mipWidth(mip);
            surface.height = texture.mipHeight(mip);
            surface.data = raw ? extractSurface(texture, layer, mip)
                               : decodeTextureSurface(texture, layer, mip, options.decode);
            dds.layers[layer].push_back(std::move(surface));
        }
    }
    image.bytes = encodeDds(dds);
    return image;
}

std::filesystem::path exportTexture(const TextureResource& texture, const std::filesystem::path& directory,
                                    const ExportOptions& options) {
    const std::string base = sanitizeFileName(texture.name);
    const auto primary = directory / (base + "." + extensionFor(options.format));
    writeFile(primary, encodeTexture(texture, options, 0).bytes);
    if (options.format == ExportFormat::Png) {
        for (uint32_t layer = 1; layer < texture.arrayCount; ++layer) {
            const auto path = directory / (base + "_layer" + std::to_string(layer) + ".png");
            writeFile(path, encodeTexture(texture, options, layer).bytes);
        }
    }
    return primary;
}

void parallelFor(size_t count, const std::function<void(size_t)>& fn) {
    const size_t workers = std::min<size_t>(count, std::max<unsigned>(1, std::thread::hardware_concurrency()));
    if (workers <= 1) {
        for (size_t i = 0; i < count; ++i) {
            fn(i);
        }
        return;
    }
    std::atomic<size_t> next{0};
    std::exception_ptr failure;
    std::mutex failureMutex;
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (size_t w = 0; w < workers; ++w) {
        threads.emplace_back([&] {
            for (size_t i = next++; i < count; i = next++) {
                try {
                    fn(i);
                } catch (...) {
                    std::lock_guard lock(failureMutex);
                    if (!failure) {
                        failure = std::current_exception();
                    }
                }
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

} // namespace bfrass::tex
