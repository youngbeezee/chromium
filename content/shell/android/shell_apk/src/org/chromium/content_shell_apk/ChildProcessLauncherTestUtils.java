// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_shell_apk;

import android.content.Context;

import org.chromium.base.process_launcher.ChildProcessCreationParams;
import org.chromium.base.process_launcher.FileDescriptorInfo;
import org.chromium.base.process_launcher.IChildProcessService;
import org.chromium.content.browser.BaseChildProcessConnection;
import org.chromium.content.browser.ChildProcessLauncher;
import org.chromium.content.browser.LauncherThread;

import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.Semaphore;

/** An assortment of static methods used in tests that deal with launching child processes. */
public final class ChildProcessLauncherTestUtils {
    // Do not instanciate, use static methods instead.
    private ChildProcessLauncherTestUtils() {}

    public static void runOnLauncherThreadBlocking(final Runnable runnable) {
        if (LauncherThread.runningOnLauncherThread()) {
            runnable.run();
            return;
        }
        final Semaphore done = new Semaphore(0);
        LauncherThread.post(new Runnable() {
            @Override
            public void run() {
                runnable.run();
                done.release();
            }
        });
        done.acquireUninterruptibly();
    }

    public static <R> R runOnLauncherAndGetResult(Callable<R> callable) {
        if (LauncherThread.runningOnLauncherThread()) {
            try {
                return callable.call();
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }
        try {
            FutureTask<R> task = new FutureTask<R>(callable);
            LauncherThread.post(task);
            return task.get();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    public static BaseChildProcessConnection startInternalForTesting(final Context context,
            final String[] commandLine, final FileDescriptorInfo[] filesToMap,
            final ChildProcessCreationParams params) {
        return runOnLauncherAndGetResult(new Callable<BaseChildProcessConnection>() {
            @Override
            public BaseChildProcessConnection call() {
                return ChildProcessLauncher.startInternal(context, commandLine,
                        0 /* childProcessId */, filesToMap, null /* launchCallback */,
                        null /* childProcessCallback */, true /* inSandbox */,
                        false /* alwaysInForeground */, params);
            }
        });
    }

    // Retrieves the PID of the passed in connection on the launcher thread as to not assert.
    public static int getConnectionPid(final BaseChildProcessConnection connection) {
        return runOnLauncherAndGetResult(new Callable<Integer>() {
            @Override
            public Integer call() {
                return connection.getPid();
            }
        });
    }

    // Retrieves the service number of the passed in connection on the launcher thread as to not
    // assert.
    public static int getConnectionServiceNumber(final BaseChildProcessConnection connection) {
        return runOnLauncherAndGetResult(new Callable<Integer>() {
            @Override
            public Integer call() {
                return connection.getServiceNumber();
            }
        });
    }

    // Retrieves the service of the passed in connection on the launcher thread as to not assert.
    public static IChildProcessService getConnectionService(
            final BaseChildProcessConnection connection) {
        return runOnLauncherAndGetResult(new Callable<IChildProcessService>() {
            @Override
            public IChildProcessService call() {
                return connection.getService();
            }
        });
    }
}
