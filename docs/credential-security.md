# Credential safety

Never commit real passwords, server secrets, access tokens, private keys, or signing stores.
Use environment variables, CI secrets, or an ignored local configuration file. Commit only
empty example files. API/client keys in an APK can be extracted: configure provider restrictions
and keep server credentials on a trusted server.

Install [Gitleaks v8.30.1](https://github.com/gitleaks/gitleaks/releases/tag/v8.30.1).
Before committing, run `gitleaks git . --pre-commit --staged --config .gitleaks.toml --redact=100`.
For an independent full audit, run `gitleaks git . --log-opts="--all --full-history -m" --config .gitleaks.toml --redact=100`.
Review findings by their use, including low-entropy passwords and configuration that scanners miss.

The CI check scans a tracked-file snapshot (including files matched by .gitignore) and new
commits. It does not exempt old credential values or use a secret-containing baseline.
For a new branch without a base SHA, it scans the final tree and latest commit; the full-history
command above is still required for a historical audit. Make the CI check required in branch rules,
and enable GitHub secret scanning and push protection where available.

If a real credential was committed, remove it from current code and revoke/rotate it in the
provider console. Deleting a file or adding .gitignore does not remove history. Coordinate any
history rewrite and force push with every collaborator; do not perform them automatically.
Do not print credential values in logs, reports, issue descriptions, or commit messages.
