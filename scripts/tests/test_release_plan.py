import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error


SPEC = importlib.util.spec_from_file_location(
    'release_plan', Path(__file__).resolve().parents[1] / 'plan-release.py'
)
planner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(planner)


def native_release(version='0.1.1'):
    return {'tag_name': f'v{version}', 'assets': [
        {'name': f'slang-slim-native-v{version}-{target}.zip'}
        for target in planner.TARGETS
    ]}


class ReleasePlanningTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        key_patch = patch.object(planner, 'native_source_key', return_value='test-native-inputs')
        key_patch.start()
        self.addCleanup(key_patch.stop)
        self.write_versions('0.1.2')

    def write_versions(self, version, *, safe_version=None, sys_requirement=None):
        (self.root / 'Cargo.toml').write_text(
            '[workspace]\nresolver = "3"\nmembers = ["crates/slang-slim", "crates/slang-slim-sys"]\n'
            f'[workspace.package]\nversion = "{version}"\n'
            '[workspace.dependencies]\n'
            'slang-slim-sys = { path = "crates/slang-slim-sys", '
            f'version = "{sys_requirement or version}" }}\n', encoding='utf-8',
        )
        for package in planner.CRATES:
            member = self.root / 'crates' / package
            (member / 'src').mkdir(parents=True, exist_ok=True)
            (member / 'src' / 'lib.rs').touch()
            manifest = f'[package]\nname = "{package}"\nversion.workspace = true\n'
            if package == 'slang-slim':
                if safe_version:
                    manifest = f'[package]\nname = "{package}"\nversion = "{safe_version}"\n'
                manifest += '[dependencies]\nslang-slim-sys.workspace = true\n'
            (member / 'Cargo.toml').write_text(manifest, encoding='utf-8')

    def read_metadata(self, url):
        if '/releases?' in url:
            return [native_release()]
        if '/releases/tags/v0.1.1' in url:
            return native_release()
        if '/slang-slim-sys/0.1.1' in url or '/slang-slim/0.1.1' in url:
            return {'version': {'num': '0.1.1'}}
        return None

    def plan(self, tag=None, compare=None, read_json=None, ancestor=None):
        return planner.plan(self.root, 'owner/repository', tag,
                            read_json=read_json or self.read_metadata,
                            compare=compare or (lambda *args: False),
                            ancestor=ancestor or (lambda *args: True))

    def test_rust_only_release_repackages_old_binaries_without_compilation(self):
        result = self.plan('v0.1.2')
        self.assertFalse(result['build_native'])
        self.assertEqual(result['native_action'], 'repackage')
        self.assertEqual(result['source_version'], '0.1.1')
        self.assertEqual(result['native_version'], '0.1.2')
        self.assertTrue(result['publish_native'])

    def test_native_changes_trigger_compilation_under_the_same_tag_rule(self):
        result = self.plan('v0.1.2', compare=lambda *args: True)
        self.assertTrue(result['build_native'])
        self.assertEqual(result['native_action'], 'build')
        self.assertTrue(result['publish_native'])

    def test_existing_version_reuses_its_published_archives(self):
        self.write_versions('0.1.1')
        result = self.plan('v0.1.1')
        self.assertEqual(result['native_action'], 'reuse')
        self.assertFalse(result['build_native'])
        self.assertFalse(result['publish_native'])

    def test_publish_job_checks_for_the_exact_crate_version(self):
        for package in planner.CRATES:
            with self.subTest(package=package):
                self.assertFalse(planner.crate_needs_publish(package, '0.1.1', self.read_metadata))
                self.assertTrue(planner.crate_needs_publish(package, '0.1.2', self.read_metadata))

    def test_missing_local_baseline_triggers_compilation(self):
        result = self.plan('v0.1.2', compare=lambda *args: None)
        self.assertEqual(result['native_action'], 'build')

    def test_first_release_compiles_without_a_published_baseline(self):
        result = self.plan('v0.1.2', read_json=lambda url: [] if '/releases?' in url else None)
        self.assertTrue(result['build_native'])
        self.assertTrue(result['publish_native'])

    def test_missing_or_unrelated_git_tags_cannot_supply_old_binaries(self):
        result = self.plan('v0.1.2', ancestor=lambda *args: False)
        self.assertEqual(result['native_action'], 'build')
        self.assertEqual(result['source_version'], '')

    def test_incomplete_and_draft_baselines_are_skipped(self):
        incomplete = native_release('0.1.0')
        incomplete['assets'].pop()
        draft = dict(native_release('0.1.3'), draft=True)
        def metadata(url):
            return [incomplete, draft, native_release()] if '/releases?' in url else None
        result = self.plan('v0.1.2', read_json=metadata)
        self.assertEqual(result['source_version'], '0.1.1')
        self.assertEqual(result['native_action'], 'repackage')

    def test_partial_current_release_can_repackage_from_a_complete_previous_release(self):
        partial = native_release('0.1.2')
        partial['assets'].pop()
        def metadata(url):
            if '/releases/tags/v0.1.2' in url:
                return partial
            return self.read_metadata(url)
        result = self.plan('v0.1.2', read_json=metadata)
        self.assertEqual(result['native_action'], 'repackage')
        self.assertTrue(result['publish_native'])

    def test_native_baseline_search_paginates(self):
        def metadata(url):
            if url.endswith('&page=1'):
                return [{'tag_name': f'unrelated-{n}'} for n in range(100)]
            return [native_release()]
        version = planner.previous_native_version(self.root, 'owner/repository', '0.1.2',
                                                  metadata, lambda *args: True)
        self.assertEqual(version, '0.1.1')

    def test_force_build_bypasses_reuse_and_repackaging(self):
        for version, release in (('0.1.1', native_release()), ('0.1.2', None)):
            with self.subTest(version=version):
                result = planner.native_decision(version, release, '0.1.1', False, True)
                self.assertTrue(result['build_native'])
                self.assertEqual(result['native_action'], 'build')

    def test_invalid_tags_are_rejected_before_network_requests(self):
        def no_network(url):
            raise AssertionError('Invalid tags must not query remote state')
        for tag in ('v0.1.3', 'slang-slim-v0.1.2', 'v0.1.2-extra/branch', 'latest'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                self.plan(tag, read_json=no_network)

    def test_crate_version_override_is_rejected_with_or_without_a_tag(self):
        self.write_versions('0.1.1', safe_version='0.1.2')
        for tag in (None, 'v0.1.2'):
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, 'Both crate versions must match'):
                self.plan(tag)

    def test_stale_sys_dependency_requirement_is_rejected_without_a_tag(self):
        self.write_versions('0.1.2', sys_requirement='0.1.1')
        with self.assertRaisesRegex(ValueError, r'must declare slang-slim-sys version 0\.1\.2'):
            self.plan()

    def test_draft_current_release_still_requires_native_publication(self):
        draft = dict(native_release('0.1.2'), draft=True)

        def metadata(url):
            if '/releases/tags/v0.1.2' in url:
                return draft
            return self.read_metadata(url)

        result = self.plan('v0.1.2', read_json=metadata)
        self.assertEqual(result['native_action'], 'repackage')
        self.assertEqual(result['source_version'], '0.1.1')
        self.assertTrue(result['publish_native'])

    def test_git_comparison_handles_head_and_missing_baseline(self):
        root = Path(__file__).resolve().parents[2]
        self.assertFalse(planner.changed_since(root, 'HEAD', planner.NATIVE_INPUTS))
        self.assertTrue(planner.is_ancestor(root, 'HEAD'))
        self.assertIsNone(planner.changed_since(root, 'refs/tags/does-not-exist-for-release-test', planner.NATIVE_INPUTS))

    def test_http_errors_are_not_treated_as_missing_releases(self):
        for status in (401, 403, 429, 500):
            with self.subTest(status=status), patch.object(planner.urllib.request, 'urlopen') as request:
                request.side_effect = urllib.error.HTTPError('https://example.invalid', status, 'failed', {}, None)
                with self.assertRaisesRegex(RuntimeError, f'HTTP {status}'):
                    planner.get_json('https://api.github.com/repos/owner/repository/releases/tags/v0.1.1')

    def test_http_not_found_is_a_missing_release(self):
        with patch.object(planner.urllib.request, 'urlopen') as request:
            request.side_effect = urllib.error.HTTPError('https://example.invalid', 404, 'missing', {}, None)
            self.assertIsNone(planner.get_json('https://api.github.com/repos/owner/repository/releases/tags/v0.1.1'))

    def test_github_token_is_never_sent_to_crates_io(self):
        with patch.dict(planner.os.environ, {'GH_TOKEN': 'test-token'}), \
                patch.object(planner.urllib.request, 'urlopen') as request:
            request.side_effect = urllib.error.HTTPError('https://example.invalid', 404, 'missing', {}, None)
            planner.get_json('https://crates.io/api/v1/crates/slang-slim/0.1.2')
            self.assertNotIn('Authorization', request.call_args.args[0].headers)


if __name__ == '__main__':
    unittest.main()
