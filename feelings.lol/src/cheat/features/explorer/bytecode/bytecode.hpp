#ifndef EXTERNAL_LUA_BYTECODE_HPP
#define EXTERNAL_LUA_BYTECODE_HPP

#include <vector>
#include <string>

#include <zstd.h>
#define XXH_INLINE_ALL
#include <xxhash.h>
#undef XXH_INLINE_ALL


class Bytecode {
private:
    static constexpr uint8_t BYTECODE_SIGNATURE[4] = { 'R', 'S', 'B', '1' };
    static constexpr uint8_t BYTECODE_HASH_MULTIPLIER = 41;
    static constexpr uint32_t BYTECODE_HASH_SEED = 42u;

    static constexpr uint32_t MAGIC_A = 0x4C464F52;
    static constexpr uint32_t MAGIC_B = 0x946AC432;
    static constexpr uint8_t  KEY_BYTES[4] = { 0x52, 0x4F, 0x46, 0x4C };

    static inline uint8_t rotl8(uint8_t value, int shift) {
        shift &= 7;
        return (value << shift) | (value >> (8 - shift));
    }

public:

    static std::string compress(const std::string& BytecodeData)
    {
        const auto MaxSize = ZSTD_compressBound(BytecodeData.size());
        auto Buffer = std::vector<char>(MaxSize + 8);

        memcpy(&Buffer[0], BYTECODE_SIGNATURE, 4);

        const auto Size = static_cast<uint32_t>(BytecodeData.size());
        memcpy(&Buffer[4], &Size, sizeof(Size));

        const auto compressed_size = ZSTD_compress(&Buffer[8], MaxSize, BytecodeData.data(), BytecodeData.size(), ZSTD_maxCLevel());
        if (ZSTD_isError(compressed_size))
            return "";

        const auto FinalSize = compressed_size + 8;
        Buffer.resize(FinalSize);

        const auto HashKey = XXH32(Buffer.data(), FinalSize, BYTECODE_HASH_SEED);
        const auto Bytes = reinterpret_cast<const uint8_t*>(&HashKey);

        for (auto i = 0u; i < FinalSize; ++i)
            Buffer[i] ^= (Bytes[i % 4] + i * BYTECODE_HASH_MULTIPLIER) & 0xFF;

        return std::string(Buffer.data(), FinalSize);
    }


    static std::string decompress(const std::string& compressed)
    {
        if (compressed.size() < 8)
            return "";

        auto CompressedData = std::vector<char>(compressed.begin(), compressed.end());
        auto HeaderBuffer = std::vector<uint8_t>(4);

        for (auto i = 0u; i < 4; ++i) {
            HeaderBuffer[i] = CompressedData[i] ^ BYTECODE_SIGNATURE[i];
            HeaderBuffer[i] = (HeaderBuffer[i] - i * BYTECODE_HASH_MULTIPLIER);
        }

        for (auto i = 0u; i < CompressedData.size(); ++i) {
            const auto XorValue = (HeaderBuffer[i % 4] + i * BYTECODE_HASH_MULTIPLIER);
            CompressedData[i] ^= XorValue;
        }

        const auto HashValue = *reinterpret_cast<const uint32_t*>(HeaderBuffer.data());
        const auto Rehash = XXH32(CompressedData.data(), CompressedData.size(), BYTECODE_HASH_SEED);
        if (Rehash != HashValue)
            return "";

        const auto DecompressedSize = *reinterpret_cast<const uint32_t*>(&CompressedData[4]);

        auto Decompressed = std::string(DecompressedSize, '\0');
        const auto ActualSize = ZSTD_decompress(&Decompressed[0], DecompressedSize, &CompressedData[8], CompressedData.size() - 8);

        if (ZSTD_isError(ActualSize) || ActualSize != DecompressedSize)
            return "";

        return Decompressed;
    }
};

#endif