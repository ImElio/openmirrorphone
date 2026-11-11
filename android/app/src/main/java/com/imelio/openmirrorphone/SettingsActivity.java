package com.imelio.openmirrorphone;

import android.app.Activity;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.drawable.RippleDrawable;
import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;

/** Settings: help, release notes, and app information. */
public final class SettingsActivity extends Activity {
    private static final String PREFS = MainActivity.APP_STATE;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Ui.Screen screen = Ui.screen(this);
        LinearLayout page = screen.page;
        page.addView(Ui.header(this, getString(R.string.settings_title)), Ui.matchWrap());

        page.addView(Ui.sectionLabel(this, getString(R.string.settings_capture)), Ui.matchWrap(this, 24));
        addSwitchPreference(page, R.string.setting_capture_audio_title, R.string.setting_capture_audio_text,
                MainActivity.KEY_CAPTURE_APP_AUDIO, true);
        page.addView(Ui.divider(this), new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, Ui.dp(this, 1)));
        addSwitchPreference(page, R.string.setting_remember_address_title, R.string.setting_remember_address_text,
                MainActivity.KEY_REMEMBER_PC_ADDRESS, true);

        // Help
        page.addView(Ui.sectionLabel(this, getString(R.string.help_title)), Ui.matchWrap(this, 24));
        LinearLayout howRow = Ui.row(this, R.drawable.ic_help, getString(R.string.steps_title),
                getString(R.string.settings_help_text), Ui.chevron(this));
        howRow.setOnClickListener(v -> startActivity(new Intent(this, HelpActivity.class)));
        page.addView(howRow, Ui.matchWrap());

        // Latest stable release notes, fetched from GitHub.
        page.addView(Ui.divider(this), new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, Ui.dp(this, 1)));
        page.addView(Ui.sectionLabel(this, getString(R.string.settings_updates)), Ui.matchWrap(this, 24));
        LinearLayout whatsNew = Ui.row(this, R.drawable.ic_notification,
                getString(R.string.settings_whats_new_title), getString(R.string.settings_whats_new_text),
                Ui.chevron(this));
        whatsNew.setOnClickListener(v -> startActivity(new Intent(this, WhatsNewActivity.class)));
        page.addView(whatsNew, Ui.matchWrap());

        // About
        page.addView(Ui.divider(this), new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, Ui.dp(this, 1)));
        page.addView(Ui.sectionLabel(this, getString(R.string.settings_about)), Ui.matchWrap(this, 24));
        page.addView(Ui.row(this, R.drawable.ic_info, getString(R.string.app_name),
                getString(R.string.settings_version, Ui.versionName(this)), null), Ui.matchWrap());

        setContentView(screen.root);
    }

    private void addSwitchPreference(LinearLayout page, int titleId, int subtitleId,
                                     String key, boolean defaultValue) {
        SharedPreferences preferences = getSharedPreferences(PREFS, MODE_PRIVATE);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, Ui.dp(this, 10), 0, Ui.dp(this, 10));
        row.setMinimumHeight(Ui.dp(this, 68));
        row.setBackground(new RippleDrawable(ColorStateList.valueOf(0x26FFFFFF), null,
                Ui.roundRect(this, 0xFFFFFFFF, 4)));

        TextView title = Ui.text(this, getString(titleId), 15, Ui.TEXT_PRIMARY, android.graphics.Typeface.BOLD);
        TextView subtitle = Ui.text(this, getString(subtitleId), 13, Ui.TEXT_SECONDARY,
                android.graphics.Typeface.NORMAL);
        subtitle.setPadding(0, Ui.dp(this, 3), 0, 0);
        LinearLayout labels = Ui.column(this, title, subtitle);
        labels.setPadding(0, 0, Ui.dp(this, 12), 0);
        row.addView(labels, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Switch toggle = new Switch(this);
        toggle.setContentDescription(getString(titleId));
        toggle.setChecked(preferences.getBoolean(key, defaultValue));
        toggle.setOnCheckedChangeListener((button, checked) -> {
            preferences.edit().putBoolean(key, checked).apply();
            if (MainActivity.KEY_REMEMBER_PC_ADDRESS.equals(key) && !checked) {
                preferences.edit().remove(MainActivity.KEY_PC_ADDRESS).apply();
            }
        });
        row.addView(toggle, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        row.setOnClickListener(view -> toggle.setChecked(!toggle.isChecked()));
        page.addView(row, Ui.matchWrap());
    }
}
