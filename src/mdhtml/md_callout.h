#ifndef FILE_MDHTML_MD_CALLOUT_H_
#define FILE_MDHTML_MD_CALLOUT_H_

#include <string>
#include <string_view>

// Rewrites blockquote HTML produced by md4c into MPE-style callouts:
//
//   <blockquote><p>[!note] Title\nBody</p></blockquote>
//     ->
//   <div class="callout callout-note">
//   <div class="callout-title"><span class="callout-icon">..</span>Title</div>
//   <p>Body</p>
//   </div>
//
// Called only in MdAdmonitionMode::kMpe. The marker must be the first text
// of the first paragraph; inline text after it is the title, matching
// Markdown Preview Enhanced's callout extension.
std::string ApplyMpeCallouts(std::string_view html);

#endif  // FILE_MDHTML_MD_CALLOUT_H_
