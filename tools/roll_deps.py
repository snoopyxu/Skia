#!/usr/bin/python2

# Copyright 2014 Google Inc.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Skia's Chromium DEPS roll script.

This script:
- searches through the last N Skia git commits to find out the hash that is
  associated with the SVN revision number.
- creates a new branch in the Chromium tree, modifies the DEPS file to
  point at the given Skia commit, commits, uploads to Rietveld, and
  deletes the local copy of the branch.
- creates a whitespace-only commit and uploads that to to Rietveld.
- returns the Chromium tree to its previous state.

Usage:
  %prog -c CHROMIUM_PATH -r REVISION [OPTIONAL_OPTIONS]
"""


import optparse
import os
import re
import shutil
import subprocess
import sys
import tempfile


class DepsRollConfig(object):
    """Contains configuration options for this module.

    Attributes:
        git: (string) The git executable.
        chromium_path: (string) path to a local chromium git repository.
        save_branches: (boolean) iff false, delete temporary branches.
        verbose: (boolean)  iff false, suppress the output from git-cl.
        search_depth: (int) how far back to look for the revision.
        skia_url: (string) Skia's git repository.
        self.skip_cl_upload: (boolean)
        self.cl_bot_list: (list of strings)
    """

    # pylint: disable=I0011,R0903,R0902
    def __init__(self, options=None):
        self.skia_url = 'https://skia.googlesource.com/skia.git'
        self.revision_format = (
            'git-svn-id: http://skia.googlecode.com/svn/trunk@%d ')

        if not options:
            options = DepsRollConfig.GetOptionParser()
        # pylint: disable=I0011,E1103
        self.verbose = options.verbose
        self.vsp = VerboseSubprocess(self.verbose)
        self.save_branches = not options.delete_branches
        self.search_depth = options.search_depth
        self.chromium_path = options.chromium_path
        self.git = options.git_path
        self.skip_cl_upload = options.skip_cl_upload
        # Split and remove empty strigns from the bot list.
        self.cl_bot_list = [bot for bot in options.bots.split(',') if bot]
        self.skia_git_checkout_path = options.skia_git_path
        self.default_branch_name = 'autogenerated_deps_roll_branch'

    @staticmethod
    def GetOptionParser():
        # pylint: disable=I0011,C0103
        """Returns an optparse.OptionParser object.

        Returns:
            An optparse.OptionParser object.

        Called by the main() function.
        """
        default_bots_list = [
            'android_clang_dbg',
            'android_dbg',
            'android_rel',
            'cros_daisy',
            'linux',
            'linux_asan',
            'linux_chromeos',
            'linux_chromeos_asan',
            'linux_gpu',
            'linux_heapcheck',
            'linux_layout',
            'linux_layout_rel',
            'mac',
            'mac_asan',
            'mac_gpu',
            'mac_layout',
            'mac_layout_rel',
            'win',
            'win_gpu',
            'win_layout',
            'win_layout_rel',
            ]

        option_parser = optparse.OptionParser(usage=__doc__)
        # Anyone using this script on a regular basis should set the
        # CHROMIUM_CHECKOUT_PATH environment variable.
        option_parser.add_option(
            '-c', '--chromium_path', help='Path to local Chromium Git'
            ' repository checkout, defaults to CHROMIUM_CHECKOUT_PATH'
            ' if that environment variable is set.',
            default=os.environ.get('CHROMIUM_CHECKOUT_PATH'))
        option_parser.add_option(
            '-r', '--revision', type='int', default=None,
            help='The Skia SVN revision number, defaults to top of tree.')
        option_parser.add_option(
            '-g', '--git_hash', default=None,
            help='A partial Skia Git hash.  Do not set this and revision.')

        # Anyone using this script on a regular basis should set the
        # SKIA_GIT_CHECKOUT_PATH environment variable.
        option_parser.add_option(
            '', '--skia_git_path',
            help='Path of a pure-git Skia repository checkout.  If empty,'
            ' a temporary will be cloned.  Defaults to SKIA_GIT_CHECKOUT'
            '_PATH, if that environment variable is set.',
            default=os.environ.get('SKIA_GIT_CHECKOUT_PATH'))
        option_parser.add_option(
            '', '--search_depth', type='int', default=100,
            help='How far back to look for the revision.')
        option_parser.add_option(
            '', '--git_path', help='Git executable, defaults to "git".',
            default='git')
        option_parser.add_option(
            '', '--delete_branches', help='Delete the temporary branches',
            action='store_true', dest='delete_branches', default=False)
        option_parser.add_option(
            '', '--verbose', help='Do not suppress the output from `git cl`.',
            action='store_true', dest='verbose', default=False)
        option_parser.add_option(
            '', '--skip_cl_upload', help='Skip the cl upload step; useful'
            ' for testing or with --save_branches.',
            action='store_true', default=False)

        default_bots_help = (
            'Comma-separated list of bots, defaults to a list of %d bots.'
            '  To skip `git cl try`, set this to an empty string.'
            % len(default_bots_list))
        default_bots = ','.join(default_bots_list)
        option_parser.add_option(
            '', '--bots', help=default_bots_help, default=default_bots)

        return option_parser


def test_git_executable(git_executable):
    """Test the git executable.

    Args:
        git_executable: git executable path.
    Returns:
        True if test is successful.
    """
    with open(os.devnull, 'w') as devnull:
        try:
            subprocess.call([git_executable, '--version'], stdout=devnull)
        except (OSError,):
            return False
    return True


class DepsRollError(Exception):
    """Exceptions specific to this module."""
    pass


class VerboseSubprocess(object):
    """Call subprocess methods, but print out command before executing.

    Attributes:
        verbose: (boolean) should we print out the command or not.  If
                 not, this is the same as calling the subprocess method
        quiet: (boolean) suppress stdout on check_call and call.
        prefix: (string) When verbose, what to print before each command.
    """

    def __init__(self, verbose):
        self.verbose = verbose
        self.quiet = not verbose
        self.prefix = '~~$ '

    @staticmethod
    def _fix(string):
        """Quote and escape a string if necessary."""
        if ' ' in string or '\n' in string:
            string = '"%s"' % string.replace('\n', '\\n')
        return string

    @staticmethod
    def print_subprocess_args(prefix, *args, **kwargs):
        """Print out args in a human-readable manner."""
        if 'cwd' in kwargs:
            print '%scd %s' % (prefix, kwargs['cwd'])
        print prefix + ' '.join(VerboseSubprocess._fix(arg) for arg in args[0])
        if 'cwd' in kwargs:
            print '%scd -' % prefix

    def check_call(self, *args, **kwargs):
        """Wrapper for subprocess.check_call().

        Args:
            *args: to be passed to subprocess.check_call()
            **kwargs: to be passed to subprocess.check_call()
        Returns:
            Whatever subprocess.check_call() returns.
        Raises:
            OSError or subprocess.CalledProcessError: raised by check_call.
        """
        if self.verbose:
            self.print_subprocess_args(self.prefix, *args, **kwargs)
        if self.quiet:
            with open(os.devnull, 'w') as devnull:
                return subprocess.check_call(*args, stdout=devnull, **kwargs)
        else:
            return subprocess.check_call(*args, **kwargs)

    def call(self, *args, **kwargs):
        """Wrapper for subprocess.check().

        Args:
            *args: to be passed to subprocess.check_call()
            **kwargs: to be passed to subprocess.check_call()
        Returns:
            Whatever subprocess.call() returns.
        Raises:
            OSError or subprocess.CalledProcessError: raised by call.
        """
        if self.verbose:
            self.print_subprocess_args(self.prefix, *args, **kwargs)
        if self.quiet:
            with open(os.devnull, 'w') as devnull:
                return subprocess.call(*args, stdout=devnull, **kwargs)
        else:
            return subprocess.call(*args, **kwargs)

    def check_output(self, *args, **kwargs):
        """Wrapper for subprocess.check_output().

        Args:
            *args: to be passed to subprocess.check_output()
            **kwargs: to be passed to subprocess.check_output()
        Returns:
            Whatever subprocess.check_output() returns.
        Raises:
            OSError or subprocess.CalledProcessError: raised by check_output.
        """
        if self.verbose:
            self.print_subprocess_args(self.prefix, *args, **kwargs)
        return subprocess.check_output(*args, **kwargs)

    def strip_output(self, *args, **kwargs):
        """Wrap subprocess.check_output and str.strip().

        Pass the given arguments into subprocess.check_output() and return
        the results, after stripping any excess whitespace.

        Args:
            *args: to be passed to subprocess.check_output()
            **kwargs: to be passed to subprocess.check_output()

        Returns:
            The output of the process as a string without leading or
            trailing whitespace.
        Raises:
            OSError or subprocess.CalledProcessError: raised by check_output.
        """
        if self.verbose:
            self.print_subprocess_args(self.prefix, *args, **kwargs)
        return str(subprocess.check_output(*args, **kwargs)).strip()

    def popen(self, *args, **kwargs):
        """Wrapper for subprocess.Popen().

        Args:
            *args: to be passed to subprocess.Popen()
            **kwargs: to be passed to subprocess.Popen()
        Returns:
            The output of subprocess.Popen()
        Raises:
            OSError or subprocess.CalledProcessError: raised by Popen.
        """
        if self.verbose:
            self.print_subprocess_args(self.prefix, *args, **kwargs)
        return subprocess.Popen(*args, **kwargs)


class ChangeDir(object):
    """Use with a with-statement to temporarily change directories."""
    # pylint: disable=I0011,R0903

    def __init__(self, directory, verbose=False):
        self._directory = directory
        self._verbose = verbose

    def __enter__(self):
        if self._verbose:
            print '~~$ cd %s' % self._directory
        cwd = os.getcwd()
        os.chdir(self._directory)
        self._directory = cwd

    def __exit__(self, etype, value, traceback):
        if self._verbose:
            print '~~$ cd %s' % self._directory
        os.chdir(self._directory)


class ReSearch(object):
    """A collection of static methods for regexing things."""

    @staticmethod
    def search_within_stream(input_stream, pattern, default=None):
        """Search for regular expression in a file-like object.

        Opens a file for reading and searches line by line for a match to
        the regex and returns the parenthesized group named return for the
        first match.  Does not search across newlines.

        For example:
            pattern = '^root(:[^:]*){4}:(?P<return>[^:]*)'
            with open('/etc/passwd', 'r') as stream:
                return search_within_file(stream, pattern)
        should return root's home directory (/root on my system).

        Args:
            input_stream: file-like object to be read
            pattern: (string) to be passed to re.compile
            default: what to return if no match

        Returns:
            A string or whatever default is
        """
        pattern_object = re.compile(pattern)
        for line in input_stream:
            match = pattern_object.search(line)
            if match:
                return match.group('return')
        return default

    @staticmethod
    def search_within_string(input_string, pattern, default=None):
        """Search for regular expression in a string.

        Args:
            input_string: (string) to be searched
            pattern: (string) to be passed to re.compile
            default: what to return if no match

        Returns:
            A string or whatever default is
        """
        match = re.search(pattern, input_string)
        return match.group('return') if match else default

    @staticmethod
    def search_within_output(verbose, pattern, default, *args, **kwargs):
        """Search for regular expression in a process output.

        Does not search across newlines.

        Args:
            verbose: (boolean) shoule we call
                     VerboseSubprocess.print_subprocess_args?
            pattern: (string) to be passed to re.compile
            default: what to return if no match
            *args: to be passed to subprocess.Popen()
            **kwargs: to be passed to subprocess.Popen()

        Returns:
            A string or whatever default is
        """
        if verbose:
            VerboseSubprocess.print_subprocess_args(
                '~~$ ', *args, **kwargs)
        proc = subprocess.Popen(*args, stdout=subprocess.PIPE, **kwargs)
        return ReSearch.search_within_stream(proc.stdout, pattern, default)


def get_svn_revision(config, commit):
    """Works in both git and git-svn. returns a string."""
    svn_format = (
        '(git-svn-id: [^@ ]+@|SVN changes up to revision |'
        'LKGR w/ DEPS up to revision )(?P<return>[0-9]+)')
    svn_revision = ReSearch.search_within_output(
        config.verbose, svn_format, None,
        [config.git, 'log', '-n', '1', '--format=format:%B', commit])
    if not svn_revision:
        raise DepsRollError(
            'Revision number missing from Chromium origin/master.')
    return int(svn_revision)


class SkiaGitCheckout(object):
    """Class to create a temporary skia git checkout, if necessary.
    """
    # pylint: disable=I0011,R0903

    def __init__(self, config, depth):
        self._config = config
        self._depth = depth
        self._use_temp = None
        self._original_cwd = None

    def __enter__(self):
        config = self._config
        git = config.git
        skia_dir = None
        self._original_cwd = os.getcwd()
        if config.skia_git_checkout_path:
            skia_dir = config.skia_git_checkout_path
            ## Update origin/master if needed.
            if self._config.verbose:
                print '~~$', 'cd', skia_dir
            os.chdir(skia_dir)
            config.vsp.check_call([git, 'fetch', '-q', 'origin'])
            self._use_temp = None
        else:
            skia_dir = tempfile.mkdtemp(prefix='git_skia_tmp_')
            self._use_temp = skia_dir
            try:
                os.chdir(skia_dir)
                config.vsp.check_call(
                    [git, 'clone', '-q', '--depth=%d' % self._depth,
                     '--single-branch', config.skia_url, '.'])
            except (OSError, subprocess.CalledProcessError) as error:
                shutil.rmtree(skia_dir)
                raise error

    def __exit__(self, etype, value, traceback):
        if self._config.verbose:
            print '~~$', 'cd', self._original_cwd
        os.chdir(self._original_cwd)
        if self._use_temp:
            shutil.rmtree(self._use_temp)


def revision_and_hash(config):
    """Finds revision number and git hash of origin/master in the Skia tree.

    Args:
        config: (roll_deps.DepsRollConfig) object containing options.

    Returns:
        A tuple (revision, hash)
            revision: (int) SVN revision number.
            git_hash: (string) full Git commit hash.

    Raises:
        roll_deps.DepsRollError: if the revision can't be found.
        OSError: failed to execute git or git-cl.
        subprocess.CalledProcessError: git returned unexpected status.
    """
    with SkiaGitCheckout(config, 1):
        revision = get_svn_revision(config, 'origin/master')
        git_hash = config.vsp.strip_output(
            [config.git, 'show-ref', 'origin/master', '--hash'])
        if not git_hash:
            raise DepsRollError('Git hash can not be found.')
    return revision, git_hash


def revision_and_hash_from_revision(config, revision):
    """Finds revision number and git hash of a commit in the Skia tree.

    Args:
        config: (roll_deps.DepsRollConfig) object containing options.
        revision: (int) SVN revision number.

    Returns:
        A tuple (revision, hash)
            revision: (int) SVN revision number.
            git_hash: (string) full Git commit hash.

    Raises:
        roll_deps.DepsRollError: if the revision can't be found.
        OSError: failed to execute git or git-cl.
        subprocess.CalledProcessError: git returned unexpected status.
    """
    with SkiaGitCheckout(config, config.search_depth):
        revision_regex = config.revision_format % revision
        git_hash = config.vsp.strip_output(
            [config.git, 'log', '--grep', revision_regex,
             '--format=format:%H', 'origin/master'])
        if not git_hash:
            raise DepsRollError('Git hash can not be found.')
    return revision, git_hash


def revision_and_hash_from_partial(config, partial_hash):
    """Returns the SVN revision number and full git hash.

    Args:
        config: (roll_deps.DepsRollConfig) object containing options.
        partial_hash: (string) Partial git commit hash.

    Returns:
        A tuple (revision, hash)
            revision: (int) SVN revision number.
            git_hash: (string) full Git commit hash.

    Raises:
        roll_deps.DepsRollError: if the revision can't be found.
        OSError: failed to execute git or git-cl.
        subprocess.CalledProcessError: git returned unexpected status.
    """
    with SkiaGitCheckout(config, config.search_depth):
        git_hash = config.vsp.strip_output(
            ['git', 'log', '-n', '1', '--format=format:%H', partial_hash])
        if not git_hash:
            raise DepsRollError('Partial Git hash can not be found.')
        revision = get_svn_revision(config, git_hash)
    return revision, git_hash


class GitBranchCLUpload(object):
    """Class to manage git branches and git-cl-upload.

    This class allows one to create a new branch in a repository based
    off of origin/master, make changes to the tree inside the
    with-block, upload that new branch to Rietveld, restore the original
    tree state, and delete the local copy of the new branch.

    See roll_deps() for an example of use.

    Constructor Args:
        config: (roll_deps.DepsRollConfig) object containing options.
        message: (string) the commit message, can be multiline.
        set_brach_name: (string or none) if not None, the name of the
            branch to use.  If None, then use a temporary branch that
            will be deleted.

    Attributes:
        issue: a string describing the codereview issue, after __exit__
            has been called, othrwise, None.

    Raises:
        OSError: failed to execute git or git-cl.
        subprocess.CalledProcessError: git returned unexpected status.
    """
    # pylint: disable=I0011,R0903,R0902

    def __init__(self, config, message, set_branch_name):
        self._message = message
        self._file_list = []
        self._branch_name = set_branch_name
        self._stash = None
        self._original_branch = None
        self._config = config
        self.issue = None

    def stage_for_commit(self, *paths):
        """Calls `git add ...` on each argument.

        Args:
            *paths: (list of strings) list of filenames to pass to `git add`.
        """
        self._file_list.extend(paths)

    def __enter__(self):
        git = self._config.git
        vsp = self._config.vsp
        def branch_exists(branch):
            """Return true iff branch exists."""
            return 0 == vsp.call([git, 'show-ref', '--quiet', branch])
        def has_diff():
            """Return true iff repository has uncommited changes."""
            return bool(vsp.call([git, 'diff', '--quiet', 'HEAD']))

        self._stash = has_diff()
        if self._stash:
            vsp.check_call([git, 'stash', 'save'])
        try:
            self._original_branch = vsp.strip_output(
                [git, 'symbolic-ref', '--short', 'HEAD'])
        except (subprocess.CalledProcessError,):
            self._original_branch = vsp.strip_output(
                [git, 'rev-parse', 'HEAD'])

        if not self._branch_name:
            self._branch_name = self._config.default_branch_name

        if branch_exists(self._branch_name):
            vsp.check_call([git, 'checkout', '-q', 'master'])
            vsp.check_call([git, 'branch', '-q', '-D', self._branch_name])

        vsp.check_call(
            [git, 'checkout', '-q', '-b', self._branch_name, 'origin/master'])

    def __exit__(self, etype, value, traceback):
        # pylint: disable=I0011,R0912
        git = self._config.git
        vsp = self._config.vsp
        svn_info = str(get_svn_revision(self._config, 'HEAD'))

        for filename in self._file_list:
            assert os.path.exists(filename)
            vsp.check_call([git, 'add', filename])
        vsp.check_call([git, 'commit', '-q', '-m', self._message])

        git_cl = [git, 'cl', 'upload', '-f', '--cc=skia-team@google.com',
                  '--bypass-hooks', '--bypass-watchlists']
        git_try = [git, 'cl', 'try', '--revision', svn_info]
        git_try.extend([arg for bot in self._config.cl_bot_list
                        for arg in ('-b', bot)])

        if self._config.skip_cl_upload:
            print 'You should call:'
            print '    cd %s' % os.getcwd()
            VerboseSubprocess.print_subprocess_args(
                '    ', [git, 'checkout', self._branch_name])
            VerboseSubprocess.print_subprocess_args('    ', git_cl)
            if self._config.cl_bot_list:
                VerboseSubprocess.print_subprocess_args('    ', git_try)
            print
            self.issue = ''
        else:
            vsp.check_call(git_cl)
            self.issue = vsp.strip_output([git, 'cl', 'issue'])
            if self._config.cl_bot_list:
                vsp.check_call(git_try)

        # deal with the aftermath of failed executions of this script.
        if self._config.default_branch_name == self._original_branch:
            self._original_branch = 'master'
        vsp.check_call([git, 'checkout', '-q', self._original_branch])

        if self._config.default_branch_name == self._branch_name:
            vsp.check_call([git, 'branch', '-q', '-D', self._branch_name])
        if self._stash:
            vsp.check_call([git, 'stash', 'pop'])


def change_skia_deps(revision, git_hash, depspath):
    """Update the DEPS file.

    Modify the skia_revision and skia_hash entries in the given DEPS file.

    Args:
        revision: (int) Skia SVN revision.
        git_hash: (string) Skia Git hash.
        depspath: (string) path to DEPS file.
    """
    temp_file = tempfile.NamedTemporaryFile(delete=False,
                                            prefix='skia_DEPS_ROLL_tmp_')
    try:
        deps_regex_rev = re.compile('"skia_revision": "[0-9]*",')
        deps_regex_hash = re.compile('"skia_hash": "[0-9a-f]*",')

        deps_regex_rev_repl = '"skia_revision": "%d",' % revision
        deps_regex_hash_repl = '"skia_hash": "%s",' % git_hash

        with open(depspath, 'r') as input_stream:
            for line in input_stream:
                line = deps_regex_rev.sub(deps_regex_rev_repl, line)
                line = deps_regex_hash.sub(deps_regex_hash_repl, line)
                temp_file.write(line)
    finally:
        temp_file.close()
    shutil.move(temp_file.name, depspath)


def roll_deps(config, revision, git_hash):
    """Upload changed DEPS and a whitespace change.

    Given the correct git_hash, create two Reitveld issues.

    Args:
        config: (roll_deps.DepsRollConfig) object containing options.
        revision: (int) Skia SVN revision.
        git_hash: (string) Skia Git hash.

    Returns:
        a tuple containing textual description of the two issues.

    Raises:
        OSError: failed to execute git or git-cl.
        subprocess.CalledProcessError: git returned unexpected status.
    """

    git = config.git
    with ChangeDir(config.chromium_path, config.verbose):
        config.vsp.check_call([git, 'fetch', '-q', 'origin'])

        old_revision = ReSearch.search_within_output(
            config.verbose, '"skia_revision": "(?P<return>[0-9]+)",', None,
            [git, 'show', 'origin/master:DEPS'])
        assert old_revision
        if revision == int(old_revision):
            print 'DEPS is up to date!'
            return None

        master_hash = config.vsp.strip_output(
            [git, 'show-ref', 'origin/master', '--hash'])
        master_revision = get_svn_revision(config, 'origin/master')

        branch = None

        # master_hash[8] gives each whitespace CL a unique name.
        message = ('whitespace change %s\n\n'
                   'Chromium base revision: %d / %s\n\n'
                   'This CL was created by Skia\'s roll_deps.py script.\n'
                  ) % (master_hash[:8], master_revision, master_hash[:8])
        if config.save_branches:
            branch = 'control_%s' % master_hash[:8]

        codereview = GitBranchCLUpload(config, message, branch)
        with codereview:
            with open('build/whitespace_file.txt', 'a') as output_stream:
                output_stream.write('\nCONTROL\n')
            codereview.stage_for_commit('build/whitespace_file.txt')
        whitespace_cl = codereview.issue
        if branch:
            whitespace_cl = '%s\n    branch: %s' % (whitespace_cl, branch)

        control_url = ReSearch.search_within_string(
            codereview.issue, '(?P<return>https?://[^) ]+)', '?')

        if config.save_branches:
            branch = 'roll_%d_%s' % (revision, master_hash[:8])
        message = (
            'roll skia DEPS to %d\n\n'
            'Chromium base revision: %d / %s\n'
            'Old Skia revision: %s\n'
            'New Skia revision: %d\n'
            'Control CL: %s\n\n'
            'This CL was created by Skia\'s roll_deps.py script.\n'
            % (revision, master_revision, master_hash[:8],
               old_revision, revision, control_url))
        codereview = GitBranchCLUpload(config, message, branch)
        with codereview:
            change_skia_deps(revision, git_hash, 'DEPS')
            codereview.stage_for_commit('DEPS')
        deps_cl = codereview.issue
        if branch:
            deps_cl = '%s\n    branch: %s' % (deps_cl, branch)

        return deps_cl, whitespace_cl


def find_hash_and_roll_deps(config, revision=None, partial_hash=None):
    """Call find_hash_from_revision() and roll_deps().

    The calls to git will be verbose on standard output.  After a
    successful upload of both issues, print links to the new
    codereview issues.

    Args:
        config: (roll_deps.DepsRollConfig) object containing options.
        revision: (int or None) the Skia SVN revision number or None
            to use the tip of the tree.
        partial_hash: (string or None) a partial pure-git Skia commit
            hash.  Don't pass both partial_hash and revision.

    Raises:
        roll_deps.DepsRollError: if the revision can't be found.
        OSError: failed to execute git or git-cl.
        subprocess.CalledProcessError: git returned unexpected status.
    """

    if revision and partial_hash:
        raise DepsRollError('Pass revision or partial_hash, not both.')

    if partial_hash:
        revision, git_hash = revision_and_hash_from_partial(
            config, partial_hash)
    elif revision:
        revision, git_hash = revision_and_hash_from_revision(config, revision)
    else:
        revision, git_hash = revision_and_hash(config)

    print 'revision=%r\nhash=%r\n' % (revision, git_hash)

    roll = roll_deps(config, revision, git_hash)

    if roll:
        deps_issue, whitespace_issue = roll
        print 'DEPS roll:\n    %s\n' % deps_issue
        print 'Whitespace change:\n    %s\n' % whitespace_issue


def main(args):
    """main function; see module-level docstring and GetOptionParser help.

    Args:
        args: sys.argv[1:]-type argument list.
    """
    option_parser = DepsRollConfig.GetOptionParser()
    options = option_parser.parse_args(args)[0]

    if not options.chromium_path:
        option_parser.error('Must specify chromium_path.')
    if not os.path.isdir(options.chromium_path):
        option_parser.error('chromium_path must be a directory.')
    if not test_git_executable(options.git_path):
        option_parser.error('Invalid git executable.')

    config = DepsRollConfig(options)
    find_hash_and_roll_deps(config, options.revision, options.git_hash)


if __name__ == '__main__':
    main(sys.argv[1:])
