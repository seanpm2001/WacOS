# utils/update_checkout.py - Utility to update local checkouts --*- python -*-
#
# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See https://swift.org/LICENSE.txt for license information
# See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import argparse
import json
import os
import platform
import re
import sys
import traceback
from multiprocessing import Lock, Pool, cpu_count, freeze_support

from build_swift.build_swift.constants import SWIFT_SOURCE_ROOT

from swift_build_support.swift_build_support import shell


SCRIPT_FILE = os.path.abspath(__file__)
SCRIPT_DIR = os.path.dirname(SCRIPT_FILE)


def child_init(lck):
    global lock
    lock = lck


def run_parallel(fn, pool_args, n_processes=0):
    """Function used to run a given closure in parallel.

    NOTE: This function was originally located in the shell module of
    swift_build_support and should eventually be replaced with a better
    parallel implementation.
    """

    if n_processes == 0:
        n_processes = cpu_count() * 2

    lk = Lock()
    print("Running ``%s`` with up to %d processes." %
          (fn.__name__, n_processes))
    pool = Pool(processes=n_processes, initializer=child_init, initargs=(lk,))
    results = pool.map_async(func=fn, iterable=pool_args).get(999999)
    pool.close()
    pool.join()
    return results


def check_parallel_results(results, op):
    """Function used to check the results of run_parallel.

    NOTE: This function was originally located in the shell module of
    swift_build_support and should eventually be replaced with a better
    parallel implementation.
    """

    fail_count = 0
    if results is None:
        return 0
    for r in results:
        if r is not None:
            if fail_count == 0:
                print("======%s FAILURES======" % op)
            print("%s failed (ret=%d): %s" % (r.repo_path, r.ret, r))
            fail_count += 1
            if r.stderr:
                print(r.stderr.decode('utf-8'))
    return fail_count


def confirm_tag_in_repo(tag, repo_name):
    tag_exists = shell.capture(['git', 'ls-remote', '--tags',
                                'origin', tag], echo=False)
    if not tag_exists:
        print("Tag '" + tag + "' does not exist for '" +
              repo_name + "', just updating regularly")
        tag = None
    return tag


def find_rev_by_timestamp(timestamp, repo_name, refspec):
    args = ["git", "log", "-1", "--format=%H", "--first-parent",
            '--before=' + timestamp, refspec]
    rev = shell.capture(args).strip()
    if rev:
        return rev
    else:
        raise RuntimeError('No rev in %s before timestamp %s' %
                           (repo_name, timestamp))


def get_branch_for_repo(config, repo_name, scheme_name, scheme_map,
                        cross_repos_pr):
    cross_repo = False
    repo_branch = scheme_name
    if scheme_map:
        scheme_branch = scheme_map[repo_name]
        repo_branch = scheme_branch
        remote_repo_id = config['repos'][repo_name]['remote']['id']
        if remote_repo_id in cross_repos_pr:
            cross_repo = True
            pr_id = cross_repos_pr[remote_repo_id]
            repo_branch = "ci_pr_{0}".format(pr_id)
            shell.run(["git", "checkout", scheme_branch],
                      echo=True)
            shell.capture(["git", "branch", "-D", repo_branch],
                          echo=True, allow_non_zero_exit=True)
            shell.run(["git", "fetch", "origin",
                       "pull/{0}/merge:{1}"
                       .format(pr_id, repo_branch), "--tags"], echo=True)
    return repo_branch, cross_repo


def update_single_repository(pool_args):
    source_root, config, repo_name, scheme_name, scheme_map, tag, timestamp, \
        reset_to_remote, should_clean, cross_repos_pr = pool_args
    repo_path = os.path.join(source_root, repo_name)
    if not os.path.isdir(repo_path) or os.path.islink(repo_path):
        return

    try:
        print("Updating '" + repo_path + "'")
        with shell.pushd(repo_path, dry_run=False, echo=False):
            cross_repo = False
            checkout_target = None
            if tag:
                checkout_target = confirm_tag_in_repo(tag, repo_name)
            elif scheme_name:
                checkout_target, cross_repo = get_branch_for_repo(
                    config, repo_name, scheme_name, scheme_map, cross_repos_pr)
                if timestamp:
                    checkout_target = find_rev_by_timestamp(timestamp,
                                                            repo_name,
                                                            checkout_target)

            # The clean option restores a repository to pristine condition.
            if should_clean:
                shell.run(['git', 'clean', '-fdx'], echo=True)
                shell.run(['git', 'submodule', 'foreach', '--recursive', 'git',
                           'clean', '-fdx'], echo=True)
                shell.run(['git', 'submodule', 'foreach', '--recursive', 'git',
                           'reset', '--hard', 'HEAD'], echo=True)
                shell.run(['git', 'reset', '--hard', 'HEAD'], echo=True)
                # It is possible to reset --hard and still be mid-rebase.
                try:
                    shell.run(['git', 'rebase', '--abort'], echo=True)
                except Exception:
                    pass

            if checkout_target:
                shell.run(['git', 'status', '--porcelain', '-uno'],
                          echo=False)
                try:
                    shell.run(['git', 'checkout', checkout_target], echo=True)
                except Exception as originalException:
                    try:
                        result = shell.run(['git', 'rev-parse', checkout_target])
                        revision = result[0].strip()
                        shell.run(['git', 'checkout', revision], echo=True)
                    except Exception:
                        raise originalException

            # It's important that we checkout, fetch, and rebase, in order.
            # .git/FETCH_HEAD updates the not-for-merge attributes based on
            # which branch was checked out during the fetch.
            shell.run(["git", "fetch", "--recurse-submodules=yes", "--tags"],
                      echo=True)

            # If we were asked to reset to the specified branch, do the hard
            # reset and return.
            if checkout_target and reset_to_remote and not cross_repo:
                full_target = full_target_name('origin', checkout_target)
                shell.run(['git', 'reset', '--hard', full_target], echo=True)
                return

            # Query whether we have a "detached HEAD", which will mean that
            # we previously checked out a tag rather than a branch.
            detached_head = False
            try:
                # This git command returns error code 1 if HEAD is detached.
                # Otherwise there was some other error, and we need to handle
                # it like other command errors.
                shell.run(["git", "symbolic-ref", "-q", "HEAD"], echo=False)
            except Exception as e:
                if e.ret == 1:
                    detached_head = True
                else:
                    raise  # Pass this error up the chain.

            # If we have a detached HEAD in this repository, we don't want
            # to rebase. With a detached HEAD, the fetch will have marked
            # all the branches in FETCH_HEAD as not-for-merge, and the
            # "git rebase FETCH_HEAD" will try to rebase the tree from the
            # default branch's current head, making a mess.

            # Prior to Git 2.6, this is the way to do a "git pull
            # --rebase" that respects rebase.autostash.  See
            # http://stackoverflow.com/a/30209750/125349
            if not cross_repo and not detached_head:
                shell.run(["git", "rebase", "FETCH_HEAD"], echo=True)
            elif detached_head:
                print(repo_path,
                      "\nDetached HEAD; probably checked out a tag. No need "
                      "to rebase.\n")

            shell.run(["git", "submodule", "update", "--recursive"], echo=True)
    except Exception:
        (type, value, tb) = sys.exc_info()
        print('Error on repo "%s": %s' % (repo_path, traceback.format_exc()))
        return value


def get_timestamp_to_match(args):
    if not args.match_timestamp:
        return None
    with shell.pushd(os.path.join(args.source_root, "swift"),
                     dry_run=False, echo=False):
        return shell.capture(["git", "log", "-1", "--format=%cI"],
                             echo=False).strip()


def update_all_repositories(args, config, scheme_name, cross_repos_pr):
    scheme_map = None
    if scheme_name:
        # This loop is only correct, since we know that each alias set has
        # unique contents. This is checked by validate_config. Thus the first
        # branch scheme data that has scheme_name as one of its aliases is
        # the only possible correct answer.
        for v in config['branch-schemes'].values():
            if scheme_name in v['aliases']:
                scheme_map = v['repos']
                break
    pool_args = []
    timestamp = get_timestamp_to_match(args)
    for repo_name in config['repos'].keys():
        if repo_name in args.skip_repository_list:
            print("Skipping update of '" + repo_name + "', requested by user")
            continue
        my_args = [args.source_root, config,
                   repo_name,
                   scheme_name,
                   scheme_map,
                   args.tag,
                   timestamp,
                   args.reset_to_remote,
                   args.clean,
                   cross_repos_pr]
        pool_args.append(my_args)

    return run_parallel(update_single_repository, pool_args, args.n_processes)


def obtain_additional_swift_sources(pool_args):
    (args, repo_name, repo_info, repo_branch, remote, with_ssh, scheme_name,
     skip_history, skip_tags, skip_repository_list) = pool_args

    env = dict(os.environ)
    env.update({'GIT_TERMINAL_PROMPT': 0})

    with shell.pushd(args.source_root, dry_run=False, echo=False):

        print("Cloning '" + repo_name + "'")

        if skip_history:
            shell.run(['git', 'clone', '--recursive', '--depth', '1',
                       '--branch', repo_branch, remote, repo_name] +
                      (['--no-tags'] if skip_tags else []),
                      env=env,
                      echo=True)
        else:
            shell.run(['git', 'clone', '--recursive', remote, repo_name] +
                      (['--no-tags'] if skip_tags else []),
                      env=env,
                      echo=True)
        if scheme_name:
            src_path = os.path.join(args.source_root, repo_name, ".git")
            shell.run(['git', '--git-dir',
                       src_path, '--work-tree',
                       os.path.join(args.source_root, repo_name),
                       'checkout', repo_branch],
                      env=env,
                      echo=False)
    with shell.pushd(os.path.join(args.source_root, repo_name),
                     dry_run=False, echo=False):
        shell.run(["git", "submodule",
                   "update", "--recursive"],
                  env=env,
                  echo=False)


def obtain_all_additional_swift_sources(args, config, with_ssh, scheme_name,
                                        skip_history, skip_tags,
                                        skip_repository_list):

    pool_args = []
    with shell.pushd(args.source_root, dry_run=False, echo=False):
        for repo_name, repo_info in config['repos'].items():
            if repo_name in skip_repository_list:
                print("Skipping clone of '" + repo_name + "', requested by "
                      "user")
                continue

            if os.path.isdir(os.path.join(repo_name, ".git")):
                print("Skipping clone of '" + repo_name + "', directory "
                      "already exists")
                continue

            # If we have a url override, use that url instead of
            # interpolating.
            remote_repo_info = repo_info['remote']
            if 'url' in remote_repo_info:
                remote = remote_repo_info['url']
            else:
                remote_repo_id = remote_repo_info['id']
                if with_ssh is True or 'https-clone-pattern' not in config:
                    remote = config['ssh-clone-pattern'] % remote_repo_id
                else:
                    remote = config['https-clone-pattern'] % remote_repo_id

            repo_branch = None
            repo_not_in_scheme = False
            if scheme_name:
                for v in config['branch-schemes'].values():
                    if scheme_name not in v['aliases']:
                        continue
                    # If repo is not specified in the scheme, skip cloning it.
                    if repo_name not in v['repos']:
                        repo_not_in_scheme = True
                        continue
                    repo_branch = v['repos'][repo_name]
                    break
                else:
                    repo_branch = scheme_name
            if repo_not_in_scheme:
                continue

            pool_args.append([args, repo_name, repo_info, repo_branch, remote,
                              with_ssh, scheme_name, skip_history, skip_tags,
                              skip_repository_list])

    if not pool_args:
        print("Not cloning any repositories.")
        return

    return run_parallel(
        obtain_additional_swift_sources, pool_args, args.n_processes)


def dump_repo_hashes(args, config, branch_scheme_name='repro'):
    """
    Dumps the current state of the repo into a new config file that contains a
    main branch scheme with the relevant branches set to the appropriate
    hashes.
    """
    new_config = {}
    config_copy_keys = ['ssh-clone-pattern', 'https-clone-pattern', 'repos']
    for config_copy_key in config_copy_keys:
        new_config[config_copy_key] = config[config_copy_key]
    repos = {}
    repos = repo_hashes(args, config)
    branch_scheme = {'aliases': [branch_scheme_name], 'repos': repos}
    new_config['branch-schemes'] = {branch_scheme_name: branch_scheme}
    json.dump(new_config, sys.stdout, indent=4)


def repo_hashes(args, config):
    repos = {}
    for repo_name, repo_info in sorted(config['repos'].items(),
                                       key=lambda x: x[0]):
        repo_path = os.path.join(args.source_root, repo_name)
        if os.path.exists(repo_path):
            with shell.pushd(repo_path, dry_run=False, echo=False):
                h = shell.capture(["git", "rev-parse", "HEAD"],
                                  echo=False).strip()
        else:
            h = 'skip'
        repos[repo_name] = str(h)
    return repos


def print_repo_hashes(args, config):
    repos = repo_hashes(args, config)
    for repo_name, repo_hash in sorted(repos.items(),
                                       key=lambda x: x[0]):
        print("{:<35}: {:<35}".format(repo_name, repo_hash))


def validate_config(config):
    # Make sure that our branch-names are unique.
    scheme_names = config['branch-schemes'].keys()
    if len(scheme_names) != len(set(scheme_names)):
        raise RuntimeError('Configuration file has duplicate schemes?!')

    # Ensure the branch-scheme name is also an alias
    # This guarantees sensible behavior of update_repository_to_scheme when
    # the branch-scheme is passed as the scheme name
    for scheme_name in config['branch-schemes'].keys():
        if scheme_name not in config['branch-schemes'][scheme_name]['aliases']:
            raise RuntimeError('branch-scheme name: "{0}" must be an alias '
                               'too.'.format(scheme_name))

    # Then make sure the alias names used by our branches are unique.
    seen = dict()
    for (scheme_name, scheme) in config['branch-schemes'].items():
        aliases = scheme['aliases']
        for alias in aliases:
            if alias in seen:
                raise RuntimeError('Configuration file defines the alias {0} '
                                   'in both the {1} scheme and the {2} scheme?!'
                                   .format(alias, seen[alias], scheme_name))
            else:
                seen[alias] = scheme_name


def full_target_name(repository, target):
    tag = shell.capture(["git", "tag", "-l", target], echo=True).strip()
    if tag == target:
        return tag

    branch = shell.capture(["git", "branch", "--list", target],
                           echo=True).strip().replace("* ", "")
    if branch == target:
        name = "%s/%s" % (repository, target)
        return name

    raise RuntimeError('Cannot determine if %s is a branch or a tag' % target)


def skip_list_for_platform(config):
    # If there is a platforms key only include the repo if the
    # plaform is in the list
    skip_list = []
    platform_name = platform.system()

    for repo_name, repo_info in config['repos'].items():
        if 'platforms' in repo_info:
            if platform_name not in repo_info['platforms']:
                print("Skipping", repo_name, "on", platform_name)
                skip_list.append(repo_name)
            else:
                print("Including", repo_name, "on", platform_name)

    return skip_list


def main():
    freeze_support()
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="""
By default, updates your checkouts of Swift, SourceKit, LLDB, and SwiftPM
repositories.
""")
    parser.add_argument(
        "--clone",
        help="Obtain Sources for Swift and Related Projects",
        action="store_true")
    parser.add_argument(
        "--clone-with-ssh",
        help="Obtain Sources for Swift and Related Projects via SSH",
        action="store_true")
    parser.add_argument(
        "--skip-history",
        help="Skip histories when obtaining sources",
        action="store_true")
    parser.add_argument(
        "--skip-tags",
        help="Skip tags when obtaining sources",
        action="store_true")
    parser.add_argument(
        "--skip-repository",
        metavar="DIRECTORY",
        default=[],
        help="Skip the specified repository",
        dest='skip_repository_list',
        action="append")
    parser.add_argument(
        "--scheme",
        help='Use branches from the specified branch-scheme. A "branch-scheme"'
        ' is a list of (repo, branch) pairs.',
        metavar='BRANCH-SCHEME',
        dest='scheme')
    parser.add_argument(
        '--reset-to-remote',
        help='Reset each branch to the remote state.',
        action='store_true')
    parser.add_argument(
        '--clean',
        help='Clean unrelated files from each repository.',
        action='store_true')
    parser.add_argument(
        "--config",
        default=os.path.join(SCRIPT_DIR, os.pardir,
                             "update-checkout-config.json"),
        help="Configuration file to use")
    parser.add_argument(
        "--github-comment",
        help="""Check out related pull requests referenced in the given
        free-form GitHub-style comment.""",
        metavar='GITHUB-COMMENT',
        dest='github_comment')
    parser.add_argument(
        '--dump-hashes',
        action='store_true',
        help='Dump the git hashes of all repositories being tracked')
    parser.add_argument(
        '--dump-hashes-config',
        help='Dump the git hashes of all repositories packaged into '
             'update-checkout-config.json',
        metavar='BRANCH-SCHEME-NAME')
    parser.add_argument(
        "--tag",
        help="""Check out each repository to the specified tag.""",
        metavar='TAG-NAME')
    parser.add_argument(
        "--match-timestamp",
        help='Check out adjacent repositories to match timestamp of '
        ' current swift checkout.',
        action='store_true')
    parser.add_argument(
        "-j", "--jobs",
        type=int,
        help="Number of threads to run at once",
        default=0,
        dest="n_processes")
    parser.add_argument(
        "--source-root",
        help="The root directory to checkout repositories",
        default=SWIFT_SOURCE_ROOT,
        dest='source_root')
    args = parser.parse_args()

    if not args.scheme:
        if args.reset_to_remote:
            print("update-checkout usage error: --reset-to-remote must "
                  "specify --scheme=foo")
            sys.exit(1)
        if args.match_timestamp:
            # without a scheme, we won't be able match timestamps forward in
            # time, which is an annoying footgun for bisection etc.
            print("update-checkout usage error: --match-timestamp must "
                  "specify --scheme=foo")
            sys.exit(1)

    clone = args.clone
    clone_with_ssh = args.clone_with_ssh
    skip_history = args.skip_history
    skip_tags = args.skip_tags
    scheme = args.scheme
    github_comment = args.github_comment

    with open(args.config) as f:
        config = json.load(f)
    validate_config(config)

    if args.dump_hashes:
        dump_repo_hashes(args, config)
        return (None, None)

    if args.dump_hashes_config:
        dump_repo_hashes(args, config, args.dump_hashes_config)
        return (None, None)

    cross_repos_pr = {}
    if github_comment:
        regex_pr = r'(apple/[-a-zA-Z0-9_]+/pull/\d+|apple/[-a-zA-Z0-9_]+#\d+)'
        repos_with_pr = re.findall(regex_pr, github_comment)
        print("Found related pull requests:", str(repos_with_pr))
        repos_with_pr = [pr.replace('/pull/', '#') for pr in repos_with_pr]
        cross_repos_pr = dict(pr.split('#') for pr in repos_with_pr)

    clone_results = None
    if clone or clone_with_ssh:
        # If branch is None, default to using the default branch alias
        # specified by our configuration file.
        if scheme is None:
            scheme = config['default-branch-scheme']

        skip_repo_list = skip_list_for_platform(config)
        skip_repo_list.extend(args.skip_repository_list)
        clone_results = obtain_all_additional_swift_sources(args, config,
                                                            clone_with_ssh,
                                                            scheme,
                                                            skip_history,
                                                            skip_tags,
                                                            skip_repo_list)

    # Quick check whether somebody is calling update in an empty directory
    directory_contents = os.listdir(args.source_root)
    if not ('cmark' in directory_contents or
            'llvm' in directory_contents or
            'clang' in directory_contents):
        print("You don't have all swift sources. "
              "Call this script with --clone to get them.")

    update_results = update_all_repositories(args, config, scheme,
                                             cross_repos_pr)
    fail_count = 0
    fail_count += check_parallel_results(clone_results, "CLONE")
    fail_count += check_parallel_results(update_results, "UPDATE")
    if fail_count > 0:
        print("update-checkout failed, fix errors and try again")
    else:
        print("update-checkout succeeded")
        print_repo_hashes(args, config)
    sys.exit(fail_count)
