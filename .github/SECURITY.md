# Security Policy

## Scope

This policy covers the import plugins, shared libraries, packaged resources,
build and release workflows, and third-party code shipped by this repository.

The `main` branch and the latest tagged release are the primary supported
security targets. Older releases may not receive fixes.

## Reporting a vulnerability

Please do **not** report a suspected vulnerability in a public issue or pull
request. Do not include secrets, credentials, private assets, or personal data
in a report.

1. If the repository's GitHub Security tab offers **Report a vulnerability**,
   use that private reporting path.
2. Otherwise, contact a repository maintainer privately through GitHub or
   through a private channel they have provided.
3. If you cannot find a private contact, open an issue containing no technical
   details and ask for a private security contact. Do not describe the
   vulnerability in that issue.

Useful reports usually include:

- the affected version, branch, or package;
- the conditions needed to trigger the problem;
- the impact and likely attack path;
- a minimal reproduction or proof of concept, if safe to share privately;
- any suggested mitigation.

Please use test data only and remove secrets before sending a reproduction.

## What happens next

Maintainers will acknowledge a private report when they receive it, assess its
impact, and coordinate a fix or mitigation. We may ask for more information.
If a fix is released, we will credit the reporter when they want credit and
when doing so is safe.
