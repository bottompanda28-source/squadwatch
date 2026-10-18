#pragma once
// Port of the composer's wlCandidateVariants / trimNameDecor.
#include <string>
#include <vector>

namespace sw {
std::string trimNameDecor(const std::string& s);
std::string stripAccountSuffix(const std::string& name);  // "Foo Bar.1234" -> "Foo Bar" (composer: stripAccountSuffix)
std::vector<std::string> candidateVariants(const std::string& raw);  // normalised lookup keys
}
