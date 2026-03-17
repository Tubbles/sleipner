# Prompt for agent

* remove IWYU export pragmas from module headers, ie. foo.h for foo.c. Include all necessary files both in the header
  file and the source file, as needed
* move away from #embed and use raylibs built in asset handling
* generally see if there are other areas where we are inventing our own thing, where raylib actually already has a
  solution available. Lets utilize raylib
* create .clangd for editor lsp support
* enable all clang-tidy checks, and make them all warningaserrors, and then selectively disabling them only if they
  truly are not applicable (c++, verilog), otherwise go through them one by one and decide on a case by case. the
  .clang-tidy file shall not be edited without confirming first, it carries the difference between modern, safe C and
  a buggy memory-unsafe mess
