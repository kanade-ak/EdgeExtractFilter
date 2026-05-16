#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "filter2.h"

namespace {

bool proc_video(FILTER_PROC_VIDEO* video);

auto threshold = FILTER_ITEM_TRACK(L"精度", 20.0, 1.0, 255.0, 1.0);

FILTER_ITEM_SELECT::ITEM range_items[] = {
    { L"3 x 3", 1 },
    { L"5 x 5", 2 },
    { L"7 x 7", 3 },
    { L"9 x 9", 4 },
    { L"11 x 11", 5 },
    { L"13 x 13", 6 },
    { L"15 x 15", 7 },
    { nullptr }
};
auto compare_range = FILTER_ITEM_SELECT(L"抽出比較範囲", 2, range_items);

FILTER_ITEM_SELECT::ITEM polarity_items[] = {
    { L"暗い線", 0 },
    { L"明るい線", 1 },
    { L"両方", 2 },
    { nullptr }
};
auto polarity = FILTER_ITEM_SELECT(L"抽出方向", 0, polarity_items);

auto thin_line = FILTER_ITEM_TRACK(L"細線化", 5.0, 0.0, 10.0, 0.1);
auto smoothing = FILTER_ITEM_CHECK(L"平滑化前処理", false);
auto prune_short_lines = FILTER_ITEM_CHECK(L"短線後処理", false);
auto noise_length = FILTER_ITEM_TRACK(L"ノイズ除去", 10.0, 0.0, 200.0, 1.0);
auto connect_range = FILTER_ITEM_TRACK(L"接続距離", 1.0, 1.0, 3.0, 1.0);

FILTER_ITEM_SELECT::ITEM output_items[] = {
    { L"白背景に線", 0 },
    { L"透明背景に線", 1 },
    { L"元画像に重ねる", 2 },
    { nullptr }
};
auto output_mode = FILTER_ITEM_SELECT(L"出力", 0, output_items);

auto line_color = FILTER_ITEM_COLOR(L"線色", 0x000000);
auto background_color = FILTER_ITEM_COLOR(L"背景色", 0xffffff);
auto opacity = FILTER_ITEM_TRACK(L"合成率", 1.0, 0.0, 1.0, 0.01);

void* items[] = {
    &threshold,
    &compare_range,
    &polarity,
    &thin_line,
    &smoothing,
    &prune_short_lines,
    &noise_length,
    &connect_range,
    &output_mode,
    &line_color,
    &background_color,
    &opacity,
    nullptr
};

FILTER_PLUGIN_TABLE filter_plugin_table = {
    FILTER_PLUGIN_TABLE::FLAG_VIDEO | FILTER_PLUGIN_TABLE::FLAG_FILTER,
    L"輪郭抽出",
    L"輪郭抽出",
    L"輪郭を抽出して線画風にします",
    items,
    proc_video,
    nullptr
};

inline int index_of(int x, int y, int width) {
    return y * width + x;
}

inline unsigned char clamp_u8(double value) {
    return static_cast<unsigned char>(std::clamp(value, 0.0, 255.0));
}

inline PIXEL_RGBA make_pixel(const FILTER_ITEM_COLOR::VALUE& color, unsigned char alpha = 255) {
    return PIXEL_RGBA{ color.r, color.g, color.b, alpha };
}

inline int luminance(const PIXEL_RGBA& p) {
    return static_cast<int>(p.b * 0.114478 + p.g * 0.586611 + p.r * 0.298912 + 0.5);
}

void smooth_luminance(std::vector<int>& gray, int width, int height) {
    if (width < 3 || height < 3) {
        return;
    }

    std::vector<int> src = gray;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            gray[index_of(x, y, width)] = (
                src[index_of(x - 1, y - 1, width)] +
                2 * src[index_of(x,     y - 1, width)] +
                src[index_of(x + 1, y - 1, width)] +
                2 * src[index_of(x - 1, y,     width)] +
                4 * src[index_of(x,     y,     width)] +
                2 * src[index_of(x + 1, y,     width)] +
                src[index_of(x - 1, y + 1, width)] +
                2 * src[index_of(x,     y + 1, width)] +
                src[index_of(x + 1, y + 1, width)] + 8
            ) / 16;
        }
    }
}

std::vector<unsigned char> detect_edges(const std::vector<PIXEL_RGBA>& src, int width, int height) {
    std::vector<int> gray(width * height);
    for (int i = 0; i < width * height; ++i) {
        gray[i] = luminance(src[i]);
    }
    if (smoothing.value) {
        smooth_luminance(gray, width, height);
    }

    std::vector<unsigned char> edge(width * height, 0);
    const int range = std::clamp(compare_range.value, 1, 7);
    const int th = static_cast<int>(std::lround(threshold.value));

    for (int y = range; y < height - range; ++y) {
        for (int x = range; x < width - range; ++x) {
            const int center = gray[index_of(x, y, width)];
            const int lap =
                gray[index_of(x - range, y - range, width)] +
                gray[index_of(x,         y - range, width)] +
                gray[index_of(x + range, y - range, width)] +
                gray[index_of(x - range, y,         width)] -
                8 * center +
                gray[index_of(x + range, y,         width)] +
                gray[index_of(x - range, y + range, width)] +
                gray[index_of(x,         y + range, width)] +
                gray[index_of(x + range, y + range, width)];

            const bool hit =
                (polarity.value == 0 && lap > th) ||
                (polarity.value == 1 && -lap > th) ||
                (polarity.value == 2 && std::abs(lap) > th);
            edge[index_of(x, y, width)] = hit ? 1 : 0;
        }
    }

    return edge;
}

int transition_count(const std::vector<unsigned char>& img, int x, int y, int width) {
    const unsigned char p2 = img[index_of(x,     y - 1, width)];
    const unsigned char p3 = img[index_of(x + 1, y - 1, width)];
    const unsigned char p4 = img[index_of(x + 1, y,     width)];
    const unsigned char p5 = img[index_of(x + 1, y + 1, width)];
    const unsigned char p6 = img[index_of(x,     y + 1, width)];
    const unsigned char p7 = img[index_of(x - 1, y + 1, width)];
    const unsigned char p8 = img[index_of(x - 1, y,     width)];
    const unsigned char p9 = img[index_of(x - 1, y - 1, width)];
    const unsigned char n[9] = { p2, p3, p4, p5, p6, p7, p8, p9, p2 };

    int count = 0;
    for (int i = 0; i < 8; ++i) {
        if (n[i] == 0 && n[i + 1] != 0) {
            ++count;
        }
    }
    return count;
}

int neighbor_count(const std::vector<unsigned char>& img, int x, int y, int width) {
    return
        img[index_of(x,     y - 1, width)] +
        img[index_of(x + 1, y - 1, width)] +
        img[index_of(x + 1, y,     width)] +
        img[index_of(x + 1, y + 1, width)] +
        img[index_of(x,     y + 1, width)] +
        img[index_of(x - 1, y + 1, width)] +
        img[index_of(x - 1, y,     width)] +
        img[index_of(x - 1, y - 1, width)];
}

std::vector<int> direct_neighbors(const std::vector<unsigned char>& img, int x, int y, int width, int height) {
    std::vector<int> neighbors;
    neighbors.reserve(8);

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }

            const int nx = x + dx;
            const int ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
                const int ni = index_of(nx, ny, width);
                if (img[ni] != 0) {
                    neighbors.push_back(ni);
                }
            }
        }
    }

    return neighbors;
}

void thin_edges(std::vector<unsigned char>& img, int width, int height) {
    if (width < 3 || height < 3) {
        return;
    }

    std::vector<int> erase;
    bool changed = true;
    while (changed) {
        changed = false;

        for (int step = 0; step < 2; ++step) {
            erase.clear();
            for (int y = 1; y < height - 1; ++y) {
                for (int x = 1; x < width - 1; ++x) {
                    const int idx = index_of(x, y, width);
                    if (img[idx] == 0) {
                        continue;
                    }

                    const int n = neighbor_count(img, x, y, width);
                    if (n < 2 || n > 6 || transition_count(img, x, y, width) != 1) {
                        continue;
                    }

                    const unsigned char p2 = img[index_of(x,     y - 1, width)];
                    const unsigned char p4 = img[index_of(x + 1, y,     width)];
                    const unsigned char p6 = img[index_of(x,     y + 1, width)];
                    const unsigned char p8 = img[index_of(x - 1, y,     width)];

                    const bool removable =
                        (step == 0 && p2 * p4 * p6 == 0 && p4 * p6 * p8 == 0) ||
                        (step == 1 && p2 * p4 * p8 == 0 && p2 * p6 * p8 == 0);
                    if (removable) {
                        erase.push_back(idx);
                    }
                }
            }

            if (!erase.empty()) {
                changed = true;
                for (const int idx : erase) {
                    img[idx] = 0;
                }
            }
        }
    }
}

void restore_from_raw_edges(
    std::vector<unsigned char>& edge,
    const std::vector<unsigned char>& raw,
    int width,
    int height,
    int passes) {
    if (passes <= 0) {
        return;
    }

    for (int pass = 0; pass < passes; ++pass) {
        std::vector<unsigned char> next = edge;
        bool changed = false;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int idx = index_of(x, y, width);
                if (raw[idx] == 0 || edge[idx] != 0) {
                    continue;
                }

                if (!direct_neighbors(edge, x, y, width, height).empty()) {
                    next[idx] = 1;
                    changed = true;
                }
            }
        }

        edge.swap(next);
        if (!changed) {
            break;
        }
    }
}

void trim_line_ends(std::vector<unsigned char>& edge, int width, int height, int passes) {
    if (passes <= 0) {
        return;
    }

    for (int pass = 0; pass < passes; ++pass) {
        std::vector<int> erase;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int idx = index_of(x, y, width);
                if (edge[idx] == 0) {
                    continue;
                }

                if (direct_neighbors(edge, x, y, width, height).size() <= 1) {
                    erase.push_back(idx);
                }
            }
        }

        if (erase.empty()) {
            break;
        }

        for (const int idx : erase) {
            edge[idx] = 0;
        }
    }
}

std::vector<double> apply_thin_line_amount(std::vector<unsigned char>& edge, int width, int height) {
    const double value = std::clamp(thin_line.value, 0.0, 10.0);
    std::vector<double> coverage(edge.size(), 0.0);

    if (value <= 0.0) {
        std::fill(edge.begin(), edge.end(), static_cast<unsigned char>(0));
        return coverage;
    }
    if (value >= 10.0) {
        for (std::size_t i = 0; i < edge.size(); ++i) {
            coverage[i] = edge[i] != 0 ? 1.0 : 0.0;
        }
        return coverage;
    }

    const std::vector<unsigned char> raw = edge;
    thin_edges(edge, width, height);

    if (value < 5.0) {
        const double amount = value / 5.0;
        for (std::size_t i = 0; i < edge.size(); ++i) {
            coverage[i] = edge[i] != 0 ? amount : 0.0;
        }
    } else if (value > 5.0) {
        const double amount = (value - 5.0) / 5.0;
        for (std::size_t i = 0; i < edge.size(); ++i) {
            if (edge[i] != 0) {
                coverage[i] = 1.0;
            } else if (raw[i] != 0) {
                edge[i] = 1;
                coverage[i] = amount;
            }
        }
    } else {
        for (std::size_t i = 0; i < edge.size(); ++i) {
            coverage[i] = edge[i] != 0 ? 1.0 : 0.0;
        }
    }

    return coverage;
}

void remove_short_line_artifacts(std::vector<unsigned char>& edge, int width, int height) {
    if (width < 3 || height < 3) {
        return;
    }

    const int max_len = std::max(2, static_cast<int>(std::lround(noise_length.value)));
    bool changed = true;
    int pass = 0;

    while (changed && pass++ < max_len) {
        changed = false;
        std::vector<unsigned char> erase(edge.size(), 0);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int start = index_of(x, y, width);
                if (edge[start] == 0 || erase[start] != 0) {
                    continue;
                }

                auto neighbors = direct_neighbors(edge, x, y, width, height);
                if (neighbors.size() > 1) {
                    continue;
                }

                int previous = -1;
                int current = start;
                std::vector<int> path;
                path.reserve(static_cast<std::size_t>(max_len) + 1);
                bool keep = false;

                while (true) {
                    path.push_back(current);
                    if (static_cast<int>(path.size()) > max_len) {
                        keep = true;
                        break;
                    }

                    const int cx = current % width;
                    const int cy = current / width;
                    auto next = direct_neighbors(edge, cx, cy, width, height);
                    next.erase(std::remove(next.begin(), next.end(), previous), next.end());

                    if (previous >= 0 && next.size() >= 2) {
                        path.pop_back();
                        break;
                    }
                    if (next.empty()) {
                        break;
                    }

                    previous = current;
                    current = next[0];

                    if (std::find(path.begin(), path.end(), current) != path.end()) {
                        keep = true;
                        break;
                    }
                }

                if (!keep && !path.empty()) {
                    for (const int pos : path) {
                        erase[pos] = 1;
                    }
                }
            }
        }

        for (std::size_t i = 0; i < edge.size(); ++i) {
            if (erase[i] != 0 && edge[i] != 0) {
                edge[i] = 0;
                changed = true;
            }
        }
    }
}

void remove_noise(std::vector<unsigned char>& edge, int width, int height) {
    const int max_len = static_cast<int>(std::lround(noise_length.value));
    if (max_len <= 0) {
        return;
    }

    const int cr = std::clamp(static_cast<int>(std::lround(connect_range.value)), 1, 3);
    std::vector<unsigned char> visited(width * height, 0);
    std::vector<int> queue;
    std::vector<int> component;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int start = index_of(x, y, width);
            if (edge[start] == 0 || visited[start] != 0) {
                continue;
            }

            queue.clear();
            component.clear();
            queue.push_back(start);
            visited[start] = 1;

            for (std::size_t qi = 0; qi < queue.size(); ++qi) {
                const int pos = queue[qi];
                component.push_back(pos);
                const int px = pos % width;
                const int py = pos / width;

                for (int dy = -cr; dy <= cr; ++dy) {
                    for (int dx = -cr; dx <= cr; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }

                        const int nx = px + dx;
                        const int ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }

                        const int ni = index_of(nx, ny, width);
                        if (edge[ni] != 0 && visited[ni] == 0) {
                            visited[ni] = 1;
                            queue.push_back(ni);
                        }
                    }
                }
            }

            if (static_cast<int>(component.size()) <= max_len) {
                for (const int pos : component) {
                    edge[pos] = 0;
                }
            }
        }
    }
}

PIXEL_RGBA blend(const PIXEL_RGBA& base, const PIXEL_RGBA& line, double amount) {
    return PIXEL_RGBA{
        clamp_u8(base.r * (1.0 - amount) + line.r * amount),
        clamp_u8(base.g * (1.0 - amount) + line.g * amount),
        clamp_u8(base.b * (1.0 - amount) + line.b * amount),
        base.a
    };
}

PIXEL_RGBA with_alpha(PIXEL_RGBA pixel, double alpha) {
    pixel.a = clamp_u8(pixel.a * alpha);
    return pixel;
}

bool proc_video(FILTER_PROC_VIDEO* video) {
    const int width = video->object->width;
    const int height = video->object->height;
    if (width <= 0 || height <= 0) {
        return true;
    }

    std::vector<PIXEL_RGBA> src(static_cast<std::size_t>(width) * height);
    video->get_image_data(src.data());

    auto edge = detect_edges(src, width, height);
    auto coverage = apply_thin_line_amount(edge, width, height);
    remove_noise(edge, width, height);
    if (prune_short_lines.value) {
        remove_short_line_artifacts(edge, width, height);
    }
    for (std::size_t i = 0; i < edge.size(); ++i) {
        if (edge[i] == 0) {
            coverage[i] = 0.0;
        }
    }

    std::vector<PIXEL_RGBA> dst(src.size());
    const PIXEL_RGBA line = make_pixel(line_color.value, 255);
    const PIXEL_RGBA background = make_pixel(background_color.value, 255);
    const double amount = std::clamp(opacity.value, 0.0, 1.0);

    for (std::size_t i = 0; i < dst.size(); ++i) {
        const double line_amount = coverage[i];
        if (output_mode.value == 1) {
            dst[i] = line_amount > 0.0 ? with_alpha(line, line_amount) : PIXEL_RGBA{ 0, 0, 0, 0 };
        } else if (output_mode.value == 2) {
            dst[i] = line_amount > 0.0 ? blend(src[i], line, amount * line_amount) : src[i];
        } else {
            dst[i] = line_amount > 0.0 ? blend(background, line, line_amount) : background;
        }
    }

    video->set_image_data(dst.data(), width, height);
    return true;
}

}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
    return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
}

EXTERN_C __declspec(dllexport) FILTER_PLUGIN_TABLE* GetFilterPluginTable() {
    return &filter_plugin_table;
}
