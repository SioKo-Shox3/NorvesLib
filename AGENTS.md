# Repository Guidelines

## Project Structure & Module Organization
`NorvesLib` is a C++23 CMake project. Core library code lives under `Library/Core`, split into `Public/` headers and `Private/` implementation files. The root `Library/CMakeLists.txt` exposes the aggregate `NorvesLib` interface target and links the `Core` static library. `Game/` contains the Windows executable and boot/application handlers. `Test/` contains standalone CTest executables, currently focused on `Test/Thread`. Runtime assets are in `Assets/`, especially `Assets/Shaders` and `Assets/Data`. Usage notes belong in `Docs/`, and small demonstrations belong in `Examples/`.

## Build, Test, and Development Commands
Configure the project with CMake before building:

```powershell
cmake -B build -S . -G "Visual Studio 17 2022"
```

Build the main game target:

```powershell
cmake --build build --config Debug --target Game
```

Build and run tests through CTest:

```powershell
cmake --build build --config Debug --target RingBufferTest
cmake --build build --config Debug --target ThreadLocalStorageTest
ctest --test-dir build -C Debug
```

The project requires the Vulkan SDK, including `shaderc_combined`. Set `SLANG_SDK_DIR` during configure only when working on optional Slang-backed neural shader compilation.

## Coding Style & Naming Conventions
Follow the existing C++ style: 4-space indentation, braces on their own control-flow blocks, and namespaces such as `NorvesLib::Thread` or `NorvesLib::Core`. Public types and methods use PascalCase, for example `RingBuffer`, `TryWrite`, and `GetSize`. Member fields use `m_` prefixes, pointer parameters often use `p` prefixes, and boolean fields may use `b` prefixes. Keep headers in matching `Public/<Area>/` paths and implementation files in `Private/<Area>/`. No repository formatter config is present, so preserve local formatting and use the VS Code CMake Tools setup when possible.

## Testing Guidelines
Tests are small executable programs registered with CTest. Use `assert` and clear `std::cout` progress messages, matching `RingBufferTest.cpp`. Name test files and targets with the `*Test` suffix, and add each executable to the relevant `Test/<Module>/CMakeLists.txt` with `add_test`. Add or update tests for behavior changes in containers, threading, memory, parsing, rendering coordination, or file/asset loading.

## Commit & Pull Request Guidelines
Recent commits use short imperative summaries, mostly Japanese with occasional English, without rigid prefixes. Keep commits focused on one logical change, for example `ログ書式とJsonDocumentの安定性を修正する` or `Fix MegaGeometry culling orientation`. Pull requests should include the intent, affected modules, build/test commands run, linked issues when applicable, and screenshots or logs for rendering or game-visible changes.

## Multi-Agent Workflow
Use the main agent as the orchestrator for non-trivial work. The orchestrator owns phase decomposition, architecture decisions, sequencing, integration, commit boundaries, and final review.

At task intake, split the work into explicit phases before editing code. Each phase should be small enough to review, validate, commit, and push independently when possible. If a requested change cannot be cleanly split, state why and define the smallest practical checkpoint. Do not start implementing a later phase until the earlier dependent phase has passed its implementation review and validation gate.

For each phase, write a concrete implementation plan before editing files. The plan must include:
- Phase goal and expected behavioral outcome.
- Affected modules, public APIs, and specific file or directory targets.
- Concrete implementation approach, including ownership/lifetime rules, dependency direction, data structures, threading assumptions, and migration or compatibility strategy.
- Tests or validation commands to run for that phase, including focused targets and CTest coverage.
- Risk level and rollback/containment notes for engine-critical changes.
- Commit boundary: what must be true before the phase can be committed and pushed.

Review each phase plan before implementation. The plan review must check API boundaries, ownership model, dependency direction, resource lifetime, thread safety, CMake/test registration, backward compatibility, and whether the phase is still an appropriate commit-sized unit. For engine-critical changes, especially Public API changes, RHI/Vulkan work, render-thread behavior, memory management, job scheduling, asset loading, shader pipeline changes, or object/resource lifetime changes, this review is required even if the code change looks small.

After implementing each phase, perform an implementation review before committing. Review the actual diff against the approved phase plan, including public API shape, ownership and destruction paths, dependency direction, shared header impact, generated/build file exclusions, CMake registration, test coverage, and any sub-agent output. Fix review findings before moving on. If a planned validation command cannot be run, record the exact reason and the residual risk.

Commit and push by phase when the phase is independently valid. Use focused Japanese commit messages matching the repository style. Do not bundle unrelated phases into one commit unless they are inseparable and the reason is stated. Before staging, explicitly exclude unrelated user changes and generated outputs. After committing, push the current branch when remote configuration allows it.

Use sub-agents for concrete, bounded work that can progress without blocking the orchestrator's immediate next step. Suitable sub-agent tasks include independent codebase inspection, narrow implementation in a disjoint file set, focused test additions, log or CI analysis, and independent diff review. Avoid delegating the critical-path task that the orchestrator must decide next.

When assigning sub-agents, define their scope precisely:
- The phase and objective they support.
- Allowed write paths and forbidden paths.
- Expected output format, including changed files, validation run, unresolved risks, and assumptions.
- Whether their task is read-only, implementation, test creation, or review.

Assign disjoint write scopes to sub-agents. Examples: one agent may handle `Library/Core/Private/Rendering/*`, another `Assets/Shaders/*`, and another `Test/<Module>/*`. Shared files such as public headers, central CMake files, build configuration, serialization schemas, and core lifetime managers must have a single owner or be edited sequentially by the orchestrator. Do not ask multiple agents to edit the same file set unless the orchestrator explicitly sequences the work.

Integrate sub-agent results through the orchestrator. Treat sub-agent output as proposed work, not final work, until the orchestrator has reviewed the diff, confirmed it matches the phase plan, checked API boundaries and ownership, and rerun the relevant validation. Sub-agent results must not be committed directly without this integration review.

For each phase completion, prefer this gate: implementation integrated, implementation review complete, relevant targets build, CTest or focused executable tests pass, and rendering/game-visible changes have logs or screenshots when practical. For final task completion, all phases must be committed or intentionally left uncommitted with a stated reason, and the remaining working tree state must be reported.

## GitHub MCP Usage
Use GitHub MCP or the connected GitHub app for repository, issue, pull request, review, and Actions context when the task depends on GitHub state. Prefer GitHub-backed context for PR summaries, review comments, issue triage, branch/PR metadata, and CI investigation before making local assumptions.

This repository includes `.vscode/mcp.json` for the official remote GitHub MCP server. Do not commit personal access tokens or hardcoded authorization headers. Codex-specific MCP setup belongs in the user's `~/.codex/config.toml` and should reference a token environment variable instead of storing the token directly.

## Documentation & Web Research MCP Usage
Use Context7 MCP first for library, framework, API, and tool documentation lookups. It is the preferred source for current API usage examples because it is documentation-focused. For broader web discovery, current release notes, vendor pages, standards, or unknown documentation sources, use the built-in web search capability instead of adding an API-key-backed search MCP.

Prefer primary sources for technical claims: official documentation, specification pages, vendor release notes, source repositories, or standards documents. When MCP search returns secondary summaries, follow through to the original source before making implementation decisions.

Keep MCP credentials out of the repository. If a future MCP server needs authentication, use user-level environment variables or host-managed prompts rather than literal secrets in committed files.

## Agent-Specific Instructions
Do not edit generated build outputs, logs, or Visual Studio project files under `build/`. Prefer updating CMake source lists, source files, tests, docs, and assets directly. Follow the multi-agent workflow above for substantial work in this repository.
