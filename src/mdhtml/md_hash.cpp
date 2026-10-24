#include "md_hash.h"

#include <array>
#include <cstdint>

namespace {

constexpr uint32_t RightRotate(uint32_t value, int bits) {
  return (value >> bits) | (value << (32 - bits));
}

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

void CompressBlock(const uint8_t* block, uint32_t* state) {
  uint32_t words[64];
  for (int i = 0; i < 16; ++i) {
    words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = RightRotate(words[i - 15], 7) ^
                        RightRotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
    const uint32_t s1 = RightRotate(words[i - 2], 17) ^
                        RightRotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t sum1 =
        RightRotate(e, 6) ^ RightRotate(e, 11) ^ RightRotate(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + sum1 + choice + kRoundConstants[i] + words[i];
    const uint32_t sum0 =
        RightRotate(a, 2) ^ RightRotate(a, 13) ^ RightRotate(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

}  // namespace

std::string MdSha256Hex(std::string_view data) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  std::size_t offset = 0;
  while (data.size() - offset >= 64) {
    CompressBlock(reinterpret_cast<const uint8_t*>(data.data()) + offset,
                  state);
    offset += 64;
  }

  uint8_t tail[128] = {};
  const std::size_t remaining = data.size() - offset;
  for (std::size_t i = 0; i < remaining; ++i) {
    tail[i] = static_cast<uint8_t>(data[offset + i]);
  }
  tail[remaining] = 0x80;
  const std::size_t tail_blocks = remaining + 9 > 64 ? 2 : 1;
  const uint64_t bit_length = static_cast<uint64_t>(data.size()) * 8;
  const std::size_t length_offset = tail_blocks * 64 - 8;
  for (int i = 0; i < 8; ++i) {
    tail[length_offset + i] = static_cast<uint8_t>(bit_length >> (56 - i * 8));
  }
  for (std::size_t block = 0; block < tail_blocks; ++block) {
    CompressBlock(tail + block * 64, state);
  }

  constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (uint32_t word : state) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      out.push_back(kHex[(word >> shift) & 0xF]);
    }
  }
  return out;
}

std::string MdAssetId(MdAssetKind kind, std::string_view key) {
  std::string input = kind == MdAssetKind::Formula ? "f|" : "m|";
  input.append(key);
  return MdSha256Hex(input).substr(0, 32);
}
