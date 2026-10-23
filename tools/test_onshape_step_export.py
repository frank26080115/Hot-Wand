"""Offline checks for configured Onshape exports. Run with unittest discovery."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock

import requests

from onshape_step_export import OnshapeExportError, download_onshape_step


URL = (
    "https://cad.onshape.com/documents/cfeb2fa9b83109508c6ff5e8"
    "/w/69b4ef0849f116bf37eeaccd/e/969a8f9c953454fe4aa374bb"
)
ELEMENT_ID = URL.rsplit("/", 1)[1]
PARAMETER = {
    "btType": "BTMConfigurationParameterEnum-105",
    "parameterId": "List_example",
    "options": [
        {"optionName": "16mm", "option": "Default"},
        {"optionName": "18mm", "option": "_19mm"},
    ],
}
TRANSLATION = {
    "id": "translation-id",
    "requestState": "DONE",
    "resultExternalDataIds": ["result-id"],
}


def response(body):
    result = requests.Response()
    result.status_code = 200
    result._content = json.dumps(body).encode()
    return result


def session_with(*extra_responses):
    session = Mock(spec=requests.Session)
    session.request.side_effect = [
        response({}),
        response([{"id": ELEMENT_ID, "elementType": "PARTSTUDIO", "name": "Stand"}]),
        *extra_responses,
    ]
    return session


class ConfigurationExportTests(unittest.TestCase):
    def export(self, session, **kwargs):
        with tempfile.TemporaryDirectory() as directory:
            path = download_onshape_step(
                URL, Path(directory) / "stand.step",
                access_key="test-access", secret_key="test-secret",
                session=session, **kwargs,
            )
            return path.read_bytes()

    def test_display_names_resolve_to_internal_ids(self):
        for name, option in (("16mm", "Default"), ("18mm", "_19mm")):
            with self.subTest(name=name):
                encoded_id = f"List_example={option}"
                download = Mock(spec=requests.Response)
                download.ok = True
                download.headers = {"Content-Type": "application/octet-stream"}
                download.iter_content.return_value = [b"ISO-10303-21;\n"]
                session = session_with(
                    response({"configurationParameters": [PARAMETER]}),
                    response({"encodedId": encoded_id}),
                    response(TRANSLATION), download,
                )
                self.assertEqual(self.export(session, configuration=name), b"ISO-10303-21;\n")
                calls = session.request.call_args_list
                self.assertEqual(calls[3].kwargs["json"], {
                    "parameters": [{"parameterId": "List_example", "parameterValue": option}],
                })
                self.assertTrue(calls[4].args[1].endswith("/translations"))
                self.assertEqual(calls[4].kwargs["json"], {
                    "grouping": True, "notifyUser": False, "storeInDocument": False,
                    "formatName": "STEP", "configuration": encoded_id,
                })

    def test_unknown_name_fails_before_export(self):
        session = session_with(response({"configurationParameters": [PARAMETER]}))
        with self.assertRaisesRegex(OnshapeExportError, "Available options: '16mm', '18mm'"):
            self.export(session, configuration="20mm")
        self.assertTrue(all(call.args[0] == "GET" for call in session.request.call_args_list))

    def test_unsupported_inputs_fail_before_export(self):
        for parameters in ([], [PARAMETER, PARAMETER], [{"btType": "BTMConfigurationParameterBoolean-2550"}]):
            with self.subTest(parameters=parameters):
                session = session_with(response({"configurationParameters": parameters}))
                with self.assertRaisesRegex(OnshapeExportError, "exactly one list input"):
                    self.export(session, configuration="18mm")
                self.assertTrue(all(call.args[0] == "GET" for call in session.request.call_args_list))

    def test_missing_encoding_fails_before_export(self):
        session = session_with(
            response({"configurationParameters": [PARAMETER]}), response({}),
        )
        with self.assertRaisesRegex(OnshapeExportError, "encoded configuration ID"):
            self.export(session, configuration="18mm")
        self.assertEqual(session.request.call_count, 4)

    def test_default_export_keeps_existing_endpoint(self):
        download = Mock(spec=requests.Response)
        download.ok = True
        download.headers = {"Content-Type": "application/octet-stream"}
        download.iter_content.return_value = [b"default STEP"]
        session = session_with(response(TRANSLATION), download)
        self.assertEqual(self.export(session), b"default STEP")
        export_call = session.request.call_args_list[2]
        self.assertTrue(export_call.args[1].endswith("/export/step"))
        self.assertNotIn("configuration", export_call.kwargs["json"])


if __name__ == "__main__":
    unittest.main()
