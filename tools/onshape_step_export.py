#!/usr/bin/env python3
"""Download a Part Studio or Assembly from Onshape as a STEP file.

The public entry point is :func:`download_onshape_step`, which can be imported
by other Python code.  When run as a script, pass an Onshape workspace element
URL and, optionally, an output file or existing output directory.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Mapping
from urllib.parse import urlparse

import requests


ACCESS_KEY_ENV = "ONSHAPE_API_ACCESS_KEY"
SECRET_KEY_ENV = "ONSHAPE_API_SECRET_KEY"
DEFAULT_EXPORT_TIMEOUT = 600.0
DEFAULT_POLL_INTERVAL = 2.0
DEFAULT_REQUEST_TIMEOUT = 60.0
API_VERSION = "v16"


class OnshapeExportError(RuntimeError):
    """Raised when an Onshape STEP export cannot be completed."""


def parse_onshape_url(document_url: str) -> tuple[str, str, str, str]:
    """Return ``(origin, document_id, workspace_id, element_id)``.

    Expected URL form::

        https://cad.onshape.com/documents/{did}/w/{wid}/e/{eid}

    The origin is retained so the same function also works with an Onshape
    Enterprise domain, provided the API keys were created on that domain.
    """

    parsed = urlparse(document_url)
    path_parts = [part for part in parsed.path.split("/") if part]

    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or not (
            parsed.hostname == "cad.onshape.com"
            or parsed.hostname.endswith(".onshape.com")
        )
        or len(path_parts) != 6
        or path_parts[0] != "documents"
        or path_parts[2] != "w"
        or path_parts[4] != "e"
    ):
        raise ValueError(
            "Expected an Onshape workspace element URL like "
            "https://cad.onshape.com/documents/{did}/w/{wid}/e/{eid}"
        )

    did, wid, eid = path_parts[1], path_parts[3], path_parts[5]
    id_pattern = re.compile(r"^[0-9a-fA-F]{24}$")
    if not all(id_pattern.fullmatch(value) for value in (did, wid, eid)):
        raise ValueError(
            "Onshape document, workspace, and element IDs must be 24 hex characters"
        )

    origin = f"{parsed.scheme}://{parsed.hostname}"
    return origin, did, wid, eid


def _credentials_from_environment() -> tuple[str, str]:
    try:
        access_key = os.environ[ACCESS_KEY_ENV]
        secret_key = os.environ[SECRET_KEY_ENV]
    except KeyError as exc:
        raise OnshapeExportError(
            f"Required environment variable {exc.args[0]} is not set"
        ) from None

    if not access_key or not secret_key:
        raise OnshapeExportError(
            "Onshape API credential environment variables must not be empty"
        )
    return access_key, secret_key


def _response_error_detail(response: requests.Response) -> str:
    try:
        body = response.json()
    except ValueError:
        body = response.text.strip()

    if isinstance(body, Mapping):
        detail = body.get("message") or body.get("error") or body.get("moreInfo")
        if detail:
            return str(detail)
        detail = str(dict(body))
    else:
        detail = str(body)

    detail = " ".join(detail.split())
    return detail[:500] if detail else response.reason


def _request(
    session: requests.Session,
    method: str,
    url: str,
    *,
    request_timeout: float,
    **kwargs: Any,
) -> requests.Response:
    try:
        response = session.request(method, url, timeout=request_timeout, **kwargs)
    except requests.RequestException as exc:
        raise OnshapeExportError(f"Onshape request failed: {exc}") from exc

    if not response.ok:
        detail = _response_error_detail(response)
        raise OnshapeExportError(
            f"Onshape API returned HTTP {response.status_code} for {method} {url}: {detail}"
        )
    return response


def _json_object(response: requests.Response, description: str) -> dict[str, Any]:
    try:
        value = response.json()
    except ValueError as exc:
        raise OnshapeExportError(f"Onshape returned invalid JSON while {description}") from exc
    if not isinstance(value, dict):
        raise OnshapeExportError(f"Onshape returned unexpected JSON while {description}")
    return value


def _safe_step_filename(name: str, fallback: str) -> str:
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name).strip().rstrip(". ")
    if not name:
        name = fallback

    # Keep generated names usable on Windows as well as POSIX systems.
    suffix = ".stp" if name.lower().endswith(".stp") else ".step"
    stem = name[: -len(suffix)] if name.lower().endswith(suffix) else name

    reserved = {"CON", "PRN", "AUX", "NUL"}
    reserved.update(f"COM{i}" for i in range(1, 10))
    reserved.update(f"LPT{i}" for i in range(1, 10))
    if stem.upper() in reserved:
        stem = f"_{stem}"

    return stem[: 240 - len(suffix)] + suffix


def _resolve_output_path(save_location: os.PathLike[str] | str | None, filename: str) -> Path:
    if save_location is None:
        return Path.cwd() / filename

    requested = Path(save_location).expanduser()
    if requested.is_dir():
        return requested / filename
    return requested


def _find_element(elements: Any, element_id: str) -> dict[str, Any]:
    if not isinstance(elements, list):
        raise OnshapeExportError("Onshape returned an unexpected element list")
    for element in elements:
        if isinstance(element, dict) and element.get("id") == element_id:
            return element
    raise OnshapeExportError(
        f"Element {element_id} is not present in the specified document workspace"
    )


def _wait_for_translation(
    session: requests.Session,
    api_base: str,
    initial_status: dict[str, Any],
    *,
    export_timeout: float,
    poll_interval: float,
    request_timeout: float,
) -> dict[str, Any]:
    translation_id = initial_status.get("id")
    if not translation_id:
        raise OnshapeExportError("Onshape did not return a translation ID")

    deadline = time.monotonic() + export_timeout
    status = initial_status
    while True:
        state = str(status.get("requestState", "")).upper()
        if state == "DONE":
            return status
        if state == "FAILED":
            reason = status.get("failureReason") or "no failure reason was supplied"
            raise OnshapeExportError(f"STEP translation failed: {reason}")
        if state and state != "ACTIVE":
            raise OnshapeExportError(f"Onshape returned unknown translation state {state!r}")

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise OnshapeExportError(
                f"STEP translation did not finish within {export_timeout:g} seconds"
            )
        time.sleep(min(poll_interval, remaining))

        response = _request(
            session,
            "GET",
            f"{api_base}/translations/{translation_id}",
            request_timeout=request_timeout,
            headers={"Accept": "application/json;charset=UTF-8"},
        )
        status = _json_object(response, "checking STEP translation status")


def _translation_download_url(
    api_base: str,
    status: Mapping[str, Any],
    document_id: str,
    workspace_id: str,
) -> str:
    external_ids = status.get("resultExternalDataIds") or []
    if external_ids:
        if not isinstance(external_ids, list) or len(external_ids) != 1:
            raise OnshapeExportError("STEP translation produced an unexpected number of files")
        result_document_id = status.get("resultDocumentId") or document_id
        return f"{api_base}/documents/d/{result_document_id}/externaldata/{external_ids[0]}"

    # This fallback also supports servers that store the result as a temporary
    # blob element despite storeInDocument=False.
    element_ids = status.get("resultElementIds") or []
    if element_ids:
        if not isinstance(element_ids, list) or len(element_ids) != 1:
            raise OnshapeExportError("STEP translation produced an unexpected number of files")
        result_document_id = status.get("resultDocumentId") or document_id
        result_workspace_id = status.get("resultWorkspaceId") or workspace_id
        return (
            f"{api_base}/blobelements/d/{result_document_id}/w/"
            f"{result_workspace_id}/e/{element_ids[0]}"
        )

    raise OnshapeExportError("Completed STEP translation did not contain a downloadable result")


def _download_to_file(
    session: requests.Session,
    url: str,
    output_path: Path,
    *,
    request_timeout: float,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    response = _request(
        session,
        "GET",
        url,
        request_timeout=request_timeout,
        headers={"Accept": "application/octet-stream"},
        stream=True,
    )

    content_type = response.headers.get("Content-Type", "").lower()
    if "application/json" in content_type:
        raise OnshapeExportError(
            "Onshape returned JSON instead of the translated STEP file: "
            + _response_error_detail(response)
        )

    temporary_name: str | None = None
    bytes_written = 0
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=f".{output_path.name}.",
            suffix=".part",
            dir=output_path.parent,
            delete=False,
        ) as temporary_file:
            temporary_name = temporary_file.name
            for chunk in response.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    temporary_file.write(chunk)
                    bytes_written += len(chunk)

        if bytes_written == 0:
            raise OnshapeExportError("Onshape returned an empty STEP file")
        os.replace(temporary_name, output_path)
        temporary_name = None
    except OSError as exc:
        raise OnshapeExportError(f"Could not save STEP file to {output_path}: {exc}") from exc
    finally:
        response.close()
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass


def download_onshape_step(
    document_url: str,
    save_location: os.PathLike[str] | str | None = None,
    *,
    access_key: str | None = None,
    secret_key: str | None = None,
    export_timeout: float = DEFAULT_EXPORT_TIMEOUT,
    poll_interval: float = DEFAULT_POLL_INTERVAL,
    request_timeout: float = DEFAULT_REQUEST_TIMEOUT,
    session: requests.Session | None = None,
) -> Path:
    """Export an Onshape workspace element and return the saved STEP path.

    ``document_url`` must identify a Part Studio or Assembly in a workspace.
    If ``save_location`` is omitted, the element name is used in the current
    directory.  An existing directory receives that generated filename;
    anything else is treated as the exact destination file path.

    Credentials default to ``ONSHAPE_API_ACCESS_KEY`` and
    ``ONSHAPE_API_SECRET_KEY``.  Explicit credentials are supported primarily
    for callers that already manage secrets outside this module.
    """

    if export_timeout <= 0 or poll_interval <= 0 or request_timeout <= 0:
        raise ValueError("Timeouts and polling interval must be greater than zero")

    origin, did, wid, eid = parse_onshape_url(document_url)
    if access_key is None and secret_key is None:
        access_key, secret_key = _credentials_from_environment()
    elif not access_key or not secret_key:
        raise OnshapeExportError("Both access_key and secret_key must be supplied together")

    owns_session = session is None
    api_session = session or requests.Session()
    api_session.auth = (access_key, secret_key)
    api_base = f"{origin}/api/{API_VERSION}"

    try:
        # Deliberately validate document access before inspecting the workspace
        # or starting a billable asynchronous translation.
        document_response = _request(
            api_session,
            "GET",
            f"{api_base}/documents/{did}",
            request_timeout=request_timeout,
            headers={"Accept": "application/json;charset=UTF-8"},
        )
        _json_object(document_response, "validating document access")

        elements_response = _request(
            api_session,
            "GET",
            f"{api_base}/documents/d/{did}/w/{wid}/elements",
            request_timeout=request_timeout,
            headers={"Accept": "application/json;charset=UTF-8"},
        )
        try:
            elements = elements_response.json()
        except ValueError as exc:
            raise OnshapeExportError(
                "Onshape returned invalid JSON while reading elements"
            ) from exc

        element = _find_element(elements, eid)
        element_type = str(element.get("elementType", "")).upper()
        endpoint_prefix = {
            "PARTSTUDIO": "partstudios",
            "ASSEMBLY": "assemblies",
        }.get(element_type)
        if endpoint_prefix is None:
            raise OnshapeExportError(
                f"Element {element.get('name', eid)!r} is {element_type or 'an unknown type'}, "
                "not a Part Studio or Assembly"
            )

        default_filename = _safe_step_filename(str(element.get("name") or ""), eid)
        output_path = _resolve_output_path(save_location, default_filename).resolve()

        export_response = _request(
            api_session,
            "POST",
            f"{api_base}/{endpoint_prefix}/d/{did}/w/{wid}/e/{eid}/export/step",
            request_timeout=request_timeout,
            headers={
                "Accept": "application/json;charset=UTF-8",
                "Content-Type": "application/json;charset=UTF-8",
            },
            json={
                "grouping": True,
                "notifyUser": False,
                "storeInDocument": False,
            },
        )
        initial_status = _json_object(export_response, "starting STEP translation")
        final_status = _wait_for_translation(
            api_session,
            api_base,
            initial_status,
            export_timeout=export_timeout,
            poll_interval=poll_interval,
            request_timeout=request_timeout,
        )
        download_url = _translation_download_url(api_base, final_status, did, wid)
        _download_to_file(
            api_session,
            download_url,
            output_path,
            request_timeout=request_timeout,
        )
        return output_path
    finally:
        if owns_session:
            api_session.close()


def _build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Export an Onshape Part Studio or Assembly as a STEP file."
    )
    parser.add_argument(
        "url",
        help="Onshape URL: https://cad.onshape.com/documents/{did}/w/{wid}/e/{eid}",
    )
    parser.add_argument(
        "save_location",
        nargs="?",
        help=(
            "output file or existing directory; defaults to the element name "
            "in the current directory"
        ),
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_argument_parser().parse_args(argv)
    try:
        output_path = download_onshape_step(args.url, args.save_location)
    except (OnshapeExportError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
