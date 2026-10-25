# Issue tracker: GitHub

Specs and tickets for this repository live in GitHub Issues for `789852tonnam/GPU-Frustum-Culling-and-Indirect-Draw-in-Vulkan`.

Use the authenticated `gh` CLI. Pass `--repo 789852tonnam/GPU-Frustum-Culling-and-Indirect-Draw-in-Vulkan` to issue commands, or use that owner/repository explicitly in API calls.

## Operations

- Publish a spec or ticket: create one GitHub issue with the approved title/body and the configured triage role. Read `triage-labels.md` in this directory for the actual label name.
- For multiline bodies or comments, write the exact text to a temporary UTF-8 file and pass it with `--body-file`.
- Read an issue with `gh issue view <number> --json number,title,body,labels,comments` and the explicit repository option.
- List issues with `gh issue list --state open --json number,title,body,labels` and the explicit repository option; add filters relevant to the task.
- Update labels through `gh issue edit` with `--add-label` / `--remove-label` after resolving the role mapping. Preserve unrelated labels.
- When closing an approved completed issue, use `gh issue close` with the explicit repository option.

## Parent and blocking relationships

Publish blockers before dependent tickets so references use actual issue identifiers. When a source issue is the parent specification, link its tickets as native GitHub sub-issues using the available CLI or API. If sub-issues are unavailable, include a `Parent: #<number>` reference in each child.

For native parent links, the REST operation is POST `repos/<owner>/<repo>/issues/<parent>/sub_issues` with `sub_issue_id` set to the child's numeric database ID.

Use GitHub native issue dependencies when supported. To mark a child as blocked, POST `repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by` with `issue_id` set to the blocker's numeric database ID. Obtain IDs from the issue API; issue numbers and node IDs are different values.

If native dependencies are unavailable, include `Blocked by: #<number>` references in each dependent issue. Select work from tickets whose blockers are all complete. A ready-for-agent label does not remove a blocker.

Follow the invoked skill's rules for reviewing breakdowns, publishing, changing parent issues, and closing tickets. Preserve the parent specification unless the user or invoked workflow explicitly authorizes an update.

## Pull requests as a triage surface

PRs as a request surface: no.
