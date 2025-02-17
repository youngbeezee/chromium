# Copyright (c) 2012 Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import sys


def add_typ_dir_to_sys_path():
    path_to_typ = get_typ_dir()
    if path_to_typ not in sys.path:
        sys.path.append(path_to_typ)


def add_bindings_scripts_dir_to_sys_path():
    path_to_bindings_scripts = get_bindings_scripts_dir()
    if path_to_bindings_scripts not in sys.path:
        sys.path.append(path_to_bindings_scripts)


def get_bindings_scripts_dir():
    return os.path.join(get_source_dir(), 'bindings', 'scripts')


def get_blink_dir():
    return os.path.dirname(os.path.dirname(get_scripts_dir()))


def get_chromium_src_dir():
    return os.path.dirname(os.path.dirname(get_blink_dir()))


def get_scripts_dir():
    return os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.realpath(__file__))))


def get_source_dir():
    return os.path.join(get_blink_dir(), 'Source')


def get_typ_dir():
    return os.path.join(get_chromium_src_dir(), 'third_party', 'typ')


def get_webkitpy_thirdparty_dir():
    return os.path.join(get_scripts_dir(), 'webkitpy', 'thirdparty')


class WebKitFinder(object):

    def __init__(self, filesystem):
        self._filesystem = filesystem
        self._dirsep = filesystem.sep
        self._sys_path = sys.path
        self._env_path = os.environ['PATH'].split(os.pathsep)
        self._webkit_base = None
        self._chromium_base = None
        self._depot_tools = None

    # TODO(tkent): Make this private. We should use functions for
    # sub-directories in order to make the code robust against directory
    # structure changes.
    def webkit_base(self):
        """Returns the absolute path to the top of the WebKit tree.

        Raises an AssertionError if the top dir can't be determined.
        """
        # TODO(qyearsley): This code somewhat duplicates the code in
        # git.find_checkout_root().
        if not self._webkit_base:
            self._webkit_base = self._webkit_base
            module_path = self._filesystem.abspath(self._filesystem.path_to_module(self.__module__))
            tools_index = module_path.rfind('Tools')
            assert tools_index != -1, 'could not find location of this checkout from %s' % module_path
            self._webkit_base = self._filesystem.normpath(module_path[0:tools_index - 1])
        return self._webkit_base

    def chromium_base(self):
        if not self._chromium_base:
            self._chromium_base = self._filesystem.dirname(self._filesystem.dirname(self.webkit_base()))
        return self._chromium_base

    # Do not expose this function in order to make the code robust against
    # directory structure changes.
    def _path_from_webkit_base(self, *comps):
        return self._filesystem.join(self.webkit_base(), *comps)

    def path_from_chromium_base(self, *comps):
        return self._filesystem.join(self.chromium_base(), *comps)

    def path_from_blink_source(self, *comps):
        return self._filesystem.join(self._filesystem.join(self.webkit_base(), 'Source'), *comps)

    def path_from_tools_scripts(self, *comps):
        return self._filesystem.join(self._filesystem.join(self.webkit_base(), 'Tools', 'Scripts'), *comps)

    def layout_tests_dir(self):
        return self._path_from_webkit_base('LayoutTests')

    def path_from_layout_tests(self, *comps):
        return self._filesystem.join(self.layout_tests_dir(), *comps)

    def perf_tests_dir(self):
        return self._path_from_webkit_base('PerformanceTests')

    def layout_test_name(self, file_path):
        """Returns a layout test name, given the path from the repo root.

        Note: this appears to not work on Windows; see crbug.com/658795.
        Also, this function duplicates functionality that's in
        Port.relative_test_filename.
        TODO(qyearsley): De-duplicate this and Port.relative_test_filename,
        and ensure that it works properly with Windows paths.

        Args:
            file_path: A relative path from the root of the Chromium repo.

        Returns:
            The normalized layout test name, which is just the relative path from
            the LayoutTests directory, using forward slash as the path separator.
            Returns None if the given file is not in the LayoutTests directory.
        """
        layout_tests_abs_path = self._filesystem.join(self.webkit_base(), self.layout_tests_dir())
        layout_tests_rel_path = self._filesystem.relpath(layout_tests_abs_path, self.chromium_base())
        if not file_path.startswith(layout_tests_rel_path):
            return None
        return file_path[len(layout_tests_rel_path) + 1:]

    def depot_tools_base(self):
        if not self._depot_tools:
            # This basically duplicates src/build/find_depot_tools.py without the side effects
            # (adding the directory to sys.path and importing breakpad).
            self._depot_tools = (self._check_paths_for_depot_tools(self._sys_path) or
                                 self._check_paths_for_depot_tools(self._env_path) or
                                 self._check_upward_for_depot_tools())
        return self._depot_tools

    def _check_paths_for_depot_tools(self, paths):
        for path in paths:
            if path.rstrip(self._dirsep).endswith('depot_tools'):
                return path
        return None

    def _check_upward_for_depot_tools(self):
        fs = self._filesystem
        prev_dir = ''
        current_dir = fs.dirname(self._webkit_base)
        while current_dir != prev_dir:
            if fs.exists(fs.join(current_dir, 'depot_tools', 'pylint.py')):
                return fs.join(current_dir, 'depot_tools')
            prev_dir = current_dir
            current_dir = fs.dirname(current_dir)

    def path_from_depot_tools_base(self, *comps):
        return self._filesystem.join(self.depot_tools_base(), *comps)
