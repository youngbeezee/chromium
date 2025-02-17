// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.locale;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.support.annotation.IntDef;
import android.support.annotation.Nullable;

import org.chromium.base.Callback;
import org.chromium.base.ContextUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.AppHooks;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.preferences.PreferencesLauncher;
import org.chromium.chrome.browser.preferences.SearchEnginePreference;
import org.chromium.chrome.browser.snackbar.Snackbar;
import org.chromium.chrome.browser.snackbar.SnackbarManager;
import org.chromium.chrome.browser.snackbar.SnackbarManager.SnackbarController;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.ref.WeakReference;

/**
 * Manager for some locale specific logics.
 */
public class LocaleManager {
    public static final String PREF_AUTO_SWITCH = "LocaleManager_PREF_AUTO_SWITCH";
    public static final String PREF_PROMO_SHOWN = "LocaleManager_PREF_PROMO_SHOWN";
    public static final String PREF_WAS_IN_SPECIAL_LOCALE = "LocaleManager_WAS_IN_SPECIAL_LOCALE";
    public static final String SPECIAL_LOCALE_ID = "US";

    /** The different types of search engine promo dialogs. */
    @IntDef({SEARCH_ENGINE_PROMO_DONT_SHOW, SEARCH_ENGINE_PROMO_SHOW_SOGOU,
            SEARCH_ENGINE_PROMO_SHOW_EXISTING, SEARCH_ENGINE_PROMO_SHOW_NEW})
    @Retention(RetentionPolicy.SOURCE)
    public @interface SearchEnginePromoType {}

    public static final int SEARCH_ENGINE_PROMO_DONT_SHOW = -1;
    public static final int SEARCH_ENGINE_PROMO_SHOW_SOGOU = 0;
    public static final int SEARCH_ENGINE_PROMO_SHOW_EXISTING = 1;
    public static final int SEARCH_ENGINE_PROMO_SHOW_NEW = 2;

    private static final int SNACKBAR_DURATION_MS = 6000;

    private static LocaleManager sInstance;

    // LocaleManager is a singleton and it should not have strong reference to UI objects.
    // SnackbarManager is owned by ChromeActivity and is not null as long as the activity is alive.
    private WeakReference<SnackbarManager> mSnackbarManager;
    private SpecialLocaleHandler mLocaleHandler;

    private SnackbarController mSnackbarController = new SnackbarController() {
        @Override
        public void onDismissNoAction(Object actionData) { }

        @Override
        public void onAction(Object actionData) {
            Context context = ContextUtils.getApplicationContext();
            Intent intent = PreferencesLauncher.createIntentForSettingsPage(context,
                    SearchEnginePreference.class.getName());
            context.startActivity(intent);
        }
    };

    /**
     * Starts listening to state changes of the phone.
     */
    public void startObservingPhoneChanges() {
        maybeAutoSwitchSearchEngine();
    }

    /**
     * Stops listening to state changes of the phone.
     */
    public void stopObservingPhoneChanges() {}

    /**
     * @return An instance of the {@link LocaleManager}. This should only be called on UI thread.
     */
    public static LocaleManager getInstance() {
        assert ThreadUtils.runningOnUiThread();
        if (sInstance == null) {
            sInstance = AppHooks.get().createLocaleManager();
        }
        return sInstance;
    }

    /**
     * Starts recording metrics in deferred startup.
     */
    public void recordStartupMetrics() {}

    /**
     * @return Whether the Chrome instance is running in a special locale.
     */
    public boolean isSpecialLocaleEnabled() {
        // If there is a kill switch sent from the server, disable the feature.
        if (!ChromeFeatureList.isEnabled("SpecialLocaleWrapper")) {
            return false;
        }
        boolean inSpecialLocale = ChromeFeatureList.isEnabled("SpecialLocale");
        inSpecialLocale = isReallyInSpecialLocale(inSpecialLocale);
        return inSpecialLocale;
    }

    /**
     * @return The country id of the special locale.
     */
    public String getSpecialLocaleId() {
        return SPECIAL_LOCALE_ID;
    }

    /**
     * Adds local search engines for special locale.
     */
    public void addSpecialSearchEngines() {
        if (!isSpecialLocaleEnabled()) return;
        getSpecialLocaleHandler().loadTemplateUrls();
    }

    /**
     * Removes local search engines for special locale.
     */
    public void removeSpecialSearchEngines() {
        if (isSpecialLocaleEnabled()) return;
        getSpecialLocaleHandler().removeTemplateUrls();
    }

    /**
     * Overrides the default search engine to a different search engine we designate. This is a
     * no-op if the user has manually changed DSP settings.
     */
    public void overrideDefaultSearchEngine() {
        if (!isSearchEngineAutoSwitchEnabled() || !isSpecialLocaleEnabled()) return;
        getSpecialLocaleHandler().overrideDefaultSearchProvider();
        showSnackbar(ContextUtils.getApplicationContext().getString(R.string.using_sogou));
    }

    /**
     * Reverts the temporary change made in {@link #overrideDefaultSearchEngine()}. This is a no-op
     * if the user has manually changed DSP settings.
     */
    public void revertDefaultSearchEngineOverride() {
        if (!isSearchEngineAutoSwitchEnabled() || isSpecialLocaleEnabled()) return;
        getSpecialLocaleHandler().setGoogleAsDefaultSearch();
        showSnackbar(ContextUtils.getApplicationContext().getString(R.string.using_google));
    }

    /**
     * Switches the default search engine based on the current locale, if the user has delegated
     * Chrome to do so. This method also adds some special engines to user's search engine list, as
     * long as the user is in this locale.
     */
    protected void maybeAutoSwitchSearchEngine() {
        SharedPreferences preferences = ContextUtils.getAppSharedPreferences();
        boolean wasInSpecialLocale = preferences.getBoolean(PREF_WAS_IN_SPECIAL_LOCALE, false);
        boolean isInSpecialLocale = isSpecialLocaleEnabled();
        if (wasInSpecialLocale && !isInSpecialLocale) {
            revertDefaultSearchEngineOverride();
            removeSpecialSearchEngines();
        } else if (isInSpecialLocale && !wasInSpecialLocale) {
            addSpecialSearchEngines();
            overrideDefaultSearchEngine();
        } else if (isInSpecialLocale) {
            // As long as the user is in the special locale, special engines should be in the list.
            addSpecialSearchEngines();
        }
        preferences.edit().putBoolean(PREF_WAS_IN_SPECIAL_LOCALE, isInSpecialLocale).apply();
    }

    /**
     * Shows a promotion dialog about search engines depending on Locale and other conditions.
     * See {@link LocaleManager#getSearchEnginePromoShowType()} for possible types and logic.
     *
     * @param context     Context showing the dialog.
     * @param onDismissed Notified when the dialog is dismissed and whether the user acted on it.
     * @return Whether such dialog is needed.
     */
    public boolean showSearchEnginePromoIfNeeded(
            Context context, @Nullable Callback<Boolean> onDismissed) {
        int shouldShow = getSearchEnginePromoShowType();
        switch (shouldShow) {
            case SEARCH_ENGINE_PROMO_DONT_SHOW:
                return false;
            case SEARCH_ENGINE_PROMO_SHOW_SOGOU:
                new SogouPromoDialog(context, this, onDismissed).show();
                return true;
            case SEARCH_ENGINE_PROMO_SHOW_EXISTING:
            case SEARCH_ENGINE_PROMO_SHOW_NEW:
                DefaultSearchEnginePromoDialog.show(context, shouldShow, onDismissed);
                return true;
            default:
                assert false;
                return false;
        }
    }

    /**
     * @return Whether auto switch for search engine is enabled.
     */
    public boolean isSearchEngineAutoSwitchEnabled() {
        return ContextUtils.getAppSharedPreferences().getBoolean(PREF_AUTO_SWITCH, false);
    }

    /**
     * Sets whether auto switch for search engine is enabled.
     */
    public void setSearchEngineAutoSwitch(boolean isEnabled) {
        ContextUtils.getAppSharedPreferences().edit().putBoolean(PREF_AUTO_SWITCH, isEnabled)
                .apply();
    }

    /**
     * Sets the {@link SnackbarManager} used by this instance.
     */
    public void setSnackbarManager(SnackbarManager manager) {
        mSnackbarManager = new WeakReference<SnackbarManager>(manager);
    }

    private void showSnackbar(CharSequence title) {
        SnackbarManager manager = mSnackbarManager.get();
        if (manager == null) return;

        Context context = ContextUtils.getApplicationContext();
        Snackbar snackbar = Snackbar.make(title, mSnackbarController, Snackbar.TYPE_NOTIFICATION,
                Snackbar.UMA_SPECIAL_LOCALE);
        snackbar.setDuration(SNACKBAR_DURATION_MS);
        snackbar.setAction(context.getString(R.string.preferences), null);
        manager.showSnackbar(snackbar);
    }

    /**
     * Does some extra checking about whether the user is in special locale.
     * @param inSpecialLocale Whether the variation service thinks the client is in special locale.
     * @return The result after extra confirmation.
     */
    protected boolean isReallyInSpecialLocale(boolean inSpecialLocale) {
        return inSpecialLocale;
    }

    /**
     * @return Whether and which search engine promo should be shown.
     */
    @SearchEnginePromoType
    protected int getSearchEnginePromoShowType() {
        if (!isSpecialLocaleEnabled()) return SEARCH_ENGINE_PROMO_DONT_SHOW;
        SharedPreferences preferences = ContextUtils.getAppSharedPreferences();
        if (preferences.getBoolean(PREF_PROMO_SHOWN, false)) {
            return SEARCH_ENGINE_PROMO_DONT_SHOW;
        }
        return SEARCH_ENGINE_PROMO_SHOW_SOGOU;
    }

    private SpecialLocaleHandler getSpecialLocaleHandler() {
        if (mLocaleHandler == null) mLocaleHandler = new SpecialLocaleHandler(getSpecialLocaleId());
        return mLocaleHandler;
    }
}
