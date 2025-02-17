// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.searchwidget;

import android.app.Activity;
import android.app.Instrumentation;
import android.app.Instrumentation.ActivityMonitor;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.support.test.filters.SmallTest;
import android.test.InstrumentationTestCase;
import android.util.Pair;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.RemoteViews;
import android.widget.TextView;

import org.junit.Assert;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.AdvancedMockContext;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.InMemorySharedPreferences;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.firstrun.FirstRunActivity;
import org.chromium.chrome.browser.util.IntentUtils;
import org.chromium.chrome.test.util.ApplicationTestUtils;
import org.chromium.content.browser.test.util.CriteriaHelper;

import java.util.ArrayList;
import java.util.List;

/**
 * Tests for the SearchWidgetProvider.
 */
@CommandLineFlags.Add(ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE)
public class SearchWidgetProviderTest extends InstrumentationTestCase {
    private static final class TestDelegate
            extends SearchWidgetProvider.SearchWidgetProviderDelegate {
        public static final int[] ALL_IDS = {11684, 20170525};

        public final List<Pair<Integer, RemoteViews>> mViews = new ArrayList<>();
        private Context mContext;
        private SharedPreferences mPreferences;

        private TestDelegate(Context context) {
            super(context);
            mContext = context;
            mPreferences = new InMemorySharedPreferences();
        }

        @Override
        protected Context getContext() {
            return mContext;
        }

        @Override
        protected SharedPreferences getSharedPreferences() {
            return mPreferences;
        }

        @Override
        protected int[] getAllSearchWidgetIds() {
            return ALL_IDS;
        }

        @Override
        protected void updateAppWidget(int id, RemoteViews views) {
            mViews.add(new Pair<Integer, RemoteViews>(id, views));
        }
    }

    private final class TestContext extends AdvancedMockContext {
        public TestContext() {
            super(getInstrumentation().getTargetContext().getApplicationContext());
        }
    }

    private static final String TEXT_GENERIC = "Search";
    private static final String TEXT_SEARCH_ENGINE = "Stuff and Thangs";
    private static final String TEXT_SEARCH_ENGINE_FULL = "Search with Stuff and Thangs";

    private TestContext mContext;
    private TestDelegate mDelegate;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        ApplicationTestUtils.setUp(getInstrumentation().getTargetContext(), true);
        SearchActivity.disableForTests();

        mContext = new TestContext();
        mDelegate = new TestDelegate(mContext);
        SearchWidgetProvider.setDelegateForTest(mDelegate);
    }

    @Override
    public void tearDown() throws Exception {
        ApplicationTestUtils.tearDown(getInstrumentation().getTargetContext());
        super.tearDown();
    }

    @SmallTest
    public void testUpdateAll() {
        SearchWidgetProvider.handleAction(
                new Intent(SearchWidgetProvider.ACTION_UPDATE_ALL_WIDGETS));

        // Without any idea of what the default search engine is, widgets should default to saying
        // just "Search".
        checkWidgetStates(TEXT_GENERIC, View.VISIBLE);

        // The microphone icon should disappear if voice queries are unavailable.
        mDelegate.mViews.clear();
        SearchWidgetProvider.updateCachedVoiceSearchAvailability(false);
        checkWidgetStates(TEXT_GENERIC, View.GONE);

        // After recording that the default search engine is "X", it should say "Search with X".
        mDelegate.mViews.clear();
        SearchWidgetProvider.updateCachedEngineName(TEXT_SEARCH_ENGINE);
        checkWidgetStates(TEXT_SEARCH_ENGINE_FULL, View.GONE);

        // The microphone icon should appear if voice queries are available.
        mDelegate.mViews.clear();
        SearchWidgetProvider.updateCachedVoiceSearchAvailability(true);
        checkWidgetStates(TEXT_SEARCH_ENGINE_FULL, View.VISIBLE);
    }

    @SmallTest
    @CommandLineFlags.Remove(ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE)
    public void testUpdateCachedEngineNameBeforeFirstRun() {
        assertFalse(SearchWidgetProvider.shouldShowFullString());
        SearchWidgetProvider.handleAction(
                new Intent(SearchWidgetProvider.ACTION_UPDATE_ALL_WIDGETS));

        // Without any idea of what the default search engine is, widgets should default to saying
        // just "Search".
        checkWidgetStates(TEXT_GENERIC, View.VISIBLE);

        // Until First Run is complete, no search engine branding should be displayed.  Widgets are
        // already displaying the generic string, and should continue doing so, so they don't get
        // updated.
        mDelegate.mViews.clear();
        SearchWidgetProvider.updateCachedEngineName(TEXT_SEARCH_ENGINE);
        assertEquals(0, mDelegate.mViews.size());

        // Manually set the preference, then update the cached engine name again.  The
        // SearchWidgetProvider should now believe that its widgets are displaying branding when it
        // isn't allowed to, then update them.
        mDelegate.mViews.clear();
        mDelegate.getSharedPreferences()
                .edit()
                .putString(SearchWidgetProvider.PREF_SEARCH_ENGINE_SHORTNAME, TEXT_SEARCH_ENGINE)
                .apply();
        SearchWidgetProvider.updateCachedEngineName(TEXT_SEARCH_ENGINE);
        checkWidgetStates(TEXT_GENERIC, View.VISIBLE);
    }

    private void checkWidgetStates(final String expectedString, final int expectedMicrophoneState) {
        // Confirm that all the widgets got updated.
        Assert.assertEquals(TestDelegate.ALL_IDS.length, mDelegate.mViews.size());
        for (int i = 0; i < TestDelegate.ALL_IDS.length; i++) {
            Assert.assertEquals(TestDelegate.ALL_IDS[i], mDelegate.mViews.get(i).first.intValue());
        }

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                // Check the contents of the RemoteViews by inflating them.
                for (int i = 0; i < mDelegate.mViews.size(); i++) {
                    FrameLayout parentView = new FrameLayout(mContext);
                    RemoteViews views = mDelegate.mViews.get(i).second;
                    View view = views.apply(mContext, parentView);
                    parentView.addView(view);

                    // Confirm that the string is correct.
                    TextView titleView = (TextView) view.findViewById(R.id.title);
                    Assert.assertEquals(View.VISIBLE, titleView.getVisibility());
                    Assert.assertEquals(expectedString, titleView.getText());

                    // Confirm the visibility of the microphone.
                    View microphoneView = view.findViewById(R.id.microphone_icon);
                    Assert.assertEquals(expectedMicrophoneState, microphoneView.getVisibility());
                }
            }
        });
    }

    @SmallTest
    public void testMicrophoneClick() {
        SearchWidgetProvider.handleAction(
                new Intent(SearchWidgetProvider.ACTION_UPDATE_ALL_WIDGETS));
        for (int i = 0; i < mDelegate.mViews.size(); i++) {
            RemoteViews views = mDelegate.mViews.get(i).second;
            clickOnWidget(views, R.id.microphone_icon, true);
        }
    }

    @SmallTest
    public void testTextClick() {
        SearchWidgetProvider.handleAction(
                new Intent(SearchWidgetProvider.ACTION_UPDATE_ALL_WIDGETS));
        for (int i = 0; i < mDelegate.mViews.size(); i++) {
            RemoteViews views = mDelegate.mViews.get(i).second;
            clickOnWidget(views, R.id.text_container, true);
        }
    }

    @SmallTest
    @CommandLineFlags.Remove(ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE)
    public void testOnboardingRequired() {
        SearchWidgetProvider.handleAction(
                new Intent(SearchWidgetProvider.ACTION_UPDATE_ALL_WIDGETS));
        for (int i = 0; i < mDelegate.mViews.size(); i++) {
            RemoteViews views = mDelegate.mViews.get(i).second;
            clickOnWidget(views, R.id.text_container, false);
        }
    }

    private void clickOnWidget(
            final RemoteViews views, final int clickTarget, boolean isFirstRunComplete) {
        String className = isFirstRunComplete ? SearchActivity.class.getName()
                                              : FirstRunActivity.class.getName();
        ActivityMonitor monitor = new ActivityMonitor(className, null, false);

        Instrumentation instrumentation = getInstrumentation();
        instrumentation.addMonitor(monitor);

        // Click on the widget.
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                FrameLayout parentView = new FrameLayout(mContext);
                View view = views.apply(mContext, parentView);
                parentView.addView(view);
                view.findViewById(clickTarget).performClick();
            }
        });

        Activity activity = instrumentation.waitForMonitorWithTimeout(
                monitor, CriteriaHelper.DEFAULT_MAX_TIME_TO_POLL);
        assertNotNull(activity);
        if (isFirstRunComplete) {
            // Check that the Activity was launched in the right mode.
            Intent intent = activity.getIntent();
            boolean microphoneState = IntentUtils.safeGetBooleanExtra(
                    intent, SearchWidgetProvider.EXTRA_START_VOICE_SEARCH, false);
            Assert.assertEquals(clickTarget == R.id.microphone_icon, microphoneState);
        }
    }

    @SmallTest
    public void testCrashAbsorption() {
        Runnable crashingRunnable = new Runnable() {
            @Override
            public void run() {
                throw new RuntimeException();
            }
        };

        SharedPreferences prefs = mDelegate.getSharedPreferences();
        Assert.assertEquals(0, SearchWidgetProvider.getNumConsecutiveCrashes(prefs));

        // The first few crashes should be silently absorbed.
        SearchWidgetProvider.run(crashingRunnable);
        Assert.assertEquals(1, SearchWidgetProvider.getNumConsecutiveCrashes(prefs));
        SearchWidgetProvider.run(crashingRunnable);
        Assert.assertEquals(2, SearchWidgetProvider.getNumConsecutiveCrashes(prefs));

        // The crash should be thrown after hitting the crash limit, which is 3.
        boolean exceptionWasThrown = false;
        try {
            SearchWidgetProvider.run(crashingRunnable);
        } catch (Exception e) {
            exceptionWasThrown = true;
        }
        Assert.assertEquals(3, SearchWidgetProvider.getNumConsecutiveCrashes(prefs));
        Assert.assertTrue(exceptionWasThrown);
    }
}
