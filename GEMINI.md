# Workspace Mandates

- **Strict Surgical Git Staging:** DO NOT use `git add .` or any global staging commands. Every file must be added explicitly or through directory-specific patterns (e.g., `git add src/*.c`) to prevent accidental staging of artifacts or sensitive data.
- **Artifact Protection:** Rigorously verify `git status` before committing to ensure no `.ts`, `.json`, or temporary log files are included in the index.
- **Mandatory Formatting Check:** Before any `git commit`, I MUST execute `make format` followed by `git diff --exit-code` to ensure all code adheres to the project's style guidelines. Any identified formatting changes must be staged before finalizing the commit.
- **Mandatory Whitespace Check:** Before any `git commit`, I MUST execute `git diff --check` (or `git diff HEAD~1 --check` for the latest commit) to ensure there are no trailing whitespaces, conflict markers, or formatting regressions. Fix all identified issues before finalizing the commit.
