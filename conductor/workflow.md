# Project Workflow

## Development Loop
1. **Research**: Analyze the codebase and requirements.
2. **Strategy**: Propose a implementation plan.
3. **Execution**:
    - **TDD (Test-Driven Development)**:
        - Write unit/integration tests (85% coverage required).
        - Implement the feature/fix.
        - Verify results.
    - **Validation**:
        - Mandatory `clang-format` check.
        - Mandatory `git diff --check` for whitespace/formatting errors.
4. **Commit**:
    - Commit after each task.
    - Include task summaries in the commit message body.
5. **Auto-Transition**: Automatically proceed to the next task until the phase or track is complete.

## Quality Gates
- Build: All builds must pass without warnings.
- Tests: All tests must pass; minimum 85% coverage.
- Code Style: C11 standard with `clang-format` compliance.
- Git Consistency: Clean diffs (no trailing whitespace, proper indentation).
