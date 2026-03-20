#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

command -v codex >/dev/null || {
  echo "Install codex CLI to continue:"
  echo "-> https://developers.openai.com/codex/cli"
  echo
  exit 1
}

codex exec --cd "$ROOT" "$(cat <<EOF
Update openpilot UI translations in selfdrive/ui/translations.
- Translate English UI text naturally.
- Preserve placeholders (%n, %1, {}, {:.1f}), HTML/tags, and plural forms.
- Edit .po files in place.
- Print a short summary of changes.
EOF
)"
