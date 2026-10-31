"""Local Find service user commands; account secrets never enter arguments."""
from __future__ import annotations

import getpass
from typing import Annotated
import typer
from .wait_status import pause_waiting


def request(context, path, body=None):
    from . import app
    from .local_service import local_service_request
    try:
        payload=local_service_request(None, '/api/v1/find/'+path, body)
    except ValueError as error:
        app._emit_error(context.obj,str(error)); raise typer.Exit(1) from None
    app._emit(context.obj,payload)
    return payload


def status(context: typer.Context):
    """Show local Find account/session and identity status; no Apple query."""
    request(context,'status')


def login(context: typer.Context,
          email: Annotated[str, typer.Option(prompt=True, help='Apple account email.')],
          consent: Annotated[bool,typer.Option('--accept-account-risk',help='Confirm that you have read the privacy/account notice and accept account risk.')] = False):
    """Log in locally. The password is entered in a hidden prompt."""
    with pause_waiting():
        if not consent and not typer.confirm('The local service uses Apple unofficial interfaces. Have you read the privacy/account notice and accepted the account risk?'):
            raise typer.Abort()
        password = getpass.getpass('Apple account password: ')
    request(context,'login',{'email':email,'password':password,'consent':True})


def logout(context: typer.Context):
    """Log out of Apple; retain local Find identities and location history."""
    request(context,'logout',{})


def request_code(context: typer.Context,
                 method: Annotated[int,typer.Option(help='Method index from findmy status.')]):
    """Request a verification code using a listed trusted phone/device method."""
    request(context,'2fa/request',{'index':method})


def verify(context: typer.Context,
           method: Annotated[int,typer.Option(help='Method index from findmy status.')] = -1):
    """Submit a verification code from a hidden prompt."""
    with pause_waiting():
        code = getpass.getpass('Verification code: ')
    request(context,'2fa/submit',{'index':method,'code':code})


def tracking_set(context: typer.Context,
                 fingerprint: Annotated[str,typer.Argument(help='Local Find identity fingerprint.')],
                 enabled: Annotated[bool,typer.Option('--enabled/--disabled')],
                 interval: Annotated[int,typer.Option(min=1,help='Tracking interval in minutes.')] = 15):
    """Enable or disable scheduled location queries for one identity."""
    request(context,'tracking/configure',{'fingerprint':fingerprint,'enabled':enabled,'intervalMinutes':interval})


def history(context: typer.Context,
            fingerprint: Annotated[str,typer.Argument(help='Local Find identity fingerprint.')],
            limit: Annotated[int,typer.Option(min=1,max=1000)] = 250,
            hours: Annotated[int,typer.Option(min=1,max=168)] = 24):
    """Read local saved location history; does not query Apple."""
    request(context,'tracking/history',{'fingerprint':fingerprint,'limit':limit,'hours':hours})
