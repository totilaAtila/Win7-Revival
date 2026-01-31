# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| latest  | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in Win7Revival, please report it responsibly.

**Do NOT open a public GitHub issue for security vulnerabilities.**

Instead, send an email to **totila6@gmail.com** with:

- A description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

You should receive a response within 7 days. Critical issues will be patched and released as soon as possible.

## Scope

This project uses Win32 P/Invoke APIs to customize Windows UI elements. Security-relevant areas include:

- **Settings file handling** (`%AppData%/Win7Revival/`) — path traversal, JSON deserialization
- **Registry access** (HKCU auto-start) — key injection
- **P/Invoke surface** — memory safety in `Marshal.AllocHGlobal`/`FreeHGlobal` calls
- **NuGet dependencies** — supply chain vulnerabilities

## Out of Scope

- Windows OS vulnerabilities
- Attacks requiring physical access to the machine
- Social engineering
