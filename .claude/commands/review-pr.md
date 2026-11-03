---
description: Recenzja PR przez subagenta pr-reviewer i publikacja jako komentarz (gh pr review --comment)
argument-hint: <numer>
allowed-tools: Bash(gh pr view:*), Bash(gh pr diff:*), Bash(gh pr checks:*), Bash(gh pr review:*), Bash(gh pr comment:*), Bash(gh api repos/{owner}/{repo}/pulls/*), Bash(gh issue list:*), Bash(gh issue create:*), Bash(gh label list:*), Bash(gh label create:*), Agent, Write
---

Review pull request `$ARGUMENTS` of this repository and publish the result as a comment review.

1. Check that `$ARGUMENTS` is a single PR number. Otherwise stop and reply: „Użycie: /review-pr <numer>”.
2. Wait for CI first: `gh pr checks $ARGUMENTS --watch --interval 30` (run it in the background if your shell
   limits a command to a few minutes, and wait for it to exit). Its exit code is not enough: it can exit before every
   check has finished. Afterwards run `gh pr checks $ARGUMENTS` and repeat the watch while any check is pending or
   `linux-status`/`windows-status` is missing. Review only once every check has finished, so the review reports the CI
   result instead of „CI w toku”. If a check is still pending after 60 minutes, stop and tell the user which checks
   are pending; do not publish a review.
3. Collect context (read-only):
   - `gh pr view $ARGUMENTS --json number,title,author,baseRefName,headRefName,headRefOid,files,labels,statusCheckRollup`
     (without `body`: the subagent reads the diff before the description);
   - `gh pr checks $ARGUMENTS` (a non-zero exit only means a check failed; keep the output);
   - the last automatic review: `gh api repos/{owner}/{repo}/pulls/$ARGUMENTS/reviews --paginate` (GET only: never
     pass `-X`/`--method`, `-f`/`-F` or `--input` to `gh api`) and take the newest review whose body contains `_Recenzja automatyczna: /review-pr`. Its `commit_id` is the last reviewed
     SHA and its `body` the previous findings. No such review means a first review.
4. Run the `pr-reviewer` subagent (Agent tool, `subagent_type: "pr-reviewer"`). Give it the PR number, head SHA,
   base branch, title, file list, CI result from step 3, and — for a re-review — the last reviewed SHA and the
   previous review body. Tell it to read the diff itself first (`gh pr diff`, files at the head SHA, every commit since
   the last reviewed SHA) and only then the description. Do not review the PR yourself instead of the subagent.
5. Take the subagent's text unchanged (only strip anything outside the „## Recenzja PR” block) and append the line
   `_Recenzja automatyczna: /review-pr (subagent pr-reviewer), commit <head-sha>, tylko komentarz._`
   Write it to a temporary file outside the repository (the session scratchpad or the system temp directory).
6. Publish: `gh pr review $ARGUMENTS --comment --body-file <file>`.
   Never use `--approve` or `--request-changes` (you cannot approve your own PR, and the verdict is advisory).
7. Skipped findings become issues. The subagent's section „Uwagi pominięte” lists findings of severity `minor` or
   higher from earlier reviews that the new commits did not fix. Line numbers move between pushes, so look a finding up
   by PR and file, not by line: `gh issue list --state all --label review-followup --search "\"PR #$ARGUMENTS\" <plik> in:body"`
   (`<plik>` without the line), then compare the finding's description with each result. For each finding without
   an issue:
   - make sure the label exists (`gh label list --search review-followup`; if missing:
     `gh label create review-followup --color FBCA04 --description "Pominięte uwagi recenzji PR"`);
   - `gh issue create --label review-followup --title "<krótki opis> (PR #$ARGUMENTS)" --body-file <file>`, with
     the body in Polish: the finding (`plik:linia` – waga – opis – sugestia), the link to the review it came from and
     a last line `Źródło: PR #$ARGUMENTS, plik <plik>` (the key of the lookup above).
   Then post one PR comment listing the new issues (`gh pr comment $ARGUMENTS --body-file <file>`, heading
   „Pominięte uwagi recenzji → issues”). Skip the comment when no issue was created.
8. Do not change anything else in the PR: no edits, labels, pushes or merges.
9. Reply to the user with the verdict, the number of findings per severity, the link to the review and the issues
   created in step 7.
