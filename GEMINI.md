# Workspace Mandates

- **Strict Surgical Git Staging:** DO NOT use `git add .` or any global staging commands. Every file must be added explicitly or through directory-specific patterns (e.g., `git add src/*.c`) to prevent accidental staging of artifacts or sensitive data.
- **Artifact Protection:** Rigorously verify `git status` before committing to ensure no `.ts`, `.json`, or temporary log files are included in the index.
