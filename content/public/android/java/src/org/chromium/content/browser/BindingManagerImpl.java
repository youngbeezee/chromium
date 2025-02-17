// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.annotation.TargetApi;
import android.content.ComponentCallbacks2;
import android.content.Context;
import android.content.res.Configuration;
import android.os.Build;
import android.util.LruCache;
import android.util.SparseArray;

import org.chromium.base.Log;
import org.chromium.base.SysUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.metrics.RecordHistogram;

import java.util.Map;

/**
 * Manages oom bindings used to bound child services.
 * This object must only be accessed from the launcher thread.
 */
class BindingManagerImpl implements BindingManager {
    private static final String TAG = "cr.BindingManager";

    // Low reduce ratio of moderate binding.
    private static final float MODERATE_BINDING_LOW_REDUCE_RATIO = 0.25f;
    // High reduce ratio of moderate binding.
    private static final float MODERATE_BINDING_HIGH_REDUCE_RATIO = 0.5f;

    // Delay of 1 second used when removing temporary strong binding of a process (only on
    // non-low-memory devices).
    private static final long DETACH_AS_ACTIVE_HIGH_END_DELAY_MILLIS = 1 * 1000;

    // Delays used when clearing moderate binding pool when onSentToBackground happens.
    private static final long MODERATE_BINDING_POOL_CLEARER_DELAY_MILLIS = 10 * 1000;

    // These fields allow to override the parameters for testing - see
    // createBindingManagerForTesting().
    private final boolean mIsLowMemoryDevice;

    private static class ModerateBindingPool
            extends LruCache<Integer, ManagedConnection> implements ComponentCallbacks2 {
        private Runnable mDelayedClearer;

        public ModerateBindingPool(int maxSize) {
            super(maxSize);
        }

        @Override
        public void onTrimMemory(final int level) {
            ThreadUtils.assertOnUiThread();
            LauncherThread.post(new Runnable() {
                @Override
                public void run() {
                    Log.i(TAG, "onTrimMemory: level=%d, size=%d", level, size());
                    if (size() <= 0) {
                        return;
                    }
                    if (level <= TRIM_MEMORY_RUNNING_MODERATE) {
                        reduce(MODERATE_BINDING_LOW_REDUCE_RATIO);
                    } else if (level <= TRIM_MEMORY_RUNNING_LOW) {
                        reduce(MODERATE_BINDING_HIGH_REDUCE_RATIO);
                    } else if (level == TRIM_MEMORY_UI_HIDDEN) {
                        // This will be handled by |mDelayedClearer|.
                        return;
                    } else {
                        evictAll();
                    }
                }
            });
        }

        @Override
        public void onLowMemory() {
            ThreadUtils.assertOnUiThread();
            LauncherThread.post(new Runnable() {
                @Override
                public void run() {
                    Log.i(TAG, "onLowMemory: evict %d bindings", size());
                    evictAll();
                }
            });
        }

        @Override
        public void onConfigurationChanged(Configuration configuration) {}

        @TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR1)
        private void reduce(float reduceRatio) {
            int oldSize = size();
            int newSize = (int) (oldSize * (1f - reduceRatio));
            Log.i(TAG, "Reduce connections from %d to %d", oldSize, newSize);
            if (newSize == 0) {
                evictAll();
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
                trimToSize(newSize);
            } else {
                // Entries will be removed from the front because snapshot() returns ones ordered
                // from least recently accessed to most recently accessed.
                int count = 0;
                for (Map.Entry<Integer, ManagedConnection> entry : snapshot().entrySet()) {
                    remove(entry.getKey());
                    ++count;
                    if (count == oldSize - newSize) break;
                }
            }
        }

        void addConnection(ManagedConnection managedConnection) {
            ManagedChildProcessConnection connection = managedConnection.mConnection;
            if (connection != null && connection.isSandboxed()) {
                managedConnection.addModerateBinding();
                if (connection.isModerateBindingBound()) {
                    put(connection.getServiceNumber(), managedConnection);
                } else {
                    remove(connection.getServiceNumber());
                }
            }
        }

        void removeConnection(ManagedConnection managedConnection) {
            ManagedChildProcessConnection connection = managedConnection.mConnection;
            if (connection != null && connection.isSandboxed()) {
                remove(connection.getServiceNumber());
            }
        }

        @Override
        protected void entryRemoved(boolean evicted, Integer key, ManagedConnection oldValue,
                ManagedConnection newValue) {
            if (oldValue != newValue) {
                oldValue.removeModerateBinding();
            }
        }

        void onSentToBackground(final boolean onTesting) {
            if (size() == 0) return;
            mDelayedClearer = new Runnable() {
                @Override
                public void run() {
                    if (mDelayedClearer == null) return;
                    mDelayedClearer = null;
                    Log.i(TAG, "Release moderate connections: %d", size());
                    if (!onTesting) {
                        RecordHistogram.recordCountHistogram(
                                "Android.ModerateBindingCount", size());
                    }
                    evictAll();
                }
            };
            LauncherThread.postDelayed(mDelayedClearer, MODERATE_BINDING_POOL_CLEARER_DELAY_MILLIS);
        }

        void onBroughtToForeground() {
            if (mDelayedClearer != null) {
                LauncherThread.removeCallbacks(mDelayedClearer);
                mDelayedClearer = null;
            }
        }
    }

    private ModerateBindingPool mModerateBindingPool;

    /**
     * Wraps ManagedChildProcessConnection keeping track of additional information needed to manage
     * the bindings of the connection. The reference to ManagedChildProcessConnection is cleared
     * when the connection goes away, but ManagedConnection itself is kept (until overwritten by a
     * new entry for the same pid).
     */
    private class ManagedConnection {
        // Set in constructor, cleared in clearConnection() (on a separate thread).
        // Need to keep a local reference to avoid it being cleared while using it.
        private ManagedChildProcessConnection mConnection;

        // True iff there is a strong binding kept on the service because it is working in
        // foreground.
        private boolean mInForeground;

        // True iff there is a strong binding kept on the service because it was bound for the
        // application background period.
        private boolean mBoundForBackgroundPeriod;

        /**
         * Removes the initial service binding.
         * @return true if the binding was removed.
         */
        private boolean removeInitialBinding() {
            ManagedChildProcessConnection connection = mConnection;
            if (connection == null || !connection.isInitialBindingBound()) return false;

            connection.removeInitialBinding();
            return true;
        }

        /** Adds a strong service binding. */
        private void addStrongBinding() {
            ManagedChildProcessConnection connection = mConnection;
            if (connection == null) return;

            connection.addStrongBinding();
            if (mModerateBindingPool != null) mModerateBindingPool.removeConnection(this);
        }

        /** Removes a strong service binding. */
        private void removeStrongBinding(final boolean keepAsModerate) {
            final ManagedChildProcessConnection connection = mConnection;
            // We have to fail gracefully if the strong binding is not present, as on low-end the
            // binding could have been removed by dropOomBindings() when a new service was started.
            if (connection == null || !connection.isStrongBindingBound()) return;

            // This runnable performs the actual unbinding. It will be executed synchronously when
            // on low-end devices and posted with a delay otherwise.
            Runnable doUnbind = new Runnable() {
                @Override
                public void run() {
                    if (connection.isStrongBindingBound()) {
                        connection.removeStrongBinding();
                        if (keepAsModerate) {
                            addConnectionToModerateBindingPool(connection);
                        }
                    }
                }
            };

            if (mIsLowMemoryDevice) {
                doUnbind.run();
            } else {
                LauncherThread.postDelayed(doUnbind, DETACH_AS_ACTIVE_HIGH_END_DELAY_MILLIS);
            }
        }

        /**
         * Adds connection to the moderate binding pool. No-op if the connection has a strong
         * binding.
         * @param connection The ChildProcessConnection to add to the moderate binding pool.
         */
        private void addConnectionToModerateBindingPool(ManagedChildProcessConnection connection) {
            if (mModerateBindingPool != null && !connection.isStrongBindingBound()) {
                mModerateBindingPool.addConnection(ManagedConnection.this);
            }
        }

        /** Removes the moderate service binding. */
        private void removeModerateBinding() {
            ManagedChildProcessConnection connection = mConnection;
            if (connection == null || !connection.isModerateBindingBound()) return;
            connection.removeModerateBinding();
        }

        /** Adds the moderate service binding. */
        private void addModerateBinding() {
            ManagedChildProcessConnection connection = mConnection;
            if (connection == null) return;

            connection.addModerateBinding();
        }

        /**
         * Drops the service bindings. This is used on low-end to drop bindings of the current
         * service when a new one is used in foreground.
         */
        private void dropBindings() {
            assert mIsLowMemoryDevice;
            ManagedChildProcessConnection connection = mConnection;
            if (connection == null) return;

            connection.dropOomBindings();
        }

        ManagedConnection(ManagedChildProcessConnection connection) {
            mConnection = connection;
        }

        /**
         * Sets the visibility of the service, adding or removing the strong binding as needed.
         */
        void setInForeground(boolean nextInForeground) {
            if (!mInForeground && nextInForeground) {
                addStrongBinding();
            } else if (mInForeground && !nextInForeground) {
                removeStrongBinding(true);
            }

            mInForeground = nextInForeground;
        }

        /**
         * Called when it is safe to rely on setInForeground() for binding management.
         */
        void onDeterminedVisibility() {
            if (!removeInitialBinding()) return;
            // Decrease the likelihood of a recently created background tab getting evicted by
            // immediately adding moderate binding.
            addConnectionToModerateBindingPool(mConnection);
        }

        /**
         * Sets or removes additional binding when the service is main service during the embedder
         * background period.
         */
        void setBoundForBackgroundPeriod(boolean nextBound) {
            if (!mBoundForBackgroundPeriod && nextBound) {
                addStrongBinding();
            } else if (mBoundForBackgroundPeriod && !nextBound) {
                removeStrongBinding(false);
            }

            mBoundForBackgroundPeriod = nextBound;
        }

        void clearConnection() {
            if (mModerateBindingPool != null) mModerateBindingPool.removeConnection(this);
            mConnection = null;
        }
    }

    private final SparseArray<ManagedConnection> mManagedConnections =
            new SparseArray<ManagedConnection>();

    // The connection that was most recently set as foreground (using setInForeground()). This is
    // used to add additional binding on it when the embedder goes to background. On low-end, this
    // is also used to drop process bindings when a new one is created, making sure that only one
    // renderer process at a time is protected from oom killing.
    private ManagedConnection mLastInForeground;

    // The connection bound with additional binding in onSentToBackground().
    private ManagedConnection mBoundForBackgroundPeriod;

    // Whether this instance is used on testing.
    private final boolean mOnTesting;

    /**
     * The constructor is private to hide parameters exposed for testing from the regular consumer.
     * Use factory methods to create an instance.
     */
    private BindingManagerImpl(boolean isLowMemoryDevice, boolean onTesting) {
        assert LauncherThread.runningOnLauncherThread();
        mIsLowMemoryDevice = isLowMemoryDevice;
        mOnTesting = onTesting;
    }

    public static BindingManagerImpl createBindingManager() {
        assert LauncherThread.runningOnLauncherThread();
        return new BindingManagerImpl(SysUtils.isLowEndDevice(), false);
    }

    /**
     * Creates a testing instance of BindingManager. Testing instance will have the unbinding delays
     * set to 0, so that the tests don't need to deal with actual waiting.
     * @param isLowEndDevice true iff the created instance should apply low-end binding policies
     */
    public static BindingManagerImpl createBindingManagerForTesting(boolean isLowEndDevice) {
        assert LauncherThread.runningOnLauncherThread();
        return new BindingManagerImpl(isLowEndDevice, true);
    }

    @Override
    public void addNewConnection(int pid, ManagedChildProcessConnection connection) {
        assert LauncherThread.runningOnLauncherThread();
        // This will reset the previous entry for the pid in the unlikely event of the OS
        // reusing renderer pids.
        mManagedConnections.put(pid, new ManagedConnection(connection));
    }

    @Override
    public void setInForeground(int pid, boolean inForeground) {
        assert LauncherThread.runningOnLauncherThread();
        ManagedConnection managedConnection = mManagedConnections.get(pid);
        if (managedConnection == null) {
            Log.w(TAG, "Cannot setInForeground() - never saw a connection for the pid: %d", pid);
            return;
        }

        if (inForeground && mIsLowMemoryDevice && mLastInForeground != null
                && mLastInForeground != managedConnection) {
            mLastInForeground.dropBindings();
        }

        managedConnection.setInForeground(inForeground);
        if (inForeground) mLastInForeground = managedConnection;
    }

    @Override
    public void onDeterminedVisibility(int pid) {
        assert LauncherThread.runningOnLauncherThread();
        ManagedConnection managedConnection = mManagedConnections.get(pid);
        if (managedConnection == null) {
            Log.w(TAG, "Cannot call determinedVisibility() - never saw a connection for the pid: "
                    + "%d", pid);
            return;
        }

        managedConnection.onDeterminedVisibility();
    }

    @Override
    public void onSentToBackground() {
        assert LauncherThread.runningOnLauncherThread();
        assert mBoundForBackgroundPeriod == null;
        // mLastInForeground can be null at this point as the embedding application could be
        // used in foreground without spawning any renderers.
        if (mLastInForeground != null) {
            mLastInForeground.setBoundForBackgroundPeriod(true);
            mBoundForBackgroundPeriod = mLastInForeground;
        }
        if (mModerateBindingPool != null) mModerateBindingPool.onSentToBackground(mOnTesting);
    }

    @Override
    public void onBroughtToForeground() {
        assert LauncherThread.runningOnLauncherThread();
        if (mBoundForBackgroundPeriod != null) {
            mBoundForBackgroundPeriod.setBoundForBackgroundPeriod(false);
            mBoundForBackgroundPeriod = null;
        }
        if (mModerateBindingPool != null) mModerateBindingPool.onBroughtToForeground();
    }

    @Override
    public void removeConnection(int pid) {
        assert LauncherThread.runningOnLauncherThread();
        ManagedConnection managedConnection = mManagedConnections.get(pid);
        if (managedConnection != null) {
            mManagedConnections.remove(pid);
            managedConnection.clearConnection();
        }
    }

    /** @return true iff the connection reference is no longer held */
    @VisibleForTesting
    public boolean isConnectionCleared(int pid) {
        assert LauncherThread.runningOnLauncherThread();
        return mManagedConnections.get(pid) == null;
    }

    @Override
    public void startModerateBindingManagement(Context context, int maxSize) {
        assert LauncherThread.runningOnLauncherThread();
        if (mIsLowMemoryDevice) return;

        if (mModerateBindingPool == null) {
            Log.i(TAG, "Moderate binding enabled: maxSize=%d", maxSize);
            mModerateBindingPool = new ModerateBindingPool(maxSize);
            if (context != null) {
                // Note that it is safe to call Context.registerComponentCallbacks from a background
                // thread.
                context.registerComponentCallbacks(mModerateBindingPool);
            }
        }
    }

    @Override
    public void releaseAllModerateBindings() {
        assert LauncherThread.runningOnLauncherThread();
        if (mModerateBindingPool != null) {
            Log.i(TAG, "Release all moderate bindings: %d", mModerateBindingPool.size());
            mModerateBindingPool.evictAll();
        }
    }
}
