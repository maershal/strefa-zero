#!/usr/bin/env bash
# PreToolUse(Bash): refuse shell commands that read raw measurement evidence.
# Reports in docs/reports/ quote the numbers that matter; the raw data under
# docs/measurements/ is large, byte-identical evidence. git commands are allowed
# so files can still be moved, staged and diffed by name.
command=$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))')
[[ $command == *measurements/* || $command == *measurements[[:space:]]* || $command == *measurements ]] || exit 0
[[ $command =~ ^[[:space:]]*git[[:space:]] ]] && exit 0
echo 'Blocked: docs/measurements/ holds raw evidence. Read the matching report in docs/reports/ instead, or ask the user to lift this rule.' >&2
exit 2
