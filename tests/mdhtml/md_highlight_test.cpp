#include "md_highlight.h"

#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

TEST(MdHighlightTest, ColorsCppTokens) {
  const std::string html = MdHighlightCode(
      "class A { int n = 42; const char* s = \"x\"; };  // note", "cpp",
      nullptr);
  EXPECT_NE(html.find("class=\"kw\""), std::string::npos) << html;
  EXPECT_NE(html.find("class=\"num\">42<"), std::string::npos) << html;
  EXPECT_NE(html.find("class=\"str\""), std::string::npos) << html;
  EXPECT_NE(html.find("class=\"cmt\""), std::string::npos) << html;
  EXPECT_NE(html.find("&quot;x&quot;"), std::string::npos) << html;
}

TEST(MdHighlightTest, CoversBundledLanguages) {
  // One representative snippet per registered language: each must produce at
  // least one span, so a missing import (language silently unregistered and
  // escaped) fails instead of quietly degrading.
  struct Sample {
    const char* language;
    const char* code;
  };
  const Sample samples[] = {
      {"c", "int main(void) { return 0; }"},
      {"cpp", "class A { public: int x = 1; };"},
      {"csharp", "public class A { private int n = 1; }"},
      {"go", "package main\nfunc main() {}"},
      {"java", "public class A { private int n = 1; }"},
      {"kotlin", "fun main() { val x: Int = 1 }"},
      {"objectivec", "@interface Foo : NSObject\n@end"},
      {"rust", "fn main() { let x: i32 = 1; }"},
      {"scala", "object Main extends App { val x = 1 }"},
      {"swift", "func greet(name: String) -> String { return name }"},
      {"dart", "void main() { final int x = 1; }"},
      {"bash", "if [ -f \"$file\" ]; then echo hi; fi"},
      {"dos", "@echo off\nset X=1"},
      {"javascript", "const f = (x) => x + 1;"},
      {"typescript", "interface User { id: number }"},
      {"python", "def f(x):\n    return x + 1"},
      {"ruby", "def greet(name)\n  puts \"hi #{name}\"\nend"},
      {"php", "<?php function f($x) { return $x; }"},
      {"lua", "local function f(x) return x + 1 end"},
      {"perl", "my $x = 1; print $x;"},
      {"powershell",
       R"(Get-ChildItem -Path C:\ | Where-Object { $_.Length -gt 0 })"},
      {"groovy", "def greet(name) { println \"hi $name\" }"},
      {"r", "x <- c(1, 2, 3)"},
      {"julia", "function f(x)\n    return x + 1\nend"},
      {"elixir", "defmodule M do\n  def f(x), do: x + 1\nend"},
      {"erlang", "-module(m).\nf(X) -> X + 1."},
      {"clojure", "(defn f [x] (+ x 1))"},
      {"haskell", "f :: Int -> Int\nf x = x + 1"},
      {"fsharp", "let add x y = x + y"},
      {"vbnet", "Public Class A\n  Private x As Integer\nEnd Class"},
      {"ocaml", "let add x y = x + y"},
      {"xml", "<div class=\"a\">x</div>"},
      {"css", "a { color: red; }"},
      {"scss", "$c: red;\n.a { color: $c; }"},
      {"less", "@c: red;\n.a { color: @c; }"},
      {"markdown", "# Title\n\n**bold** and `code`"},
      {"latex", "\\section{Intro} $x^2$"},
      {"graphql", "query { user(id: 1) { name } }"},
      {"json", "{\"a\": 1}"},
      {"yaml", "name: qlitemd\nversion: 0.3.1"},
      {"ini", "[core]\nname = qlitemd"},
      {"sql", "SELECT * FROM t WHERE a = 1;"},
      {"protobuf", "message User { string name = 1; }"},
      {"http", "GET /api HTTP/1.1\nHost: example.com"},
      {"diff", "--- a/x\n+++ b/x\n@@ -1 +1 @@\n-old\n+new"},
      {"makefile", "all:\n\tcc -o app main.c"},
      {"cmake", "add_executable(app main.cpp)"},
      {"dockerfile", "FROM ubuntu:24.04\nRUN apt-get update"},
      {"nginx", "server { listen 80; }"},
      {"properties", "key=value\nother=thing"},
      {"vim", "set number\nmap <F2> :w<CR>"},
      {"x86asm", "mov eax, 1\nret"},
      {"fortran", "program hello\n  print *, \"hi\"\nend program hello"},
      {"matlab", "x = 1:10; y = sin(x);"},
      // Not in the curated examples: these prove the bundle really ships all
      // upstream languages and registers them on first use.
      {"ada", "procedure Hello is begin null; end Hello;"},
      {"verilog", "module m(input clk);\nendmodule"},
      {"scheme", "(define (f x) (+ x 1))"},
      {"nim", "proc f(x: int): int = x + 1"},
      {"crystal", "def f(x); x + 1; end"},
      {"prolog", "parent(X, Y) :- father(X, Y)."},
      {"elm", "add : Int -> Int -> Int"},
      {"lisp", "(defun f (x) (+ x 1))"},
  };
  for (const Sample& sample : samples) {
    const std::string html =
        MdHighlightCode(sample.code, sample.language, nullptr);
    EXPECT_NE(html.find("<span"), std::string::npos) << sample.language;
  }
}

TEST(MdHighlightTest, AliasesResolve) {
  const std::string python =
      MdHighlightCode("def f():\n    pass\n", "py", nullptr);
  EXPECT_NE(python.find("class=\"kw\""), std::string::npos) << python;

  const std::string cpp = MdHighlightCode("class A {};", "cpp", nullptr);
  for (const char* alias : {"c++", "cpp"}) {
    EXPECT_EQ(MdHighlightCode("class A {};", alias, nullptr), cpp) << alias;
  }
  EXPECT_EQ(MdHighlightCode("const a = 1;", "js", nullptr),
            MdHighlightCode("const a = 1;", "javascript", nullptr));
  EXPECT_EQ(MdHighlightCode("SELECT 1;", "html", nullptr),
            MdHighlightCode("SELECT 1;", "xml", nullptr));
}

TEST(MdHighlightTest, SqlKeywordsAreHighlighted) {
  // sql was dropped when highlighting moved to tree-sitter; highlight.js
  // brings it back, so pin one representative keyword.
  const std::string html =
      MdHighlightCode("SELECT * FROM t WHERE a = 1;", "sql", nullptr);
  EXPECT_NE(html.find("class=\"kw\">SELECT<"), std::string::npos) << html;
}

TEST(MdHighlightTest, AliasResolvesBeforeRegistration) {
  // "v" is verilog's upstream alias. It must resolve through the baked alias
  // table even though verilog has never been registered in this process.
  const std::string html =
      MdHighlightCode("module m(input clk);\nendmodule", "v", nullptr);
  EXPECT_NE(html.find("<span"), std::string::npos) << html;
}

TEST(MdHighlightTest, UnknownLanguageIsPlainEscaped) {
  EXPECT_EQ(MdHighlightCode("a < b & c", "notalang", nullptr),
            "a &lt; b &amp; c");
  EXPECT_EQ(MdHighlightCode("a'b", "notalang", nullptr), "a&#x27;b");
}

TEST(MdHighlightTest, EmptyInputYieldsEmpty) {
  EXPECT_EQ(MdHighlightCode("", "cpp", nullptr), "");
}

TEST(MdHighlightTest, SpansAreBalancedPerLine) {
  const std::string html = MdHighlightCode(
      "/* first\nsecond */ int a = 1;\n// done\n", "cpp", nullptr);
  std::size_t start = 0;
  while (start <= html.size()) {
    const std::size_t newline = html.find('\n', start);
    const std::size_t end =
        newline == std::string::npos ? html.size() : newline;
    const std::string line = html.substr(start, end - start);
    std::size_t open = 0;
    std::size_t close = 0;
    for (std::size_t at = line.find("<span"); at != std::string::npos;
         at = line.find("<span", at + 1)) {
      ++open;
    }
    for (std::size_t at = line.find("</span>"); at != std::string::npos;
         at = line.find("</span>", at + 1)) {
      ++close;
    }
    EXPECT_EQ(open, close) << line;
    if (newline == std::string::npos) {
      break;
    }
    start = newline + 1;
  }
}

TEST(MdHighlightTest, RenderKeySeparatesLanguage) {
  EXPECT_EQ(MdHighlightRenderKey("int a = 1;", "cpp"), "cpp\nint a = 1;");
  EXPECT_NE(MdHighlightRenderKey("x", "cpp"),
            MdHighlightRenderKey("x", "python"));
}

TEST(MdHighlightTest, EngineWorksOnWorkerThread) {
  std::string html;
  std::thread worker([&html] {
    MdHighlightEngine engine;
    html = MdHighlightCode("int a = 1;", "cpp", &engine);
  });
  worker.join();
  EXPECT_NE(html.find("<span"), std::string::npos) << html;
}

TEST(MdHighlightTest, EngineReusesRuntime) {
  MdHighlightEngine engine;
  const std::string first = MdHighlightCode("int a = 1;", "cpp", &engine);
  const std::string second = MdHighlightCode("int a = 1;", "cpp", &engine);
  EXPECT_EQ(first, second);
  EXPECT_NE(first.find("<span"), std::string::npos) << first;
}

}  // namespace
