# Code Style Guide

This document defines the code style preferences for this project.

## Language

- C++23
- Use RAII
- Avoid raw owning pointers

## C++ Formatting

### Indentation

- 4 spaces per indent
- No tabs

### Line Length

- No hard limit; never wrap — statements and comments always stay on one physical line, however long they get

### Braces

- Class definitions: opening brace on its own line
- Function definitions: opening brace on the same line as the signature
- Control structures (`if`, `for`, `while`): opening brace on the same line
- Braces required unless the body is a single line (no braces for single-line bodies); if a comment makes the body span two lines, braces are required

### Naming

- Class names: `PascalCase`
- Functions and variables: `snake_case`
- Member variables: `m_` prefix with snake_case
- Template parameters: `T`, `U`, `Key`, `Value`
- Constants: `UPPER_SNAKE_CASE` for file-scope constants and enum values

### Includes

- Three sections separated by a blank line:
  1. Standard library (alphabetical)
  2. Third-party libraries (alphabetical)
  3. Project files (relative paths, alphabetical)
- No `#include` paths with `..` when the file lives in a nearby directory

### Header Guards

- `#pragma once` only

### Function Definitions and Calls

- No multiline function definitions or calls; keep them on a single line, even if long

### Comments

- Comments are placed **above** the code they describe, not inline
- Format: `// comment text` — a single short line that starts with a lowercase word and has no period
- Action oriented: the comment states what the next line does (e.g. `// resolve the executable from the environment`)
- One short line per comment; never a paragraph of consecutive comment lines
- Never wrap a comment: it stays on one physical line, however long it gets
- Every non-trivial statement gets its own comment line above it — including statements inside `if` blocks, loops, and other control structures
- No docstring format; use plain `//` comments

### Classes

- Public section first, then private
- Use `= default` and `= delete` for special member functions
- Prefer `explicit` for single-argument constructors
- Prefer `[[nodiscard]]` for accessors and functions where ignoring the return value is likely an error
- Use member initializer lists in constructors

### Line Breaks

- One blank line between function definitions
- A maximum of one blank line between sections inside a function
- One blank line between include sections

### Misc

- Use `auto` where it aids readability (iterators, casts, long type names)
- Prefer `std::span` and `std::string_view` for non-owning views
- Use `nullptr`, not `NULL` or `0`
- Use `override` on all overridden virtual functions
- Return `const&` from accessors
- Pass by value and `std::move` for sink parameters

## Python Formatting

### Indentation

- 4 spaces per indent
- No tabs

### Line Length

- No hard limit; never wrap — statements and comments always stay on one physical line, however long they get

### Naming

- Class names: `PascalCase`
- Functions and variables: `snake_case`
- Member variables: `_` prefix with snake_case
- Constants: `UPPER_SNAKE_CASE` for file-scope constants and enum values

### Imports

- Three sections separated by a blank line:
  1. Python standard library (alphabetical)
  2. Third-party libraries (alphabetical)
  3. Project files (alphabetical)

### Comments

- Comments are placed **above** the code they describe, not inline
- Format: `# comment text` — a single short line that starts with a lowercase word and has no period
- Action oriented: the comment states what the next line does (e.g. `# resolve the executable from the environment`)
- One short line per comment; never a paragraph of consecutive comment lines
- Never wrap a comment: it stays on one physical line, however long it gets
- Every non-trivial statement gets its own comment line above it — including statements inside `if` blocks, loops, and other control structures
- No docstring comments, including for files (no module-level docstrings)

### Classes

- One blank line between the class declaration and the first method

  Incorrect:
  ```python
  class FakeClient:
      def __init__(self) -> None:
  ```

  Correct:
  ```python
  class FakeClient:

      def __init__(self) -> None:
  ```

### Line Breaks

- One blank line between function definitions
- A maximum of one blank line between sections inside a function
- One blank line between import sections

### Function Definitions and Calls

- No multiline function definitions or calls; keep them on a single line, even if long

### Type Annotations

- All functions must explicitly declare input and return value types
- Use `-> None` to indicate no return value

  ```python
  # incorrect: missing return type
  def properties(self):
  # incorrect: missing parameter type
  def properties(self, name) -> None:
  # correct
  def properties(self, name: str) -> None:
  ```

## Testing

### C++

#### Framework

- Use Google Test (`gtest`)

#### File Naming

- Test files live under the `tests/` directory and mirror the source tree layout
- Named `<module>.test.cpp`

#### Naming

- Test suite name: `PascalCase` (e.g. `DCSimulationParametersChecks`)
- Test case name: descriptive `snake_case` string (e.g. `parses_lin_sweep`)

#### Structure — Arrange / Act / Assert

- Every test **must** use the `arrange, act, assert` format with explicit section-comment markers
  ```cpp
  // arrange
  ...
  // act
  ...
  // assert
  ...
  ```
- `// arrange / act` is also acceptable when setup and execution are a single step
- Do not separate each section with blank lines
- All test methods should be self-contained whenever possible, avoid utility functions

#### Assertions

- Use `ASSERT_*` and `EXPECT_*` macros from Google Test, not `assert`
- Prefer `ASSERT_EQ`, `ASSERT_TRUE`, `ASSERT_FALSE`, `ASSERT_THROW`
- Use one assertion per line and group related assertions together without blank lines between them

### Python

#### Framework

- Use `unittest`

#### File Naming

- Test files live under the `tests/` directory and mirror the source tree layout
- Named `<module>_test.py`

#### Naming

- Test suite name: `PascalCase`
- Test case name: descriptive `snake_case`

#### Structure — Arrange / Act / Assert

- Every test **must** use the `arrange, act, assert` format with explicit section-comment markers
  ```python
  # arrange
  ...
  # act
  ...
  # assert
  ...
  ```
- `# arrange / act` is also acceptable when setup and execution are a single step
- Do not separate each section with blank lines
- All test methods should be self-contained whenever possible, avoid utility functions
