package com.imelio.openmirrorphone;

import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;


/** Colours and building blocks shared by the app's screens, which are all built in code. */
final class Ui {
    // Neutral Android dark palette; the logo provides the only strong colour.
    static final int BACKGROUND = 0xFF121316;
    static final int TEXT_PRIMARY = 0xFFF2F2F3;
    static final int TEXT_SECONDARY = 0xFFB4B5B8;
    static final int TEXT_FAINT = 0xFF85868A;
    static final int ACCENT = 0xFF66A9FF;
    static final int GREEN = 0xFF4ADE80;
    static final int RED = 0xFFFF6B70;

    private Ui() {}

    /** A plain dark screen with a scrolling page that respects the system bars. */
    static final class Screen {
        final FrameLayout root;
        final LinearLayout page;

        Screen(FrameLayout root, LinearLayout page) {
            this.root = root;
            this.page = page;
        }
    }

    static Screen screen(Activity activity) {
        activity.getWindow().setStatusBarColor(Color.TRANSPARENT);
        activity.getWindow().setNavigationBarColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= 30) activity.getWindow().setDecorFitsSystemWindows(false);

        FrameLayout root = new FrameLayout(activity);
        root.setBackgroundColor(BACKGROUND);

        ScrollView scroll = new ScrollView(activity);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        scroll.setVerticalScrollBarEnabled(false);
        LinearLayout page = new LinearLayout(activity);
        page.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(page, new ScrollView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(scroll, fill());

        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            int[] bars = systemBars(insets);
            page.setPadding(dp(activity, 20) + bars[0], dp(activity, 12) + bars[1], dp(activity, 20) + bars[2],
                    dp(activity, 28) + bars[3]);
            return insets;
        });
        return new Screen(root, page);
    }

    /** left, top, right, bottom system-bar insets. */
    private static int[] systemBars(WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) {
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
            return new int[]{bars.left, bars.top, bars.right, bars.bottom};
        }
        return new int[]{insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom()};
    }

    /** Back arrow and title, at the top of the Settings and Help screens. */
    static View header(Activity activity, String title) {
        LinearLayout bar = new LinearLayout(activity);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        ImageButton back = iconButton(activity, R.drawable.ic_back, activity.getString(R.string.action_back));
        back.setOnClickListener(v -> activity.finish());
        bar.addView(back, new LinearLayout.LayoutParams(dp(activity, 44), dp(activity, 44)));
        TextView label = text(activity, title, 22, TEXT_PRIMARY, Typeface.BOLD);
        label.setPadding(dp(activity, 6), 0, 0, 0);
        bar.addView(label, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return bar;
    }

    /** A borderless icon button with a subtle circular touch ripple. */
    static ImageButton iconButton(Context context, int drawable, String description) {
        ImageButton button = new ImageButton(context);
        button.setImageResource(drawable);
        button.setContentDescription(description);
        button.setScaleType(ImageView.ScaleType.CENTER);
        GradientDrawable shape = circle(Color.TRANSPARENT);
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x40FFFFFF), shape, null));
        return button;
    }

    static LinearLayout group(Context context) {
        LinearLayout group = new LinearLayout(context);
        group.setOrientation(LinearLayout.VERTICAL);
        // Kept as a layout helper for content groups; pages use spacing and dividers instead
        // of nested decorative panels.
        return group;
    }

    /** A tappable flat row: icon, title, optional subtitle, and an optional view at the end. */
    static LinearLayout row(Context context, int icon, String title, String subtitle, View end) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(context, 12), 0, dp(context, 12));
        row.setMinimumHeight(dp(context, 60));
        row.setBackground(new RippleDrawable(ColorStateList.valueOf(0x26FFFFFF), null,
                roundRect(context, 0xFFFFFFFF, 4)));

        ImageView image = new ImageView(context);
        image.setImageResource(icon);
        image.setScaleType(ImageView.ScaleType.CENTER);
        row.addView(image, new LinearLayout.LayoutParams(dp(context, 24), dp(context, 24)));

        TextView titleView = text(context, title, 15, TEXT_PRIMARY, Typeface.BOLD);
        LinearLayout labels = column(context, titleView);
        if (subtitle != null) {
            TextView sub = text(context, subtitle, 13, TEXT_SECONDARY, Typeface.NORMAL);
            sub.setPadding(0, dp(context, 2), 0, 0);
            labels.addView(sub, matchWrap());
        }
        labels.setPadding(dp(context, 14), 0, dp(context, 8), 0);
        row.addView(labels, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        if (end != null) row.addView(end);
        return row;
    }

    static ImageView chevron(Context context) {
        ImageView view = new ImageView(context);
        view.setImageResource(R.drawable.ic_chevron);
        return view;
    }

    /** Small, plain heading above a group of settings. */
    static TextView sectionLabel(Context context, String label) {
        TextView view = text(context, label, 14, TEXT_PRIMARY, Typeface.BOLD);
        view.setPadding(0, 0, 0, dp(context, 6));
        return view;
    }

    static View divider(Context context) {
        View divider = new View(context);
        divider.setBackgroundColor(0xFF303135);
        return divider;
    }

    static TextView text(Context context, String value, float size, int color, int style) {
        TextView view = new TextView(context);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setTypeface(Typeface.create("sans-serif", style));
        return view;
    }

    static LinearLayout column(Context context, View... children) {
        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        for (View child : children) column.addView(child, matchWrap());
        return column;
    }

    static GradientDrawable roundRect(Context context, int color, int radiusDp) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(dp(context, radiusDp));
        return shape;
    }

    static GradientDrawable circle(int color) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setShape(GradientDrawable.OVAL);
        return shape;
    }

    static RippleDrawable ripple(Drawable content) {
        return new RippleDrawable(ColorStateList.valueOf(0x40FFFFFF), content, null);
    }

    static LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    static LinearLayout.LayoutParams matchWrap(Context context, int topMarginDp) {
        LinearLayout.LayoutParams params = matchWrap();
        params.topMargin = dp(context, topMarginDp);
        return params;
    }

    static FrameLayout.LayoutParams fill() {
        return new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
    }

    static String versionName(Context context) {
        try {
            return context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "";
        }
    }

    static int dp(Context context, int value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }
}
