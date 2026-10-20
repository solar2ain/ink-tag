# Client updates

[中文](../zh-CN/updates.md) | English

Settings → Client settings → About shows the WebUI version and build identifier. Client versions are checked separately from device firmware.

Update checks offer stable releases only, containing approved, published attachments.

The online, standalone HTML and local-service editions use this project's GitHub Pages `updates/stable.json`. Requests contain no device address, identity, password or Find data and require no embedded GitHub token. Failures and timeouts do not block ordinary operations.

Download approved attachments from the project Releases and follow the installation guide. Checks never install software or flash devices automatically. The first release contains clients only, with an empty firmware list.

Future OTA recommendations require an exact board_id, firmware family, protocol, layout and compatibility epoch. Missing metadata prevents a recommendation. Updates retain the download → select local file → normal OTA flow. SHA256 checks integrity; it does not replace publisher-signature verification or device attestation.

## Release maintenance

Public promotion also requires `python tools/verify-release.py --public --candidate`
and license checks against actual ZIP, sdist and wheel bytes. Renderer
Corresponding Source, some plugin/font/image provenance and WebUI binary terms
remain unresolved. New independent approval uses schema 2 and purpose
`public-client-release`, binding the exact final commit, ledger and artifacts;
old private-preview approvals are not valid.

Pages is NOT deployed by default. Only explicit `--execute --deploy-pages`,
after successful Release round-trip hash verification and feed push, dispatches
the manual workflow for the exact feed commit. Otherwise Release-published and
Pages-pending are separate states, not overall completion. Inspect the local
publication status after deployment failure; do not recreate the Release.
Disabled Pages and Actions account-payment/spending restrictions are external
blockers; the tool does not change those settings.

`releases/record.json` (schema 1) is the single release ledger. `tools/generate-updates.py` verifies downloaded Release bytes and SHA256 before producing the schema 3 stable feed. Unapproved or unpublished candidates are excluded. Firmware publication is disabled in this version.

```sh
python tools/publish-updates.py --repo . --tag <release-tag>
```

This is a dry run: no upload, commit or deployment. After independent approval binds the exact commit, ledger, attachments and metadata, an auditor may supply a local approval receipt and `--execute`. The pipeline pushes a staging branch, creates a draft Release, downloads and verifies attachments, publishes the Release, and generates/pushes feeds. Inspect partial state before retrying any failed publication.

**The current script does not trigger Pages deployment.** The deploy job in
`.github/workflows/pages.yml` accepts only a manual dispatch on `main`; pushes
and pull requests run validation only. Successful script completion does not
mean the site or online update feed has changed. After publication authorization,
licensing review and attachment verification are complete, a maintainer must
manually run `Deploy InkTag Web` in GitHub Actions, selecting `main`. Confirm that
the run uses the approved feed commit and that both validation and deployment
succeed. Compare the live `updates/stable.json` with the reviewed file before
announcing the website update as complete.

Before launch, confirm that Pages is enabled and Actions can actually execute;
a job blocked before starting by account/billing restrictions is not a passing
check. The Pages workflow requires independent review and a validation job.
Private Pages plan restrictions and Actions billing limits still apply;
repository visibility is not changed. Live Pages CORS, standalone HTML and
mobile access require verification after the first real deployment. After any
documentation or code changes, regenerate affected attachments, web manuals and
checksums, and bind approval to the final commit and bytes.
