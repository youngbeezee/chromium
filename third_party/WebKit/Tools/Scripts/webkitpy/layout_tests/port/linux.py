# Copyright (C) 2010 Google Inc. All rights reserved.
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

import logging
import tempfile

from webkitpy.layout_tests.breakpad.dump_reader_multipart import DumpReaderLinux
from webkitpy.layout_tests.port import base
from webkitpy.layout_tests.port import win


_log = logging.getLogger(__name__)


class LinuxPort(base.Port):
    port_name = 'linux'

    SUPPORTED_VERSIONS = ('trusty',)

    FALLBACK_PATHS = {}
    FALLBACK_PATHS['trusty'] = ['linux'] + win.WinPort.latest_platform_fallback_path()

    DEFAULT_BUILD_DIRECTORIES = ('out',)

    BUILD_REQUIREMENTS_URL = 'https://chromium.googlesource.com/chromium/src/+/master/docs/linux_build_instructions.md'

    XVFB_START_TIMEOUT = 5.0  # Wait up to 5 seconds for Xvfb to start.

    @classmethod
    def determine_full_port_name(cls, host, options, port_name):
        if port_name.endswith('linux'):
            assert host.platform.is_linux()
            version = host.platform.os_version
            return port_name + '-' + version
        return port_name

    def __init__(self, host, port_name, **kwargs):
        super(LinuxPort, self).__init__(host, port_name, **kwargs)
        self._version = port_name[port_name.index('linux-') + len('linux-'):]
        self._architecture = 'x86_64'
        assert self._version in self.SUPPORTED_VERSIONS

        if not self.get_option('disable_breakpad'):
            self._dump_reader = DumpReaderLinux(host, self._build_path())
        self._original_home = None
        self._original_display = None
        self._xvfb_process = None
        self._xvfb_stdout = None
        self._xvfb_stderr = None

    def additional_driver_flag(self):
        flags = super(LinuxPort, self).additional_driver_flag()
        if not self.get_option('disable_breakpad'):
            flags += ['--enable-crash-reporter', '--crash-dumps-dir=%s' % self._dump_reader.crash_dumps_directory()]
        return flags

    def check_build(self, needs_http, printer):
        result = super(LinuxPort, self).check_build(needs_http, printer)

        if result:
            _log.error('For complete Linux build requirements, please see:')
            _log.error('')
            _log.error('    https://chromium.googlesource.com/chromium/src/+/master/docs/linux_build_instructions.md')
        return result

    def look_for_new_crash_logs(self, crashed_processes, start_time):
        if self.get_option('disable_breakpad'):
            return None
        return self._dump_reader.look_for_new_crash_logs(crashed_processes, start_time)

    def clobber_old_port_specific_results(self):
        if not self.get_option('disable_breakpad'):
            self._dump_reader.clobber_old_results()

    def operating_system(self):
        return 'linux'

    def path_to_apache(self):
        # The Apache binary path can vary depending on OS and distribution
        # See http://wiki.apache.org/httpd/DistrosDefaultLayout
        for path in ['/usr/sbin/httpd', '/usr/sbin/apache2']:
            if self._filesystem.exists(path):
                return path
        _log.error('Could not find apache. Not installed or unknown path.')
        return None

    def setup_test_run(self):
        super(LinuxPort, self).setup_test_run()
        self._start_xvfb()
        self._setup_dummy_home_dir()

    def clean_up_test_run(self):
        super(LinuxPort, self).clean_up_test_run()
        self._stop_xvfb()
        self._clean_up_dummy_home_dir()

    #
    # PROTECTED METHODS
    #

    def _setup_dummy_home_dir(self):
        """Creates a dummy home directory for running the test.

        This is a workaround for crbug.com/595504; see crbug.com/612730.
        If crbug.com/612730 is resolved in another way, then this may be
        unnecessary.
        """
        self._original_home = self.host.environ.get('HOME')
        dummy_home = str(self._filesystem.mkdtemp())
        self.host.environ['HOME'] = dummy_home
        self._copy_files_to_dummy_home_dir(dummy_home)

    def _copy_files_to_dummy_home_dir(self, dummy_home):
        # Note: This may be unnecessary.
        fs = self._filesystem
        for filename in ['.Xauthority']:
            original_path = fs.join(self._original_home, filename)
            if not fs.exists(original_path):
                continue
            fs.copyfile(original_path, fs.join(dummy_home, filename))

    def _clean_up_dummy_home_dir(self):
        """Cleans up the dummy dir and resets the HOME environment variable."""
        dummy_home = self.host.environ['HOME']
        assert dummy_home != self._original_home
        self._filesystem.rmtree(dummy_home)
        self.host.environ['HOME'] = self._original_home

    def _start_xvfb(self):
        display = self._find_display()
        if not display:
            _log.warn('Failed to find a free display to start Xvfb.')
            return

        _log.info('Starting Xvfb with display "%s".', display)
        self._xvfb_stdout = tempfile.NamedTemporaryFile(delete=False)
        self._xvfb_stderr = tempfile.NamedTemporaryFile(delete=False)
        self._xvfb_process = self.host.executive.popen(
            ['Xvfb', display, '-screen', '0', '1280x800x24', '-ac', '-dpi', '96'],
            stdout=self._xvfb_stdout, stderr=self._xvfb_stderr)

        # By setting DISPLAY here, the individual worker processes will
        # get the right DISPLAY. Note, if this environment could be passed
        # when creating workers, then we wouldn't need to modify DISPLAY here.
        self._original_display = self.host.environ.get('DISPLAY')
        self.host.environ['DISPLAY'] = display

        # The poll() method will return None if the process has not terminated:
        # https://docs.python.org/2/library/subprocess.html#subprocess.Popen.poll
        if self._xvfb_process.poll() is not None:
            _log.warn('Failed to start Xvfb on display "%s."', display)
            self._stop_xvfb()

        start_time = self.host.time()
        while self.host.time() - start_time < self.XVFB_START_TIMEOUT:
            # We don't explicitly set the display, as we want to check the
            # environment value.
            exit_code = self.host.executive.run_command(
                ['xdpyinfo'], return_exit_code=True)
            if exit_code == 0:
                _log.info('Successfully started Xvfb with display "%s".', display)
                return
            _log.warn('xdpyinfo check failed with exit code %s while starting Xvfb on "%s".', exit_code, display)
            self.host.sleep(0.1)
        _log.fatal('Failed to start Xvfb on display "%s" (xdpyinfo check failed).', display)
        self._stop_xvfb()

    def _find_display(self):
        """Tries to find a free X display, looping if necessary."""
        # The "xvfb-run" command uses :99 by default.
        for display_number in range(99, 120):
            display = ':%d' % display_number
            exit_code = self.host.executive.run_command(
                ['xdpyinfo', '-display', display], return_exit_code=True)
            if exit_code == 1:
                return display
        return None

    def _stop_xvfb(self):
        if self._original_display:
            self.host.environ['DISPLAY'] = self._original_display
        if self._xvfb_stdout:
            self._xvfb_stdout.close()
        if self._xvfb_stderr:
            self._xvfb_stderr.close()
        if self._xvfb_process:
            _log.debug('Killing Xvfb process pid %d.', self._xvfb_process.pid)
            self._xvfb_process.kill()
            self._xvfb_process.wait()
        if self._xvfb_stdout and self.host.filesystem.exists(self._xvfb_stdout.name):
            for line in self.host.filesystem.read_text_file(self._xvfb_stdout.name).splitlines():
                _log.warn('Xvfb stdout:  %s', line)
            self.host.filesystem.remove(self._xvfb_stdout.name)
        if self._xvfb_stderr and self.host.filesystem.exists(self._xvfb_stderr.name):
            for line in self.host.filesystem.read_text_file(self._xvfb_stderr.name).splitlines():
                _log.warn('Xvfb stderr:  %s', line)
            self.host.filesystem.remove(self._xvfb_stderr.name)
        self._xvfb_stdout = self._xvfb_stderr = self._xvfb_process = None

    def _path_to_driver(self, target=None):
        binary_name = self.driver_name()
        return self._build_path_with_target(target, binary_name)
