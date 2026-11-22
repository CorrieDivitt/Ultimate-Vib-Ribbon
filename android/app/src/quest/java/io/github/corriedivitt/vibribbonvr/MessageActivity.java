// Vib-Ribbon VR on the Quest (task 6.14): a flat panel with a message, for when the game
// cannot start in the headset (most often: no disc image copied over yet). The headset
// shows an ordinary Android window as a floating panel, and Android draws the text in the
// system font, so this needs no font from the disc. It runs in a process of its own, so it
// stays open after the game's own process has closed; Close ends it.
package io.github.corriedivitt.vibribbonvr;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public class MessageActivity extends Activity {
    public static final String EXTRA_TITLE = "title";
    public static final String EXTRA_TEXT = "text";

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        String title = getIntent().getStringExtra(EXTRA_TITLE);
        String text = getIntent().getStringExtra(EXTRA_TEXT);
        setTitle(title != null ? title : "Vib-Ribbon VR");

        int pad = dp(32);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(pad, pad, pad, pad);
        column.setBackgroundColor(Color.rgb(16, 16, 24));

        TextView heading = new TextView(this);
        heading.setText(title != null ? title : "Vib-Ribbon VR");
        heading.setTextColor(Color.WHITE);
        heading.setTextSize(TypedValue.COMPLEX_UNIT_SP, 26);
        heading.setPadding(0, 0, 0, dp(16));
        column.addView(heading);

        TextView body = new TextView(this);
        body.setText(text != null ? text : "");
        body.setTextColor(Color.rgb(225, 225, 235));
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        body.setLineSpacing(0, 1.2f);
        body.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(body);
        column.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        Button close = new Button(this);
        close.setText("Close");
        close.setOnClickListener(v -> finishAndRemoveTask());
        LinearLayout.LayoutParams at = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
                                                                     LinearLayout.LayoutParams.WRAP_CONTENT);
        at.gravity = Gravity.END;
        at.topMargin = dp(16);
        column.addView(close, at);

        setContentView(column);
    }

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
    }
}
