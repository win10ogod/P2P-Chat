# Contributing to P2P Chat

Thank you for your interest in contributing! This document provides guidelines for contributing to the P2P Chat project.

## Getting Started

1. Fork the repository and clone your fork.
2. Create a feature branch from `main`: `git checkout -b feature/my-feature`.
3. Make your changes following the coding standards below.
4. Ensure the project compiles cleanly with zero warnings on all supported platforms.
5. Submit a pull request with a clear description of your changes.

## Coding Standards

This project follows modern C++17 conventions. Please adhere to the following guidelines:

| Aspect | Convention |
|--------|-----------|
| Language standard | C++17 (no compiler extensions) |
| Naming: types | `PascalCase` |
| Naming: functions/methods | `snake_case` |
| Naming: constants | `kPascalCase` |
| Naming: member variables | `trailing_underscore_` |
| Indentation | 4 spaces (no tabs) |
| Line length | 100 characters soft limit |
| Braces | K&R style (opening brace on same line) |
| Headers | `#pragma once` guard |
| Includes | Grouped: project headers, then system headers |

## Design Principles

The codebase follows these architectural principles:

- **RAII everywhere**: Resources are managed by their owning objects. No raw `new`/`delete`.
- **Value semantics**: Prefer value types. Use `std::unique_ptr` for polymorphism or ownership transfer.
- **Thread safety**: Shared state is protected by `std::mutex` or `std::shared_mutex`. Use `ConcurrentQueue` for inter-thread communication.
- **Error handling**: Use `std::optional` for fallible operations. Reserve exceptions for truly exceptional conditions.
- **No global state**: Aside from platform initialization singletons (e.g., Winsock), avoid global mutable state.

## Commit Messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) specification:

```
feat: add voice mute toggle to GUI
fix: resolve race condition in P2PConnection heartbeat
docs: update build instructions for macOS
refactor: simplify ByteReader bounds checking
```

## Reporting Issues

When reporting bugs, please include:

- Operating system and version
- Compiler and version
- Steps to reproduce
- Expected vs. actual behavior

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
