package com.imelio.openmirrorphone;

import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.Manifest;
import android.content.Context;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioPlaybackCaptureConfiguration;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.projection.MediaProjection;
import android.os.Build;
import android.os.Process;
import android.content.pm.PackageManager;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Surface;

import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicInteger;

/** Encodes the projected display as H.264 and sends access units over the local network. */
final class AndroidStreamSender {
    interface Listener {
        void onConnected();
        void onError(String message);
        void onAudioUnavailable(String message);
    }

    private static final int PORT = 24862;
    private static final int TARGET_FPS = 30;
    private static final int AUDIO_SAMPLE_RATE = 44100;
    private static final int AUDIO_FRAME_BYTES = 4;
    private static final int AUDIO_BUFFER_BYTES = AUDIO_SAMPLE_RATE * AUDIO_FRAME_BYTES / 50;
    private static final int PACKET_VIDEO = 1;
    private static final int PACKET_AUDIO = 2;
    private static final int PACKET_VISIBILITY = 3;
    private static final String TAG = "OMPStream";
    private final MediaProjection projection;
    private final Context context;
    private final DisplayMetrics metrics;
    private final String host;
    private final Listener listener;
    private final boolean captureAudio;
    private volatile boolean stopped;
    private volatile Socket socket;
    private volatile AudioRecord audioRecord;
    private volatile Thread audioThread;
    private final Object outputLock = new Object();
    private boolean audioUnavailableReported;
    private final AtomicInteger capturedContentVisible = new AtomicInteger(-1);
    private Thread worker;

    AndroidStreamSender(Context context, MediaProjection projection, DisplayMetrics metrics, String host,
                        boolean captureAudio, Listener listener) {
        this.context = context.getApplicationContext();
        this.projection = projection;
        this.metrics = metrics;
        this.host = host;
        this.captureAudio = captureAudio;
        this.listener = listener;
    }

    void start() {
        worker = new Thread(this::run, "OpenMirrorPhone-video-sender");
        worker.start();
    }

    void stop() {
        stopped = true;
        Socket current = socket;
        if (current != null) {
            try { current.close(); } catch (IOException ignored) { }
        }
        stopAudioCapture();
        if (worker != null) worker.interrupt();
    }

    void setCapturedContentVisible(boolean visible) {
        capturedContentVisible.set(visible ? 1 : 0);
    }

    private void run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY);
        MediaCodec encoder = null;
        Surface input = null;
        VirtualDisplay display = null;
        try (Socket connection = new Socket()) {
            socket = connection;
            connection.connect(parseAddress(host), 5000);
            connection.setTcpNoDelay(true);
            DataOutputStream output = new DataOutputStream(new BufferedOutputStream(connection.getOutputStream(), 256 * 1024));
            output.writeBytes("OMPH");
            output.writeInt(2);
            output.flush();
            if (captureAudio) startAudioCapture(output);

            boolean emulator = Build.FINGERPRINT.startsWith("generic")
                    || Build.MODEL.toLowerCase(java.util.Locale.US).contains("emulator")
                    || Build.HARDWARE.contains("ranchu") || Build.HARDWARE.contains("goldfish");
            int maxDimension = emulator ? 960 : 1280;
            float scale = Math.min(1f, maxDimension / (float) Math.max(metrics.widthPixels, metrics.heightPixels));
            int width = Math.max(2, ((int) (metrics.widthPixels * scale)) & ~1);
            int height = Math.max(2, ((int) (metrics.heightPixels * scale)) & ~1);
            MediaFormat format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            format.setInteger(MediaFormat.KEY_BIT_RATE, emulator ? 2_500_000 : 5_000_000);
            format.setInteger(MediaFormat.KEY_FRAME_RATE, TARGET_FPS);
            format.setInteger(MediaFormat.KEY_PRIORITY, 0); // realtime encoder workload
            format.setInteger(MediaFormat.KEY_MAX_FPS_TO_ENCODER, TARGET_FPS);
            format.setInteger(MediaFormat.KEY_LATENCY, 1);
            format.setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0);
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
            encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
            encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            input = encoder.createInputSurface();
            encoder.start();
            Log.i(TAG, "Encoder " + encoder.getName() + " configured for " + width + "x" + height
                    + " at " + TARGET_FPS + " fps; " + encoder.getOutputFormat());
            display = projection.createVirtualDisplay("OpenMirrorPhoneAndroid", width, height, metrics.densityDpi,
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, input, null, null);
            if (display == null) throw new IOException("Android could not create a virtual display.");

            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean reportedConnected = false;
            boolean loggedFirstFrame = false;
            int sentVisibility = -1;
            long fpsWindowStart = System.nanoTime();
            int fpsWindowFrames = 0;
            while (!stopped) {
                int currentVisibility = capturedContentVisible.get();
                if (currentVisibility >= 0 && currentVisibility != sentVisibility) {
                    Log.i(TAG, "Sending captured content visibility: " + (currentVisibility != 0));
                    sendPacket(output, PACKET_VISIBILITY, new byte[]{(byte) currentVisibility}, 1);
                    sentVisibility = currentVisibility;
                }
                int index = encoder.dequeueOutputBuffer(info, 100_000);
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    MediaFormat actual = encoder.getOutputFormat();
                    for (String key : new String[]{"csd-0", "csd-1"}) {
                        ByteBuffer csd = actual.getByteBuffer(key);
                        if (csd != null && csd.hasRemaining()) {
                            byte[] packet = toAnnexB(csd);
                            sendPacket(output, PACKET_VIDEO, packet, packet.length);
                        }
                    }
                } else if (index >= 0) {
                    ByteBuffer buffer = encoder.getOutputBuffer(index);
                    if (buffer != null && info.size > 0) {
                        buffer.position(info.offset);
                        buffer.limit(info.offset + info.size);
                        byte[] bytes = new byte[info.size];
                        buffer.get(bytes);
                        byte[] packet = toAnnexB(ByteBuffer.wrap(bytes));
                        sendPacket(output, PACKET_VIDEO, packet, packet.length);
                        if (!reportedConnected && (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                            reportedConnected = true;
                            if (!loggedFirstFrame) {
                                loggedFirstFrame = true;
                                Log.i(TAG, "First H.264 video access unit sent: " + packet.length + " bytes");
                            }
                            listener.onConnected();
                        }
                        if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                            fpsWindowFrames++;
                            long now = System.nanoTime();
                            long elapsed = now - fpsWindowStart;
                            if (elapsed >= 3_000_000_000L) {
                                Log.i(TAG, String.format(java.util.Locale.US, "Encoded %.1f fps", fpsWindowFrames * 1_000_000_000f / elapsed));
                                fpsWindowFrames = 0;
                                fpsWindowStart = now;
                            }
                        }
                    }
                    encoder.releaseOutputBuffer(index, false);
                }
            }
        } catch (Exception error) {
            if (!stopped) listener.onError(error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage());
        } finally {
            socket = null;
            stopAudioCapture();
            if (display != null) display.release();
            if (encoder != null) {
                try { encoder.stop(); } catch (RuntimeException ignored) { }
                encoder.release();
            }
            if (input != null) input.release();
        }
    }

    private void startAudioCapture(DataOutputStream output) {
        AudioRecord record = null;
        try {
            if (context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                throw new SecurityException("Android audio permission was not granted.");
            }
            AudioPlaybackCaptureConfiguration config = new AudioPlaybackCaptureConfiguration.Builder(projection)
                    .addMatchingUsage(android.media.AudioAttributes.USAGE_MEDIA)
                    .addMatchingUsage(android.media.AudioAttributes.USAGE_GAME)
                    .addMatchingUsage(android.media.AudioAttributes.USAGE_UNKNOWN)
                    .build();
            AudioFormat format = new AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(AUDIO_SAMPLE_RATE)
                    .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                    .build();
            int minBuffer = AudioRecord.getMinBufferSize(AUDIO_SAMPLE_RATE, AudioFormat.CHANNEL_IN_STEREO,
                    AudioFormat.ENCODING_PCM_16BIT);
            if (minBuffer <= 0) throw new IllegalStateException("Android could not create an audio capture buffer.");
            record = new AudioRecord.Builder()
                    .setAudioFormat(format)
                    .setBufferSizeInBytes(Math.max(minBuffer * 2, AUDIO_BUFFER_BYTES * 4))
                    .setAudioPlaybackCaptureConfig(config)
                    .build();
            if (record.getState() != AudioRecord.STATE_INITIALIZED) {
                throw new IllegalStateException("Android could not initialize app audio capture.");
            }
            record.startRecording();
            if (stopped) {
                record.stop();
                record.release();
                return;
            }
            AudioRecord activeRecord = record;
            audioRecord = activeRecord;
            audioThread = new Thread(() -> sendAudio(activeRecord, output), "OpenMirrorPhone-audio-sender");
            audioThread.start();
            Log.i(TAG, "Android app playback audio capture started at " + AUDIO_SAMPLE_RATE + " Hz");
        } catch (Exception error) {
            if (record != null) record.release();
            reportAudioUnavailable(error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage());
        }
    }

    private void sendAudio(AudioRecord record, DataOutputStream output) {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
        byte[] buffer = new byte[AUDIO_BUFFER_BYTES];
        long reportAt = System.nanoTime() + 3_000_000_000L;
        int capturedBytes = 0;
        int peak = 0;
        try {
            while (!stopped) {
                int size = record.read(buffer, 0, buffer.length, AudioRecord.READ_BLOCKING);
                if (size > 0) {
                    int alignedSize = size - size % AUDIO_FRAME_BYTES;
                    if (alignedSize > 0) {
                        for (int i = 0; i + 1 < alignedSize; i += 2) {
                            int sample = Math.abs((short) ((buffer[i] & 255) | (buffer[i + 1] << 8)));
                            if (sample > peak) peak = sample;
                        }
                        byte[] packet = Arrays.copyOf(buffer, alignedSize);
                        sendPacket(output, PACKET_AUDIO, packet, packet.length);
                        capturedBytes += alignedSize;
                        long now = System.nanoTime();
                        if (now >= reportAt) {
                            Log.i(TAG, "Captured app playback audio: " + capturedBytes + " bytes, peak " + peak);
                            capturedBytes = 0;
                            peak = 0;
                            reportAt = now + 3_000_000_000L;
                        }
                    }
                } else {
                    throw new IllegalStateException("Android stopped app audio capture (" + size + ").");
                }
            }
        } catch (Exception error) {
            if (!stopped) {
                reportAudioUnavailable(error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage());
            }
        } finally {
            if (audioRecord == record) audioRecord = null;
            try {
                if (record.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) record.stop();
            } catch (RuntimeException ignored) { }
            record.release();
        }
    }

    private void stopAudioCapture() {
        AudioRecord record = audioRecord;
        if (record != null) {
            try {
                if (record.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) record.stop();
            } catch (RuntimeException ignored) { }
        }
        Thread thread = audioThread;
        if (thread != null && thread != Thread.currentThread()) {
            try {
                thread.join(500);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private void reportAudioUnavailable(String message) {
        synchronized (this) {
            if (audioUnavailableReported || stopped) return;
            audioUnavailableReported = true;
        }
        Log.w(TAG, "App playback audio is unavailable: " + message);
        listener.onAudioUnavailable(message);
    }

    private static InetSocketAddress parseAddress(String address) {
        String host = address.trim();
        int port = PORT;
        if (host.startsWith("[")) {
            int closingBracket = host.indexOf(']');
            if (closingBracket <= 1) throw new IllegalArgumentException("Enter a valid PC address.");
            String suffix = host.substring(closingBracket + 1);
            if (!suffix.isEmpty()) {
                if (!suffix.startsWith(":")) throw new IllegalArgumentException("Enter a valid PC address.");
                port = Integer.parseInt(suffix.substring(1));
            }
            host = host.substring(1, closingBracket);
        } else {
            int colon = host.lastIndexOf(':');
            if (colon > 0 && colon == host.indexOf(':')) {
                port = Integer.parseInt(host.substring(colon + 1));
                host = host.substring(0, colon);
            }
        }
        if (host.isEmpty() || port < 1 || port > 65535) {
            throw new IllegalArgumentException("Enter a valid PC address.");
        }
        return new InetSocketAddress(host, port);
    }

    private static byte[] toAnnexB(ByteBuffer source) {
        ByteBuffer data = source.duplicate();
        byte[] bytes = new byte[data.remaining()];
        data.get(bytes);
        if (bytes.length >= 4 && bytes[0] == 0 && bytes[1] == 0 && (bytes[2] == 1 || (bytes[2] == 0 && bytes[3] == 1))) {
            return bytes;
        }
        java.io.ByteArrayOutputStream result = new java.io.ByteArrayOutputStream(bytes.length + 32);
        int offset = 0;
        while (offset + 4 <= bytes.length) {
            int length = ((bytes[offset] & 255) << 24) | ((bytes[offset + 1] & 255) << 16)
                    | ((bytes[offset + 2] & 255) << 8) | (bytes[offset + 3] & 255);
            offset += 4;
            if (length <= 0 || length > bytes.length - offset) {
                result.reset();
                result.write(bytes, 0, bytes.length);
                break;
            }
            result.write(new byte[]{0, 0, 0, 1}, 0, 4);
            result.write(bytes, offset, length);
            offset += length;
        }
        return result.toByteArray();
    }

    private void sendPacket(DataOutputStream output, int type, byte[] bytes, int size) throws IOException {
        if (size <= 0 || stopped) return;
        synchronized (outputLock) {
            if (stopped) return;
            output.writeByte(type);
            output.writeInt(size);
            output.write(bytes, 0, size);
            output.flush();
        }
    }
}
