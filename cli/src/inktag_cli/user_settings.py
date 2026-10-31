"""User settings shared by the reorganized CLI; existing v1 wire formats only."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Annotated

import typer

from .generated_commands_v2 import CapabilityV2, CommandV2, NfcCommandV2
from .protocol import encode_input_action, input_action_mask
from .nfc_command import NfcCommandRecord, encode_command_text, decode_capabilities
from .aux_io import encode_aux_configuration
from .pin_config import PUBLIC_PIN_ROLES, user_pin_configuration, validate_pin_update, describe_pin_report


def core():
    from . import app
    return app


def read_setting(context: typer.Context, field: str):
    app = core(); options = app._context(context)
    async def operation():
        async with options.client(auto_sync_time=False) as client:
            return getattr(client.snapshot, field)
    app._emit(options, app._run(options, operation()))


def ble_name_show(context: typer.Context):
    """Show the Bluetooth broadcast name used to find this device."""
    read_setting(context, 'device_name')


def device_name_show(context: typer.Context):
    """Show the name printed on the device information page."""
    read_setting(context, 'device_label')


def settings_show(context: typer.Context):
    """Show the device's current reported settings and editable capabilities."""
    app = core(); options = app._context(context)
    async def operation():
        async with options.client(auto_sync_time=False) as client:
            value = client.snapshot.to_dict()
            return {key: value.get(key) for key in ('bluetoothName', 'deviceName', 'screen', 'settingsCapabilities')}
    app._emit(options, app._run(options, operation()))


def _pins_view(report, aux):
    result = {}
    if report:
        effective = report['effective']; low = effective['activeLow']; pins = effective['pins']
        result.update(screen={'pins': {r: pins[r] for r in PUBLIC_PIN_ROLES[:7]}, 'enableActiveLow': bool(low & 64)},
                      led={'pins': {r: pins[r] for r in PUBLIC_PIN_ROLES[7:]},
                           'activeLow': {r: bool(low & (1 << PUBLIC_PIN_ROLES.index(r))) for r in PUBLIC_PIN_ROLES[7:]}})
    if aux:
        result.update(buttons=aux['buttons'][:2], buzzer={
            'mode': aux['buzzerMode'], 'gpioA': aux['buzzerGpioA'], 'gpioB': aux['buzzerGpioB'],
            'frequencyHz': aux['buzzerFrequencyHz']})
    return result


async def _read_pins(client):
    report = await client.get_pin_config() if client.supports_capability(CapabilityV2.PIN_CONFIG) else None
    aux = await client.get_aux_configuration() if client.supports_capability(CapabilityV2.IO | CapabilityV2.PIN_CONFIG) else None
    return report, aux


def validate_pin_sections(values):
    """Validate file structure before any BLE connection or partial save."""
    if not isinstance(values,dict) or not values or set(values)-{'screen','led','buttons','buzzer'}:
        raise ValueError('Use screen, led, buttons and/or buzzer sections')
    disabled={'gpio':255,'activeLow':False,'pull':'none'}
    try:
        for section,roles in [('screen',PUBLIC_PIN_ROLES[:7]),('led',PUBLIC_PIN_ROLES[7:])]:
            if section not in values:continue
            value=values[section]
            expected={'pins','enableActiveLow'} if section=='screen' else {'pins','activeLow'}
            if set(value)!=expected or set(value['pins'])!=set(roles):
                raise ValueError(f'{section} requires the complete named pin section from pins show')
            if any(type(pin) is not int or not 0<=pin<=255 for pin in value['pins'].values()):
                raise ValueError('GPIO values must be integers 0..255')
            if section=='screen' and type(value['enableActiveLow']) is not bool:
                raise ValueError('enableActiveLow must be boolean')
            if section=='led' and (set(value['activeLow'])!=set(roles) or any(type(v) is not bool for v in value['activeLow'].values())):
                raise ValueError('Provide boolean red/green/blue activeLow values')
        buttons=values.get('buttons',[])
        if not isinstance(buttons,list) or len(buttons)>2:
            raise ValueError('Configure at most two physical buttons')
        buttons=[*buttons,*[disabled.copy() for _ in range(3-len(buttons))]]
        buzzer=values.get('buzzer',{'mode':'off','gpioA':255,'gpioB':255,'frequencyHz':0})
        if set(buzzer)!={'mode','gpioA','gpioB','frequencyHz'}:
            raise ValueError('Buzzer requires mode, gpioA, gpioB and frequencyHz')
        encode_aux_configuration(dict(version=1,buttons=buttons,buzzerMode=buzzer['mode'],
            buzzerGpioA=buzzer['gpioA'],buzzerGpioB=buzzer['gpioB'],buzzerFrequencyHz=buzzer['frequencyHz']))
    except (KeyError,TypeError,AttributeError) as error:
        raise ValueError('Invalid pin section; use the configuration returned by pins show') from error


def pins_show(context: typer.Context):
    """Show effective screen/LED pins, two buttons and buzzer configuration."""
    app = core(); options = app._context(context)
    async def operation():
        async with options.client(auto_sync_time=False) as client:
            report, aux = await _read_pins(client)
            return {'configuration': _pins_view(report, aux), 'pinReport': report,
                    'display': describe_pin_report(report,client.snapshot.board_id,getattr(client.snapshot,'screen',None)) if report else None,
                    'pinsSupported': report is not None,
                    'auxSupported': aux is not None, 'source': 'device-readback'}
    app._emit(options, app._run(options, operation()))


def pins_set(context: typer.Context,
             config: Annotated[Path, typer.Option(exists=True, dir_okay=False, help='JSON sections: screen, led, buttons (up to two), buzzer. Omitted sections are preserved.')],
             yes: Annotated[bool, typer.Option('--yes', '-y')] = False):
    """Save checked GPIO configuration. Separate device writes are reported individually."""
    app = core(); options = app._context(context)
    try:
        values = json.loads(config.read_text(encoding='utf-8'))
        validate_pin_sections(values)
    except (ValueError, OSError) as error:
        raise typer.BadParameter(str(error), param_hint='--config') from error
    app._require_confirmation(options, yes, 'Apply the checked screen, LED, button or buzzer pins?')
    async def operation():
        async with options.client() as client:
            report, aux = await _read_pins(client)
            view = _pins_view(report, aux)
            if any(key not in view for key in values):
                raise ValueError('The device does not support one of the requested pin sections')
            caps = client.snapshot.settings_capabilities
            if 'buttons' in values and not caps.custom_button_gpio:
                raise ValueError('Button pins are read-only on this device')
            if 'buzzer' in values and not caps.custom_buzzer_gpio:
                raise ValueError('Buzzer pins are read-only on this device')
            view.update(values)
            routing = None
            if 'screen' in values or 'led' in values:
                pins = {**view['screen']['pins'], **view['led']['pins']}
                low = int(view['screen'].get('enableActiveLow', False)) << 6
                for index, role in enumerate(PUBLIC_PIN_ROLES[7:], 7):
                    low |= int(view['led'].get('activeLow', {}).get(role, False)) << index
                routing = user_pin_configuration({'pins': pins, 'activeLow': low})
                validate_pin_update(routing, report)
            if 'buttons' in values or 'buzzer' in values:
                buttons = view['buttons']
                if not isinstance(buttons, list) or len(buttons) > 2:
                    raise ValueError('Configure at most two physical buttons')
                disabled = {'gpio': 255, 'activeLow': False, 'pull': 'none'}
                buttons = [*buttons, *[disabled.copy() for _ in range(2-len(buttons))], aux['buttons'][2]]
                buzzer = view['buzzer']
                aux = dict(version=1, buttons=buttons, buzzerMode=buzzer['mode'],
                           buzzerGpioA=buzzer['gpioA'], buzzerGpioB=buzzer['gpioB'],
                           buzzerFrequencyHz=buzzer['frequencyHz'])
                encode_aux_configuration(aux)
            planned = routing or (report['effective'] if report else None)
            if planned and aux:
                active = [p for p in planned['pins'].values() if p != 255]
                active += [b['gpio'] for b in aux['buttons'] if b['gpio'] != 255]
                active += [p for p in (aux['buzzerGpioA'], aux['buzzerGpioB']) if p != 255]
                if len(active) != len(set(active)):
                    raise ValueError('Screen, LED, button and buzzer GPIOs must not overlap')
            committed = []
            try:
                if routing:
                    report = await client.set_pin_config(routing); committed.append('screen-led')
                if 'buttons' in values or 'buzzer' in values:
                    aux = await client.set_aux_configuration(aux); committed.append('buttons-buzzer')
            except Exception as error:
                raise ValueError(f'Pin save failed or unconfirmed; confirmed sections: {committed}. Read current pins before retrying. {error}') from error
            return {'confirmedSections': committed, 'configuration': _pins_view(report, aux)}
    app._emit(options, app._run(options, operation()))


def button_show(context: typer.Context):
    """Read the shared single/double/long-press actions for both buttons."""
    app=core(); options=app._context(context)
    async def operation():
        async with options.client(auto_sync_time=False) as client:
            payload=await client.request_v2(CommandV2.GET_INPUT_CONFIG)
            if len(payload) not in (10,14) or payload[0] != 1:
                raise ValueError('Invalid input configuration')
            from .protocol import _INPUT_ACTION_IDS
            names={value:key for key,value in _INPUT_ACTION_IDS.items()}
            return {'sharedByAllButtons': True, **{gesture:{'action':names.get(payload[offset],'unknown'), 'slot':payload[offset+1]}
                    for gesture,offset in [('single',4),('double',6),('long',8)]}}
    app._emit(options, app._run(options,operation()))


def button_set(context: typer.Context,
               gesture: Annotated[str, typer.Argument(help='single, double or long')],
               action: Annotated[str, typer.Argument(help='next, previous, slot, calendar, clock, device-info, slideshow, stop, clear-previous or disabled')],
               slot: Annotated[int, typer.Option(min=0,max=254)] = 0):
    """Set one gesture shared by both physical buttons."""
    app=core(); options=app._context(context)
    actions={**app._INPUT_ACTIONS,'previous':'previousPage','clear-previous':'toggleClearPrevious'}
    if gesture not in ('single','double','long') or action not in actions:
        raise typer.BadParameter('Unsupported gesture or action')
    async def operation():
        async with options.client() as client:
            caps=client.snapshot.settings_capabilities
            if not caps.button_gesture_mask & (1 << ('single','double','long').index(gesture)) or not caps.button_action_mask & input_action_mask(actions[action]):
                raise ValueError('The device does not support this button gesture/action')
            await client.request_v2(CommandV2.SET_INPUT_CONFIG,encode_input_action(gesture,actions[action],slot_id=slot))
            payload=await client.request_v2(CommandV2.GET_INPUT_CONFIG)
            return {'sharedByAllButtons':True,'configuration':list(payload)}
    app._emit(options,app._run(options,operation()))


def nfc_usage(context: typer.Context):
    """Show this device's NFC instructions and supported phone commands. Read-only."""
    app=core(); options=app._context(context)
    async def operation():
        async with options.client(auto_sync_time=False) as client:
            if not client.supports_capability(CapabilityV2.NFC):
                return {'supported':False,'usage':'This device does not support NFC.'}
            caps=decode_capabilities(await client.request_v2(CommandV2.GET_NFC_CAPABILITIES))
            commands=[name.lower().replace('_','-') for name,code in NfcCommandV2.__members__.items()
                      if caps['flags'] & 0x40 and caps['commandMask'] & (1 << int(code))]
            return {'supported':True,'usage':'Read the tag with your phone to open its device link. The first Bluetooth connection requires browser permission.',
                    'phoneCommands':commands,'commandHelp':
                    'Use NFC Tools: Write > Add a record > Text. Use device nfc commands to generate the text. Phone commands require Pro.'
                    if commands else 'This firmware does not advertise phone text commands.'}
    app._emit(options,app._run(options,operation()))


def nfc_commands(context: typer.Context,
                 command: Annotated[str | None, typer.Argument(help='Optional command to encode for an NFC text record.')] = None,
                 slot: Annotated[int, typer.Option(min=0,max=254)] = 0):
    """List NFC text commands or generate text to write with an NFC app. Offline."""
    commands={name.lower().replace('_','-'):int(value) for name,value in NfcCommandV2.__members__.items()}
    if command is None:
        core()._emit(core()._context(context),{'commands':list(commands),'usage':'Write the generated text as an NFC text record using an NFC writing app; supported commands depend on the device.'})
        return
    if command not in commands:
        raise typer.BadParameter('Choose a command from nfc commands')
    payload=bytes((slot,)) if command=='show-slot' else b''
    typer.echo(encode_command_text(NfcCommandRecord(0,commands[command],payload)))
