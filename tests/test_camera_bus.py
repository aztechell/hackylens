from pathlib import Path
import os, shutil, subprocess, sys, tempfile
import pytest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from board_contract import load_board
from gen_board import write_board_config

@pytest.mark.parametrize('board', ['huskylens-sen0305','sipeed-maix-cube'])
def test_camera_control_schema_and_legacy_defaults_are_unambiguous(board):
    import copy
    from board_contract import validate_board_data, ContractError
    original = load_board(board)
    from gen_board import render_board_config
    generated = render_board_config(original)
    assert 'CAMERA_SCCB_ADDR' not in generated and 'CAMERA_SCCB_HZ' not in generated
    for field, value in [('camera_sccb_address',0x60),('camera_sccb_hz',100000)]:
        data = copy.deepcopy(original.data)
        data['defaults'][field] = value
        with pytest.raises(ContractError): validate_board_data(data)
    control = original.data['camera_control']
    invalid = [('address_7bit',0),('address_7bit',0x80),('address_7bit',True),('backend','unknown')]
    invalid += [('clock_hz',100000),('controller',2),('scl_low_count',0),('scl_high_count',256)] \
        if control['backend']=='dvp-sccb' else [('scl_low_count',255),('scl_high_count',255),('controller',3),('clock_hz',0)]
    for field, value in invalid:
        data = copy.deepcopy(original.data)
        data['camera_control'][field] = value
        with pytest.raises(ContractError): validate_board_data(data)
    data = copy.deepcopy(original.data)
    clock = next(r for r in data['routes'] if r['macro']=='IO_CAM_SCCB_SCLK')
    clock['function'] = 'i2c1-sclk'
    clock['peripheral'] = 'i2c1'
    with pytest.raises(ContractError): validate_board_data(data)
@pytest.mark.parametrize('backend,board', [('dvp','huskylens-sen0305'),('i2c','sipeed-maix-cube')])
def test_real_camera_bus_mmio(backend, board):
    cc = os.environ.get('CC') or shutil.which('gcc')
    assert cc
    with tempfile.TemporaryDirectory(prefix='camera-bus-') as temp:
        out = Path(temp)
        write_board_config(load_board(board), out/'board_config.h')
        includes = [out, ROOT/'platforms/k210/hal', ROOT/'_deps/kendryte-standalone-sdk/lib/drivers/include', ROOT/'_deps/kendryte-standalone-sdk/lib/bsp/include']
        exe = out/'bus.exe'
        subprocess.run([cc, '-std=c11','-Wall','-Wextra','-Werror',*(f'-I{x}' for x in includes),str(ROOT/f'tests/camera_bus_{backend}_harness.c'),'-o',str(exe)],check=True)
        result = subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
        assert result.returncode == 0, result.stdout+result.stderr
        assert f'CAMERA_BUS_{backend.upper()}_OK' in result.stdout


@pytest.mark.parametrize('board,sensor', [('sipeed-maix-cube','ov7740'), ('huskylens-sen0305','ov2640')])
def test_sensor_sequences_stop_at_first_bus_error(board, sensor):
    cc = os.environ.get('CC') or shutil.which('gcc')
    assert cc
    with tempfile.TemporaryDirectory(prefix='camera-sensor-') as temp:
        out = Path(temp)
        write_board_config(load_board(board), out/'board_config.h')
        includes = [out, ROOT/'firmware/include', ROOT/'firmware/src/drivers',
                    ROOT/'firmware/src/internal', ROOT/'platforms/k210/hal']
        exe = out/'sensor.exe'
        sources = [ROOT/'tests/camera_sensor_sequence_harness.c',
                   ROOT/'firmware/src/drivers/camera_sensor.c',
                   ROOT/f'firmware/src/drivers/{sensor}_sensor.c']
        subprocess.run([cc, '-std=c11','-Wall','-Wextra','-Werror',
                        *(f'-I{x}' for x in includes),*(str(x) for x in sources),
                        '-o',str(exe)],check=True)
        result = subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
        assert result.returncode == 0, result.stdout+result.stderr
        assert 'CAMERA_SENSOR_SEQUENCE_OK' in result.stdout


@pytest.mark.parametrize('board,backend', [('sipeed-maix-cube','i2c'), ('huskylens-sen0305','dvp')])
@pytest.mark.parametrize('camera_enabled', [True, False])
def test_build_stages_exactly_one_or_no_camera_transport(tmp_path, board, backend, camera_enabled):
    import build_firmware, service_bindings
    board = load_board(board)
    disabled = set() if camera_enabled else {'camera','qr-camera','face-detect','apriltag','object-detect'}
    selection = service_bindings.select(board, disabled, set(), set())
    build_firmware.stage_platform_sources(tmp_path, disabled, selection)
    staged = tmp_path/'platforms/k210/hal'
    for candidate in ('dvp','i2c'):
        assert (staged/f'hal_camera_bus_{candidate}.c').exists() == (camera_enabled and candidate == backend)
    assert (staged/'hal_dvp.c').exists() == camera_enabled
