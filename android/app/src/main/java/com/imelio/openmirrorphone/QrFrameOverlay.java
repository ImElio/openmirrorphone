package com.imelio.openmirrorphone;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

/** Draws a simple, square alignment guide over the camera preview. */
public final class QrFrameOverlay extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint maskPaint = new Paint();
    private final float density;

    public QrFrameOverlay(Context context, AttributeSet attrs) {
        super(context, attrs);
        density = getResources().getDisplayMetrics().density;
        paint.setColor(0xFFFFFFFF);
        paint.setStrokeWidth(3 * density);
        paint.setStrokeCap(Paint.Cap.BUTT);
        paint.setStyle(Paint.Style.STROKE);
        maskPaint.setColor(0x99000000);
        maskPaint.setStyle(Paint.Style.FILL);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        float side = Math.min(280 * density, Math.min(getWidth(), getHeight()) - 48 * density);
        float left = (getWidth() - side) / 2f;
        float top = (getHeight() - side) / 2f;
        float right = left + side;
        float bottom = top + side;
        float corner = 28 * density;

        canvas.drawRect(0, 0, getWidth(), top, maskPaint);
        canvas.drawRect(0, bottom, getWidth(), getHeight(), maskPaint);
        canvas.drawRect(0, top, left, bottom, maskPaint);
        canvas.drawRect(right, top, getWidth(), bottom, maskPaint);

        canvas.drawLine(left, top, left + corner, top, paint);
        canvas.drawLine(left, top, left, top + corner, paint);
        canvas.drawLine(right - corner, top, right, top, paint);
        canvas.drawLine(right, top, right, top + corner, paint);
        canvas.drawLine(left, bottom - corner, left, bottom, paint);
        canvas.drawLine(left, bottom, left + corner, bottom, paint);
        canvas.drawLine(right - corner, bottom, right, bottom, paint);
        canvas.drawLine(right, bottom - corner, right, bottom, paint);
    }
}
