// make_icon.cpp — dependency-free .ICO generator for HddGauge.
// Renders a small gauge dial with 4x4 supersampling and writes a multi-size
// 32bpp BMP-based ICO. Build: g++ -O2 -o make_icon.exe make_icon.cpp
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

struct Px { double r, g, b, a; };

static double distSeg(double px, double py, double ax, double ay, double bx, double by)
{
    const double vx = bx - ax, vy = by - ay;
    const double wx = px - ax, wy = py - ay;
    const double len2 = vx * vx + vy * vy;
    double t = len2 > 0 ? (wx * vx + wy * vy) / len2 : 0.0;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    const double dx = px - (ax + t * vx), dy = py - (ay + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

// Normalized scene, x/y in [0,1]. Returns straight-alpha colour.
static Px shade(double x, double y)
{
    Px out = {0, 0, 0, 0};

    // --- rounded-square background plate ---
    const double m = 0.045, rad = 0.235;
    bool inside;
    {
        const double lo = m, hi = 1.0 - m;
        const double cx = x < lo + rad ? lo + rad : (x > hi - rad ? hi - rad : x);
        const double cy = y < lo + rad ? lo + rad : (y > hi - rad ? hi - rad : y);
        inside = (x >= lo && x <= hi && y >= lo && y <= hi) &&
                 ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad);
    }
    if (inside) {
        // subtle vertical gradient
        const double k = (y - m) / (1.0 - 2.0 * m);
        out.r = 20 - 8 * k; out.g = 25 - 10 * k; out.b = 33 - 13 * k; out.a = 1.0;
    }

    const double dx = x - 0.5, dy = y - 0.5;
    const double d = std::sqrt(dx * dx + dy * dy);
    const double ringR = 0.335, halfT = 0.042;
    const double prog = 0.62;                       // needle value

    // --- gauge ring: 270 deg sweep starting bottom-left (225 deg), clockwise ---
    if (std::fabs(d - ringR) <= halfT) {
        double ang = std::atan2(-dy, dx) * 180.0 / 3.14159265358979;  // -180..180
        double s = std::fmod(225.0 - ang, 360.0);
        if (s < 0) s += 360.0;
        if (s <= 270.0) {
            const double p = s / 270.0;
            if (p <= prog) { out.r = 34; out.g = 211; out.b = 238; }
            else           { out.r = 58; out.g = 66; out.b = 78; }
            out.a = 1.0;
        }
    }

    // --- needle ---
    {
        double ang = (225.0 - 270.0 * prog) * 3.14159265358979 / 180.0;
        const double ex = 0.5 + 0.255 * std::cos(ang), ey = 0.5 - 0.255 * std::sin(ang);
        if (distSeg(x, y, 0.5, 0.5, ex, ey) <= 0.021) {
            out.r = 232; out.g = 236; out.b = 242; out.a = 1.0;
        }
    }

    // --- hub ---
    if (d <= 0.055) {
        if (d <= 0.030) { out.r = 15; out.g = 19; out.b = 25; out.a = 1.0; }
        else            { out.r = 34; out.g = 211; out.b = 238; out.a = 1.0; }
    }

    return out;
}

static void render(int S, std::vector<uint8_t>& bgra)
{
    bgra.assign(size_t(S) * S * 4, 0);
    const int SS = 4;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            double ar = 0, ag = 0, ab = 0, aa = 0;
            for (int sy = 0; sy < SS; ++sy) {
                for (int sx = 0; sx < SS; ++sx) {
                    const double fx = (x + (sx + 0.5) / SS) / S;
                    const double fy = (y + (sy + 0.5) / SS) / S;
                    const Px c = shade(fx, fy);
                    ar += c.r * c.a; ag += c.g * c.a; ab += c.b * c.a; aa += c.a;
                }
            }
            const int n = SS * SS;
            const double a = aa / n;
            uint8_t R = 0, G = 0, B = 0, A = uint8_t(a * 255.0 + 0.5);
            if (aa > 0) {
                R = uint8_t(ar / aa + 0.5); G = uint8_t(ag / aa + 0.5); B = uint8_t(ab / aa + 0.5);
            }
            // BMP rows are bottom-up
            const size_t off = (size_t(S - 1 - y) * S + x) * 4;
            bgra[off + 0] = B; bgra[off + 1] = G; bgra[off + 2] = R; bgra[off + 3] = A;
        }
    }
}

static void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); }
static void put32(std::vector<uint8_t>& v, uint32_t x)
{ v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF); }

int main(int argc, char** argv)
{
    const char* outPath = argc > 1 ? argv[1] : "app.ico";
    const int sizes[] = {16, 24, 32, 48, 64};
    const int n = int(sizeof(sizes) / sizeof(sizes[0]));

    struct Img { int S; std::vector<uint8_t> pixels, mask, blob; };
    std::vector<Img> imgs(n);

    for (int i = 0; i < n; ++i) {
        Img& im = imgs[i];
        im.S = sizes[i];
        render(im.S, im.pixels);

        const int maskRow = ((im.S + 31) / 32) * 4;
        im.mask.assign(size_t(maskRow) * im.S, 0);

        const uint32_t pixBytes = uint32_t(im.pixels.size());
        const uint32_t maskBytes = uint32_t(im.mask.size());

        put32(im.blob, 40);                      // biSize
        put32(im.blob, uint32_t(im.S));          // biWidth
        put32(im.blob, uint32_t(im.S) * 2);      // biHeight (XOR + AND)
        put16(im.blob, 1);                       // biPlanes
        put16(im.blob, 32);                      // biBitCount
        put32(im.blob, 0);                       // biCompression = BI_RGB
        put32(im.blob, pixBytes + maskBytes);    // biSizeImage
        put32(im.blob, 0); put32(im.blob, 0); put32(im.blob, 0); put32(im.blob, 0);
        im.blob.insert(im.blob.end(), im.pixels.begin(), im.pixels.end());
        im.blob.insert(im.blob.end(), im.mask.begin(), im.mask.end());
    }

    std::vector<uint8_t> ico;
    put16(ico, 0); put16(ico, 1); put16(ico, uint16_t(n));   // ICONDIR

    uint32_t offset = 6 + 16 * uint32_t(n);
    for (int i = 0; i < n; ++i) {
        const int s = imgs[i].S;
        ico.push_back(uint8_t(s >= 256 ? 0 : s));            // width
        ico.push_back(uint8_t(s >= 256 ? 0 : s));            // height
        ico.push_back(0); ico.push_back(0);                  // colors, reserved
        put16(ico, 1);                                       // planes
        put16(ico, 32);                                      // bitcount
        put32(ico, uint32_t(imgs[i].blob.size()));
        put32(ico, offset);
        offset += uint32_t(imgs[i].blob.size());
    }
    for (int i = 0; i < n; ++i)
        ico.insert(ico.end(), imgs[i].blob.begin(), imgs[i].blob.end());

    FILE* f = std::fopen(outPath, "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", outPath); return 1; }
    std::fwrite(ico.data(), 1, ico.size(), f);
    std::fclose(f);
    std::printf("wrote %s (%u bytes, %d sizes)\n", outPath, unsigned(ico.size()), n);
    return 0;
}
