# Client updates

[中文](../zh-CN/updates.md) | English

Settings → Client settings → About shows the WebUI version and build identifier. Client versions are checked separately from device firmware.

Update checks offer stable releases only, containing approved, published attachments.

The online, standalone HTML and local-service editions use this project's GitHub Pages `updates/stable.json`. Requests contain no device address, identity, password or Find data and require no embedded GitHub token. Failures and timeouts do not block ordinary operations.

Download approved attachments from the project Releases and follow the installation guide. Checks never install software or flash devices automatically. The first release contains clients only, with an empty firmware list.

Future OTA recommendations require an exact board_id, firmware family, protocol, layout and compatibility epoch. Missing metadata prevents a recommendation. Updates retain the download → select local file → normal OTA flow. SHA256 checks integrity; it does not replace publisher-signature verification or device attestation.

## Release maintenance

`releases/record.json` (schema 1) is the single release ledger. `tools/generate-updates.py` verifies downloaded Release bytes and SHA256 before producing the schema 3 stable feed. Unapproved or unpublished candidates are excluded. Firmware publication is disabled in this version.

```sh
python tools/publish-updates.py --repo . --tag <release-tag>
```

This is a dry run: no upload, commit or deployment. After independent approval binds the exact commit, ledger, attachments and metadata, an auditor may supply a local approval receipt and `--execute`. The pipeline pushes a non-deploying staging branch, creates a draft Release, downloads and verifies attachments, publishes the Release, generates/pushes feeds, and lets the existing workflow deploy only `webui/`. Inspect partial state before retrying any failed publication.

The Pages workflow requires independent review and a validation job. Private Pages plan restrictions and Actions billing limits still apply; repository visibility is not changed. Live Pages CORS, standalone HTML and mobile access require verification after the first real deployment.
