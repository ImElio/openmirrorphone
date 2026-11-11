package com.imelio.openmirrorphone;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.RippleDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/** Shows release notes for the latest stable Android companion release. */
public final class WhatsNewActivity extends Activity {
    private static final String RELEASE_API =
            "https://api.github.com/repos/ImElio/openmirrorphone/releases?per_page=100";
    private static final String ANDROID_TAG_PREFIX = "android-v";
    private static final String RELEASE_PREFS = "github_android_release_notes";
    private static final String PREF_VERSION = "version";
    private static final String PREF_BODY = "body";
    private static final String PREF_URL = "url";
    private static final String PREF_APK_URL = "apk_url";
    private static final int MAX_RESPONSE_BYTES = 1024 * 1024;

    private TextView versionView;
    private TextView bodyView;
    private TextView statusView;
    private TextView downloadLink;
    private TextView githubLink;
    private TextView refreshLink;
    private volatile boolean loading;

    private static final class ReleaseNotes {
        final String version;
        final String body;
        final String url;
        final String apkUrl;

        ReleaseNotes(String version, String body, String url, String apkUrl) {
            this.version = version;
            this.body = body;
            this.url = url;
            this.apkUrl = apkUrl;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Ui.Screen screen = Ui.screen(this);
        LinearLayout page = screen.page;
        page.addView(Ui.header(this, getString(R.string.settings_whats_new_title)), Ui.matchWrap());

        TextView intro = Ui.text(this, getString(R.string.whats_new_intro), 14,
                Ui.TEXT_SECONDARY, Typeface.NORMAL);
        intro.setLineSpacing(Ui.dp(this, 3), 1f);
        page.addView(intro, Ui.matchWrap(this, 20));

        LinearLayout card = Ui.group(this);
        versionView = Ui.text(this, "", 16, Ui.TEXT_PRIMARY, Typeface.BOLD);
        card.addView(versionView, Ui.matchWrap());

        statusView = Ui.text(this, getString(R.string.whats_new_loading), 13,
                Ui.TEXT_SECONDARY, Typeface.NORMAL);
        statusView.setPadding(0, Ui.dp(this, 8), 0, 0);
        card.addView(statusView, Ui.matchWrap());

        bodyView = Ui.text(this, "", 14, Ui.TEXT_PRIMARY, Typeface.NORMAL);
        bodyView.setLineSpacing(Ui.dp(this, 4), 1f);
        bodyView.setPadding(0, Ui.dp(this, 14), 0, 0);
        card.addView(bodyView, Ui.matchWrap());
        page.addView(card, Ui.matchWrap(this, 18));

        downloadLink = actionLink(R.string.whats_new_download_android);
        downloadLink.setVisibility(View.GONE);
        page.addView(downloadLink, Ui.matchWrap(this, 12));

        githubLink = actionLink(R.string.whats_new_view_github);
        githubLink.setVisibility(View.GONE);
        page.addView(githubLink, Ui.matchWrap(this, 12));

        refreshLink = actionLink(R.string.whats_new_refresh);
        refreshLink.setOnClickListener(v -> refreshFromGitHub());
        page.addView(refreshLink, Ui.matchWrap(this, 4));

        setContentView(screen.root);
        showCachedNotes();
        refreshFromGitHub();
    }

    private TextView actionLink(int label) {
        TextView link = Ui.text(this, getString(label), 14, Ui.ACCENT, Typeface.BOLD);
        link.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        link.setPadding(0, Ui.dp(this, 10), 0, Ui.dp(this, 10));
        link.setBackground(new RippleDrawable(ColorStateList.valueOf(0x26FFFFFF), null,
                Ui.roundRect(this, 0xFFFFFFFF, 4)));
        return link;
    }

    private void showCachedNotes() {
        SharedPreferences prefs = getSharedPreferences(RELEASE_PREFS, MODE_PRIVATE);
        String version = prefs.getString(PREF_VERSION, "");
        String body = prefs.getString(PREF_BODY, "");
        if (version.isEmpty()) return;

        versionView.setText(getString(R.string.whats_new_latest_android_version, version));
        bodyView.setText(body.isEmpty() ? getString(R.string.whats_new_empty) : markdownToPlainText(body, version));
        statusView.setText(R.string.whats_new_cached);
        showAndroidDownload(version, prefs.getString(PREF_APK_URL, ""));
        showGitHubLink(prefs.getString(PREF_URL, ""));
    }

    private void refreshFromGitHub() {
        if (loading) return;
        loading = true;
        refreshLink.setEnabled(false);
        if (bodyView.length() == 0) statusView.setText(R.string.whats_new_loading);

        Thread worker = new Thread(() -> {
            ReleaseNotes result = null;
            String error = null;
            try {
                result = fetchLatestRelease();
            } catch (Exception exception) {
                error = exception.getMessage();
                if (error == null || error.trim().isEmpty()) error = getString(R.string.whats_new_network_error);
            }

            ReleaseNotes notes = result;
            String message = error;
            runOnUiThread(() -> {
                loading = false;
                if (isFinishing() || isDestroyed()) return;
                refreshLink.setEnabled(true);
                if (notes != null) {
                    getSharedPreferences(RELEASE_PREFS, MODE_PRIVATE).edit()
                            .putString(PREF_VERSION, notes.version)
                            .putString(PREF_BODY, notes.body)
                            .putString(PREF_URL, notes.url)
                            .putString(PREF_APK_URL, notes.apkUrl)
                            .apply();
                    versionView.setText(getString(R.string.whats_new_latest_android_version, notes.version));
                    bodyView.setText(notes.body.isEmpty()
                            ? getString(R.string.whats_new_empty)
                            : markdownToPlainText(notes.body, notes.version));
                    statusView.setText(R.string.whats_new_from_github);
                    showAndroidDownload(notes.version, notes.apkUrl);
                    showGitHubLink(notes.url);
                } else if (bodyView.length() > 0) {
                    statusView.setText(getString(R.string.whats_new_cached_error, message));
                } else if (getString(R.string.whats_new_no_release).equals(message)) {
                    versionView.setText("");
                    bodyView.setText("");
                    statusView.setText(R.string.whats_new_no_release);
                } else {
                    versionView.setText("");
                    bodyView.setText("");
                    statusView.setText(getString(R.string.whats_new_error, message));
                }
            });
        }, "OpenMirrorPhone-release-notes");
        worker.setDaemon(true);
        worker.start();
    }

    private ReleaseNotes fetchLatestRelease() throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(RELEASE_API).openConnection();
        connection.setRequestMethod("GET");
        connection.setConnectTimeout(8000);
        connection.setReadTimeout(12000);
        connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("User-Agent", "OpenMirrorPhone-Android/" + Ui.versionName(this));
        try {
            int status = connection.getResponseCode();
            if (status != HttpURLConnection.HTTP_OK) {
                throw new IOException(status == HttpURLConnection.HTTP_NOT_FOUND
                        ? getString(R.string.whats_new_no_release)
                        : getString(R.string.whats_new_http_error, status));
            }
            String response = readResponse(connection.getInputStream());
            JSONArray releases = new JSONArray(response);
            for (int i = 0; i < releases.length(); i++) {
                JSONObject release = releases.optJSONObject(i);
                if (release == null || release.optBoolean("draft") || release.optBoolean("prerelease")) continue;
                String tag = release.optString("tag_name", "").trim();
                if (!tag.startsWith(ANDROID_TAG_PREFIX)) continue;
                String version = tag.substring(ANDROID_TAG_PREFIX.length()).trim();
                if (version.isEmpty()) continue;
                return new ReleaseNotes(version, release.optString("body", "").trim(),
                        release.optString("html_url", "").trim(), findAndroidApk(release.optJSONArray("assets")));
            }
            throw new IOException(getString(R.string.whats_new_no_release));
        } finally {
            connection.disconnect();
        }
    }

    private String findAndroidApk(JSONArray assets) {
        if (assets == null) return "";
        for (int i = 0; i < assets.length(); i++) {
            JSONObject asset = assets.optJSONObject(i);
            if (asset == null) continue;
            String name = asset.optString("name", "").toLowerCase(java.util.Locale.ROOT);
            String url = asset.optString("browser_download_url", "").trim();
            if (name.endsWith("-android.apk")
                    && url.startsWith("https://github.com/ImElio/openmirrorphone/releases/download/")) {
                return url;
            }
        }
        return "";
    }

    private String readResponse(InputStream stream) throws IOException {
        try (InputStream input = stream; ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() + count > MAX_RESPONSE_BYTES) {
                    throw new IOException(getString(R.string.whats_new_response_too_large));
                }
                output.write(buffer, 0, count);
            }
            return output.toString("UTF-8");
        }
    }

    private void showGitHubLink(String url) {
        if (url == null || !url.startsWith("https://github.com/")) {
            githubLink.setVisibility(View.GONE);
            return;
        }
        githubLink.setVisibility(View.VISIBLE);
        githubLink.setOnClickListener(v -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (ActivityNotFoundException exception) {
                statusView.setText(R.string.whats_new_browser_error);
            }
        });
    }

    private void showAndroidDownload(String version, String url) {
        if (compareVersions(version, Ui.versionName(this)) <= 0
                || url == null || !url.startsWith("https://github.com/ImElio/openmirrorphone/releases/download/")) {
            downloadLink.setVisibility(View.GONE);
            return;
        }
        downloadLink.setVisibility(View.VISIBLE);
        downloadLink.setOnClickListener(v -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (ActivityNotFoundException exception) {
                statusView.setText(R.string.whats_new_browser_error);
            }
        });
    }

    private static int compareVersions(String left, String right) {
        String[] leftParts = left.split("[.+-]");
        String[] rightParts = right.split("[.+-]");
        int length = Math.max(leftParts.length, rightParts.length);
        for (int i = 0; i < length; i++) {
            int leftPart = i < leftParts.length ? parseVersionPart(leftParts[i]) : 0;
            int rightPart = i < rightParts.length ? parseVersionPart(rightParts[i]) : 0;
            if (leftPart != rightPart) return Integer.compare(leftPart, rightPart);
        }
        return 0;
    }

    private static int parseVersionPart(String value) {
        int end = 0;
        while (end < value.length() && Character.isDigit(value.charAt(end))) end++;
        if (end == 0) return 0;
        try {
            return Integer.parseInt(value.substring(0, end));
        } catch (NumberFormatException ignored) {
            return 0;
        }
    }

    private static String markdownToPlainText(String markdown, String version) {
        StringBuilder output = new StringBuilder();
        String[] lines = markdown.replace("\r", "").split("\n");
        for (String original : lines) {
            String line = original.trim();
            if (line.startsWith("###")) line = line.substring(3).trim();
            else if (line.startsWith("##")) line = line.substring(2).trim();
            else if (line.startsWith("#")) line = line.substring(1).trim();
            String normalizedTitle = line.replace("’", "'").replace("'", "").toLowerCase(java.util.Locale.ROOT);
            String expectedTitle = ("What's new in OpenMirrorPhone " + version)
                    .replace("'", "").toLowerCase(java.util.Locale.ROOT);
            if (output.length() == 0 && normalizedTitle.equals(expectedTitle)) continue;
            if (line.startsWith("- ") || line.startsWith("* ")) line = "\u2022 " + line.substring(2);
            else if (line.startsWith("> ")) line = line.substring(2);
            line = line.replaceAll("!\\[([^\\]]*)\\]\\([^)]*\\)", "$1");
            line = line.replaceAll("\\[([^\\]]+)\\]\\(([^)]+)\\)", "$1");
            line = line.replace("**", "").replace("__", "").replace("`", "");
            if (line.isEmpty()) {
                if (output.length() > 0 && output.charAt(output.length() - 1) != '\n') output.append('\n');
            } else {
                output.append(line).append('\n');
            }
        }
        while (output.length() > 0 && output.charAt(output.length() - 1) == '\n') output.setLength(output.length() - 1);
        return output.toString();
    }
}
