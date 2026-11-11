# OpenMirrorPhone for Android

OpenMirrorPhone for Android is a companion app that streams an Android screen to the Windows
receiver. It is an early preview and is distributed separately from the Windows installer.

## Set up

1. Start OpenMirrorPhone on Windows and open **Settings > Android**.
2. Turn on **Enable Android support**, then copy the address shown there.
3. Install and open this companion app on Android. Tap **Scan QR** and scan the code in Windows
   **Settings > Android**. This saves the PC address; it does not start screen sharing. With
   **Remember PC address** enabled, it stays saved for next time. You can also enter the address
   manually.
4. When ready to share, tap **Start capture**, approve Android's screen-sharing prompt, and allow playback-audio access
   if **Stream app audio** is enabled in Settings.
5. Keep the selected app open to mirror it. If you leave it, the PC and phone show a notice;
   return to the selected app to resume.
6. Stop sharing from the Android app or its foreground notification.

Connect the phone and PC to the same trusted local network. For the Android emulator, use
`10.0.2.2:24862`. On Android 13 and newer, allow notifications to see the persistent Stop action
and status updates while the app is in the background. Denying notification permission does not
prevent screen sharing; stop it from the app or the Windows receiver instead.

## Current limitations

- Video and supported app playback audio are streamed to Windows; the microphone is not captured.
  Some apps can block playback capture, so audio is not guaranteed for every source app. Automatic
  PC discovery is not available yet.
- The Windows receiver is off by default and listens on TCP port 24862 only after you enable it
  in **Settings > Android**.
- There is no PIN, sender authentication or encryption. Any device on the same network can connect
  while Android support is enabled. Turn it off when you are not using it.
- The selected-app visibility notice requires Android 14 or newer. On older Android versions,
  screen capture continues according to the system's sharing behavior.

Settings includes the latest stable release notes from GitHub and keeps the last fetched notes
available offline. It checks the Android release channel only. A newer release with an APK asset
can be downloaded from that page; Android still asks the user to confirm installation. The Home
screen has a PayPal link for people who want to support development.

Settings includes two preferences: **Stream app audio** (on by default) and **Remember PC
address** (on by default). Turning off the address preference also clears the saved receiver
address from the device.

## Android release channel

Android uses its own version number, independent of the Windows app. Stable Android releases use
tags in the form `android-vX.Y.Z`; attach one APK asset named
`OpenMirrorPhone-X.Y.Z-android.apk` and put Android-specific notes in the release description.
For example, the first public companion release would use tag `android-v0.1.0` and asset
`OpenMirrorPhone-0.1.0-android.apk`. The Android app's **What's New** page reads only stable
`android-v*` releases and offers the APK when its version is newer than the installed app.

The Windows updater ignores `android-*` tags. Keep the Android signing keystore stable and
increase `versionCode` for every APK, even when `versionName` follows a separate Android version
sequence. Do not publish a tag or release until the APK and notes have been reviewed.

## Build and run

This project uses Java and downloads its Android libraries through Gradle.

1. Install Android Studio and an Android SDK for API 36.
2. Open this `android` folder as a project and let Gradle sync, or run `gradlew.bat
   assembleDebug` from this folder.
3. Install the debug APK at `app/build/outputs/apk/debug/app-debug.apk` on an Android 10+
   device or emulator.

For a signed release APK, keep the signing keystore outside the repository, copy
`keystore.properties.example` to `keystore.properties`, and enter the local keystore path, alias
and passwords there. Then run `gradlew.bat assembleRelease`; the APK is written to
`app/build/outputs/apk/release/app-release.apk`. Back up the keystore and its passwords securely:
future Android updates must use the same signing key. Never commit either file.

The app UI is built in code: `MainActivity` (Home), `HelpActivity`, `SettingsActivity`,
`WhatsNewActivity` and `Ui` (shared colours and building blocks). The QR scanner opens in portrait
and saves the Windows receiver address without starting screen sharing. `AndroidStreamSender`
encodes and sends video; `ScreenCaptureService` manages Android's screen-capture session and
foreground notification. QR scanning uses ZXing Android Embedded under Apache License 2.0.

## Privacy

Android asks for screen-capture approval each time sharing starts. When **Stream app audio** is
enabled, Android separately asks for audio permission. Audio permission is used for app playback
capture, not microphone recording. Captured
video and supported app audio are sent directly to the PC address entered in the app; the
companion does not save or upload them. Source apps can block audio capture. The local-network
stream is not encrypted or authenticated. Use a trusted network and disable Android support in
Windows when finished.
