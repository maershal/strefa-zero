---
description: Recenzja PR przez subagenta pr-reviewer i publikacja jako komentarz (gh pr review --comment)
argument-hint: <numer>
allowed-tools: Bash(gh pr view:*), Bash(gh pr diff:*), Bash(gh pr checks:*), Bash(gh pr review:*), Agent, Write
---

Review pull request `$ARGUMENTS` of this repository and publish the result as a comment review.

1. Check that `$ARGUMENTS` is a single PR number. Otherwise stop and reply: „Użycie: /review-pr <numer>”.
2. Collect context (read-only):
   - `gh pr view $ARGUMENTS --json number,title,body,author,baseRefName,headRefName,headRefOid,files,labels,statusCheckRollup`
   - `gh pr diff $ARGUMENTS`
   - `gh pr checks $ARGUMENTS` (a non-zero exit only means checks are failing or pending; keep the output)
3. Run the `pr-reviewer` subagent (Agent tool, `subagent_type: "pr-reviewer"`). Give it the PR number, head SHA,
   base branch, title, description, file list and CI status from step 2, and tell it to read the diff with
   `gh pr diff` and files at the head SHA itself. Do not review the PR yourself instead of the subagent.
4. Take the subagent's text unchanged (only strip anything outside the „## Recenzja PR” block) and append the line
   `_Recenzja automatyczna: /review-pr (subagent pr-reviewer), tylko komentarz._`
   Write it to a temporary file outside the repository (the session scratchpad or the system temp directory).
5. Publish: `gh pr review $ARGUMENTS --comment --body-file <file>`.
   Never use `--approve` or `--request-changes` (you cannot approve your own PR, and the verdict is advisory).
   Do not change anything else in the PR: no edits, labels, pushes, merges or other comments.
6. Reply to the user with the verdict, the number of findings per severity and the link to the review.
