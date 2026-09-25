import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

fixtures = module('parity_fixtures', ROOT / 'tools/parity_fixtures.py')
capture = module('capture_simulator', ROOT / 'engine/platform/ios/verify/capture_simulator.py')

class FixtureAuditTests(unittest.TestCase):
    def test_record_preserves_external_resource_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.gltf'
            path.write_text(json.dumps({'buffers': [{'uri': 'data:application/octet-stream;base64,AAAA'}]}))
            record = fixtures.digerir(path)
            fixtures.gravar(path, record)
            loaded, _ = fixtures.carregar(path)
            self.assertEqual(loaded['externalResources'], 0)
            self.assertEqual(capture.validate_face_fixture(path, loaded), loaded['sha256'])

    def test_missing_or_tampered_audit_is_rejected(self):
        path = ROOT / 'docs/parity/fixtures/metal-face-control.gltf'
        record, _ = fixtures.carregar(path)
        capture.validate_face_fixture(path, record)
        for changed in ({k: v for k, v in record.items() if k != 'externalResources'},
                        dict(record, sha256='0' * 64), dict(record, externalResources=1)):
            with self.assertRaises(RuntimeError):
                capture.validate_face_fixture(path, changed)

    def test_external_uri_cannot_be_hidden_by_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.gltf'
            path.write_text(json.dumps({'buffers': [{'uri': 'missing.bin'}], 'images': [{'uri': 'missing.png'}]}))
            record = fixtures.digerir(path)
            self.assertEqual(record['externalResources'], 2)
            for count in (0, 2):
                with self.assertRaises(RuntimeError):
                    capture.validate_face_fixture(path, dict(record, externalResources=count))

    def test_both_real_fixture_audits_pass_with_original_bytes(self):
        for name in ('metal-face-control', 'metal-face-culling'):
            path = ROOT / 'docs/parity/fixtures' / (name + '.gltf')
            record, _ = fixtures.carregar(path)
            self.assertEqual(capture.validate_face_fixture(path, record), record['sha256'])

if __name__ == '__main__':
    unittest.main()
