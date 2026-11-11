package com.imelio.openmirrorphone;

import android.animation.ObjectAnimator;
import android.animation.PropertyValuesHolder;
import android.animation.ValueAnimator;
import android.annotation.SuppressLint;
import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.media.projection.MediaProjectionManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import com.google.zxing.integration.android.IntentIntegrator;
import com.google.zxing.integration.android.IntentResult;


public final class MainActivity extends Activity {
    private static final String PAYPAL_URL = "https://paypal.me/CallMeElio";
    private static final int REQUEST_CAPTURE = 41;
    private static final int REQUEST_NOTIFICATIONS = 42;
    private static final int REQUEST_AUDIO = 43;
    static final String APP_STATE = "app_state";
    static final String KEY_PC_ADDRESS = "pc_address";
    static final String KEY_REMEMBER_PC_ADDRESS = "remember_pc_address";
    static final String KEY_CAPTURE_APP_AUDIO = "capture_app_audio";

    private static final int IDLE_DOT = 0xFF5B6680;

    private TextView captureStatus;
    private View statusDot;
    private View statusHalo;
    private ObjectAnimator haloPulse;
    private Button captureButton;
    private EditText pcAddress;
    private boolean receiverRegistered;
    private boolean captureActive;
    private boolean permissionPending;
    private boolean captureAudio;

    private final BroadcastReceiver captureEvents = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!ScreenCaptureService.ACTION_STATUS.equals(intent.getAction())) return;
            String message = intent.getStringExtra(ScreenCaptureService.EXTRA_STATUS);
            boolean active = intent.getBooleanExtra(ScreenCaptureService.EXTRA_ACTIVE, false);
            String status = message == null ? getString(R.string.status_screen_sharing_off) : message;
            setCaptureState(active, withNotificationHint(active, status));
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Ui.Screen screen = Ui.screen(this);
        buildHome(screen.page);

        setContentView(screen.root);
    }

    @Override
    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    protected void onStart() {
        super.onStart();
        IntentFilter filter = new IntentFilter(ScreenCaptureService.ACTION_STATUS);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(captureEvents, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(captureEvents, filter);
        }
        receiverRegistered = true;
        if (ScreenCaptureService.isActive()) {
            int status = !ScreenCaptureService.isCapturedContentVisible()
                    ? R.string.status_capture_app_hidden
                    : ScreenCaptureService.isAudioUnavailable() ? R.string.status_audio_unavailable
                    : !ScreenCaptureService.isAudioCaptureEnabled() ? R.string.status_video_only
                    : R.string.status_capture_active;
            setCaptureState(true, withNotificationHint(true, getString(status)));
        }
    }

    @Override
    protected void onStop() {
        if (receiverRegistered) {
            unregisterReceiver(captureEvents);
            receiverRegistered = false;
        }
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        if (haloPulse != null) haloPulse.cancel();
        super.onDestroy();
    }

    // --- Layout ------------------------------------------------------------------

    private void buildHome(LinearLayout page) {
        add(page, buildTopBar(), 0);

        TextView title = Ui.text(this, getString(R.string.home_title), 30, Ui.TEXT_PRIMARY, Typeface.BOLD);
        title.setLineSpacing(0, 1.05f);
        TextView subtitle = Ui.text(this, getString(R.string.home_subtitle), 15, Ui.TEXT_SECONDARY, Typeface.NORMAL);
        subtitle.setLineSpacing(Ui.dp(this, 3), 1f);
        subtitle.setPadding(0, Ui.dp(this, 10), 0, 0);
        add(page, Ui.column(this, title, subtitle), 34);

        add(page, buildCaptureSection(), 24);

        TextView help = Ui.text(this, getString(R.string.home_help_link), 14, Ui.ACCENT, Typeface.BOLD);
        help.setGravity(Gravity.CENTER);
        help.setPadding(0, Ui.dp(this, 12), 0, Ui.dp(this, 12));
        help.setBackground(new RippleDrawable(ColorStateList.valueOf(0x263FA9FF), null,
                Ui.roundRect(this, 0xFFFFFFFF, 12)));
        help.setOnClickListener(v -> startActivity(new Intent(this, HelpActivity.class)));
        add(page, help, 10);

        // Keep the support link at the foot of the home screen when space permits.
        page.addView(new View(this), new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout support = new LinearLayout(this);
        support.setOrientation(LinearLayout.VERTICAL);
        support.setGravity(Gravity.CENTER);
        TextView supportTitle = Ui.text(this, getString(R.string.support_development), 13,
                Ui.TEXT_SECONDARY, Typeface.NORMAL);
        supportTitle.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        supportTitle.setGravity(Gravity.CENTER);
        support.addView(supportTitle, Ui.matchWrap());

        ImageButton paypal = new ImageButton(this);
        paypal.setImageResource(R.drawable.paypal_donate);
        paypal.setScaleType(ImageView.ScaleType.FIT_CENTER);
        paypal.setAdjustViewBounds(true);
        paypal.setPadding(0, 0, 0, 0);
        paypal.setContentDescription(getString(R.string.support_paypal_description));
        paypal.setBackground(new RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), null,
                Ui.roundRect(this, 0xFFFFFFFF, 12)));
        paypal.setOnClickListener(v -> openPayPal());
        LinearLayout.LayoutParams paypalParams = new LinearLayout.LayoutParams(Ui.dp(this, 172), Ui.dp(this, 48));
        paypalParams.topMargin = Ui.dp(this, 6);
        support.addView(paypal, paypalParams);
        add(page, support, 14);

    }

    private void openPayPal() {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(PAYPAL_URL)));
        } catch (ActivityNotFoundException error) {
            Toast.makeText(this, R.string.support_paypal_unavailable, Toast.LENGTH_LONG).show();
        }
    }

    private void add(LinearLayout page, View section, int topMarginDp) {
        page.addView(section, Ui.matchWrap(this, topMarginDp));
    }

    private View buildTopBar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);

        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.ic_openmirrorphone);
        logo.setContentDescription(getString(R.string.app_name));
        bar.addView(logo, new LinearLayout.LayoutParams(Ui.dp(this, 34), Ui.dp(this, 34)));

        SpannableString name = new SpannableString(getString(R.string.app_name));
        int start = name.toString().indexOf("Mirror");
        if (start >= 0) {
            name.setSpan(new ForegroundColorSpan(Ui.ACCENT), start, start + 6, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        TextView wordmark = Ui.text(this, "", 18, Ui.TEXT_PRIMARY, Typeface.BOLD);
        wordmark.setText(name);
        wordmark.setPadding(Ui.dp(this, 10), 0, 0, 0);
        bar.addView(wordmark, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        ImageButton settings = Ui.iconButton(this, R.drawable.ic_settings, getString(R.string.settings_title));
        settings.setOnClickListener(v -> startActivity(new Intent(this, SettingsActivity.class)));
        bar.addView(settings, new LinearLayout.LayoutParams(Ui.dp(this, 44), Ui.dp(this, 44)));
        return bar;
    }

    private View buildCaptureSection() {
        LinearLayout section = Ui.group(this);

        LinearLayout heading = new LinearLayout(this);
        heading.setOrientation(LinearLayout.HORIZONTAL);
        heading.setGravity(Gravity.CENTER_VERTICAL);
        ImageView icon = new ImageView(this);
        icon.setImageResource(R.drawable.ic_cast);
        icon.setScaleType(ImageView.ScaleType.CENTER);
        icon.setColorFilter(Ui.ACCENT);
        heading.addView(icon, new LinearLayout.LayoutParams(Ui.dp(this, 24), Ui.dp(this, 24)));

        TextView title = Ui.text(this, getString(R.string.capture_title), 17, Ui.TEXT_PRIMARY, Typeface.BOLD);
        // Status under the title: a dot that pulses while capture is running.
        LinearLayout stateRow = new LinearLayout(this);
        stateRow.setOrientation(LinearLayout.HORIZONTAL);
        stateRow.setGravity(Gravity.CENTER_VERTICAL);
        FrameLayout dotBox = new FrameLayout(this);
        statusHalo = new View(this);
        statusHalo.setBackground(Ui.circle(Ui.GREEN));
        statusHalo.setAlpha(0f);
        dotBox.addView(statusHalo, new FrameLayout.LayoutParams(Ui.dp(this, 8), Ui.dp(this, 8), Gravity.CENTER));
        statusDot = new View(this);
        statusDot.setBackground(Ui.circle(IDLE_DOT));
        dotBox.addView(statusDot, new FrameLayout.LayoutParams(Ui.dp(this, 8), Ui.dp(this, 8), Gravity.CENTER));
        stateRow.addView(dotBox, new LinearLayout.LayoutParams(Ui.dp(this, 18), Ui.dp(this, 18)));
        captureStatus = Ui.text(this, getString(R.string.status_screen_sharing_off), 13, Ui.TEXT_SECONDARY,
                Typeface.NORMAL);
        captureStatus.setPadding(Ui.dp(this, 4), 0, 0, 0);
        stateRow.addView(captureStatus, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        LinearLayout labels = Ui.column(this, title, stateRow);
        labels.setPadding(Ui.dp(this, 12), 0, 0, 0);
        heading.addView(labels, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        section.addView(heading, Ui.matchWrap());

        TextView description = Ui.text(this, getString(R.string.capture_description), 14, Ui.TEXT_SECONDARY,
                Typeface.NORMAL);
        description.setLineSpacing(Ui.dp(this, 2), 1f);
        description.setPadding(0, Ui.dp(this, 14), 0, 0);
        section.addView(description, Ui.matchWrap());

        TextView addressLabel = Ui.text(this, getString(R.string.pc_address_label), 13, Ui.TEXT_SECONDARY, Typeface.NORMAL);
        LinearLayout.LayoutParams labelParams = Ui.matchWrap();
        labelParams.topMargin = Ui.dp(this, 16);
        section.addView(addressLabel, labelParams);
        LinearLayout addressRow = new LinearLayout(this);
        addressRow.setOrientation(LinearLayout.HORIZONTAL);
        addressRow.setGravity(Gravity.CENTER_VERTICAL);
        pcAddress = new EditText(this);
        pcAddress.setSingleLine(true);
        pcAddress.setTextSize(15);
        pcAddress.setHint(R.string.pc_address_hint);
        pcAddress.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        pcAddress.setText(getPreferences().getBoolean(KEY_REMEMBER_PC_ADDRESS, true)
                ? getPreferences().getString(KEY_PC_ADDRESS, "") : "");
        LinearLayout.LayoutParams addressParams = new LinearLayout.LayoutParams(0, Ui.dp(this, 50), 1f);
        addressParams.topMargin = Ui.dp(this, 8);
        addressParams.rightMargin = Ui.dp(this, 8);
        addressRow.addView(pcAddress, addressParams);
        Button scanQr = new Button(this);
        scanQr.setAllCaps(false);
        scanQr.setText(R.string.action_scan_qr);
        scanQr.setTextSize(13);
        scanQr.setMinWidth(0);
        scanQr.setPadding(Ui.dp(this, 8), 0, Ui.dp(this, 8), 0);
        addressRow.addView(scanQr, new LinearLayout.LayoutParams(Ui.dp(this, 102), Ui.dp(this, 50)));
        scanQr.setOnClickListener(view -> scanReceiverQr());
        section.addView(addressRow, Ui.matchWrap());

        captureButton = new Button(this);
        captureButton.setAllCaps(false);
        captureButton.setTextSize(16);
        captureButton.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        captureButton.setStateListAnimator(null);
        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                Ui.dp(this, 56));
        buttonParams.topMargin = Ui.dp(this, 18);
        section.addView(captureButton, buttonParams);
        captureButton.setOnClickListener(view -> {
            if (permissionPending) return;
            if (captureActive) stopCapture();
            else requestCapture();
        });
        styleCaptureButton();
        return section;
    }

    // --- Screen capture ----------------------------------------------------------

    private void requestCapture() {
        permissionPending = true;
        captureStatus.setText(R.string.status_waiting_for_notifications);
        captureStatus.setTextColor(Ui.ACCENT);
        statusDot.setBackground(Ui.circle(Ui.ACCENT));
        styleCaptureButton();

        String host = pcAddress.getText().toString().trim();
        if (host.isEmpty()) {
            pcAddress.setError(getString(R.string.pc_address_required));
            permissionPending = false;
            setCaptureState(false, getString(R.string.pc_address_required));
            return;
        }
        if (getPreferences().getBoolean(KEY_REMEMBER_PC_ADDRESS, true)) {
            getPreferences().edit().putString(KEY_PC_ADDRESS, host).apply();
        } else {
            getPreferences().edit().remove(KEY_PC_ADDRESS).apply();
        }
        if (Build.VERSION.SDK_INT >= 33 && !hasNotificationPermission()) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQUEST_NOTIFICATIONS);
            return;
        }
        requestAudioPermission();
    }

    private void scanReceiverQr() {
        IntentIntegrator scanner = new IntentIntegrator(this);
        scanner.setDesiredBarcodeFormats(IntentIntegrator.QR_CODE);
        scanner.setPrompt(getString(R.string.scan_qr_prompt));
        scanner.setCaptureActivity(ReceiverQrCaptureActivity.class);
        scanner.setBeepEnabled(false);
        scanner.setOrientationLocked(true);
        scanner.initiateScan();
    }

    private void requestAudioPermission() {
        if (!getPreferences().getBoolean(KEY_CAPTURE_APP_AUDIO, true)) {
            captureAudio = false;
            requestProjectionPermission();
            return;
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            captureAudio = true;
            requestProjectionPermission();
        } else {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQUEST_AUDIO);
        }
    }

    private android.content.SharedPreferences getPreferences() {
        return getSharedPreferences(APP_STATE, MODE_PRIVATE);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (pcAddress != null && !getPreferences().getBoolean(KEY_REMEMBER_PC_ADDRESS, true)) {
            pcAddress.setText("");
        }
    }

    private void requestProjectionPermission() {
        captureStatus.setText(R.string.status_waiting_for_approval);
        MediaProjectionManager manager = (MediaProjectionManager) getSystemService(MEDIA_PROJECTION_SERVICE);
        startActivityForResult(manager.createScreenCaptureIntent(), REQUEST_CAPTURE);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_NOTIFICATIONS) {
            if (!hasNotificationPermission()) {
                Toast.makeText(this, R.string.status_notifications_disabled, Toast.LENGTH_LONG).show();
            }
            requestAudioPermission();
            return;
        }
        if (requestCode == REQUEST_AUDIO) {
            if (checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                    == android.content.pm.PackageManager.PERMISSION_GRANTED) {
                captureAudio = true;
                requestProjectionPermission();
            } else {
                new AlertDialog.Builder(this)
                        .setTitle(R.string.audio_permission_denied_title)
                        .setMessage(R.string.audio_permission_denied_message)
                        .setPositiveButton(R.string.action_continue_video_only, (dialog, which) -> {
                            captureAudio = false;
                            requestProjectionPermission();
                        })
                        .setNegativeButton(android.R.string.cancel, (dialog, which) ->
                                setCaptureState(false, getString(R.string.status_screen_sharing_off)))
                        .setOnCancelListener(dialog ->
                                setCaptureState(false, getString(R.string.status_screen_sharing_off)))
                        .show();
            }
        }
    }

    private boolean hasNotificationPermission() {
        return Build.VERSION.SDK_INT < 33
                || checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    private String withNotificationHint(boolean active, String message) {
        if (active && !hasNotificationPermission()) {
            return message + " " + getString(R.string.status_notifications_disabled);
        }
        return message;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        IntentResult scan = IntentIntegrator.parseActivityResult(requestCode, resultCode, data);
        if (scan != null) {
            if (scan.getContents() != null) {
                String address = scan.getContents().trim();
                if (isValidReceiverAddress(address)) {
                    pcAddress.setText(address);
                    pcAddress.setError(null);
                    if (getPreferences().getBoolean(KEY_REMEMBER_PC_ADDRESS, true)) {
                        getPreferences().edit().putString(KEY_PC_ADDRESS, address).apply();
                    }
                    Toast.makeText(this, R.string.scan_qr_saved, Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(this, R.string.scan_qr_invalid, Toast.LENGTH_LONG).show();
                }
            }
            return;
        }
        if (requestCode != REQUEST_CAPTURE) return;
        if (resultCode != RESULT_OK || data == null) {
            setCaptureState(false, getString(R.string.status_capture_permission_denied));
            return;
        }
        Intent service = new Intent(this, ScreenCaptureService.class)
                .setAction(ScreenCaptureService.ACTION_START)
                .putExtra(ScreenCaptureService.EXTRA_RESULT_CODE, resultCode)
                .putExtra(ScreenCaptureService.EXTRA_RESULT_DATA, data)
                .putExtra(ScreenCaptureService.EXTRA_HOST, pcAddress.getText().toString().trim())
                .putExtra(ScreenCaptureService.EXTRA_CAPTURE_AUDIO, captureAudio);
        startForegroundService(service);
    }

    private static boolean isValidReceiverAddress(String address) {
        String[] parts = address.split(":", -1);
        if (parts.length != 2 || parts[0].isEmpty()) return false;
        String[] octets = parts[0].split("\\.", -1);
        if (octets.length != 4) return false;
        try {
            for (String octet : octets) {
                int value = Integer.parseInt(octet);
                if (value < 0 || value > 255) return false;
            }
            int port = Integer.parseInt(parts[1]);
            return port > 0 && port <= 65535;
        } catch (NumberFormatException ignored) {
            return false;
        }
    }

    private void stopCapture() {
        startService(new Intent(this, ScreenCaptureService.class).setAction(ScreenCaptureService.ACTION_STOP));
        setCaptureState(false, getString(R.string.status_screen_sharing_stopped));
    }

    private void setCaptureState(boolean active, String message) {
        captureActive = active;
        permissionPending = false;
        if (captureStatus != null) {
            boolean sourceHidden = active && message.startsWith(getString(R.string.status_capture_app_hidden));
            int statusColor = !active ? Ui.TEXT_SECONDARY : sourceHidden ? 0xFFFFC857 : Ui.GREEN;
            captureStatus.setText(active && !sourceHidden ? "" : message);
            captureStatus.setTextColor(statusColor);
            statusDot.setBackground(Ui.circle(active ? statusColor : IDLE_DOT));
            pulseHalo(active && !sourceHidden);
        }
        if (captureButton != null) styleCaptureButton();
    }

    private void pulseHalo(boolean on) {
        if (haloPulse != null) haloPulse.cancel();
        statusHalo.setScaleX(1f);
        statusHalo.setScaleY(1f);
        statusHalo.setAlpha(0f);
        if (!on || !ValueAnimator.areAnimatorsEnabled()) return;
        haloPulse = ObjectAnimator.ofPropertyValuesHolder(statusHalo,
                PropertyValuesHolder.ofFloat(View.SCALE_X, 1f, 2.2f),
                PropertyValuesHolder.ofFloat(View.SCALE_Y, 1f, 2.2f),
                PropertyValuesHolder.ofFloat(View.ALPHA, 0.55f, 0f));
        haloPulse.setDuration(1400);
        haloPulse.setRepeatCount(ValueAnimator.INFINITE);
        haloPulse.start();
    }

    /** Plain primary action; stopping uses a subtle red treatment. */
    private void styleCaptureButton() {
        if (permissionPending) {
            captureButton.setText(R.string.action_waiting_for_approval);
            captureButton.setEnabled(false);
            captureButton.setAlpha(0.6f);
            return;
        }
        captureButton.setEnabled(true);
        captureButton.setAlpha(1f);
        if (captureActive) {
            GradientDrawable stop = Ui.roundRect(this, 0xFF292326, 8);
            captureButton.setBackground(Ui.ripple(stop));
            captureButton.setTextColor(Ui.RED);
            captureButton.setText(R.string.action_stop_capture);
        } else {
            captureButton.setBackground(Ui.ripple(Ui.roundRect(this, 0xFF2878D0, 8)));
            captureButton.setTextColor(0xFFFFFFFF);
            captureButton.setText(R.string.action_test_capture);
        }
    }
}
