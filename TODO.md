# Prompt for agent

* refactor src/vendor -> vendor, to simplify clang tidy run (and same for test/vendor)
* remove IWYU export pragmas from module headers, ie. foo.h for foo.c. Include all necessary files both in the header
  file and the source file, as needed
