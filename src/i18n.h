#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ghx {

// Lightweight embedded i18n for CLI + Markdown output.
//
// - Locale codes supported: "en", "zh-CN" (also accepts "zh", "zh_CN", "zh-cn").
// - Translation keys are stable identifiers like "cli.error.prefix".
// - Missing keys fall back to English; if still missing, fall back to the key itself.
//
// Formatting:
//   I18n::tf("progress.fetching_repo", {{"owner_repo", "octo/repo"}})
// uses "{owner_repo}" placeholders in the translated template.
class I18n {
 public:
  static void set_locale(std::string locale);
  static std::string locale();

  static std::string_view t(std::string_view key);

  // Translate + format template placeholders "{name}".
  static std::string tf(
      std::string_view key,
      const std::vector<std::pair<std::string_view, std::string>>& vars);

 private:
  static std::string normalize_locale(std::string_view locale);
  static std::string replace_all(
      std::string s,
      std::string_view from,
      std::string_view to);
};

}  // namespace ghx

