package com.imelio.openmirrorphone;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.hardware.display.VirtualDisplay;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.WindowManager;

public final class ScreenCaptureService extends Service {
    private static final String TAG = "OMPCapture";
    public static final String ACTION_START = "com.imelio.openmirrorphone.START_CAPTURE";
    public static final String ACTION_STOP = "com.imelio.openmirrorphone.STOP_CAPTURE";
    public static final String ACTION_STATUS = "com.imelio.openmirrorphone.CAPTURE_STATUS";
    public static final String EXTRA_STATUS = "status";
    public static final String EXTRA_ACTIVE = "active";
    public static final String EXTRA_RESULT_CODE = "result_code";
    public static final String EXTRA_RESULT_DATA = "result_data";
    public static final String EXTRA_HOST = "host";
    public static final String EXTRA_CAPTURE_AUDIO = "capture_audio";

    private static final String CHANNEL_ID = "screen_capture";
    private static final int NOTIFICATION_ID = 7;
    private static volatile boolean active;
    private static volatile boolean capturedContentVisible = true;
    private static volatile boolean captureAudio;
    private static volatile boolean audioUnavailable;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private MediaProjection projection;
    private VirtualDisplay virtualDisplay;
    private AndroidStreamSender sender;
    private boolean stopped;

    public static boolean isActive() {
        return active;
    }

    public static boolean isCapturedContentVisible() {
        return capturedContentVisible;
    }

    public static boolean isAudioUnavailable() {
        return audioUnavailable;
    }

    public static boolean isAudioCaptureEnabled() {
        return captureAudio;
    }

    private final MediaProjection.Callback projectionCallback = new MediaProjection.Callback() {
        @Override
        public void onCapturedContentVisibilityChanged(boolean isVisible) {
            Log.i(TAG, "Captured content visibility changed: " + isVisible);
            capturedContentVisible = isVisible;
            if (sender != null) sender.setCapturedContentVisible(isVisible);
            if (active) {
                String message = getString(isVisible ? captureStatusResource() : R.string.status_capture_app_hidden);
                publishStatus(message, true);
                updateForegroundNotification(message);
            }
        }

        @Override
        public void onStop() {
            stopCapture(getString(R.string.status_stopped_by_android));
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || ACTION_STOP.equals(intent.getAction())) {
            stopCapture(getString(R.string.status_screen_sharing_stopped));
            return START_NOT_STICKY;
        }
        if (!ACTION_START.equals(intent.getAction()) || projection != null) return START_NOT_STICKY;

        startForeground(NOTIFICATION_ID, buildNotification(getString(R.string.capture_notification_text)),
                android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);

        int resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, 0);
        String host = intent.getStringExtra(EXTRA_HOST);
        captureAudio = intent.getBooleanExtra(EXTRA_CAPTURE_AUDIO, false)
                && checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                == android.content.pm.PackageManager.PERMISSION_GRANTED;
        audioUnavailable = !captureAudio;
        if (host == null || host.trim().isEmpty()) {
            stopCapture(getString(R.string.status_pc_address_missing));
            return START_NOT_STICKY;
        }
        Intent resultData = getResultData(intent);
        if (resultData == null) {
            stopCapture(getString(R.string.status_capture_access_missing));
            return START_NOT_STICKY;
        }

        try {
            MediaProjectionManager manager = (MediaProjectionManager) getSystemService(MEDIA_PROJECTION_SERVICE);
            projection = manager.getMediaProjection(resultCode, resultData);
            if (projection == null) {
                stopCapture(getString(R.string.status_capture_access_missing));
                return START_NOT_STICKY;
            }
            projection.registerCallback(projectionCallback, mainHandler);
            startCapture(host.trim());
        } catch (RuntimeException error) {
            stopCapture(getString(R.string.status_capture_start_failed, error.getMessage()));
        }
        return START_NOT_STICKY;
    }

    @SuppressWarnings("deprecation")
    private Intent getResultData(Intent intent) {
        if (Build.VERSION.SDK_INT >= 33) return intent.getParcelableExtra(EXTRA_RESULT_DATA, Intent.class);
        return intent.getParcelableExtra(EXTRA_RESULT_DATA);
    }

    private void startCapture(String host) {
        stopped = false;
        capturedContentVisible = true;
        DisplayMetrics metrics = new DisplayMetrics();
        WindowManager windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        windowManager.getDefaultDisplay().getRealMetrics(metrics);
        publishStatus(getString(R.string.status_connecting_to_pc), false);
        sender = new AndroidStreamSender(getApplicationContext(), projection, metrics, host, captureAudio,
                new AndroidStreamSender.Listener() {
            @Override public void onConnected() {
                mainHandler.post(() -> {
                    if (stopped) return;
                    active = true;
                    String message = getString(captureStatusResource());
                    publishStatus(message, true);
                    updateForegroundNotification(message);
                });
            }
            @Override public void onError(String message) {
                mainHandler.post(() -> stopCapture(getString(R.string.status_connection_failed) + " " + message));
            }
            @Override public void onAudioUnavailable(String message) {
                mainHandler.post(() -> {
                    if (stopped) return;
                    audioUnavailable = true;
                    if (active && capturedContentVisible) {
                        String status = getString(R.string.status_audio_unavailable);
                        publishStatus(status, true);
                        updateForegroundNotification(status);
                    }
                });
            }
        });
        sender.setCapturedContentVisible(capturedContentVisible);
        sender.start();
    }

    private int captureStatusResource() {
        if (!captureAudio) return R.string.status_video_only;
        if (audioUnavailable) return R.string.status_audio_unavailable;
        return R.string.status_capture_active;
    }

    private void updateForegroundNotification(String message) {
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.notify(NOTIFICATION_ID, buildNotification(message));
    }

    private Notification buildNotification(String message) {
        Intent stop = new Intent(this, ScreenCaptureService.class).setAction(ACTION_STOP);
        PendingIntent stopPending = PendingIntent.getService(this, 8, stop,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_notification)
                .setContentTitle(getString(R.string.capture_notification_title))
                .setContentText(message)
                .setCategory(Notification.CATEGORY_SERVICE)
                .setOngoing(true)
                .addAction(new Notification.Action.Builder(null, "Stop", stopPending).build())
                .build();
    }

    private void createNotificationChannel() {
        NotificationChannel channel = new NotificationChannel(CHANNEL_ID, getString(R.string.capture_channel_name),
                NotificationManager.IMPORTANCE_LOW);
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(channel);
    }

    private void publishStatus(String message, boolean active) {
        Intent event = new Intent(ACTION_STATUS).setPackage(getPackageName())
                .putExtra(EXTRA_STATUS, message)
                .putExtra(EXTRA_ACTIVE, active);
        sendBroadcast(event);
    }

    private void stopCapture(String message) {
        if (stopped) return;
        stopped = true;
        active = false;
        capturedContentVisible = true;
        if (projection != null) {
            projection.unregisterCallback(projectionCallback);
            projection.stop();
            projection = null;
        }
        if (sender != null) {
            sender.stop();
            sender = null;
        }
        if (virtualDisplay != null) {
            virtualDisplay.release();
            virtualDisplay = null;
        }
        publishStatus(message, false);
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    @Override
    public void onDestroy() {
        stopCapture(getString(R.string.status_screen_sharing_stopped));
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
