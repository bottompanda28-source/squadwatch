#pragma once
#include <cstddef>

namespace sw { namespace uni {
struct Range { char32_t lo, hi; };
struct LowerMap { char32_t from, to1, to2; };  // to2 == 0 when the lowercase form is a single code point
extern const Range kLetterNumber[];
extern const size_t kLetterNumberCount;
extern const LowerMap kLower[];
extern const size_t kLowerCount;
}} // namespace sw::uni
