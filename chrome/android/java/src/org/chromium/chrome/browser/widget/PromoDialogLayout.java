// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.ViewGroup;
import android.view.ViewStub;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.infobar.InfoBarControlLayout;
import org.chromium.chrome.browser.widget.PromoDialog.DialogParams;

/**
 * Lays out a promo dialog that is shown when Clank starts up.
 *
 * Because of the versatility of dialog content and screen sizes, this layout exhibits a bunch of
 * specific behaviors (see go/snowflake-dialogs for details):
 *
 * + It hides controls when their resources are not specified by the {@link DialogParams}.
 *   The only two required components are the header text and the primary button label.
 *
 * + When the width is greater than the height, the promo content switches from vertical to
 *   horizontal and moves the illustration from the top of the text to the side of the text.
 *
 * + The buttons are always locked to the bottom of the dialog and stack when there isn't enough
 *   room to display them on one row.
 *
 * + If there is no promo illustration, the header text becomes locked to the top of the dialog and
 *   doesn't scroll away.
 */
public final class PromoDialogLayout extends BoundedLinearLayout {
    /** Content in the dialog that will flip orientation when the screen is wide. */
    private LinearLayout mFlippableContent;

    /** Content in the dialog that can be scrolled. */
    private LinearLayout mScrollableContent;

    /** Illustration that teases the thing being promoted. */
    private ImageView mIllustrationView;

    /** View containing the header of the promo. */
    private TextView mHeaderView;

    /** View containing the header of the promo. */
    private TextView mFooterView;

    /** View containing text explaining the promo. */
    private TextView mSubheaderView;

    /** Paramters used to build the promo. */
    private DialogParams mParams;

    public PromoDialogLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    @Override
    public void onFinishInflate() {
        mFlippableContent = (LinearLayout) findViewById(R.id.full_promo_content);
        mScrollableContent = (LinearLayout) findViewById(R.id.scrollable_promo_content);
        mIllustrationView = (ImageView) findViewById(R.id.illustration);
        mHeaderView = (TextView) findViewById(R.id.header);
        mSubheaderView = (TextView) findViewById(R.id.subheader);

        super.onFinishInflate();
    }

    /** Initializes the dialog contents using the given params.  Should only be called once. */
    void initialize(DialogParams params) {
        assert mParams == null && params != null;
        assert params.headerStringResource != 0;
        assert params.primaryButtonStringResource != 0;
        mParams = params;

        if (mParams.drawableResource == 0) {
            // Dialogs with no illustration make the header stay visible at all times instead of
            // scrolling off on small screens.
            ((ViewGroup) mIllustrationView.getParent()).removeView(mIllustrationView);
            ((ViewGroup) mHeaderView.getParent()).removeView(mHeaderView);
            addView(mHeaderView, 0);

            // The margins only apply here (after it moves to the root) because the scroll layout it
            // is normally in has implicit padding.
            int marginSize =
                    getContext().getResources().getDimensionPixelSize(R.dimen.dialog_header_margin);
            ApiCompatibilityUtils.setMarginStart(
                    (MarginLayoutParams) mHeaderView.getLayoutParams(), marginSize);
            ApiCompatibilityUtils.setMarginEnd(
                    (MarginLayoutParams) mHeaderView.getLayoutParams(), marginSize);
        } else {
            mIllustrationView.setImageResource(mParams.drawableResource);
        }

        // Create the header.
        mHeaderView.setText(mParams.headerStringResource);

        // Set up the subheader text.
        if (mParams.subheaderStringResource == 0) {
            ((ViewGroup) mSubheaderView.getParent()).removeView(mSubheaderView);
        } else {
            mSubheaderView.setText(mParams.subheaderStringResource);
        }

        // Create the footer.
        ViewStub footerStub = (ViewStub) findViewById(R.id.footer_stub);
        if (mParams.footerStringResource == 0) {
            ((ViewGroup) footerStub.getParent()).removeView(footerStub);
        } else {
            mFooterView = (TextView) footerStub.inflate();
            mFooterView.setText(mParams.footerStringResource);
        }

        // Create the buttons.
        DualControlLayout buttonBar = (DualControlLayout) findViewById(R.id.button_bar);
        String primaryString = getResources().getString(mParams.primaryButtonStringResource);
        buttonBar.addView(
                DualControlLayout.createButtonForLayout(getContext(), true, primaryString, null));

        if (mParams.secondaryButtonStringResource != 0) {
            String secondaryString =
                    getResources().getString(mParams.secondaryButtonStringResource);
            buttonBar.addView(DualControlLayout.createButtonForLayout(
                    getContext(), false, secondaryString, null));
        }
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int availableWidth = MeasureSpec.getSize(widthMeasureSpec);
        int availableHeight = MeasureSpec.getSize(heightMeasureSpec);

        if (availableWidth > availableHeight * 1.5) {
            mFlippableContent.setOrientation(LinearLayout.HORIZONTAL);
        } else {
            mFlippableContent.setOrientation(LinearLayout.VERTICAL);
        }

        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
    }

    /** Adds a standardized set of controls to the layout. */
    InfoBarControlLayout addControlLayout() {
        InfoBarControlLayout layout = new InfoBarControlLayout(getContext());
        mScrollableContent.addView(
                layout, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        return layout;
    }
}
