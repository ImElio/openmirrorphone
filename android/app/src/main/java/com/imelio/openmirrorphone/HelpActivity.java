package com.imelio.openmirrorphone;

import android.app.Activity;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Help: how Android mirroring will work, what works today, and privacy. */
public final class HelpActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Ui.Screen screen = Ui.screen(this);
        LinearLayout page = screen.page;
        page.addView(Ui.header(this, getString(R.string.help_title)), Ui.matchWrap());

        page.addView(Ui.sectionLabel(this, getString(R.string.steps_title)), Ui.matchWrap(this, 24));
        LinearLayout steps = Ui.group(this);
        int[][] items = {{R.string.step_1_title, R.string.step_1_text}, {R.string.step_2_title, R.string.step_2_text},
                {R.string.step_3_title, R.string.step_3_text}};
        for (int i = 0; i < items.length; i++) {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            if (i > 0) row.setPadding(0, Ui.dp(this, 16), 0, 0);

            TextView number = Ui.text(this, String.valueOf(i + 1), 15, Ui.ACCENT, Typeface.BOLD);
            number.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
            row.addView(number, new LinearLayout.LayoutParams(Ui.dp(this, 24), Ui.dp(this, 24)));

            TextView title = Ui.text(this, getString(items[i][0]), 15, Ui.TEXT_PRIMARY, Typeface.BOLD);
            TextView text = Ui.text(this, getString(items[i][1]), 13, Ui.TEXT_SECONDARY, Typeface.NORMAL);
            text.setPadding(0, Ui.dp(this, 2), 0, 0);
            LinearLayout labels = Ui.column(this, title, text);
            labels.setPadding(Ui.dp(this, 14), Ui.dp(this, 1), 0, 0);
            row.addView(labels, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            steps.addView(row, Ui.matchWrap());
        }
        page.addView(steps, Ui.matchWrap());

        page.addView(Ui.sectionLabel(this, getString(R.string.help_today_title)), Ui.matchWrap(this, 24));
        page.addView(paragraph(R.string.help_today_text), Ui.matchWrap());

        page.addView(Ui.sectionLabel(this, getString(R.string.help_privacy_title)), Ui.matchWrap(this, 24));
        page.addView(paragraph(R.string.help_privacy_text), Ui.matchWrap());

        setContentView(screen.root);
    }

    private LinearLayout paragraph(int text) {
        LinearLayout card = Ui.group(this);
        TextView body = Ui.text(this, getString(text), 14, Ui.TEXT_SECONDARY, Typeface.NORMAL);
        body.setLineSpacing(Ui.dp(this, 3), 1f);
        card.addView(body, Ui.matchWrap());
        return card;
    }
}
