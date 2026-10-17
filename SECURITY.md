# Security Policy

## Supported versions

Only the latest release gets security fixes. OpenMirrorPhone checks for updates when it
starts, so keeping the automatic update check on is the easiest way to stay up to date.

## Reporting a vulnerability

Please **don't open a public issue** for a security problem. Report it privately through
GitHub instead: open the [Security tab](https://github.com/ImElio/openmirrorphone/security)
of the repository and click **Report a vulnerability**.

Include, if you can:

- the OpenMirrorPhone version (Help → About) and your Windows version;
- what the problem is and what an attacker could do with it;
- the steps to reproduce it, or a proof of concept.

You'll get an answer as soon as possible, usually within a week. Once a fix is released, the
report can be made public, with credit to you if you'd like.

### Scope

This policy covers the code in this repository. Problems in the libraries it uses are best
reported to their projects too:

- the AirPlay protocol core in `third_party/airplay/`: [UxPlay](https://github.com/FDH2/UxPlay);
- video and audio decoding: [FFmpeg](https://ffmpeg.org/security.html);
- encryption: [OpenSSL](https://www.openssl.org/community/#reporting-security-issues).

## How OpenMirrorPhone handles security

Good to know before you use it:

- **Local network only, no PIN.** Any device on the same network can mirror to the PC, as
  with many Apple TVs. Use it on networks you trust, and allow it only on **private**
  networks when the Windows firewall asks.
- **Encrypted streams.** The screen and sound travel encrypted from the device to the PC,
  using the keys negotiated by the AirPlay handshake.
- **Updates.** The update check and the download use HTTPS to GitHub, and only releases
  published in this repository are installed. The downloaded zip is not separately signed or
  checksummed.
- **Your data stays on your PC.** Settings, the session history and the receiver's key are in
  `%APPDATA%\OpenMirrorPhone`; recordings are in `Videos\OpenMirrorPhone`. The only thing the
  app contacts on the internet is GitHub, to check for updates (it sends nothing but the app's
  version); you can turn that off in Settings.
