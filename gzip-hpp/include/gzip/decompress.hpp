#include <gzip/config.hpp>

// zlib
#include <zlib.h>

// std
#include <limits>
#include <string>

namespace gzip {

class Decompressor
{
    std::size_t max_;

  public:
    Decompressor(std::size_t max_bytes = 1000000000) // by default refuse operation if compressed data is > 1GB
        : max_(max_bytes)
    {
    }

    // Decompresses the zlib/gzip stream in data into output.
    // Returns true on success. On any failure (zlib init error, input or
    // output that would exceed max_, or a decompression error) returns
    // false, leaves output empty, and has released the zlib stream. Callers
    // cannot rely on exceptions: report failure via the return value.
    template <typename OutputType>
    bool decompress(OutputType& output,
                    const char* data,
                    std::size_t size) const
    {
        output.clear();

        z_stream inflate_s;

        inflate_s.zalloc = nullptr;
        inflate_s.zfree = nullptr;
        inflate_s.opaque = nullptr;
        inflate_s.avail_in = 0;
        inflate_s.next_in = nullptr;

        // The windowBits parameter is the base two logarithm of the window size (the size of the history buffer).
        // It should be in the range 8..15 for this version of the library.
        // Larger values of this parameter result in better compression at the expense of memory usage.
        // This range of values also changes the decoding type:
        //  -8 to -15 for raw deflate
        //  8 to 15 for zlib
        // (8 to 15) + 16 for gzip
        // (8 to 15) + 32 to automatically detect gzip/zlib header
        constexpr int window_bits = 15 + 32; // auto with windowbits of 15

        int init_ret = Z_OK;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
        init_ret = inflateInit2(&inflate_s, window_bits);
#pragma GCC diagnostic pop
        if (init_ret != Z_OK)
        {
            return false;
        }
        inflate_s.next_in = reinterpret_cast<z_const Bytef*>(data);

#ifdef DEBUG
        // Verify if size (long type) input will fit into unsigned int, type used for zlib's avail_in
        std::uint64_t size_64 = size * 2;
        if (size_64 > std::numeric_limits<unsigned int>::max())
        {
            inflateEnd(&inflate_s);
            return false;
        }
#endif
        if (size > max_ || (size * 2) > max_)
        {
            inflateEnd(&inflate_s);
            return false;
        }
        inflate_s.avail_in = static_cast<unsigned int>(size);
        std::size_t size_uncompressed = 0;
        do
        {
            std::size_t resize_to = size_uncompressed + 2 * size;
            if (resize_to > max_)
            {
                inflateEnd(&inflate_s);
                return false;
            }
            output.resize(resize_to);
            inflate_s.avail_out = static_cast<unsigned int>(2 * size);
            inflate_s.next_out = reinterpret_cast<Bytef*>(&output[0] + size_uncompressed);
            int ret = inflate(&inflate_s, Z_FINISH);
            if (ret != Z_STREAM_END && ret != Z_OK && ret != Z_BUF_ERROR)
            {
                inflateEnd(&inflate_s);
                output.clear();
                return false;
            }

            size_uncompressed += (2 * size - inflate_s.avail_out);
        } while (inflate_s.avail_out == 0);
        inflateEnd(&inflate_s);
        output.resize(size_uncompressed);
        return true;
    }
};

// Convenience wrapper around Decompressor. Returns the decompressed data on
// success and an empty string on failure; check emptiness to detect errors.
inline std::string decompress(const char* data, std::size_t size)
{
    Decompressor decomp;
    std::string output;
    decomp.decompress(output, data, size);
    return output;
}

} // namespace gzip
