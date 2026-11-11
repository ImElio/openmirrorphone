package com.imelio.openmirrorphone;

import android.view.View;

import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import com.journeyapps.barcodescanner.CaptureActivity;
import com.journeyapps.barcodescanner.DecoratedBarcodeView;

/** Portrait QR scanner used to save a Windows receiver address. */
public final class ReceiverQrCaptureActivity extends CaptureActivity {
    @Override
    protected DecoratedBarcodeView initializeContent() {
        setContentView(R.layout.activity_scan_receiver_qr);

        DecoratedBarcodeView scanner = findViewById(R.id.zxing_barcode_scanner);
        if (scanner.getStatusView() != null) {
            scanner.getStatusView().setVisibility(View.GONE);
        }

        View header = findViewById(R.id.scan_header);
        int baseTopPadding = header.getPaddingTop();
        ViewCompat.setOnApplyWindowInsetsListener(header, (view, insets) -> {
            int statusBarInset = insets.getInsets(WindowInsetsCompat.Type.statusBars()).top;
            view.setPadding(view.getPaddingLeft(), baseTopPadding + statusBarInset,
                    view.getPaddingRight(), view.getPaddingBottom());
            return insets;
        });
        ViewCompat.requestApplyInsets(header);

        findViewById(R.id.scan_close).setOnClickListener(view -> finish());
        return scanner;
    }
}
